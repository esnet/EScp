#!/usr/bin/python

import os
import random

try: 
  os.mkdir("/tmp/test_src_1k")
  os.mkdir("/tmp/test_dst_1k")
except:
  pass


try: 
  for i in range(100):
    with open( "/tmp/test_src_1k/test_%04d" % i, "wb") as fi:
      sz = random.randint( 0, 192000 )
      fi.write( os.urandom(sz) )
finally: 
  pass

