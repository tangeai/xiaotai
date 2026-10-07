#!/usr/bin/env python3
"""Production RGB preparation preserves the active rectangle and clears margins."""
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[5]
source = (repo / "platforms/esp-idf/waveshare_p4/video_frame_converter.c").read_text()
def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start)+2]

code = '#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n'
code += function("static void video_frame_swap_rgb565_bytes(")
code += function("static void video_frame_clear_margins(")
code += function("static void video_frame_swap_region(")
code += r'''
int main(void) {
    uint16_t storage[640*480+2];
    uint16_t *out=storage+2;
    const unsigned rects[][4]={{0,0,640,480},{140,0,360,480},{0,60,640,360},
                                  {3,5,630,470},{0,0,1,1},{1,1,639,479}};
    for(unsigned r=0;r<sizeof(rects)/sizeof(rects[0]);++r) {
        unsigned x=rects[r][0],y=rects[r][1],w=rects[r][2],h=rects[r][3];
        storage[0]=storage[1]=0xbeef;
        for(unsigned i=0;i<640*480;++i) out[i]=0x1234;
        video_frame_clear_margins(out,640,480,x,y,w,h);
        for(unsigned j=0;j<480;++j) for(unsigned i=0;i<640;++i)
            assert(out[j*640+i]==((i>=x&&i<x+w&&j>=y&&j<y+h)?0x1234:0));
        video_frame_swap_region(out,640,x,y,w,h);
        for(unsigned j=0;j<480;++j) for(unsigned i=0;i<640;++i)
            assert(out[j*640+i]==((i>=x&&i<x+w&&j>=y&&j<y+h)?0x3412:0));
        assert(storage[0]==0xbeef&&storage[1]==0xbeef);
    }
}
'''
with tempfile.TemporaryDirectory(prefix="video-regions-") as tmp:
    p=Path(tmp)
    (p/"test.c").write_text(code)
    subprocess.run(["cc","-O2","-Wall","-Wextra","-Werror","-fsanitize=undefined",
                    "-fno-sanitize-recover=all",str(p/"test.c"),"-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
assert source.count("video_frame_clear_margins(output,") == 2
assert source.count("video_frame_swap_region(output,") == 2
print("PASS: video active-area preservation, black margins and region-only byte swap")
