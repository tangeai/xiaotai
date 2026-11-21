#ifndef XIAOTAI_NTP_CONFIG_H
#define XIAOTAI_NTP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/* One DNS hostname or IPv4 literal. NTP uses UDP/123; no URL/port syntax. */
static inline bool xiaotai_ntp_server_valid(const char *server)
{
    if (server == NULL || server[0] == '\0') return false;
    size_t length = 0, label = 0;
    bool numeric = true;
    for (; length < 254 && server[length]; ++length) {
        char c = server[length];
        if (c == '.') {
            if (label == 0 || server[length - 1] == '-') return false;
            label = 0;
            continue;
        }
        bool digit = c >= '0' && c <= '9';
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!digit && !letter && c != '-') return false;
        if (c == '-' && label == 0) return false;
        if (++label > 63) return false;
        if (!digit) numeric = false;
    }
    if (length == 254 || label == 0 || server[length - 1] == '-') return false;
    if (numeric) {
        unsigned octet = 0, parts = 1;
        for (size_t i = 0; i < length; ++i) {
            if (server[i] == '.') { ++parts; octet = 0; }
            else { octet = octet * 10U + (unsigned)(server[i] - '0'); }
            if (octet > 255 || parts > 4) return false;
        }
        if (parts != 4) return false;
    }
    return true;
}

#endif
