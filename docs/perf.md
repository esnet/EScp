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


