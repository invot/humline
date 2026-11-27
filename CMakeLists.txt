cmake_minimum_required(VERSION 3.22)

project(Humline VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    # JUCE 6.1.6's VST3 client relies on tentative definitions for the VST3
    # interface IIDs. GCC 10+ defaults to -fno-common, which turns those into
    # "multiple definition" link errors, so keep common symbols on MinGW.
    add_compile_options (-fcommon)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(DBC_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/JUCE-8.0.6" CACHE PATH "JUCE source tree")

set(DBC_CORE_SOURCES
    Source/core/Chords.cpp
    Source/core/DrumClassifier.cpp
    Source/core/InstrumentProbe.cpp
    Source/core/KeyDetector.cpp
    Source/core/OnsetDetector.cpp
    Source/core/PitchDetector.cpp
    Source/core/Quantizer.cpp
    Source/core/VoiceEngine.cpp)

# ---------------------------------------------------------------------------
# JUCE-free test binary for the analysis engine.
# ---------------------------------------------------------------------------
add_executable(HumlineTests Tests/main.cpp ${DBC_CORE_SOURCES})
target_include_directories(HumlineTests PRIVATE Source)
set_target_properties(HumlineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (NOT MSVC)
    target_compile_options(HumlineTests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# The plugin itself.
# ---------------------------------------------------------------------------
if (EXISTS "${DBC_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${DBC_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

    juce_add_plugin(Humline
        COMPANY_NAME              "Codebuff"
        BUNDLE_ID                 "com.codebuff.humline"
        PLUGIN_MANUFACTURER_CODE  Cdbx
        PLUGIN_CODE               Hmln
        PRODUCT_NAME              "Humline"
        DESCRIPTION               "Sing or beatbox into your mic and get quantised MIDI"
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          TRUE
        NEEDS_MIDI_OUTPUT         TRUE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        FORMATS                   VST3 Standalone
        VST3_CATEGORIES           "Fx"
        AAX_CATEGORY              AAX_ePlugInCategory_None)

    target_sources(Humline PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StandaloneBridge.cpp
        ${DBC_CORE_SOURCES})

    target_include_directories(Humline PRIVATE Source)

    target_compile_definitions(Humline PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(Humline PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    if (NOT MSVC)
        # A plugin gets loaded by a host that knows nothing about our toolchain,
        # so the runtimes have to be inside the binary. JUCE does the final link
        # on the per-format targets, so the flags go there.
        foreach (dbcTarget Humline Humline_VST3 Humline_Standalone)
            if (TARGET ${dbcTarget})
                target_link_options(${dbcTarget} PRIVATE -static -static-libgcc -static-libstdc++)
            endif()
        endforeach()
    endif()

    # Small console harness: builds the processor + editor without a host.
    add_executable(HumlineSmoke
        Tests/smoke.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StandaloneBridge.cpp
        ${DBC_CORE_SOURCES})
    target_include_directories(HumlineSmoke PRIVATE Source)
    target_compile_definitions(HumlineSmoke PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(HumlineSmoke PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    if (NOT MSVC)
        target_link_options(HumlineSmoke PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(HumlineSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
else()
    message(WARNING "JUCE not found at ${DBC_JUCE_DIR}; only the test target will be built.")
endif()
