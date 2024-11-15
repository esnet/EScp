#!/bin/bash

# run to build escp
if [ ! -d "target/dtn" ]; then
  mkdir -p target/dtn && cd target/dtn && cmake ../../libdtn && cd ../.. || (echo cargo:error="CMAKE BUILD FAILED" && cd ../.. && rmdir target/dtn )
fi

if [ -z $1 ]; then
  cd target/dtn && make -j 24 && cd ../.. && cargo build || echo cargo:error="MAKE FAILED"
else 
  cd target/dtn && make -j 24 && cd ../.. || echo cargo:error="MAKE FAILED"
fi
