cmake_minimum_required(VERSION 3.20)
project(ra LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/bin")

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(CLANGTIDY)
    message(STATUS "clang-tidy: enabled")
    set(CMAKE_CXX_CLANG_TIDY ${CLANGTIDY} -extra-arg=-Wno-unknown-warning-option)
  else()
    message(SEND_ERROR "clang-tidy requested but executable not found")
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

set(RA_VERSION "1.3")

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/config.h.in
  config.h
  @ONLY
)

option(ENABLE_ADDRESS_SANITIZER "Enable Address Sanitizer" OFF)
if(ENABLE_ADDRESS_SANITIZER)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

add_compile_options(-fno-omit-frame-pointer)
add_compile_options(-Weverything)
add_compile_options(-Wno-c++98-compat)
add_compile_options(-Wno-c++98-compat-pedantic)
add_compile_options(-Wno-padded)
add_compile_options(-Wno-poison-system-directories)
add_compile_options(-Wno-function-effects)
add_compile_options(-Werror=unguarded-availability-new)

add_executable(ra
    main.cpp
    args.cpp
    capture.cpp
    device.cpp
    extend_caf.cpp
    monitor.cpp
    output_file.cpp
    signal_handler.cpp
    session.cpp
    writer.cpp
    display.cpp
    file_util.cpp
    args.h
    capture.h
    config.h.in
    extend_caf.h
    monitor.h
    device.h
    display.h
    file_util.h
    output_file.h
    recording_context.h
    writer.h
    signal_handler.h
    sleep_guard.h
    session.h
    ring_buffer.h
    util.h
)

target_include_directories(ra PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

include(GitVersion)
add_git_version_tracking(ra)

target_link_libraries(ra PRIVATE
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework IOKit"
    "-framework CoreFoundation"
)
