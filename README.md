EScp 0.7
========

ESCP 0.7 is the second major relase of EScp; EScp 0.6 was based on Python
and C. EScp 0.7 is a re-write of the Python component in RUST, which
improves performance, stability, and eases platform distribution.

EScp is an application for high speed data transfer. This version has been
tested across the WAN at ~200 gbit/s at SC23, disk-to-disk, limited by NIC. It
is designed to be a modern transport protocol replicating the functionality of
SCP. Some features include:

  * Multiple TCP/IP streams per transfer ( 1/thread )
  * Lockless multi threaded application
  * ZeroCopy (When blocksize < L3), uses shared memory, and page aligned
  * AES-GCM 128 over the wire encryption, optimized for performance
  * Authentication to remote systems through SSH
  * Swappable I/O Engines ( Currently: DUMMY & POSIX )
  * Block based interface to transfering data
  * Checksums, Direct I/O, API's.

In general, the approach taken by EScp is to have algorithms optimized to give
good performance across all metrics (i.e. billions of small files, single large file), that scale linearly with core count.

Release Notes
=============

EScp is tested on Linux and it has successfully transferred PBs of data and
hundreds of millions of files. By default, encryption and file checksumming
are enabled. It is thought to be a reliable transfer protocol, however, the
fact that EScp is not listed as a 1.x release is meant to imply that danger
could lurk below the surface.

File transfers default to `O_DIRECT` and then switch to indirect mode if
that fails on a target/source filesystem, in some cases you may want to
specify indirect mode on the command line.

The stable release is the latest *tagged* release, for instance, 0.7.0. If you
are not participating in the development of EScp, you are strongly advised to
use a tagged release. Known issues (if any) will be tracked on GitHub at
https://github.com/ESnet/EScp. The 0.7 branch is incompatible with prior
releases.

The tagged releases have been tested and should work under most conditions,
however, error messages are not always handled gracefully. Check the logs
on *both* the client and receiver. If you enabled verbose logging search for
`[ERROR]`. If you find that EScp is still not working, please file a bug
report. Logs presently are directed to /tmp/escp.log.{client,server}.

If you found this software useful and/or have any questions/requests, please
reach out.

The cconf file, `/etc/escp.conf , is a YAML file. At present it has only
been tested with the following parameters (change the values for your system):

```
cpumask: FFFF
nodemask: 1
```

Usage
=====

```
Energy Sciences Network transfer tool (EScp)

Usage: escp [OPTIONS] <SOURCE>... <DESTINATION>

Arguments:
  <SOURCE>...    Source Files/Path
  <DESTINATION>  Destination host:<path/file> [default: ]

Options:
  -P, --port <SSH_PORT>        Port [default: 22]
      --escp_port <ESCP_PORT>  ESCP Port [default: 1232]
  -v, --verbose                Verbose/Debug output
  -q, --quiet                  
  -A, --agent                  Enable SSH Agent Forwarding
  -c, --cipher <CIPHER>        CIPHER used by SSH [default: ]
  -i, --identity <IDENTITY>    IDENTITY pubkey for SSH auth [default: ]
  -l, --limit <LIMIT>          LIMIT transfer to (bytes/sec) [default: 0]
  -p, --preserve               Preserve source attributes (TODO)
  -C, --compression            Compression (TODO)
  -r, --recursive              Copy recursively
  -o <SSH_OPTION>              SSH_OPTION to SSH [default: ]
  -S, --ssh <SSH>              SSH binary [default: ssh]
  -D, --escp <ESCP>            EScp binary [default: escp]
      --blocksize <BLOCK_SZ>   [default: 1M]
      --ioengine <IO_ENGINE>   posix,dummy [default: posix]
  -t, --parallel <THREADS>     # of EScp parallel threads [default: 4]
      --mgmt <MGMT>            mgmt UDS/IPC connection [default: ]
      --bits                   Display speed in bits/s
      --nodirect               Don't enable direct mode
      --nochecksum             Don't enable file checksum
  -L, --license                Display License
  -h, --help                   Print help
  -V, --version                Print version


Example:

# Transfer file1 and file2 to server host using SSH
escp file1 file2 host:/remoteDirectory


```

INSTALL
=======

The recommended approach to using EScp is by compiling it yourself and then to
use the resultant RPM/DEB file to install on your systems.

COMPILING
=========

```
# Install system dependencies (Debian)
apt install cmake libtool g++ libnuma-dev nasm autoconf automake \
   curl           # for get rust stanza \
   libclang-dev   # for bindgen

# Get rust
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

# Then install as
sudo dpkg -i target/debian/escp_0.7.0_amd64.deb

# For development
cargo install bindgen-cli --version 0.68.1
bindgen libdtn/include/dtn.h -o bindings.rs --use-core  --generate-cstr

# flatc version 23.5.26
# You will need to grab the tagged version in GIT and compile it.

# You probably also want gdb/valgrind/whatever your favorite debug tools are

```

KNOWNBUGS
=========

  * Error messages are inconsistent. If you find that the sender is giving you
    an error message you can't parse, check both the client and server log. 
      - /tmp/escp.log.sender
      - /tmp/escp.log.receiver
  * Dummy engine can only be used with a few files
  * You can't disable encryption
  * CLI parsing sometimes requires a file/host argument even when it shouldn't
  * Check https://github.com/ESnet/EScp/issues
  * Compression is not enabled in this release


TUNING
======

For the most part, performance should be good without any tuning, however,
if you do need to tune something, the most important parameters are
blocksize, cpumask, and nodemask (memory). 

blocksize if the unit that will be used for reading from disk and sending
across the network. It should be as big as possible without making it so
big that you no longer fit into L3 Cache. It should be aligned closely to
whatever your storage is using.

For instance, if your block size is greater then 2MB but less than or equal
to 4MB use:

```
  $ escp -b 4M <files> [host]:[path]
```


If running on a NUMA enabled processor, you should also pin the threads/memory
node using /etc/escp.conf. As an example:

```
  # escp/scripts/config_tool.py eno1 > /etc/escp.conf
```

This will pin EScp to the netork card eno1. The `config_tool` is misleading
because you really want EScp to run on the NUMA node that *isn't* where your
network card is. So for instance, if we are running a transfer utilizing eth0,
you would need to find a network card that is on a different NUMA domain from
eth0 and then pin EScp to that domain. Alternatively, hand edit the config
generated to specifically exclude the eth0 domain.

nodemask and cpumask are passed to `set_mempolicy` and `sched_setaffinity` as
binary masks. The input format is expected to be HEX.


DEBUGGING
=========

Your first step is to enable verbose logs;

```
  escp -v
```

This will create files /tmp/escp.log.client and /tmp/escp.log.server;

If this does not resolve your issue, consider reaching out or attempting
to debug using system debuggers. Typically this involves compiling code
with debug symbols (the default unless --release is specified), and then
running escp through a debugger. As an example:

```
  # Start server
  rust-gdb --args escp --mgmt /path/to/mgmt/socket --server dont care:

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


SECURITY
========

EScp works by establishing an SSH session to a remote host (using system SSH
binary) and then starting EScp on the receiver. After exchanging session
information through the SSH channel, the sender connects the DATA channel
using the port specified by receiver. All data (outside of SSH) is encrypted
with AES128-GCM.

The on-the-wire format consists of a series of tags with 16 bytes identifying
what the data is, followed by a payload, followed by a 16 byte HMAC, as shown
below:

```
  /*  ---------+--------+----+----+--------------+----------\
   *  hdr_type | magic  | sz | IV | Payload      | HMAC     |
   *    2      |   2    | 4  |  8 | sz - 32      |  16      |
   *          AAD                 | Encrypted    | Auth tag |
   *  ---------+------------------+--------------+----------/
   */
```

Internally EScp uses AES-GCM using the `ISA-L_crypto` library. The
implementation follows NIST 800-38D guidelines and has not been peer
reviewed.


AUTHOR
======

EScp is written by Charles Shiflett with the support of [ESnet](es.net). EScp
is a side project and is not in any way an official or supported project of
ESnet, The University of California, Lawrence Berkeley National Lab, nor the
US Department of Energy.

I'd like to thank my team at ESnet; Anne White, Goran Pejović, Dhivakaran
Muruganantham, George Robb, Luke Baker and Shawn Kwang, as well as Brian
Tierney, Eli Dart, Ezra Kissel, Ken Miller, Eric Pouyoul, Jason Zurawski,
Seyoung You, and Andrew Wiedlea for their support, encouragement, and
assistance in developing EScp.

Lastly, thanks to you, for using this application. EScp was written for you!
If you are interested in contributing, you are in the unique position of
being the first external collaborator to be added here.


LICENSE (BSD3)
==============

```
ESnet Secure Copy (EScp) Copyright (c) 2021-24, The Regents of the
University of California, through Lawrence Berkeley National Laboratory
(subject to receipt of any required approvals from the U.S. Dept. of
Energy). All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

(1) Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

(2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

(3) Neither the name of the University of California, Lawrence Berkeley
National Laboratory, U.S. Dept. of Energy nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

You are under no obligation whatsoever to provide any bug fixes, patches,
or upgrades to the features, functionality or performance of the source
code ("Enhancements") to anyone; however, if you choose to make your
Enhancements available either publicly, or directly to Lawrence Berkeley
National Laboratory, without imposing a separate written license agreement
for such Enhancements, then you hereby grant the following license: a
non-exclusive, royalty-free perpetual license to install, use, modify,
prepare derivative works, incorporate into other computer software,
distribute, and sublicense such enhancements or derivative works thereof,
in binary and source code form.
```


