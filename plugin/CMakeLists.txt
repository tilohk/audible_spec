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
