EScp 0.9
========

EScp is an application for high speed data transfer. This version has been
tested across the WAN at ~200 gbit/s at SC23, disk-to-disk, limited by NIC.
EScp's command line interface is derived from [SCP](https://openssh.com) and
then extended to provide additional functionality. Some of the features of
EScp include:

  * Multiple TCP/IP streams per transfer ( 1/thread )
  * Lockless multi threaded application
  * ZeroCopy (When blocksize < L3), uses shared memory, and page aligned
  * AES-GCM 128 over the wire encryption, optimized for performance
  * Authentication to remote systems through SSH
  * Swappable I/O Engines ( Currently: DUMMY & POSIX )
  * Block based interface to transferring data
  * Compression (using zstd)
  * Checksums, Direct I/O, API's
  * Memory/Block Based Transfers (as well as File based transfers)

In general, the approach taken by EScp is to have algorithms optimized to give
good performance while providing full-set functionality. This means baseline
security (AES-GCM 128 Encryption, SSH Authorization) and File Reliability
(Checksumming, File Verification, and Transfer Reliability) that scale with
core-count. Additional functionality is available through API's, CLI options,
and so on.

EScp is currently only available for x86-64 on Linux. If you would like to see
*Platform* supported, please open a ticket at https://github.com/ESnet/EScp.

RELEASE NOTES
=============

**EScp is in active development**. The latest tagged releases likely offers the
best stability. Currently that is 0.8.1.

At this point, most scp flags are supported although not necessarily 1:1. EScp
adds additional flags to support high performance transfer flows.

Installation
============

```
# Ubuntu
apt install cmake libtool g++ nasm autoconf automake rustup
git checkout 0.8.1
cargo install cargo-deb
cargo deb
sudo dpkg -i target/debian/escp_0.8.1-1_amd64.deb

# Uninstall
# dpkg -r escp
```

More Information
================

 * [Install/Developer Build Environment](docs/install.md)
 * [Performance/Tuning](docs/perf.md)
 * [Roadmap/Notes/FAQs](docs/notes.md)


USAGE
=====

```
Energy Sciences Network transfer tool (EScp)

Usage: escp [OPTIONS] [SOURCE]...

Arguments:
  [SOURCE]...  [ Destination ]

Options:
      --filelist <FILE_LIST>             FILE_LIST instead of SOURCE (YAML or '\n' list) [default: ]
  -P, --port <SSH_PORT>                  SSH Port
      --escp_port <ESCP_PORT>            ESCP Port [default: 1232]
      --escp_portrange <ESCP_PORTRANGE>  ESCP Port range (if unset, use 1232-1242) [default: 10]
  -v, --verbose                          Verbose/Debug output (use with logfile)
      --logfile <LOG_FILE>               Log to FILE (or syslog LOCALN where N between 0-7)
  -q, --quiet
  -l, --limit <LIMIT>                    LIMIT/thread (bytes/sec) using SO_MAX_PACING_RATE
  -p, --preserve                         Preserve source attributes
  -C, --compress                         Compression
      --sparse                           Sparse file support, use with compression
  -r, --recursive                        Copy recursively
  -o <SSH_OPTION>                        SSH_OPTION to SSH
  -S, --ssh <SSH>                        SSH binary [default: ssh]
  -D, --escp <ESCP>                      EScp binary [default: escp]
      --blocksize <BLOCK_SZ>             [default: 1M]
      --ioengine <IO_ENGINE>             posix,dummy [default: posix]
  -t, --parallel <THREADS>               # of IO worker threads [default: 4]
      --bits                             Display speed in bits/s
      --nodirect                         Disable direct (O_DIRECT) mode
      --nochecksum                       disable checksum
  -A, --agent                            Enable SSH Agent Forwarding
  -c, --cipher <CIPHER>                  CIPHER used by SSH
  -i, --identity <IDENTITY>              IDENTITY pubkey for SSH auth
  -F, --ssh_config <SSH_CONFIG>          SSH_CONFIG passed to SSH [default: ]
  -J, --jump_host <JUMP_HOST>            JUMP_HOST used by SSH [default: ]
  -L, --license                          Display License
  -h, --help                             Print help
  -V, --version                          Print version

Example:

# Transfer file1 and file2 to server host using SSH
escp file1 file2 host:/remoteDirectory


```

Notes:

**filelist** option expects either a file containing newline seperated
strings or YAML. The YAML option allows specifying source/dest pairs,
as an example:

```
  - [ "SRC_A", "DST_A" ]
  - [ "SRC_B", "DST_B" ]
```

** compress ** is using zstd at compression level 3. If a block of data
fails to compress, compression is skipped for that block.

** preserve ** only applies to files at present. This should be fixed in
a later release.




AUTHOR
======

EScp is written by Charles Shiflett with the support of [ESnet](es.net). EScp
is a side project and is not in any way an official or supported project of
ESnet, The University of California, Lawrence Berkeley National Lab, or the
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
