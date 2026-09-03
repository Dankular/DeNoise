# SPDX-License-Identifier: Apache-2.0

set(DENOISE_TEST_MODELS_DIR "${CMAKE_SOURCE_DIR}/models/test")

function(denoise_add_test name)
    cmake_parse_arguments(ARG "" "" "EXTRA_LIBS" ${ARGN})
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE denoise_core ${ARG_EXTRA_LIBS})
    target_compile_definitions(${name} PRIVATE
        DENOISE_TEST_MODELS_DIR="${DENOISE_TEST_MODELS_DIR}")
    add_test(NAME ${name} COMMAND ${name})
    # The imported onnxruntime shared library isn't installed system-wide;
    # point the test binary at it directly at runtime.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "LD_LIBRARY_PATH=${ONNXRUNTIME_LIBRARY_DIR}:$ENV{LD_LIBRARY_PATH}")
endfunction()

get_filename_component(ONNXRUNTIME_LIBRARY_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)

denoise_add_test(test_ring_buffer)
denoise_add_test(test_framer)
denoise_add_test(test_resampler)
denoise_add_test(test_pipeline_passthrough)
denoise_add_test(test_pipeline_onnx)
denoise_add_test(test_apo_processor EXTRA_LIBS denoise_windows_apo_processor)
