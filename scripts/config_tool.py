#!/usr/bin/python3

# This parses systool output to provide a mask that binds to
# a particular socket using NIC as a reference. If your NIC
# is on socket 0 then you probably want to bind EScp to
# socket 1 (as an example).


import subprocess
import sys
import pprint

if len(sys.argv) < 2:
  print("No arguments.")
  print("config_tool <interface>")
  sys.exit(-1)

cpulist = ""
try: 
  s=subprocess.run(["systool", "-A", "local_cpulist", "-c", "net", sys.argv[1]], stdout=subprocess.PIPE)
except AttributeError as e:
  print("Incorrect python version. Python >= 3 is required.");
  sys.exit(0)
except Exception as e:
  print("Error running systool command. You could install sysfsutils package.")
  print("System error was: ", e)
  sys.exit(0)

for i in s.stdout.decode("utf-8").splitlines():
  if i.find("local_cpulist") > 0:
    cpulist = i

cpulist=cpulist.split("=")[1]
cpulist=cpulist.split("\"")[1]

mask = 0

cpurange = cpulist.split(",")
tmp = []
for i in cpurange:
    hi = lo = 0
    split = i.split('-')
    if len(split) == 2:
        lo = split[0]
        hi = split[1]
    else:
        lo = split[0]
        hi = lo
    tmp += [ int(lo), int(hi) ]
cpurange = tmp

while 1:
  if not cpurange:
    break
  lo = cpurange[0]
  hi = cpurange[1]
  for i in range(lo, hi+1):
    mask = mask | 1 << i
  cpurange = cpurange[2:]

output = "cpumask: %X\n" % mask

s=subprocess.run(["systool", "-A", "numa_node", "-c", "net", sys.argv[1]], stdout=subprocess.PIPE)

numanode = ""
for i in s.stdout.decode("utf-8").splitlines():
  if i.find("numa_node") > 0:
    numanode = i

numanode=numanode.split("=")[1]
numanode=numanode.split("\"")[1]
numanode=int(numanode)

if (numanode>=0):
  output += "nodemask: %X" % (1 << numanode)

print(output)
