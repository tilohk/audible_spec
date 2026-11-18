cmake_minimum_required(VERSION 3.22)

project(ImageToSound VERSION 0.1.0 LANGUAGES C CXX)

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
add_library(ImageToSoundCore INTERFACE)
target_sources(ImageToSoundCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/RenderParams.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/ImageProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Spectrogram.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/GriffinLim.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp/Renderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/image/ImageDecoder.cpp)
target_include_directories(ImageToSoundCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(ImageToSoundCore INTERFACE
    juce::juce_core
    juce::juce_dsp
    "-framework ImageIO"
    "-framework CoreGraphics")

set(ITS_CONSOLE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

# Tests: compare every DSP stage with the Python fixtures (run with ctest)
juce_add_console_app(DspTests PRODUCT_NAME DspTests)
target_sources(DspTests PRIVATE
    Tests/TestMain.cpp
    Tests/Npy.cpp
    Tests/DspTests.cpp)
target_compile_definitions(DspTests PRIVATE
    ${ITS_CONSOLE_DEFINITIONS}
    ITS_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(DspTests PRIVATE
    ImageToSoundCore
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

# ------------------------------------------------------------------
# The plugin
# ------------------------------------------------------------------
juce_add_plugin(ImageToSound
    PRODUCT_NAME "Image To Sound"
    COMPANY_NAME "Tilo Hauk"
    BUNDLE_ID com.tilohauk.imagetosound
    PLUGIN_MANUFACTURER_CODE Tlhk   # 4 chars, at least one upper case
    PLUGIN_CODE I2Sd                # 4 chars, unique per plugin
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD TRUE)   # installs into ~/Library/Audio/Plug-Ins for Ableton

target_sources(ImageToSound PRIVATE
    Source/PluginProcessor.cpp)

target_compile_definitions(ImageToSound PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ImageToSound
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
