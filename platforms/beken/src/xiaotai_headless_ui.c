#include "xiaotai_ui.h"
#if XIAOTAI_DUAL_EYES
#include "xiaotai_eyes_ui.h"
#include <stdio.h>
#endif
#include "xiaotai_log.h"
#include "board_indicator.h"
#include <os/os.h>
#include <stdatomic.h>
#include <string.h>
/* Presentation mode only: startup, ready, incoming, error, setup, binding. */
static atomic_uint s_indicator;
#define TAG "xiaotai_headless"

int xiaotai_ui_init(void)
{
#if XIAOTAI_DUAL_EYES
    int rc=xiaotai_eyes_ui_start();
    if(rc!=0) BK_LOGE(TAG,"eye display unavailable rc=%d; keys/network remain available\n",rc);
#endif
#if XIAOTAI_DUAL_EYES
    BK_LOGI(TAG,"eyes-key: dual eyes and setup text; touch disabled\n");
#else
    BK_LOGI(TAG,"headless-key: LCD/touch disabled; binding uses spoken code\n");
#endif
    return xiaotai_board_indicator_init();
}

void xiaotai_ui_show_status(const char *status)
{
    if (!status) return;
    BK_LOGI(TAG, "status=%s\n", status);
    unsigned mode=0;
    if (strcmp(status,"READY")==0) mode=1;
    else if (strcmp(status,"VOIP CALL")==0) mode=2;
    else if (strcmp(status,"ERROR")==0 || strcmp(status,"TIMEOUT")==0 ||
             strcmp(status,"BUSY")==0 || strcmp(status,"UNSUPPORTED CONTACT")==0) mode=3;
    atomic_store(&s_indicator,mode);
#if XIAOTAI_DUAL_EYES
    if(mode==1) xiaotai_eyes_ui_idle();
    else if(mode==2) xiaotai_eyes_ui_text("微信来电","单击接听\n双击拒接");
    else if(mode==3) xiaotai_eyes_ui_text("操作未完成","请重试\n或查看手机提示");
    else xiaotai_eyes_ui_text("正在连接",status);
#endif
}

void xiaotai_ui_show_call_active(bool wechat, bool microphone_muted)
{
    atomic_store(&s_indicator,2);
#if XIAOTAI_DUAL_EYES
    xiaotai_eyes_ui_text("微信通话中","单击挂断");
#endif
    (void)wechat; (void)microphone_muted;
}

void xiaotai_ui_show_verification_code(const char *code)
{
    atomic_store(&s_indicator,5);
#if XIAOTAI_DUAL_EYES
    char text[80];snprintf(text,sizeof text,"绑定验证码\n%s",code?code:"");
    xiaotai_eyes_ui_text(text,"手机打开\nxiaotai.chat\n添加设备\n输入验证码");
#endif
    BK_LOGI(TAG, "打开 https://xiaotai.chat 添加设备，输入语音播报的验证码；串口备用验证码=%s\n", code ? code : "");
}

void xiaotai_ui_refresh_verification_countdown(void)
{
    unsigned mode=atomic_load(&s_indicator);
    bool blink=((rtos_get_time()/250U)&1U)!=0;
    bool red=mode==3 || (mode==2 && blink) || (mode==4 && blink);
    bool green=mode==1 || mode==2 || ((mode==0 || mode==5) && blink) || (mode==4 && !blink);
    (void)xiaotai_board_indicator_set(red,green);
}

void xiaotai_ui_show_wechat_qr(void)
{
    atomic_store(&s_indicator,3);
    BK_LOGW(TAG, "快捷呼叫不可用：请在 https://xiaotai.chat 绑定微信联系人，并将其设为第一个联系人\n");
}

void xiaotai_ui_show_verification_audio_error(const char *code)
{
    atomic_store(&s_indicator,3);
    (void)code; BK_LOGE(TAG, "验证码播报失败，请检查网络并重试；本次绑定尚未完成\n");
}

void xiaotai_ui_show_home(const char *time_text,
                          const char *date_text,
                          int wifi_rssi, bool clock_face,
                          const char *emotion, bool touch_enabled)
{
    atomic_store(&s_indicator,1);
#if XIAOTAI_DUAL_EYES
    xiaotai_eyes_ui_idle();
#endif
    (void)time_text; (void)date_text; (void)wifi_rssi; (void)clock_face; (void)emotion; (void)touch_enabled;
}

void xiaotai_ui_show_ai(xiaotai_ai_ui_phase_t phase,
                        const char *emotion,
                        const char *caption,
                        bool caption_from_ai,
                        bool emotion_tag_present)
{
#if XIAOTAI_DUAL_EYES
    xiaotai_eyes_ui_tag(emotion,emotion_tag_present,(int)phase);
#endif
    (void)phase; (void)emotion; (void)caption; (void)caption_from_ai; (void)emotion_tag_present;
}

void xiaotai_ui_update_ai_mouth(uint32_t columns)
{
    (void)columns;
}

void xiaotai_ui_show_network_setup(const char *ssid)
{
    atomic_store(&s_indicator,4);
#if XIAOTAI_DUAL_EYES
    char text[96];snprintf(text,sizeof text,"等待配网\n热点名称\n%s",ssid?ssid:"");
    xiaotai_eyes_ui_text(text,"手机连接热点\n浏览器打开\n192.168.4.1");
#endif
    BK_LOGI(TAG, "配网热点=%s；连接热点后打开 192.168.4.1\n", ssid ? ssid : "");
}

void xiaotai_ui_show_launcher(unsigned selected)
{
    (void)selected;
}

void xiaotai_ui_show_ai_history(const char *text, bool from_ai,
                                unsigned page, unsigned count, bool available)
{
    (void)text; (void)from_ai; (void)page; (void)count; (void)available;
}

void xiaotai_ui_show_contacts(const char *const *names,
                              const bool *online, const bool *wechat,
                              size_t count,
                              size_t page, size_t selected)
{
    (void)names; (void)online; (void)wechat; (void)count; (void)page; (void)selected;
}

void xiaotai_ui_show_contact_detail(const char *name, bool online,
                                    bool wechat)
{
    (void)name; (void)online; (void)wechat;
}

void xiaotai_ui_show_expressions(unsigned selected,
                                 const char *active_emotion)
{
    (void)selected; (void)active_emotion;
}

void xiaotai_ui_show_expression_preview(unsigned selected)
{
    (void)selected;
}

void xiaotai_ui_show_reset_confirmation(unsigned status)
{
    (void)status;
}

void xiaotai_ui_show_diagnostics(unsigned tab, const char *details)
{
    (void)tab; (void)details;
}

void xiaotai_ui_show_network(int wifi_rssi, const char *ip_address)
{
    (void)wifi_rssi; (void)ip_address;
}

void xiaotai_ui_show_settings_preview_error(void)
{
    
}

void xiaotai_ui_show_settings(uint8_t volume, bool speaker_muted,
                              bool microphone_muted,
                              uint8_t microphone_sensitivity,
                              const char *screen_timeout, bool acknowledgement_male,
                              bool save_failed)
{
    (void)volume; (void)speaker_muted; (void)microphone_muted; (void)microphone_sensitivity; (void)screen_timeout; (void)acknowledgement_male; (void)save_failed;
}

void xiaotai_ui_show_room(const xiaotai_room_snapshot_t *room, size_t page)
{
    (void)room; (void)page;
}

void xiaotai_ui_show_room_entry(bool request_pending)
{
    (void)request_pending;
}

void xiaotai_ui_show_room_join_code(const char *room_code)
{
    (void)room_code;
}

void xiaotai_ui_show_room_password(bool create, const char *password)
{
    (void)create; (void)password;
}

void xiaotai_ui_show_room_leave_confirm(void)
{
    
}

int xiaotai_ui_set_backlight(bool enabled)
{
    (void)enabled; return -1;
}

bool xiaotai_ui_backlight_on(void)
{
    return false;
}

const void * xiaotai_ui_probe_lcd(void)
{
    return NULL;
}

