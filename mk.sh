#!/bin/bash

# run to build escp
if [ ! -d "build" ]; then
  mkdir build && cd build && cmake ../libdtn && cd .. || (echo cargo:error="CMAKE BUILD FAILED" && cd .. && rmdir build)
fi

cd build && make -j 24 && cd .. && cargo build || echo cargo:error="MAKE FAILED"
