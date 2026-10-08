/* Shared UI style and packed alpha access; no SDK or heap dependency. */
#ifndef XIAOTAI_WECHAT_CALL_STYLE_H
#define XIAOTAI_WECHAT_CALL_STYLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define XIAOTAI_WECHAT_CALL_FOREGROUND 0xFFFFFFU
#define XIAOTAI_WECHAT_CALL_BACKGROUND 0x18A957U
#define XIAOTAI_WECHAT_CALL_PRESSED 0x0F7A40U
#define XIAOTAI_WECHAT_CALL_BORDER 0x7EF1A3U
static inline uint8_t xiaotai_icon_alpha4(const uint8_t *map,
    unsigned width,unsigned height,unsigned x,unsigned y)
{
    if(map==NULL || x>=width || y>=height || width%2!=0)return 0;
    uint8_t value=map[(size_t)y*(width/2U)+x/2U];
    return (uint8_t)((x%2U==0 ? value>>4 : value&15U)*17U);
}
static inline uint16_t xiaotai_icon_rgb565(uint32_t color)
{
    return (uint16_t)(((color>>19)&31U)<<11 | ((color>>10)&63U)<<5 | ((color>>3)&31U));
}
static inline bool xiaotai_wechat_call_use_big(unsigned display_height)
{
    return display_height>=480U;
}
#endif
