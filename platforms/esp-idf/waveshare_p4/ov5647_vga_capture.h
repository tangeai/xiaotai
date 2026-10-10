/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <string.h>
#include "esp_err.h"
#include "esp_cam_sensor.h"
#include "ov5647_types.h"

/* Custom-format extension of the pinned Espressif 24 MHz RAW10 960p mode.
 * Preserve its PLL, MIPI timing, orientation and full sensor window. The
 * additional sensor subsampling produces VGA before CSI/ISP, not in PPA.
 * 0x35 increments and 0x3708/09 VGA sampling controls are also documented by
 * the upstream Linux OV5647 VGA mode; no Linux driver code is incorporated.
 * Hardware validation is required for Bayer phase, field of view and AE.
 * The sensor retains these pointers: storage must outlive close/reopen.
 */
static esp_err_t ov5647_build_vga_capture(const esp_cam_sensor_format_t *base,
                                         const esp_cam_sensor_format_t **result)
{
    static ov5647_reginfo_t registers[160];
    static esp_cam_sensor_isp_info_t isp = {.isp_v1_info = {.version = 1}};
    static esp_cam_sensor_format_t format;
    if (base == NULL || result == NULL || base->name == NULL ||
        strcmp(base->name, "MIPI_2lane_24Minput_RAW10_1280x960_binning_45fps") != 0 ||
        base->width != 1280 || base->height != 960 || base->fps != 45 ||
        base->xclk != 24000000 || base->format != ESP_CAM_SENSOR_PIXFORMAT_RAW10 ||
        base->regs == NULL || base->isp_info == NULL ||
        base->isp_info->isp_v1_info.version != 1 ||
        base->regs_size < 1 || base->regs_size > 157) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const ov5647_reginfo_t *source = base->regs;
    if (source[base->regs_size - 1].reg != 0xffff) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(registers, source, (size_t)base->regs_size * sizeof(registers[0]));
    for (int i = 0; i < base->regs_size - 1; ++i) {
        switch (registers[i].reg) {
        case 0x3808: registers[i].val = 0x02; break;
        case 0x3809: registers[i].val = 0x80; break;
        case 0x380a: registers[i].val = 0x01; break;
        case 0x380b: registers[i].val = 0xe0; break;
        case 0x3814:
        case 0x3815: registers[i].val = 0x35; break;
        default: break;
        }
    }
    int count = base->regs_size - 1;
    registers[count++] = (ov5647_reginfo_t){0x3708, 0x64};
    registers[count++] = (ov5647_reginfo_t){0x3709, 0x52};
    registers[count++] = (ov5647_reginfo_t){0xffff, 0x00};
    isp.isp_v1_info.pclk = base->isp_info->isp_v1_info.pclk;
    isp.isp_v1_info.exp_def = base->isp_info->isp_v1_info.exp_def;
    isp.isp_v1_info.gain_def = base->isp_info->isp_v1_info.gain_def;
    isp.isp_v1_info.bayer_type = base->isp_info->isp_v1_info.bayer_type;
    /* Match 0x380c-f; the pinned base metadata has HTS/VTS interchanged. */
    isp.isp_v1_info.hts = 1796;
    isp.isp_v1_info.vts = 1093;
    format = *base;
    format.name = "MIPI_2lane_24Minput_RAW10_640x480_subsample_45fps";
    format.width = 640;
    format.height = 480;
    format.regs = registers;
    format.regs_size = count;
    format.isp_info = &isp;
    *result = &format;
    return ESP_OK;
}
