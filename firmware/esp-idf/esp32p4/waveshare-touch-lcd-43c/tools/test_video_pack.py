#!/usr/bin/env python3
"""Exercise production I420 packing, including crop/row alignment and guards."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[5]
source = (repo / "platforms/esp-idf/waveshare_p4/video_frame_converter.c").read_text()
start = source.index("static inline void video_frame_pack_ouev_row(")
end = source.index("static void video_frame_swap_rgb565_bytes(", start)
code = "#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n#include <stdlib.h>\n"
code += source[start:end]
code += r'''
int main(void) {
    const unsigned widths[] = {8, 10, 14, 16, 320, 640};
    for (unsigned k = 0; k < sizeof(widths)/sizeof(widths[0]); ++k) {
        unsigned w = widths[k], h = 8, bytes = w*h*3/2;
        for (unsigned offset = 0; offset < 4; ++offset) {
            uint8_t *input = malloc(bytes+4), *output = malloc(bytes+8);
            assert(input && output);
            uint8_t *src = input+offset, *dst = output+offset;
            for (unsigned i = 0; i < bytes; ++i) src[i] = (uint8_t)(i*17+29);
            for (unsigned crop = 0; crop <= 2; crop += 2) {
                unsigned cw=w-crop, ch=h-2, size=cw*ch*3/2;
                memset(output, 0xa5, bytes+8);
                video_frame_pack_i420_region_for_ppa(src,w,h,crop,2,cw,ch,dst);
                for (unsigned y=0; y<ch; ++y) {
                    for (unsigned x=0; x<cw; x+=2) {
                        unsigned pos=y*cw*3/2+x*3/2;
                        unsigned plane=w*h+((y&1)?w*h/4:0);
                        assert(dst[pos]==src[plane+(y+2)/2*(w/2)+(x+crop)/2]);
                        assert(dst[pos+1]==src[(y+2)*w+x+crop]);
                        assert(dst[pos+2]==src[(y+2)*w+x+crop+1]);
                    }
                }
                for(unsigned i=0;i<offset;++i) assert(output[i]==0xa5);
                for(unsigned i=offset+size;i<bytes+8;++i) assert(output[i]==0xa5);
            }
            free(input); free(output);
        }
    }
}
'''
with tempfile.TemporaryDirectory(prefix="video-pack-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("PASS: production I420 pack pixels, cropped/unaligned rows and guard bytes")
