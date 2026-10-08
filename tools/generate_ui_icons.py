#!/usr/bin/env python3
"""Generate shared alpha4 icon variants and PNG previews with no image library."""
import argparse
import hashlib
import json
import math
import re
import xml.etree.ElementTree as ET
import struct
import zlib
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / 'product/assets/icons/wechat_call'

def png(width,height,alpha):
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    rows=b''.join(b'\0'+b''.join(bytes((255,255,255,a)) for a in alpha[y*width:(y+1)*width]) for y in range(height))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+
            chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))

def vector_contours(source):
    """Read the intentionally small SVG subset used by this product icon.

    Supports absolute M/L/C/Z closed paths with evenodd fill. Fail on other
    commands rather than silently producing a different icon. No host graphics
    library, runtime SVG parser or platform dependency is required.
    """
    root=ET.fromstring(source)
    if root.attrib.get('viewBox')!='0 0 28 28':
        raise ValueError('icon SVG must use viewBox="0 0 28 28"')
    contours=[]
    for path in root.findall('{http://www.w3.org/2000/svg}path'):
        if path.attrib.get('fill-rule')!='evenodd':
            raise ValueError('icon paths must specify evenodd fill')
        tokens=re.findall(r'[A-Za-z]|[-+]?(?:\d*\.\d+|\d+)',path.attrib['d'])
        i=0;command=None;points=[];current=(0.0,0.0)
        while i<len(tokens):
            if tokens[i].isalpha():
                command=tokens[i];i+=1
            if command=='Z':
                if len(points)<3: raise ValueError('closed path has too few points')
                points.append(points[0]);contours.append(points);points=[];command=None
                continue
            if command not in ('M','L','C'):
                raise ValueError(f'unsupported SVG command: {command}')
            count=6 if command=='C' else 2
            values=[float(v) for v in tokens[i:i+count]];i+=count
            if len(values)!=count: raise ValueError('incomplete SVG command')
            if command=='C':
                x0,y0=current;x1,y1,x2,y2,x3,y3=values
                for step in range(1,65):
                    t=step/64;u=1-t
                    points.append((u*u*u*x0+3*u*u*t*x1+3*u*t*t*x2+t*t*t*x3,
                                   u*u*u*y0+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y3))
                current=(x3,y3)
            else:
                if command=='M' and points: raise ValueError('previous path must close')
                current=tuple(values);points.append(current)
                if command=='M': command='L'
        if points: raise ValueError('icon paths must close')
    if not contours: raise ValueError('icon SVG has no supported paths')
    return contours

def render_alpha4(contours,width,height):
    """Rasterize each requested native size at8x, then quantize coverage.

    Curve geometry is sampled directly at the target size. There is no resize
    of an already-rasterized small icon and no blur filter on the edge.
    """
    samples=8
    coverage=[0]*(width*height)
    sx=width*samples/28;sy=height*samples/28
    edges=[(a,b) for contour in contours for a,b in zip(contour,contour[1:])]
    for row in range(height*samples):
        y=(row+0.5)/sy
        crossings=[]
        for (x0,y0),(x1,y1) in edges:
            if min(y0,y1)<=y<max(y0,y1):
                crossings.append(x0+(y-y0)*(x1-x0)/(y1-y0))
        crossings.sort()
        if len(crossings)%2: raise ValueError('invalid vector fill intersection')
        for left,right in zip(crossings[::2],crossings[1::2]):
            start=max(0,math.ceil(left*sx-0.5))
            end=min(width*samples,math.ceil(right*sx-0.5))
            for col in range(start,end):
                coverage[(row//samples)*width+col//samples]+=1
    total=samples*samples
    return [((count*15+total//2)//total)*17 for count in coverage]

def generate():
    manifest=json.loads((ASSETS/'manifest.json').read_text())
    source=(ASSETS/manifest['source']).read_bytes()
    if hashlib.sha256(source).hexdigest()!=manifest['source_sha256']:
        raise ValueError('canonical icon checksum differs from manifest')
    contours=vector_contours(source)
    style=manifest['style']
    common='''/* Shared UI style and packed alpha access; no SDK or heap dependency. */
#ifndef XIAOTAI_WECHAT_CALL_STYLE_H
#define XIAOTAI_WECHAT_CALL_STYLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
'''
    for key,value in style.items():
        common+=f'#define XIAOTAI_WECHAT_CALL_{key.upper()} 0x{value}U\n'
    common+='''static inline uint8_t xiaotai_icon_alpha4(const uint8_t *map,
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
'''
    outputs={'style.h':common.encode()}
    for name,(w,h) in manifest['variants'].items():
        if w<=0 or h<=0 or w!=h or w%2:
            raise ValueError('variants must be positive square sizes with even width')
        alpha=render_alpha4(contours,w,h)
        packed=bytes((alpha[i]//17<<4)|(alpha[i+1]//17) for i in range(0,len(alpha),2))
        body='\n'.join('    '+', '.join(f'0x{v:02x}' for v in packed[i:i+14])+',' for i in range(0,len(packed),14))
        text=f'''/* Generated by tools/generate_ui_icons.py. Do not edit.
 * Source SHA-256: {manifest['source_sha256']}
 */
#ifndef XIAOTAI_WECHAT_CALL_{name.upper()}_H
#define XIAOTAI_WECHAT_CALL_{name.upper()}_H
#include "style.h"
#define XIAOTAI_WECHAT_CALL_{name.upper()}_WIDTH {w}U
#define XIAOTAI_WECHAT_CALL_{name.upper()}_HEIGHT {h}U
static const uint8_t xiaotai_wechat_call_{name}[] = {{
{body}
}};
#endif
'''
        outputs[f'icon_{name}.h']=text.encode()
        outputs[f'icon_{name}.a4']=packed
        outputs[f'icon_{name}.png']=png(w,h,alpha)
    return outputs

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
    for name,data in generate().items():
        path=ASSETS/name
        if args.check:
            if not path.is_file() or path.read_bytes()!=data:
                raise SystemExit(f'stale UI resource: {path}')
        else:
            path.write_bytes(data)
    print('Shared UI icon variants are current' if args.check else 'Generated shared UI icon variants')

if __name__=='__main__':
    main()
