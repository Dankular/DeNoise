# SPDX-License-Identifier: Apache-2.0

add_executable(denoise-cli
    main.cpp
    wav_io.cpp
)
target_link_libraries(denoise-cli PRIVATE denoise_core)
