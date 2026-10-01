/*
 * Wi-Fi STA 与 SoftAP 配网 adapter。
 *
 * 启动时优先读取 NVS 并连接 STA；没有配置或连续连接失败时开启 APSTA，提供
 * 一个最小配置页。网页只保存配置并重启，连接状态仍由同一事件处理路径建立。
 * 实时媒体要求关闭 Wi-Fi power save，避免 KCP 音视频排队和抖动。
 */
#include "wifi_manager.h"
#include "wifi_captive_dns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define WIFI_NVS_SSID "ssid"
#define WIFI_NVS_PASSWORD "password"
#define WIFI_CONNECT_RETRIES 5
#define WIFI_SETUP_URL "http://192.168.6.1"
#define WIFI_SETUP_IP_A 192
#define WIFI_SETUP_IP_B 168
#define WIFI_SETUP_IP_C 6
#define WIFI_SETUP_IP_D 1

static const char *TAG = "wifi_manager";
static bool s_started;
static volatile bool s_connected;
static volatile bool s_provisioning;
static int s_retry_count;
static char s_provisioning_ssid[33];
static httpd_handle_t s_http_server;

/* 页面内嵌在固件中，避免模板依赖额外文件系统分区。 */
static const char s_setup_page[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>小钛 Wi-Fi 配置</title><style>body{font-family:sans-serif;max-width:420px;"
    "margin:40px auto;padding:0 18px}input,button{box-sizing:border-box;width:100%;"
    "padding:12px;margin:7px 0;font-size:16px}#msg{white-space:pre-wrap}</style>"
    "<h2>小钛设备配网</h2><p>填写设备需要连接的 Wi-Fi。</p>"
    "<input id=s placeholder='Wi-Fi 名称' maxlength=32>"
    "<input id=p type=password placeholder='Wi-Fi 密码（开放网络可留空）' maxlength=64>"
    "<button onclick=save()>保存并重启</button><p id=msg></p>"
    "<script>async function save(){let m=document.getElementById('msg');m.textContent='保存中…';"
    "try{let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s.value,password:p.value})});m.textContent=await r.text()}"
    "catch(e){m.textContent='请求失败：'+e}}</script></html>";

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

bool wifi_manager_credentials_valid(const char *ssid,
                                    const char *password,
                                    char *error,
                                    size_t error_size)
{
    if (ssid == NULL || password == NULL) {
        set_error(error, error_size, "SSID/password is null");
        return false;
    }
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > WIFI_MANAGER_SSID_MAX) {
        set_error(error, error_size, "SSID length must be 1..32 bytes");
        return false;
    }
    if (password_length != 0 && (password_length < 8 ||
                                 password_length > WIFI_MANAGER_PASSWORD_MAX)) {
        set_error(error, error_size, "password length must be 0 or 8..64 bytes");
        return false;
    }
    set_error(error, error_size, "");
    return true;
}

esp_err_t wifi_manager_load_credentials(wifi_manager_credentials_t *credentials)
{
    if (credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(credentials, 0, sizeof(*credentials));
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    /* SSID/password 必须成组读取；任一失败都清空输出。 */
    size_t ssid_size = sizeof(credentials->ssid);
    size_t password_size = sizeof(credentials->password);
    err = nvs_get_str(nvs, WIFI_NVS_SSID, credentials->ssid, &ssid_size);
    if (err == ESP_OK) {
        err = nvs_get_str(nvs, WIFI_NVS_PASSWORD, credentials->password, &password_size);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        memset(credentials, 0, sizeof(*credentials));
    }
    return err;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    char validation_error[80];
    if (!wifi_manager_credentials_valid(ssid,
                                        password,
                                        validation_error,
                                        sizeof(validation_error))) {
        ESP_LOGE(TAG, "invalid Wi-Fi credentials: %s", validation_error);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_NVS_SSID, ssid);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, WIFI_NVS_PASSWORD, password);
    }
    /* commit 成功后新配置才对下一次启动可见。 */
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (nvs != 0) {
        nvs_close(nvs);
    }
    return err;
}

esp_err_t wifi_manager_forget_credentials(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    if (nvs != 0) {
        nvs_close(nvs);
    }
    return err;
}

static void restart_task(void *argument)
{
    /* 先给 HTTP 响应留出发送时间，再让正常启动路径应用新配置。 */
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t setup_page_get(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, s_setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_config_post(httpd_req_t *request)
{
    /* 请求体有硬上限且分段读取，防止配置 HTTP 任务被无界占用。 */
    if (request->content_len <= 0 || request->content_len > 512) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid request size");
    }
    char body[513];
    size_t total = 0;
    unsigned receive_timeouts = 0;
    while (total < (size_t)request->content_len) {
        int received = httpd_req_recv(request,
                                      body + total,
                                      (size_t)request->content_len - total);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++receive_timeouts >= 3U) {
                return httpd_resp_send_err(request,
                                           HTTPD_408_REQ_TIMEOUT,
                                           "request body timed out");
            }
            continue;
        }
        if (received <= 0) {
            return httpd_resp_send_err(request,
                                       HTTPD_400_BAD_REQUEST,
                                       "cannot read request");
        }
        receive_timeouts = 0;
        total += (size_t)received;
    }
    body[total] = '\0';

    cJSON *root = cJSON_ParseWithLength(body, total);
    const cJSON *ssid = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
        cJSON_Delete(root);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "ssid/password required");
    }

    char validation_error[80];
    if (!wifi_manager_credentials_valid(ssid->valuestring,
                                        password->valuestring,
                                        validation_error,
                                        sizeof(validation_error))) {
        cJSON_Delete(root);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, validation_error);
    }
    esp_err_t err = wifi_manager_save_credentials(ssid->valuestring, password->valuestring);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
    }

    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    esp_err_t response = httpd_resp_sendstr(request, "保存成功，设备将在 1 秒后重启。");
    if (response == ESP_OK) {
        (void)xTaskCreate(restart_task, "wifi_restart", 2048, NULL, 3, NULL);
    }
    return response;
}

static esp_err_t captive_portal_redirect(httpd_req_t *request,
                                         httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", WIFI_SETUP_URL);
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(request, "打开 TiRTC 配网页面");
}

static esp_err_t start_http_server(void)
{
    if (s_http_server != NULL) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) {
        s_http_server = NULL;
        ESP_LOGE(TAG, "cannot start provisioning HTTP server");
        return err;
    }
    const httpd_uri_t page = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = setup_page_get,
    };
    const httpd_uri_t api = {
        .uri = "/api/wifi",
        .method = HTTP_POST,
        .handler = wifi_config_post,
    };
    err = httpd_register_uri_handler(s_http_server, &page);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &api);
    }
    if (err == ESP_OK) {
        err = httpd_register_err_handler(s_http_server,
                                         HTTPD_404_NOT_FOUND,
                                         captive_portal_redirect);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot register provisioning HTTP handlers: %s",
                 esp_err_to_name(err));
        (void)httpd_stop(s_http_server);
        s_http_server = NULL;
    }
    return err;
}

static esp_err_t configure_provisioning_netif(esp_netif_t *ap_netif)
{
    if (ap_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_netif_dhcp_status_t dhcp_status = ESP_NETIF_DHCP_INIT;
    esp_err_t err = esp_netif_dhcps_get_status(ap_netif, &dhcp_status);
    if (err != ESP_OK) {
        return err;
    }
    /* A newly created AP is INIT, but set_ip_info requires STOPPED. */
    if (dhcp_status != ESP_NETIF_DHCP_STOPPED) {
        err = esp_netif_dhcps_stop(ap_netif);
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_netif_ip_info_t ip_info = {0};
    esp_netif_set_ip4_addr(&ip_info.ip, WIFI_SETUP_IP_A, WIFI_SETUP_IP_B,
                           WIFI_SETUP_IP_C, WIFI_SETUP_IP_D);
    esp_netif_set_ip4_addr(&ip_info.gw, WIFI_SETUP_IP_A, WIFI_SETUP_IP_B,
                           WIFI_SETUP_IP_C, WIFI_SETUP_IP_D);
    esp_netif_set_ip4_addr(&ip_info.netmask, 255, 255, 255, 0);
    err = esp_netif_set_ip_info(ap_netif, &ip_info);
    /*
     * Do not advertise DHCP option 114 here. RFC 8910 requires that URI to be
     * an RFC 8908 HTTPS/JSON API endpoint; this provisioning server is a local
     * HTTP page. Legacy captive-portal probes are handled by wildcard DNS and
     * the HTTP redirect instead.
     */
    if (err != ESP_OK) {
        return err;
    }
    return esp_netif_dhcps_start(ap_netif);
}

static void start_provisioning(void)
{
    if (s_provisioning) {
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    (void)snprintf(s_provisioning_ssid,
                   sizeof(s_provisioning_ssid),
                   "XiaoTai-%02X%02X",
                   mac[4],
                   mac[5]);

    /* AP 名称后缀来自 STA MAC，方便同时调试多块开发板。 */
    wifi_config_t ap = {0};
    size_t ap_ssid_length = strlen(s_provisioning_ssid);
    memcpy(ap.ap.ssid, s_provisioning_ssid, ap_ssid_length);
    ap.ap.ssid_len = ap_ssid_length;
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    s_provisioning = true;
    esp_err_t err = start_http_server();
    if (err == ESP_OK) {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t ip_info = {0};
        if (ap_netif == NULL) {
            err = ESP_ERR_NOT_FOUND;
        } else {
            err = esp_netif_get_ip_info(ap_netif, &ip_info);
        }
        if (err == ESP_OK) {
            err = wifi_captive_dns_start(ip_info.ip.addr);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cannot start captive portal DNS: %s",
                     esp_err_to_name(err));
        }
    }
    ESP_LOGW(TAG,
             "provisioning active: connect open SSID=%s; captive portal=%s",
             s_provisioning_ssid, WIFI_SETUP_URL);
}

static void wifi_event(void *argument,
                       esp_event_base_t event_base,
                       int32_t event_id,
                       void *event_data)
{
    /* 所有连接/重试/降级到 SoftAP 的转换都集中在 ESP-IDF 网络事件中。 */
    (void)argument;
    (void)event_data;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_manager_credentials_t credentials;
        if (wifi_manager_load_credentials(&credentials) == ESP_OK) {
            (void)esp_wifi_connect();
        } else {
            start_provisioning();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (!s_provisioning && s_retry_count++ < WIFI_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d",
                     s_retry_count, WIFI_CONNECT_RETRIES);
            (void)esp_wifi_connect();
        } else {
            start_provisioning();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        s_connected = true;
        s_retry_count = 0;
        ESP_LOGI(TAG, "Wi-Fi connected, IP=" IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
}

esp_err_t wifi_manager_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (sta_netif == NULL || ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    err = configure_provisioning_netif(ap_netif);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot configure provisioning address: %s",
                 esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    /* 凭证只由 wifi_cfg 命名空间管理，避免 Wi-Fi 驱动复用旧的 Flash STA 配置。 */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    /* 启动前先决定 STA 还是 APSTA；STA_START 事件负责真正 connect。 */
    wifi_manager_credentials_t credentials;
    if (wifi_manager_load_credentials(&credentials) == ESP_OK) {
        wifi_config_t station = {0};
        memcpy(station.sta.ssid, credentials.ssid, strlen(credentials.ssid));
        memcpy(station.sta.password, credentials.password, strlen(credentials.password));
        station.sta.threshold.authmode = credentials.password[0] == '\0'
                                             ? WIFI_AUTH_OPEN
                                             : WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
        ESP_LOGI(TAG, "connecting to configured SSID=%s", credentials.ssid);
    } else {
        /* 驱动初始化可能已从旧 NVS 载入 STA 参数；无本机凭证时必须清空。 */
        wifi_config_t station = {0};
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }
    /* 实时媒体需要稳定时延，明确关闭默认省电。 */
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot disable Wi-Fi power save: %s",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Wi-Fi power save disabled for real-time media");
    s_started = true;
    return ESP_OK;
}

bool wifi_manager_connected(void)
{
    return s_connected;
}

bool wifi_manager_provisioning(void)
{
    return s_provisioning;
}

const char *wifi_manager_provisioning_ssid(void)
{
    return s_provisioning ? s_provisioning_ssid : "";
}
