cmake_minimum_required(VERSION 3.22)

project(KlangbildKanalzug VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: liegt der Ordner "JUCE" neben dieser Datei, wird er benutzt,
# sonst wird JUCE automatisch von GitHub geladen.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(KB_FORMATS VST3 Standalone)
if(APPLE)
    set(KB_FORMATS AU VST3 Standalone)
endif()

juce_add_binary_data(KanalzugAssets SOURCES
    Resources/Yellowtail-Regular.ttf
    Resources/Oswald-Medium.ttf
    Resources/Oswald-Regular.ttf)

set(KB_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

#==============================================================================
# Plugin 1: Klangbild Kanalzug (alle Module)
juce_add_plugin(KlangbildKanalzug
    COMPANY_NAME "Klangbild"
    BUNDLE_ID "de.klangbild.kanalzug"
    PLUGIN_MANUFACTURER_CODE Klbd
    PLUGIN_CODE Kbkz
    FORMATS ${KB_FORMATS}
    PRODUCT_NAME "Klangbild Kanalzug"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KlangbildKanalzug PRIVATE
    Source/KanalzugProcessor.cpp
    Source/KanalzugEditor.cpp
    Shared/VintageUI.cpp
    Modules/Kal/PluginProcessor.cpp    Modules/Kal/PluginEditor.cpp
    Modules/Drive/PluginProcessor.cpp  Modules/Drive/PluginEditor.cpp
    Modules/Duo/PluginProcessor.cpp    Modules/Duo/PluginEditor.cpp
    Modules/Box/PluginProcessor.cpp    Modules/Box/PluginEditor.cpp
    Modules/Opto/PluginProcessor.cpp   Modules/Opto/PluginEditor.cpp
    Modules/EQ/PluginProcessor.cpp     Modules/EQ/PluginEditor.cpp
    Modules/Raum/PluginProcessor.cpp   Modules/Raum/PluginEditor.cpp)

target_include_directories(KlangbildKanalzug PRIVATE Shared)
target_include_directories(KlangbildKanalzug SYSTEM PRIVATE
    ThirdParty ThirdParty/NAM ThirdParty/nlohmann ThirdParty/AudioDSPTools ThirdParty/eigen)
target_compile_definitions(KlangbildKanalzug PUBLIC ${KB_DEFINES} KB_EMBEDDED=1 EIGEN_MPL2_ONLY=1)
target_link_libraries(KlangbildKanalzug
    PRIVATE KanalzugAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# NAM meldet seine Modelltypen über statische Objekte an. Diese Dateien werden deshalb
# direkt in jedes Format gelinkt (in der statischen Shared-Code-Bibliothek würde der
# Linker sie verwerfen und kein Profil ließe sich laden).
file(GLOB NAM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/NAM/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/NAM/*/*.cpp")
add_library(nam_core OBJECT ${NAM_SOURCES})
set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(nam_core PRIVATE -w)
target_compile_definitions(nam_core PRIVATE EIGEN_MPL2_ONLY=1)
target_include_directories(nam_core SYSTEM PRIVATE ThirdParty ThirdParty/NAM ThirdParty/nlohmann ThirdParty/eigen)
if(APPLE)
    set_target_properties(nam_core PROPERTIES OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}")
endif()
foreach(fmt ${KB_FORMATS})
    if(TARGET KlangbildKanalzug_${fmt})
        target_sources(KlangbildKanalzug_${fmt} PRIVATE $<TARGET_OBJECTS:nam_core>)
    endif()
endforeach()

#==============================================================================
# Plugin 2: Klangbild Drive (einzeln)
juce_add_plugin(KlangbildDrive
    COMPANY_NAME "Klangbild"
    BUNDLE_ID "de.klangbild.drive"
    PLUGIN_MANUFACTURER_CODE Klbd
    PLUGIN_CODE Kbdr
    FORMATS ${KB_FORMATS}
    PRODUCT_NAME "Klangbild Drive"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KlangbildDrive PRIVATE
    Shared/VintageUI.cpp
    Modules/Drive/PluginProcessor.cpp
    Modules/Drive/PluginEditor.cpp)
target_include_directories(KlangbildDrive PRIVATE Shared)
target_compile_definitions(KlangbildDrive PUBLIC ${KB_DEFINES})
target_link_libraries(KlangbildDrive
    PRIVATE KanalzugAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
