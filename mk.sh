#!/bin/bash

# run to build escp

if [ ! -d "build" ]; then
  mkdir build && cd build && cmake ../libdtn && cd .. || echo cargo:error="CMAKE BUILD FAILED. PLEASE CHECK output"
fi

if [ -n "$1" ]; then
  cd build && make -j 24 && cd .. || echo cargo:error="CMAKE BUILD FAILED. PLEASE CHECK output"
else
  cd build && make -j 24 && cd .. && cargo build
fi
