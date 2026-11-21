#ifndef XIAOTAI_NTP_RESPONSE_H
#define XIAOTAI_NTP_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* RFC 5905: use only a synchronized server response to this request.
 * The connected UDP socket separately limits the peer address/port.
 * The nonce correlates replies; it does not authenticate network time. */
static inline bool xiaotai_ntp_response_valid(const uint8_t *packet, size_t size,
                                             const uint8_t nonce[8])
{
    if (packet == NULL || nonce == NULL || size < 48) return false;
    unsigned version = (packet[0] >> 3) & 7U;
    if ((packet[0] >> 6) == 3 || (version != 3 && version != 4) ||
        (packet[0] & 7U) != 4 || packet[1] == 0 || packet[1] >= 16 ||
        memcmp(packet + 24, nonce, 8) != 0) return false;
    uint8_t receive = 0, transmit = 0;
    for (unsigned i = 0; i < 8; ++i) {
        receive |= packet[32 + i];
        transmit |= packet[40 + i];
    }
    return receive != 0 && transmit != 0;
}
#endif
