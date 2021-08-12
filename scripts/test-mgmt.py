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

s = escp.EScp()

args = ['--args_src=\"--engine=shmem\"', '--args_dst==\"--engine=shmem\"'] + sys.argv 
s.parseArgs(args=args)


s.rx_cmd  = subprocess.Popen( self.receiver_args, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE )
s.rx_mgmt = queue.Queue()
s.rx_stat = queue.Queue()

s.rx_thread = threading.Thread(target=escp.mgmt_reader,
      args=(s.rx_cmd, s.rx_stat, s.rx_mgmt, "RX"), daemon=True)
s.rx_thread.start()

try:
  s.push_rx( None, "REDY" )
except:
  print("Error connecting to host: ", self.rx_cmd.stderr.read().decode());
  sys.exit(0)

s.push_rx( ["HASH"], "OKAY" )
s.push_rx( ["CKEY", self.sekret], "OKAY" )

    #self.push_rx( ["FILE", self.dst_files], "OKAY" )
s.push_rx( ['CHDR', self.dst_path], "OKAY" )

    self.push_rx( ["RECV"], "OKAY" )

    self.tx_cmd = subprocess.Popen( self.sender_args, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE )

    self.tx_mgmt = queue.Queue()
    self.tx_stat = queue.Queue()

    self.tx_thread = threading.Thread(target=mgmt_reader,
          args=(self.tx_cmd, self.tx_stat, self.tx_mgmt, "TX"), daemon=True)
    self.tx_thread.start()

    self.push_tx( None, "REDY")
    self.push_tx( ["HASH"], "OKAY" )
    self.push_tx( ["CKEY", self.sekret], "OKAY" )

    del self.sekret
    self.sekret = "Meow! I'm a cat!"

    #self.push_tx( ["FILE", self.src_files], "OKAY" )
    self.push_tx( ["PERS"], "OKAY" )
    self.push_tx( ["SEND"], "OKAY" )

    self.m_thread = \
      threading.Thread(target=run_transfer, args=(self,), daemon=True)
    self.m_thread.start()

    # time.sleep(0.1)
    progress_bar( self.rx_stat, self.tx_stat )

    self.m_thread.join()





pprint.pprint(args)

