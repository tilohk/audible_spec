cmake_minimum_required(VERSION 3.22)

project(ImageToSound VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# JUCE is pinned to a release tag. Bump it deliberately, never track a branch.
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

enable_testing()

# ------------------------------------------------------------------
# DSP core: the C++ port of main.py. An INTERFACE library, so every
# target (plugin, tests, CLI) compiles it together with its own copy of
# the JUCE modules; that avoids duplicate JUCE symbols.
# ------------------------------------------------------------------
set(ITS_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/RenderParams.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/ImageProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Spectrogram.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/GriffinLim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Renderer.cpp)

# Image decoding: macOS ImageIO on Apple; elsewhere JUCE (PNG/JPEG/GIF) + libheif (HEIC).
# ITS_PORTABLE_DECODER=ON builds the Windows/Linux decoder on macOS too (for testing).
option(ITS_PORTABLE_DECODER "Use the Windows/Linux image decoder" OFF)
if(APPLE AND NOT ITS_PORTABLE_DECODER)
    list(APPEND ITS_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Source/image/ImageDecoder.cpp)
else()
    list(APPEND ITS_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Source/image/ImageDecoderPortable.cpp)
    find_package(libheif CONFIG QUIET)
    if(NOT libheif_FOUND AND NOT APPLE)
        message(FATAL_ERROR "libheif not found: configure with the vcpkg toolchain (see README)")
    endif()
endif()

# Rendering is far too slow unoptimised, so the DSP core is always built optimised
# (Debug builds keep their debug info). MSVC: /O2 clashes with Debug's /RTC1, so Release only.
if(NOT MSVC)
    set_source_files_properties(${ITS_CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
endif()

add_library(ImageToSoundCore INTERFACE)
target_sources(ImageToSoundCore INTERFACE ${ITS_CORE_SOURCES})
target_include_directories(ImageToSoundCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
if(MSVC)
    target_compile_definitions(ImageToSoundCore INTERFACE _USE_MATH_DEFINES) # M_PI in <cmath>
endif()
target_link_libraries(ImageToSoundCore INTERFACE
    juce::juce_core
    juce::juce_dsp)
if(APPLE AND NOT ITS_PORTABLE_DECODER)
    target_link_libraries(ImageToSoundCore INTERFACE "-framework ImageIO" "-framework CoreGraphics")
else()
    target_link_libraries(ImageToSoundCore INTERFACE juce::juce_graphics)
    target_compile_definitions(ImageToSoundCore INTERFACE ITS_PORTABLE_DECODER=1)
    if(libheif_FOUND)
        target_link_libraries(ImageToSoundCore INTERFACE heif)
        target_compile_definitions(ImageToSoundCore INTERFACE ITS_HAVE_LIBHEIF=1)
    endif()
endif()

# FFT: macOS uses Apple's vDSP through JUCE. Elsewhere JUCE's fallback FFT is ~10x slower,
# so Windows/Linux link FFTW (GPL, compatible with the AGPL) statically from vcpkg.
if(NOT APPLE)
    find_package(FFTW3f CONFIG REQUIRED)
    target_link_libraries(ImageToSoundCore INTERFACE FFTW3::fftw3f)
    target_compile_definitions(ImageToSoundCore INTERFACE JUCE_DSP_USE_STATIC_FFTW=1)
endif()

set(ITS_CONSOLE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

# Playback engine + parameters: shared by the plugin and the tests
set(ITS_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/audio/SamplerEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/RenderJob.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/DisplayData.cpp)

# Plugin processor + editor (used by the plugin and the UI snapshot tool)
set(ITS_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Views.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/Panels.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/audio/AudioCodec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Presets.cpp)

# Embedded files: default image (generated test image) and the built-in presets.
file(GLOB ITS_PRESET_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../presets/*.txt)
juce_add_binary_data(ImageToSoundAssets SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../reference/test_images/scene_portrait.heic
    ${ITS_PRESET_FILES})

# Tests: DSP stages vs the Python fixtures, sampler engine, parameters (run with ctest)
juce_add_console_app(DspTests PRODUCT_NAME DspTests)
target_sources(DspTests PRIVATE
    ${ITS_AUDIO_SOURCES}
    Tests/TestMain.cpp
    Tests/AllocationCounter.cpp
    Tests/Npy.cpp
    Tests/DspTests.cpp
    Tests/SamplerTests.cpp
    Tests/RenderJobTests.cpp
    Tests/DisplayTests.cpp)
target_compile_definitions(DspTests PRIVATE
    ${ITS_CONSOLE_DEFINITIONS}
    ITS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(DspTests PRIVATE
    ImageToSoundCore
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME dsp_tests COMMAND DspTests)

# render_cli: image -> WAV from the command line
juce_add_console_app(RenderCli PRODUCT_NAME render_cli)
target_sources(RenderCli PRIVATE Tools/RenderCli.cpp)
set_target_properties(RenderCli PROPERTIES OUTPUT_NAME render_cli)
target_compile_definitions(RenderCli PRIVATE ${ITS_CONSOLE_DEFINITIONS})
target_link_libraries(RenderCli PRIVATE
    ImageToSoundCore
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# UI smoke test: real processor + editor off-screen, writes a PNG of the editor
juce_add_console_app(UiSnapshot PRODUCT_NAME UiSnapshot)
target_sources(UiSnapshot PRIVATE ${ITS_AUDIO_SOURCES} ${ITS_PLUGIN_SOURCES} Tools/UiSnapshot.cpp)
target_compile_definitions(UiSnapshot PRIVATE
    ${ITS_CONSOLE_DEFINITIONS}
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="ImageToSound")
target_link_libraries(UiSnapshot PRIVATE
    ImageToSoundCore
    ImageToSoundAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME ui_snapshot COMMAND UiSnapshot ${CMAKE_CURRENT_BINARY_DIR}/ui_snapshot.png)

# Plugin-level tests: project state save/load, presets (real processor, no host)
juce_add_console_app(PluginTests PRODUCT_NAME PluginTests)
target_sources(PluginTests PRIVATE ${ITS_AUDIO_SOURCES} ${ITS_PLUGIN_SOURCES}
    Tests/TestMain.cpp
    Tests/StateTests.cpp)
target_compile_definitions(PluginTests PRIVATE
    ${ITS_CONSOLE_DEFINITIONS}
    JucePlugin_Name="ImageToSound")
target_link_libraries(PluginTests PRIVATE
    ImageToSoundCore
    ImageToSoundAssets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME plugin_tests COMMAND PluginTests)

# ------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------
# Formats per platform: AU only exists on macOS, LV2 is for Linux hosts.
if(APPLE)
    set(ITS_FORMATS VST3 AU Standalone)
elseif(UNIX)
    set(ITS_FORMATS VST3 LV2 Standalone)
else()
    set(ITS_FORMATS VST3 Standalone)
endif()
# CI turns this off: it packages the build output instead of installing it.
option(ITS_INSTALL_PLUGIN "Copy the plug-ins into the user's plug-in folders after building" ON)

juce_add_plugin(ImageToSound
    PRODUCT_NAME "audible_spec"     # shown in the DAW; codes below keep it the same plugin
    COMPANY_NAME "Tilo Hauk"
    BUNDLE_ID com.tilohauk.imagetosound
    PLUGIN_MANUFACTURER_CODE Tlhk   # 4 chars, at least one upper case
    PLUGIN_CODE I2Sd                # 4 chars, unique per plugin
    FORMATS ${ITS_FORMATS}
    LV2URI https://github.com/tilohk/audible_spec
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${ITS_INSTALL_PLUGIN})   # installs into the user's plug-in folders

target_sources(ImageToSound PRIVATE ${ITS_AUDIO_SOURCES} ${ITS_PLUGIN_SOURCES})

target_compile_definitions(ImageToSound PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ImageToSound
    PRIVATE
        ImageToSoundCore
        ImageToSoundAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
