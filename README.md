EScp 0.6
========

This is a early *BETA* release of EScp.  It is designed to transfer files
quickly and efficiently using standard protocols. EScp itself is a python
script to manage the DTN C based program.   DTN is a tool for performance
testing and data transfer, which provides a lock-free, 0-copy,  encrypted
solution to transfering block based data (+ files) over the network.  DTN
and EScp are shipped together. EScp refers both to the package containing
DTN + EScp as well as the EScp script.

EScp is *BETA* software, expect bugs, missing features,  and all sorts of
problems. It is tested against Ubuntu 20.04 on x64 on filesets containing
~500,000 files and TBs of data without issue,  as such it is thought that
it generally respects the integrity of your files.  With this release the
principal goal has been on not subtly introducing file inconsistencies so
CRC checksumming is force enabled. Command line syntax, APIs,  and on the 
wire format are all expected to change, with no backwards compatibility.


HOW TO USE
==========

usage: escp.py [-h] [-p PORT] [-q] [-r] [-v] [--args_dst ARG] [--args_src ARG] [--path_dst PATH] [--path_src PATH] [--version] [FILE [FILE ...]]

EScp: Secure Network Transfer

positional arguments:
  FILE                  [SRC] ... [DST], where DST is HOST:PATH

optional arguments:
  -h, --help            show this help message and exit
  -p PORT, --port PORT  Port for DTN application
  -q, --quiet
  -r, --recursive
  -v, --verbose
  --args_dst ARG        Arguments to DST DTN Executable
  --args_src ARG        Arguments to SRC DTN Executable
  --path_dst PATH       Path to DST DTN Executable
  --path_src PATH       Path to SRC DTN Executable
  --version

Example: 

$ escp file1 file2 host:/remoteDirectory

COMPILING
=========

Compiling EScp requires CMake 

```
  git clone git@github.com:esnet/EScp.git
  mkdir EScp/Build
  cd EScp/build
  cmake ..
  make -j 24
  
  # optionally 
  make install # Will install EScp and dtn binary
```

TUNING
======

The most important parameters are the number of threads,  the block size, and
enabling or disabling direct mode. If your system is NUMA aware, you may also
want to enable CPU pinning. See 'dtn' command line help for supported params.

To pass tuning options to dtn when using escp, use --args-src, and --args-dst
options with a string containing the parameters to pass to DTN. Once you have
a configuration that works for you, create a file, `/etc/escp.conf`, and pass
those parameters as default arguments.

As an example:

```
  [escp]

  dtn_args = -t 4 -b 64k --nodirect
  dtn_path = /home/cshiflett/dtn/b/dtn
```

Here is a command line example which performs a network transfer, but reads in
dummy data instead of reading data from disk.

```
  escp --args_src="--engine=dummy" --args_dst="--engine=dummy" src_file dest: 
```

DEBUGGING
=========

  Most bugs can be found by enabling verbose logging and the logic flow until
  an error is displayed. You can also do things like enable valgrind, as in
  the example below.

```
  dtn_args = --log-file="/tmp/valgrind.tx.log" /home/cshiflett/dtn/b/dtn -t 4 -b 1M --engine=posix --nodirect
  dtn_path = /usr/bin/valgrind

  [receive_host]
  dtn_path = /usr/bin/valgrind
  dtn_args = --log-file="/tmp/valgrind.rx.log" /home/cshiflett/dtn/b/dtn --nodirect

```

  To enable logging of the EScp script, you must manually edit the script and
  uncomment the line which enables logging.

SECURITY
========

EScp opens a text-channel to communicate with both the sender and receiver. As
the receiver (typically) resides on a remote host, EScp attempts to SSH into
the remote host and will then run the DTN application.

Once the DTN application is running on both hosts, EScp will set configuration
parameters like, the encryption key (randomly generated), the files to
transfer, and so on. If successful, The sender will attempt to connect to the
receiver on the specified port (default=2222) using TCP.

If successful, the transfer will then commence with the entire session
encrypted using AES128-GCM. The on-the-wire format consists of a series of
tags with 16 bytes identifying what the data is, followed by a payload,
followed by a 16 byte HMAC. 

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



