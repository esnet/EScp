#/usr/bin/sh
#
# This gets called from the cargo build series of steps to make sure libdtn is built

if [ ! -d "build" ]; then
  mkdir build
fi

cd build
cmake ../libdtn && make -j 24 && echo BUILD OK || echo cargo:warning="CMAKE BUILD FAILED. PLEASE CHECK output"
