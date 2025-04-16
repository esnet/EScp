How EScp is different from SCP
==============================

EScp is focused on performance. From the ground up, it is a zero-copy design
that is page aligned to support direct disk-IO.

At high speed, many things are bottle-necks, these include external things, 
i.e. the linux disk-cache (VFS) or SSH as well as internal things like,
concurrency, 

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

KNOWNBUGS
=========

  * Error messages are inconsistent. If you find that the sender is giving you
    an error message you can't parse, check both the client and server log.
      - /tmp/escp.log.sender
      - /tmp/escp.log.receiver
  * Dummy engine can only be used with a few files
  * You can't disable encryption
  * Check https://github.com/ESnet/EScp/issues

ROADMAP
=======

EScp 0.9 (Expected Nov 2025): 
  * Block based transfers
  * API
  * "-3" mode (Time permitting)
  * Better error messages
  * Misc. Updates / Fixes 

EScp 1.0 (Expected Nov 2026):
  * "-3" mode
  * Local Copy
  * Feature parity with SCP
  * Improve preserve mode (If needed)
  * Auth via TLS (Time permitting)
  * Example programs using API (Time permitting)

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
  * File completion criteria changed to blocks written instead of bytes written
  * Format of progress bars changed
  * Stability Improvements / Bug Fixes

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
  * Complete rewrite of python code to rust
  * Change to fully lockless design
    - Previous versions locked when iterating through files
  * CLI Syntax more closely mirrors scp, w/ some borrow from iPerf
  * Config File format changed to YAML
  * HashMap for indexing file descriptors/file numbers


