# SPDX-License-Identifier: Apache-2.0

add_executable(denoise-pipewire-filter
    denoise_pipewire_filter.cpp
)
target_link_libraries(denoise-pipewire-filter PRIVATE denoise_core PkgConfig::PIPEWIRE)
