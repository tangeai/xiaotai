#include "starter_jpeg_collector.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t output[8] = {0};
    starter_jpeg_collector_t collect = {
        .buffer = output,
        .capacity = sizeof(output),
    };

    if (starter_jpeg_collect(&collect, 0, "abc", 3) != 3 ||
        memcmp(output, "abc", 3) != 0 || collect.length != 3 ||
        collect.overflow) {
        fputs("FAIL: JPEG data chunk was not collected\n", stderr);
        return 1;
    }

    (void)starter_jpeg_collect(&collect, 3, NULL, 0);
    if (collect.overflow || collect.length != 3) {
        fputs("FAIL: terminal (NULL, 0) callback was treated as overflow\n",
              stderr);
        return 1;
    }

    if (starter_jpeg_collect(&collect, 7, "xy", 2) != 0 ||
        !collect.overflow) {
        fputs("FAIL: a real JPEG buffer overflow was not rejected\n", stderr);
        return 1;
    }

    puts("PASS: JPEG terminal callback preserves the completed frame");
    return 0;
}
