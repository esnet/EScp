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

set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -O3 -pthread -DO_CRC")
#set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -pthread -rdynamic")
#set(CMAKE_C_FLAGS_RELEASE "-g -march=native -O3 -pthread")
#set(CMAKE_C_FLAGS_RELEASE "-g -march=sandybridge -pthread")

#set(CMAKE_C_FLAGS_RELEASE "-g")
set(CMAKE_C_FLAGS_DEBUG "-g -march=sandybridge -pthread")
add_definitions("-Wall -DO_VERSION=\\\"${ver}\\\"")



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
ExternalProject_Add(nasm_project
  URL https://github.com/netwide-assembler/nasm/archive/refs/tags/nasm-2.16.01.zip
  URL_HASH SHA256=aa9340cde57d097062781d236bcccd96089d4ab636d67b843bdfd183511a1854
  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/nasm
  BUILD_COMMAND     make -j 24 && make manpages
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  libnuma.a
  BUILD_IN_SOURCE   1
)
add_custom_target(nasm DEPENDS nasm_project)

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
  CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/isal PATH=${CMAKE_CURRENT_BINARY_DIR}/nasm/bin:$ENV{PATH}
  DEPENDS           nasm
  BUILD_COMMAND     ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_isa-l.sh ${CMAKE_CURRENT_BINARY_DIR}
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  libisal_crypto.a
  BUILD_IN_SOURCE   1
)

add_library(isal STATIC IMPORTED)
set_property(TARGET isal PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/isal/lib/libisal_crypto.a)
add_dependencies(isal isa-l_crypto_project)

ExternalProject_Add(liburing_project
  URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.3.zip
  URL_HASH SHA256=439651d535475e20b56d7b0f1e72708f30ee46f4b818d0993359d9372a39df27
  CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/uring
  BUILD_COMMAND     make -j 24
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  liburing.a
  BUILD_IN_SOURCE   1
)

add_library(liburing STATIC IMPORTED)
set_property(TARGET liburing PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/uring/lib/liburing.a)
add_dependencies(liburing liburing_project)


find_package(Threads REQUIRED)

# Build DTN

include_directories(include ${CMAKE_CURRENT_BINARY_DIR}/isal/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/libnuma/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/uring/include)
set(SOURCES src/file_io.c src/args.c
            src/engine_shmem.c src/engine_posix.c src/engine_uring.c
            src/engine_dummy.c)

add_executable(dtn ${SOURCES} src/dtn.c)
add_executable(example-shmem examples/client-shmem.c)
add_executable(dtn-utility src/shmem-utility.c
               src/file_io.c src/args.c)
# add_executable(test_unit ${SOURCES} src/test_unit.c)

#target_compile_definitions(dtn PUBLIC EBUG_VERBOSE)
target_compile_definitions(dtn PUBLIC __ENGINE_POSIX__ __ENGINE_URING__ __ENGINE_DUMMY__ __ENGINE_URING__)


target_link_libraries(example-shmem Threads::Threads "rt")
target_link_libraries(dtn-utility libnuma isal Threads::Threads "rt")
target_link_libraries(dtn libnuma liburing isal Threads::Threads "rt")
# target_link_libraries(test_unit libnuma liburing isal Threads::Threads "rt")

install(TARGETS dtn DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/scripts/escp.py
        DESTINATION bin
        RENAME escp
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE
        )

string(TIMESTAMP DATETIME "%Y%m%d-%H%M%S")
configure_file(conf/escp.conf.in ${CMAKE_CURRENT_BINARY_DIR}/escp.conf)

# And tests

enable_testing ()
find_program ( python NAMES python3 python )

# add_test ( test_unit test_unit COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_unit )
add_test ( NAME local-transfer-posix COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-local.sh ${CMAKE_CURRENT_BINARY_DIR}/dtn posix )
add_test ( NAME local-transfer-uring COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-local.sh ${CMAKE_CURRENT_BINARY_DIR}/dtn uring )
