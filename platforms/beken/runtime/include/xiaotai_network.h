#ifndef XIAOTAI_NETWORK_H
#define XIAOTAI_NETWORK_H

#include <stdbool.h>
#include <stddef.h>

int xiaotai_network_start(void);
bool xiaotai_network_ready(void);
bool xiaotai_network_provisioning(void);
int xiaotai_network_get_ip(char *address, size_t capacity);
/** Retain saved credentials, disconnect STA, and open the AP portal. */
int xiaotai_network_enter_provisioning(void);

#endif
