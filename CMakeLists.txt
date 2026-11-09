cmake_minimum_required(VERSION 3.20)
project(HearMeOut LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# statický CRT – DLL beží v audiodg.exe a nesmie závisieť od VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(hmo_core STATIC core/src/dsp.cpp core/src/voicefx.cpp core/src/config.cpp)
target_include_directories(hmo_core PUBLIC core/include)
if(MSVC)
  target_compile_options(hmo_core PRIVATE /W4 /fp:fast /O2)
else()
  target_compile_options(hmo_core PRIVATE -Wall -Wextra -O2)
endif()

add_executable(hmo-wav tools/hmo-wav.cpp)
target_link_libraries(hmo-wav PRIVATE hmo_core)

add_executable(hmo-tests tests/test_dsp.cpp)
target_link_libraries(hmo-tests PRIVATE hmo_core)
enable_testing()
add_test(NAME dsp COMMAND hmo-tests)

if(WIN32)
  add_library(HearMeOutApo SHARED apo/src/HearMeOutApo.cpp apo/src/HearMeOutApo.def)
  target_link_libraries(HearMeOutApo PRIVATE hmo_core ole32 advapi32)
  set_target_properties(HearMeOutApo PROPERTIES PREFIX "")
  if(MSVC)
    target_compile_options(HearMeOutApo PRIVATE /W4)
  else()
    target_link_options(HearMeOutApo PRIVATE -static)
  endif()
endif()

# ---- HearMeOutHost.exe (VST3 host pod účtom používateľa) ----
if(WIN32)
  set(VST3_PI ${CMAKE_SOURCE_DIR}/third_party/vst3)
  add_executable(HearMeOutHost WIN32
    host/src/main_win.cpp host/src/vst3host.cpp host/src/vst3iids.cpp host/src/soundboard.cpp host/src/ma_impl.cpp
    ${VST3_PI}/pluginterfaces/base/funknown.cpp ${VST3_PI}/pluginterfaces/base/coreiids.cpp
    host/res/host.rc)
  target_include_directories(HearMeOutHost PRIVATE ${VST3_PI} host/src core/include third_party/miniaudio)
  target_compile_definitions(HearMeOutHost PRIVATE UNICODE _UNICODE)
  target_link_libraries(HearMeOutHost PRIVATE hmo_core dwmapi avrt ole32 uuid shell32 psapi)
  if(NOT MSVC)
    target_link_options(HearMeOutHost PRIVATE -municode -static)
  endif()
endif()
