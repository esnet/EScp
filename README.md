EScp 0.7
========

ESCP 0.7 is the second major release of EScp; EScp 0.6 was based on Python
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
  * Compression (using zstd)
  * Checksums, Direct I/O, API's.

In general, the approach taken by EScp is to have algorithms optimized to give
good performance across all metrics (i.e. billions of small files, single large
file), that scale linearly with core count.

EScp is currently only available for x86 on Linux. If you want it to run on
a specific platform that is not yet supported, please reach out to the author
and/or create an issue on github that describes what you would like and how
you would like to use EScp.

RELEASE NOTES
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
ERROR. If you find that EScp is still not working, please file a bug
report.

If you found this software useful and/or have any questions/requests, please
reach out.

The conf file, `/etc/escp.conf`, is a YAML file. At present it has only
been tested with the following parameters (change the values for your system):

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
cargo install bindgen-cli --version 0.68.1
bindgen libdtn/include/dtn.h -o src/escp/bindings.rs --use-core  --generate-cstr

# flatc version 23.5.26
# You will need to grab the tagged version in GIT and compile it.

# You probably also want gdb/valgrind/whatever your favorite debug tools are

# You can enable autocomplete by adding below to .bashrc
complete -F _scp -o nospace escp
_completion_loader scp



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

If our storage optimizations are either un-needed or insufficient, lets move on
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
transfering your files.

If you did all of these things and feel that the network and block storage
are still not the bottle neck, you can reduce CPU usage doing things like:

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
to debug using system debuggers. Typically this involves compiling code
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
  /*  ---+--------------+----------\
   *  IV | Payload      | HMAC     |
   *   8 | <variable>   |  16      |
   * AAD | Encrypted    | Auth tag |
   *  ---+--------------+----------/
   */
```

There are two data types, metadata and file, which is inferred from the IV.
Each datatype specifies the length of the payload.

Internally EScp uses AES-GCM using the `ISA-L_crypto` library. The
implementation follows NIST 800-38D guidelines and has not been peer
reviewed. If you want more information, check `network_recv` or
`network_initrx` in `libdtn/src/dtn.c`.


DEV NOTES
=========

```
Compression:

  EScp supports compression, however:

  We use zstd simplified API. It would be better to use the streaming API
  (to conserve state between frames), but it requires testing that our input
  state matches our output state. At a very basic level, we don't do
  compression if our compressed data is larger than our un-compressed data.

  If we took our existing design and switched to streaming it, we would need
  to handle lossing state between the compressor and decompressor.

Checksums:

  Write checksums to file? We don't right now because there is no tool to do
  anything with those checksums, but... Maybe we should support the option?
  The checksums are not cryptographic in nature.

Sparse Files:

  Currently, the receiver checks to see if a block of data contains all zeroes,
  and if it does it then skips the block (assuming engine support). In theory,
  it would be better if the sender just didn't send the block if it contains
  all zeroes, but you would still need some way to tell the reveiver not to
  wait on the data. Obviously, that is possible, but not sure it is worth the
  complexity for that particular use case.

  Also, while EScp allows sparse file support without compression, doing so
  probably doesn't make much sense.

Error Messages:

  VRFY macro is used to terminate execution if the receiver runs into a
  condition in which it is impossible to continue. That error message should
  be gracefully sent to sender before terminating.

NUMA Pinning:

  It would be nice to do NUMA pinning automatically. The library I foound to
  probe our hardware resources had too many dependencies, but, if a light
  weight method to do that existed, that would be interesting.

SHM Engine:

  Earlier versions of EScp contained a SHM engine to allow applications to
  write directly to an EScp transfer stream. This engine was overly
  complicated because of the Python Layer, but, it should now be possible to
  do in a relatively light weight fashion.

Test Harness:

  Testing right now is against a series of data sets and manual.

```


HARD LIMITS
===========

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
755fd7c88a9983e45250e7d3ea2f5942295b5f3bdedb980cb06cd6deede212f2  EScp-0.7.1.tar.gz
c91d47a3b0c6578e7a727af26700dabd79e0acbf0db7eeffbf3151b48980b8a6  EScp-0.7.0.zip
```

Changes from 0.7.1 to 0.8.0 (TBD):
  * Breaks compatability with previous versions
  * Change to On-Wire format; Drops un-needed crypto wrapper. Change other
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


