#!/usr/bin/env python3
"""Exercise production camera format negotiation at its V4L2 SDK seam."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLATFORM = ROOT / "platforms/esp-idf/waveshare_p4"


def function(source, name):
    start = source.index("static esp_err_t " + name + "(")
    return source[start:source.index("\n}", start) + 2]


source = (PLATFORM / "camera_driver.c").read_text()
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <linux/videodev2.h>
#include "ov5647_vga_capture.h"
#define VIDIOC_G_SENSOR_FMT 1001
#define VIDIOC_S_SENSOR_FMT 1002
#define TAG "test"
static void test_log(const char *tag,const char *fmt,...) {(void)tag;(void)fmt;}
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGW(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_RETURN_ON_ERROR(expr, ...) do {int r=(expr);if(r)return r;} while(0)
#define CAMERA_DRIVER_TARGET_FORMAT V4L2_PIX_FMT_YUV420
static int s_fd=3;
static uint16_t s_target_width=640,s_target_height=480;
static struct v4l2_format s_active_format;
static esp_cam_sensor_format_t sensor;
static unsigned sets, pixel_sets;
static int fail_set, fail_get, fail_pixel;
static const esp_cam_sensor_format_t *retained;
static void camera_driver_log_supported_formats(void) {}
static void camera_driver_format_to_str(uint32_t f,char *s) {(void)f;s[0]=0;}
static void camera_driver_prepare_pix_format(struct v4l2_format *f,uint32_t p) {
 f->fmt.pix.width=s_target_width;f->fmt.pix.height=s_target_height;f->fmt.pix.pixelformat=p;
}
static int ioctl(int fd,unsigned long op,...) {
 va_list ap;va_start(ap,op);void *arg=va_arg(ap,void*);va_end(ap);
 assert(fd==s_fd);
 if(op==VIDIOC_G_SENSOR_FMT){if(fail_get)return -1;*(esp_cam_sensor_format_t*)arg=sensor;return 0;}
 if(op==VIDIOC_S_SENSOR_FMT){sets++;if(fail_set)return -1;
  retained=arg;sensor=*(esp_cam_sensor_format_t*)arg;return 0;}
 struct v4l2_format *f=arg;
 if(op==VIDIOC_G_FMT){f->fmt.pix.width=sensor.width;f->fmt.pix.height=sensor.height;
  f->fmt.pix.pixelformat=V4L2_PIX_FMT_SBGGR10;return 0;}
 if(op==VIDIOC_S_FMT){pixel_sets++;if(fail_pixel)return -1;
  assert(f->fmt.pix.width==sensor.width && f->fmt.pix.height==sensor.height);
  /* Return the actual negotiated format on the next G_FMT. */
  return 0;}
 return -1;
}
'''
# G_FMT must expose RAW before pixel negotiation and YUV after it.
code = code.replace("f->fmt.pix.pixelformat=V4L2_PIX_FMT_SBGGR10;",
                    "f->fmt.pix.pixelformat=pixel_sets&&!fail_pixel?V4L2_PIX_FMT_YUV420:V4L2_PIX_FMT_SBGGR10;")
code += function(source, "camera_driver_select_sensor_format")
code += function(source, "camera_driver_config_format")
code += r'''
static uint8_t reg_value(const esp_cam_sensor_format_t *f,uint16_t addr) {
 const ov5647_reginfo_t *r=f->regs;
 for(int i=f->regs_size-1;i>=0;i--)if(r[i].reg==addr)return r[i].val;
 assert(0);return 0;
}
int main(void) {
 const ov5647_reginfo_t regs[]={
  {0x3036,0x6a},{0x3808,5},{0x3809,0},{0x380a,3},{0x380b,0xc0},
  {0x3814,0x31},{0x3815,0x31},{0x3820,1},{0x3821,3},
  {0x3800,0},{0x3801,24},{0x380c,7},{0x380d,4},{0x380e,4},{0x380f,0x45},
  {0xffff,0}};
 const esp_cam_sensor_isp_info_t isp={.isp_v1_info={.version=1,.pclk=88333333,.hts=1093,.vts=1796}};
 esp_cam_sensor_format_t base={.name="MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps",
  .width=1280,.height=960,.fps=45,.xclk=24000000,.format=ESP_CAM_SENSOR_PIXFORMAT_RAW10,
  .regs=regs,.regs_size=sizeof(regs)/sizeof(regs[0]),.isp_info=&isp};
 sensor=base;
 assert(camera_driver_config_format()==ESP_OK);
 assert(sets==1 && pixel_sets==1 && retained);
 assert(s_active_format.fmt.pix.width==640 && s_active_format.fmt.pix.height==480);
 assert(retained->width==640 && retained->height==480 && retained->fps==45);
 assert(retained->isp_info->isp_v1_info.hts==1796 && retained->isp_info->isp_v1_info.vts==1093);
 assert(reg_value(retained,0x3808)==2 && reg_value(retained,0x3809)==0x80);
 assert(reg_value(retained,0x380a)==1 && reg_value(retained,0x380b)==0xe0);
 assert(reg_value(retained,0x3814)==0x35 && reg_value(retained,0x3815)==0x35);
 assert(reg_value(retained,0x3820)==0x01); /* revert unhelpful register trial */
 assert(reg_value(retained,0x3036)==0x6a && reg_value(retained,0x3801)==24);
 assert(regs[1].val==5 && isp.isp_v1_info.hts==1093); /* managed source unchanged */
 assert(((const ov5647_reginfo_t*)retained->regs)[retained->regs_size-1].reg==0xffff);
 assert(camera_driver_config_format()==ESP_OK && sets==1); /* restart */
 s_target_width=1280;s_target_height=960;sensor=base;sets=0;pixel_sets=0;
 assert(camera_driver_config_format()==ESP_OK && sets==0); /* H5 */
 s_target_width=640;s_target_height=480;sensor=base;pixel_sets=0;fail_set=1;
 assert(camera_driver_config_format()!=ESP_OK && pixel_sets==0);
 fail_set=0;fail_get=1;assert(camera_driver_config_format()!=ESP_OK);fail_get=0;
 sensor=base;sensor.name="different-sensor";
 assert(camera_driver_config_format()!=ESP_OK); /* do not program unknown sensor */
 sensor=base;fail_pixel=1;
 assert(camera_driver_config_format()!=ESP_OK); /* no silent RAW fallback */
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="p4-native-vga-") as tmp:
    path = Path(tmp)
    (path / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_NOT_SUPPORTED 0x106\n#define ESP_ERR_INVALID_ARG 0x102\n")
    (path / "esp_cam_sensor.h").write_text('''#pragma once
#include <stdint.h>
typedef struct {uint16_t reg;uint8_t val;} ov5647_reginfo_t;
#define ESP_CAM_SENSOR_PIXFORMAT_RAW10 9
typedef struct {struct {const uint32_t version; int pclk,hts,vts; uint32_t exp_def,gain_def; int bayer_type;} isp_v1_info;} esp_cam_sensor_isp_info_t;
typedef struct {const char *name;uint16_t width,height;unsigned fps,xclk,format;
const void *regs;int regs_size;const esp_cam_sensor_isp_info_t *isp_info;} esp_cam_sensor_format_t;
''')
    (path / "ov5647_types.h").write_text('#include "esp_cam_sensor.h"\n')
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror",
                    "-I", str(path), "-I", str(PLATFORM), str(path / "test.c"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("PASS: native VGA sensor negotiation, persistent format, restarts and failures")

# Test the startup seam too: no scaler is needed for native VGA, which must
# not abort p4_video_init's existing prewarm chain.
pipeline = (ROOT / "firmware/esp-idf/esp32p4/waveshare-touch-lcd-43c/main/media/camera_pipeline.c").read_text()
start = pipeline.index("esp_err_t camera_pipeline_prewarm_call_scaler(")
prewarm = pipeline[start:pipeline.index("\n}", start) + 2]
prewarm_code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_RETURN_ON_ERROR(x,...) do {int r=(x);if(r)return r;} while(0)
#define ESP_RETURN_ON_FALSE(x,e,...) do {if(!(x))return e;} while(0)
#define ESP_LOGW(...) ((void)0)
#define APP_LOG_DETAIL(...) ((void)0)
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
typedef struct {int input_width,input_height,output_width,output_height;} video_yuv420_scaler_config_t;
typedef void *video_yuv420_scaler_handle_t;
static int s_lock, allocations;
static void *s_task;
static bool s_rtc_enabled,s_starting,s_scaler_reserve_in_progress;
static video_yuv420_scaler_handle_t s_reserved_call_scaler;
static int camera_pipeline_init(void){return 0;}
static bool camera_driver_is_configured(void){return true;}
static bool camera_pipeline_build_call_scaler_config(video_yuv420_scaler_config_t *c){
 *c=(video_yuv420_scaler_config_t){640,480,640,480};return false;
}
static bool video_yuv420_scaler_matches(void *s,const void *c){(void)s;(void)c;return false;}
static void video_yuv420_scaler_destroy(void *s){(void)s;}
static int video_yuv420_scaler_create(const void *c,void **s){(void)c;(void)s;allocations++;return 0;}
static int video_yuv420_scaler_warmup(void *s){(void)s;return 0;}
static bool camera_pipeline_scaler_store_reserved(void **s,const char *r){(void)s;(void)r;return true;}
''' + prewarm + r'''
int main(void){assert(camera_pipeline_prewarm_call_scaler()==ESP_OK);assert(allocations==0);}
'''
with tempfile.TemporaryDirectory(prefix="p4-vga-prewarm-") as tmp:
    path = Path(tmp)
    (path / "test.c").write_text(prewarm_code)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("PASS: native VGA skips scaler prewarm without failing video startup")
