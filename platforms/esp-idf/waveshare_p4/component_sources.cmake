# Shared implementation set for the Waveshare ESP32-P4 family component.
# The caller provides `ref` (its project-local main directory) and keeps the
# board-specific p4_video/display/pipeline code and renderer policy config in
# that project.
set(XIAOTAI_P4_FAMILY_SRCS
    "${xiaotai_platform}/p4_video_capture.c"
    "${xiaotai_platform}/hardware_board.c"
    "${xiaotai_platform}/camera_driver.c"
    "${xiaotai_platform}/media_dma_reserve.c"
    "${xiaotai_platform}/video_yuv420_scaler.c"
    "${xiaotai_platform}/video_frame_converter.c"
    "${xiaotai_platform}/app_memory_policy.c"
    "${xiaotai_platform}/call_video_renderer.c")

set(XIAOTAI_P4_PROJECT_MEDIA_SRCS
    "${ref}/drivers/display/display_driver.c"
    "${ref}/media/camera_pipeline.c"
    "${ref}/media/media_governor.c")

set(XIAOTAI_P4_INCLUDE_DIRS
    "${xiaotai_platform}"
    "${ref}/hardware"
    "${ref}/drivers/display"
    "${ref}/services")

set(XIAOTAI_P4_PRIVATE_INCLUDE_DIRS
    "${ref}/media"
    "${ref}/platform")
