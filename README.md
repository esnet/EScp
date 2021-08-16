EScp 0.6
========

This is an early **BETA** release of EScp, an application for high speed
data transfer. It is created to be a simple high performance transfer
tool based on papers and presentations by Brian Tierney and Eli Dart
at ESnet
  [1](https://www.es.net/assets/Uploads/100G-Tuning-TechEx2016.tierney.pdf)
  [2](https://www.es.net/assets/pubs_presos/eScience-networks.pdf)
combined with my research on high speed file transport
  [3](https://data-over-distance.ornl.gov/documents/s1t3-shiflett.pdf)
  [4](https://www.nctatechnicalpapers.com/Paper/2016/2016-accelerating-big-data-applications-with-multi-gigabit-per-second-file-transfers/download)
  [5](https://www.slideshare.net/jstleger/dpdk-summit-2015-aspera-charles-shiflett)
while working at Aspera (later acquired by IBM).

The general set of features that may make this application different from
traditional transfer tools:

  * Multiple TCP/IP streams per transfer ( 1/thread )
  * Lockless multi threaded application per file transferred
  * Traditional pthreads based locking to handle assigning workers to files
  * 0-Copy, shared memory, and page aligned memory layout (when blocksize < L3).
  * AES-GCM 128 over the wire encryption, optimized for performance
  * Authentication to remote systems through SSH
  * Hot spot optimized ( higher level functions use Python, lower level C/ASM )
  * Swappable I/O Engines ( Currently: URING, SHM, DUMMY, & POSIX )
  * Block based interface to transfering data

EScp is principally tested on Linux and it has successfully transferred PBs
of data and millions of files. In this **BETA**, a distributed CRC checksum
has been hard coded into the transfer work flow such that silent corruption
of data should be unlikely, although caution is advised.


Usage
=====

```
usage: escp.py [-h] [-p PORT] [-q] [-r] [-v] [--args_dst ARG] [--args_src ARG] [--path_dst PATH] [--path_src PATH] [--version] [FILE [FILE ...]]

EScp: Secure Network Transfer

positional arguments:
  FILE                  [SRC] ... [DST], where DST is HOST:PATH

optional arguments:
  -h, --help            show this help message and exit
  -P PORT, --port PORT  Port for SSH[:DTN]
  -q, --quiet
  -v, --verbose
  --args_dst ARG        Arguments to DST DTN Executable
  --args_src ARG        Arguments to SRC DTN Executable
  --path_dst PATH       Path to DST DTN Executable
  --path_src PATH       Path to SRC DTN Executable
  --version

Example:

# Transfe file1 and file2 to server host using SSH port 22 and DTN port 2000
escp -P 22:2000 file1 file2 host:/remoteDirectory

Note:

When EScp is transferring a file, the `escp` script will run along with the
`dtn` sender and receiver. The `dtn` sender and receiver use a separate port
from the standard SSH connection, and that defaults to port 2222.

Recursive mode is implied by trying to transfer a directory.

```

COMPILING
=========

Compiling EScp requires modern versions of CMake, Yasm, & Python 3.

```
  # apt install cmake yasm

  git clone git@github.com:esnet/EScp.git
  mkdir EScp/Build
  cd EScp/build
  cmake ..
  make -j 24

  # Make will pull in libnuma, isa-l_crypto, and liburing
  # to which the application will be statically linked.

  # optionally
  ctest        # Run test framework
  make install # Install EScp and dtn binary
```

TUNING
======

As EScp is oriented towards high speed file transfer, the application tries
to expose any knobs that might be needed to optimize a transfer. These knobs
include things like number of threads, block size, enable direct I/O, NUMA
pinning, TCP window size, MTU, and so on. See README_dtn.md for more
information on the knobs and how to optimize your application.

Thus far I have been able to show transfers across the ESnet network
(continental Unites States) at 100 Gbit/s. Currently transfers are bound
by the network interface speed (which in turn is bound by the performance
of the network).

Once you have tuned the `dtn` tool to a point where you are happy, the
next step is to create an escp.conf configuration. Here is an example:

```
  [escp]

  dtn_args = -t 4 -b 1M --nodirect
  dtn_path = /home/cshiflett/dtn/b/dtn
```

Here is a command line example which performs a network transfer, but reads in
dummy data instead of reading data from disk. This is useful for benchmarking
the performance of your network while not being constrained by the speed of
your storage subsystem.

```
  escp --args_src="--engine=dummy" --args_dst="--engine=dummy" src_file dest:
```

DEBUGGING
=========

  Most bugs can be found by enabling verbose logging and following the logic
  flow. In some cases you may need to increase/reduce the verbosity of the
  logging, which in some cases involves enabling/disabling C macros in args.h.

  Logging uses hardcoded files created in /tmp. The logs are:

    `/tmp/escp.log`: Log output from EScp script
    `/tmp/dtn.rx.log`: DTN Receive logs, located on receiving system
    `/tmp/dtn.tx.log`: DTN Transmit logs

  You can also do things like enable memory checking (see valgrind example),
  profilers, and so on. Here is an example:

```
  dtn_args = --log-file="/tmp/valgrind.tx.log" /home/cshiflett/dtn/b/dtn -t 4 -b 1M --engine=posix --nodirect
  dtn_path = /usr/bin/valgrind

  [receive_host]
  dtn_path = /usr/bin/valgrind
  dtn_args = --log-file="/tmp/valgrind.rx.log" /home/cshiflett/dtn/b/dtn --nodirect

```


  Lastly, you can debug the application directly using traditional tools
  like gdb. If you go this route you probably want to edit the CMakeFile
  first to enable debugging (or check what the flags currently are), and
  then go from there.


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



