#!/usr/bin/env python3
"""Check bounded pre-render JPEG capture and lossless serial export."""
from pathlib import Path
import subprocess
import tempfile

from capture_downlink import DownlinkDump, until_downlink
from capture_uplink import fnv


root = Path(__file__).resolve().parents[5]
source = (root / "platforms/esp-idf/waveshare_p4/p4_video_capture.c").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#define DOWNLINK_CAPTURE_CAP 64
#define pdTRUE 1
static int s_mutex, available = 1;
static atomic_bool s_down_active;
static uint8_t storage[DOWNLINK_CAPTURE_CAP], *s_down_data=storage;
static size_t s_down_size;
static uint32_t s_down_generation=1;
static const char *s_down_reason;
static int xSemaphoreTake(int m, int ticks) { (void)m; assert(!ticks); return available; }
static void xSemaphoreGive(int m) { (void)m; }
'''
code += function("void p4_video_capture_offer_downlink(")
code += r'''
static void reset(void) {
 s_down_active=true; s_down_size=0; s_down_generation=1;
 s_down_reason="armed"; available=1;
}
int main(void) {
 const uint8_t jpeg[]={0xff,0xd8,1,2,0xff,0xd9};
 const uint8_t broken[]={0xff,0xd8,1,2,3,4};
 uint8_t large[DOWNLINK_CAPTURE_CAP+1]={0xff,0xd8};
 reset(); p4_video_capture_offer_downlink(broken,sizeof(broken),1);
 assert(s_down_active && !s_down_size);
 reset(); p4_video_capture_offer_downlink(jpeg,sizeof(jpeg),2);
 assert(!s_down_active && !s_down_size && !strcmp(s_down_reason,"session-changed"));
 reset(); available=0; p4_video_capture_offer_downlink(jpeg,sizeof(jpeg),1);
 assert(s_down_active && !s_down_size);
 available=1; p4_video_capture_offer_downlink(jpeg,sizeof(jpeg),1);
 assert(!s_down_active && s_down_size==sizeof(jpeg) && !memcmp(s_down_data,jpeg,sizeof(jpeg)));
 reset(); large[sizeof(large)-2]=0xff; large[sizeof(large)-1]=0xd9;
 p4_video_capture_offer_downlink(large,sizeof(large),1);
 assert(!s_down_active && !s_down_size && !strcmp(s_down_reason,"capacity"));
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="downlink-capture-test-") as directory:
    test_file = Path(directory) / "test.c"
    test_file.write_text(code)
    executable = Path(directory) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(test_file), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

jpeg = b"\xff\xd8frame\xff\xd9"
header = f"DCAP BEGIN bytes={len(jpeg)} fnv={fnv(jpeg):08x} generation=3"
dump = DownlinkDump()
dump.feed(header)
dump.feed("DCAP DATA 00000000 " + jpeg.hex())
assert dump.feed("DCAP END") and dump.data == jpeg
for line in ("DCAP DATA 00000001 " + jpeg.hex(), "DCAP END", "DCAP DATA 00000000 zz"):
    dump = DownlinkDump()
    dump.feed(header)
    try:
        dump.feed(line)
    except ValueError:
        pass
    else:
        raise AssertionError("corrupted downlink frame was accepted")


class FakeTerminal:
    def __init__(self, line):
        self.line = line

    def readline(self):
        return self.line


assert until_downlink(FakeTerminal(b"DCAP STATUS active=0 bytes=8\n"), "DCAP STATUS") == "DCAP STATUS active=0 bytes=8"
try:
    until_downlink(FakeTerminal(b"DCAP ERROR no frame\n"), "DCAP STATUS")
except RuntimeError as error:
    assert str(error) == "DCAP ERROR no frame"
else:
    raise AssertionError("device capture error was not reported")
print("PASS: bounded downlink JPEG capture and checked serial export")
