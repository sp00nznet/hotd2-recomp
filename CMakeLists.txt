# =============================================================================
# The House of the Dead 2 - static recompilation host (32-bit, MSVC x86)
#
#   py -3 run_lift.py --all  # -> src/recomp/gen/  (needs work/functions.json)
#   build.cmd                # vcvarsall x86 + CMake + Ninja -> build/hotd2recomp.exe
#   build\hotd2recomp.exe --run --headless --watchdog 60
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hotd2 C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "the host must be 32-bit: use build.cmd (vcvarsall x86)")
endif()
if(NOT MSVC)
  message(FATAL_ERROR "runtime/native32 is MSVC x86 inline assembly")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# The C runtime linked in: a test VM has no 32-bit VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(PCRECOMP "${CMAKE_SOURCE_DIR}/../pcrecomp" CACHE PATH "pcrecomp checkout")
include(${PCRECOMP}/cmake/pcrecomp_gen.cmake)   # needs pcrecomp with cmake/ (main)

add_executable(hotd2
  src/runtime/host.c
  ${PCRECOMP}/runtime/native32/native32.c
  ${PCRECOMP}/runtime/recomp32/image_loader.c
  ${PCRECOMP}/runtime/recomp32/recomp_trace.c
)
pcrecomp_gen_sources(hotd2)   # /Od; -DPCRECOMP_GEN_OPT=ON for the build type's /O2
target_include_directories(hotd2 PRIVATE
  src/runtime src/recomp/gen ${PCRECOMP}/runtime/native32 ${PCRECOMP}/runtime/recomp32)
target_compile_definitions(hotd2 PRIVATE RECOMP_RETADDR=0u _CRT_SECURE_NO_WARNINGS
                                            RECOMP_FLAT_MEMORY RECOMP_LOCAL_REGS)
# Linked high and not relocatable, so the guest's 0x00400000..0x009D5000 is free.
target_link_options(hotd2 PRIVATE /BASE:0x60000000 /FIXED:NO /DYNAMICBASE:NO
                                       /NXCOMPAT /LARGEADDRESSAWARE /STACK:0x1000000
                                       /MANIFESTUAC:NO /MANIFEST:NO)
set_source_files_properties(src/runtime/host.c PROPERTIES COMPILE_OPTIONS "/W3")
target_link_libraries(hotd2 PRIVATE user32 gdi32 kernel32 ole32 uuid dxguid winmm ws2_32 vfw32)

option(HOD2_TRACE "record a ring buffer of lifted function entries" OFF)
if(HOD2_TRACE)
  target_compile_definitions(hotd2 PRIVATE RECOMP_TRACE=1)
endif()

# Not "hotd2.exe": Windows' compatibility database matches that name and the
# process then gets no 16-bit display modes, which the game needs.
set_target_properties(hotd2 PROPERTIES OUTPUT_NAME hotd2recomp)
