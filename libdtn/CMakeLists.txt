cmake_minimum_required(VERSION 3.5)
include(ExternalProject)
project(dtn)

if(NOT CMAKE_BUILD_TYPE)
  # Default to Release builds
  set(CMAKE_BUILD_TYPE Release)
endif()

find_program(GIT NAMES git)

execute_process(
  COMMAND ${GIT} describe --abbrev=8 --dirty
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE ver
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

#set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -O3 -pthread")
#set(CMAKE_C_FLAGS_DEBUG "-g -march=sandybridge -O3 -pthread")

set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -pthread -O -fPIC")
set(CMAKE_C_FLAGS_DEBUG "-g -march=native -pthread -O -fPIC")

add_definitions("-Wall -Wno-unused-result -Wno-stringop-truncation")

#add_compile_options(-fsanitize=address)
#add_link_options(-fsanitize=address)

# Package defines

message(STATUS "version=${ver} src=${CMAKE_CURRENT_SOURCE_DIR}")

set(CPACK_PACKAGE_VERSION ${ver})
set(CPACK_GENERATOR "RPM;DEB;TGZ")
set(CPACK_PACKAGE_NAME "dtn")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_CONTACT "Charles Shiflett")
set(CPACK_PACKAGE_VENDOR "ESnet")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
set(CPACK_PACKAGING_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)


ExternalProject_Add(zstd_project
  URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
  URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
  CONFIGURE_COMMAND echo
  BUILD_COMMAND     cd lib && make ZSTD_LEGACY_SUPPORT=0 ZSTD_LIB_DEPRECATED=0  ZSTD_LIB_DICTBUILDER=0  -j 24 lib-nomt
  INSTALL_COMMAND   make -j 24 PREFIX=${CMAKE_CURRENT_BINARY_DIR}/zstd ZSTD_LEGACY_SUPPORT=0 ZSTD_LIB_DEPRECATED=0  ZSTD_LIB_DICTBUILDER=0  lib-nomt install
  BUILD_BYPRODUCTS  libzstd.a
  BUILD_IN_SOURCE   1
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

add_library(zstd STATIC IMPORTED)
set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/zstd/lib/libzstd.a)
add_dependencies(zstd zstd_project)

ExternalProject_Add(isa-l_crypto_project
  URL https://github.com/intel/isa-l_crypto/archive/refs/tags/v2.25.0.zip
  URL_HASH SHA256=46f36985ec3e0e4a3b37f926b0fe8d3ee9d2a65f09c0af5c6d5a90903104d81f
  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/isal
  BUILD_COMMAND     make -j 24
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  libisal_crypto.a
  BUILD_IN_SOURCE   1
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

add_library(isal STATIC IMPORTED)
set_property(TARGET isal PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/isal/lib/libisal_crypto.a)
add_dependencies(isal isa-l_crypto_project)

set(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/link_libraries.txt")
file(WRITE "${OUTPUT_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/libdtn.a\n")
file(APPEND "${OUTPUT_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/isal/lib/libisal_crypto.a\n")
file(APPEND "${OUTPUT_FILE}" "${CMAKE_CURRENT_BINARY_DIR}/zstd/lib/libzstd.a\n")

# Build DTN

include_directories(include ${CMAKE_CURRENT_BINARY_DIR}/isal/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/zstd/include)
set(SOURCES src/file_io.c src/args.c
            src/engine_shmem.c
            src/engine_posix.c
            src/engine_dummy.c)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
find_library(LIBURING_LIBRARY uring)
if (LIBURING_LIBRARY)
  message(STATUS "Found liburing at: ${LIBURING_LIBRARY}")
  file(APPEND "${OUTPUT_FILE}" "${LIBURING_LIBRARY}\n")
  list(APPEND SOURCES src/engine_uring.c)
else()
  message(WARNING "liburing not found. uring support disabled.")
endif()


add_library(dtn STATIC ${SOURCES} src/dtn.c)
add_dependencies(dtn isa-l_crypto_project zstd_project)

target_compile_definitions(dtn PUBLIC __ENGINE_POSIX__ __ENGINE_DUMMY__ __ENGINE_SHMEM__ __ENGINE_URING__)
