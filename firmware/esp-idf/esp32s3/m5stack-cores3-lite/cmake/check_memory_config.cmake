# Called after IDF project() has loaded the effective sdkconfig.
# Defaults alone cannot correct a retained sdkconfig from an older build.
# Hardware evidence: CoreS3 schematic v1.0 page 3, U7 SIO0..SIO3.
if(NOT CONFIG_SPIRAM OR NOT CONFIG_SPIRAM_MODE_QUAD OR CONFIG_SPIRAM_MODE_OCT)
    message(FATAL_ERROR
        "CoreS3-Lite requires Quad PSRAM (U7 SIO0..SIO3). "
        "Update the existing sdkconfig: CONFIG_SPIRAM=y, "
        "CONFIG_SPIRAM_MODE_QUAD=y, and disable CONFIG_SPIRAM_MODE_OCT. "
        "Changing sdkconfig.defaults alone does not override saved settings.")
endif()
