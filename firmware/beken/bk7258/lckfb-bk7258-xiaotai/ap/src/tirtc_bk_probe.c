#include "tirtc_bk_probe.h"

#include <driver/flash.h>
#include <driver/lcd.h>
#include "xiaotai_log.h"
#include <os/os.h>

#include "sys_driver.h"

#if CONFIG_SOC_BK7258
#define TIRTC_BK_SOC "BK7258"
#else
#error "this probe is for the BK7258 bk_avdk_smp v3.1.1 line"
#endif

#define SDK_VERSION "release/v3.1.1.8"
#define SDK_REVISION "1cfd56af09a3cb6470f35f1e0c604035ed1b6ee7"
#define BOARD_CONFIG_SHA256 "71aab64bd263cd6570402b81fa2d9990313699560b62f461d84ad9fc497c64d6"

static const char *test_name(tirtc_bk_test_t value)
{
    switch (value) {
    case TIRTC_BK_TEST_PASS: return "pass";
    case TIRTC_BK_TEST_FAIL: return "fail";
    default: return "not_run";
    }
}

static uint32_t flash_size_for_id(uint32_t id)
{
    switch (id) {
    case 0x0b4014: return 1U * 1024U * 1024U;
    case 0x1c7015:
    case 0x0b4015:
    case 0xc84015:
    case 0xc86515:
    case 0xc22315:
    case 0xeb6015: return 2U * 1024U * 1024U;
    case 0x1c7016:
    case 0x0b4016:
    case 0x0e4016:
    case 0x1c4116:
    case 0xc84016:
    case 0xc86516:
    case 0xef4016:
    case 0x204016: return 4U * 1024U * 1024U;
    case 0x0b4017:
    case 0x0b6017:
    case 0xc84017:
    case 0xc86517:
    case 0xcd6017: return 8U * 1024U * 1024U;
    case 0x0b6018:
    case 0x0b4018:
    case 0x5e5018:
    case 0xc86018:
    case 0x204118: return 16U * 1024U * 1024U;
    default: return 0;
    }
}

static void emit_endpoint(const tirtc_bk_audio_endpoint_t *endpoint)
{
    if (!endpoint->present) {
        BK_LOG_RAW("{\"state\":\"unknown\",\"method\":null,\"source\":null,"
                  "\"channels\":null,\"sample_rates_hz\":[],\"sample_bits\":[],"
                  "\"formats\":[],\"test\":\"not_run\"}");
        return;
    }
    BK_LOG_RAW("{\"state\":\"declared\",\"method\":\"compiled_board_profile\","
              "\"source\":\"lckfb-bk7258-xiaotai\",\"channels\":%u,"
              "\"sample_rates_hz\":[%u],\"sample_bits\":[%u],"
              "\"formats\":[\"pcm_s16le\"],\"test\":\"%s\"}",
              endpoint->channels, (unsigned)endpoint->sample_rate_hz,
              endpoint->sample_bits, test_name(endpoint->test));
}

void tirtc_bk_probe_emit(void)
{
    const lcd_device_t *lcd = (const lcd_device_t *)tirtc_bk_probe_active_lcd();
    tirtc_bk_audio_snapshot_t audio = {0};
    bool has_audio = tirtc_bk_probe_audio(&audio);
    uint32_t app_bytes = tirtc_bk_probe_application_usable_bytes();
    uint32_t flash_id = bk_flash_get_id();
    uint32_t mapped_bytes = flash_size_for_id(flash_id);
    bool flash_known = mapped_bytes != 0;

    BK_LOG_RAW("TIRTC_BK_PROBE {");
    BK_LOG_RAW("\"schema_version\":1,\"artifact_sha256\":null,");
    BK_LOG_RAW("\"identity\":{\"soc\":\"%s\",\"chip_id\":\"0x%08x\","
              "\"chip_revision\":null,"
              "\"board_model\":\"立创·实战派BK7258\","
              "\"pcb_revision\":null,\"sdk\":{\"name\":\"bk_avdk_smp\","
              "\"version\":\"%s\",\"revision\":\"%s\"},"
              "\"board_config_sha256\":\"%s\"},",
              TIRTC_BK_SOC, (unsigned)sys_drv_get_chip_id(),
              SDK_VERSION, SDK_REVISION, BOARD_CONFIG_SHA256);
    if (flash_id == 0) {
        BK_LOG_RAW("\"flash\":{\"state\":\"unknown\","
                   "\"method\":\"AP flash-client API unavailable\","
                   "\"source\":\"bk_avdk_smp flash_client stub\","
                   "\"jedec_id\":null,\"recognized\":null,"
                   "\"physical_bytes\":null,\"mapped_bytes\":null,"
                   "\"application_usable_bytes\":%u},", (unsigned)app_bytes);
    } else {
        BK_LOG_RAW("\"flash\":{\"state\":\"measured\","
                   "\"method\":\"bk_flash_get_id/official_id_table\","
                   "\"source\":\"runtime\",\"jedec_id\":\"0x%08x\","
                   "\"recognized\":%s,\"physical_bytes\":",
                   (unsigned)flash_id, flash_known ? "true" : "false");
        if (flash_known) BK_LOG_RAW("%u", (unsigned)mapped_bytes);
        else BK_LOG_RAW("null");
        BK_LOG_RAW(",\"mapped_bytes\":");
        if (flash_known) BK_LOG_RAW("%u", (unsigned)mapped_bytes);
        else BK_LOG_RAW("null");
        BK_LOG_RAW(",\"application_usable_bytes\":%u},", (unsigned)app_bytes);
    }
    BK_LOG_RAW("\"memory\":{\"internal_sram\":{\"state\":\"measured\","
              "\"total_bytes\":%u,\"free_boot_bytes\":%u,"
              "\"minimum_free_bytes\":%u,\"largest_free_block_bytes\":null},",
              (unsigned)rtos_get_total_heap_size(),
              (unsigned)rtos_get_free_heap_size(),
              (unsigned)rtos_get_minimum_free_heap_size());
    BK_LOG_RAW("\"psram\":{\"state\":\"measured\",\"present\":%s,"
              "\"total_bytes\":%u,\"free_boot_bytes\":%u,"
              "\"minimum_free_bytes\":%u,\"largest_free_block_bytes\":null}},",
              rtos_get_psram_total_heap_size() ? "true" : "false",
              (unsigned)rtos_get_psram_total_heap_size(),
              (unsigned)rtos_get_psram_free_heap_size(),
              (unsigned)rtos_get_psram_minimum_free_heap_size());

    if (lcd) {
        BK_LOG_RAW("\"display\":{\"state\":\"detected\","
                  "\"method\":\"bk_display_open\",\"source\":\"runtime\","
                  "\"controller\":\"ST7789V2\",\"bus\":\"SPI1\","
                  "\"width_px\":%u,\"height_px\":%u,\"width_mm\":null,"
                  "\"height_mm\":null,\"pixel_format\":\"RGB565\","
                  "\"test\":\"not_run\"},", lcd->width, lcd->height);
    } else {
        BK_LOG_RAW("\"display\":{\"state\":\"unknown\",\"method\":null,"
                  "\"source\":null,\"controller\":\"ST7789V2\","
                  "\"bus\":\"SPI1\",\"width_px\":320,\"height_px\":240,"
                  "\"width_mm\":null,\"height_mm\":null,"
                  "\"pixel_format\":\"RGB565\",\"test\":\"fail\"},");
    }
    BK_LOG_RAW("\"touch\":{\"state\":\"declared\","
              "\"method\":\"compiled_ft6336_profile\","
              "\"source\":\"LCKFB lcd_tp_test reference\","
              "\"controller\":\"FT6336\","
              "\"bus\":\"sim-I2C GPIO42/43\",\"points\":1,"
              "\"test\":\"not_run\"},");
    BK_LOG_RAW("\"camera\":{\"state\":\"declared\","
              "\"method\":\"compiled_dvp_h264_profile\","
              "\"source\":\"pinned-bsp\",\"sensor\":null,"
              "\"output_profiles\":[\"H264 640x480@20\"],"
              "\"test\":\"not_run\"},\"audio\":{\"microphone\":");
    emit_endpoint(&audio.microphone);
    BK_LOG_RAW(",\"speaker\":");
    emit_endpoint(&audio.speaker);
    if (has_audio) {
        BK_LOG_RAW(",\"duplex\":{\"simultaneous\":%s,"
                  "\"playback_reference\":%s,\"aec_available\":%s,"
                  "\"aec_test\":\"%s\"}},",
                  audio.simultaneous ? "true" : "false",
                  audio.playback_reference ? "true" : "false",
                  audio.aec_available ? "true" : "false",
                  test_name(audio.aec_test));
    } else {
        BK_LOG_RAW(",\"duplex\":{\"simultaneous\":null,"
                  "\"playback_reference\":null,\"aec_available\":null,"
                  "\"aec_test\":\"not_run\"}},");
    }
    BK_LOG_RAW("\"controls\":{\"verified_buttons\":0,"
              "\"intent_map_approved\":true},\"integration\":{"
              "\"tirtc_sdk_compatible\":null,"
              "\"audio_uplink_pipeline\":true,"
              "\"audio_downlink_pipeline\":true,"
              "\"video_uplink_pipeline\":true,"
              "\"session_arbiter\":true,"
              "\"device_call_protocol\":true,"
              "\"wechat_voip_protocol\":true},"
              "\"requested_features\":[\"h5_audio\",\"h5_video\","
              "\"h5_talkback\",\"ai_talk\",\"device_call\","
              "\"wechat_voip\"],\"issues\":["
              "\"active display/audio/camera probes not run\","
              "\"AP flash client cannot read JEDEC ID; collect it on CP\","
              "\"audio adapter lacks playback reference and AEC\","
              "\"TiRTC DTLS/WHIP runtime compatibility not verified\"]}\r\n");
}
