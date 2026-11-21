#ifndef XIAOTAI_CONFIG_STORE_H
#define XIAOTAI_CONFIG_STORE_H
#include <stdbool.h>
/* Nonblocking admission for every persistent configuration writer. */
bool config_store_write_begin(void);
void config_store_write_end(void);
/* Reset owns all stores exclusively; success freezes other writers until reboot.
 * Failure releases admission so the user can retry. No spinlock or critical section spans flash I/O. */
bool config_store_reset_begin(void);
void config_store_reset_end(bool success);
#endif
