#include "xiaotai_network.h"

#include <common/bk_err.h>
#include <components/event.h>
#include "xiaotai_log.h"
#include <components/netif.h>
#include <components/system.h>
#include <lwip/sockets.h>
#include <modules/wifi.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "wifi_api.h"
#include "xiaotai_storage.h"
#include "xiaotai_metrics.h"
#include "xiaotai_network_state.h"
#include "xiaotai_ui.h"

#define TAG "xiaotai_net"
#define SETUP_IP "192.168.6.1"
#define WIFI_RETRIES 5
#define HTTP_PORT 80
#define HTTP_REQUEST_MAX 1024
#define SCAN_AP_MAX 20U
#define SCAN_JSON_MAX 4096U
#define PORTAL_IO_TIMEOUT_SECONDS 5

typedef enum {
    PORTAL_SCAN_IDLE = 0,
    PORTAL_SCAN_RUNNING,
    PORTAL_SCAN_READY,
    PORTAL_SCAN_ERROR,
} portal_scan_state_t;

typedef struct {
    char ssid[WIFI_SSID_STR_LEN];
    int rssi;
    uint8_t channel;
    wifi_security_t security;
} portal_ap_t;

static volatile bool s_started;
static volatile bool s_ready;
static volatile bool s_provisioning;
static volatile bool s_disconnected;
static char s_ap_ssid[33];
static beken_mutex_t s_scan_mutex;
static bool s_scan_mutex_ready;
static portal_scan_state_t s_scan_state;
static portal_ap_t s_scan_aps[SCAN_AP_MAX];
static size_t s_scan_ap_count;

static const char s_setup_page[] =
    "<!doctype html><html lang=zh-CN><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1,viewport-fit=cover'>"
    "<title>小钛联网助手</title><style>"
    "*{box-sizing:border-box}body{margin:0;background:#f5f5f7;color:#1d1d1f;"
    "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','PingFang SC',sans-serif}"
    ".page{max-width:480px;min-height:100vh;margin:auto;background:#fff;padding:28px 20px 110px}"
    ".brand{font-size:13px;font-weight:700;color:#6e3bd1}h1{font-size:27px;margin:7px 0}"
    ".lead{color:#667085;line-height:1.55;margin:0 0 16px}.steps{display:flex;gap:7px;margin:18px 0}"
    ".steps i{height:5px;flex:1;border-radius:5px;background:#e4e7ec}.steps i.on{background:#6e3bd1}"
    ".notice{padding:13px 14px;background:#f4f0ff;border-radius:14px;font-size:14px;"
    "line-height:1.55;margin-bottom:14px}.notice b{display:block}.btn{width:100%;border:0;"
    "border-radius:14px;padding:15px;font-size:17px;font-weight:700;background:#6e3bd1;color:#fff}"
    ".btn.alt{background:#f0f1f3;color:#344054}.btn:disabled{opacity:.55}.list{display:grid;"
    "gap:9px;margin-top:13px}.network{width:100%;border:1px solid #e4e7ec;background:#fff;"
    "border-radius:14px;padding:13px 14px;text-align:left;display:flex;justify-content:space-between;"
    "align-items:center;font-size:15px}.network strong,.network small{display:block}.network small{"
    "color:#667085;margin-top:3px}.link{width:100%;border:0;background:none;color:#6e3bd1;"
    "padding:15px;font-size:15px}.hidden{display:none!important}.back{border:0;background:none;"
    "color:#6e3bd1;padding:0 0 15px;font-size:15px}.chosen{display:flex;justify-content:space-between;"
    "align-items:center;padding:14px;background:#f8fafc;border:1px solid #e4e7ec;border-radius:14px}"
    ".chosen small{display:block;color:#667085;margin-bottom:3px}.label{display:block;font-size:14px;"
    "font-weight:700;margin:18px 0 8px}.field{display:flex;border:1px solid #cfd4dc;border-radius:14px;"
    "overflow:hidden;background:#fff}.field input{width:100%;border:0;outline:0;padding:15px;font-size:17px}"
    ".field button{border:0;background:#fff;color:#6e3bd1;padding:0 14px;font-weight:700}"
    ".help,.message{font-size:14px;color:#667085;line-height:1.5}.message{min-height:22px;color:#b54708}"
    ".footer{position:fixed;left:50%;bottom:0;transform:translateX(-50%);width:100%;max-width:480px;"
    "padding:13px 20px calc(13px + env(safe-area-inset-bottom));background:#fff;"
    "border-top:1px solid #eaecf0}.footer small{display:block;text-align:center;color:#667085;"
    "margin-top:7px}.result{text-align:center;padding-top:60px}.result .icon{font-size:52px}"
    ".result h2{font-size:25px}.result p{color:#667085;line-height:1.7;text-align:left;"
    "background:#f8fafc;padding:15px;border-radius:14px}</style></head><body><main class=page>"
    "<div class=brand>小钛联网助手</div><section id=form><h1 id=title>选择家里的 Wi-Fi</h1>"
    "<p class=lead id=lead>先选择网络，下一步再输入密码。</p><div class=steps>"
    "<i class=on></i><i id=step2></i></div><section id=choose><div class=notice>"
    "<b>① 手机已连接 <span id=ap>XiaoTai</span></b>手机提示“无互联网”是正常的，"
    "请保持连接，不要切回移动网络。</div><button class='btn alt' id=scan onclick=scanWifi()>"
    "扫描附近 Wi-Fi</button><p class=message id=scanmsg></p><div class=list id=networks></div>"
    "<button class=link onclick=manual()>找不到我的 Wi-Fi？手动输入名称</button>"
    "<div id=manual class=hidden><label class=label for=manualssid>Wi-Fi 名称</label>"
    "<div class=field><input id=manualssid maxlength=32 autocomplete=off></div>"
    "<button class='btn alt' style='margin-top:10px' onclick=useManual()>下一步</button></div>"
    "</section><section id=password class=hidden><button class=back onclick=showStep(1)>"
    "← 重新选择 Wi-Fi</button><div class=chosen><span><small>准备连接</small>"
    "<strong id=selected></strong></span><span id=quality></span></div>"
    "<div id=passwordbox><label class=label for=pass>输入这个 Wi-Fi 的密码</label>"
    "<div class=field><input id=pass type=password maxlength=64 placeholder='Wi-Fi 密码' "
    "autocomplete=current-password><button onclick=togglePassword(this) type=button>显示</button></div>"
    "<p class=help>密码只保存在小钛设备中；无密码网络可以留空。</p></div>"
    "<p class=message id=savemsg></p></section></section><section id=result class='result hidden'>"
    "<div class=icon>✓</div><h2>配置已保存</h2><p>小钛正在重启并尝试连接家庭 Wi-Fi。"
    "手机与 XiaoTai 热点断开属于正常现象。<br><br>请查看设备屏幕：连接成功后会进入下一步；"
    "如果配网失败，XiaoTai 热点会重新出现。</p></section></main>"
    "<div class='footer hidden' id=footer><button class=btn id=connect onclick=connectWifi()>"
    "连接此 Wi-Fi</button><small>连接通常需要 10–30 秒</small></div><script>"
    "const byId=id=>document.getElementById(id);let choice=null;"
    "const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));"
    "function signal(rssi){return rssi>=-50?'信号很好':rssi>=-67?'信号良好':"
    "rssi>=-75?'信号一般':'信号较弱'}"
    "function showStep(step){let second=step===2;byId('choose').classList.toggle('hidden',second);"
    "byId('password').classList.toggle('hidden',!second);byId('footer').classList.toggle('hidden',!second);"
    "byId('step2').classList.toggle('on',second);byId('title').textContent=second?'输入 Wi-Fi 密码':"
    "'选择家里的 Wi-Fi';byId('lead').textContent=second?'网络已经选好，密码框和连接按钮都在当前屏。':"
    "'先选择网络，下一步再输入密码。';if(!second){byId('savemsg').textContent='';choice=null}}"
    "function selectNetwork(ap){choice=ap;byId('selected').textContent=ap.ssid;"
    "byId('quality').textContent=ap.rssi?signal(ap.rssi):'';byId('passwordbox').classList.toggle('hidden',"
    "ap.secure===false);byId('pass').value='';showStep(2);if(ap.secure!==false)setTimeout(()=>byId('pass').focus(),80)}"
    "function addNetwork(ap){let button=document.createElement('button');button.type='button';"
    "button.className='network';let label=document.createElement('span');let name=document.createElement('strong');"
    "name.textContent=ap.ssid;let detail=document.createElement('small');detail.textContent=signal(ap.rssi);"
    "label.appendChild(name);label.appendChild(detail);let lock=document.createElement('span');"
    "lock.textContent=ap.secure?'🔒':'无密码';button.appendChild(label);button.appendChild(lock);"
    "button.onclick=()=>selectNetwork(ap);byId('networks').appendChild(button)}"
    "async function scanWifi(){let button=byId('scan');button.disabled=true;button.textContent='正在扫描…';"
    "byId('scanmsg').textContent='请稍候';let list=byId('networks');while(list.firstChild)list.removeChild(list.firstChild);"
    "try{let start=await fetch('/api/scan',"
    "{method:'POST'});if(!start.ok)throw Error();for(let i=0;i<20;i++){await wait(500);let response=await "
    "fetch('/api/networks',{cache:'no-store'});let data=await response.json();if(data.state==='ready'){"
    "data.networks.forEach(addNetwork);byId('scanmsg').textContent=data.networks.length?'请选择家里的 Wi-Fi':"
    "'没有发现 Wi-Fi，可以重新扫描或手动输入';return}if(data.state==='error')throw Error()}}catch(error){"
    "byId('scanmsg').textContent='扫描失败，请点击重新扫描'}finally{button.disabled=false;button.textContent='重新扫描附近 Wi-Fi'}}"
    "function manual(){byId('manual').classList.remove('hidden');byId('manualssid').focus()}"
    "function useManual(){let ssid=byId('manualssid').value.trim();if(!ssid){byId('scanmsg').textContent='请填写 Wi-Fi 名称';"
    "return}selectNetwork({ssid:ssid,secure:true,rssi:0})}"
    "function togglePassword(button){let input=byId('pass');input.type=input.type==='password'?'text':'password';"
    "button.textContent=input.type==='password'?'显示':'隐藏'}"
    "async function connectWifi(){if(!choice)return;if(choice.secure!==false&&!byId('pass').value){"
    "byId('savemsg').textContent='请输入这个 Wi-Fi 的密码';byId('pass').focus();return}"
    "let button=byId('connect');button.disabled=true;"
    "button.textContent='正在发送配置…';byId('savemsg').textContent='请保持手机连接当前热点';try{let response=await "
    "fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:choice.ssid,"
    "password:choice.secure===false?'':byId('pass').value})});if(!response.ok){byId('savemsg').textContent=await response.text();"
    "button.disabled=false;button.textContent='连接此 Wi-Fi';return}byId('form').classList.add('hidden');"
    "byId('footer').classList.add('hidden');byId('result').classList.remove('hidden')}catch(error){"
    "byId('savemsg').textContent='未收到设备响应，请确认仍连接小钛热点后重试';button.disabled=false;"
    "button.textContent='连接此 Wi-Fi'}}"
    "async function init(){try{let info=await (await fetch('/api/setup',{cache:'no-store'})).json();"
    "if(info.ap_ssid)byId('ap').textContent=info.ap_ssid}catch(error){}scanWifi()}init();"
    "</script></body></html>";

static int scan_compare(const void *left, const void *right)
{
    const portal_ap_t *a = left;
    const portal_ap_t *b = right;
    return b->rssi - a->rssi;
}

static int wifi_scan_done(void *arg, event_module_t module,
                          int event_id, void *event_data)
{
    (void)arg;
    (void)module;
    (void)event_id;
    (void)event_data;
    wifi_scan_result_t result = {0};
    int rc = bk_wifi_scan_get_result(&result);
    portal_ap_t selected[SCAN_AP_MAX] = {0};
    size_t count = 0U;
    if (rc == BK_OK) {
        for (int i = 0; i < result.ap_num; ++i) {
            const wifi_scan_ap_info_t *source = &result.aps[i];
            if (source->ssid[0] == '\0') continue;
            size_t existing = count;
            for (size_t j = 0; j < count; ++j) {
                if (strncmp(selected[j].ssid, source->ssid,
                            sizeof(selected[j].ssid)) == 0) {
                    existing = j;
                    break;
                }
            }
            if (existing < count) {
                if (source->rssi <= selected[existing].rssi) continue;
            } else if (count < SCAN_AP_MAX) {
                existing = count++;
            } else {
                size_t weakest = 0U;
                for (size_t j = 1; j < count; ++j) {
                    if (selected[j].rssi < selected[weakest].rssi) weakest = j;
                }
                if (source->rssi <= selected[weakest].rssi) continue;
                existing = weakest;
            }
            snprintf(selected[existing].ssid, sizeof(selected[existing].ssid),
                     "%.*s", (int)sizeof(selected[existing].ssid) - 1,
                     source->ssid);
            selected[existing].rssi = source->rssi;
            selected[existing].channel = source->channel;
            selected[existing].security = source->security;
        }
        qsort(selected, count, sizeof(selected[0]), scan_compare);
    }
    if (result.aps != NULL) bk_wifi_scan_free_result(&result);
    rtos_lock_mutex(&s_scan_mutex);
    memcpy(s_scan_aps, selected, count * sizeof(selected[0]));
    s_scan_ap_count = count;
    s_scan_state = rc == BK_OK ? PORTAL_SCAN_READY : PORTAL_SCAN_ERROR;
    rtos_unlock_mutex(&s_scan_mutex);
    BK_LOGI(TAG, "provisioning Wi-Fi scan complete rc=%d networks=%u\n",
            rc, (unsigned)count);
    return BK_OK;
}

static int start_portal_scan(void)
{
    rtos_lock_mutex(&s_scan_mutex);
    bool running = s_scan_state == PORTAL_SCAN_RUNNING;
    if (!running) s_scan_state = PORTAL_SCAN_RUNNING;
    rtos_unlock_mutex(&s_scan_mutex);
    if (running) return BK_OK;
    int rc = bk_wifi_scan_start(NULL);
    if (rc != BK_OK) {
        rtos_lock_mutex(&s_scan_mutex);
        s_scan_state = PORTAL_SCAN_ERROR;
        rtos_unlock_mutex(&s_scan_mutex);
    }
    return rc;
}

static bool json_append(char *output, size_t capacity, size_t *used,
                        const char *text)
{
    size_t length = strlen(text);
    if (*used + length >= capacity) return false;
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = '\0';
    return true;
}

static bool json_append_escaped(char *output, size_t capacity, size_t *used,
                                const char *text)
{
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; ++cursor) {
        char encoded[7] = {0};
        if (*cursor == '"' || *cursor == '\\') {
            encoded[0] = '\\';
            encoded[1] = (char)*cursor;
        } else if (*cursor < 0x20U) {
            snprintf(encoded, sizeof(encoded), "\\u%04x", *cursor);
        } else {
            encoded[0] = (char)*cursor;
        }
        if (!json_append(output, capacity, used, encoded)) return false;
    }
    return true;
}

static char *scan_json(void)
{
    char *output = psram_malloc(SCAN_JSON_MAX);
    if (output == NULL) return NULL;
    size_t used = 0U;
    portal_ap_t snapshot[SCAN_AP_MAX];
    rtos_lock_mutex(&s_scan_mutex);
    portal_scan_state_t state = s_scan_state;
    size_t count = s_scan_ap_count;
    memcpy(snapshot, s_scan_aps, count * sizeof(snapshot[0]));
    rtos_unlock_mutex(&s_scan_mutex);
    const char *state_name = state == PORTAL_SCAN_RUNNING ? "scanning" :
                             state == PORTAL_SCAN_READY ? "ready" :
                             state == PORTAL_SCAN_ERROR ? "error" : "idle";
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "{\"state\":\"%s\",\"networks\":[",
             state_name);
    bool valid = json_append(output, SCAN_JSON_MAX, &used, prefix);
    for (size_t i = 0; valid && i < count; ++i) {
        char fields[96];
        snprintf(fields, sizeof(fields),
                 "%s{\"ssid\":\"", i == 0U ? "" : ",");
        valid = json_append(output, SCAN_JSON_MAX, &used, fields) &&
                json_append_escaped(output, SCAN_JSON_MAX, &used,
                                    snapshot[i].ssid);
        snprintf(fields, sizeof(fields),
                 "\",\"rssi\":%d,\"channel\":%u,\"secure\":%s}",
                 snapshot[i].rssi, snapshot[i].channel,
                 snapshot[i].security == WIFI_SECURITY_NONE ? "false" : "true");
        valid = valid && json_append(output, SCAN_JSON_MAX, &used, fields);
    }
    valid = valid && json_append(output, SCAN_JSON_MAX, &used, "]}");
    if (!valid) {
        psram_free(output);
        return NULL;
    }
    return output;
}

static void wifi_event(void *event)
{
    const wifi_linkstate_reason_t *info = event;
    if (info == NULL) return;
    if (info->state == WIFI_LINKSTATE_STA_GOT_IP) {
        bool changed = xiaotai_network_link_changed(s_ready, true);
        s_ready = true;
        s_disconnected = false;
        netif_ip4_config_t ip = {0};
        if (bk_netif_get_ip4_config(NETIF_IF_STA, &ip) == BK_OK) {
            BK_LOGI(TAG, "STA connected IP=%s\n", ip.ip);
        }
        if (changed) {
            xiaotai_metrics_network_event(true);
            xiaotai_ui_show_status("WIFI");
        }
    } else if (info->state == WIFI_LINKSTATE_STA_DISCONNECTED) {
        bool changed = xiaotai_network_link_changed(s_ready, false);
        s_ready = false;
        s_disconnected = true;
        if (changed) xiaotai_metrics_network_event(false);
    }
}

static int start_sta(const xiaotai_wifi_credentials_t *credentials)
{
    wifi_sta_config_t config = {0};
    snprintf(config.ssid, sizeof(config.ssid), "%s", credentials->ssid);
    snprintf(config.password, sizeof(config.password), "%s", credentials->password);
    int rc = bk_wifi_sta_set_config(&config);
    if (rc == BK_OK) rc = bk_wifi_sta_start();
    return rc;
}

static bool send_all(int fd, const void *data, size_t size)
{
    const uint8_t *cursor = data;
    while (size > 0U) {
        int sent = send(fd, cursor, size, 0);
        if (sent <= 0) return false;
        cursor += (size_t)sent;
        size -= (size_t)sent;
    }
    return true;
}

static void send_response(int fd, const char *status,
                          const char *content_type, const char *body)
{
    char header[256];
    size_t body_size = strlen(body);
    int count = snprintf(header, sizeof(header),
                         "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
                         "Content-Length: %u\r\nConnection: close\r\n\r\n",
                         status, content_type, (unsigned)body_size);
    if (count <= 0 || (size_t)count >= sizeof(header) ||
        !send_all(fd, header, (size_t)count)) return;
    (void)send_all(fd, body, body_size);
}

static void send_redirect(int fd)
{
    static const char response[] =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://192.168.6.1/\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    (void)send_all(fd, response, sizeof(response) - 1U);
}

static bool is_captive_probe(const char *request)
{
    return strstr(request, "GET /generate_204 ") != NULL ||
           strstr(request, "GET /gen_204 ") != NULL ||
           strstr(request, "GET /hotspot-detect.html ") != NULL ||
           strstr(request, "GET /library/test/success.html ") != NULL ||
           strstr(request, "GET /connecttest.txt ") != NULL ||
           strstr(request, "GET /ncsi.txt ") != NULL ||
           strstr(request, "GET /redirect ") != NULL;
}

static bool receive_request(int fd, char *buffer, size_t capacity,
                            size_t *received_out)
{
    size_t received = 0;
    while (received + 1U < capacity) {
        int rc = recv(fd, buffer + received, capacity - received - 1U, 0);
        if (rc <= 0) return false;
        received += (size_t)rc;
        buffer[received] = '\0';
        char *header_end = strstr(buffer, "\r\n\r\n");
        if (header_end == NULL) continue;
        unsigned content_length = 0;
        const char *length = strstr(buffer, "Content-Length:");
        if (length != NULL) (void)sscanf(length, "Content-Length: %u", &content_length);
        size_t header_size = (size_t)(header_end + 4 - buffer);
        if (content_length > capacity - header_size - 1U) return false;
        if (received >= header_size + content_length) {
            *received_out = received;
            return true;
        }
    }
    return false;
}

static bool save_request_wifi(char *request)
{
    char *body = strstr(request, "\r\n\r\n");
    if (body == NULL) return false;
    body += 4;
    cJSON *root = cJSON_Parse(body);
    const cJSON *ssid = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "password");
    xiaotai_wifi_credentials_t credentials = {0};
    bool valid = cJSON_IsString(ssid) && cJSON_IsString(password);
    if (valid) {
        size_t ssid_length = strlen(ssid->valuestring);
        size_t password_length = strlen(password->valuestring);
        valid = ssid_length <= XIAOTAI_WIFI_SSID_MAX &&
                password_length <= XIAOTAI_WIFI_PASSWORD_MAX;
        if (valid) {
            memcpy(credentials.ssid, ssid->valuestring, ssid_length + 1U);
            memcpy(credentials.password, password->valuestring,
                   password_length + 1U);
            valid = xiaotai_wifi_credentials_valid(&credentials) &&
                    xiaotai_storage_save_wifi(&credentials) == BK_OK;
        }
    }
    cJSON_Delete(root);
    return valid;
}

static void portal_task(beken_thread_arg_t argument)
{
    (void)argument;
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0) goto done;
    int reuse = 1;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(HTTP_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server, 2) != 0) {
        close(server);
        goto done;
    }
    BK_LOGI(TAG, "provisioning portal ready at http://%s\n", SETUP_IP);
    for (;;) {
        int client = accept(server, NULL, NULL);
        if (client < 0) continue;
        struct timeval timeout = {
            .tv_sec = PORTAL_IO_TIMEOUT_SECONDS,
            .tv_usec = 0,
        };
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));
        char request[HTTP_REQUEST_MAX + 1] = {0};
        size_t received = 0;
        if (!receive_request(client, request, sizeof(request), &received)) {
            send_response(client, "400 Bad Request", "text/plain", "invalid request");
        } else if (strncmp(request, "POST /api/scan ", 15) == 0) {
            int scan_rc = start_portal_scan();
            send_response(client, scan_rc == BK_OK ? "202 Accepted" :
                                                    "503 Service Unavailable",
                          "application/json",
                          scan_rc == BK_OK ? "{\"state\":\"scanning\"}" :
                                             "{\"state\":\"error\"}");
        } else if (strncmp(request, "GET /api/setup ", 15) == 0) {
            char setup[128];
            snprintf(setup, sizeof(setup),
                     "{\"ap_ssid\":\"%s\",\"setup_ip\":\"%s\"}",
                     s_ap_ssid, SETUP_IP);
            send_response(client, "200 OK", "application/json", setup);
        } else if (strncmp(request, "GET /api/networks ", 18) == 0) {
            char *json = scan_json();
            if (json != NULL) {
                send_response(client, "200 OK", "application/json", json);
                psram_free(json);
            } else {
                send_response(client, "503 Service Unavailable",
                              "application/json", "{\"state\":\"error\"}");
            }
        } else if (strncmp(request, "POST /api/wifi ", 15) == 0) {
            if (save_request_wifi(request)) {
                send_response(client, "200 OK", "text/plain; charset=utf-8",
                              "保存成功，设备即将重启。");
                close(client);
                rtos_delay_milliseconds(1000);
                bk_reboot();
            } else {
                send_response(client, "400 Bad Request", "text/plain; charset=utf-8",
                              "Wi-Fi 名称或密码格式不正确，请返回检查。");
            }
        } else if (strncmp(request, "GET / ", 6) == 0 ||
                   strncmp(request, "GET /index.html ", 16) == 0) {
            send_response(client, "200 OK", "text/html; charset=utf-8", s_setup_page);
        } else if (is_captive_probe(request) ||
                   strncmp(request, "GET ", 4) == 0 ||
                   strncmp(request, "HEAD ", 5) == 0) {
            send_redirect(client);
        } else {
            send_response(client, "404 Not Found", "text/plain; charset=utf-8",
                          "not found");
        }
        close(client);
    }
done:
    BK_LOGE(TAG, "provisioning portal failed\n");
    rtos_delete_thread(NULL);
}

static int start_provisioning(void)
{
    if (s_provisioning) return BK_OK;
    uint8_t mac[6] = {0};
    (void)bk_get_mac(mac, MAC_TYPE_BASE);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "XiaoTai-%02X%02X", mac[4], mac[5]);

    netif_ip4_config_t ip = {0};
    snprintf(ip.ip, sizeof(ip.ip), "%s", SETUP_IP);
    snprintf(ip.mask, sizeof(ip.mask), "255.255.255.0");
    snprintf(ip.gateway, sizeof(ip.gateway), "%s", SETUP_IP);
    snprintf(ip.dns, sizeof(ip.dns), "%s", SETUP_IP);
    int rc = bk_netif_set_ip4_config(NETIF_IF_AP, &ip);

    wifi_ap_config_t ap = WIFI_DEFAULT_AP_CONFIG();
    snprintf(ap.ssid, sizeof(ap.ssid), "%s", s_ap_ssid);
    ap.password[0] = '\0';
    ap.channel = 1;
    if (rc == BK_OK) rc = bk_wifi_ap_set_config(&ap);
    if (rc == BK_OK) rc = bk_wifi_ap_start();
    if (rc != BK_OK) return rc;

    s_provisioning = true;
    BK_LOGW(TAG, "SoftAP provisioning active SSID=%s\n", s_ap_ssid);
    xiaotai_ui_show_network_setup(s_ap_ssid);
    return rtos_create_thread(NULL, BEKEN_APPLICATION_PRIORITY,
                              "xiaotai_portal", portal_task, 4096, NULL);
}

static void network_task(beken_thread_arg_t argument)
{
    (void)argument;
    xiaotai_wifi_credentials_t credentials = {0};
    if (xiaotai_storage_load_wifi(&credentials) != BK_OK) {
        (void)start_provisioning();
        rtos_delete_thread(NULL);
        return;
    }

    for (unsigned attempt = 1; attempt <= WIFI_RETRIES && !s_ready; ++attempt) {
        s_disconnected = false;
        int rc = start_sta(&credentials);
        if (rc != BK_OK) {
            BK_LOGW(TAG, "STA start failed attempt=%u rc=%d\n", attempt, rc);
        }
        for (unsigned wait = 0; wait < 100U && !s_ready && !s_disconnected; ++wait) {
            rtos_delay_milliseconds(100);
        }
        if (!s_ready) {
            BK_LOGW(TAG, "STA connection attempt %u/%u failed\n",
                    attempt, WIFI_RETRIES);
            (void)bk_wifi_sta_stop();
        }
    }
    if (!s_ready) (void)start_provisioning();
    rtos_delete_thread(NULL);
}

int xiaotai_network_start(void)
{
    if (s_started) return BK_OK;
    if (!s_scan_mutex_ready) {
        int mutex_rc = rtos_init_mutex(&s_scan_mutex);
        if (mutex_rc != BK_OK) return mutex_rc;
        s_scan_mutex_ready = true;
    }
    int event_rc = bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                        wifi_scan_done, NULL);
    if (event_rc != BK_OK) return event_rc;
    bk_wlan_status_register_cb(wifi_event);
    s_started = true;
    return rtos_create_thread(NULL, BEKEN_APPLICATION_PRIORITY,
                              "xiaotai_network", network_task, 4096, NULL);
}

bool xiaotai_network_ready(void)
{
    return s_ready;
}

int xiaotai_network_get_ip(char *address, size_t capacity)
{
    if (address == NULL || capacity == 0U) return BK_ERR_PARAM;
    netif_ip4_config_t ip = {0};
    int rc = bk_netif_get_ip4_config(NETIF_IF_STA, &ip);
    if (rc != BK_OK) {
        address[0] = '\0';
        return rc;
    }
    snprintf(address, capacity, "%s", ip.ip);
    return BK_OK;
}

bool xiaotai_network_provisioning(void)
{
    return s_provisioning;
}

int xiaotai_network_enter_provisioning(void)
{
    if (!s_started) return BK_ERR_NOT_INIT;
    if (s_ready) (void)bk_wifi_sta_stop();
    s_ready = false;
    s_disconnected = true;
    return start_provisioning();
}
