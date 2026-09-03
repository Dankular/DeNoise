# SPDX-License-Identifier: Apache-2.0

add_library(denoise_core STATIC
    audio/ring_buffer.cpp
    audio/framer.cpp
    audio/resampler.cpp
    inference/onnx_model.cpp
    pipeline/pipeline.cpp
)

target_include_directories(denoise_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(denoise_core PUBLIC onnxruntime::onnxruntime)

target_compile_features(denoise_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(denoise_core PRIVATE /W4)
else()
    target_compile_options(denoise_core PRIVATE -Wall -Wextra)
endif()
