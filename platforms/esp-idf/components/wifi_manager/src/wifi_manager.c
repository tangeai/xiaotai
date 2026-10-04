/*
 * Wi-Fi STA 与 SoftAP 配网 adapter。
 *
 * 启动时优先读取 NVS 并连接 STA；没有配置或连续连接失败时开启 APSTA，提供
 * 产品化的扫描/选择配置页。网页只保存配置并重启，连接状态仍由同一事件处理路径建立。
 * 实时媒体要求关闭 Wi-Fi power save，避免 KCP 音视频排队和抖动。
 */
#include "wifi_manager.h"
#include "captive_dns.h"

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

static const char *TAG = "wifi_manager";
static bool s_started;
static volatile bool s_connected;
/* RSSI is informational. Hosted queries may block on C6 RPC, so only this
 * background worker reads the hardware; UI consumers read a cached snapshot. */
#define WIFI_SIGNAL_POLL_MS 30000U
static TaskHandle_t s_signal_task;
static portMUX_TYPE s_signal_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_signal_epoch;
static uint32_t s_signal_revision;
static int s_cached_rssi = 1; /* positive sentinel: no sample */

static void signal_connection_changed(void)
{
    taskENTER_CRITICAL(&s_signal_lock);
    ++s_signal_epoch;
    ++s_signal_revision;
    s_cached_rssi = 1;
    taskEXIT_CRITICAL(&s_signal_lock);
    if (s_signal_task != NULL) xTaskNotifyGive(s_signal_task);
}

static void signal_task(void *context)
{
    (void)context;
    for (;;) {
        taskENTER_CRITICAL(&s_signal_lock);
        uint32_t epoch = s_signal_epoch;
        taskEXIT_CRITICAL(&s_signal_lock);
        wifi_ap_record_t access_point = {0};
        if (s_connected && esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
            taskENTER_CRITICAL(&s_signal_lock);
            if (s_connected && epoch == s_signal_epoch && s_cached_rssi != access_point.rssi) {
                s_cached_rssi = access_point.rssi;
                ++s_signal_revision;
            }
            taskEXIT_CRITICAL(&s_signal_lock);
        }
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WIFI_SIGNAL_POLL_MS));
    }
}
static volatile bool s_provisioning;
static volatile bool s_connection_failed;
static bool s_has_saved_credentials;
static int s_retry_count;
static char s_provisioning_ssid[33];
static char s_provisioning_status[96];
static httpd_handle_t s_http_server;
static esp_netif_t *s_ap_netif;

/* 页面内嵌在固件中，避免模板依赖额外文件系统分区。 */
static const char s_setup_page[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>小钛联网助手</title><style>*{box-sizing:border-box}body{margin:0;background:#f4f7f8;"
    "color:#16313b;font-family:-apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif}"
    ".hero{padding:34px 22px 54px;color:#fff;background:linear-gradient(145deg,#103742,#17606c)}"
    ".brand{font-size:25px;font-weight:800}.sub{margin-top:8px;color:#c9e4e7}.card{margin:-28px 16px 94px;"
    "padding:20px;border-radius:20px;background:#fff;box-shadow:0 12px 34px #173b4930}h1{font-size:21px;margin:0 0 6px}"
    ".hint{margin:0 0 17px;color:#72848b;font-size:14px}.wifi{display:flex;width:100%;align-items:center;"
    "gap:12px;padding:14px 4px;border:0;border-bottom:1px solid #e7edef;background:#fff;text-align:left;font-size:16px}"
    ".wifi b{flex:1;overflow:hidden;text-overflow:ellipsis}.lock{color:#759097}.notice{margin:18px 0 8px;"
    "padding:13px;border-radius:12px;background:#edf7f6;color:#537078;font-size:13px;line-height:1.55}"
    ".link{width:100%;padding:12px;border:0;background:transparent;color:#178271;font-size:15px}"
    ".back{padding:0;border:0;background:transparent;color:#178271;font-size:15px}.chosen{margin:18px 0;"
    "padding:14px;border-radius:12px;background:#f1f7f7;font-weight:700}label{display:block;color:#536b74;font-size:14px}"
    ".password{display:flex;margin-top:7px;border:1px solid #d6e0e3;border-radius:12px;overflow:hidden}"
    "input{min-width:0;flex:1;padding:14px;border:0;outline:0;font-size:17px}.show{width:72px;border:0;background:#fff;color:#178271}"
    ".footer{position:fixed;left:0;right:0;bottom:0;padding:12px 16px calc(12px + env(safe-area-inset-bottom));"
    "background:#fff;border-top:1px solid #e1e8ea}.primary{width:100%;padding:15px;border:0;border-radius:13px;"
    "background:#168b74;color:#fff;font-size:17px;font-weight:700}.primary:disabled{background:#b7c7c7}"
    ".empty{padding:28px 0;text-align:center;color:#7b8c92}.spin{display:inline-block;width:18px;height:18px;"
    "border:2px solid #bdd2d2;border-top-color:#178271;border-radius:50%;animation:r .8s linear infinite}"
    "@keyframes r{to{transform:rotate(360deg)}}[hidden]{display:none!important}</style><body>"
    "<header class=hero><div class=brand>小钛联网助手</div><div class=sub>让设备连接家里的 Wi-Fi</div></header>"
    "<main class=card><section id=list><h1>选择家庭 Wi-Fi</h1><p class=hint>请选择设备要连接的 2.4 GHz 网络</p>"
    "<div id=networks class=empty><span class=spin></span><p>正在搜索附近网络…</p></div>"
    "<div class=notice>手机当前连接的是小钛热点，显示“无互联网连接”属于正常现象。配网完成后设备会自动重启。</div>"
    "<button class=link onclick=scan()>重新扫描</button><button class=link onclick=manualWifi()>手动输入网络名称</button></section>"
    "<section id=detail hidden><button class=back onclick=chooseAgain()>‹ 重新选择 Wi-Fi</button>"
    "<h1 style='margin-top:18px'>输入 Wi-Fi 密码</h1><p class=hint>确认网络名称后输入密码</p>"
    "<div class=chosen id=chosen></div><label>Wi-Fi 密码<div class=password>"
    "<input id=p type=password maxlength=64 autocomplete=current-password placeholder='开放网络可留空'>"
    "<button class=show type=button onclick=togglePassword(this)>显示</button></div></label>"
    "<p class=hint style='margin-top:10px'>密码只保存在小钛设备中。</p></section></main>"
    "<footer id=footer class=footer hidden><button id=connect class=primary onclick=save()>连接此 Wi-Fi</button></footer>"
    "<script>let selected='',secured=false;function esc(v){let d=document.createElement('div');d.textContent=v;return d.innerHTML}"
    "async function scan(){let n=document.getElementById('networks');n.className='empty';n.innerHTML='<span class=spin></span><p>正在搜索附近网络…</p>';"
    "try{let r=await fetch('/api/wifi/scan',{cache:'no-store'}),a=await r.json();if(!r.ok)throw Error('扫描失败');"
    "if(!a.length){n.innerHTML='<p>未找到网络，请靠近路由器后重试</p>';return}n.className='';let seen={};"
    "n.innerHTML=a.filter(x=>x.ssid&&!seen[x.ssid]&&(seen[x.ssid]=1)).map(x=>'<button class=wifi data-ssid=\"'+encodeURIComponent(x.ssid)+'\" data-secure=\"'+x.secure+'\"><span>⌁</span><b>'+esc(x.ssid)+'</b><span class=lock>'+(x.secure?'锁':'开放')+'</span></button>').join('');"
    "n.querySelectorAll('.wifi').forEach(b=>b.onclick=()=>selectWifi(decodeURIComponent(b.dataset.ssid),b.dataset.secure==='true'))}"
    "catch(e){n.innerHTML='<p>扫描失败，请点击重新扫描</p>'}}"
    "function selectWifi(ssid,secure){selected=ssid;secured=secure;chosen.textContent=ssid;list.hidden=true;detail.hidden=false;footer.hidden=false;"
    "p.value='';p.placeholder=secure?'请输入 Wi-Fi 密码':'开放网络可留空';connect.disabled=secure;p.oninput=()=>connect.disabled=secured&&p.value.length<8}"
    "function chooseAgain(){detail.hidden=true;footer.hidden=true;list.hidden=false}"
    "function manualWifi(){let ssid=prompt('请输入家庭 Wi-Fi 名称');if(ssid&&ssid.trim())selectWifi(ssid.trim(),true)}"
    "function togglePassword(button){p.type=p.type==='password'?'text':'password';button.textContent=p.type==='password'?'显示':'隐藏'}"
    "async function save(){connect.disabled=true;connect.textContent='正在保存…';try{let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:selected,password:p.value})}),m=await r.text();if(!r.ok)throw Error(m);"
    "document.querySelector('main').innerHTML='<section style=\"text-align:center;padding:34px 4px\"><h1>配置已保存</h1><p class=hint>设备正在重启并连接 '+esc(selected)+'，手机可以关闭此页面。</p></section>';footer.hidden=true}"
    "catch(e){connect.disabled=false;connect.textContent='连接此 Wi-Fi';alert('保存失败：'+e.message)}}scan()</script></body></html>";

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
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, s_setup_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_scan_get(httpd_req_t *request)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Wi-Fi scan failed");
    }

    uint16_t count = 0;
    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot read Wi-Fi scan");
    }
    if (count > 24U) count = 24U;
    wifi_ap_record_t *records = count == 0U ? NULL :
        calloc(count, sizeof(*records));
    if (count > 0U && records == NULL) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "scan memory unavailable");
    }
    if (count > 0U) {
        err = esp_wifi_scan_get_ap_records(&count, records);
    }

    cJSON *array = err == ESP_OK ? cJSON_CreateArray() : NULL;
    for (uint16_t index = 0; array != NULL && index < count; ++index) {
        cJSON *network = cJSON_CreateObject();
        if (network == NULL ||
            !cJSON_AddStringToObject(network,
                                    "ssid",
                                    (const char *)records[index].ssid) ||
            !cJSON_AddBoolToObject(network,
                                  "secure",
                                  records[index].authmode != WIFI_AUTH_OPEN) ||
            !cJSON_AddNumberToObject(network, "rssi", records[index].rssi)) {
            cJSON_Delete(network);
            cJSON_Delete(array);
            array = NULL;
            break;
        }
        cJSON_AddItemToArray(array, network);
    }
    free(records);
    if (array == NULL) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot encode Wi-Fi scan");
    }
    char *json = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    if (json == NULL) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "cannot encode Wi-Fi scan");
    }
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    esp_err_t response = httpd_resp_sendstr(request, json);
    cJSON_free(json);
    return response;
}

static esp_err_t captive_portal_404(httpd_req_t *request,
                                    httpd_err_code_t error)
{
    (void)error;
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(request, "Location", WIFI_SETUP_URL);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(
        request,
        "<!doctype html><meta charset=utf-8>"
        "<meta http-equiv=refresh content='0;url=http://192.168.6.1'>"
        "正在打开 TiRTC 配网页…");
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

static esp_err_t start_http_server(void)
{
    if (s_http_server != NULL) {
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 5;
    config.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_http_server, &config);
    if (err != ESP_OK) {
        s_http_server = NULL;
        ESP_LOGE(TAG, "cannot start provisioning HTTP server: %s",
                 esp_err_to_name(err));
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
    const httpd_uri_t scan = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = wifi_scan_get,
    };
    const httpd_uri_t apple_probe = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = setup_page_get,
    };
    err = httpd_register_uri_handler(s_http_server, &page);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &api);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &scan);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(s_http_server, &apple_probe);
    }
    if (err == ESP_OK) {
        err = httpd_register_err_handler(s_http_server,
                                         HTTPD_404_NOT_FOUND,
                                         captive_portal_404);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cannot register provisioning HTTP handlers: %s",
                 esp_err_to_name(err));
        (void)httpd_stop(s_http_server);
        s_http_server = NULL;
    }
    return err;
}

static void start_provisioning(void)
{
    if (s_provisioning) {
        return;
    }
    uint8_t mac[6] = {0};
    esp_err_t mac_err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (mac_err != ESP_OK) {
        /* ESP32-P4 has no native Wi-Fi MAC. Its base MAC is nevertheless a
         * stable last-resort suffix if the hosted C6 MAC is not ready yet. */
        (void)esp_read_mac(mac, ESP_MAC_BASE);
    }
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
    esp_err_t portal_err = start_http_server();
    if (portal_err == ESP_OK) {
        portal_err = captive_dns_start(s_ap_netif);
    }
    if (portal_err != ESP_OK) {
        ESP_LOGE(TAG, "cannot start captive portal: %s",
                 esp_err_to_name(portal_err));
    }
    ESP_LOGW(TAG,
             "provisioning active: connect open SSID=%s; portal=%s",
             s_provisioning_ssid,
             WIFI_SETUP_URL);
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
            s_has_saved_credentials = true;
            s_connection_failed = false;
            (void)esp_wifi_connect();
        } else {
            s_has_saved_credentials = false;
            s_connection_failed = false;
            (void)snprintf(s_provisioning_status,
                           sizeof(s_provisioning_status),
                           "尚未配置 Wi-Fi，请在手机页面选择家庭网络");
            start_provisioning();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        signal_connection_changed();
        if (!s_provisioning && s_has_saved_credentials &&
            s_retry_count++ < WIFI_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d",
                     s_retry_count, WIFI_CONNECT_RETRIES);
            (void)esp_wifi_connect();
        } else if (!s_provisioning) {
            if (s_has_saved_credentials) {
                s_connection_failed = true;
                (void)snprintf(s_provisioning_status,
                               sizeof(s_provisioning_status),
                               "无法连接已保存的 Wi-Fi，请检查名称或密码后重试");
            } else {
                (void)snprintf(s_provisioning_status,
                               sizeof(s_provisioning_status),
                               "尚未配置 Wi-Fi，请在手机页面选择家庭网络");
            }
            start_provisioning();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got_ip = event_data;
        s_connected = true;
        signal_connection_changed();
        s_connection_failed = false;
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
    (void)esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* 产品配网页明确约定 192.168.6.1，不能依赖 IDF 的 192.168.4.1 默认值。 */
    esp_netif_ip_info_t ap_ip = {0};
    esp_netif_set_ip4_addr(&ap_ip.ip, 192, 168, 6, 1);
    esp_netif_set_ip4_addr(&ap_ip.gw, 192, 168, 6, 1);
    esp_netif_set_ip4_addr(&ap_ip.netmask, 255, 255, 255, 0);
    (void)esp_netif_dhcps_stop(s_ap_netif);
    err = esp_netif_set_ip_info(s_ap_netif, &ap_ip);
    if (err != ESP_OK) {
        return err;
    }
    esp_netif_dns_info_t ap_dns = {0};
    ap_dns.ip.type = ESP_IPADDR_TYPE_V4;
    ap_dns.ip.u_addr.ip4.addr = ap_ip.ip.addr;
    err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &ap_dns);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t offer_dns = 1U;
    err = esp_netif_dhcps_option(s_ap_netif,
                                 ESP_NETIF_OP_SET,
                                 ESP_NETIF_DOMAIN_NAME_SERVER,
                                 &offer_dns,
                                 sizeof(offer_dns));
    if (err != ESP_OK) {
        return err;
    }
    /*
     * Do not advertise the local HTML page through DHCP option 114. RFC 8910
     * clients treat that URI as an RFC 8908 HTTPS/JSON API; an HTTP page can
     * suppress their legacy captive-network probe instead of opening it.
     * Wildcard DNS plus the HTTP 303 fallback below remain the discovery path.
     */
    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return err;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        return err;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    /* 启动前先决定 STA 还是 APSTA；STA_START 事件负责真正 connect。 */
    wifi_manager_credentials_t credentials;
    if (wifi_manager_load_credentials(&credentials) == ESP_OK) {
        s_has_saved_credentials = true;
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
        s_has_saved_credentials = false;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    }
    if (xTaskCreate(signal_task, "wifi_signal", 3072, NULL, 1, &s_signal_task) != pdPASS)
        return ESP_ERR_NO_MEM;
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

uint32_t wifi_manager_signal_revision(void)
{
    taskENTER_CRITICAL(&s_signal_lock);
    uint32_t revision = s_signal_revision;
    taskEXIT_CRITICAL(&s_signal_lock);
    return revision;
}

bool wifi_manager_signal_dbm(int8_t *rssi)
{
    if (rssi == NULL || !s_connected) {
        return false;
    }
    taskENTER_CRITICAL(&s_signal_lock);
    int cached = s_cached_rssi;
    taskEXIT_CRITICAL(&s_signal_lock);
    if (cached > 0) return false;
    *rssi = (int8_t)cached;
    return s_connected;
}

bool wifi_manager_provisioning(void)
{
    return s_provisioning;
}

bool wifi_manager_connection_failed(void)
{
    return s_connection_failed;
}

const char *wifi_manager_provisioning_ssid(void)
{
    return s_provisioning ? s_provisioning_ssid : "";
}

const char *wifi_manager_provisioning_password(void)
{
    return "";
}

const char *wifi_manager_provisioning_url(void)
{
    return WIFI_SETUP_URL;
}

const char *wifi_manager_provisioning_status(void)
{
    return s_provisioning ? s_provisioning_status : "";
}
