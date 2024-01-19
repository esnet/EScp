cmake_minimum_required(VERSION 3.1)
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

set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -pthread")
set(CMAKE_C_FLAGS_DEBUG "-g -march=sandybridge -pthread")

add_definitions("-Wall -Wno-unused-result -Wno-stringop-truncation -DO_VERSION=\\\"${ver}\\\"")

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


# Externals
#ExternalProject_Add(nasm_project
#  URL https://github.com/netwide-assembler/nasm/archive/refs/tags/nasm-2.16.01.zip
#  URL_HASH SHA256=aa9340cde57d097062781d236bcccd96089d4ab636d67b843bdfd183511a1854
#  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/nasm
#  BUILD_COMMAND     make -j 24 && make manpages
#  INSTALL_COMMAND   make install
#  BUILD_BYPRODUCTS  libnuma.a
#  BUILD_IN_SOURCE   1
#)
#add_custom_target(nasm DEPENDS nasm_project)

ExternalProject_Add(zstd_project
  URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.5.zip
  URL_HASH SHA256=c5c8daa1d40dabc51790c62a5b86af2b36dfc4e1a738ff10dc4a46ea4e68ee51
  CONFIGURE_COMMAND echo
  BUILD_COMMAND     cd lib && make ZSTD_LEGACY_SUPPORT=0 ZSTD_LIB_DEPRECATED=0  ZSTD_LIB_DICTBUILDER=0  -j 24 lib-nomt
  INSTALL_COMMAND   make -j 24 PREFIX=${CMAKE_CURRENT_BINARY_DIR}/zstd ZSTD_LEGACY_SUPPORT=0 ZSTD_LIB_DEPRECATED=0  ZSTD_LIB_DICTBUILDER=0  lib-nomt install
  BUILD_BYPRODUCTS  libzstd.a
  BUILD_IN_SOURCE   1
)

add_library(zstd STATIC IMPORTED)
set_property(TARGET zstd PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/zstd/lib/libzstd.a)
add_dependencies(zstd zstd_project)

ExternalProject_Add(libnuma_project
  URL https://github.com/numactl/numactl/archive/refs/tags/v2.0.16.zip
  URL_HASH SHA256=49a7cb6c7e0d95754d03a0b70ef7473e8598b6d76aee81b541d9afe742fe24c6
#  GIT_REPOSITORY    https://github.com/numactl/numactl.git
  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/libnuma
  BUILD_COMMAND     make -j 24
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  libnuma.a
  BUILD_IN_SOURCE   1
)

add_library(libnuma STATIC IMPORTED)
set_property(TARGET libnuma PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/libnuma/lib/libnuma.a)
add_dependencies(libnuma libnuma_project)

ExternalProject_Add(isa-l_crypto_project
  URL https://github.com/intel/isa-l_crypto/archive/refs/tags/v2.24.0.zip
  URL_HASH SHA256=653c13e46b3923d0f04021604d6c70e445335c9b54fd62bf5303860de7904d89
  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/isal
  BUILD_COMMAND     make -j 24
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  libisal_crypto.a
  BUILD_IN_SOURCE   1
)

add_library(isal STATIC IMPORTED)
set_property(TARGET isal PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/isal/lib/libisal_crypto.a)
add_dependencies(isal isa-l_crypto_project)

# 
# ExternalProject_Add(liburing_project
#   URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.3.zip
#   URL_HASH SHA256=439651d535475e20b56d7b0f1e72708f30ee46f4b818d0993359d9372a39df27
#   CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/uring
#   BUILD_COMMAND     make -j 24
#   INSTALL_COMMAND   make install
#   BUILD_BYPRODUCTS  liburing.a
#   BUILD_IN_SOURCE   1
# )

add_library(liburing STATIC IMPORTED)
set_property(TARGET liburing PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/uring/lib/liburing.a)
add_dependencies(liburing liburing_project)


find_package(Threads REQUIRED)

# Build DTN

include_directories(include ${CMAKE_CURRENT_BINARY_DIR}/isal/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/libnuma/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/uring/include)
set(SOURCES src/file_io.c src/args.c
            src/engine_posix.c
            src/engine_dummy.c)
add_library( dtn STATIC ${SOURCES} src/dtn.c)

target_compile_definitions(dtn PUBLIC __ENGINE_POSIX__ __ENGINE_DUMMY__ )
target_link_libraries(dtn libnuma liburing isal Threads::Threads "rt")

# And tests (Needs updating)
enable_testing ()

