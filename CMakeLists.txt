cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel), runs on macOS 11+ including Sonoma 14.6.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
endif()

project(ScrapeScape VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE ------------------------------------------------------------------
# Uses a local checkout if -DJUCE_DIR=/path/to/JUCE is given, otherwise downloads it.
set(JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Plugin ----------------------------------------------------------------
if(APPLE)
    set(SCAPE_FORMATS AU VST3 Standalone)
else()
    set(SCAPE_FORMATS VST3 Standalone)
endif()

juce_add_plugin(ScrapeScape
    COMPANY_NAME "Scrape-Scape"
    BUNDLE_ID "com.scrapescape.scrapescape"
    PRODUCT_NAME "Scrape-Scape"
    PLUGIN_MANUFACTURER_CODE Scrs
    PLUGIN_CODE Scrp
    FORMATS ${SCAPE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED FALSE
    VST3_CATEGORIES Instrument Synth Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE
    HARDENED_RUNTIME_ENABLED FALSE)

target_sources(ScrapeScape PRIVATE
    Source/Engine/Elements.cpp
    Source/Engine/Voice.cpp
    Source/Engine/ScapeEngine.cpp
    Source/Engine/FxPad.cpp
    Source/Engine/SoundChain.cpp
    Source/Recorder.cpp
    Source/OfflineRenderer.cpp
    Source/Skins.cpp
    Source/UI/ImageOutliner.cpp
    Source/UI/ModKnob.cpp
    Source/UI/LfoPanel.cpp
    Source/UI/SoundPage.cpp
    Source/UI/VisualField.cpp
    Source/UI/Backdrop.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/SceneCanvas.cpp
    Source/UI/ScapeLookAndFeel.cpp)

target_include_directories(ScrapeScape PRIVATE Source Source/Engine Source/UI)

target_compile_definitions(ScrapeScape PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ScrapeScape
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Optional headless smoke test (cmake -DSCAPE_BUILD_TESTS=ON) -------------
option(SCAPE_BUILD_TESTS "Build the headless smoke test" OFF)
if(SCAPE_BUILD_TESTS)
    juce_add_console_app(ScrapeScapeTests PRODUCT_NAME "ScrapeScapeTests")
    get_target_property(SCAPE_SOURCES ScrapeScape SOURCES)
    target_sources(ScrapeScapeTests PRIVATE Tests/SmokeTest.cpp ${SCAPE_SOURCES})
    target_include_directories(ScrapeScapeTests PRIVATE Source Source/Engine Source/UI)
    target_compile_definitions(ScrapeScapeTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="Scrape-Scape")
    target_link_libraries(ScrapeScapeTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
