# SPDX-License-Identifier: Apache-2.0
#
# denoise_windows_apo_processor is the portable half of the Windows backend
# (see denoise_apo_processor.h) -- it has no Windows SDK dependency, so
# unlike DeNoiseApo.{h,cpp} (the actual COM/APO shim, Windows-only and
# UNVERIFIED -- see DeNoiseApo.h) it is built and tested on every platform,
# including in this project's own CI.

add_library(denoise_windows_apo_processor STATIC
    denoise_apo_processor.cpp
)
target_include_directories(denoise_windows_apo_processor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(denoise_windows_apo_processor PUBLIC denoise_core)

# DeNoiseApo.cpp (the actual IAudioProcessingObjectRT COM shim) is
# intentionally not built anywhere yet -- DeNoiseApo.h requires Windows SDK
# headers this project has never had access to. See its file header.
