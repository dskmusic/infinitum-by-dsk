cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(Infinitum VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- JUCE: third_party/JUCE (offline) or FetchContent (pinned tag) ----
set(INFINITUM_JUCE_TAG "8.0.17-security")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${INFINITUM_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- Plugin ----
juce_add_plugin(Infinitum
    COMPANY_NAME "DSK Music"
    COMPANY_WEBSITE "https://dskmusic.com"
    PRODUCT_NAME "Infinitum by DSK"
    BUNDLE_ID com.dskmusic.infinitum
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/branding/infinitum-icon.png"   # regenerate: InfinitumTool icon branding
    PLUGIN_MANUFACTURER_CODE Dskm
    PLUGIN_CODE Infm
    FORMATS VST3 AU Standalone   # AU only built on macOS
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

# ---- Shared code (plugin + tool/tests compile the same sources) ----
file(GLOB_RECURSE INFINITUM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
list(FILTER INFINITUM_SOURCES EXCLUDE REGEX ".*/src/PluginEntry\\.cpp$")
if(INFINITUM_NO_UI) # ponytail: temporary engine-only build while the UI is rewritten
    list(FILTER INFINITUM_SOURCES EXCLUDE REGEX ".*/src/(ui/.*|PluginEditor\\..*|gen/Generator\\..*)$")
endif()

file(GLOB INFINITUM_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
if(INFINITUM_RESOURCES)
    juce_add_binary_data(InfinitumData SOURCES ${INFINITUM_RESOURCES})
endif()

add_library(InfinitumShared INTERFACE)
target_sources(InfinitumShared INTERFACE ${INFINITUM_SOURCES})
target_include_directories(InfinitumShared INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(InfinitumShared INTERFACE
    INFINITUM_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_SILENCE_XCODE_15_LINKER_WARNING=1)
target_link_libraries(InfinitumShared INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
if(TARGET InfinitumData)
    target_link_libraries(InfinitumShared INTERFACE InfinitumData)
endif()
if(MSVC)
    target_compile_options(InfinitumShared INTERFACE /bigobj /utf-8)
endif()

target_sources(Infinitum PRIVATE src/PluginEntry.cpp)
target_link_libraries(Infinitum PRIVATE InfinitumShared)

# ---- Tool: unit tests, offline render, preset validation, factory bank, SFZ, screenshots ----
juce_add_console_app(InfinitumTool PRODUCT_NAME "InfinitumTool")
file(GLOB INFINITUM_TOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")
if(INFINITUM_NO_UI)
    list(FILTER INFINITUM_TOOL_SOURCES EXCLUDE REGEX ".*/tests/(TestsFactory|TestsGen)\\.cpp$")
    target_compile_definitions(InfinitumShared INTERFACE INFINITUM_NO_UI=1)
endif()
target_sources(InfinitumTool PRIVATE ${INFINITUM_TOOL_SOURCES})
target_include_directories(InfinitumTool PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(InfinitumTool PRIVATE InfinitumShared)

enable_testing()
add_test(NAME infinitum_unit COMMAND InfinitumTool test)
