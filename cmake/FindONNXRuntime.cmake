# SPDX-License-Identifier: Apache-2.0
#
# Locates a prebuilt ONNX Runtime C/C++ distribution (the .tgz/.zip released
# at https://github.com/microsoft/onnxruntime/releases -- NOT built from
# source here). Set ONNXRUNTIME_ROOT to the extracted directory (the one
# containing include/ and lib/) if it isn't in one of the default search
# locations below.
#
# On success defines the imported target `onnxruntime::onnxruntime`.

set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to an extracted ONNX Runtime release (contains include/ and lib/)")

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS
        "${ONNXRUNTIME_ROOT}/include"
        "$ENV{ONNXRUNTIME_ROOT}/include"
    PATHS
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64/include"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/include"
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS
        "${ONNXRUNTIME_ROOT}/lib"
        "$ENV{ONNXRUNTIME_ROOT}/lib"
    PATHS
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64/lib"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime/lib"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
    REQUIRED_VARS ONNXRUNTIME_LIBRARY ONNXRUNTIME_INCLUDE_DIR
)

if(ONNXRuntime_FOUND AND NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)
