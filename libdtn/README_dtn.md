
When this software was originally written, a 'dtn' tool existed which
was used to perform I/O and data transfers. This tool sort of morphed
into escp, although some support may still exist for the dtn tool. To
use it, make a link from escp to 'dtn',  and then run 'dtn'. You will
get a interface similar to the original dtn tool.

While the tool more-or-less doesn't exist, EScp uses the guts of this
'dtn' tool, with the library becoming the useful component instead of
the command line interface. This writing was written for the original
tool; I removed the sections that described specific details and this
is what was left.

** NONE OF THE COMMAND LINE EXAMPLES BELOW ARE VALID, DO NOT USE THEM **

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









