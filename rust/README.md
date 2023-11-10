apt install clang hwloc-dev liburing-dev
cargo build

bindgen ../include/dtn.h -o bindings.rs --use-core  --generate-cstr


