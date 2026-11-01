cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime: the plugin needs no VC++ redistributable on the user's PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

project(Nokia3310Plugin VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise fetch it
if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

if(NOT N3310_FORMATS)
  if(APPLE)
    set(N3310_FORMATS VST3 AU Standalone)
  else()
    set(N3310_FORMATS VST3 Standalone)
  endif()
endif()

juce_add_plugin(N3310
  COMPANY_NAME "Underwater Palace"
  BUNDLE_ID com.underwaterpalace.n3310
  PLUGIN_MANUFACTURER_CODE Upal
  PLUGIN_CODE N331
  FORMATS ${N3310_FORMATS}
  PRODUCT_NAME "3310"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(N3310Assets SOURCES Assets/phone.png)

target_sources(N3310 PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/PixelArt.h
  Source/Dsp/Primitives.h
  Source/Dsp/Codec.h
  Source/Dsp/Telephone.h)

target_include_directories(N3310 PRIVATE Source Source/Dsp)

target_compile_definitions(N3310 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_link_libraries(N3310
  PRIVATE
    N3310Assets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# Offline DSP test (no JUCE): cmake --build . --target dsp_test
add_executable(dsp_test EXCLUDE_FROM_ALL tests/dsp_test.cpp)
target_include_directories(dsp_test PRIVATE Source/Dsp)

# Headless plugin test (audio path + UI snapshots): -DN3310_BUILD_TESTS=ON
option(N3310_BUILD_TESTS "Build the headless plugin test" OFF)
if(N3310_BUILD_TESTS)
  juce_add_console_app(N3310Test PRODUCT_NAME "N3310Test")
  target_sources(N3310Test PRIVATE tests/plugin_test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_include_directories(N3310Test PRIVATE Source Source/Dsp)
  target_compile_definitions(N3310Test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(N3310Test PRIVATE N3310Assets juce::juce_audio_utils juce::juce_recommended_config_flags)

  # loads the built 3310.vst3 like a DAW would (needs the N3310_VST3 target built first)
  juce_add_console_app(N3310HostTest PRODUCT_NAME "N3310HostTest")
  target_sources(N3310HostTest PRIVATE tests/vst3_load_test.cpp)
  target_compile_definitions(N3310HostTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
                             JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(N3310HostTest PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
