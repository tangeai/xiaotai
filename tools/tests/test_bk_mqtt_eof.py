"""The actual BK MQTT reader must terminate on peer close, including mid-frame."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'platforms/beken/runtime/src/xiaotai_platform_client.c'


def function(source, name):
    begin = source.index('static int ' + name + '(')
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


class BKMqttEof(unittest.TestCase):
    def test_eof_exits_without_polling_and_timeout_remains_idle(self):
        source = SOURCE.read_text()
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define BK_FAIL -1
#define BK_LOGD(...) ((void)0)
static const uint8_t *wire;
static unsigned wire_size, offset, calls;
static int timeout;
static uint32_t rtos_get_time(void) { return 0; }
static int transport_read(int fd, void *data, size_t size) {
    (void)fd;
    if (++calls > 10) { fputs("EOF busy loop detected\n",stderr); exit(90); }
    if (timeout) { errno=EAGAIN; return -1; }
    if (offset == wire_size) return 0;
    assert(size > 0);
    *(uint8_t *)data=wire[offset++]; return 1;
}
static void input(const uint8_t *bytes, unsigned count) {
    wire=bytes; wire_size=count; offset=0; calls=0; timeout=0;
}
''' + function(source, 'recv_exact') + '\n' + function(source, 'mqtt_read_frame') + r'''
int main(void) {
    uint8_t header=0, body[8]={0}; size_t size=0;
    input(NULL,0);
    assert(recv_exact(0,body,1) < 0 && calls == 1);
    input(NULL,0);
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) < 0 && calls == 1);
    const uint8_t truncated_header[]={0x30}; input(truncated_header,1);
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) < 0 && calls == 2);
    const uint8_t truncated_body[]={0x30,2,0x61}; input(truncated_body,3);
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) < 0 && calls == 4);
    input(NULL,0); timeout=1;
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) == 0 && calls == 1);
    const uint8_t ping[]={0xd0,0}; input(ping,2);
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) == 1 && header == 0xd0 && size == 0);
    const uint8_t message[]={0x30,2,0x61,0x62}; input(message,4);
    assert(mqtt_read_frame(0,&header,body,sizeof(body),&size) == 1 && size == 2 && body[0] == 0x61 && body[1] == 0x62);
    puts("PASS: MQTT EOF and truncated frames fail promptly; idle timeout and fragmented input preserved");
}
'''
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            source_file = temp / 'mqtt.c'
            source_file.write_text(harness)
            executable = temp / 'mqtt'
            subprocess.run(['cc', '-std=c11', str(source_file), '-o', str(executable)],
                           check=True, capture_output=True)
            result = subprocess.run([str(executable)], text=True, capture_output=True, timeout=2)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
