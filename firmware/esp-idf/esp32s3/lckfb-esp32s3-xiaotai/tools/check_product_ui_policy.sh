#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$project_dir/tools/repository.sh"
repo_dir="$(xiaotai_find_repository_root "$project_dir")"
source_path=${1:-$repo_dir/platforms/esp-idf/components/starter_product/src/starter_product.c}

fail() {
    echo "product UI policy failed: $1" >&2
    exit 1
}

require_text() {
    local text=$1
    local reason=$2
    rg -Fq "$text" "$source_path" || fail "$reason"
}

home_tap_body=$(sed -n '/static void on_home_tap/,/^}/p' "$source_path")
if rg -Fq 'PAGE_AI_CHAT' <<<"$home_tap_body"; then
    fail 'tapping the home surface must keep the conversation on the expressive home page'
fi
rg -Fq 's_page = PAGE_HOME_FACE;' <<<"$home_tap_body" ||
    fail 'home conversation must render on the expressive home face'

require_text 'render_header(screen, "小钛")' \
    'the confirmed product name 小钛 must appear in the home header'
require_text 'PAGE_EMOJI_PREVIEW' \
    'the 21-item expression library needs a dedicated preview page'
require_text 'ACTION_EMOJI_APPLY' \
    'an expression must only become the home expression after Apply'
require_text 'static const char *const s_emoji_keys[]' \
    'the 21 selectable expressions need stable renderer keys'
require_text '"neutral", "happy", "laughing", "funny", "sad", "angry", "crying"' \
    'the expression catalog must begin with the prototype-defined keys'
require_text '"loving", "embarrassed", "surprised", "shocked", "thinking"' \
    'the expression catalog must include emotional preview variants'
require_text '"winking", "cool", "relaxed", "delicious", "kissy", "confident"' \
    'the expression catalog must include the positive fallback variants'
require_text '"sleepy", "silly", "confused"' \
    'the expression catalog must contain all 21 prototype expressions'

for required in neutral happy sad angry surprised thinking listening ambient speech sleepy; do
    require_text "strcmp(effective, \"$required\")" \
        "required expression '$required' has no explicit renderer geometry"
done

require_text '"正在聆听"' 'listening state copy must follow the product document'
require_text '"AI 正在思考"' 'thinking state copy must follow the product document'
require_text '"AI 回复中"' 'reply state copy must follow the product document'
require_text 'HOME_SUBTITLE_TRANSPARENT' \
    'the current home subtitle must share the home canvas background'
require_text 'HOME_MENU_FLOATING_DOTS' \
    'the 40x40 home menu target must use the minimal floating-dot treatment'
require_text 'HOME_WECHAT_CALL_ICON' \
    'the home WeChat action must be recognizable as a call'
require_text 'HOME_WECHAT_COMPOSITE_BITMAP' \
    'the home WeChat action must use one precomposed bitmap without clipped child glyphs'
require_text 'LV_IMG_CF_ALPHA_4BIT' \
    'the home WeChat bitmap must retain antialiased transparency'
require_text 'lv_img_set_src(icon, &s_wechat_call_icon)' \
    'the complete WeChat-call artwork must be rendered at its native size'
if rg -Fq 'lv_line_set_points(stroke, strokes[i].points, strokes[i].count)' "$source_path"; then
    fail 'the home WeChat receiver must not use visibly segmented LVGL line strokes'
fi
wechat_button_body=$(sed -n '/static lv_obj_t \*make_home_wechat_button/,/^}/p' "$source_path")
if rg -Fq 'lv_label_create(button)' <<<"$wechat_button_body"; then
    fail 'the home WeChat bitmap must not overlay separately clipped phone or 微 labels'
fi
require_text 'HOME_BRAND_ROBOT_MARK' \
    'the home header must carry the 小钛 robot-face product mark'
require_text 'HOME_WIFI_REAL_RSSI' \
    'the header must expose actual Wi-Fi signal strength'
require_text 'HOME_WIFI_SIGNAL_BARS' \
    'Wi-Fi quality must use the confirmed ascending signal-bar icon'
require_text 'HOME_WIFI_ICON_ONLY_RIGHT' \
    'Wi-Fi status must be icon-only and occupy the far-right header slot'
require_text 'lv_obj_set_pos(s_wifi_signal, 292, 5)' \
    'Wi-Fi status must be placed at the far-right edge of the 320px display'
require_text 'HEADER_BACK_NATIVE_CHEVRON' \
    'the header back affordance must not depend on a missing CJK glyph'
require_text 'lv_label_set_text(chevron, LV_SYMBOL_LEFT)' \
    'the header back affordance must use the built-in LVGL symbol font'
require_text 'wifi_manager_signal_dbm(&rssi)' \
    'the signal bars must read RSSI from the Wi-Fi driver'
if rg -Fq 's_wifi_signal_value' "$source_path"; then
    fail 'Wi-Fi status must not render a numeric RSSI value'
fi
require_text '"说“你好小钛”或点击 AI 聊天"' \
    'the home invitation must describe the dedicated wake phrase and manual fallback'
if rg -Fq '你好小钛，和我聊天' "$source_path"; then
    fail 'the home invitation must not require an obsolete follow-up command'
fi
require_text '"小钛在这儿"' 'awake idle copy must feel responsive'
require_text '"发会儿呆…"' 'quiet idle must have a playful daydream stage'
require_text '"...zzZ"' 'long quiet idle must visibly transition to sleepy'
require_text 'PRODUCT_IDLE_DAYDREAM_MS 45000' \
    'daydream must begin after the confirmed 45-second delay'
require_text 'PRODUCT_IDLE_SLEEPY_MS 180000' \
    'sleepy persona must begin after the confirmed 3-minute delay'
require_text 'PRODUCT_UI_TASK_PRIORITY 6' \
    'LVGL must remain below the realtime media workers'
require_text 'PRODUCT_UI_TASK_CORE 0' \
    'LVGL must stay off CPU1, which carries capture and AEC deadlines'
require_text '.task_priority = PRODUCT_UI_TASK_PRIORITY' \
    'LVGL port configuration must use the documented UI priority'
require_text '.task_affinity = PRODUCT_UI_TASK_CORE' \
    'LVGL port configuration must use the documented UI core'
require_text 'PRODUCT_UI_REFRESH_SLOW_US' \
    'UI performance changes require a measured slow-refresh diagnostic'
require_text 'label_set_text_if_changed' \
    'periodic labels must not invalidate unchanged LVGL content'
require_text 'clear_expression_part' \
    'expression animation must not refill the complete PSRAM canvas per frame'
require_text 'refresh_settings_controls' \
    'settings adjustments must update their existing controls'
require_text 'static void enter_settings_page(void)' \
    'settings navigation must centralize its foreground-session policy'
require_text 'static bool page_ends_ai(product_page_t page)' \
    'functional page navigation must share a tested AI-stop policy'
require_text '(void)enter_page(PAGE_SETTINGS);' \
    'settings must use the common navigation policy'
require_text 'PAGE_DIAGNOSTICS' 'runtime diagnostics must have a dedicated page'
require_text 's_diagnostics_due_ms = now + 1000;' \
    'diagnostics must refresh at most once a second during periodic ticks'
require_text 'enter_settings_page();' \
    'all settings entry paths must invoke the AI-stop policy'
require_text 's_settings_volume' \
    'settings must retain a volume label instead of rebuilding the page'
require_text 'CALL_PERSISTENT_CONTROLS' \
    'call mode changes must retain one persistent control tree'
require_text 'refresh_call_controls(runtime, &product)' \
    'runtime call state transitions must update controls in place'
require_text 's_call_accept_button' \
    'incoming-call accept control must remain available across transitions'
require_text 'set_object_visible(s_call_hangup_button, !incoming)' \
    'call controls must switch visibility instead of recreating a page'
require_text 'call ringtone %s' \
    'ringtone state transitions must remain observable during device testing'
require_text 'runtime.state == STARTER_RUNTIME_CALL_CONNECTING' \
    'outgoing ringtone must stop as soon as a call becomes active'

echo 'product UI policy passed'
