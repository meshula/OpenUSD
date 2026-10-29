set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usd)

pxr_cpp_bin(usdprofilegraph
        LIBRARIES
        ar
        sdf
        tf
        usd
        usdProfiles
        )
