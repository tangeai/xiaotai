#include "xiaotai_platform_client.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <components/system.h>
#include <components/webclient.h>
#include <driver/aon_rtc.h>
#include <driver/trng.h>
#include <errno.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <os/mem.h>
#include <os/os.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pbkdf2_sha256.h"

#include "cJSON.h"
#include "xiaotai_audio.h"
#include "xiaotai_network.h"
#include "xiaotai_pcm_stream.h"
#include "xiaotai_time.h"
#include "xiaotai_ui.h"

#define TAG "xiaotai_platform"
#define HTTP_BODY_MAX 4096U
#define TTS_PCM_MAX_BYTES (320U * 1024U)
#define MQTT_PACKET_MAX 2048U
#define MQTT_TOKEN_MAX 1024U
#define MQTT_TOPIC_MAX 128U
#define BIND_RECV_TIMEOUT_MS 1000
#define NTP_UNIX_EPOCH_DELTA 2208988800UL

/* BK-AVDK exports this function from webclient.c but omits its declaration
 * from components/webclient.h.  It is required to complete an empty POST:
 * webclient_post(session, url, NULL, 0) only sends the request headers. */
extern int webclient_handle_response(struct webclient_session *session);

typedef enum {
    HTTP_JSON_GET = 0,
    HTTP_JSON_POST,
    HTTP_JSON_POST_EMPTY,
} http_json_method_t;

typedef struct {
    char device[256];
    char mqtt[256];
    char ai[256];
    char call[256];
    char voip[256];
    char tirtc[256];
} platform_services_t;

typedef struct {
    char code[17];
    char temp_token[MQTT_TOKEN_MAX];
    char temp_client_id[65];
} provision_report_t;

typedef struct {
    platform_services_t services;
    char token[MQTT_TOKEN_MAX];
    char url[512];
} service_request_work_t;

static int format_device_mac(char output[18])
{
    uint8_t mac[6] = {0};
    if (bk_get_mac(mac, MAC_TYPE_BASE) != BK_OK) return BK_FAIL;
    int count = snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return count == 17 ? BK_OK : BK_FAIL;
}

int xiaotai_platform_client_id(char *output, size_t capacity)
{
    if (output == NULL || capacity < 18U) return BK_ERR_PARAM;
    return format_device_mac(output);
}

static volatile bool s_binding;
static bool s_clock_synchronized;
static char s_verification_code[17];
static beken_mutex_t s_service_mutex;
static beken_mutex_t s_http_mutex;
static bool s_service_cache_ready;
static platform_services_t s_service_cache;
static char s_service_token[MQTT_TOKEN_MAX];
/* Publish the complete directional stream routing used by the H5 clients.
 * Device live media is 10/11; browser talkback media is 14/15. */
static const char s_device_profile[] =
    "{\"hardware\":{\"chip_model\":\"BK7258\","
    "\"board_model\":\"lckfb-bk7258\"},"
    "\"firmware_version\":\"1.0.0+build.2\","
    "\"profiles\":{\"stream\":{\"up_audio_streamid\":10,"
    "\"up_video_streamid\":11,\"down_audio_streamid\":14,"
    "\"down_video_streamid\":15,\"up_audio_mt\":[\"alaw\"],"
    "\"up_video_mt\":[\"h264\"],\"down_audio_mt\":[\"alaw\"],"
    "\"audio_rate\":8000,\"audio_channels\":1,\"camera_rotation\":0,"
    "\"aspect_ratio\":\"4:3\",\"hor_mirror\":false,"
    "\"vert_mirror\":false,\"object_fit\":\"contain\","
    "\"no_video\":false},"
    "\"call\":{\"up_audio_mt\":[\"alaw\"],\"down_audio_mt\":[\"alaw\"],"
    "\"audio_rate\":8000,\"audio_channels\":1,\"no_video\":true},"
    "\"voip\":{\"screen_width\":1,\"screen_height\":1,"
    "\"audio_rate\":8000,\"audio_channels\":1,"
    "\"up_video_mt\":\"none\",\"down_video_mt\":\"none\","
    "\"down_audio_mt\":\"alaw\",\"no_video\":true,"
    "\"calling_timeout_sec\":30}}}";

static bool copy_json_string(const cJSON *object, const char *name,
                             char *destination, size_t capacity)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0' || strlen(item->valuestring) >= capacity) {
        return false;
    }
    snprintf(destination, capacity, "%s", item->valuestring);
    return true;
}

static bool has_prefix(const char *value, const char *prefix)
{
    return value != NULL && prefix != NULL &&
           strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool services_use_open_transport(const platform_services_t *services)
{
    return services != NULL &&
           has_prefix(services->device, "http://") &&
           has_prefix(services->ai, "http://") &&
           has_prefix(services->call, "http://") &&
           has_prefix(services->voip, "http://") &&
           has_prefix(services->tirtc, "http://") &&
           has_prefix(services->mqtt, "mqtt://");
}

static int ensure_http_mutex(void)
{
    if (s_http_mutex != NULL) return BK_OK;
    return rtos_init_mutex(&s_http_mutex);
}

static int http_json_request_unlocked(
    const char *url, http_json_method_t method, const char *body,
    const char *bearer, const char *const header_names[],
    const char *const header_values[], size_t header_count,
    char **response_out, size_t *response_size_out, int *http_status_out)
{
    if (url == NULL || response_out == NULL || response_size_out == NULL) {
        return BK_ERR_PARAM;
    }
    *response_out = NULL;
    *response_size_out = 0;
    if (http_status_out != NULL) *http_status_out = 0;
    const char *method_name = method == HTTP_JSON_GET ? "GET" : "POST";
    BK_LOGD(TAG,
            "HTTP request method=%s url=%s authenticated=%d body-bytes=%u\n",
            method_name, url,
            bearer != NULL && bearer[0] != '\0' ? 1 : 0,
            (unsigned)(body == NULL ? 0U : strlen(body)));
    for (size_t i = 0; i < header_count; ++i) {
        BK_LOGD(TAG, "HTTP request header present name=%s value-bytes=%u\n",
                header_names[i], (unsigned)strlen(header_values[i]));
    }
    struct webclient_session *session = webclient_session_create(2048);
    if (session == NULL) return BK_ERR_NO_MEM;
    webclient_set_timeout(session, 10000);
    char *authorization = NULL;
    if (bearer != NULL && bearer[0] != '\0') {
        authorization = psram_malloc(MQTT_TOKEN_MAX + 16U);
        if (authorization == NULL) {
            webclient_close(session);
            return BK_ERR_NO_MEM;
        }
        int count = snprintf(authorization, MQTT_TOKEN_MAX + 16U,
                             "Bearer %s", bearer);
        if (count <= 0 || (size_t)count >= MQTT_TOKEN_MAX + 16U) {
            memset(authorization, 0, MQTT_TOKEN_MAX + 16U);
            psram_free(authorization);
            webclient_close(session);
            return BK_ERR_PARAM;
        }
        webclient_header_fields_add(session, "Authorization: %s\r\n",
                                    authorization);
    }
    for (size_t i = 0; i < header_count; ++i) {
        webclient_header_fields_add(session, "%s: %s\r\n",
                                    header_names[i], header_values[i]);
    }
    int status = 0;
    if (method == HTTP_JSON_GET) {
        status = webclient_get(session, url);
    } else if (method == HTTP_JSON_POST && body != NULL) {
        webclient_header_fields_add(session, "Content-Type: application/json\r\n");
        webclient_header_fields_add(session, "Content-Length: %u\r\n",
                                    (unsigned)strlen(body));
        status = webclient_post(session, url, body, strlen(body));
    } else if (method == HTTP_JSON_POST_EMPTY && body == NULL) {
        webclient_header_fields_add(session, "Content-Type: application/json\r\n");
        webclient_header_fields_add(session, "Content-Length: 0\r\n");
        status = webclient_post(session, url, NULL, 0);
        if (status == WEBCLIENT_OK) {
            status = webclient_handle_response(session);
        }
    } else {
        status = -WEBCLIENT_ERROR;
    }
    if (http_status_out != NULL) *http_status_out = status;
    int rc = BK_FAIL;
    int content_length = webclient_content_length_get(session);
    if (status > 0 && content_length >= 0 &&
        (size_t)content_length <= HTTP_BODY_MAX) {
        void *response = NULL;
        size_t response_size = 0;
        int received = webclient_response(session, &response, &response_size);
        if (received > 0 && response != NULL && response_size <= HTTP_BODY_MAX) {
            *response_out = response;
            *response_size_out = response_size;
            if (status == 200) rc = BK_OK;
        } else if (response != NULL) {
            os_free(response);
        }
    }
    if (status != 200) {
        BK_LOGE(TAG, "HTTP request failed status=%d\n", status);
    } else if (content_length < 0 || (size_t)content_length > HTTP_BODY_MAX) {
        BK_LOGE(TAG, "HTTP response exceeds bounded buffer\n");
    }
    if (*response_out == NULL) {
        BK_LOGD(TAG, "HTTP response status=%d bytes=0\n", status);
    } else {
        BK_LOGD(TAG, "HTTP response status=%d bytes=%u\n",
                status, (unsigned)*response_size_out);
    }
    /* Keep this boundary explicit while validating the HTTP-only BK path.
     * The SDK heap checker reports corruption only when an allocation is
     * released, so without these markers a UART-interleaved assert cannot
     * distinguish WebClient cleanup from the response JSON handling below. */
    BK_LOGD(TAG, "HTTP cleanup begin status=%d\n", status);
    webclient_close(session);
    BK_LOGD(TAG, "HTTP cleanup complete status=%d\n", status);
    if (authorization != NULL) {
        memset(authorization, 0, MQTT_TOKEN_MAX + 16U);
        psram_free(authorization);
    }
    return rc;
}

static int http_json_request_ex(const char *url, http_json_method_t method,
                                const char *body,
                                const char *bearer,
                                const char *const header_names[],
                                const char *const header_values[],
                                size_t header_count,
                                char **response_out, size_t *response_size_out,
                                int *http_status_out)
{
    int rc = ensure_http_mutex();
    if (rc != BK_OK) return rc;
    /* WebClient owns shared request state. Serialize discovery, reporting and
     * ordinary service requests; callbacks run after this gate is released. */
    rtos_lock_mutex(&s_http_mutex);
    rc = http_json_request_unlocked(url, method, body, bearer, header_names,
                                    header_values, header_count, response_out,
                                    response_size_out, http_status_out);
    rtos_unlock_mutex(&s_http_mutex);
    return rc;
}

static int http_json_request(const char *url, const char *body,
                             char **response_out, size_t *response_size_out)
{
    http_json_method_t method = body == NULL ? HTTP_JSON_GET : HTTP_JSON_POST;
    return http_json_request_ex(url, method, body, NULL, NULL, NULL, 0,
                                response_out, response_size_out, NULL);
}

static int discover_services(platform_services_t *services)
{
    char *response = NULL;
    size_t response_size = 0;
    int rc = http_json_request(XIAOTAI_PLATFORM_DISCOVERY_URL, NULL,
                               &response, &response_size);
    if (rc != BK_OK) return rc;
    (void)response_size;
    cJSON *root = cJSON_Parse(response);
    os_free(response);
    bool valid = root != NULL &&
                 copy_json_string(root, "device-srv", services->device,
                                  sizeof(services->device)) &&
                 copy_json_string(root, "mqtt-srv", services->mqtt,
                                  sizeof(services->mqtt)) &&
                 copy_json_string(root, "ai-srv", services->ai,
                                  sizeof(services->ai)) &&
                 copy_json_string(root, "voip-srv", services->voip,
                                  sizeof(services->voip)) &&
                 copy_json_string(root, "tirtc-srv", services->tirtc,
                                  sizeof(services->tirtc));
    if (valid && !copy_json_string(root, "call-srv", services->call,
                                   sizeof(services->call))) {
        snprintf(services->call, sizeof(services->call), "%s",
                 services->device);
    }
    cJSON_Delete(root);
    if (!valid || !services_use_open_transport(services)) {
        BK_LOGE(TAG, "service discovery rejected unsupported endpoint\n");
        return BK_FAIL;
    }
    BK_LOGI(TAG, "open service discovery complete (HTTP/MQTT)\n");
    return BK_OK;
}

static int cache_services(const platform_services_t *services,
                          const char *token)
{
    if (services == NULL || token == NULL || token[0] == '\0') {
        return BK_ERR_PARAM;
    }
    if (s_service_mutex == NULL) {
        int rc = rtos_init_mutex(&s_service_mutex);
        if (rc != BK_OK) return rc;
    }
    rtos_lock_mutex(&s_service_mutex);
    s_service_cache = *services;
    snprintf(s_service_token, sizeof(s_service_token), "%s", token);
    s_service_cache_ready = true;
    rtos_unlock_mutex(&s_service_mutex);
    return BK_OK;
}

static bool copy_cached_session(platform_services_t *services,
                                char *token, size_t token_capacity)
{
    if (services == NULL || token == NULL || token_capacity == 0U ||
        s_service_mutex == NULL) {
        return false;
    }
    rtos_lock_mutex(&s_service_mutex);
    bool ready = s_service_cache_ready && s_service_token[0] != '\0' &&
                 strlen(s_service_token) < token_capacity;
    if (ready) {
        *services = s_service_cache;
        snprintf(token, token_capacity, "%s", s_service_token);
    }
    rtos_unlock_mutex(&s_service_mutex);
    return ready;
}

int xiaotai_platform_tirtc_endpoint(char *output, size_t capacity)
{
    if (output == NULL || capacity == 0U || s_service_mutex == NULL) {
        return BK_ERR_PARAM;
    }
    output[0] = '\0';
    rtos_lock_mutex(&s_service_mutex);
    bool plain = s_service_cache_ready &&
                 has_prefix(s_service_cache.tirtc, "http://");
    const char *host = plain ? s_service_cache.tirtc + strlen("http://") : NULL;
    bool ready = host != NULL && strlen(s_service_cache.tirtc) < capacity;
    if (ready) {
        snprintf(output, capacity, "%s", s_service_cache.tirtc);
    }
    rtos_unlock_mutex(&s_service_mutex);
    return ready ? BK_OK : BK_FAIL;
}

static const char *service_base_for_path(const platform_services_t *services,
                                         const char *path)
{
    if (strncmp(path, "/v1/ai/", 7U) == 0) return services->ai;
    if (strncmp(path, "/v1/call/", 9U) == 0) return services->call;
    if (strncmp(path, "/v1/wxvoip/", 11U) == 0 ||
        strncmp(path, "/v1/voip/", 9U) == 0) return services->voip;
    if (strncmp(path, "/v1/device/", 11U) == 0) return services->device;
    return NULL;
}

int xiaotai_platform_service_request(const char *path, const char *json,
                                     xiaotai_platform_response_fn response,
                                     void *context)
{
    if (path == NULL || path[0] != '/' || response == NULL ||
        s_service_mutex == NULL) {
        return BK_ERR_PARAM;
    }
    service_request_work_t *work = psram_malloc(sizeof(*work));
    if (work == NULL) return BK_ERR_NO_MEM;
    memset(work, 0, sizeof(*work));
    rtos_lock_mutex(&s_service_mutex);
    bool ready = s_service_cache_ready;
    if (ready) {
        work->services = s_service_cache;
        snprintf(work->token, sizeof(work->token), "%s", s_service_token);
    }
    rtos_unlock_mutex(&s_service_mutex);
    if (!ready) {
        memset(work, 0, sizeof(*work));
        psram_free(work);
        return BK_FAIL;
    }

    const char *base = service_base_for_path(&work->services, path);
    if (base == NULL || base[0] == '\0') {
        memset(work, 0, sizeof(*work));
        psram_free(work);
        return BK_ERR_PARAM;
    }
    size_t base_length = strlen(base);
    const char *separator = base_length > 0U && base[base_length - 1U] == '/'
                                ? "" : "/";
    int url_size = snprintf(work->url, sizeof(work->url), "%s%s%s",
                            base, separator, path + 1);
    if (url_size <= 0 || (size_t)url_size >= sizeof(work->url)) {
        memset(work, 0, sizeof(*work));
        psram_free(work);
        return BK_ERR_PARAM;
    }

    char *body = NULL;
    size_t body_size = 0;
    int http_status = 0;
    int rc = http_json_request_ex(work->url,
                                  json == NULL ? HTTP_JSON_GET : HTTP_JSON_POST,
                                  json, work->token, NULL, NULL, 0,
                                  &body, &body_size, &http_status);
    memset(work, 0, sizeof(*work));
    psram_free(work);
    if (rc == BK_OK && body != NULL) response(body, context);
    if (body != NULL) os_free(body);
    /* Assignment is a low-rate reconciliation request. Its unchanged 200
     * responses are intentionally silent; xiaotai_app logs state changes and
     * failures remain visible here. */
    if (rc != BK_OK ||
        strcmp(path, "/v1/call/group/device/assignment") != 0) {
        BK_LOGI(TAG, "service request path=%s status=%d bytes=%u\n",
                path, http_status, (unsigned)body_size);
    }
    if (rc == BK_OK) return 0;
    return http_status >= 300 && http_status <= 599 ? http_status : BK_FAIL;
}

static int report_device(const platform_services_t *services,
                         provision_report_t *report)
{
    char mac_text[18];
    if (format_device_mac(mac_text) != BK_OK) return BK_FAIL;
    char url[320];
    char body[48];
    int url_size = snprintf(url, sizeof(url), "%s/v1/device/report",
                            services->device);
    int body_size = snprintf(body, sizeof(body), "{\"mac\":\"%s\"}",
                             mac_text);
    if (url_size <= 0 || (size_t)url_size >= sizeof(url) ||
        body_size <= 0 || (size_t)body_size >= sizeof(body)) {
        return BK_FAIL;
    }

    char *response = NULL;
    size_t response_size = 0;
    int rc = http_json_request(url, body, &response, &response_size);
    if (rc != BK_OK) return rc;
    (void)response_size;
    cJSON *root = cJSON_Parse(response);
    os_free(response);
    const cJSON *code = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *data = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "data");
    bool valid = cJSON_IsNumber(code) && code->valueint == 200 &&
                 cJSON_IsObject(data) &&
                 copy_json_string(data, "code", report->code,
                                  sizeof(report->code)) &&
                 copy_json_string(data, "temp_token", report->temp_token,
                                  sizeof(report->temp_token)) &&
                 copy_json_string(data, "temp_client_id", report->temp_client_id,
                                  sizeof(report->temp_client_id));
    cJSON_Delete(root);
    if (!valid || strlen(report->code) != 6U) return BK_FAIL;
    for (size_t i = 0; i < 6U; ++i) {
        if (report->code[i] < '0' || report->code[i] > '9') return BK_FAIL;
    }
    return BK_OK;
}

static int webclient_pcm_read(void *context, void *output, size_t capacity)
{
    return webclient_read((struct webclient_session *)context,
                          output, capacity);
}

static int download_verification_tts(const platform_services_t *services,
                                     const provision_report_t *report,
                                     int16_t **pcm_out,
                                     size_t *sample_count_out)
{
    if (services == NULL || report == NULL || pcm_out == NULL ||
        sample_count_out == NULL) {
        return BK_ERR_PARAM;
    }
    *pcm_out = NULL;
    *sample_count_out = 0U;
    char url[384];
    int url_size = snprintf(url, sizeof(url), "%s/v1/device/tts?code=%s",
                            services->device, report->code);
    if (url_size <= 0 || (size_t)url_size >= sizeof(url)) return BK_ERR_PARAM;

    struct webclient_session *session = webclient_session_create(2048);
    if (session == NULL) return BK_ERR_NO_MEM;
    webclient_set_timeout(session, 15000);
    char *authorization = os_malloc(MQTT_TOKEN_MAX + 16U);
    if (authorization == NULL) {
        webclient_close(session);
        return BK_ERR_NO_MEM;
    }
    int auth_size = snprintf(authorization, MQTT_TOKEN_MAX + 16U,
                             "Bearer %s", report->temp_token);
    int rc = BK_FAIL;
    int status = 0;
    int content_length = -1;
    if (auth_size <= 0 || (size_t)auth_size >= MQTT_TOKEN_MAX + 16U) {
        rc = BK_ERR_PARAM;
        goto done;
    }
    webclient_header_fields_add(session, "Authorization: %s\r\n",
                                authorization);
    webclient_header_fields_add(session, "Accept: audio/pcm\r\n");
    BK_LOGD(TAG, "verification TTS GET url=%s authenticated=1\n", url);
    status = webclient_get(session, url);
    content_length = webclient_content_length_get(session);
    const char *content_type =
        webclient_header_fields_get(session, "Content-Type:");
    const char *transfer_encoding =
        webclient_header_fields_get(session, "Transfer-Encoding:");
    bool chunked = transfer_encoding != NULL &&
        (strcmp(transfer_encoding, "chunked") == 0 ||
         strcmp(transfer_encoding, "Chunked") == 0);
    if (status != 200 ||
        (content_length < 0 && !chunked) ||
        (content_length >= 0 &&
         (content_length < 4000 || (content_length & 1) != 0 ||
          (size_t)content_length > TTS_PCM_MAX_BYTES)) ||
        content_type == NULL || strstr(content_type, "audio/pcm") == NULL) {
        BK_LOGE(TAG,
                "verification TTS rejected status=%d bytes=%d chunked=%d content-type=%s\n",
                status, content_length,
                chunked ? 1 : 0,
                content_type == NULL ? "<missing>" : content_type);
        goto done;
    }

    size_t allocation = content_length >= 0 ? (size_t)content_length :
                        TTS_PCM_MAX_BYTES;
    uint8_t *pcm = psram_malloc(allocation);
    if (pcm == NULL) {
        rc = BK_ERR_NO_MEM;
        BK_LOGE(TAG, "verification TTS PSRAM allocation failed bytes=%u\n",
                (unsigned)allocation);
        goto done;
    }
    size_t received = 0U;
    if (xiaotai_pcm_stream_collect(
            webclient_pcm_read, session, pcm, allocation,
            content_length, 4000U, &received) != 0) {
        memset(pcm, 0, allocation);
        psram_free(pcm);
        BK_LOGE(TAG, "verification TTS invalid expected=%d received=%u\n",
                content_length, (unsigned)received);
        goto done;
    }
    *pcm_out = (int16_t *)pcm;
    *sample_count_out = received / sizeof(int16_t);
    rc = BK_OK;
    BK_LOGI(TAG, "verification TTS ready samples=%u duration_ms=%u\n",
            (unsigned)*sample_count_out,
            (unsigned)(*sample_count_out * 1000U / 8000U));
done:
    webclient_close(session);
    memset(authorization, 0, MQTT_TOKEN_MAX + 16U);
    os_free(authorization);
    return rc;
}

static size_t mqtt_remaining_length(uint8_t *out, size_t value)
{
    size_t used = 0;
    do {
        uint8_t byte = (uint8_t)(value % 128U);
        value /= 128U;
        if (value != 0U) byte |= 0x80U;
        out[used++] = byte;
    } while (value != 0U && used < 4U);
    return used;
}

static bool append_utf8(uint8_t *packet, size_t capacity, size_t *offset,
                        const char *text)
{
    size_t length = strlen(text);
    if (length > UINT16_MAX || *offset + length + 2U > capacity) return false;
    packet[(*offset)++] = (uint8_t)(length >> 8);
    packet[(*offset)++] = (uint8_t)length;
    memcpy(packet + *offset, text, length);
    *offset += length;
    return true;
}

static int transport_write(int socket_fd, const void *data, size_t size)
{
    return send(socket_fd, data, size, 0);
}

static int transport_read(int socket_fd, void *data, size_t size)
{
    return recv(socket_fd, data, size, 0);
}

static int send_all(int socket_fd, const void *data, size_t size)
{
    const uint8_t *cursor = data;
    while (size > 0U) {
        int sent = transport_write(socket_fd, cursor, size);
        if (sent <= 0) return BK_FAIL;
        cursor += sent;
        size -= (size_t)sent;
    }
    return BK_OK;
}

static int mqtt_send_frame(int socket_fd, uint8_t header,
                           const uint8_t *body, size_t body_size)
{
    BK_LOGD(TAG, "MQTT TX header=0x%02x bytes=%u\n",
            header, (unsigned)body_size);
    uint8_t prefix[5];
    prefix[0] = header;
    size_t encoded = mqtt_remaining_length(prefix + 1, body_size);
    if (send_all(socket_fd, prefix, encoded + 1U) != BK_OK) return BK_FAIL;
    return body_size == 0U ? BK_OK : send_all(socket_fd, body, body_size);
}

static int recv_exact(int socket_fd, void *data, size_t size)
{
    uint8_t *cursor = data;
    uint32_t started_ms = rtos_get_time();
    while (size > 0U) {
        int received = transport_read(socket_fd, cursor, size);
        if (received < 0) return received;
        if (received == 0) {
            if ((uint32_t)(rtos_get_time() - started_ms) >= 10000U) {
                return BK_FAIL;
            }
            continue;
        }
        cursor += received;
        size -= (size_t)received;
    }
    return 1;
}

static int mqtt_read_frame(int socket_fd, uint8_t *header,
                           uint8_t *body, size_t capacity, size_t *body_size)
{
    int rc = transport_read(socket_fd, header, 1U);
    if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                   errno == ETIMEDOUT)) {
        return 0;
    }
    if (rc == 0) return 0;
    if (rc != 1) return BK_FAIL;
    size_t remaining = 0;
    size_t multiplier = 1;
    for (unsigned i = 0; i < 4U; ++i) {
        uint8_t byte = 0;
        rc = recv_exact(socket_fd, &byte, 1U);
        if (rc <= 0) return rc;
        remaining += (size_t)(byte & 0x7FU) * multiplier;
        if ((byte & 0x80U) == 0U) {
            if (remaining > capacity) return BK_FAIL;
            rc = recv_exact(socket_fd, body, remaining);
            if (rc <= 0 && remaining != 0U) return rc;
            *body_size = remaining;
            BK_LOGD(TAG, "MQTT RX header=0x%02x bytes=%u\n",
                    *header, (unsigned)remaining);
            return 1;
        }
        multiplier *= 128U;
    }
    return BK_FAIL;
}

static int mqtt_send_connect_auth(int socket_fd, const char *client_id,
                                  const char *username, const char *password)
{
    BK_LOGD(TAG,
            "MQTT CONNECT client_id=%s username=%s authenticated=%d\n",
            client_id, username, password[0] != '\0' ? 1 : 0);
    uint8_t body[MQTT_PACKET_MAX];
    size_t used = 0;
    if (!append_utf8(body, sizeof(body), &used, "MQTT") || used + 4U > sizeof(body)) {
        return BK_FAIL;
    }
    body[used++] = 4U;
    body[used++] = 0xC2U;
    body[used++] = 0U;
    body[used++] = 60U;
    if (!append_utf8(body, sizeof(body), &used, client_id) ||
        !append_utf8(body, sizeof(body), &used, username) ||
        !append_utf8(body, sizeof(body), &used, password)) {
        return BK_FAIL;
    }
    return mqtt_send_frame(socket_fd, 0x10U, body, used);
}

static int mqtt_send_connect(int socket_fd, const provision_report_t *report)
{
    return mqtt_send_connect_auth(socket_fd, report->temp_client_id,
                                  report->temp_client_id, report->temp_token);
}

static int mqtt_send_subscribe_id(int socket_fd, const char *topic,
                                  uint16_t packet_id)
{
    BK_LOGD(TAG, "MQTT SUBSCRIBE topic=%s packet_id=%u qos=1\n",
            topic, (unsigned)packet_id);
    uint8_t body[MQTT_TOPIC_MAX + 8U];
    size_t used = 0;
    body[used++] = (uint8_t)(packet_id >> 8);
    body[used++] = (uint8_t)packet_id;
    if (!append_utf8(body, sizeof(body), &used, topic) || used >= sizeof(body)) {
        return BK_FAIL;
    }
    body[used++] = 1U;
    return mqtt_send_frame(socket_fd, 0x82U, body, used);
}

static int mqtt_send_subscribe(int socket_fd, const char *topic)
{
    return mqtt_send_subscribe_id(socket_fd, topic, 1U);
}

static int mqtt_send_publish(int socket_fd, const char *topic,
                             const char *payload, uint16_t packet_id,
                             bool qos1)
{
    size_t payload_size = strlen(payload);
    BK_LOGD(TAG,
            "MQTT PUBLISH topic=%s packet_id=%u qos=%u payload-bytes=%u\n",
            topic, (unsigned)packet_id, qos1 ? 1U : 0U,
            (unsigned)payload_size);
    uint8_t body[MQTT_PACKET_MAX];
    size_t used = 0;
    if (!append_utf8(body, sizeof(body), &used, topic)) return BK_FAIL;
    if (qos1) {
        if (used + 2U > sizeof(body)) return BK_FAIL;
        body[used++] = (uint8_t)(packet_id >> 8);
        body[used++] = (uint8_t)packet_id;
    }
    if (used + payload_size > sizeof(body)) return BK_FAIL;
    memcpy(body + used, payload, payload_size);
    used += payload_size;
    return mqtt_send_frame(socket_fd, qos1 ? 0x32U : 0x30U, body, used);
}

static int mqtt_send_publish_ack(int socket_fd, const char *topic)
{
    static const char payload[] = "{\"ack\":true}";
    return mqtt_send_publish(socket_fd, topic, payload, 2U, true);
}

static bool parse_auth_grant(const uint8_t *payload, size_t size,
                             xiaotai_device_credentials_t *out)
{
    cJSON *root = cJSON_ParseWithLength((const char *)payload, size);
    const cJSON *type = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *data = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "payload");
    bool valid = cJSON_IsString(type) &&
                 strcmp(type->valuestring, "auth_grant") == 0 &&
                 cJSON_IsObject(data) &&
                 copy_json_string(data, "device_id", out->device_id,
                                  sizeof(out->device_id)) &&
                 copy_json_string(data, "device_key", out->device_secret,
                                  sizeof(out->device_secret));
    cJSON_Delete(root);
    return valid;
}

static int parse_mqtt_uri(const char *uri, char *host, size_t host_capacity,
                          uint16_t *port)
{
    static const char plain_prefix[] = "mqtt://";
    if (!has_prefix(uri, plain_prefix)) return BK_FAIL;
    const char *start = uri + sizeof(plain_prefix) - 1U;
    const char *colon = strrchr(start, ':');
    const char *end = strchr(start, '/');
    if (end == NULL) end = start + strlen(start);
    size_t host_size = (colon != NULL && colon < end) ?
        (size_t)(colon - start) : (size_t)(end - start);
    if (host_size == 0U || host_size >= host_capacity) return BK_FAIL;
    memcpy(host, start, host_size);
    host[host_size] = '\0';
    unsigned parsed_port = 1883U;
    if (colon != NULL && colon < end &&
        (sscanf(colon + 1, "%u", &parsed_port) != 1 || parsed_port > UINT16_MAX)) {
        return BK_FAIL;
    }
    *port = (uint16_t)parsed_port;
    return BK_OK;
}

static void close_mqtt_socket(int socket_fd)
{
    if (socket_fd >= 0) close(socket_fd);
}

static int connect_mqtt_socket(const char *uri)
{
    char host[192];
    uint16_t port = 0;
    if (parse_mqtt_uri(uri, host, sizeof(host), &port) != BK_OK) {
        BK_LOGE(TAG, "unsupported MQTT service URI\n");
        return -1;
    }
    char service[8];
    snprintf(service, sizeof(service), "%u", port);
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(host, service, &hints, &addresses) != 0 || addresses == NULL) {
        return -1;
    }
    int socket_fd = socket(addresses->ai_family, addresses->ai_socktype,
                           addresses->ai_protocol);
    if (socket_fd >= 0 &&
        connect(socket_fd, addresses->ai_addr, addresses->ai_addrlen) != 0) {
        close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    if (socket_fd >= 0) {
        struct timeval timeout = {
            .tv_sec = BIND_RECV_TIMEOUT_MS / 1000,
            .tv_usec = (BIND_RECV_TIMEOUT_MS % 1000) * 1000,
        };
        (void)setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
    }
    return socket_fd;
}

static int wait_for_auth_grant(const platform_services_t *services,
                               const provision_report_t *report,
                               xiaotai_device_credentials_t *out)
{
    int socket_fd = connect_mqtt_socket(services->mqtt);
    if (socket_fd < 0) return BK_FAIL;
    uint8_t packet[MQTT_PACKET_MAX];
    uint8_t header = 0;
    size_t size = 0;
    int rc = mqtt_send_connect(socket_fd, report);
    if (rc != BK_OK || mqtt_read_frame(socket_fd, &header, packet,
                                       sizeof(packet), &size) <= 0 ||
        (header >> 4) != 2U || size != 2U || packet[1] != 0U) {
        rc = BK_FAIL;
        goto done;
    }
    char command_topic[MQTT_TOPIC_MAX];
    char ack_topic[MQTT_TOPIC_MAX];
    if (snprintf(command_topic, sizeof(command_topic), "device/%s/cmd",
                 report->temp_client_id) >= (int)sizeof(command_topic) ||
        snprintf(ack_topic, sizeof(ack_topic), "device/%s/ack",
                 report->temp_client_id) >= (int)sizeof(ack_topic) ||
        mqtt_send_subscribe(socket_fd, command_topic) != BK_OK) {
        rc = BK_FAIL;
        goto done;
    }

    uint32_t start = rtos_get_time();
    uint32_t last_activity = start;
    bool subscribed = false;
    bool ack_pending = false;
    rc = BK_FAIL;
    while ((uint32_t)(rtos_get_time() - start) <
           XIAOTAI_BIND_TIMEOUT_SECONDS * 1000U) {
        int read_rc = mqtt_read_frame(socket_fd, &header, packet,
                                      sizeof(packet), &size);
        if (read_rc == 0) {
            if ((uint32_t)(rtos_get_time() - last_activity) > 30000U) {
                if (mqtt_send_frame(socket_fd, 0xC0U, NULL, 0U) != BK_OK) {
                    break;
                }
                last_activity = rtos_get_time();
            }
            continue;
        }
        if (read_rc < 0) break;
        last_activity = rtos_get_time();
        uint8_t type = header >> 4;
        if (type == 9U && size >= 3U && packet[0] == 0U && packet[1] == 1U &&
            packet[2] != 0x80U) {
            subscribed = true;
            snprintf(s_verification_code, sizeof(s_verification_code), "%s",
                     report->code);
            BK_LOGW(TAG,
                    "binding MQTT subscribed; verification code=%s\n",
                    s_verification_code);
            xiaotai_ui_show_verification_code(s_verification_code);
            int16_t *tts_pcm = NULL;
            size_t tts_samples = 0U;
            int tts_rc = download_verification_tts(services, report,
                                                   &tts_pcm, &tts_samples);
            if (tts_rc == BK_OK) {
                int prompt_rc = xiaotai_audio_announce_verification_pcm(
                    tts_pcm, tts_samples);
                if (prompt_rc != BK_OK) {
                    BK_LOGW(TAG, "server verification prompt unavailable rc=%d\n",
                            prompt_rc);
                    memset(tts_pcm, 0, tts_samples * sizeof(*tts_pcm));
                    psram_free(tts_pcm);
                    xiaotai_ui_show_verification_audio_error(
                        s_verification_code);
                }
            } else {
                BK_LOGW(TAG,
                        "server verification TTS unavailable rc=%d; no local fallback\n",
                        tts_rc);
                xiaotai_ui_show_verification_audio_error(s_verification_code);
            }
        } else if (type == 3U && subscribed) {
            if (size < 2U) continue;
            size_t topic_size = ((size_t)packet[0] << 8) | packet[1];
            size_t offset = 2U + topic_size;
            unsigned qos = (header >> 1) & 3U;
            uint16_t incoming_id = 0;
            if (topic_size == 0U || offset > size) continue;
            if (qos > 0U) {
                if (offset + 2U > size) continue;
                incoming_id = (uint16_t)(((uint16_t)packet[offset] << 8) |
                                         packet[offset + 1U]);
                offset += 2U;
            }
            if (qos == 1U) {
                uint8_t puback[2] = {(uint8_t)(incoming_id >> 8),
                                     (uint8_t)incoming_id};
                (void)mqtt_send_frame(socket_fd, 0x40U, puback, sizeof(puback));
            }
            xiaotai_device_credentials_t granted = {0};
            if (!ack_pending && parse_auth_grant(packet + offset, size - offset,
                                                  &granted)) {
                /* TiRTC CLIENT_ID identifies the physical unit independently
                 * of its cloud device id.  The Linux C reference passes the
                 * board MAC here; using device_id changes the SDK identity
                 * used by active WHIP authentication. */
                if (xiaotai_platform_client_id(granted.client_id,
                                               sizeof(granted.client_id)) !=
                    BK_OK) {
                    break;
                }
                *out = granted;
                if (mqtt_send_publish_ack(socket_fd, ack_topic) != BK_OK) break;
                ack_pending = true;
            }
        } else if (type == 4U && ack_pending && size == 2U &&
                   packet[0] == 0U && packet[1] == 2U) {
            BK_LOGI(TAG, "binding ACK delivered\n");
            rc = BK_OK;
            break;
        }
    }
done:
    (void)mqtt_send_frame(socket_fd, 0xE0U, NULL, 0U);
    close_mqtt_socket(socket_fd);
    return rc;
}

static int synchronize_clock(void)
{
    /* AON RTC survives warm resets, so a plausible epoch does not prove that
     * this boot has synchronized with the network.  The token signature is
     * rejected when the clock differs from the server by more than 300 s. */
    if (s_clock_synchronized) return BK_OK;
    time_t previous = xiaotai_time_now();
    static const char *servers[] = {"pool.ntp.org", "ntp.aliyun.com"};
    for (size_t attempt = 0; attempt < sizeof(servers) / sizeof(servers[0]); ++attempt) {
        struct addrinfo hints = {0};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo *address = NULL;
        if (getaddrinfo(servers[attempt], "123", &hints, &address) != 0 ||
            address == NULL) {
            continue;
        }
        int fd = socket(address->ai_family, address->ai_socktype,
                        address->ai_protocol);
        if (fd < 0) {
            freeaddrinfo(address);
            continue;
        }
        struct timeval timeout = {.tv_sec = 2, .tv_usec = 0};
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        uint8_t packet[48] = {0};
        packet[0] = 0x23U; /* LI=0, NTPv4, client. */
        int sent = sendto(fd, packet, sizeof(packet), 0, address->ai_addr,
                          address->ai_addrlen);
        int received = sent == (int)sizeof(packet) ?
            recv(fd, packet, sizeof(packet), 0) : -1;
        close(fd);
        freeaddrinfo(address);
        if (received < (int)sizeof(packet)) continue;
        uint32_t ntp_seconds = ((uint32_t)packet[40] << 24) |
                               ((uint32_t)packet[41] << 16) |
                               ((uint32_t)packet[42] << 8) |
                               packet[43];
        if (ntp_seconds <= NTP_UNIX_EPOCH_DELTA) continue;
        struct timeval now = {
            .tv_sec = (time_t)(ntp_seconds - NTP_UNIX_EPOCH_DELTA),
            .tv_usec = 0,
        };
        if (now.tv_sec < 1700000000 || bk_rtc_settimeofday(&now, NULL) != BK_OK) {
            continue;
        }
        time_t verified = xiaotai_time_now();
        if (verified < now.tv_sec || verified > now.tv_sec + 2) {
            BK_LOGE(TAG, "network clock verification failed unix=%lld\n",
                    (long long)verified);
            continue;
        }
        s_clock_synchronized = true;
        BK_LOGI(TAG,
                "network clock synchronized unix=%lld previous=%lld delta=%lld\n",
                (long long)verified, (long long)previous,
                (long long)(verified - previous));
        return BK_OK;
    }
    BK_LOGE(TAG, "network clock synchronization failed\n");
    return BK_FAIL;
}

static int discover_services_with_network_time(platform_services_t *services)
{
    int rc = synchronize_clock();
    return rc == BK_OK ? discover_services(services) : rc;
}

static int hmac_signature(const char *key, const char *text,
                          char *output, size_t output_capacity)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint8_t digest[32];
    size_t key_size = strlen(key);
    size_t text_size = strlen(text);
    if (key_size > UINT32_MAX || text_size > UINT32_MAX ||
        output_capacity < 45U) {
        return BK_FAIL;
    }
    HMAC_SHA256_CTX context;
    hmac_sha256_init(&context, (const uint8_t *)key, (uint32_t)key_size);
    hmac_sha256_update(&context, (const uint8_t *)text, (uint32_t)text_size);
    hmac_sha256_final(&context, digest);

    size_t input = 0U;
    size_t encoded = 0U;
    while (input + 3U <= sizeof(digest)) {
        uint32_t value = ((uint32_t)digest[input] << 16) |
                         ((uint32_t)digest[input + 1U] << 8) |
                         digest[input + 2U];
        output[encoded++] = alphabet[(value >> 18) & 0x3fU];
        output[encoded++] = alphabet[(value >> 12) & 0x3fU];
        output[encoded++] = alphabet[(value >> 6) & 0x3fU];
        output[encoded++] = alphabet[value & 0x3fU];
        input += 3U;
    }
    if (input < sizeof(digest)) {
        uint32_t value = (uint32_t)digest[input] << 16;
        output[encoded++] = alphabet[(value >> 18) & 0x3fU];
        if (input + 1U < sizeof(digest)) {
            value |= (uint32_t)digest[input + 1U] << 8;
            output[encoded++] = alphabet[(value >> 12) & 0x3fU];
            output[encoded++] = alphabet[(value >> 6) & 0x3fU];
        } else {
            output[encoded++] = alphabet[(value >> 12) & 0x3fU];
            output[encoded++] = '=';
        }
        output[encoded++] = '=';
    }
    output[encoded] = '\0';
    memset(&context, 0, sizeof(context));
    memset(digest, 0, sizeof(digest));
    return BK_OK;
}

static int obtain_device_token(const platform_services_t *services,
                               const xiaotai_device_credentials_t *credentials,
                               char *token, size_t token_capacity)
{
    char mac_text[18];
    if (format_device_mac(mac_text) != BK_OK) return BK_FAIL;
    char timestamp[24];
    snprintf(timestamp, sizeof(timestamp), "%lld",
             (long long)xiaotai_time_now());
    BK_LOGI(TAG, "device token request timestamp=%s mac=%s\n",
            timestamp, mac_text);
    uint8_t random[8] = {0};
    if (bk_fill_rand(random, sizeof(random)) != BK_OK) {
        uint32_t fallback = rtos_get_time();
        memcpy(random, &fallback, sizeof(fallback));
    }
    char nonce[17];
    snprintf(nonce, sizeof(nonce),
             "%02x%02x%02x%02x%02x%02x%02x%02x",
             random[0], random[1], random[2], random[3],
             random[4], random[5], random[6], random[7]);
    char signed_text[384];
    int signed_size = snprintf(signed_text, sizeof(signed_text), "%s%s%s",
                               credentials->device_id, timestamp, nonce);
    char signature[64];
    if (signed_size <= 0 || (size_t)signed_size >= sizeof(signed_text) ||
        hmac_signature(credentials->device_secret, signed_text, signature,
                       sizeof(signature)) != BK_OK) {
        return BK_FAIL;
    }

    char url[320];
    if (snprintf(url, sizeof(url), "%s/v1/device/token", services->device) >=
        (int)sizeof(url)) {
        return BK_FAIL;
    }
    const char *names[] = {
        "X-Device-Id", "X-Timestamp", "X-Nonce", "X-Mac", "X-Signature",
    };
    const char *values[] = {
        credentials->device_id, timestamp, nonce, mac_text, signature,
    };
    char *response = NULL;
    size_t response_size = 0;
    /* Match the ESP product contract exactly: POST with a zero-length body.
     * BK-AVDK's webclient needs an explicit response-header pass after its
     * NULL-body POST because webclient_post() only sends the request header. */
    int http_status = 0;
    int rc = http_json_request_ex(url, HTTP_JSON_POST_EMPTY, NULL, NULL,
                                  names, values,
                                  sizeof(names) / sizeof(names[0]),
                                  &response, &response_size, &http_status);
    (void)response_size;
    memset(signature, 0, sizeof(signature));
    BK_LOGD(TAG, "device token JSON parse begin bytes=%u\n",
            (unsigned)response_size);
    cJSON *root = response == NULL ? NULL : cJSON_Parse(response);
    BK_LOGD(TAG, "device token JSON parse complete root=%d\n",
            root != NULL ? 1 : 0);
    if (response != NULL) {
        BK_LOGD(TAG, "device token response release begin\n");
        os_free(response);
        BK_LOGD(TAG, "device token response release complete\n");
    }
    const cJSON *code = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *message = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "msg");
    const cJSON *data = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "data");
    int platform_code = cJSON_IsNumber(code) ? code->valueint : -1;
    bool valid = rc == BK_OK &&
                 (platform_code == 0 || platform_code == 200) &&
                 cJSON_IsObject(data) &&
                 copy_json_string(data, "mqtt_token", token, token_capacity);
    if (!valid) {
        size_t message_length =
            cJSON_IsString(message) && message->valuestring != NULL
                ? strlen(message->valuestring) : 0U;
        BK_LOGE(TAG,
                "device token rejected http=%d code=%d message-bytes=%u\n",
                http_status, platform_code, (unsigned)message_length);
    }
    BK_LOGD(TAG, "device token JSON release begin\n");
    cJSON_Delete(root);
    BK_LOGD(TAG, "device token JSON release complete\n");
    if (!valid) return BK_FAIL;
    BK_LOGI(TAG, "device token obtained (value hidden)\n");
    return BK_OK;
}

static int report_capabilities_with_token(const platform_services_t *services,
                                          const char *token)
{
    char url[320];
    if (snprintf(url, sizeof(url), "%s/v1/device/profile", services->device) >=
        (int)sizeof(url)) {
        return BK_FAIL;
    }
    char *response = NULL;
    size_t response_size = 0;
    int http_status = 0;
    int rc = http_json_request_ex(url, HTTP_JSON_POST, s_device_profile,
                                  token, NULL, NULL, 0,
                                  &response, &response_size, &http_status);
    (void)response_size;
    BK_LOGD(TAG, "device profile JSON parse begin bytes=%u\n",
            (unsigned)response_size);
    cJSON *root = response == NULL ? NULL : cJSON_Parse(response);
    BK_LOGD(TAG, "device profile JSON parse complete root=%d\n",
            root != NULL ? 1 : 0);
    if (response != NULL) {
        BK_LOGD(TAG, "device profile response release begin\n");
        os_free(response);
        BK_LOGD(TAG, "device profile response release complete\n");
    }
    const cJSON *code = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "code");
    const cJSON *message = root == NULL ? NULL :
        cJSON_GetObjectItemCaseSensitive(root, "msg");
    int platform_code = cJSON_IsNumber(code) ? code->valueint : -1;
    bool accepted = rc == BK_OK && cJSON_IsNumber(code) &&
                    (platform_code == 0 || platform_code == 200);
    if (!accepted) {
        size_t message_length =
            cJSON_IsString(message) && message->valuestring != NULL
                ? strlen(message->valuestring) : 0U;
        BK_LOGE(TAG,
                "device profile rejected http=%d code=%d message-bytes=%u\n",
                http_status, platform_code, (unsigned)message_length);
        if (message_length > 0U) {
            BK_LOGD(TAG, "device profile rejected message=%s\n",
                    message->valuestring);
        }
    }
    BK_LOGD(TAG, "device profile JSON release begin\n");
    cJSON_Delete(root);
    BK_LOGD(TAG, "device profile JSON release complete\n");
    if (!accepted) return BK_FAIL;
    BK_LOGI(TAG,
            "device capabilities reported (stream=h264, call/voip=audio-only)\n");
    return BK_OK;
}

int xiaotai_platform_report_capabilities(
    const xiaotai_device_credentials_t *credentials)
{
    if (credentials == NULL || credentials->device_id[0] == '\0' ||
        credentials->device_secret[0] == '\0') {
        return BK_ERR_PARAM;
    }
    platform_services_t services = {0};
    char token[MQTT_TOKEN_MAX] = {0};
    int rc = discover_services_with_network_time(&services);
    if (rc == BK_OK) {
        rc = obtain_device_token(&services, credentials, token, sizeof(token));
    }
    if (rc == BK_OK) rc = report_capabilities_with_token(&services, token);
    if (rc == BK_OK) rc = cache_services(&services, token);
    memset(token, 0, sizeof(token));
    return rc;
}

static int mqtt_wait_connack(int socket_fd, uint8_t *packet, size_t capacity)
{
    uint32_t deadline = rtos_get_time() + 10000U;
    while ((int32_t)(deadline - rtos_get_time()) > 0) {
        uint8_t header = 0;
        size_t size = 0;
        int rc = mqtt_read_frame(socket_fd, &header, packet, capacity, &size);
        if (rc == 0) continue;
        if (rc < 0) return BK_FAIL;
        if ((header >> 4) == 2U && size == 2U) {
            BK_LOGD(TAG, "MQTT CONNACK flags=%u return_code=%u\n",
                    packet[0], packet[1]);
            if (packet[1] == 0U) return BK_OK;
        }
        return BK_FAIL;
    }
    return BK_FAIL;
}

static int mqtt_handle_online_publish(
    int socket_fd, uint8_t header, uint8_t *packet, size_t size,
    const char *command_topic, const char *notify_topic, const char *ack_topic,
    uint16_t *outgoing_id, xiaotai_platform_signal_fn signal, void *context)
{
    if (size < 2U) return BK_FAIL;
    size_t topic_size = ((size_t)packet[0] << 8) | packet[1];
    size_t offset = 2U + topic_size;
    unsigned qos = (header >> 1) & 3U;
    if (topic_size == 0U || offset > size || qos > 1U) return BK_FAIL;
    uint16_t incoming_id = 0;
    if (qos == 1U) {
        if (offset + 2U > size) return BK_FAIL;
        incoming_id = (uint16_t)(((uint16_t)packet[offset] << 8) |
                                 packet[offset + 1U]);
        offset += 2U;
        uint8_t puback[2] = {(uint8_t)(incoming_id >> 8),
                             (uint8_t)incoming_id};
        if (mqtt_send_frame(socket_fd, 0x40U, puback, sizeof(puback)) != BK_OK) {
            return BK_FAIL;
        }
    }
    bool is_command = strlen(command_topic) == topic_size &&
                      memcmp(packet + 2U, command_topic, topic_size) == 0;
    bool is_notify = strlen(notify_topic) == topic_size &&
                     memcmp(packet + 2U, notify_topic, topic_size) == 0;
    if (!is_command && !is_notify) return BK_OK;
    BK_LOGD(TAG, "MQTT RECEIVE topic=%.*s packet_id=%u qos=%u payload-bytes=%u\n",
            (int)topic_size, (const char *)packet + 2U,
            (unsigned)incoming_id, qos,
            (unsigned)(size - offset));
    if (is_command) {
        (*outgoing_id)++;
        if (*outgoing_id == 0U) *outgoing_id = 1U;
        if (mqtt_send_publish(socket_fd, ack_topic, "{\"ack\":true}",
                              *outgoing_id, true) != BK_OK) {
            return BK_FAIL;
        }
    }
    if (signal != NULL && offset < size) {
        signal((const char *)packet + offset, size - offset, context);
    }
    return BK_OK;
}

static int mqtt_online_session(const platform_services_t *services,
                               const xiaotai_device_credentials_t *credentials,
                               const char *token,
                               xiaotai_platform_signal_fn signal,
                               void *context)
{
    char client_id[80];
    char command_topic[MQTT_TOPIC_MAX];
    char notify_topic[MQTT_TOPIC_MAX];
    char ack_topic[MQTT_TOPIC_MAX];
    char up_topic[MQTT_TOPIC_MAX];
    if (snprintf(client_id, sizeof(client_id), "sn_%s", credentials->device_id) >=
            (int)sizeof(client_id) ||
        snprintf(command_topic, sizeof(command_topic), "device/sn_%s/cmd",
                 credentials->device_id) >= (int)sizeof(command_topic) ||
        snprintf(notify_topic, sizeof(notify_topic), "device/sn_%s/notify",
                 credentials->device_id) >= (int)sizeof(notify_topic) ||
        snprintf(ack_topic, sizeof(ack_topic), "device/sn_%s/ack",
                 credentials->device_id) >= (int)sizeof(ack_topic) ||
        snprintf(up_topic, sizeof(up_topic), "device/sn_%s/up",
                 credentials->device_id) >= (int)sizeof(up_topic)) {
        return BK_ERR_PARAM;
    }

    int socket_fd = connect_mqtt_socket(services->mqtt);
    if (socket_fd < 0) return BK_FAIL;
    uint8_t packet[MQTT_PACKET_MAX];
    int rc = mqtt_send_connect_auth(socket_fd, client_id,
                                    credentials->device_id, token);
    if (rc != BK_OK || mqtt_wait_connack(socket_fd, packet,
                                         sizeof(packet)) != BK_OK ||
        mqtt_send_subscribe_id(socket_fd, command_topic, 1U) != BK_OK ||
        mqtt_send_subscribe_id(socket_fd, notify_topic, 2U) != BK_OK) {
        rc = BK_FAIL;
        goto done;
    }

    bool command_ready = false;
    bool notify_ready = false;
    uint16_t outgoing_id = 10U;
    rc = BK_FAIL;
    uint32_t subscribe_deadline = rtos_get_time() + 10000U;
    while ((!command_ready || !notify_ready) &&
           (int32_t)(subscribe_deadline - rtos_get_time()) > 0) {
        uint8_t header = 0;
        size_t size = 0;
        int read_rc = mqtt_read_frame(socket_fd, &header, packet,
                                      sizeof(packet), &size);
        if (read_rc == 0) continue;
        if (read_rc < 0) goto done;
        if ((header >> 4) == 9U && size >= 3U && packet[2] != 0x80U) {
            uint16_t id = (uint16_t)(((uint16_t)packet[0] << 8) | packet[1]);
            BK_LOGD(TAG, "MQTT SUBACK packet_id=%u return_code=%u\n",
                    (unsigned)id, packet[2]);
            if (id == 1U) command_ready = true;
            if (id == 2U) notify_ready = true;
        } else if ((header >> 4) == 3U &&
                   mqtt_handle_online_publish(socket_fd, header, packet, size,
                                              command_topic, notify_topic,
                                              ack_topic, &outgoing_id, signal,
                                              context) != BK_OK) {
            goto done;
        }
    }
    if (!command_ready || !notify_ready) goto done;
    BK_LOGI(TAG, "MQTT connected and device topics subscribed\n");

    if (report_capabilities_with_token(services, token) != BK_OK) {
        BK_LOGW(TAG, "online capability report failed; retry on reconnect\n");
    }
    uint32_t last_tx = rtos_get_time();
    unsigned heartbeat_sequence = 0U;
    for (;;) {
        uint8_t header = 0;
        size_t size = 0;
        int read_rc = mqtt_read_frame(socket_fd, &header, packet,
                                      sizeof(packet), &size);
        if (read_rc < 0) break;
        if (read_rc > 0 && (header >> 4) == 3U &&
            mqtt_handle_online_publish(socket_fd, header, packet, size,
                                       command_topic, notify_topic, ack_topic,
                                       &outgoing_id, signal, context) != BK_OK) {
            break;
        }
        if ((uint32_t)(rtos_get_time() - last_tx) >= 30000U) {
            char heartbeat[128];
            int count = snprintf(heartbeat, sizeof(heartbeat),
                                 "{\"type\":\"heartbeat\",\"seq\":%u,\"ts\":%lld}",
                                 ++heartbeat_sequence,
                                 (long long)xiaotai_time_now());
            if (count <= 0 || (size_t)count >= sizeof(heartbeat) ||
                mqtt_send_publish(socket_fd, up_topic, heartbeat, 0U, false) != BK_OK) {
                break;
            }
            last_tx = rtos_get_time();
        }
    }
    rc = BK_FAIL;
done:
    (void)mqtt_send_frame(socket_fd, 0xE0U, NULL, 0U);
    close_mqtt_socket(socket_fd);
    return rc;
}

int xiaotai_platform_run(const xiaotai_device_credentials_t *credentials,
                         xiaotai_platform_signal_fn signal,
                         void *context)
{
    if (credentials == NULL || credentials->device_id[0] == '\0' ||
        credentials->device_secret[0] == '\0') {
        return BK_ERR_PARAM;
    }
    bool first_session = true;
    for (;;) {
        while (!xiaotai_network_ready()) rtos_delay_milliseconds(500);
        platform_services_t services = {0};
        char token[MQTT_TOKEN_MAX] = {0};
        int rc = BK_FAIL;
        if (first_session &&
            copy_cached_session(&services, token, sizeof(token))) {
            /* Platform preparation immediately precedes the first online
             * session. Reuse its verified discovery result and fresh token
             * instead of paying for two more TLS handshakes at boot. */
            rc = BK_OK;
            BK_LOGI(TAG, "reusing prepared platform session for MQTT\n");
        } else {
            rc = discover_services_with_network_time(&services);
            if (rc == BK_OK) {
                rc = obtain_device_token(&services, credentials, token,
                                         sizeof(token));
            }
            if (rc == BK_OK) rc = cache_services(&services, token);
        }
        first_session = false;
        if (rc == BK_OK) {
            rc = mqtt_online_session(&services, credentials, token,
                                     signal, context);
        }
        memset(token, 0, sizeof(token));
        BK_LOGW(TAG, "platform channel offline rc=%d; reconnecting\n", rc);
        rtos_delay_milliseconds(3000);
    }
}

int xiaotai_platform_bind(xiaotai_device_credentials_t *out)
{
    if (out == NULL || s_binding) return BK_ERR_PARAM;
    memset(out, 0, sizeof(*out));
    platform_services_t services = {0};
    provision_report_t report = {0};
    s_binding = true;
    int rc = discover_services_with_network_time(&services);
    if (rc == BK_OK) rc = report_device(&services, &report);
    if (rc == BK_OK) {
        rc = wait_for_auth_grant(&services, &report, out);
    }
    xiaotai_audio_cancel_prompt();
    if (rc == BK_OK) xiaotai_ui_show_status("BOUND");
    memset(&report, 0, sizeof(report));
    memset(s_verification_code, 0, sizeof(s_verification_code));
    s_binding = false;
    if (rc == BK_OK) BK_LOGI(TAG, "verification binding completed\n");
    return rc;
}

bool xiaotai_platform_binding_active(void)
{
    return s_binding;
}

const char *xiaotai_platform_verification_code(void)
{
    return s_verification_code;
}
