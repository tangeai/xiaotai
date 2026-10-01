#ifndef WIFI_CAPTIVE_DNS_H
#define WIFI_CAPTIVE_DNS_H

#include <stdint.h>

#include "esp_err.h"

/** Start the provisioning-only wildcard DNS responder on UDP port 53. */
esp_err_t wifi_captive_dns_start(uint32_t captive_portal_ip);

#endif
