** IN PROGRESS **

Shared Memory API allows an application to transfer block based data to a
remote node. 

Opening Files:
==============

For each file opened for IO a corresponding shared memory region is created.

The shared memory regions are named `/<Session_ID>-<File Descriptor>`, which
is formatted as `/%016X-%08X`. These shared memory regions consist of N
blocks of `BLOCK_SZ` data, where N defaults to 8 (changed in `io_engine`
definition) and `BLOCK_SZ` defaults to 1M and is defined by the `--blocksize`
command line argument.

Following the data region are memory segments which describe the state
of the file. This is used by the client application to determine when
blocks are ready and is used to mark data as complete.

In general, it is difficult to map the shared memory identifier to the filename
provided by your application. The preferred approach to determining how
to open these shared memory regions is to start EScp with the --api option. As
a sender, you can then query the API using the filename to retrieve the shared
memory identifier. When acting as a receiver, you can query the API and
retrieve a list of available files to claim and then write.

I/O From Files:
===============

File I/O largely follows the same semantics one would expect from using
traditional I/O except that the I/O pattern is purely block based. Once you
have attached to a shmem segment, the preferred approach to data I/O is to use
the C-API provided `shmem_block_claim` and `shmem_block_complete` functions.
See examples.

Blocks don't need to be contiguous, nor should you expect that blocks are
contiguous if acting as the receiver, however, zero byte blocks should be
avoided except to denote EOF as this API follows the libc convention of
denoting EOF through a zero byte read.

If you intend to write sparse files, the preferred approach would be to
use the API, then open the same file multiple times with different offsets.

The user of this API is neither responsible for allocating memory nor
are they responsible for cleaning up. Instead, the claim function will return
a pointer to a memory region, and sometime after calling 'complete'. Upon
I/O completion, it will cause a page fault if attempts are made to access
those memory regions.

Using APIs:
===========











