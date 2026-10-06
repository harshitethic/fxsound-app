cmake_minimum_required(VERSION 3.22)
project(FxSoundMac VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include("${CMAKE_CURRENT_LIST_DIR}/../linux/dsp_sources.cmake")

file(GLOB_RECURSE FXSOUND_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/*.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/include/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/*.H"
)

# On macOS the default filesystem is case-insensitive. Put real upstream
# include directories before Linux's case-alias compatibility directory.
set(FXSOUND_INCLUDE_DIRS
  "${CMAKE_CURRENT_LIST_DIR}/compat"
  "${CMAKE_CURRENT_LIST_DIR}/../dsp"
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/ptutil/include"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/include"
)
foreach(h IN LISTS FXSOUND_HEADERS)
  get_filename_component(d "${h}" DIRECTORY)
  list(APPEND FXSOUND_INCLUDE_DIRS "${d}")
endforeach()
list(REMOVE_DUPLICATES FXSOUND_INCLUDE_DIRS)
list(APPEND FXSOUND_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../linux/compat")

add_library(fxsound_dsp STATIC
  ${FXSOUND_DSP_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/SLOUT/Slout.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/MTH/Mthfreq.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/MTH/MthBuffer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../linux/compat/reg_linux.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../linux/compat/support_linux.cpp"
)

target_include_directories(fxsound_dsp PRIVATE ${FXSOUND_INCLUDE_DIRS})
target_compile_definitions(fxsound_dsp PRIVATE
  FXSOUND_LINUX=1
  FXSOUND_MACOS=1
  PT_NON_MFC=1
  DSPSOFT_TARGET=1
  PT_DSP_BUILD=PT_DSP_DFX
)
target_compile_options(fxsound_dsp PRIVATE
  -Wno-write-strings
  -Wno-deprecated-declarations
  -Wno-nonportable-include-path
)
target_link_libraries(fxsound_dsp PUBLIC m pthread dl)

add_executable(dsp_smoke "${CMAKE_CURRENT_LIST_DIR}/../linux/tests/dsp_smoke.cpp")
target_include_directories(dsp_smoke PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../dsp/include")
target_link_libraries(dsp_smoke PRIVATE fxsound_dsp)

add_executable(preset_audio_test "${CMAKE_CURRENT_LIST_DIR}/../linux/tests/preset_audio_test.cpp")
target_include_directories(preset_audio_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../dsp/include")
target_link_libraries(preset_audio_test PRIVATE fxsound_dsp)

add_executable(fxsound_coreaudio
  "${CMAKE_CURRENT_LIST_DIR}/audio/fxsound_coreaudio.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/audio/FxNativeTapBridge.mm"
)
target_include_directories(fxsound_coreaudio PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
)
target_link_libraries(fxsound_coreaudio PRIVATE
  fxsound_dsp
  "-framework CoreAudio"
  "-framework CoreFoundation"
  "-framework Foundation"
)


# Native macOS FxSound desktop UI.
set(FXSOUND_JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.deps/JUCE" CACHE PATH "Path to JUCE 6.1.6")
if(EXISTS "${FXSOUND_JUCE_DIR}/CMakeLists.txt")
  # JUCE 6.1.6 uses std::exchange without directly including <utility>.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include utility")
  add_subdirectory("${FXSOUND_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

  juce_add_gui_app(fxsound_macos_ui
    PRODUCT_NAME "FxSound"
    COMPANY_NAME "FxSound Community Port"
    BUNDLE_ID "com.harshitethic.fxsound"
    ICON_BIG "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Images/fxsound_large.png"
    ICON_SMALL "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Images/fxsound.png"
  )

  target_sources(fxsound_macos_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxSoundMac.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxControllerMac.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxMacWindowBridge.mm"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui/FxAudioControlsLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui/FxAudioSliderLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui/FxBalanceSliderLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui/FxEqualizerLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui/FxVisualizerLinux.cpp"

    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/JuceLibraryCode/BinaryData.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxTheme.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxWindow.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxComboBox.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxPowerButton.cpp"
  )

  target_include_directories(fxsound_macos_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui"
    "${CMAKE_CURRENT_LIST_DIR}/../linux/ui"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/JuceLibraryCode"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI"
  )

  target_compile_definitions(fxsound_macos_ui PRIVATE
    FXSOUND_MACOS_UI=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
  )

  target_link_libraries(fxsound_macos_ui PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
  )
endif()
