#ifndef STARTER_RECOGNITION_H
#define STARTER_RECOGNITION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Runtime owns admission. Network and playback execute in a bounded worker. */
void starter_recognition_init(void);
void starter_recognition_allow(bool allowed);
void starter_recognition_refresh(void);
void starter_recognition_shutdown(void);
/* Recognition HTTP 410 means the binding must be rechecked at startup. */
bool starter_recognition_binding_revoked(void);
void starter_recognition_snapshot(char *text, size_t capacity);
/* Active work remains visible; settled notices may expire on the home screen. */
bool starter_recognition_status_is_active(const char *status);
bool starter_recognition_status_visible(const char *status, int64_t until_ms,
                                        int64_t now_ms);
/* The home switch pauses local recognition; server policy remains authoritative. */
bool starter_recognition_local_enabled(void);
void starter_recognition_set_local_enabled(bool enabled);
bool starter_recognition_effective_enabled(void);
/* Monotonic remaining cooldown for the home icon: 0 when ready, -1 when busy. */
int64_t starter_recognition_cooldown_remaining_ms(void);
#endif
