#include "xiaotai_eye_text.h"
#include <string.h>
#include <stdbool.h>

static uint32_t next(const unsigned char **cursor)
{
    const unsigned char *p=*cursor;
    if (*p<128) { *cursor=p+1; return *p; }
    if ((*p&0xe0)==0xc0 && p[1] && (p[1]&0xc0)==0x80) {
        *cursor=p+2; return ((p[0]&31)<<6)|(p[1]&63);
    }
    if ((*p&0xf0)==0xe0 && p[1] && p[2] &&
        (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80) {
        *cursor=p+3; return ((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);
    }
    *cursor=p+1; return 0xfffd;
}
size_t xiaotai_eye_text_render(uint16_t *pixels,const char *text,
                              const uint8_t *(*glyph)(uint32_t))
{
    if (!pixels) return 0;
    memset(pixels,0,160U*160U*2U);
    if (!text) return 0;
    const unsigned char *p=(const unsigned char *)text;
    int x=12,row=0;
    size_t count=0;
    while (*p && row<6) {
        uint32_t c=next(&p);
        if (c=='\n') { x=12; row++; continue; }
        int advance=c<128?9:17;
        if (x+16>148) { x=12; row++; }
        if (row>=6) break;
        const uint8_t *bits=glyph?glyph(c):NULL;
        int y=16+row*20;
        for (int dy=0;dy<16;dy++) for (int dx=0;dx<16;dx++) {
            bool on=bits?((bits[dy*2+dx/8]>>(7-dx%8))&1):
                         (dx==1 || dx==12 || dy==1 || dy==14);
            if (on) pixels[(y+dy)*160+x+dx]=0xffff;
        }
        x+=advance;count++;
    }
    return count;
}
