cmake_minimum_required(VERSION 3.22)
project(StringLab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(StringLab
    COMPANY_NAME "String Lab"
    BUNDLE_ID "com.stringlab.instrument"
    PLUGIN_MANUFACTURER_CODE StLb
    PLUGIN_CODE Stl1
    FORMATS VST3 Standalone
    PRODUCT_NAME "String Lab"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(StringLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/AmpEffects.h
    Source/StringEngine.h)
target_compile_definitions(StringLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(StringLab PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    add_executable(StringEngineTests Tests/StringEngineTests.cpp)
    target_include_directories(StringEngineTests PRIVATE Source)
    add_test(NAME StringEngineTests COMMAND StringEngineTests)

    add_executable(AmpEffectsTests Tests/AmpEffectsTests.cpp)
    target_include_directories(AmpEffectsTests PRIVATE Source)
    add_test(NAME AmpEffectsTests COMMAND AmpEffectsTests)

    juce_add_console_app(StringLabPluginTests PRODUCT_NAME "String Lab Plugin Tests")
    target_sources(StringLabPluginTests PRIVATE Tests/PluginTests.cpp)
    target_compile_definitions(StringLabPluginTests PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(StringLabPluginTests PRIVATE juce::juce_audio_utils)
    add_dependencies(StringLabPluginTests StringLab_VST3)
    add_test(NAME StringLabPluginTests COMMAND StringLabPluginTests
        "${CMAKE_CURRENT_BINARY_DIR}/StringLab_artefacts/$<CONFIG>/VST3/String Lab.vst3")

    juce_add_console_app(StringLabAudioProbe PRODUCT_NAME "String Lab Audio Probe")
    target_sources(StringLabAudioProbe PRIVATE Tools/AudioProbe.cpp)
    target_include_directories(StringLabAudioProbe PRIVATE Source)
    target_compile_definitions(StringLabAudioProbe PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(StringLabAudioProbe PRIVATE juce::juce_audio_utils)
    add_dependencies(StringLabAudioProbe StringLab_VST3)
    add_test(NAME StringLabAudioProbe COMMAND StringLabAudioProbe
        "${CMAKE_CURRENT_BINARY_DIR}/StringLab_artefacts/$<CONFIG>/VST3/String Lab.vst3"
        "${CMAKE_CURRENT_BINARY_DIR}/AudioProbe")
endif()
