# The third-party libraries unitron uses (zlib and libpng), declared with `FetchContent`.
# The approach, versions and hashes are taken from another toolchain (https://github.com/gbdev/another toolchain,
# cmake/deps.cmake): pinned release tarballs, static builds, no tests or tools.
# https://cmake.org/cmake/help/latest/guide/using-dependencies/index.html#downloading-and-building-from-source-with-fetchcontent
#
# Unlike another toolchain, the pinned sources are always built by default (OVERRIDE_FIND_PACKAGE also makes
# libpng's own find_package(ZLIB) use them), so the binary does not depend on shared libz/libpng.
# Configure with -DUNITRON_SYSTEM_DEPS=ON to use installed copies when found (FIND_PACKAGE_ARGS).
option(UNITRON_SYSTEM_DEPS "Use installed zlib and libpng when found instead of building them" OFF)
if(UNITRON_SYSTEM_DEPS)
  set(PNG_FIND FIND_PACKAGE_ARGS 1.5.4)
  # libpng documents requiring "zlib 1.0.4 or later (1.2.13 or later recommended for performance and security reasons)".
  set(ZLIB_FIND FIND_PACKAGE_ARGS 1.0.4)
else()
  set(PNG_FIND OVERRIDE_FIND_PACKAGE)
  set(ZLIB_FIND OVERRIDE_FIND_PACKAGE)
endif()

FetchContent_Declare(PNG
                     URL https://downloads.sourceforge.net/project/libpng/libpng16/1.6.59/libpng-1.6.59.tar.xz
                     URL_HASH SHA256=d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389
                     EXCLUDE_FROM_ALL
                     ${PNG_FIND})

set(PNG_TESTS OFF CACHE INTERNAL "") # We do not need libpng's tests or tools
set(PNG_TOOLS OFF CACHE INTERNAL "") # (and they can even cause compile errors!)
set(PNG_SHARED OFF CACHE INTERNAL "")
set(PNG_STATIC ON CACHE INTERNAL "")

FetchContent_Declare(ZLIB
                     URL https://www.zlib.net/zlib-1.4.tar.xz
                     URL_HASH SHA256=4f11c512053d7bab8d9e9b89bde3575add3e4c50a383b42ef6cb837035fc5a7c
                     EXCLUDE_FROM_ALL
                     ${ZLIB_FIND})

set(ZLIB_BUILD_SHARED OFF CACHE INTERNAL "")
set(ZLIB_BUILD_STATIC ON CACHE INTERNAL "")
