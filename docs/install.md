INSTALL
=======

The recommended approach to using EScp is by compiling it yourself and then to
use the resultant RPM/DEB file to install on your systems.

COMPILING
=========

```
# Install system dependencies (Debian)
apt install cmake libtool g++ nasm autoconf automake \
   rustup         # for rust
   libclang-dev   # for bindgen

# Install system dependecies (RHEL Family)
sudo dnf group install "Development Tools"
dnf install epel-release
dnf install nasm autoconf automake libtool cmake curl

# Get rust (if not already acquired above)
curl https://sh.rustup.rs -sSf | sh
. "$HOME/.cargo/env"

# Build escp
./mk.sh           # If mk.sh fails because you had missing dependencies,
                  # remove the build directory and then re-run it. If that
                  # still fails, cat the file and attempt to run the
                  # build process for libdtn manually until you have found
                  # the error.


# You now need to install escp, the suggested path is to create an RPM/DEB
cargo install cargo-deb
cargo deb

# or

cargo install cargo-rpm
cargo rpm init
cargo rpm build

# Then install as (Update the version as appropriate):
sudo dpkg -i target/debian/escp_0.7.0_amd64.deb # Debian
# or
dnf install target/release/rpmbuild/RPMS/x86_64/escp-0.8.0*.rpm # Redhat Family

# For development
cargo install bindgen-cli --version 0.71.1
bindgen libdtn/include/dtn.h -o src/escp/bindings.rs --use-core  --generate-cstr --rust-target 1.78.0

# flatc version 23.5.26
# You will need to grab the tagged version in GIT and compile it.

# You probably also want gdb/valgrind/whatever your favorite debug tools are

# You can enable autocomplete by adding below to .bashrc
complete -F _scp -o nospace escp
_completion_loader scp

# To prepare for a release
cargo install cargo-audit
rustup update
cargo update
./mk.sh
cargo test
cargo audit
cargo clippy


```

DEBUGGING
=========

Your first step is to enable verbose logs;

```
  escp -v --logfile /path/to/logs
```

If this does not resolve your issue, consider reaching out or attempting
to debug using system debuggers. If debugging you may need to compile
with debug symbols (the default unless --release is specified), and then
running escp through a debugger. As an example:

```
  # Start server
  rust-gdb --args escp --mgmt /path/to/mgmt/socket --server dont care:
  # break rust_panic

  # Start client
  rust-gdb --args escp --mgmt /path/to/mgmt/socket file localhost:
```

In the example above, rather than connecting the management interface
automatically (using SSH), the management interface is through a socket
on the server.

If you are unable to replicate the issue locally, you can start escp
through a debugger but specify your remote host (as per normal). Once
the session is established you can connect your debugger on the remote
end using the --pid option.

You can also do something exotic (insecure) like create a management
socket over TCP using socat:

```
  socat TCP4-LISTEN:1234 UNIX-CONNECT:/tmp/foo.sock
  socat UNIX-RECVFROM:/tmp/foo.sock TCP4:yikes.com:1234
```

Things to watch out for with debug logs:
  1) It will slow down EScp, in some cases noticably. This is espcially
     apparent with RUST based events which block until written.
  2) libDTN events are logged to a circular buffer. This buffer can overflow
     and if an overflow occurs the oldest log entries will be overwritten.
  3) libDTN logs are not timestamped, although they are sequential. The
     timestamp comes from the RUST backend when the message is pulled off
     the queue. This means that a libDTN message can show up before
     an EScp message when in reality the EScp message occured before.


DEV NOTES
=========

```
Compression:

  EScp supports compression through the zstd simplified API. It would be better
  to use the streaming API to conserve state between frames, and thereby reduce
  over the wire data.

Checksums:

  It would be nice to have an option to write checksums for a transfer to a
  file and/or support alternative checksums.

Sparse Files:

  Sparse files are a receiver feature, which is backwards. It makes more
  sense to just not send the block from the get-go.  This is ameliorated
  by compression, however, still bad. We could also plug-into OS support
  for sparse files and avoid reading the data in the first place.

Error Messages (Expected in 0.9):

  VRFY macro is used to terminate execution if the receiver runs into a
  condition in which it is impossible to continue. That error message should
  be gracefully sent to sender before terminating.

NUMA Pinning:

  It would be nice to do NUMA pinning automatically. I attempted this in
  an earlier revision, but the library I was using was too heavy-weight and
  it seemed not worth the benefit. It would be nice to revisit this.

SHM Engine/Block Based API (Expected in 0.9):

  Earlier versions of EScp contained a SHM engine to allow applications to
  write directly to an EScp transfer stream. This engine was overly
  complicated because of the Python Layer, but, it should now be possible to
  do in a relatively light weight fashion.

Test Harness (Expected in 0.9):

  Testing right now is against a series of data sets and is manual. I created
  a stub test-harness, and this needs to be expanded.

Very Small Files in control stream (Expected in 0.9)

"-3" Mode (Try for 0.9, otherwise 1.0)

Profiler/Tracer (Expected in 0.9):

  Optional functionality that can be used to debug performance issues
  (both internal to the software and external, like disk/network).

Local Copy (1.0 Feature):

  Support High Performance local copy

Send/Receive (1.0 Feature):

  Currently EScp only supports transfers from client to server, should
  support both directions.

```


DESIGN LIMITS
=============

 * Files are limited to 2^64 bytes
 * A max of 2^56 files can be transferred per session.
 * AES-GCM uses a 2^64 counter, which limits the number of blocks sent
   in a transfer session to 2^64.
 * Limited to 28 transfer threads


