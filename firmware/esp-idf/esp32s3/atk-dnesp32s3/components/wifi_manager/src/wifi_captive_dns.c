/*
 * Minimal provisioning-only DNS responder.
 *
 * It accepts one-question IN/A queries and answers every name with the fixed
 * SoftAP address. Malformed, compressed-question, multi-question and non-A
 * requests are ignored or returned without an answer instead of being parsed
 * past their received bounds.
 */
#include "wifi_captive_dns.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define DNS_PORT 53
#define DNS_HEADER_SIZE 12U
#define DNS_ANSWER_SIZE 16U
#define DNS_QUERY_MAX 480U
#define DNS_REPLY_MAX (DNS_QUERY_MAX + DNS_ANSWER_SIZE)
#define DNS_FLAG_RESPONSE 0x8000U
#define DNS_FLAG_OPCODE 0x7800U
#define DNS_FLAG_AUTHORITATIVE 0x0400U
#define DNS_FLAG_RECURSION_DESIRED 0x0100U
#define DNS_TYPE_A 1U
#define DNS_CLASS_IN 1U
#define DNS_ANSWER_TTL_SECONDS 60U

static const char *TAG = "wifi_captive_dns";
static TaskHandle_t s_dns_task;
static int s_dns_socket = -1;
static uint32_t s_captive_portal_ip;

static uint16_t read_u16(const uint8_t *buffer)
{
    return (uint16_t)(((uint16_t)buffer[0] << 8U) | buffer[1]);
}

static void write_u16(uint8_t *buffer, uint16_t value)
{
    buffer[0] = (uint8_t)(value >> 8U);
    buffer[1] = (uint8_t)value;
}

static void write_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value >> 24U);
    buffer[1] = (uint8_t)(value >> 16U);
    buffer[2] = (uint8_t)(value >> 8U);
    buffer[3] = (uint8_t)value;
}

static size_t question_end(const uint8_t *request, size_t request_length)
{
    size_t offset = DNS_HEADER_SIZE;
    while (offset < request_length) {
        uint8_t label_length = request[offset++];
        if (label_length == 0U) {
            return offset + 4U <= request_length ? offset + 4U : 0U;
        }
        if ((label_length & 0xC0U) != 0U || label_length > 63U ||
            offset + label_length > request_length) {
            return 0U;
        }
        offset += label_length;
    }
    return 0U;
}

static size_t build_dns_reply(const uint8_t *request,
                              size_t request_length,
                              uint8_t *reply,
                              size_t reply_capacity,
                              uint32_t captive_portal_ip)
{
    if (request == NULL || reply == NULL || request_length < DNS_HEADER_SIZE ||
        request_length > DNS_QUERY_MAX) {
        return 0U;
    }

    uint16_t request_flags = read_u16(request + 2U);
    if ((request_flags & (DNS_FLAG_RESPONSE | DNS_FLAG_OPCODE)) != 0U ||
        read_u16(request + 4U) != 1U) {
        return 0U;
    }

    size_t question_length = question_end(request, request_length);
    if (question_length == 0U || question_length > reply_capacity) {
        return 0U;
    }

    memcpy(reply, request, question_length);
    uint16_t response_flags = DNS_FLAG_RESPONSE | DNS_FLAG_AUTHORITATIVE |
                              (request_flags & DNS_FLAG_RECURSION_DESIRED);
    write_u16(reply + 2U, response_flags);
    write_u16(reply + 4U, 1U);
    write_u16(reply + 6U, 0U);
    write_u16(reply + 8U, 0U);
    write_u16(reply + 10U, 0U);

    const uint8_t *question = reply + question_length - 4U;
    if (read_u16(question) != DNS_TYPE_A ||
        read_u16(question + 2U) != DNS_CLASS_IN) {
        return question_length;
    }
    if (question_length + DNS_ANSWER_SIZE > reply_capacity) {
        return 0U;
    }

    uint8_t *answer = reply + question_length;
    write_u16(answer, 0xC00CU);
    write_u16(answer + 2U, DNS_TYPE_A);
    write_u16(answer + 4U, DNS_CLASS_IN);
    write_u32(answer + 6U, DNS_ANSWER_TTL_SECONDS);
    write_u16(answer + 10U, 4U);
    memcpy(answer + 12U, &captive_portal_ip, sizeof(captive_portal_ip));
    write_u16(reply + 6U, 1U);
    return question_length + DNS_ANSWER_SIZE;
}

static void dns_task(void *argument)
{
    (void)argument;
    uint8_t request[DNS_QUERY_MAX];
    uint8_t reply[DNS_REPLY_MAX];

    for (;;) {
        struct sockaddr_storage source = {0};
        socklen_t source_length = sizeof(source);
        int received = recvfrom(s_dns_socket,
                                request,
                                sizeof(request),
                                0,
                                (struct sockaddr *)&source,
                                &source_length);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGE(TAG, "DNS receive failed: errno=%d", errno);
            break;
        }

        size_t reply_length = build_dns_reply(request,
                                              (size_t)received,
                                              reply,
                                              sizeof(reply),
                                              s_captive_portal_ip);
        if (reply_length == 0U) {
            continue;
        }
        if (sendto(s_dns_socket,
                   reply,
                   reply_length,
                   0,
                   (struct sockaddr *)&source,
                   source_length) < 0) {
            ESP_LOGW(TAG, "DNS reply failed: errno=%d", errno);
        }
    }

    int socket_fd = s_dns_socket;
    s_dns_socket = -1;
    s_dns_task = NULL;
    if (socket_fd >= 0) {
        close(socket_fd);
    }
    vTaskDelete(NULL);
}

esp_err_t wifi_captive_dns_start(uint32_t captive_portal_ip)
{
    if (s_dns_task != NULL) {
        return ESP_OK;
    }

    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "cannot create DNS socket: errno=%d", errno);
        return ESP_FAIL;
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "cannot bind DNS socket: errno=%d", errno);
        close(socket_fd);
        return ESP_FAIL;
    }

    s_dns_socket = socket_fd;
    s_captive_portal_ip = captive_portal_ip;
    if (xTaskCreate(dns_task,
                    "wifi_captive_dns",
                    4096,
                    NULL,
                    5,
                    &s_dns_task) != pdPASS) {
        close(socket_fd);
        s_dns_socket = -1;
        s_dns_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "wildcard DNS listening on UDP/%d", DNS_PORT);
    return ESP_OK;
}
