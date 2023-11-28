** This tool still exists, but this documentation needs to be
   updated. This is purely a placeholder and most of this may not apply
   to the current version of DTN.


DTN is a Linux tool to transfer files as quickly as possible using
traditional API's. It is NUMA aware and tries to set reasonable
defaults for transferring a file quickly.

In most cases,  the default configuration should transfer files at line
rate, however, if they don't you should tune the application per TUNING
below. DTN can also be used to benchmark system storage and network.

DTN is a standalone tool for benchmarking performance. If you are interested
in transferring a file, the escp script provides this in an easier to use
package.

OPTIONS:
=======

```
   -b block       I/O block sz for network transfer and disk
   -c host/port   Connect to host, can specify multiple times
   -h, --help     Display help
   -f file        read/write from file, specify multiple times
   -s             Run as server
   -t thread      Number of worker threads
   -D             Queue Depth (for supported engines, i.e. URING)
   -X [sz]        Only do file I/O; Specify sz to write file; omit to read
   --cpumask msk  Run on msk CPU's, hex
   --engine ngen  I/O engine: Posix, uring, dummy
   --managed      Enable managed mode
   --memnode msk  Use memory msk matching node, hex
   --mtu     sz   Request sz Maximum TCP Segment
   --nodirect     Disable direct mode
   --quiet        Disable messages
   --verbose      Display verbose/debug messages
   --version      Display version
   --window  sz   Request sz TCP Window
```


BUILDING:
========

DTN uses cmake for dependency management and building. Typically one would
build it doing something like:

```
  mkdir dtn/build
  cd dtn/build
  cmake ..
  make -j 24
```


For debug builds, do something like:

```
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  make clean
  make VERBOSE=1 -j 24
```

You can also generate RPMs/DEBs/.tar.gz as below; If you intend to use that
package to distribute the DTN binary, check the compile options and ensure
you are building a portable build.

`  make package`

Lastly, you can run tests using the ctest framework

`  ctest -V`

DTN NOTES:
==========

The approach taken by DTN, is to have each thread/flow reasonably independent
of each other,  which allows for efficient parallelism and takes advantage of
core pinning. File transfers are aligned to 4k page sizes, set to direct mode
(by default), and operate on large block sizes.  Work itself is divided up on
a first come/first serve basis,   such that faster threads will get more work
than slower threads to avoid long tails.

While the DTN application tries to minimize memory copies (and is typically a
zerocopy transfer solution), it is still best to select block sizes which fit
into cache. The exception is when you need to match RAID array and/or storage
requirements in which case the block size should be set to a multiple of that
size. Minimizing block size is especially important when using URING, where a
small block size and reasonable QD value provide optimum performance.

An example block size value using URING could be as low as 64k (QD=4),  where
as when using a large RAID array optimum performance could be using POSIX I/O
with a block size of 16M.

For network I/O,  TCP Windows should be sized such that window is larger than
the RTT time between server and client. Typically the DTN default sz of 512MB
should be sufficient,  however, if experiencing issues the window size should
be tweaked to check for benefit.  Also useful is to change the TCP congestion
control settings (BBR vs Traditional), to check the adapter ram/packet values
and to verify that socket pinning is correct.

TUNING:
======

Socket Pinning:
--------------

On a NUMA system, start by making sure that DTN is configured to run
on the socket closest to the network card. DTN includes a script which
will determine the parameters for you on a correctly configured system:

`  python3 scripts/config_tool.py <network interface>`

The output from the config_tool.py script are flags which should be passed
to DTN.

Storage:
-------

Storage is typically limited by how fast you can write to disk, thus we
start by testing write throughput:

```
  # Test write I/O; create 16G.file (of size 16G), 4 threads, 8M blocksize
  dtn -X 16G -b 8m -f 16G.file -t 4
```

The number of threads and block size should be adjusted such that performance
is maximized. The I/O test defaults to writing zeroes, you can also write
pseudo-random data by prepending a second X flag:

`  dtn -X -X 16G -b 8m -f 16G.file -t 4`

Lastly, read I/O (-X flag without size parameter) should be tested:

```
  # Test read I/O; Read 16G.file
  dtn -X -b 8m -f 16G.file -t 4
```


Network:
-------

It is best to start by verifying network performance without doing disk
I/O. First start the receiver:

```
  # Network test without I/O (recv)
  dtn -s --engine=dummy
```


Then start the sender:

```
  # Network test without I/O (send)
  dtn -c host -t 4 -f 16G.file --engine=dummy
```


If performance is not sufficient, you should adjust the MTU (--mtu) and window
size (--window). The kernel itself may also need to be tuned, see:

  https://fasterdata.es.net

Running manually:
----------------

Receiver:

```
  cshiflett@goran-dev-1:/mnt/t$ ~/dtn/dtn -s -b 8M
  Listening on 0.0.0.0:2222
   GiB= 1016.50, gbit/s=   79.45
  Summary Report:
  Network= 1099515822082, Written=1099511627776, Elapsed= 113.67s, gbit/s= 77.38
  # of reads = 131072, bytes/read = 8388608
```


Sender:

```
  cshiflett@goran-dev-0:/mnt/test$ ~/dtn/dtn -t 8 -f 1T -c 192.168.1.3 -c 192.168.12.9 -b 8M
  Connect to 192.168.12.9:2222, threads=8

  Summary Report:
  Bytes= 1099511627776, duration= 113.37s, gbit/s= 77.59
  # of reads = 131072, bytes/read = 8388608
```

Using ESCP:
----------

For ease of use, a script called ESCP.py is included, which mimics the command
line syntax of SCP. As this product is still in development, some flags may
work, and others will not.

Like SCP, it uses SSH to initiate a connection, however, unlike SCP, once a
connection has been initiated the transfer commences over the standard DTN
ports. The non-management based traffic (i.e. eveything that doesn't go over
SSH), is encrypted with AES-128-GCM.

For best performance, the dtn binary should be configured as described above,
and then those arguments should be stored in an escp.conf file in either
/etc/escp.conf, /usr/local/etc/escp.conf, <escp.py directory>/escp.conf, or
CWD/escp.conf. Here is an example config:

At minimum, the dtn binary must either exist on the remote host in a path
that is searchable, or, an escp.conf file should be created which specifies
the binary location on that remote machine.

```
[escp]
dtn_path = /usr/local/bin/dtn
dtn_args = -t 4 -b 2M --cpumask F00F

[remote_hostname]
dtn_path = /home/cshiflett/dtn/build/dtn
dtn_args = --engine=dummy --nodirect
```


Example usage:

```
$ ../scripts/escp.py 512M localhost:/tmp
512M                                                       100% 512.0MB 1.0GB/s
$
```


To Do
=====

1) `engine_shm` fixes.
  - Expose interface on receiver
  - Document
  - Add escp functionality, like pipe operator

2) IO Engine
  * io_uring should register files/buffers for faster access
      (waiting on Ubuntu support)
  -- Removing pipe feature as that is supported through SHM extension









