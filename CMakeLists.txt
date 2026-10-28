cmake_minimum_required(VERSION 3.16)
project(libmiplay VERSION 0.1.0 LANGUAGES C)

option(LIBMIPLAY_BUILD_SHARED "Build shared libmiplay" ON)
option(LIBMIPLAY_BUILD_STATIC "Build static libmiplay" ON)
option(LIBMIPLAY_BUILD_TOOLS "Build libmiplay test tools" ON)
option(LIBMIPLAY_BUILD_TESTS "Build libmiplay tests" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(LIBMIPLAY_SOURCES
  src/libmiplay.c
  src/miplay_protocol.c
)

if (LIBMIPLAY_BUILD_SHARED)
  add_library(miplay SHARED ${LIBMIPLAY_SOURCES})
  target_include_directories(miplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(miplay PRIVATE pthread)
  set_target_properties(miplay PROPERTIES OUTPUT_NAME miplay)
endif()

if (LIBMIPLAY_BUILD_STATIC)
  add_library(miplay_static STATIC ${LIBMIPLAY_SOURCES})
  target_include_directories(miplay_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(miplay_static PRIVATE pthread)
  set_target_properties(miplay_static PROPERTIES OUTPUT_NAME miplay)
endif()

if (LIBMIPLAY_BUILD_TOOLS)
  add_executable(miplay-receiver-test tools/miplay_receiver_test.c)
  if (TARGET miplay)
    target_link_libraries(miplay-receiver-test PRIVATE miplay)
  else()
    target_link_libraries(miplay-receiver-test PRIVATE miplay_static)
  endif()
endif()

if (LIBMIPLAY_BUILD_TESTS)
  enable_testing()
  add_executable(test_protocol tests/test_protocol.c)
  if (TARGET miplay)
    target_link_libraries(test_protocol PRIVATE miplay)
  else()
    target_link_libraries(test_protocol PRIVATE miplay_static)
  endif()
  add_test(NAME protocol COMMAND test_protocol)
endif()
