/* Minimal wildcard DNS responder used only by the local Wi-Fi captive portal. */
#include "captive_dns.h"

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
#define DNS_PACKET_MAX 512U
#define DNS_HEADER_BYTES 12U
#define DNS_TASK_STACK_BYTES 4096U
#define DNS_TASK_PRIORITY 4

static const char *TAG = "captive_dns";
static esp_ip4_addr_t s_portal_ip;
static TaskHandle_t s_dns_task;
static int s_dns_socket = -1;

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static size_t make_reply(const uint8_t *request,
                         size_t request_size,
                         uint8_t *reply,
                         size_t reply_capacity)
{
    if (request == NULL || reply == NULL ||
        request_size < DNS_HEADER_BYTES ||
        read_be16(request + 4U) != 1U) {
        return 0U;
    }

    size_t cursor = DNS_HEADER_BYTES;
    for (;;) {
        if (cursor >= request_size) {
            return 0U;
        }
        uint8_t label_size = request[cursor++];
        if (label_size == 0U) {
            break;
        }
        if ((label_size & 0xC0U) != 0U ||
            label_size > 63U || cursor + label_size > request_size) {
            return 0U;
        }
        cursor += label_size;
    }
    if (cursor + 4U > request_size) {
        return 0U;
    }
    uint16_t query_type = read_be16(request + cursor);
    uint16_t query_class = read_be16(request + cursor + 2U);
    size_t question_end = cursor + 4U;
    bool answer_ipv4 = query_type == 1U && query_class == 1U;
    size_t reply_size = question_end + (answer_ipv4 ? 16U : 0U);
    if (reply_size > reply_capacity) {
        return 0U;
    }

    /* Drop optional EDNS records; return one authoritative A answer. */
    memcpy(reply, request, question_end);
    uint16_t request_flags = read_be16(request + 2U);
    write_be16(reply + 2U, (uint16_t)(0x8400U | (request_flags & 0x0100U)));
    write_be16(reply + 4U, 1U);
    write_be16(reply + 6U, answer_ipv4 ? 1U : 0U);
    write_be16(reply + 8U, 0U);
    write_be16(reply + 10U, 0U);

    if (answer_ipv4) {
        uint8_t *answer = reply + question_end;
        write_be16(answer, 0xC00CU);
        write_be16(answer + 2U, 1U);
        write_be16(answer + 4U, 1U);
        write_be32(answer + 6U, 30U);
        write_be16(answer + 10U, 4U);
        memcpy(answer + 12U, &s_portal_ip.addr, 4U);
    }
    return reply_size;
}

static void dns_task(void *argument)
{
    (void)argument;
    uint8_t request[DNS_PACKET_MAX];
    uint8_t reply[DNS_PACKET_MAX];
    for (;;) {
        struct sockaddr_storage source;
        socklen_t source_size = sizeof(source);
        int received = recvfrom(s_dns_socket,
                                request,
                                sizeof(request),
                                0,
                                (struct sockaddr *)&source,
                                &source_size);
        if (received <= 0) {
            ESP_LOGW(TAG, "recvfrom failed: errno=%d", errno);
            continue;
        }
        size_t reply_size = make_reply(request,
                                       (size_t)received,
                                       reply,
                                       sizeof(reply));
        if (reply_size > 0U) {
            (void)sendto(s_dns_socket,
                         reply,
                         reply_size,
                         0,
                         (struct sockaddr *)&source,
                         source_size);
        }
    }
}

esp_err_t captive_dns_start(esp_netif_t *ap_netif)
{
    if (ap_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dns_task != NULL) {
        return ESP_OK;
    }
    esp_netif_ip_info_t ip_info = {0};
    esp_err_t err = esp_netif_get_ip_info(ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "socket failed: errno=%d", errno);
        return ESP_FAIL;
    }
    int reuse = 1;
    (void)setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ESP_LOGE(TAG, "bind port 53 failed: errno=%d", errno);
        close(socket_fd);
        return ESP_FAIL;
    }
    s_portal_ip = ip_info.ip;
    s_dns_socket = socket_fd;
    if (xTaskCreate(dns_task,
                    "captive_dns",
                    DNS_TASK_STACK_BYTES,
                    NULL,
                    DNS_TASK_PRIORITY,
                    &s_dns_task) != pdPASS) {
        close(socket_fd);
        s_dns_socket = -1;
        s_dns_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "wildcard DNS ready on UDP/53");
    return ESP_OK;
}
