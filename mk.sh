#!/bin/bash

# run to build escp


if [ ! -d "build" ]; then
  mkdir build && cd build && cmake ../libdtn & cd ..
fi
cd build && make -j 24 && cd .. && cargo build
