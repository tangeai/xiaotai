# JPEG conversion is a CPU-bound H5 hot path. Optimize only these translation
# units; retain the selected debug profile for sensors, DMA and product code.
function(xiaotai_optimize_camera_jpeg)
    idf_component_get_property(camera_lib espressif__esp32-camera COMPONENT_LIB)
    idf_component_get_property(camera_dir espressif__esp32-camera COMPONENT_DIR)
    set_property(SOURCE
        "${camera_dir}/conversions/jpge.cpp"
        "${camera_dir}/conversions/to_jpg.cpp"
        "${camera_dir}/conversions/yuv.c"
        TARGET_DIRECTORY ${camera_lib}
        APPEND PROPERTY COMPILE_OPTIONS -O2)
endfunction()
