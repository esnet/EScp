#!/usr/bin/python3
import os, sys, stat

import subprocess
import traceback
import pprint
import curses
import signal

import secrets
import base64
import shlex
import signal
import select
import socket

import argparse

try:
  import argcomplete
except:
  pass

import configparser

import threading
import queue
import time
import datetime

import logging

config = configparser.ConfigParser()
config.read(( os.path.join(os.path.dirname(sys.argv[0]), 'escp.conf'),
              '/usr/local/etc/escp.conf', '/etc/escp.conf',
              'escp.conf' ))

ESCP_VERSION = "NA"
LICENSE = """
ESnet Secure Copy (EScp) Copyright (c) 2021, The Regents of the
University of California, through Lawrence Berkeley National Laboratory
(subject to receipt of any required approvals from the U.S. Dept. of
Energy). All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

(1) Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

(2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

(3) Neither the name of the University of California, Lawrence Berkeley
National Laboratory, U.S. Dept. of Energy nor the names of its contributors
may be used to endorse or promote products derived from this software
without specific prior written permission.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

You are under no obligation whatsoever to provide any bug fixes, patches,
or upgrades to the features, functionality or performance of the source
code ("Enhancements") to anyone; however, if you choose to make your
Enhancements available either publicly, or directly to Lawrence Berkeley
National Laboratory, without imposing a separate written license agreement
for such Enhancements, then you hereby grant the following license: a
non-exclusive, royalty-free perpetual license to install, use, modify,
prepare derivative works, incorporate into other computer software,
distribute, and sublicense such enhancements or derivative works thereof,
in binary and source code form.
"""

try:
  ESCP_VERSION = config["escp"]["VERSION"]
except:
  pass

logging.info("Starting EScp: %s" % ESCP_VERSION)

def human_readable(number, figs):
  si_prefix = " KMGTPE"

  divisor = 10000
  if figs < 4:
    divisor = 1000

  while (number / divisor) > 1:
    number = number/1024;
    si_prefix = si_prefix[1:]

  if number < 1:
    number = 1

  sig_figs = len(str(int(number)))
  fraction = figs-sig_figs
  if fraction < 0:
    fraction = 0

  return "%*.*f%s" % (sig_figs, fraction, number, si_prefix[:1])

def show_progress( number, start_time, window, file_name, file_total=False ):
  try:
    bites = int(number)
    y,x = window.getmaxyx()

    fi = ", ".join(file_name)
    if len(fi)+30>x:
      fi = fi[:x-30] + "..."
    fill = x - len(fi)

    bytes_left = file_total - bites

    if bytes_left < 0:
      bytes_left = 0;

    rate = bites/(time.time() - start_time)

    if file_total:
      eta = bytes_left/rate
      eta = int(eta)

      if eta > 1:
        delta = datetime.timedelta(seconds=eta)
        eta = str(datetime.datetime.strptime(str(delta), "%H:%M:%S")).split()[1]
        eta = "%s ETA" % eta
      else:
        eta = ""
      progress = "%2.0f%% %sB %sB/s %s" % (
        (bites/file_total)*100,
        human_readable(bites, 4),
        human_readable(rate, 2),
        eta )
    else:

      progress = "%sB %sB/s" % (
        human_readable(bites, 4),
        human_readable(rate, 2),
        )


    sys.stdout.write("\r%s%*s" % ( fi, fill, progress ) )
  except Exception as e:
    logging.debug("show_progress got an error: %s", e)
    pass

def handler_ctrlc(signal, frame):
  print("\n\rInterrupt/Ctrl-C, exiting...")
  sys.exit(1)

def stream_write( stream, data ):

  if not data:
    return

  if isinstance( data, str ):
    data = [data,]

  if not isinstance( data, list ):
    raise ValueError("data must be a list")

  for i in data:
    if isinstance( i, list ):
      stream.stdin.write( str.encode("%d\n" % len(i)) )
      stream.stdin.write( str.encode("%s\n" % "\n".join(i)) )
    else:
      stream.stdin.write( str.encode("%s\n" % i) )

  stream.stdin.flush()


def stream_read( queue, data ):
  if data == None:
    return

  if not isinstance( data, str ) and not isinstance( data, list ):
    raise ValueError("data must be a string/list")

  res = queue.get()

  if ( isinstance(data, list) ) and res[0] in data:
    return res[0]

  if res[0] != data:
    #raise ValueError("Stream Read Error")
    print("Stream Read Error: '%s'" % " ".join(res[1:]) )
    print("actual='%s' != expected='%s'" % (res[0], data) )
    sys.exit(1)

  return res[0]


def mgmt_reader( stream, stat_queue, mgmt_queue, name ):
  logging.debug("start mgmt_reader '%s'" % name )
  while 1:
    line = stream.stdout.readline()
    line = line.decode("utf-8")
    line = line.strip("\n")

    logging.debug("mgmt_reader '%s' got: %s" % ( name, line ) )

    if not line:
      logging.debug("mgmt_reader '%s': connection terminated " % name )
      stat_queue.put("ABORT")
      stat_queue.put("Session terminated early")
      mgmt_queue.put(("ABORT", "Session terminated"));
      return;

    if line in ("OKAY", "REDY", "FILE", "CHDR"):
      logging.debug("mgmt_reader '%s': %s" % (name, line) )
      mgmt_queue.put((line,))
      continue

    if line == "SESS":
      line = stream.stdout.readline().decode("utf-8")
      logging.debug("mgmt_reader '%s': SESS %s" % (name, line) )
      #mgmt_queue.put(("SESS",line))
      continue

    if line == "SHM":
      line = stream.stdout.readline().decode("utf-8")
      line = line.strip("\n")
      logging.debug("mgmt_reader '%s': SHM %s" % (name, line) )
      mgmt_queue.put(("SHM",line))
      continue

    if line == "STAT":
      line = stream.stdout.readline().decode("utf-8")
      line = line.strip("\n")
      logging.debug("mgmt_reader '%s': STAT %s " % (name, line) )
      stat_queue.put("STAT")
      stat_queue.put(int(line))
      continue

    if line == "ABRT":
      line = stream.stdout.readline().decode("utf-8")
      logging.debug("mgmt_reader '%s': ABRT %s " % (name, line) )
      stat_queue.put("ABORT")
      stat_queue.put(line)
      mgmt_queue.put(("ABORT",line))
      return;

    if line == "FTOT":
      logging.debug("FTOT readline")
      line = stream.stdout.readline().decode("utf-8")
      line = line.strip("\n")
      count, bites= map( lambda x: int(x), line.split( " ", maxsplit=1 ) )
      logging.debug("mgmt_reader '%s': FTOT fi=%d bytes=%d" %
                     (name, count, bites))
      mgmt_queue.put( (count, bites) )
      continue


    if line == "OPEN":
      line = stream.stdout.readline().decode("utf-8")
      line = line.strip("\n")
      no, fi = line.split( " ", maxsplit=1 )
      logging.debug("mgmt_reader '%s': OPEN %s %d" % (name, fi, int(no)) )
      stat_queue.put( 'OPEN' )
      stat_queue.put( (no, fi) )
      continue

    if line == "STOP":
      line = stream.stdout.readline().decode("utf-8")
      line = line.strip("\n")
      no, fi = line.split( " ", maxsplit=1 )
      logging.debug("mgmt_reader '%s': CLOSE %s %d" % (name, fi, int(no)) )
      stat_queue.put( 'STOP' )
      stat_queue.put( (no, fi) )
      continue

    if line == "XIT":
      logging.debug("mgmt_reader '%s': Exit successfully" % name )
      stat_queue.put( 'EXIT' )
      mgmt_queue.put( 'EXIT' )
      return

    logging.debug("mgmt_reader '%s': not recognized '%s'" % (name, line))
    print ("Not recognized  '%s'" % line)


def progress_bar( rx_queue, tx_queue ):
  file_count = 0
  file_completed = 0

  bytes_total = 0

  file_open = set()

  logging.debug("start progress_bar")

  start_time = time.time()
  error = ""

  msg = "Exited normally"
  exit_count=0

  try:
    window = curses.initscr()

    while exit_count < 2:

     got_results = False

     try:
       res=rx_queue.get_nowait()
       got_results = True
       if res == "EXIT":
         logging.debug("Receiver exiting successfully")
         exit_count+=1
         continue
       if res == "ABORT":
         logging.debug("RX got ABORT");
         msg = rx_queue.get()
         error += "RX %s" % msg
         logging.info("RX Error: %s" % (error) )
         exit_count+=2

         continue
       msg = rx_queue.get()
       logging.debug("RX ignoring %s: %s" % ( res, msg ) )
     except queue.Empty:
       logging.debug("RX queue empty")
       pass
     except Exception as e:
       raise ValueError("Unexpected exception %s" % type(e))
       break

     try:
       res=tx_queue.get_nowait()
       got_results = True

       if res == "EXIT":
         logging.debug("Receiver exiting successfully")
         exit_count+=1

         continue

       if res == "ABORT":
         logging.debug("TX got ABORT");
         msg = tx_queue.get()
         error += "TX %s" % msg
         logging.info("TX Error: %s" % error)
         exit_count+=2

         continue

       msg = tx_queue.get()

       if res == "OPEN":
         number, fn = msg
         file_count += 1
         file_open.add(fn)
         logging.debug("TX got OPEN on %s" % fn);
         continue

       if res == "STOP":
         number, file_name = msg
         file_completed += 1
         file_open.remove(file_name)
         logging.debug("TX got STOP on %s" % file_name);
         continue

       if res == "STAT":
         m = str(msg)
         logging.debug("TX got STAT %s" % m);
         show_progress(msg, start_time, window, file_open)
         continue

       logging.debug("TX got Unknown operator '%s' %s" % (res, msg) )
     except queue.Empty:
       logging.debug("TX queue mt")
       pass
     except Exception as e:
       exc_type, exc_value, exc_traceback = sys.exc_info()
       traceback.print_tb(exc_traceback, limit=1, file=sys.stdout)
       raise ValueError("Unexpected exception %s" % type(e))
       break

     if not got_results:
       time.sleep(0.1)
     logging.debug("looping")
  finally:
    curses.endwin()

  logging.debug("Progress bar is finished: '%s'" % (error) )

  if error:
    print( "\nTransfer terminated: ", error )
    logging.debug("System terminate!")
    sys.exit(1)

def file_recurse( self, files, path=None ):

  total = 0

  file_list = []
  dir_list = []
  flush = 15

  logging.debug("file_recurse: path=%s", path)

  for fi in files:
    if path:
      fi = os.path.join( path, fi )
    fi_stat = os.stat(fi)
    if stat.S_ISDIR(fi_stat.st_mode):
      dir_list.append(fi)
      continue

    if not stat.S_ISREG(fi_stat.st_mode):
      logging.debug("Skipping %s, it is neither a file nor directory" % fi)

    total += fi_stat.st_size
    file_list.append(fi)
    flush += 1

    if flush > 20:
      self.push_tx( ["FILE", file_list], "OKAY" )
      flush=0
      file_list=[]
    pass

  if file_list:
    self.push_tx( ["FILE", file_list], "OKAY" )

  for d in dir_list:
    total += file_recurse( self, os.listdir(d), path=d )

  return total


def run_transfer( self ):
  self.push_tx( ["STAT"], "OKAY" )

  total = file_recurse( self, self.args.files[:-1] )
  logging.debug("File total: %d", total)

  try:
    self.push_tx( ["DONE"], "OKAY")
    print ("")
  except:
    print("Transfer failed.")
    res = self.rx_mgmt.get_nowait()
    if res != "EXIT":
      print ("Error: ", res)

class EScp:
  def push_rx( self, option, response=None ):
    stream_write( self.rx_cmd, option )
    return stream_read(  self.rx_mgmt, response )

  def push_tx( self, option, response=None ):
    stream_write( self.tx_cmd, option )
    return stream_read(  self.tx_mgmt, response )

  def parseArgs(self, args=sys.argv[1:]):
    parser = argparse.ArgumentParser(
      description='EScp: Secure Network Transfer',
      fromfile_prefix_chars='@')

    parser.add_argument('files', metavar='FILE', nargs='*',
                        help='[SRC] ... [DST], where DST is HOST:PATH')

    parser.add_argument('-P','--port', metavar='PORT',
                        help="Port for SSH[/DTN]" )
    parser.add_argument('-q','--quiet', action='store_const', const=1)
    parser.add_argument('-r','--recursive', action='store_const', const=1)
    parser.add_argument('-v','--verbose', action='store_const', const=1)
    parser.add_argument('-l','--license', action='store_const', const=1)

    parser.add_argument('--args_dst', metavar='ARG', type=str,
                        help="Arguments to DST DTN Executable")
    parser.add_argument('--args_src', metavar='ARG', type=str,
                        help="Arguments to SRC DTN Executable")
    parser.add_argument('--path_dst', metavar='PATH', type=str,
                        help="Path to DST DTN Executable")
    parser.add_argument('--path_src', metavar='PATH', type=str,
                        help="Path to SRC DTN Executable")
    parser.add_argument('--version', action='store_const', const=1)

    try:
      argcomplete.autocomplete(parser)
    except:
      pass

    args = parser.parse_args(args)

    if args.license:
      print (LICENSE)
      sys.exit(0)


    if args.version:
      print ("EScp: %s" % ESCP_VERSION )
      dtn = "dtn"
      try:
        dtn = config["escp"]["dtn_path"]
      except:
        pass
      s=subprocess.run([dtn, "--version"], capture_output=True)
      s = s.stdout.decode("utf-8").strip()
      print ("DTN:  %s" % s)
      sys.exit(0)

    if not args.files or (len(args.files) < 2):
      print ("both SRC and DST must be specified")
      sys.exit(1)

    for i in args.files[:-1]:
      if not os.path.exists(i):
        print("Source file '%s' not found" % i)
        sys.exit(-1)

    self.args = args

  def applyArgs(self):
    ssh_host = False
    dst_host = "localhost"

    ssh_opts = ""
    ssh_port = False
    dtn_port = 2222

    if self.args.port:
      try:
        ssh_port, dtn_port = map( int, self.args.port.split("/"))
      except:
        ssh_port = int( self.args.port )

    if ssh_port:
      ssh_opts += "-p %d" % ssh_port

    try:
      ssh_host, dst_path = self.args.files[-1].split(":")
      parts = ssh_host.split("@")
      if len(parts) > 1:
        dst_user, dst_host = parts
      else:
        dst_host = ssh_host
    except:
      dst_path = self.args.files[-1]

    if (self.args.path_dst):
      remote_dtn = self.args.path_dst
    else:
      try:
        remote_dtn = config[dst_host]["dtn_path"]
      except:
        remote_dtn = "dtn"

    if (self.args.path_src):
      local_dtn = self.args.path_src
    else:
      try:
        local_dtn = config["escp"]["dtn_path"]
      except:
        local_dtn = "dtn"


    args_dst = []

    try:
      args_dst += shlex.split(config[dst_host]["dtn_args"])
    except:
      pass

    args_dst += [ "-s", "--managed" ]

    if (self.args.args_dst):
      args_dst += shlex.split(self.args.args_dst)

    args_src = [ local_dtn, ]

    try:
      args_src += shlex.split(config["escp"]["dtn_args"])
    except:
      pass

    """
      This is our host logic:
        1) ssh_host is always whatever is specified in command line, this is
           so that the ssh_host you use should match your known_hosts file.
        2) We then convert the host (or IP) into an IP; This is used for DTN
        3) DTN receiver will listen on this interface
        4) DTN sender will connect to listed IP
    """

    ip_addr = socket.getaddrinfo(dst_host, None)[0][4][0]


    args_src += ["--managed", "-c %s/%d" % (ip_addr, dtn_port)]
    args_dst += ["-c", "%s/%d" % (ip_addr, dtn_port) ]

    if self.args.verbose:
      args_src += [ "--verbose", "--logfile", "/tmp/dtn.tx.log" ]
      args_dst += [ "--verbose", "--logfile", "/tmp/dtn.rx.log" ]
      logging.basicConfig(filename='/tmp/escp.log', level=logging.DEBUG)

    if (self.args.args_src):
      args_src += shlex.split(self.args.args_src);

    sekret = secrets.token_bytes(16)
    sekret = base64.b64encode(sekret)
    sekret = sekret.decode("utf-8")
    sekret = sekret.replace("=", "")

    if ssh_host:
      ssh_args = [ "ssh", ssh_host, remote_dtn ]
    else:
      ssh_args = [ local_dtn, ]

    ssh_args += args_dst


    if len(ssh_opts):
      ssh_args.insert(1, ssh_opts)

    if (self.args.verbose):
      print("local_dtn: %s, remote_dtn: %s" % (local_dtn, remote_dtn) )
      print("dst_host: %s, dst_path: %s" % ( dst_host, dst_path ))
      print("Auth secret = ...%s" % sekret[-4:])
      print("SSH command = '%s'" % " ".join(ssh_args))
      print("Local command = '%s'" % " ".join(args_src))

    if not dst_path:
      dst_path="."

    self.dst_path = dst_path
    self.receiver_args = ssh_args
    self.sender_args   = args_src

    self.sekret = sekret

  def connect(self):
    self.rx_cmd  = subprocess.Popen( self.receiver_args, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE )
    self.rx_mgmt = queue.Queue()
    self.rx_stat = queue.Queue()

    self.rx_thread = threading.Thread(target=mgmt_reader,
          args=(self.rx_cmd, self.rx_stat, self.rx_mgmt, "RX"), daemon=True)
    self.rx_thread.start()

    try:
      self.push_rx( None, "REDY" )
    except:
      print("Error connecting to host: ", self.rx_cmd.stderr.read().decode());
      sys.exit(0)

    self.push_rx( ["HASH"], "OKAY" )
    self.push_rx( ["CKEY", self.sekret], "OKAY" )

    #self.push_rx( ["FILE", self.dst_files], "OKAY" )
    if self.dst_path:
      res = self.push_rx( ['CHDR', self.dst_path], ["CHDR","FILE"] )
      if res == "FILE":
        if len(self.args.files[:-1]) != 1:
          self.push_rx( ["EXIT"], "OKAY" );
          print("target '%s' is not a directory" % self.dst_path)
          sys.exit(0)
        self.push_rx( ["FILE", [self.args.files[1]]], "OKAY" )

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


    #self.push_rx( ["DONE"], "OKAY" )
    #print ("Finished assigning options")


  def __init__(self, doInit=True):
    if doInit:
      self.parseArgs()
      self.applyArgs()

if __name__ == "__main__":

  signal.signal(signal.SIGINT, handler_ctrlc)

  escp = EScp()
  try:
    escp.connect()
  except Exception as e:
    print("Error: ", e)
    sys.exit(1)

