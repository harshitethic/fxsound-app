cmake_minimum_required(VERSION 3.22)
project(FxSoundLinuxCore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include("${CMAKE_CURRENT_LIST_DIR}/dsp_sources.cmake")

file(GLOB_RECURSE FXSOUND_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/*.hpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/include/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/*.H"
)
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

add_library(fxsound_dsp STATIC
  ${FXSOUND_DSP_SOURCES}
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/SLOUT/Slout.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/MTH/Mthfreq.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../audiopassthru/src/MTH/MthBuffer.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/compat/reg_linux.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/compat/support_linux.cpp"
)

target_include_directories(fxsound_dsp PRIVATE ${FXSOUND_INCLUDE_DIRS})
target_compile_definitions(fxsound_dsp PRIVATE
  FXSOUND_LINUX=1
  PT_NON_MFC=1
  DSPSOFT_TARGET=1
  PT_DSP_BUILD=PT_DSP_DFX
)
target_compile_options(fxsound_dsp PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
  -Wno-write-strings
  -Wno-deprecated-declarations
)

target_link_libraries(fxsound_dsp PUBLIC m pthread dl)

add_executable(dsp_smoke "${CMAKE_CURRENT_LIST_DIR}/tests/dsp_smoke.cpp")
target_include_directories(dsp_smoke PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
)
target_link_libraries(dsp_smoke PRIVATE fxsound_dsp)

add_executable(preset_dump "${CMAKE_CURRENT_LIST_DIR}/tests/preset_dump.cpp")
target_include_directories(preset_dump PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
)
target_link_libraries(preset_dump PRIVATE fxsound_dsp)

add_executable(preset_audio_test "${CMAKE_CURRENT_LIST_DIR}/tests/preset_audio_test.cpp")
target_include_directories(preset_audio_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
)
target_link_libraries(preset_audio_test PRIVATE fxsound_dsp)

option(FXSOUND_BUILD_PIPEWIRE "Build the PipeWire realtime backend" ON)
if(FXSOUND_BUILD_PIPEWIRE)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
  endif()

  if(PIPEWIRE_FOUND)
    add_executable(pipewire_sink_probe
      "${CMAKE_CURRENT_LIST_DIR}/audio/pipewire_sink_probe.cpp"
    )
    target_link_libraries(pipewire_sink_probe PRIVATE PkgConfig::PIPEWIRE)

    add_executable(fxsound_pipewire
      "${CMAKE_CURRENT_LIST_DIR}/audio/fxsound_pipewire.cpp"
    )
    target_include_directories(fxsound_pipewire PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}/../dsp/include"
    )
    target_link_libraries(fxsound_pipewire PRIVATE fxsound_dsp PkgConfig::PIPEWIRE)
  else()
    message(STATUS "PipeWire development files not found; skipping PipeWire targets")
  endif()
endif()

# FxSound Linux desktop UI: reuse the upstream JUCE 6.1.6 theme/widgets.
# Pass -DFXSOUND_JUCE_DIR=/path/to/JUCE, or vendor JUCE at <repo>/JUCE.
set(FXSOUND_JUCE_DIR "" CACHE PATH "Path to JUCE 6.1.6")
if(NOT FXSOUND_JUCE_DIR AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../JUCE/CMakeLists.txt")
  set(FXSOUND_JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../JUCE")
endif()

if(EXISTS "${FXSOUND_JUCE_DIR}/CMakeLists.txt")
  # JUCE 6.1.6 predates GCC 15 and uses std::exchange without directly
  # including <utility> in one GUI header. Pre-include it rather than patching
  # the third-party JUCE checkout.
  if(UNIX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
      "$<$<COMPILE_LANGUAGE:CXX>:-include>"
      "$<$<COMPILE_LANGUAGE:CXX>:utility>"
    )
  endif()

  add_subdirectory("${FXSOUND_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

  juce_add_gui_app(fxsound_linux_ui
    PRODUCT_NAME "FxSound Linux"
    COMPANY_NAME "FxSound Linux"
  )

  target_sources(fxsound_linux_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxSoundLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxControllerLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxAudioControlsLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxAudioSliderLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxBalanceSliderLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxEqualizerLinux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui/FxVisualizerLinux.cpp"

    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/JuceLibraryCode/BinaryData.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxTheme.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxWindow.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxComboBox.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI/FxPowerButton.cpp"
  )

  target_include_directories(fxsound_linux_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/JuceLibraryCode"
    "${CMAKE_CURRENT_LIST_DIR}/../fxsound/Source/GUI"
  )

  target_compile_definitions(fxsound_linux_ui PRIVATE
    FXSOUND_LINUX_UI=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
  )

  target_link_libraries(fxsound_linux_ui PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
  )
else()
  message(STATUS "JUCE 6.1.6 not found at ${FXSOUND_JUCE_DIR}; skipping desktop UI")
endif()
