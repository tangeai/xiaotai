#include <stdint.h>
#include <stdio.h>

#include "starter_media_camera_policy.h"

int main(void)
{
    if (!starter_media_camera_sensor_supported(GC0308_PID)) {
        fputs("FAIL: documented GC0308 is not accepted\n", stderr);
        return 1;
    }
    if (!starter_media_camera_sensor_supported(GC2145_PID)) {
        fputs("FAIL: boot-probed GC2145 would be rejected with ESP_ERR_NOT_FOUND\n", stderr);
        return 1;
    }
    if (starter_media_camera_sensor_supported(OV2640_PID)) {
        fputs("FAIL: unobserved sensor was accepted without board evidence\n", stderr);
        return 1;
    }
    puts("PASS: camera policy accepts documented GC0308 and boot-probed GC2145 only");
    return 0;
}
