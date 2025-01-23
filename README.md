EScp 0.8
========

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
  * Block based interface to transferring data (API expected in 0.9)
  * Compression (using zstd)
  * Checksums, Direct I/O, API's.

In general, the approach taken by EScp is to have algorithms optimized to give
good performance across all metrics (i.e. billions of small files, single large
file), that scale linearly with core count.

EScp isn't intended to replace SCP; Instead it is meant to fill a void in
efficiently transferring science data. That means supporting different use
cases, such as block based data transfer instead of file based data, supporting
the types of files used in science, such as sparse files, and, importantly,
being able to transfer at any scale, securely, efficiently, and with commodity
hardware. EScp is also intended to fit into existing science frameworks, and
is therefore meant to be called through an API. If these features interest
you, please reach out.

EScp is currently only available for x86 on Linux. For the most part, this
isn't a case of it not being able to support other platforms, but more a
reflection of the type of servers that are typically used for science. If you
have a use case for something that isn't supported, please create a GitHub
issue. We also accept Pull requests if you happen to have a patch for us.

RELEASE NOTES
=============

EScp 0.8 adds feature parity with most SCP flags, improves session handling,
and fixes a number of edge cases around things like directory handling, zero
byte files, and so on. Overall the result should be further improvements to
stability, performance, and ease of use.

**WARNING:** This software is in development. If you are using a *tagged*
release, for instance, 0.8.0, it has passed a number of tests to verify that
the application works as the developer expects. If you are using the latest
git version expect that things will break.

In any event, while EScp has successfully transferred PBs of data and hundreds
of millions of files. It may fail for you! If you run into a case where EScp
is failing, but SCP is not, please create a bug report. At this point in
the EScp software life-cycle, it should be transferring files safely and
successfully, although there are still a few differences. For instance, EScp
won't transfer symlinks and it only preserves attributes on files.

If you found this software useful and/or have any questions/requests, please
reach out. The primary author, see AUTHOR, can be reached using the first
letter of the first name followed by the full lastname @ ESnet domain.
Bugs/Issues are appreciated and should be reported using the Github Issues
feature.

How EScp is different from SCP
==============================

After initiating a transfer with EScp, system SSH is invoked to connect to the
remote host, however, unlike SCP, it uses SSH only to spawn a receiver and
transfer session keys. Once complete, EScp connects to the EScp service on the
remote host, typically using TCP port 1232-42 (choosing the first open port).

Once connected it encrypts all communications using AES-GCM-128 and the session
keys from earlier. Because EScp implements its own encryption protocol, it is
able to optimize this to be fast, as well as configure the TCP ports for long
haul data transmission. This enables EScp to be orders of magnitude faster than
SCP.

All transfers are encrypted, and this behavior is not possible to disable.
By default, the sender computes a checksum when reading the file. The receiver
computes a checksum when writing the file and sends that checksum to the
sender, which the sender then verifies. While the checksum is not cryptographic
in nature, between network encryption and file check summing, the protocol is
thought to securely and reliably copy data.

As EScp is a performance oriented transfer tool, all files default to
`O_DIRECT` and then switch to indirect mode if that fails on a target/source
filesystem. In rare cases, `O_DIRECT` can actually make performance worse
and you may need to disable this feature.

EScp uses multiple I/O threads with one TCP stream per thread. You may
specify the number of I/O threads via the CLI. There are also threads that
iterate directories/files, this is so that EScp can take advantage of
parallelism available in your file system and on your CPU. This allows EScp
to scale transfer performance with your CPU, as a single thread/core is
limited in how much data it can process.

Logging on EScp is different from SCP. Please see DEBUGGING for details on
logging. Additionally, as this software is still in-development, not all
error messages bubble-up correctly and you may need to enable logging to
help to understand why something is not working.

EScp does support a conf-file for some options. The conf file,
`/etc/escp.conf`, is a YAML file. An example of some of the things you can
set is shown below:

```
cpumask: FFFF
nodemask: 1
```

USAGE
=====

```
Energy Sciences Network transfer tool (EScp)

Usage: escp [OPTIONS] <SOURCE>... <DESTINATION>

Arguments:
  <SOURCE>...    Source Files/Path
  <DESTINATION>  Destination host:<path/file> [default: ]

Options:
  -P, --port <SSH_PORT>        SSH Port
      --escp_port <ESCP_PORT>  ESCP Port [default: 1232]
  -v, --verbose                Verbose/Debug output
  -q, --quiet
  -A, --agent                  Enable SSH Agent Forwarding
  -c, --cipher <CIPHER>        CIPHER used by SSH
  -i, --identity <IDENTITY>    IDENTITY pubkey for SSH auth
  -l, --limit <LIMIT>          LIMIT/thread (bytes/sec) using SO_MAX_PACING_RATE
  -p, --preserve               Preserve source attributes (TODO)
  -C, --compress               Enable Compression
      --sparse                 Sparse file support, use with compression
  -r, --recursive              Copy recursively
  -o <SSH_OPTION>              SSH_OPTION to SSH
  -S, --ssh <SSH>              SSH binary [default: ssh]
  -D, --escp <ESCP>            EScp binary [default: escp]
      --blocksize <BLOCK_SZ>   [default: 1M]
      --ioengine <IO_ENGINE>   posix,dummy [default: posix]
  -t, --parallel <THREADS>     # of EScp parallel threads [default: 4]
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

# Install system dependecies (RHEL Family)
sudo dnf group install "Development Tools"
dnf install epel-release
dnf install nasm autoconf automake libtool cmake curl

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
rustup update
cargo update
./mk.sh
cargo test
cargo audit
cargo clippy


```

KNOWNBUGS
=========

  * Error messages are inconsistent. If you find that the sender is giving you
    an error message you can't parse, check both the client and server log.
      - /tmp/escp.log.sender
      - /tmp/escp.log.receiver
  * Dummy engine can only be used with a few files
  * You can't disable encryption
  * Check https://github.com/ESnet/EScp/issues

TUNING
======

For the most part, performance should be good without any tuning, however,
EScp does allow for tuning most parameters associated with a transfer to
optimize your data transfer workflow.

First, check to see where you bottleneck is. Generally speaking you want
to figure out, are you storage bound, network bound, or CPU bound. To test
the network, execute something like this:

```
  # Create dummy 1TB file
  dd if=/dev/zero of=1T bs=1 count=1 seek=1T

  # Transfer it using dummy engine
  escp 1T remote-host: --ioengine=dummy
```

The transfer should quickly settle at some number. If this is faster than
your transfer from disk, congratulations, you are disk bound. You probably
need to increase the block size until it is bigger than your RAID size (if
using RAID). You can query your block size with:

```
# mdadm --detail /dev/md127
/dev/md127:
           Version : 1.2
     Creation Time : Mon Nov 14 17:57:39 2022
        Raid Level : raid10
        Array Size : 7813406720 (7.28 TiB 8.00 TB)
     Used Dev Size : 1562681344 (1490.29 GiB 1600.19 GB)
      Raid Devices : 10
     Total Devices : 10
       Persistence : Superblock is persistent

     Intent Bitmap : Internal

       Update Time : Tue Jul  2 23:17:28 2024
             State : clean, degraded, recovering
    Active Devices : 8
   Working Devices : 10
    Failed Devices : 0
     Spare Devices : 2

            Layout : near=2
        Chunk Size : 512K
.
.
.

# ^^^ In this example, our block size (default 1M, would need to be >= 512K)

# xfs_info /storage
meta-data=/dev/md127             isize=512    agcount=32, agsize=61042304 blks
         =                       sectsz=512   attr=2, projid32bit=1
         =                       crc=1        finobt=1, sparse=1, rmapbt=0
         =                       reflink=1    bigtime=0 inobtcount=0
data     =                       bsize=4096   blocks=1953351680, imaxpct=5
         =                       sunit=128    swidth=640 blks
naming   =version 2              bsize=4096   ascii-ci=0, ftype=1
log      =internal log           bsize=4096   blocks=521728, version=2
         =                       sectsz=512   sunit=8 blks, lazy-count=1
realtime =none                   extsz=4096   blocks=0, rtextents=0

# But the filesystem is configured with 640*4096 == 2.5MB block size
# So we would pick the next largest power of 2 for our block size

```

blocksize if the unit that will be used for reading from disk and sending
across the network. It should be as big as possible without making it so
big that you no longer fit into L3 Cache. It should be aligned closely to
whatever your storage is using.

In the example above, our block size is > 2MB but <= 4MB, so we use:

```
  $ escp -b 4M <files> [host]:[path] # Aligned to 2^n
```

If our storage optimizations are either unneeded or insufficient, lets move on
to network or CPU.

In general, for network we just increase threads until we are happy with our
performance:

```
  $ escp -b 4M -t 8 <files> [host] # Keep our block size but use 8 threads
```

However, this sometimes does not scale as expected, particularly if you are
using a NUMA enabled processor. In these sorts of cases, lets pin EScp to
run on a NUMA node different than our network card.

The preferred approach is to us `config_tool`, included with EScp. Example:

```
  # escp/scripts/config_tool.py eno1 > /etc/escp.conf
```

will result in something like:

/etc/escp.conf
```
cpumask: "FFFF0000"
nodemask: "2"
# Use 2nd NUMA node: cores 16-31 w/ memory node 2
```

This will pin EScp to the netork card eno1. Caution: `config_tool` is misleading
because you really want EScp to run on the NUMA node that *isn't* where your
transfer NIC is located.  In this particular example, our transfer NIC is called
eth100 on a different NUMA node, so the config works, but, you may find that you
need to hand edit it, reversing the config. When editing by hand, be careful
with your hex numbers (FF != FF00).

nodemask and cpumask are passed to `set_mempolicy` and `sched_setaffinity`.
cpumask is HEX, nodemask INT, both are YAML Strings. You may want to enable
verbose logging to verify that the mask was applied and/or check `htop` when
transferring your files.

If you did all of these things and feel that the network and block storage
are still not the bottleneck, you can reduce CPU usage doing things like:

```
escp --nochecksum
```

If you are CPU bound, you should see a slight uptick in performance by
disabling checksums.

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


SECURITY
========

EScp works by establishing an SSH session to a remote host (using system SSH
binary) and then starting EScp on the receiver. After exchanging keys through
the SSH channel, the sender connects the DATA channel using the port specified
by receiver. All data (outside of SSH) is encrypted with AES128-GCM.

The on-the-wire format is shown below:

```
  /* /-----+--------------+----------\
   * |  IV | Payload      | HMAC     |
   * |   8 | <variable>   | 16       |
   * | AAD | Encrypted    | Auth tag |
   * \-----+--------------+----------/
   */
```

There are two data types, metadata and file, which is inferred from the IV.
Each datatype specifies the length of the payload.

Internally EScp uses AES-GCM using the `ISA-L_crypto` library. The
implementation follows NIST 800-38D guidelines and has not been peer-reviewed.
If you want more information, check `network_recv` or `network_initrx` in
`libdtn/src/dtn.c`.


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

RELEASES
========

```
SHA256                                                            NAME
6cb3da78ece447c2ab7b9dd992bd92d30c3892075b250f2a58cba5ffed4b5974  0.8.0.tar.gz
755fd7c88a9983e45250e7d3ea2f5942295b5f3bdedb980cb06cd6deede212f2  EScp-0.7.1.tar.gz
c91d47a3b0c6578e7a727af26700dabd79e0acbf0db7eeffbf3151b48980b8a6  EScp-0.7.0.zip
```

 * [0.8.0.tar.gz](https://github.com/esnet/EScp/archive/refs/tags/0.8.0.tar.gz)
 * [0.7.1.tar.gz](https://github.com/esnet/EScp/archive/refs/tags/0.7.1.tar.gz)

Changes from 0.8.0 to 0.9.0:
  * Change how files are marked finished; Transmit and use blocks read

Changes from 0.7.1 to 0.8.0 (07 Nov 2024):
  * Breaks compatability with previous versions
  * Change to On-Wire format; Drops unneeded crypto wrapper. Change other
    message headers.
  * Add Compression (Using zstd)
    - use --compress flag or -C
    - automatic compress large metadata chunks (file information/verification)
  * Add Sparse File support (--sparse)
  * Add receiver timeout + keepalive (Minimize Zombie Receivers)
  * Session Init Changes
  * Add Preserve support (just for files)
  * Only log to file/syslog if specified on command line
  * Improve counters on receivers:
    - `bytes_network`    shows bytes transmitted over the wire
    - `bytes_disk`       shows bytes written to disk
    - `bytes_compressed` total of "data" bytes sent over the wire
    - Summary written to receiver log (if logging to a file/syslog)
  * Change how sender/receiver negotiate session termination
    - Removes session close delays and better file verification
  * Fix race condition and/or delay on file iteration
  * Fix directory traversal and close zero bytes file descriptors
  * Check that files don't leave prefix
  * Update how progress bar is displayed
  * Fix how empty files are handled
  * Long delays unschedule thread (as opposed to busy waiting)
  * Many fixes, much polishing, and more cleaning
  * Happy Birthday!

Changes from 0.7.0 to 0.7.1 (20 June 2024):
  * Checksum feature enabled
  * Change how transfer progress is displayed when calculating files
  * Code cleanup w/ clippy & cargo audit

Changes from 0.6.0 to 0.7.0 (5 June 2024):
  * Complete rewrite of python code
  * Change to fully lockless design
    - Previous versions locked when iterating through files
  * CLI Syntax more closely mirrors scp, w/ some borrow from iPerf
  * Config File format changed to YAML
  * HashMap for indexing file descriptors/file numbers

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


