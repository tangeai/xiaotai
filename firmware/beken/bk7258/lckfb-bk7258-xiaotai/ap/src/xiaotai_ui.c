#include "xiaotai_ui.h"
#include "board_display.h"

#include <common/bk_err.h>
#include "xiaotai_log.h"
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "frame_buffer.h"
#include "xiaotai_metrics.h"
#include "xiaotai_phone_shortcut.h"
#include "xiaotai_ui_assets.h"
#include "xiaotai_ui_hit_test.h"
#define TAG "xiaotai_ui"
#define LCD_WIDTH XIAOTAI_BOARD_DISPLAY_WIDTH
#define LCD_HEIGHT XIAOTAI_BOARD_DISPLAY_HEIGHT

static void centered_text(frame_buffer_t *frame, int y, const char *value,
                          int scale, uint16_t color);
static void pixel(frame_buffer_t *frame, int x, int y, uint16_t color);
static void circle(frame_buffer_t *frame, int center_x, int center_y,
                   int radius, uint16_t color);

static const xiaotai_cjk_glyph_t *cjk_glyph(uint32_t codepoint)
{
    size_t low = 0U;
    size_t high = xiaotai_cjk_glyph_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        uint32_t current = xiaotai_cjk_glyphs[middle].codepoint;
        if (current < codepoint) low = middle + 1U;
        else high = middle;
    }
    return low < xiaotai_cjk_glyph_count &&
           xiaotai_cjk_glyphs[low].codepoint == codepoint ?
           &xiaotai_cjk_glyphs[low] : NULL;
}

static uint32_t utf8_next(const char **cursor)
{
    const uint8_t *input = (const uint8_t *)*cursor;
    if (input[0] < 0x80U) {
        *cursor += 1;
        return input[0];
    }
    if ((input[0] & 0xe0U) == 0xc0U && (input[1] & 0xc0U) == 0x80U) {
        *cursor += 2;
        return ((uint32_t)(input[0] & 0x1fU) << 6) |
               (uint32_t)(input[1] & 0x3fU);
    }
    if ((input[0] & 0xf0U) == 0xe0U &&
        (input[1] & 0xc0U) == 0x80U && (input[2] & 0xc0U) == 0x80U) {
        *cursor += 3;
        return ((uint32_t)(input[0] & 0x0fU) << 12) |
               ((uint32_t)(input[1] & 0x3fU) << 6) |
               (uint32_t)(input[2] & 0x3fU);
    }
    *cursor += 1;
    return 0xfffdU;
}

static void cjk_text(frame_buffer_t *frame, int x, int y, const char *value,
                     uint16_t color)
{
    const char *cursor = value;
    while (cursor != NULL && *cursor != '\0') {
        uint32_t codepoint = utf8_next(&cursor);
        const xiaotai_cjk_glyph_t *glyph_data = cjk_glyph(codepoint);
        if (glyph_data != NULL) {
            for (int row = 0; row < 16; ++row) {
                uint16_t bits = ((uint16_t)glyph_data->bitmap[row * 2] << 8) |
                                glyph_data->bitmap[row * 2 + 1];
                for (int column = 0; column < 16; ++column) {
                    if ((bits & (1U << (15 - column))) != 0U) {
                        pixel(frame, x + column, y + row, color);
                    }
                }
            }
        }
        x += 17;
    }
}

static void centered_cjk_text(frame_buffer_t *frame, int y, const char *value,
                              uint16_t color)
{
    size_t glyphs = 0;
    const char *cursor = value;
    while (cursor != NULL && *cursor != '\0') {
        (void)utf8_next(&cursor);
        glyphs++;
    }
    int width = glyphs == 0U ? 0 : (int)(glyphs * 17U - 1U);
    cjk_text(frame, ((int)LCD_WIDTH - width) / 2, y, value, color);
}

static bk_err_t release_frame(void *arg)
{
    frame_buffer_display_free((frame_buffer_t *)arg);
    return BK_OK;
}

static void pixel(frame_buffer_t *frame, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= (int)LCD_WIDTH || y >= (int)LCD_HEIGHT) return;
    size_t offset = ((size_t)y * LCD_WIDTH + (size_t)x) * 2U;
    frame->frame[offset] = (uint8_t)(color >> 8);
    frame->frame[offset + 1U] = (uint8_t)color;
}

static uint16_t frame_pixel(const frame_buffer_t *frame, int x, int y)
{
    size_t offset = ((size_t)y * LCD_WIDTH + (size_t)x) * 2U;
    return (uint16_t)((uint16_t)frame->frame[offset] << 8) |
           frame->frame[offset + 1U];
}

static uint16_t blend_rgb565(uint16_t foreground, uint16_t background,
                             uint8_t alpha)
{
    unsigned inverse = 255U - alpha;
    unsigned red = ((((foreground >> 11) & 0x1fU) * alpha) +
                    (((background >> 11) & 0x1fU) * inverse) + 127U) / 255U;
    unsigned green = ((((foreground >> 5) & 0x3fU) * alpha) +
                      (((background >> 5) & 0x3fU) * inverse) + 127U) / 255U;
    unsigned blue = (((foreground & 0x1fU) * alpha) +
                     ((background & 0x1fU) * inverse) + 127U) / 255U;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

static void phone_shortcut(frame_buffer_t *frame)
{
    const int origin_x = (int)XIAOTAI_UI_HOME_QUICK_CALL_ICON_X;
    const int origin_y = (int)XIAOTAI_UI_HOME_QUICK_CALL_ICON_Y;
    /* Match the canonical ESP32 treatment: white sits only beneath the
     * transparent handset, while the PNG keeps its antialiased outer edge. */
    circle(frame, origin_x + 22, origin_y + 22, 18, 0xffff);
    for (unsigned y = 0; y < XIAOTAI_PHONE_SHORTCUT_HEIGHT; ++y) {
        for (unsigned x = 0; x < XIAOTAI_PHONE_SHORTCUT_WIDTH; ++x) {
            size_t index = (size_t)y * XIAOTAI_PHONE_SHORTCUT_WIDTH + x;
            uint8_t alpha = xiaotai_phone_shortcut_alpha[index];
            if (alpha == 0U) continue;
            int screen_x = origin_x + (int)x;
            int screen_y = origin_y + (int)y;
            uint16_t color = xiaotai_phone_shortcut_rgb565[index];
            if (alpha != 255U) {
                color = blend_rgb565(color,
                                     frame_pixel(frame, screen_x, screen_y),
                                     alpha);
            }
            pixel(frame, screen_x, screen_y, color);
        }
    }
}

static void rectangle(frame_buffer_t *frame, int x, int y, int width,
                      int height, uint16_t color)
{
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            pixel(frame, x + column, y + row, color);
        }
    }
}

static void rectangle_outline(frame_buffer_t *frame, int x, int y, int width,
                              int height, int thickness, uint16_t color)
{
    rectangle(frame, x, y, width, thickness, color);
    rectangle(frame, x, y + height - thickness, width, thickness, color);
    rectangle(frame, x, y, thickness, height, color);
    rectangle(frame, x + width - thickness, y, thickness, height, color);
}

static void circle(frame_buffer_t *frame, int center_x, int center_y,
                   int radius, uint16_t color)
{
    int radius_squared = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius_squared) {
                pixel(frame, center_x + x, center_y + y, color);
            }
        }
    }
}

static const uint8_t *glyph(char character)
{
    static const uint8_t blank[7] = {0};
    static const uint8_t colon[7] = {0,4,4,0,4,4,0};
    static const uint8_t hyphen[7] = {0,0,0,31,0,0,0};
    static const uint8_t digits[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    };
    if (character >= '0' && character <= '9') return digits[character - '0'];
    if (character >= 'A' && character <= 'Z') return letters[character - 'A'];
    if (character == ':') return colon;
    if (character == '-') return hyphen;
    return blank;
}

static void text(frame_buffer_t *frame, int x, int y, const char *value,
                 int scale, uint16_t color)
{
    while (value != NULL && *value != '\0') {
        unsigned char input = (unsigned char)*value++;
        char character = input < 0x80U ? (char)toupper(input) : ' ';
        const uint8_t *rows = glyph(character);
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < 5; ++column) {
                if ((rows[row] & (1U << (4 - column))) != 0U) {
                    rectangle(frame, x + column * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static int mixed_codepoint_width(uint32_t codepoint, int ascii_scale)
{
    (void)ascii_scale;
    return codepoint < 0x80U ? 9 : 17;
}

static void mixed_codepoint(frame_buffer_t *frame, int x, int y,
                            uint32_t codepoint, int ascii_scale,
                            uint16_t color)
{
    (void)ascii_scale;
    const xiaotai_cjk_glyph_t *glyph_data = cjk_glyph(codepoint);
    if (glyph_data == NULL) {
        rectangle_outline(frame, x + 2, y + 2, 12, 12, 1, color);
        return;
    }
    for (int row = 0; row < 16; ++row) {
        uint16_t bits = ((uint16_t)glyph_data->bitmap[row * 2] << 8) |
                        glyph_data->bitmap[row * 2 + 1];
        int columns = codepoint < 0x80U ? 8 : 16;
        for (int column = 0; column < columns; ++column) {
            if ((bits & (1U << (15 - column))) != 0U) {
                pixel(frame, x + column, y + row, color);
            }
        }
    }
}

static int mixed_text_width(const char *value, int ascii_scale)
{
    int width = 0;
    const char *cursor = value;
    while (cursor != NULL && *cursor != '\0') {
        width += mixed_codepoint_width(utf8_next(&cursor), ascii_scale);
    }
    return width > 0 ? width - 1 : 0;
}

static void mixed_text_clipped(frame_buffer_t *frame, int x, int y,
                               const char *value, int ascii_scale,
                               uint16_t color, int max_width)
{
    int origin = x;
    const char *cursor = value;
    while (cursor != NULL && *cursor != '\0') {
        uint32_t codepoint = utf8_next(&cursor);
        int advance = mixed_codepoint_width(codepoint, ascii_scale);
        if (max_width > 0 && x + advance - origin > max_width) break;
        mixed_codepoint(frame, x, y, codepoint, ascii_scale, color);
        x += advance;
    }
}

static void mixed_text(frame_buffer_t *frame, int x, int y,
                       const char *value, int ascii_scale, uint16_t color)
{
    mixed_text_clipped(frame, x, y, value, ascii_scale, color, 0);
}

static void centered_mixed_text(frame_buffer_t *frame, int y,
                                const char *value, int ascii_scale,
                                uint16_t color)
{
    int width = mixed_text_width(value, ascii_scale);
    mixed_text_clipped(frame, ((int)LCD_WIDTH - width) / 2, y, value,
                       ascii_scale, color, LCD_WIDTH - 12);
}

static void centered_mixed_in(frame_buffer_t *frame, int center_x, int y,
                              const char *value, int ascii_scale,
                              uint16_t color, int max_width)
{
    int width = mixed_text_width(value, ascii_scale);
    if (width > max_width) width = max_width;
    mixed_text_clipped(frame, center_x - width / 2, y, value, ascii_scale,
                       color, max_width);
}

static void line(frame_buffer_t *frame, int x0, int y0, int x1, int y1,
                 int thickness, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        circle(frame, x0, y0, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static bool emotion_is(const char *emotion, const char *value)
{
    return emotion != NULL && strcmp(emotion, value) == 0;
}

static void ai_face(frame_buffer_t *frame, xiaotai_ai_ui_phase_t phase,
                    const char *emotion, uint16_t accent)
{
    int left_x = 126;
    int right_x = 194;
    int eye_y = 91;
    bool neutral = emotion_is(emotion, "neutral");
    bool sad = emotion_is(emotion, "sad") || emotion_is(emotion, "crying");
    bool angry = emotion_is(emotion, "angry");
    bool wink = emotion_is(emotion, "winking");
    bool silly = emotion_is(emotion, "silly") ||
                 emotion_is(emotion, "funny");
    bool surprised = emotion_is(emotion, "surprised") ||
                     emotion_is(emotion, "shocked");
    bool cool = emotion_is(emotion, "cool");
    bool loving = emotion_is(emotion, "loving");
    bool embarrassed = emotion_is(emotion, "embarrassed");
    bool thinking = emotion_is(emotion, "thinking");
    bool relaxed = emotion_is(emotion, "relaxed") ||
                   emotion_is(emotion, "sleepy");
    bool laughing = emotion_is(emotion, "laughing");
    bool delicious = emotion_is(emotion, "delicious");
    bool kissy = emotion_is(emotion, "kissy");
    bool confident = emotion_is(emotion, "confident");
    bool confused = emotion_is(emotion, "confused");

    if (cool) {
        rectangle(frame, left_x - 23, eye_y - 9, 46, 14, accent);
        rectangle(frame, right_x - 23, eye_y - 9, 46, 14, accent);
        line(frame, left_x + 23, eye_y - 6, right_x - 23, eye_y - 6,
             2, accent);
    } else if (relaxed || laughing) {
        line(frame, left_x - 16, eye_y, left_x, eye_y + 7, 3, accent);
        line(frame, left_x, eye_y + 7, left_x + 16, eye_y, 3, accent);
        line(frame, right_x - 16, eye_y, right_x, eye_y + 7, 3, accent);
        line(frame, right_x, eye_y + 7, right_x + 16, eye_y, 3, accent);
    } else if (sad || angry) {
        line(frame, left_x - 19, eye_y + (sad ? -8 : 4),
             left_x + 19, eye_y + (sad ? 4 : -8), 3, accent);
        line(frame, right_x - 19, eye_y + (sad ? 4 : -8),
             right_x + 19, eye_y + (sad ? -8 : 4), 3, accent);
    } else if (silly) {
        line(frame, left_x - 13, eye_y - 8, left_x + 13, eye_y + 8,
             3, accent);
        line(frame, left_x - 13, eye_y + 8, left_x + 13, eye_y - 8,
             3, accent);
        line(frame, right_x - 13, eye_y - 8, right_x + 13, eye_y + 8,
             3, accent);
        line(frame, right_x - 13, eye_y + 8, right_x + 13, eye_y - 8,
             3, accent);
    } else if (confused) {
        circle(frame, left_x, eye_y - 7, 8, accent);
        circle(frame, right_x, eye_y + 7, 12, accent);
    } else {
        int radius = surprised || phase == XIAOTAI_AI_UI_LISTENING ? 13 : 9;
        circle(frame, left_x, eye_y, radius, accent);
        if (wink) {
            rectangle(frame, right_x - 18, eye_y - 2, 36, 5, accent);
        } else {
            circle(frame, right_x, eye_y, radius, accent);
        }
    }

    if (surprised) {
        circle(frame, 160, 132, 13, accent);
        circle(frame, 160, 132, 7, 0x0000);
    } else if (laughing) {
        rectangle_outline(frame, 137, 120, 46, 26, 4, accent);
        rectangle(frame, 148, 137, 24, 7, 0xf90a);
    } else if (neutral) {
        rectangle(frame, 142, 130, 36, 4, accent);
    } else if (silly) {
        line(frame, 139, 126, 150, 137, 3, accent);
        line(frame, 150, 137, 170, 137, 3, accent);
        line(frame, 170, 137, 181, 126, 3, accent);
        rectangle(frame, 154, 138, 12, 8, 0xf90a);
    } else if (sad || angry) {
        line(frame, 141, 139, 160, 126, 3, accent);
        line(frame, 160, 126, 179, 139, 3, accent);
    } else if (kissy) {
        circle(frame, 160, 132, 8, accent);
        circle(frame, 160, 132, 4, 0x0000);
    } else if (delicious) {
        line(frame, 139, 126, 150, 137, 3, accent);
        line(frame, 150, 137, 170, 137, 3, accent);
        line(frame, 170, 137, 181, 126, 3, accent);
        rectangle(frame, 153, 138, 14, 9, 0xf90a);
    } else if (confident) {
        line(frame, 140, 134, 159, 140, 3, accent);
        line(frame, 159, 140, 181, 126, 3, accent);
    } else if (thinking) {
        circle(frame, 147, 132, 3, accent);
        circle(frame, 160, 132, 3, accent);
        circle(frame, 173, 132, 3, accent);
    } else if (phase == XIAOTAI_AI_UI_SPEAKING) {
        rectangle_outline(frame, 138, 121, 44, 22, 4, accent);
    } else if (phase == XIAOTAI_AI_UI_THINKING) {
        circle(frame, 147, 132, 3, accent);
        circle(frame, 160, 132, 3, accent);
        circle(frame, 173, 132, 3, accent);
    } else {
        line(frame, 139, 127, 150, 138, 3, accent);
        line(frame, 150, 138, 170, 138, 3, accent);
        line(frame, 170, 138, 181, 127, 3, accent);
    }
    if (emotion_is(emotion, "crying")) {
        line(frame, left_x - 8, eye_y + 14, left_x - 8, eye_y + 29,
             3, 0x2d7f);
        line(frame, right_x + 8, eye_y + 14, right_x + 8, eye_y + 29,
             3, 0x2d7f);
    }
    if (loving) {
        circle(frame, left_x - 6, eye_y - 2, 8, 0xf90a);
        circle(frame, left_x + 6, eye_y - 2, 8, 0xf90a);
        circle(frame, right_x - 6, eye_y - 2, 8, 0xf90a);
        circle(frame, right_x + 6, eye_y - 2, 8, 0xf90a);
    }
    if (embarrassed) {
        line(frame, 102, 119, 116, 113, 2, 0xf90a);
        line(frame, 204, 113, 218, 119, 2, 0xf90a);
    }
}

static void caption_text(frame_buffer_t *frame, const char *caption,
                         bool caption_from_ai)
{
    const char *parts[2] = {caption_from_ai ? "AI：" : "你：", caption};
    int x = 18;
    int y = 172;
    int row = 0;
    for (size_t part = 0; part < 2U && row < 2; ++part) {
        const char *cursor = parts[part];
        while (cursor != NULL && *cursor != '\0' && row < 2) {
            uint32_t codepoint = utf8_next(&cursor);
            if (codepoint == '\r') continue;
            if (codepoint == '\n') {
                x = 18;
                y += 30;
                row++;
                continue;
            }
            int width = mixed_codepoint_width(codepoint, 2);
            if (x + width > 302) {
                x = 18;
                y += 30;
                row++;
                if (row >= 2) break;
            }
            mixed_codepoint(frame, x, y, codepoint, 2, 0xffff);
            x += width;
        }
    }
}

void xiaotai_ui_show_ai(xiaotai_ai_ui_phase_t phase,
                        const char *emotion,
                        const char *caption,
                        bool caption_from_ai)
{
    if (!xiaotai_board_display_ready()) return;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) {
        BK_LOGE(TAG, "AI display frame allocation failed\n");
        return;
    }
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0, frame->size);

    const char *phase_text = phase == XIAOTAI_AI_UI_THINKING ? "AI正在思考" :
                             phase == XIAOTAI_AI_UI_SPEAKING ? "AI回复中" :
                                                               "正在聆听";
    uint16_t accent = phase == XIAOTAI_AI_UI_THINKING ? 0xfbe0 :
                      phase == XIAOTAI_AI_UI_SPEAKING ? 0x07e0 : 0x2d7f;
    rectangle(frame, 0, 0, LCD_WIDTH, 34, 0x10a4);
    mixed_text(frame, 12, 8, "小钛", 2, 0xffff);
    int phase_x = 310 - mixed_text_width(phase_text, 1);
    mixed_text(frame, phase_x, 9, phase_text, 1, accent);
    ai_face(frame, phase, emotion, accent);

    rectangle(frame, 8, 158, 304, 74, 0x0000);
    if (caption != NULL && *caption != '\0') {
        caption_text(frame, caption, caption_from_ai);
    } else {
        centered_mixed_text(frame, 184, phase_text, 1, 0xbdf7);
    }
    if (xiaotai_board_display_flush(frame, release_frame) != BK_OK) {
        release_frame(frame);
        BK_LOGE(TAG, "AI display flush failed\n");
    }
}

void xiaotai_ui_show_home(const char *time_text,
                          const char *date_text,
                          int wifi_rssi, bool clock_face,
                          const char *emotion, bool touch_enabled)
{
    if (!xiaotai_board_display_ready()) return;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) {
        BK_LOGE(TAG, "home display frame allocation failed\n");
        return;
    }
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0, frame->size);

    rectangle(frame, 0, 0, LCD_WIDTH, 34, 0x10a4);
    mixed_text(frame, 10, 8, "小钛", 2, 0xffff);
    text(frame, 204, 10, time_text == NULL ? "--:--" : time_text,
         2, 0xffff);
    int bars = wifi_rssi >= -50 ? 4 : wifi_rssi >= -60 ? 3 :
               wifi_rssi >= -70 ? 2 : wifi_rssi > -128 ? 1 : 0;
    uint16_t wifi_color = bars >= 3 ? 0x07e0 : bars == 2 ? 0xfbe0 : 0xf800;
    for (int i = 0; i < 4; ++i) {
        rectangle(frame, 286 + i * 7, 25 - i * 5, 5, 4 + i * 5,
                  i < bars ? wifi_color : 0x3186);
    }

    if (clock_face) {
        centered_text(frame, 73, time_text == NULL ? "--:--" : time_text,
                      7, 0x2d7f);
        centered_text(frame, 145, date_text == NULL ? "" : date_text,
                      3, 0xbdf7);
        if (touch_enabled) {
            centered_mixed_text(frame, 184, "左右滑动切换表盘", 1, 0x7bef);
        }
    } else {
        ai_face(frame, XIAOTAI_AI_UI_SPEAKING,
                emotion == NULL ? "happy" : emotion, 0x2d7f);
        centered_cjk_text(frame, 158, "短按对话", 0x07e0);
        centered_cjk_text(frame, 179, "双击微信呼叫", 0xbdf7);
    }
    if (touch_enabled) {
        if (!clock_face) {
            centered_text(frame, 203, date_text == NULL ? "" : date_text,
                          2, 0xbdf7);
        }
        phone_shortcut(frame);
        circle(frame, 285, 219, 3, 0x2d7f);
        circle(frame, 296, 219, 3, 0x2d7f);
        circle(frame, 307, 219, 3, 0x2d7f);
    } else if (!clock_face) {
        centered_text(frame, 203, date_text == NULL ? "" : date_text,
                      2, 0xbdf7);
    }

    if (xiaotai_board_display_flush(frame, release_frame) != BK_OK) {
        release_frame(frame);
        BK_LOGE(TAG, "home display flush failed\n");
    }
}

static void centered_text(frame_buffer_t *frame, int y, const char *value,
                          int scale, uint16_t color)
{
    size_t length = value == NULL ? 0U : strlen(value);
    int width = length == 0U ? 0 : (int)(length * 6U * (size_t)scale - scale);
    text(frame, ((int)LCD_WIDTH - width) / 2, y, value, scale, color);
}

static const char *status_hint(const char *status)
{
    if (status == NULL || strcmp(status, "BOOT") == 0) return "正在初始化";
    if (strcmp(status, "SETUP") == 0) return "请使用手机完成配网";
    if (strcmp(status, "WIFI") == 0) return "正在连接云端服务";
    if (strcmp(status, "BIND") == 0) return "请在手机端输入验证码";
    if (strcmp(status, "BIND VOICE ERR") == 0) return "请使用屏幕验证码";
    if (strcmp(status, "BOUND") == 0) return "正在进入首页";
    if (strcmp(status, "READY") == 0) return "短按按键开始对话";
    if (strcmp(status, "AI CONNECT") == 0) return "请稍候";
    if (strcmp(status, "AI") == 0) return "短按按键结束对话";
    if (strcmp(status, "ENDING") == 0) return "请稍候";
    if (strcmp(status, "CALL PREP") == 0) return "正在释放AI音频";
    if (strcmp(status, "NO CONTACT") == 0) return "请检查联系人名称";
    if (strcmp(status, "DUP CONTACT") == 0) return "请说出完整名称";
    if (strcmp(status, "CONTACT OFFLINE") == 0) return "请稍后再试";
    if (strcmp(status, "REMOTE") == 0) return "设备画面正在共享";
    if (strcmp(status, "ROOM PAUSED") == 0) {
        return "房间已保留，再次进入可连接";
    }
    if (strcmp(status, "DEVICE CALL") == 0) return "请选择接听或拒绝";
    if (strcmp(status, "DEVICE OUT") == 0) return "等待对方接听";
    if (strcmp(status, "DEVICE CONNECT") == 0) return "请稍候";
    if (strcmp(status, "DEVICE TALK") == 0) return "短按按键挂断";
    if (strcmp(status, "VOIP CALL") == 0) return "请选择接听或拒绝";
    if (strcmp(status, "VOIP OUT") == 0) return "等待对方接听";
    if (strcmp(status, "VOIP CONNECT") == 0) return "请稍候";
    if (strcmp(status, "VOIP TALK") == 0) return "短按按键挂断";
    if (strcmp(status, "TIMEOUT") == 0) return "短按按键重试";
    if (strcmp(status, "ERROR") == 0) return "请稍后重试";
    if (strcmp(status, "RECOVERING") == 0) return "请稍候";
    return "短按按键开始对话";
}

static const char *status_title(const char *status)
{
    if (status == NULL || strcmp(status, "BOOT") == 0) return "启动中";
    if (strcmp(status, "SETUP") == 0) return "需要连接网络";
    if (strcmp(status, "WIFI") == 0) return "正在连接网络";
    if (strcmp(status, "BIND") == 0) return "绑定设备";
    if (strcmp(status, "BIND VOICE ERR") == 0) return "语音获取失败";
    if (strcmp(status, "BOUND") == 0) return "绑定成功";
    if (strcmp(status, "READY") == 0) return "准备就绪";
    if (strcmp(status, "AI CONNECT") == 0) return "正在连接AI";
    if (strcmp(status, "AI") == 0) return "AI对话中";
    if (strcmp(status, "ENDING") == 0) return "正在结束对话";
    if (strcmp(status, "CALL PREP") == 0) return "正在准备通话";
    if (strcmp(status, "NO CONTACT") == 0) return "未找到联系人";
    if (strcmp(status, "DUP CONTACT") == 0) return "找到多个联系人";
    if (strcmp(status, "CONTACT OFFLINE") == 0) return "呼叫对象不在线";
    if (strcmp(status, "REMOTE") == 0) return "远程查看中";
    if (strcmp(status, "ROOM PAUSED") == 0) return "已结束本机对讲";
    if (strcmp(status, "DEVICE CALL") == 0) return "设备来电";
    if (strcmp(status, "DEVICE OUT") == 0) return "正在呼叫设备";
    if (strcmp(status, "DEVICE CONNECT") == 0) return "正在接通";
    if (strcmp(status, "DEVICE TALK") == 0) return "设备通话中";
    if (strcmp(status, "VOIP CALL") == 0) return "微信来电";
    if (strcmp(status, "VOIP OUT") == 0) return "正在呼叫微信";
    if (strcmp(status, "VOIP CONNECT") == 0) return "微信接通中";
    if (strcmp(status, "VOIP TALK") == 0) return "微信通话中";
    if (strcmp(status, "TIMEOUT") == 0) return "连接超时";
    if (strcmp(status, "ERROR") == 0) return "出现异常";
    if (strcmp(status, "RECOVERING") == 0) return "正在恢复";
    return "小钛在线";
}

static bool s_call_microphone_muted;

static uint16_t status_accent(const char *status)
{
    if (status != NULL && strcmp(status, "ERROR") == 0) return 0xf800;
    if (status != NULL && (strcmp(status, "DEVICE CALL") == 0 ||
                           strcmp(status, "DEVICE OUT") == 0 ||
                           strcmp(status, "VOIP CALL") == 0 ||
                           strcmp(status, "VOIP OUT") == 0)) return 0xfbe0;
    if (status != NULL && strcmp(status, "TIMEOUT") == 0) return 0xfd20;
    if (status != NULL && (strcmp(status, "AI") == 0 ||
                           strcmp(status, "REMOTE") == 0 ||
                           strcmp(status, "DEVICE TALK") == 0 ||
                           strcmp(status, "VOIP TALK") == 0)) return 0x07e0;
    return 0x2d7f;
}

static void status_icon(frame_buffer_t *frame, const char *status,
                        uint16_t accent)
{
    int center_x = (int)LCD_WIDTH / 2;
    int center_y = 102;
    if (status != NULL && strcmp(status, "REMOTE") == 0) {
        rectangle_outline(frame, center_x - 38, center_y - 24,
                          76, 48, 4, accent);
        circle(frame, center_x, center_y, 13, accent);
        circle(frame, center_x, center_y, 6, 0x0000);
        return;
    }
    if (status != NULL && (strcmp(status, "AI") == 0 ||
                           strcmp(status, "AI CONNECT") == 0 ||
                           strcmp(status, "ENDING") == 0)) {
        circle(frame, center_x, center_y, 31, accent);
        circle(frame, center_x - 11, center_y - 5, 4, 0x0000);
        circle(frame, center_x + 11, center_y - 5, 4, 0x0000);
        rectangle(frame, center_x - 13, center_y + 12, 26, 4, 0x0000);
        return;
    }
    if (status != NULL && (strcmp(status, "DEVICE CALL") == 0 ||
                           strcmp(status, "DEVICE OUT") == 0 ||
                           strcmp(status, "DEVICE CONNECT") == 0 ||
                           strcmp(status, "DEVICE TALK") == 0 ||
                           strcmp(status, "VOIP CALL") == 0 ||
                           strcmp(status, "VOIP OUT") == 0 ||
                           strcmp(status, "VOIP CONNECT") == 0 ||
                           strcmp(status, "VOIP TALK") == 0)) {
        circle(frame, center_x, center_y, 31, accent);
        rectangle(frame, center_x - 20, center_y - 7, 40, 14, 0x0000);
        rectangle(frame, center_x - 20, center_y - 15, 10, 10, 0x0000);
        rectangle(frame, center_x + 10, center_y + 5, 10, 10, 0x0000);
        return;
    }
    circle(frame, center_x, center_y, 31, accent);
    circle(frame, center_x, center_y, 19, 0x0000);
    circle(frame, center_x, center_y, 8, accent);
}

static void present(const char *status, const char *code)
{
    if (!xiaotai_board_display_ready()) return;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) {
        BK_LOGE(TAG, "display frame allocation failed\n");
        return;
    }
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0, frame->size);
    uint16_t accent = status_accent(status);
    rectangle(frame, 0, 0, LCD_WIDTH, 34, 0x10a4);
    mixed_text(frame, 12, 8, "小钛", 2, 0xffff);
    circle(frame, 296, 17, 6, accent);
    if (code != NULL && strlen(code) == 6U) {
        centered_cjk_text(frame, 58, "设备绑定", accent);
        rectangle(frame, 44, 98, 232, 64, 0x18e3);
        rectangle_outline(frame, 44, 98, 232, 64, 2, accent);
        centered_text(frame, 117, code, 5, 0xffe0);
        if (strcmp(status, "BIND VOICE ERR") == 0) {
            centered_cjk_text(frame, 191, "语音获取失败", 0xf800);
        } else {
            centered_cjk_text(frame, 191, "验证码", 0xbdf7);
        }
    } else {
        status_icon(frame, status, accent);
        centered_mixed_text(frame, 145, status_title(status), 2, accent);
        bool incoming = status != NULL &&
                        (strcmp(status, "DEVICE CALL") == 0 ||
                         strcmp(status, "VOIP CALL") == 0);
        bool active_call = status != NULL &&
                           (strcmp(status, "DEVICE TALK") == 0 ||
                            strcmp(status, "VOIP TALK") == 0);
        if (incoming) {
            rectangle(frame, 20, 188, 120, 40, 0x7800);
            rectangle(frame, 180, 188, 120, 40, 0x03e0);
            mixed_text(frame, 64, 198, "拒绝", 1, 0xffff);
            mixed_text(frame, 224, 198, "接听", 1, 0xffff);
        } else if (active_call) {
            rectangle(frame, 20, 188, 120, 40,
                      s_call_microphone_muted ? 0x4208 : 0x0451);
            rectangle(frame, 180, 188, 120, 40, 0xb800);
            centered_mixed_in(
                frame, 80, 198,
                s_call_microphone_muted ? "麦克风已关" : "麦克风已开",
                1, 0xffff, 110);
            mixed_text(frame, 224, 198, "挂断", 1, 0xffff);
        } else {
            centered_mixed_text(frame, 205, status_hint(status), 1, 0xbdf7);
        }
    }
    if (xiaotai_board_display_flush(frame, release_frame) != BK_OK) {
        release_frame(frame);
        BK_LOGE(TAG, "display flush failed\n");
    }
}

int xiaotai_ui_init(void)
{
    int rc = xiaotai_board_display_init();
    if (rc != BK_OK) return rc;
    present("BOOT", NULL);
    return BK_OK;
}

const void *xiaotai_ui_probe_lcd(void)
{
    return xiaotai_board_display_probe_lcd();
}

void xiaotai_ui_show_status(const char *status)
{
    xiaotai_metrics_set_state(status);
    present(status, NULL);
}

void xiaotai_ui_show_call_active(bool wechat, bool microphone_muted)
{
    const char *status = wechat ? "VOIP TALK" : "DEVICE TALK";
    s_call_microphone_muted = microphone_muted;
    xiaotai_metrics_set_state(status);
    present(status, NULL);
}

void xiaotai_ui_show_verification_code(const char *code)
{
    xiaotai_metrics_set_state("BIND");
    present("BIND", code);
}

void xiaotai_ui_show_verification_audio_error(const char *code)
{
    present("BIND VOICE ERR", code);
    BK_LOGW(TAG, "binding code remains visible; server voice unavailable\n");
}

void xiaotai_ui_show_network_setup(const char *ssid)
{
    xiaotai_metrics_set_state("SETUP");
    if (!xiaotai_board_display_ready()) return;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) return;
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0, frame->size);
    rectangle(frame, 0, 0, LCD_WIDTH, 34, 0x10a4);
    mixed_text(frame, 12, 8, "小钛", 2, 0xffff);
    centered_mixed_text(frame, 48, "请在手机上继续", 2, 0x2d7f);
    centered_mixed_text(frame, 81, "1 连接小钛热点", 1, 0xffff);
    centered_mixed_text(frame, 105, ssid == NULL ? "XiaoTai" : ssid,
                        2, 0xffe0);
    centered_mixed_text(frame, 145, "2 打开 192.168.6.1", 1, 0xbdf7);
    centered_mixed_text(frame, 174, "3 选择家庭WiFi并输入密码", 1, 0xbdf7);
    centered_mixed_text(frame, 207, "手机提示无互联网是正常的", 1, 0x07e0);
    if (xiaotai_board_display_flush(frame, release_frame) != BK_OK) {
        release_frame(frame);
    }
}

void xiaotai_ui_show_wechat_qr(void)
{
    xiaotai_metrics_set_state("WX QR");
    if (!xiaotai_board_display_ready()) return;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) {
        BK_LOGE(TAG, "WeChat QR frame allocation failed\n");
        return;
    }
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0xff, frame->size);

    const int scale = 4;
    const int quiet = 4;
    const int qr_size = ((int)XIAOTAI_WX_QR_MODULES + quiet * 2) * scale;
    const int qr_x = ((int)LCD_WIDTH - qr_size) / 2;
    const int qr_y = 5;
    rectangle(frame, qr_x, qr_y, qr_size, qr_size, 0xffff);
    for (unsigned row = 0; row < XIAOTAI_WX_QR_MODULES; ++row) {
        for (unsigned column = 0; column < XIAOTAI_WX_QR_MODULES; ++column) {
            size_t bit = row * XIAOTAI_WX_QR_MODULES + column;
            if ((xiaotai_wx_qr_bits[bit / 8U] &
                 (uint8_t)(0x80U >> (bit % 8U))) != 0U) {
                rectangle(frame, qr_x + (quiet + (int)column) * scale,
                          qr_y + (quiet + (int)row) * scale,
                          scale, scale, 0x0000);
            }
        }
    }
    centered_cjk_text(frame, 191, "微信扫码添加联系人", 0x0000);
    centered_cjk_text(frame, 215, "授权后双击呼叫", 0x3186);
    if (xiaotai_board_display_flush(frame, release_frame) != BK_OK) {
        release_frame(frame);
        BK_LOGE(TAG, "WeChat QR display flush failed\n");
    }
}

static frame_buffer_t *page_frame(const char *title)
{
    if (!xiaotai_board_display_ready()) return NULL;
    frame_buffer_t *frame = frame_buffer_display_malloc(
        LCD_WIDTH * LCD_HEIGHT * 2U);
    if (frame == NULL) return NULL;
    frame->fmt = PIXEL_FMT_RGB565;
    frame->width = LCD_WIDTH;
    frame->height = LCD_HEIGHT;
    memset(frame->frame, 0, frame->size);
    rectangle(frame, 0, 0, LCD_WIDTH, 34, 0x10a4);
    line(frame, 20, 9, 10, 17, 2, 0xffff);
    line(frame, 10, 17, 20, 25, 2, 0xffff);
    centered_mixed_text(frame, 8, title, 1, 0xffff);
    return frame;
}

static void flush_page(frame_buffer_t *frame)
{
    if (frame != NULL && xiaotai_board_display_flush(frame,
                                                     release_frame) != BK_OK) {
        release_frame(frame);
    }
}

void xiaotai_ui_show_launcher(unsigned selected)
{
    frame_buffer_t *frame = page_frame("功能菜单");
    if (frame == NULL) return;
    const char *labels[6] = {
        "AI聊天", "通讯录", "多人对讲", "表情包",
        "设备设置", "网络状态"
    };
    for (int i = 0; i < 6; ++i) {
        int x = 10 + (i % 2) * 155;
        int y = 42 + (i / 2) * 64;
        bool focused = selected == (unsigned)i;
        rectangle(frame, x, y, 145, 56, focused ? 0x2d7f : 0x18e3);
        rectangle_outline(frame, x, y, 145, 56, 2,
                          focused ? 0xffff : 0x2d7f);
        centered_mixed_in(frame, x + 72, y + 20, labels[i], 1,
                          focused ? 0x0000 : 0xffff, 145);
    }
    flush_page(frame);
}

void xiaotai_ui_show_contacts(const char *const *names,
                              const bool *online, const bool *wechat,
                              size_t count,
                              size_t page, size_t selected)
{
    frame_buffer_t *frame = page_frame("通讯录");
    if (frame == NULL) return;
    size_t start = page * 4U;
    for (size_t row = 0; row < 4U; ++row) {
        int y = 43 + (int)row * 46;
        size_t index = start + row;
        bool focused = index < count && index == selected;
        if (focused) rectangle(frame, 8, y, 304, 40, 0x18e3);
        rectangle_outline(frame, 8, y, 304, 40, focused ? 2 : 1,
                          focused ? 0x2d7f : 0x3186);
        if (index < count) {
            circle(frame, 24, y + 20, 6,
                   wechat[index] ? 0x07e0 :
                   (online[index] ? 0x2d7f : 0x7bef));
            mixed_text_clipped(frame, 42, y + 12, names[index], 1,
                               0xffff, 190);
            mixed_text(frame, 267, y + 12,
                       wechat[index] ? "微信" : "设备", 1,
                       wechat[index] || online[index] ? 0x2d7f : 0x7bef);
        }
    }
    mixed_text(frame, 258, 220, "下一页", 1, 0xbdf7);
    flush_page(frame);
}

void xiaotai_ui_show_contact_detail(const char *name, bool online,
                                    bool wechat)
{
    frame_buffer_t *frame = page_frame("联系人");
    if (frame == NULL) return;
    circle(frame, 160, 88, 35, wechat ? 0x07e0 : 0x2d7f);
    centered_mixed_text(frame, 137, name == NULL ? "未知联系人" : name,
                        2, 0xffff);
    centered_mixed_text(frame, 174, wechat ? "微信联系人" :
                        (online ? "设备在线" : "设备离线"), 1,
                        online || wechat ? 0x07e0 : 0xf800);
    rectangle(frame, 72, 204, 176, 29, online || wechat ? 0x2d7f : 0x3186);
    centered_mixed_text(frame, 210, online || wechat ? "语音通话" :
                        "暂不可用", 1, 0x0000);
    flush_page(frame);
}

void xiaotai_ui_show_expressions(unsigned selected,
                                 const char *active_emotion)
{
    frame_buffer_t *frame = page_frame("表情包");
    if (frame == NULL) return;
    size_t count = xiaotai_ai_emotion_count();
    size_t index = count == 0U ? 0U : (size_t)selected % count;
    const char *emotion = xiaotai_ai_emotion_at(index);
    if (emotion == NULL) emotion = "happy";
    ai_face(frame, XIAOTAI_AI_UI_SPEAKING, emotion, 0x2d7f);
    centered_mixed_text(frame, 154, xiaotai_ai_emotion_label_zh(emotion),
                        2, 0xffff);
    centered_mixed_text(frame, 188,
                        active_emotion != NULL &&
                        strcmp(active_emotion, emotion) == 0 ?
                        "当前使用" : "按键应用", 1, 0x07e0);
    mixed_text(frame, 18, 216, "上一个", 1, 0x7bef);
    mixed_text(frame, 270, 216, "下一个", 1, 0x7bef);
    flush_page(frame);
}

void xiaotai_ui_show_network(int wifi_rssi, const char *ip_address)
{
    frame_buffer_t *frame = page_frame("网络状态");
    if (frame == NULL) return;
    char value[32];
    centered_mixed_text(frame, 58, wifi_rssi > -128 ? "WiFi已连接" :
                        "网络未连接", 2,
                        wifi_rssi > -128 ? 0x07e0 : 0xf800);
    snprintf(value, sizeof(value), "信号强度 %d", wifi_rssi);
    centered_mixed_text(frame, 112, value, 1, 0xffff);
    snprintf(value, sizeof(value), "地址 %s",
             ip_address == NULL ? "不可用" : ip_address);
    centered_mixed_text(frame, 151, value, 1, 0x2d7f);
    centered_mixed_text(frame, 205, "按返回键退出", 1, 0x7bef);
    flush_page(frame);
}

void xiaotai_ui_show_settings(uint8_t volume, bool speaker_muted,
                              bool microphone_muted,
                              uint8_t microphone_sensitivity,
                              const char *screen_timeout)
{
    frame_buffer_t *frame = page_frame("设备设置");
    if (frame == NULL) return;
    char value[24];
    mixed_text(frame, 16, 54, "扬声器音量", 1, 0xffff);
    rectangle(frame, 166, 44, 42, 36, 0x10a4);
    rectangle_outline(frame, 166, 44, 42, 36, 2, 0x2d7f);
    line(frame, 177, 62, 197, 62, 2, 0xffff);
    snprintf(value, sizeof(value), "%u", (unsigned)volume);
    centered_mixed_in(frame, 240, 54, value, 2, 0xffff, 42);
    rectangle(frame, 271, 44, 41, 36, 0x10a4);
    rectangle_outline(frame, 271, 44, 41, 36, 2, 0x2d7f);
    line(frame, 281, 62, 301, 62, 2, 0xffff);
    line(frame, 291, 52, 291, 72, 2, 0xffff);
    mixed_text(frame, 16, 92, "扬声器", 1, 0xffff);
    mixed_text(frame, 220, 92, speaker_muted ? "已静音" : "开启", 1,
               speaker_muted ? 0xf800 : 0x07e0);
    mixed_text(frame, 16, 130, "麦克风", 1, 0xffff);
    mixed_text(frame, 220, 130, microphone_muted ? "已静音" : "开启", 1,
               microphone_muted ? 0xf800 : 0x07e0);
    mixed_text(frame, 16, 168, "麦克风灵敏度", 1, 0xffff);
    snprintf(value, sizeof(value), "%u/5", (unsigned)microphone_sensitivity);
    mixed_text(frame, 250, 168, value, 1, 0x2d7f);
    mixed_text(frame, 16, 206, "自动息屏", 1, 0xffff);
    mixed_text(frame, 220, 206, screen_timeout, 1, 0x2d7f);
    flush_page(frame);
}

void xiaotai_ui_show_room(const xiaotai_room_snapshot_t *room, size_t page)
{
    if (room == NULL) return;
    xiaotai_metrics_set_state(room->talking ? "ROOM TALK" : "ROOM LIST");
    frame_buffer_t *frame = page_frame("多人对讲");
    if (frame == NULL) return;
    char state[40];
    snprintf(state, sizeof(state), "%s  %s  %u人",
             room->assigned ? room->room_code : "未分配",
             room->joined ? "已加入" : "等待加入", room->members);
    centered_mixed_text(frame, 42, state, 1,
                        room->joined ? 0x07e0 : 0xfbe0);
    page = xiaotai_ui_room_page_clamp(page, room->participant_count);
    size_t start = page * XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE;
    size_t remaining = room->participant_count > start ?
                       room->participant_count - start : 0U;
    size_t shown = remaining > XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE ?
                   XIAOTAI_UI_ROOM_PARTICIPANTS_PER_PAGE : remaining;
    for (size_t i = 0U; i < shown; ++i) {
        const xiaotai_room_participant_t *participant =
            &room->participants[start + i];
        int y = 65 + (int)i * 34;
        rectangle(frame, 12, y - 5, 296, 29,
                  participant->speaking ? 0x0320 : 0x0841);
        mixed_text(frame, 20, y,
                   participant->name[0] != '\0' ? participant->name :
                   participant->id, 1, 0xffff);
        if (participant->self) {
            mixed_text(frame, 202, y, "本机", 1, 0x2d7f);
        }
        mixed_text(frame, 246, y,
                   participant->speaking ? "说话中" : "收听",
                   1, participant->speaking ? 0xffe0 : 0xbdf7);
    }
    if (shown == 0U) {
        centered_mixed_text(frame, 92, "暂无在线设备", 1, 0x8410);
    }
    size_t pages = xiaotai_ui_room_page_count(room->participant_count);
    bool has_uncached_members = room->members > room->participant_count;
    if (pages > 1U) {
        rectangle(frame, (int)XIAOTAI_UI_ROOM_PAGE_PREV_X,
                  (int)XIAOTAI_UI_ROOM_PAGE_Y,
                  (int)XIAOTAI_UI_ROOM_PAGE_BUTTON_WIDTH,
                  (int)XIAOTAI_UI_ROOM_PAGE_HEIGHT,
                  page > 0U ? 0x10a4 : 0x4208);
        rectangle(frame, (int)XIAOTAI_UI_ROOM_PAGE_NEXT_X,
                  (int)XIAOTAI_UI_ROOM_PAGE_Y,
                  (int)XIAOTAI_UI_ROOM_PAGE_BUTTON_WIDTH,
                  (int)XIAOTAI_UI_ROOM_PAGE_HEIGHT,
                  page + 1U < pages ? 0x10a4 : 0x4208);
        centered_mixed_in(frame, 50, 161, "上一页", 1, 0xffff, 72);
        centered_mixed_in(frame, 270, 161, "下一页", 1, 0xffff, 72);
        if (has_uncached_members) {
            snprintf(state, sizeof(state), "%u/%u 前%u/%u",
                     (unsigned)(page + 1U), (unsigned)pages,
                     (unsigned)room->participant_count, room->members);
        } else {
            snprintf(state, sizeof(state), "%u/%u",
                     (unsigned)(page + 1U), (unsigned)pages);
        }
        centered_mixed_in(frame, 160, 161, state, 1, 0xbdf7, 132);
    } else if (has_uncached_members) {
        snprintf(state, sizeof(state), "显示前%u/%u",
                 (unsigned)room->participant_count, room->members);
        centered_mixed_text(frame, 162, state, 1, 0x8410);
    }
    const int button_y = (int)XIAOTAI_UI_ROOM_BUTTON_Y;
    const int button_w = (int)XIAOTAI_UI_ROOM_BUTTON_WIDTH;
    const int button_h = (int)XIAOTAI_UI_ROOM_BUTTON_HEIGHT;
    rectangle(frame, (int)XIAOTAI_UI_ROOM_LEAVE_X, button_y,
              button_w, button_h, 0xa800);
    rectangle(frame, (int)XIAOTAI_UI_ROOM_TALK_X, button_y,
              button_w, button_h,
              room->talking ? 0xf800 : (room->joined ? 0x2d7f : 0x8410));
    centered_mixed_in(frame,
        (int)XIAOTAI_UI_ROOM_LEAVE_X + button_w / 2, button_y + 16,
        "退出房间", 1, 0xffff, button_w - 8);
    centered_mixed_in(frame,
        (int)XIAOTAI_UI_ROOM_TALK_X + button_w / 2, button_y + 16,
        room->talking ? "正在说话" : "按住说话", 1,
        room->talking ? 0xffff : 0x0000, button_w - 8);
    flush_page(frame);
}

void xiaotai_ui_show_room_entry(bool request_pending)
{
    xiaotai_metrics_set_state("ROOM ENTRY");
    frame_buffer_t *frame = page_frame("多人对讲");
    if (frame == NULL) return;
    centered_mixed_text(frame, 58,
                        request_pending ? "正在提交" : "尚未加入房间",
                        2, request_pending ? 0xfbe0 : 0xffff);
    centered_mixed_text(frame, 104,
                        request_pending ? "请稍候" : "创建新房间或输入房间码",
                        1, 0xbdf7);
    rectangle(frame, (int)XIAOTAI_UI_ROOM_LEAVE_X,
              (int)XIAOTAI_UI_ROOM_BUTTON_Y,
              (int)XIAOTAI_UI_ROOM_BUTTON_WIDTH,
              (int)XIAOTAI_UI_ROOM_BUTTON_HEIGHT, 0x0320);
    rectangle(frame, (int)XIAOTAI_UI_ROOM_TALK_X,
              (int)XIAOTAI_UI_ROOM_BUTTON_Y,
              (int)XIAOTAI_UI_ROOM_BUTTON_WIDTH,
              (int)XIAOTAI_UI_ROOM_BUTTON_HEIGHT, 0x10a4);
    centered_mixed_in(frame, 82, 200, "创建房间", 1, 0xffff, 140);
    centered_mixed_in(frame, 238, 200, "加入房间", 1, 0xffff, 140);
    flush_page(frame);
}

void xiaotai_ui_show_room_join_code(const char *room_code)
{
    xiaotai_metrics_set_state("ROOM JOIN");
    frame_buffer_t *frame = page_frame("加入房间");
    if (frame == NULL) return;
    char value[16];
    size_t length = room_code == NULL ? 0U : strlen(room_code);
    snprintf(value, sizeof(value), "%s%.*s", room_code == NULL ? "" : room_code,
             (int)(6U - (length > 6U ? 6U : length)), "------");
    centered_mixed_in(frame, 160, 39, value, 1, 0x2d7f, 300);
    static const char *keys[4][3] = {
        {"1", "2", "3"}, {"4", "5", "6"},
        {"7", "8", "9"}, {"删除", "0", ""},
    };
    for (unsigned row = 0U; row < 4U; ++row) {
        for (unsigned column = 0U; column < 3U; ++column) {
            if (keys[row][column][0] == '\0') continue;
            int x = (int)column * 107 + 4;
            int y = 64 + (int)row * 29;
            rectangle(frame, x, y, 99, 25, 0x10a4);
            centered_mixed_in(frame, x + 49, y + 4, keys[row][column],
                              1, 0xffff, 91);
        }
    }
    rectangle(frame, 8, 190, 304, 42,
              length == 6U ? 0x0320 : 0x4208);
    centered_mixed_in(frame, 160, 201, "加入房间", 1, 0xffff, 296);
    flush_page(frame);
}

void xiaotai_ui_show_room_leave_confirm(void)
{
    xiaotai_metrics_set_state("ROOM LEAVE");
    frame_buffer_t *frame = page_frame("退出房间");
    if (frame == NULL) return;
    centered_mixed_text(frame, 62, "确认退出房间？", 2, 0xffff);
    centered_mixed_text(frame, 108, "将取消本设备的房间分配", 1, 0xfbe0);
    centered_mixed_text(frame, 132, "再次使用需要重新加入", 1, 0xbdf7);
    rectangle(frame, 8, (int)XIAOTAI_UI_ROOM_BUTTON_Y, 148,
              (int)XIAOTAI_UI_ROOM_BUTTON_HEIGHT, 0x2104);
    rectangle(frame, 164, (int)XIAOTAI_UI_ROOM_BUTTON_Y, 148,
              (int)XIAOTAI_UI_ROOM_BUTTON_HEIGHT, 0xa800);
    centered_mixed_in(frame, 82, 200, "取消", 1, 0xffff, 140);
    centered_mixed_in(frame, 238, 200, "退出房间", 1, 0xffff, 140);
    flush_page(frame);
}

int xiaotai_ui_set_backlight(bool enabled)
{
    return xiaotai_board_display_set_backlight(enabled);
}

bool xiaotai_ui_backlight_on(void)
{
    return xiaotai_board_display_backlight_on();
}
