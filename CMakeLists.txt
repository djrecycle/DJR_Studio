cmake_minimum_required(VERSION 3.22)

# Version -----------------------------------------------------------------
# The numeric part is set here; the rest is derived from git so a build can
# always be traced back to the exact commit it came from. Bump DJR_VERSION_BASE
# for a release and tag it; everything else follows.
set(DJR_VERSION_BASE "0.2.1")
set(DJR_VERSION_STAGE "beta")

# Deliberately the commit hash, not "git describe": once a release is tagged,
# describe would return the tag itself and the version would repeat itself as
# 0.2.0-beta+v0.2.0-beta. A hash always points at something traceable.
set(DJR_GIT_HASH "")
find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE DJR_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    # Mark builds made from uncommitted changes, so an artefact never claims to
    # be a commit it is not.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE DJR_GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    if(NOT DJR_GIT_DIRTY STREQUAL "")
        set(DJR_GIT_HASH "${DJR_GIT_HASH}-dirty")
    endif()
endif()

if(DJR_GIT_HASH STREQUAL "")
    set(DJR_VERSION_STRING "${DJR_VERSION_BASE}-${DJR_VERSION_STAGE}")
else()
    set(DJR_VERSION_STRING "${DJR_VERSION_BASE}-${DJR_VERSION_STAGE}+${DJR_GIT_HASH}")
endif()

message(STATUS "DJR_Studio version: ${DJR_VERSION_STRING}")

project(DJR_Studio
    VERSION ${DJR_VERSION_BASE}
    DESCRIPTION "Linux DAW with MIDI, recording, mixer, piano roll, and VST3 support"
    HOMEPAGE_URL "https://github.com/djrecycle/DJR_Studio"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(JACK IMPORTED_TARGET jack)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

set(DJR_JUCE_TAG "8.0.8" CACHE STRING "JUCE git tag to fetch")

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${DJR_JUCE_TAG}
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(JUCE)

# The DSP half of the app, shared with the offline engine tests. Absolute paths
# so the tests subdirectory can reuse the list.
set(DJR_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Transport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AutomationLane.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/ChannelSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Track.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioTrack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AudioClip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/TimeStretch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/BusTrack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/MidiTrack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/AlignmentDelay.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Mixer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/MasterBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/Metronome.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio/SimpleSynth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/EditHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/SessionState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/MidiClip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/PianoRollModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/midi/MidiEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/PluginChain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/Project.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/project/ProjectTrackLayout.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recording/Recorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recording/RecordingBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recording/SampleCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/export/ExportManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/export/TrackRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/Logger.cpp)

set(DJR_STUDIO_SOURCES
    ${DJR_ENGINE_SOURCES}
    src/main.cpp
    src/app/DJRStudioApplication.cpp
    src/app/MainWindow.cpp
    src/app/Localisation.cpp
    src/app/Settings.cpp
    src/app/TypingKeyboard.cpp
    src/audio/AudioEngine.cpp
    src/plugins/PluginManager.cpp
    src/plugins/AudioEditorProcessor.cpp
    src/plugins/PluginScanner.cpp
    src/plugins/PluginWindow.cpp
    src/project/ProjectManager.cpp
    src/project/ProjectSerializer.cpp
    src/ui/Knob.cpp
    src/ui/MainComponent.cpp
    src/ui/MenuBarView.cpp
    src/ui/TransportBar.cpp
    src/ui/BrowserPanel.cpp
    src/ui/ArrangementView.cpp
    src/ui/EditorPanel.cpp
    src/ui/MixerView.cpp
    src/ui/MixerChannelStrip.cpp
    src/ui/PanelWindow.cpp
    src/ui/PanelHost.cpp
    src/ui/PianoRollView.cpp
    src/ui/VelocityLane.cpp
    src/ui/StepSequencerView.cpp
    src/ui/SampleEditorView.cpp
    src/ui/PluginBrowserView.cpp
    src/ui/InsertChainPanel.cpp
    src/ui/StatusBar.cpp
    src/ui/ProjectManagerDialog.cpp
    src/ui/PreferencesDialog.cpp
    src/ui/Icons.cpp
    src/ui/UiControls.cpp
    src/ui/DJRLookAndFeel.cpp
    src/ui/Theme.cpp
    src/utils/FileUtils.cpp)

juce_add_gui_app(djr_studio
    PRODUCT_NAME "DJR_Studio"
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "DJR Audio Lab"
    COMPANY_WEBSITE "https://github.com/djrecycle"
    COMPANY_EMAIL "rangga@example.local"
    BUNDLE_ID "audio.djraudiolab.djrstudio"
    ICON_BIG "packaging/icons/djr_studio.png")

target_sources(djr_studio PRIVATE ${DJR_STUDIO_SOURCES})

target_include_directories(djr_studio PRIVATE src)

set_target_properties(djr_studio PROPERTIES
    OUTPUT_NAME "djr_studio")

target_compile_definitions(djr_studio
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:djr_studio,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="${DJR_VERSION_STRING}"
        DJR_STUDIO_VERSION_STRING="${DJR_VERSION_STRING}"
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_VST3=1
        # The Linux plugin library is overwhelmingly LV2 - on a typical Ubuntu
        # Studio install it is hundreds of plugins against a handful of VST3.
        # VST2 stays off: hosting it needs the Steinberg SDK, which is no longer
        # distributable, and Carla loaded as an LV2 covers that case anyway.
        JUCE_PLUGINHOST_LV2=1
        JUCE_PLUGINHOST_AU=0
        JUCE_ALSA=1
        JUCE_JACK=1
        JUCE_USE_CURL=1
        JUCE_WEB_BROWSER=0)

target_compile_options(djr_studio PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

target_link_libraries(djr_studio
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
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        PkgConfig::ALSA
        PkgConfig::CURL)

if(TARGET PkgConfig::JACK)
    target_link_libraries(djr_studio PRIVATE PkgConfig::JACK)
endif()

# Everything the app needs at runtime goes in one component. JUCE is pulled in
# with FetchContent and brings its own install rules - headers, cmake config,
# helper tools - which have no business in a user facing .deb, so the package is
# limited to this component below.
install(TARGETS djr_studio
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT runtime)

install(FILES packaging/djr_studio.desktop
    DESTINATION share/applications
    COMPONENT runtime)

install(FILES packaging/icons/djr_studio.png
    DESTINATION share/icons/hicolor/256x256/apps
    COMPONENT runtime)

install(DIRECTORY resources/
    DESTINATION share/djr_studio
    COMPONENT runtime)

option(DJR_STUDIO_BUILD_TESTS "Build DJR_Studio unit smoke tests" OFF)
if(DJR_STUDIO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

set(CPACK_GENERATOR "DEB")
# Package only our runtime files; see the install rules above for why.
# CPACK_COMPONENTS_ALL alone is ignored by the monolithic DEB generator, so the
# component has to be named here: "<build dir>;<project>;<component>;<subdir>".
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};runtime;/")
set(CPACK_PACKAGE_NAME "djr-studio")
set(CPACK_PACKAGE_VERSION "${DJR_VERSION_BASE}")
# A tilde sorts *below* the plain version in Debian, which is exactly what a
# pre-release should do: 0.2.0~beta upgrades cleanly to 0.2.0 later.
set(CPACK_DEBIAN_PACKAGE_VERSION "${DJR_VERSION_BASE}~${DJR_VERSION_STAGE}")
set(CPACK_PACKAGE_VENDOR "DJR Audio Lab")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Linux DAW (beta) - MIDI, audio, VST3, mixer, piano roll")
set(CPACK_PACKAGE_CONTACT "djrecycle <djrecycle@users.noreply.github.com>")
set(CPACK_PACKAGE_FILE_NAME "djr-studio-${DJR_VERSION_BASE}-${DJR_VERSION_STAGE}-Linux")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "djrecycle <djrecycle@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libasound2, libjack-jackd2-0, libfreetype6, libx11-6, libxext6, libxinerama1, libxcursor1, libxrandr2, libgl1, libcurl4")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")

include(CPack)
