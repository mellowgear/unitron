cmake_minimum_required(VERSION 3.24) # FetchContent FIND_PACKAGE_ARGS
project(unitron VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

## Dependencies: zlib and libpng, fetched and built statically

include(FetchContent)
include(cmake/deps.cmake)

# Be sure to make zlib available before libpng, otherwise libpng
# will attempt to `find_package(ZLIB)` without going through `FetchContent`.
if(NOT DEFINED ZLIB_BUILD_TESTING) # Unless overridden (e.g. in the cache), we don't care about zlib's tests.
  set(ZLIB_BUILD_TESTING OFF)
endif()
FetchContent_MakeAvailable(ZLIB)
if(NOT DEFINED ZLIB_INCLUDE_DIRS)
  set(ZLIB_INCLUDE_DIRS "${zlib_BINARY_DIR};${zlib_SOURCE_DIR}") # libpng's `genout` script relies on this variable to be set.
endif()
# Statically building zlib (from source) generates a target named differently.
if(NOT TARGET ZLIB::ZLIB AND TARGET zlibstatic)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

if(NOT DEFINED PNG_TESTS) # Unless overridden (e.g. in the cache), we don't care about libpng's tests.
  set(PNG_TESTS OFF)
endif()
FetchContent_MakeAvailable(PNG)
FetchContent_MakeAvailable(json)
if(NOT TARGET PNG::PNG)
  if(PNG_SHARED)
    add_library(PNG::PNG ALIAS png_shared)
  else()
    add_library(PNG::PNG ALIAS png_static)
  endif()
endif()

## unitron.

file(GLOB UNITRON_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM UNITRON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
set(EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp)
add_custom_command(OUTPUT ${EMBEDDED}
  COMMAND ${CMAKE_COMMAND} -DOUT=${EMBEDDED} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS data/hwnames.tsv cmake/embed.cmake
  COMMENT "Embedding data files")
add_library(unitron_core STATIC ${UNITRON_SOURCES} ${EMBEDDED})
target_include_directories(unitron_core PUBLIC src)
target_link_libraries(unitron_core PUBLIC PNG::PNG ZLIB::ZLIB nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(unitron_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(unitron_core PRIVATE -Wall -Wextra)
endif()

add_executable(unitron src/main.cpp)
target_link_libraries(unitron PRIVATE unitron_core)
if(MINGW)
  target_link_options(unitron PRIVATE -static)
endif()
install(TARGETS unitron RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/cpp/*.cpp)
  add_executable(unitron_tests ${TEST_SOURCES})
  target_link_libraries(unitron_tests PRIVATE unitron_core)
  set(V ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME opspace COMMAND unitron_tests opspace ${V}/vectors)
  add_test(NAME lines COMMAND unitron_tests lines ${V}/vectors)
  add_test(NAME lcf COMMAND unitron_tests lcf ${V})
  add_test(NAME hwinc COMMAND unitron_tests hwinc ${CMAKE_CURRENT_SOURCE_DIR}/data)
  add_test(NAME hwnames COMMAND unitron_tests hwnames ${CMAKE_CURRENT_SOURCE_DIR}/data)
  add_test(NAME datasheet COMMAND unitron_tests datasheet ${V}/datasheet)
  add_test(NAME rom COMMAND unitron_tests rom ${V}/vectors $<TARGET_FILE:unitron>)
  add_test(NAME z80opspace COMMAND unitron_tests z80opspace ${V}/vectors)
  add_test(NAME z80rom COMMAND unitron_tests z80rom ${V}/vectors $<TARGET_FILE:unitron>)
  add_test(NAME z80shift COMMAND unitron_tests z80shift .)
  add_test(NAME cart COMMAND unitron_tests cart ${V}/cart $<TARGET_FILE:unitron>)
  # the per-feature CLI cases, one ctest test per folder (tests/README.md)
  foreach(F asm mpp link lcf dis gfx text bin2db z80 toshiba cli lsp)
    if(IS_DIRECTORY ${V}/${F})
      add_test(NAME cases-${F} COMMAND unitron_tests cases ${V} $<TARGET_FILE:unitron> ${F})
    endif()
  endforeach()
endif()
