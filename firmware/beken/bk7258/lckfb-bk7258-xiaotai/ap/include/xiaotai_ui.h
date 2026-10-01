#ifndef XIAOTAI_UI_H
#define XIAOTAI_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xiaotai_ai_view.h"
#include "xiaotai_room.h"

int xiaotai_ui_init(void);
void xiaotai_ui_show_status(const char *status);
/** Show touch controls for an established one-to-one audio call. */
void xiaotai_ui_show_call_active(bool wechat, bool microphone_muted);
void xiaotai_ui_show_verification_code(const char *code);
/** Show the fixed WeChat mini-program code when no authorized contact exists. */
void xiaotai_ui_show_wechat_qr(void);
/** Keep the binding code visible while reporting that server TTS failed. */
void xiaotai_ui_show_verification_audio_error(const char *code);
void xiaotai_ui_show_home(const char *time_text,
                          const char *date_text,
                          int wifi_rssi, bool clock_face,
                          const char *emotion, bool touch_enabled);
void xiaotai_ui_show_ai(xiaotai_ai_ui_phase_t phase,
                        const char *emotion,
                        const char *caption,
                        bool caption_from_ai);
/** Show the product AP setup page with the actual advertised SSID. */
void xiaotai_ui_show_network_setup(const char *ssid);
void xiaotai_ui_show_launcher(unsigned selected);
void xiaotai_ui_show_contacts(const char *const *names,
                              const bool *online, const bool *wechat,
                              size_t count,
                              size_t page, size_t selected);
void xiaotai_ui_show_contact_detail(const char *name, bool online,
                                    bool wechat);
void xiaotai_ui_show_expressions(unsigned selected,
                                 const char *active_emotion);
void xiaotai_ui_show_network(int wifi_rssi, const char *ip_address);
void xiaotai_ui_show_settings(uint8_t volume, bool speaker_muted,
                              bool microphone_muted,
                              uint8_t microphone_sensitivity,
                              const char *screen_timeout);
void xiaotai_ui_show_room(const xiaotai_room_snapshot_t *room, size_t page);
int xiaotai_ui_set_backlight(bool enabled);
bool xiaotai_ui_backlight_on(void);
const void *xiaotai_ui_probe_lcd(void);

#endif
