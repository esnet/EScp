#!/usr/bin/python

import os
import random

try: 
  os.mkdir("/tmp/test_src_1k")
  os.mkdir("/tmp/test_dst_1k")
except:
  pass


try: 
  for i in range(1024*128):
    with open( "/tmp/test_src_1k/test_%06d" % i, "wb") as fi:
      sz = random.randint( 1, 16384 )
      fi.write( os.urandom(sz) )
finally: 
  pass

