set(PXR_PREFIX pxr/usdImaging)
set(PXR_PACKAGE usdBakeMtlx)

# MaterialX's MaterialXRenderGlsl library, which is used by usdBakeMtlx,
# requires OpenGL.
if (NOT ${PXR_ENABLE_GL_SUPPORT} OR PXR_APPLE_EMBEDDED)
    message(STATUS
        "Skipping ${PXR_PACKAGE} because PXR_ENABLE_GL_SUPPORT is OFF or PXR_APPLE_EMBEDDED is ON")
    return()
endif()

# this tool has no public headers, so provide a dummy
# target for the cmake linking script.
if(NOT TARGET ${BIN_NAME}_headerfiles)
    add_custom_target(${BIN_NAME}_headerfiles)
endif()

pxr_cpp_bin(usdBakeMtlx
    LIBRARIES
        tf
        sdr
        usdMtlx
        usdShade
        hd
        hdMtlx
        usdImaging
        MaterialXCore
        MaterialXFormat
        MaterialXRenderGlsl

    CPPFILES
        bakeMaterialX
        usdBakeMtlx
)
