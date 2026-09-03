# SPDX-License-Identifier: Apache-2.0
#
# UNTESTED: this has never been configured or built (no macOS SDK in the
# environment this project was developed in). It is a reasonable starting
# point -- an AudioServerPlugIn ships as a CFBundle named *.driver installed
# to /Library/Audio/Plug-Ins/HAL -- but expect to need an Info.plist (with
# the AudioServerPlugIn_* keys documented in AudioServerPlugIn.h), a
# provisioning/signing setup, and iteration against a real coreaudiod to
# get this loading. See README.md.

add_library(DeNoiseAudioDriver MODULE
    DeNoiseAudioDriver.cpp
)
target_link_libraries(DeNoiseAudioDriver PRIVATE
    denoise_core
    "-framework CoreAudio"
    "-framework CoreFoundation"
)
set_target_properties(DeNoiseAudioDriver PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)
