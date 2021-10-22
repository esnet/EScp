#!/usr/bin/env python3
import time
import sys
import subprocess
import pprint
import random
import secrets
import base64
import queue
import threading

import escp


def test_read(): 
  # Build args
  args = [ "./dtn", "-X", "-f", "1T", "--managed", "--engine", "shmem",
           "--logfile", "/tmp/test-shm.log", "--verbose", "-t", "4" ]

  dtn = subprocess.Popen( args, 
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE )

  # Push options 

  escp.push_option( dtn, [], "REDY" )
  escp.push_option( dtn, ["TEST",], "OKAY" )

  res = dtn.stdout.readline().decode("utf-8")
  res = res.strip("\n")
  if (res != 'SHM'):
    print("Bad output from DTN application", res)
  res = dtn.stdout.readline().decode("utf-8")
  res = res.strip("\n")

  SHMpath = res

  # Initiate client 

  args   = ["./dtn-utility", SHMpath]
  pprint.pprint(args)

  client = subprocess.Popen( args, stdout = subprocess.PIPE )

  er = dtn.stdout.readline().decode("utf-8")
  er = er.strip("\n")
  print(er)

  escp.push_option( dtn, ["QUIT",], "EXIT" )

def test_connection( engine ):
  # Build args
  port = random.randint( 10000, 60000 )
  tx_args = [ "./dtn", "--managed", "--engine", engine, "-t", "4", "-b", "2M", 
              "--logfile", "/tmp/shm-tx.log", "--verbose", 
              "-c", "127.0.0.1/%d" % port ]

  rx_args = [ "./dtn", "-s", "--managed", "--engine", "dummy", "-t", "4",
              "--logfile", "/tmp/shm-rx.log", "--verbose", "-t", "4",
              "-c", "127.0.0.1/%s" % port ]

  sender = subprocess.Popen( tx_args, 
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE )

  receiver = subprocess.Popen( rx_args, 
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE )

  print (" ".join(tx_args))
  print (" ".join(rx_args))

  # Push options 

  sekret = secrets.token_bytes(16)
  sekret = base64.b64encode(sekret)
  sekret = sekret.decode("utf-8")
  sekret = sekret.replace("=", "")

  escp.push_option( receiver, [], "REDY" )
  escp.push_option( receiver, ["CKEY",sekret], "OKAY" )
  escp.push_option( receiver, ["FILE",["16G",]], "OKAY" )
  escp.push_option( receiver, ["RECV",], "GoRX" )

  escp.push_option( sender, [], "REDY" )
  escp.push_option( sender, ["CKEY",sekret], "OKAY" )
  escp.push_option( sender, ["FILE",["16G",]], "OKAY" )
  escp.push_option( sender, ["SEND",], "GoTX" )

  # Initiate client 
  if (engine == "shmem"):

    res = sender.stdout.readline().decode("utf-8")
    res = res.strip("\n")
    if (res != 'SHM'):
      print("Bad output from DTN application", res)
    res = sender.stdout.readline().decode("utf-8")
    res = res.strip("\n")

    SHMpath = res
    print("Got SHMpath=%s" % SHMpath)

    args   = ["./dtn-utility", SHMpath, "/tmp/shm.log"]
    #args   = ["./dtn-utility", SHMpath ]
    #pprint.pprint(args)
    client = subprocess.Popen( args, stdout = subprocess.PIPE )

  #er = client.stdout.readline().decode("utf-8")
  #er = er.strip("\n")
  #print("Here is some text", er)

  tx_queue = queue.Queue()
  tx_read = threading.Thread( target=escp.mgmt_reader,
    args=( sender, tx_queue, "TX" ), daemon=True )
  tx_read.start()

  rx_queue = queue.Queue()
  rx_read = threading.Thread( target=escp.mgmt_reader,
    args=( receiver, rx_queue, "RX" ), daemon=True )
  rx_read.start()

  res = escp.progress_bar(rx_queue, tx_queue, receiver, sender)
  print( "" )


  time.sleep(1)
  print( sender.stdout.readline() )
  print( sender.stdout.readline() )
  print( sender.stdout.readline() )
  time.sleep(10)
  escp.push_option( sender, ["QUIT",], "EXIT" )

  if ( res != True ):
    print( "Transfer failed" )

# test_connection( "dummy" )

s = escp.EScp(doInit=False)

args = ['--args_src=\"--engine=shmem\"', '--args_dst=\"--engine=shmem\"'] + sys.argv[1:]
s.parseArgs(args=args)
s.applyArgs()


s.rx_cmd  = subprocess.Popen( s.receiver_args, stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE )
s.rx_mgmt = queue.Queue()
s.rx_stat = queue.Queue()

s.rx_thread = threading.Thread(target=escp.mgmt_reader,
      args=(s.rx_cmd, s.rx_stat, s.rx_mgmt, "RX"), daemon=True)
s.rx_thread.start()

try:
  s.push_rx( None, "REDY" )
except:
  print("Error connecting to host: ", s.rx_cmd.stderr.read().decode());
  sys.exit(0)

s.push_rx( ["HASH"], "OKAY" )
s.push_rx( ["CKEY", s.sekret], "OKAY" )

s.push_rx( ['CHDR', s.dst_path], ["CHDR","FILE"] )


s.push_rx( ["RECV"], "OKAY" )

s.tx_cmd = subprocess.Popen( s.sender_args, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE )

s.tx_mgmt = queue.Queue()
s.tx_stat = queue.Queue()

s.tx_thread = threading.Thread(target=escp.mgmt_reader,
      args=(s.tx_cmd, s.tx_stat, s.tx_mgmt, "TX"), daemon=True)
s.tx_thread.start()

s.push_tx( None, "REDY")
s.push_tx( ["HASH"], "OKAY" )
s.push_tx( ["CKEY", s.sekret], "OKAY" )

del s.sekret
s.sekret = "Meow! I'm a cat!"

#self.push_tx( ["FILE", self.src_files], "OKAY" )


s.push_tx( ["PERS"], "OKAY" )
s.push_tx( ["SEND"], "OKAY" )

res = s.tx_mgmt.get()
shm_args = [ './dtn-utility', res[1], "/tmp/shm.tx.log" ]
pprint.pprint(shm_args)

s.tx_shm  = subprocess.Popen( shm_args, stdin=subprocess.PIPE,
               stdout=subprocess.PIPE, stderr=subprocess.PIPE )

s.m_thread = \
  threading.Thread(target=escp.run_transfer, args=(s,), daemon=True)
s.m_thread.start()

pprint.pprint("Receiver")

res = s.rx_mgmt.get()
pprint.pprint(res)

# Need receive personality
shm_args = [ './dtn-utility', res[1], "/tmp/shm.rx.log" ]
s.rx_shm  = subprocess.Popen( shm_args, stdin=subprocess.PIPE,
               stdout=subprocess.PIPE, stderr=subprocess.PIPE )

# time.sleep(0.1)
#escp.progress_bar( s.rx_stat, s.tx_stat )

s.m_thread.join()




