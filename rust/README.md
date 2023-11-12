curl https://sh.rustup.rs -sSf | sh
. "$HOME/.cargo/env"

cargo build

# Become root (and install rust for root, same as above)
cargo install --path . --root /usr/local --force



# For development
cargo install bindgen-cli
bindgen ../include/dtn.h -o bindings.rs --use-core  --generate-cstr
