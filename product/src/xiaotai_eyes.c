#include "xiaotai_eyes.h"
#include <string.h>

typedef struct {
    const char *tag;
    int height, tilt, gaze, curve;
    uint16_t color;
} eye_style_t;
/* Presentation geometry only. The runtime supplies canonical emotion tags. */
static const eye_style_t styles[] = {
    {"neutral",34,0,0,0,0x5fff}, {"happy",28,0,0,1,0x57ff},
    {"laughing",18,0,0,2,0x47ff}, {"funny",36,0,12,1,0x67ff},
    {"sad",24,-12,0,-1,0x3dbf}, {"angry",32,18,0,-1,0xf9c7},
    {"crying",23,-8,0,-1,0x2dff}, {"loving",36,0,0,3,0xfc18},
    {"embarrassed",22,0,8,1,0xfa9c}, {"surprised",46,0,0,0,0x7fff},
    {"shocked",51,0,0,0,0xafff}, {"thinking",28,0,16,0,0x5dfb},
    {"winking",32,0,0,0,0x5fff}, {"cool",16,0,0,-1,0x3bdf},
    {"relaxed",13,0,0,1,0x3fda}, {"delicious",28,0,-8,1,0x7ff0},
    {"kissy",20,0,0,3,0xfbb7}, {"confident",26,10,6,-1,0x5fbf},
    {"sleepy",9,0,0,0,0x3377}, {"silly",40,0,-16,0,0x97ff},
    {"confused",26,-6,10,0,0x7e7f},
};
static uint16_t wire(uint16_t c) { return (uint16_t)((c<<8)|(c>>8)); }

void xiaotai_eye_render(uint16_t *pixels, const char *emotion,
                        bool right_eye, uint32_t now_ms)
{
    if (!pixels) return;
    const eye_style_t *s=&styles[0];
    if (emotion) for (unsigned i=0;i<sizeof(styles)/sizeof(styles[0]);i++)
        if (strcmp(emotion,styles[i].tag)==0) { s=&styles[i]; break; }
    memset(pixels,0,XIAOTAI_EYE_WIDTH*XIAOTAI_EYE_HEIGHT*sizeof(*pixels));
    bool blink=now_ms%4000U<100U || (right_eye && strcmp(s->tag,"winking")==0);
    int height=blink?3:s->height;
    if (!blink && strcmp(s->tag,"confused")==0 && right_eye) height+=12;
    int tilt=right_eye?-s->tilt:s->tilt;
    for (int y=18;y<142;y++) for (int x=18;x<142;x++) {
        int dx=x-80,dy=y-78;
        bool lit=dx*dx*height*height+dy*dy*46*46<=46*46*height*height;
        if (!blink && tilt && dy < tilt*dx/46-12) lit=false;
        if (!blink && s->curve>0 && s->curve<3 && dy>dx*dx/90-9) lit=false;
        if (!blink && s->curve==3) {
            /* Twin lobes and a tapered point form a heart-shaped eye. */
            int ax=dx<0?-dx:dx;
            lit=dy>=-height && dy<height && ax<44 &&
                (dy<0 ? (ax-21)*(ax-21)+dy*dy<24*24 : ax<height-dy);
        }
        if (!blink && lit && s->curve==0 &&
            (dx-s->gaze)*(dx-s->gaze)*4+dy*dy<12*12) lit=false;
        if (!blink && strcmp(s->tag,"crying")==0 &&
            dx>24 && dx<34 && dy>height && dy<58) lit=true;
        if (lit) pixels[y*160+x]=wire(s->color);
    }
}
