# Shared media algorithms compiled into each board-specific starter_media
# component. This file deliberately does not call idf_component_register():
# codec, camera and DMA ownership remain with the selected board component.
set(XIAOTAI_STARTER_MEDIA_COMMON_SRCS
    "${CMAKE_CURRENT_LIST_DIR}/src/starter_aec.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/starter_audio_resampler.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/starter_preroll.c"
)
set(XIAOTAI_STARTER_MEDIA_COMMON_INCLUDE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
