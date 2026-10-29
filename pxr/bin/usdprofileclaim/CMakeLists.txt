set(PXR_PREFIX pxr/usd)
set(PXR_PACKAGE usd)

pxr_cpp_bin(usdprofileclaim
        LIBRARIES
        ar
        js
        sdf
        tf
        usd
        usdProfiles
        )

# This test runs the usdprofileclaim tool multiple times to exercise its
# options and check its correctness.
pxr_register_test(testUsdProfileClaim
    PYTHON
    COMMAND "${CMAKE_INSTALL_PREFIX}/tests/ctest/testUsdProfileClaim/runTest.py"
    TESTENV testenv/testUsdProfileClaim
    EXPECTED_RETURN_CODE 0
    PRE_PATH
        ${CMAKE_INSTALL_PREFIX}/bin
)
