add_executable(djr_studio_basic_tests basic_tests.cpp)
target_compile_features(djr_studio_basic_tests PRIVATE cxx_std_20)
add_test(NAME djr_studio_basic_tests COMMAND djr_studio_basic_tests)

# Drives the real mixer offline, so the audio graph is checked without a device.
juce_add_console_app(djr_studio_engine_tests
    PRODUCT_NAME "DJR_Studio Engine Tests")

target_sources(djr_studio_engine_tests PRIVATE
    engine_tests.cpp
    ${DJR_ENGINE_SOURCES})

target_include_directories(djr_studio_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(djr_studio_engine_tests
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="DJR_Studio Engine Tests"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=1
        JUCE_PLUGINHOST_AU=0
        JUCE_ALSA=1
        JUCE_JACK=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0)

target_compile_options(djr_studio_engine_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

target_link_libraries(djr_studio_engine_tests
    PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        PkgConfig::ALSA)

add_test(NAME djr_studio_engine_tests COMMAND djr_studio_engine_tests)
