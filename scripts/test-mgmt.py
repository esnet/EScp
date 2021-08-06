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

test_connection( "dummy" )


