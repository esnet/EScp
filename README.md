EScp 0.7
========

*This is the development branch of EScp 0.7. Check tags to see if there are any release candidates.*

ESCP 0.7 is the second major relase of EScp; EScp 0.6 was based on Python
and C. EScp 0.7 is a re-write of the Python component in RUST, which
improves performance, stability, and eases platform distribution.

EScp is an application for high speed data transfer. It is designed to scale
past 100 gbit/s while retaining features important to a modern transport
framework. Some features include:

  * Multiple TCP/IP streams per transfer ( 1/thread )
  * Lockless multi threaded application
  * ZeroCopy (When blocksize < L3), shared memory, and page aligned
  * AES-GCM 128 over the wire encryption, optimized for performance
  * Authentication to remote systems through SSH
  * Swappable I/O Engines ( Currently: DUMMY & POSIX )
  * Block based interface to transfering data
  * Checksums, Direct I/O, API's.

EScp is tested on Linux and it has successfully transferred PBs of data
and millions of files. That being said, this software is under active
development and YMMV. Please reach out to the EScp with any success or
failure stories.


Usage
=====

```
Usage: escp [OPTIONS] <SOURCE>... <DESTINATION>

Arguments:
  <SOURCE>...    Source Files/Path
  <DESTINATION>  Destination host:<path/file> [default: ]

Options:
  -P, --port <PORT>          Port [default: 1232]
  -v, --verbose              Verbose
  -q, --quiet                Quiet
  -c, --cipher <CIPHER>      Pass <CIPHER> Cipher to SSH [default: ]
  -i, --identity <IDENTITY>  Use <IDENTITY> Key for SSH authentication [default: ]
  -l, --limit <LIMIT>        Limit transfer to <LIMIT> Kbit/s [default: 0]
  -o, --option <OPTION>      Pass <OPTION> SSH option to SSH [default: ]
  -p, --preserve             Preserve source attributes at destination
  -r, --recursive            Copy recursively
  -L, --license              Display License
  -h, --help                 Print help
  -V, --version              Print version

Example:

# Transfe file1 and file2 to server host using SSH
escp file1 file2 host:/remoteDirectory


```

COMPILING
=========

```
  # First build C "DTN" library:

  # apt install cmake libtool g++ libnuma-dev nasm

  git clone git@github.com:esnet/EScp.git
  mkdir EScp/Build
  cd EScp/build
  cmake ..
  make -j 24

  # Make will pull in libnuma and isa-l_crypto dependencies
  # to which the application will be statically linked.

  # FIXME (currently broken): make package # Create DEB/RPM/TGZ packages

  # Now compile escp/dtn programs

  cd ../rust

  # This needs a relatively new vesion of cargo; If you get compile
  # errors from system cargo/rust, then grab a new version using:

  curl https://sh.rustup.rs -sSf | sh
  # Follow prompts then add something like below to .bashrc
  . "$HOME/.cargo/env"

  cargo build
  # or
  # cargo build --release

  # dtn and escp executables at target/{release,debug}/{escp,dtn}

  # Typically escp wants to live in a system path:
  cargo install --path . --root /usr/local --force

  # DEVELOPMENT TOOLS:

  cargo install bindgen-cli # bindgen at version 0.68.1
  # and flatbuffers-compiler
  apt  install flatbuffers-compiler # flatc at version 23.5.26

  # + GDB, valgrind,


```

KNOWNBUGS
=========

  * Error messages are inconsistent at best. For more information, run with -v
    and check logs on sender (/tmp/escp.log.sender) and
    receiver (/tmp/escp.log.server on remote host).
  * Dummy engine is really only meant to be used with a few files
  * Check https://github.com/ESnet/EScp/issues


TODO
====

  * Add `engine_uring` back
  * Add API
  * reimplement `engine_shmem`


TUNING
======

Most important is to verify I/O is appropriate for storage. For instance, if
your block size is greater then 2MB but less than or equal to 4MB use:

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

Most issues can be debugged solely by enabling verbose logs;

```
  escp -v
```

This will create files /tmp/escp.log.client and /tmp/escp.log.server;

For more complicated things, GDB is your friend.

```
  # Start server
  gdb --args escp --mgmt /path/to/mgmt/socket --server dont care:

  # Start client
  gdb --args escp --mgmt /path/to/mgmt/socket file localhost:
```

If you need to run the management sockets over the internet, you can
use socat; something like:

```
  socat TCP4-LISTEN:1234 UNIX-CONNECT:/tmp/foo.sock
  socat UNIX-RECVFROM:/tmp/foo.sock TCP4:yikes.com:1234
```


SECURITY
========

EScp works by establishing an SSH session to a remote host (using system SSH
binary), and then starting the DTN receiver. Once the receiver has initialized,
the sender attempts to connect to the receiver using the DTN port, through an
AES-128 GCM encrypted session using a shared randomly generated secret key
(shared through the SSH session).

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

For implementation see network_ family of functions in src/dtn.c.


AUTHOR
======

EScp is written by Charles Shiflett with the support of [ESnet](es.net). EScp
is a side project and is not in any way an official or supported project of
ESnet.

I'd like to thank my team at ESnet; Anne White, Goran Pejović, Dhivakaran
Muruganantham, George Robb, Shawn Kwang, and Deb Heller, as well as Brian
Tierney, Eli Dart, Ezra Kissel, Ken Miller, Eric Pouyoul, Jason Zurawski,
and Andrew Wiedlea, (also at ESnet) for their support, encouragement, and
assistance in developing EScp.

Lastly, thanks to you, for using this application. EScp was written for you!
If you are interested in contributing, you are in the unique position of
being the first external collaborator to be added here.


LICENSE
=======

ESnet Secure Copy (EScp) Copyright (c) 2021, The Regents of the
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



