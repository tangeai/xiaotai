#ifndef XIAOTAI_BINDING_COUNTDOWN_H
#define XIAOTAI_BINDING_COUNTDOWN_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Read server-issued JWT expiry for display only. This is not authentication.
 * Never start a fresh TTL when an existing report/token is replayed. */
static inline uint32_t xiaotai_binding_token_expiry(const char *token)
{
    if (!token) return 0;
    const char *p = strchr(token, '.');
    if (!p) return 0;
    char payload[1024];
    size_t n = 0;
    unsigned bits = 0, count = 0;
    for (++p; *p && *p != '.'; ++p) {
        unsigned value;
        if (*p >= 'A' && *p <= 'Z') value = (unsigned)(*p - 'A');
        else if (*p >= 'a' && *p <= 'z') value = (unsigned)(*p - 'a') + 26;
        else if (*p >= '0' && *p <= '9') value = (unsigned)(*p - '0') + 52;
        else if (*p == '-') value = 62;
        else if (*p == '_') value = 63;
        else return 0;
        bits = (bits << 6) | value;
        count += 6;
        if (count >= 8) {
            count -= 8;
            if (n + 1 >= sizeof(payload)) return 0;
            payload[n++] = (char)((bits >> count) & 255U);
        }
    }
    if (*p != '.') return 0;
    payload[n] = '\0';
    const char *exp = strstr(payload, "\"exp\"");
    if (!exp) return 0;
    exp += 5;
    while (*exp == ' ' || *exp == '\t') ++exp;
    if (*exp++ != ':') return 0;
    while (*exp == ' ' || *exp == '\t') ++exp;
    uint32_t result = 0;
    if (*exp < '0' || *exp > '9') return 0;
    while (*exp >= '0' && *exp <= '9') {
        unsigned digit = (unsigned)(*exp++ - '0');
        if (result > (UINT32_MAX - digit) / 10U) return 0;
        result = result * 10U + digit;
    }
    while (*exp == ' ' || *exp == '\t') ++exp;
    return *exp == ',' || *exp == '}' ? result : 0;
}

static inline unsigned xiaotai_binding_seconds_left(uint32_t expiry, uint32_t now)
{
    return expiry > now ? expiry - now : 0;
}

static inline void xiaotai_binding_countdown_text(char *out, size_t size,
                                                 unsigned seconds)
{
    if (seconds) (void)snprintf(out, size, "有效期剩余 %u 秒", seconds);
    else (void)snprintf(out, size, "正在刷新验证码…");
}
#endif
