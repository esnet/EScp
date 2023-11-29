#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!("../bindings.rs");

extern crate clap;
extern crate flatbuffers;
extern crate rand;

// use hwloc::Topology;
use clap::Parser;
use std::{env, process, thread, collections::HashMap, sync::mpsc};
use std::ffi::{CString, CStr};
use regex::Regex;
use subprocess::{Popen, PopenConfig, Redirection};
use std;
use libc;
use std::io;
use std::io::Read;
use std::io::Write;
use std::os::unix::net::{UnixStream,UnixListener};
use std::os::fd::AsRawFd;
use std::os::fd::FromRawFd;
use std::fs;
use hex;
use crossbeam_channel;
use clean_path;

use log::{{debug, info, error}};

use shadow_rs::shadow;
shadow!(build);


#[allow(dead_code, unused_imports)]
#[allow(clippy::all)]
mod file_spec;

#[allow(dead_code, unused_imports)]
#[allow(clippy::all)]
mod session_init;

include!("license.rs");
include!("logging.rs");

const msg_session_init:u16      =1;
const msg_file_spec:u16         =2;
const msg_session_complete:u16  =3;
const msg_session_terminate:u16 =5;

const config_items: [&str; 2]= [ "cpumask", "nodemask" ];


macro_rules! sess_init {
  ($i:tt) => {
    {
      let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(128);
      let s_init= session_init::Session_Init::create(
        &mut builder,
        &session_init::Session_InitArgs $i
        );

      builder.finish(s_init, None);
      builder
    }
  }
}

static GLOBAL_FILEOPEN_CLEANUP: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
static GLOBAL_FILEOPEN_TAIL: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

static GLOBAL_FILEOPEN_COUNT: usize = 4;
static GLOBAL_DIROPEN_COUNT: usize = 2;
static GLOBAL_FINO: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

fn _print_type_of<T>(_: &T) {
    println!("{}", std::any::type_name::<T>())
}

fn int_from_human ( str: String  ) -> u64 {
  /* Examples:  IN -> OUT
                 1 -> 1
             1kbit -> 1000
                1M -> 1048576
   */
  let re = Regex::new(r"[a-zA-Z]").unwrap();
  let idx;

  match re.find(str.as_str()) {
    Some(value) => { idx = value.start(); }
    _ => { idx = str.len(); }
  }

  let (number,unit) = str.split_at(idx);
  let val;

  match number.parse::<u64>() {
    Ok(value)  => { val = value; }
    Err(_) => { println!("Unable to parse {}", str ); return 0 }
  }
  let u = &unit.to_lowercase();

  let unit_names = " kmgtpe";
  let mut pow = 0;

  if u.len() > 0 {
    let (x,_y) = u.split_at(1);
    match unit_names.find(x) {
      Some(value) => { pow = value as u32; }
      _           => { pow = 0; }
    }
  }

  let mut multiplier: u64 = 1024;
  if u.contains("bit") {
    multiplier = 1000;
  }

  return val * multiplier.pow(pow);
}

/*
fn topo() {
  let topo = Topology::new();

	for i in 0..topo.depth() {
		println!("*** Objects at level {}", i);

		for (idx, object) in topo.objects_at_depth(i).iter().enumerate() {
			println!("{}: {}", idx, object);
		}
	}
}
*/

fn to_header ( sz: u32, typ: u16 ) -> Vec<u8> {
  let input = [ (sz >> 16) as u16, (sz & 0xffff) as u16, typ ];
  input.iter().flat_map(|val| val.to_be_bytes()).collect()
}

fn from_header( i: Vec<u8> ) -> ( u32, u16 ) {
  ((i[0] as u32) << 24 | (i[1] as u32) << 16 | (i[2] as u32) << 8 | (i[3] as u32), (i[4] as u16) << 8 | (i[5] as u16) )
}

fn start_receiver( args: dtn_args_wrapper ) {
  debug!("start_receiver started");

  let ret;
  unsafe{
    ret = rx_start( args.args );
  }
  if ret != 0 {
    error!("Failed to start receiver");
    thread::sleep(std::time::Duration::from_millis(200));
    process::exit(-1);
  }

  debug!("start_receiver complete");
}

fn escp_receiver(safe_args: dtn_args_wrapper, flags: EScp_Args) {
  let args = safe_args.args;

  let (mut sin, mut sout, file, file2, listener, stream);

  let mut buf = vec![ 0u8; 6 ];
  let mut direct_mode = true;

  if flags.mgmt.len() > 0 {
    _ = fs::remove_file(flags.mgmt.clone());
    listener = UnixListener::bind(flags.mgmt).unwrap();
    stream = listener.accept().unwrap().0 ;

    /*
    let mut response = String::new();
    stream.read_to_string(&mut response);
    eprintln!("Foo {:?}", response);
    */

    let fd = stream.as_raw_fd();
    unsafe {
      file = std::fs::File::from_raw_fd(fd);
      file2 = std::fs::File::from_raw_fd(fd);
    }
    sin = file;
    sout = file2;
  } else {
    unsafe {
      file = std::fs::File::from_raw_fd(0);  // stdin
      file2 = std::fs::File::from_raw_fd(1); // stdout
    }

    sin = file;
    sout = file2;
  }

  let result = sin.read_exact( &mut buf );

  match result {
    Ok(_)  => { }
    Err(error) => {
      eprintln!("Failed to read init message {:?}", error );
      return
    }
  }

  let (sz, t) = from_header( buf.to_vec() );
  let helo;

  debug!("Got header of type {}", t);

  let mut port_start = 1232;
  let mut port_end = 65536; // XXX: port_end not implemented
  let mut bind_interface = CString::new( "" ).unwrap();

  if t == msg_session_init {

    buf.resize( sz as usize, 0 );
    let res = sin.read_exact( &mut buf);
    match res {
      Ok (_) => {},
      Err (error) => {
        info!("Bad read from SSH {:?}", error);
        return
      }
    }
    helo = flatbuffers::root::<session_init::Session_Init>(buf.as_slice()).unwrap();
    unsafe {
      (*args).session_id  = helo.session_id();
      if helo.do_verbose() {
        verbose_logging=1;
      }
    }
    if helo.port_start() > 0 {
      port_start = helo.port_start();
    }
    if helo.port_end() > 0 {
      port_end   = helo.port_end();
    }

    if helo.do_crypto() {
      unsafe {
        let ptr: Vec<i8> = helo.crypto_key().unwrap().iter().collect() ;

        std::intrinsics::copy_nonoverlapping( ptr.as_ptr(), (*args).crypto_key.as_ptr() as *mut i8, 16 );
        (*args).do_crypto = true;
      }
    }

    if helo.io_engine() > 0 {
      unsafe {
        (*args).io_engine = helo.io_engine();
        (*args).io_engine_name = "RCVER".as_ptr() as *mut i8;
      }
    }

    if helo.no_direct() { direct_mode = false; }

    match helo.bind_interface() {
      Some( string )  => { bind_interface = CString::new(string).unwrap(); },
      _ => { },
    }

    initialize_logging( "/tmp/escp.log.", safe_args);

     debug!("Session init {:016X?}", helo.session_id());
  } else {
    error!("Expected session init message");
    process::exit(-1);
  }

  let p = CString::new( port_start.to_string() ).unwrap();

  let mut connection_count=0;
  unsafe {
    (*args).sock_store[connection_count] =  dns_lookup( bind_interface.as_ptr() as *mut i8 , p.as_ptr() as *mut i8);
    connection_count += 1;
    (*args).sock_store_count = connection_count as i32;
    (*args).flags |= libc::O_CREAT|libc::O_WRONLY|libc::O_TRUNC;
    (*args).active_port = 0;
  }

  debug!("Spawning receiver");

  _ = thread::Builder::new().name("rcvr".to_string()).spawn(
        move || start_receiver( safe_args ) );

  let port;
  unsafe {
    port = file_get_activeport( args as *mut ::std::os::raw::c_void );
  }

  if port > port_end {
    error!("Couldn't assign a port between {} and {}. Got {}", port_start, port_end, port);
    process::exit(-1);
  }

  debug!("Receiver spawned on port {}", port);

  let builder = sess_init!( {
        version_major: env!("CARGO_PKG_VERSION_MAJOR").parse::<i32>().unwrap(),
        version_minor: env!("CARGO_PKG_VERSION_MINOR").parse::<i32>().unwrap(),
        port_start: port as i32,
        ..Default::default()
    }
  );
  let buf = builder.finished_data();

  let mut hdr = to_header( buf.len() as u32, msg_session_init );
  _ = sout.write( &mut hdr );
  _ = sout.write( buf );
  _ = sout.flush();


  unsafe {
    dtn_waituntilready(  args as *mut ::std::os::raw::c_void );
  }
  debug!("Finished Session Init bytes={:?}", buf.len() );

  let mut filecount=0;


  loop {
    let mut buf = vec![ 0u8; 6 ];
    let res = sin.read_exact( &mut buf );
    match res {
      Ok (_) => {},
      Err (error) => {
        info!("Bad read from SSH {:?}", error);
        return
      }
    }

    let (sz, t) = from_header( buf.to_vec() );

    if t == msg_file_spec {
      buf.resize( sz as usize, 0 );
      let res = sin.read_exact( &mut buf);

      match res {
        Ok (_) => {},
        Err (error) => {
          info!("Bad read from SSH {:?}", error);
          return
        }
      }

      let fs = flatbuffers::root::<file_spec::ESCP_file_list>(buf.as_slice()).unwrap();
      let root = fs.root().unwrap();
      debug!("Root set to: {}", root);

      for entry in fs.files().unwrap() {
        let mut full_path;

        unsafe{
          let filename = entry.name().unwrap();
          if root.len() > 0 {
            full_path = format!("{}/{}", root, filename);
          } else {
            full_path = format!("{}", filename);
          }

          if fs.complete() && (filecount==0) && (fs.files().unwrap().len()==1 &&
             (root.len() > 0) ) {

            // If src is a single file and dest is not a directory, we
            // use dest as name for file on remote system

            let path = std::path::Path::new(root);
            if !path.is_dir() {
              full_path = root.to_string();
            }

          }

          let open  = (*(*args).fob).open.unwrap();
          let close = (*(*args).fob).close_fd.unwrap();
          let mut fd;

          let fp = CString::new( full_path.clone() ).unwrap();


          for _ in 1..3 {
            if direct_mode {
              fd = open( fp.as_ptr(), (*args).flags | libc::O_DIRECT, 0o644 );
              if (fd == -1) && (*libc::__errno_location() == 22) {
                direct_mode = false;
                continue;
              }
            } else {
              fd = open( fp.as_ptr(), (*args).flags, 0o644 );
            }


            if entry.sz() < 1 {
              debug!("Closed and ignoring {fino} because 0 sz", fino=entry.fino());
              close(fd);
              break;
            }

            if fd < 1 {
              let err = io::Error::last_os_error();
              if err.kind() == std::io::ErrorKind::NotFound {

                let path = std::path::Path::new(full_path.as_str());
                let _ = fs::create_dir_all(path.parent().unwrap());

                debug!("Create directory {path:?}");
                continue;
              }
              info!("Got an error opening file {:?} {:?}",
                    filename, err);
              return;
            }

            debug!("Add file {full_path}:{fino} with {:#X} sz={sz} fd={fd}",
                   (*args).flags, fino=entry.fino(), sz=entry.sz() );

            file_addfile( entry.fino(), fd, 0, entry.sz() );
            filecount += 1;

            break;
          }
        }
      }

      continue;
    }

    if t == msg_session_terminate {
      debug!("Got terminate request sz={sz}, type={t}");
      // XXX: Should do an immediate exit here
      break;
    }

    if t == msg_session_complete {
      debug!("Got session complete request sz={sz}, type={t}");
      break;
    }

    debug!("Got message from sender sz={sz}, type={t}");
  }

  unsafe {
    debug!("Calling finish transfer");
    finish_transfer( args as *mut dtn_args, filecount );
  }

  debug!("Transfer Complete. Sending Session Finished Message.");

  let mut hdr = to_header( 0, msg_session_complete );
  _ = sout.write( &mut hdr );
  _ = sout.flush();


}

fn do_escp(args: *mut dtn_args, flags: EScp_Args) {
  let (host,dest_tmp,dest);
  let safe_args = dtn_args_wrapper{ args: args };


  match flags.destination.rfind(':') {
    Some (a) => { (host, dest_tmp) = flags.destination.split_at(a); },
    _        => {
      eprintln!("Expected ':' in argument '{}'; local copy not implemented",
                flags.destination);
      process::exit(-1);
    }
  }

  (_, dest) = dest_tmp.split_at(1);

  unsafe{
    if (*args).do_server {
      escp_receiver ( safe_args, flags );
      return ;
    }
  }

  initialize_logging("/tmp/escp.log.", safe_args);
  debug!("Transfer to host: {}, dest_files: {} ", host, dest );

  let (mut sin, mut sout, mut serr, file, proc, stream, fd);
  if flags.mgmt.len() > 0 {
    stream = UnixStream::connect(flags.mgmt).unwrap();
    fd = stream.as_raw_fd();

    unsafe {
      file = std::fs::File::from_raw_fd(fd);
    }
    sin = &file;
    sout = &file;
    serr = &file;
  } else {
    let port_str = flags.ssh_port.to_string();
    let mut ssh_args = vec![flags.ssh.as_str(), "-p", port_str.as_str()];
    let escp_cmd;

    if flags.identity.len() > 0 {
      ssh_args.extend(["-i", flags.identity.as_str()]);
    }

    if flags.cipher.len() > 0 {
      ssh_args.extend(["-c", flags.cipher.as_str()]);
    }

    if flags.agent {
      ssh_args.push("-A");
    }

    ssh_args.push(host);

    if flags.verbose {
      escp_cmd = format!("RUST_BACKTRACE=1 {}", flags.escp);
      ssh_args.push(escp_cmd.as_str());
      ssh_args.push("-v"); // This is redundant because we set in sess_init also
    } else {
      ssh_args.push(flags.escp.as_str());
    }

    ssh_args.extend([ "--server", "ignore", "me:" ]);
    debug!("Executing SSH with args: {:?} ", ssh_args );

    proc = Popen::create( &ssh_args, PopenConfig {
      stdout:  Redirection::Pipe,
      stdin:   Redirection::Pipe,
      stderr:  Redirection::Pipe,
      ..Default::default()
    }).unwrap();

    sin  = proc.stdin.as_ref().unwrap();
    sout = proc.stdout.as_ref().unwrap();
    serr = proc.stderr.as_ref().unwrap();
  }
  let (session_id, start_port, do_verbose, crypto_key, io_engine, nodirect);

  crypto_key = vec![ 0i8; 16 ];

  unsafe {
    (*args).do_crypto = true;
    tx_init(args);
    (*args).session_id = rand::random::<u64>();
    session_id = (*args).session_id;
    start_port = (*args).active_port;
    io_engine  = (*args).io_engine;
    nodirect  = (*args).nodirect;
    do_verbose = verbose_logging  > 0;
    std::intrinsics::copy_nonoverlapping( (*args).crypto_key.as_ptr() , crypto_key.as_ptr() as *mut u8, 16 );
  }

  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(128);
  let ckey = Some( builder.create_vector( &crypto_key ) );

  let bu = session_init::Session_Init::create(
    &mut builder, &session_init::Session_InitArgs{
      version_major: env!("CARGO_PKG_VERSION_MAJOR").parse::<i32>().unwrap(),
      version_minor: env!("CARGO_PKG_VERSION_MINOR").parse::<i32>().unwrap(),
      session_id: session_id,
      port_start: start_port as i32,
      do_verbose: do_verbose,
      do_crypto: true,
      crypto_key: ckey,
      io_engine: io_engine,
      no_direct: nodirect,
      ..Default::default()
    }
  );
  builder.finish( bu, None );
  let buf = builder.finished_data();

  debug!("Sending session_init message of len: {}", buf.len() );

  let mut hdr  = to_header( buf.len() as u32, msg_session_init );

  _ = sin.write( &mut hdr );
  _ = sin.write( buf );
  _ = sin.flush();

  {

    debug!("Wait for response from receiver");

    let mut buf = vec![ 0u8; 6 ];
    let result = sout.read_exact( &mut buf );

    match result {
      Ok(_)  => { }
      Err(error) => {
        error!("Bad read from SSH {:?}", error );
        let mut b = Vec::new();
        _ = serr.read_to_end( &mut b );
        let s = String::from_utf8_lossy(&b);
        error!("SSH returned {}", s );
        eprint!("{}", s);
        return
      }
    }

    let (sz, t) = from_header( buf.to_vec() );
    debug!("Got sz={:?} of type={:?}", sz, t);

    let helo;
    buf.resize( sz as usize, 0 );
    let result = sout.read_exact( &mut buf);
    match result {
      Ok (_) => {}
      Err(error) => {
        error!("SSH session read failed {:?}", error );
        return;
      }
    }

    helo = flatbuffers::root::<session_init::Session_Init>(buf.as_slice()).unwrap();
    debug!("Got response from receiver");


    unsafe {
      // Connect to host using port specified by receiver
      let d_str = CString::new( helo.port_start().to_string() ).unwrap();
      let host_str = CString::new( host ).unwrap();

      debug!("Sender params: {:?} {:?}", host_str, d_str);
      (*args).sock_store[0] =  dns_lookup( host_str.as_ptr() as *mut i8 , d_str.as_ptr() as *mut i8);
      (*args).sock_store_count = 1;
      (*args).active_port = helo.port_start() as u16;

      debug!("Starting Sender");
      tx_start (args);
    }

    // For the purpose of metrics, we consider this to be the start of the
    // transfer. At this point we have not read any data from disk but have
    // configured the transfer endpoints.

    let start    = std::time::Instant::now();

    let mut fi;

    unsafe {
      fi = std::fs::File::from_raw_fd(1);
      if !flags.quiet {
        _ = fi.write(b"\rCalculating ... ");
        _ = fi.flush();
      }
    }

    let bytes_total = iterate_files( flags.source, safe_args, sin, dest.to_string(), flags.quiet, &fi );
    debug!("Finished iterating files, total bytes={bytes_total}");

    if bytes_total <= 0 {
      eprintln!("Nothing to transfer, exiting.");
      process::exit(1);
    }

    loop {
      if flags.quiet {
        break;
      }

      let interval = std::time::Duration::from_millis(200);
      thread::sleep(interval);

      let bytes_now;
      unsafe {
        bytes_now = get_bytes_io( args as *mut dtn_args );
      }

      if bytes_now == 0 {
        continue;
      }

      let duration = start.elapsed();

      let width= ((bytes_now as f32 / bytes_total as f32) * 40.0) as usize ;
      let progress = format!("{1:=>0$}", width, ">");
      let rate = bytes_now as f32/duration.as_secs_f32();

      let eta= ((bytes_total - bytes_now) as f32 / rate) as i64;
      let eta_human;

      if eta > 3600 {
        eta_human = format!("{:02}:{:02}:{:02}", eta/3600, (eta/60)%60, eta%60);
      } else {
        eta_human = format!("{:02}:{:02}", eta/60, eta%60);
      }

      let rate_str;
      let tot_str;

      unsafe {
        let tmp = human_write( rate as u64, !flags.bits );
        rate_str= CStr::from_ptr(tmp).to_str().unwrap();

        let tmp = human_write( bytes_now as u64, true );
        tot_str= CStr::from_ptr(tmp).to_str().unwrap();

        debug!("{}/{}", bytes_now, bytes_total);
      }

      let units;
      if flags.bits {
        units = "bits"
      } else {
        units = "B"
      }


      let bar = format!("\r [{: <40}] {}B {}{}/s {: <10}",
                        progress, tot_str, rate_str, units, eta_human);
      _ = fi.write(bar.as_bytes());
      _ = fi.flush();

      if bytes_now >= bytes_total {
        let s = format!("\rComplete: {tot_str}B at {rate_str}{units}/s in {:0.1}s {:38}\n",
          duration.as_secs_f32(), "");
        _ = fi.write(s.as_bytes());
        _ = fi.flush();
        break;
      }
    }

    {
      let mut hdr  = to_header( 0, msg_session_complete );

      _ = sin.write( &mut hdr );
      _ = sin.flush();

      let mut buf = vec![ 0u8; 6 ];
      let result = sout.read_exact( &mut buf );
      match result {
        Ok(_)  => { }
        Err(error) => {
          error!("Connection to receiver disconnected; {error}");
          eprintln!("Connection to receiver disconnected; {error}");
          return;
        }
      }
    }
  }

  debug!("Finished transfer");
  println!("");
}

#[ derive(Clone, Copy) ]
struct dtn_args_wrapper {
  args: *mut dtn_args
}

fn iterate_dir_worker(  dir_out:  crossbeam_channel::Receiver<(String, String, i32)>,
                        files_in: crossbeam_channel::Sender<(String, String)>,
                        args:     dtn_args_wrapper ) {

  let (close, fdopendir, readdir);
  unsafe {
    close     = (*(*args.args).fob).close_fd.unwrap();
    fdopendir = (*(*args.args).fob).fopendir.unwrap();
    readdir   = (*(*args.args).fob).readdir.unwrap();
  }

  loop {
    let ( filename, prefix, fd );
    match dir_out.recv() {
      Ok((s,p,i)) => { (filename, prefix, fd) = (s, p, i); }
      Err(_) => { debug!("iterate_dir_worker: !dir_out, worker end."); return; }
    }

    // debug!("iterate_dir_worker: open {filename}, fd={fd}");
    let dir = unsafe { fdopendir( fd ) };

    loop {
      let fi = unsafe { readdir( dir ) };
      if fi == std::ptr::null_mut() {
        break;
      }

      let path;
      unsafe {
        let c_str = CStr::from_ptr((*fi).d_name.as_ptr());
        let s = c_str.to_str().unwrap().to_string();

        if (s == ".") || (s == "..") {
          continue;
        }
        path = format!("{filename}/{s}");
        if ((*fi).d_type as u32 == DT_REG) || ((*fi).d_type as u32 == DT_DIR) {
          debug!("iterate_dir_worker: added {path}");
          _ = files_in.send((path, prefix.clone()));
          continue;
        }
      }

      debug!("iterate_dir_worker: ignoring {path}");
    }

    let _ = GLOBAL_FILEOPEN_TAIL.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
    debug!("iterate_dir_worker: Finished traversing {filename}, close fd={fd}");
    unsafe{ close(fd) };
  }
}

fn iterate_file_worker(
  files_out: crossbeam_channel::Receiver<(String, String)>,
  dir_in:    crossbeam_channel::Sender<(String, String, i32)>,
  msg_in:    crossbeam_channel::Sender<(String, u64, stat)>,
  args:      dtn_args_wrapper) {

  let mode:i32 = 0;
  let (mut direct_mode, mut recursive) = (true, false);
  let open;


  unsafe {
    if (*args.args).nodirect  { direct_mode = false; }
    if (*args.args).recursive { recursive   = true;  }
    open = (*(*args.args).fob).open.unwrap();
  }

  let mut fd;
  let (mut filename, mut prefix, mut c_str);

  loop {


    match files_out.recv_timeout(std::time::Duration::from_millis(10)) {
      Ok((value, p)) => { (filename, prefix) = (value, p); }
      Err(crossbeam_channel::RecvTimeoutError::Timeout) => {

        // We could conceivably have something happen where one worker could be
        // very slow, nothing is in the queue, and thus a worker will prematurely
        // exit. At the worst case, there would be one file worker left, which
        // is not optimal, but OK.

        if GLOBAL_FILEOPEN_TAIL.load(std::sync::atomic::Ordering::SeqCst) ==
           GLOBAL_FILEOPEN_CLEANUP.load(std::sync::atomic::Ordering::SeqCst) {
          debug!("iterate_file_worker: break because tail==head");
          break;
        }
        continue;
      }
      Err(_) => { debug!("iterate_file_worker: !files_out, worker end."); return; }
    }

    c_str = CString::new(filename).unwrap();

    unsafe {
      let mut st: stat = std::mem::zeroed();

      if direct_mode {
        fd = open( c_str.as_ptr() as *const i8, (*args.args).flags | libc::O_DIRECT, mode );
        if (fd == -1) && (*libc::__errno_location() == 22) {
          direct_mode = false;
          fd = open( c_str.as_ptr() as *const i8, (*args.args).flags, mode );
        }
      } else {
        fd = open( c_str.as_ptr() as *const i8, (*args.args).flags, mode );
      }

      if fd == -1 {
        error!("trying to open {:?} {:?}", c_str,
                 std::io::Error::last_os_error() );
        eprintln!("trying to open {:?} {:?}", c_str,
                   std::io::Error::last_os_error() );
        continue;
      }

      let res = ((*(*args.args).fob).fstat.unwrap())( fd, &mut st as * mut _ );
      if res == -1 {
        error!("trying to stat {:?} {}", c_str,
                  std::io::Error::last_os_error().raw_os_error().unwrap() );
        continue;
      }

      let f = c_str.to_str().unwrap();
      match st.st_mode & libc::S_IFMT {

        libc::S_IFDIR => {
          if recursive {
            let _ = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            _ = dir_in.send((c_str.to_str().unwrap().to_string(), prefix, fd));
          } else {
            info!("Ignoring directory {f}");
            eprintln!("\rIgnoring directory {f}");
            _ = ((*(*args.args).fob).close_fd.unwrap())( fd );
          }
          continue;
        }
        libc::S_IFLNK => {
          info!("Ignoring link {f}");
          eprintln!("\rIgnoring link {f}");
          _ = ((*(*args.args).fob).close_fd.unwrap())( fd );
          continue;
        }
        libc::S_IFREG => { /* add */ }
        _             => {
          info!("Ignoring {:#X} {f}", st.st_mode & libc::S_IFMT);
          eprintln!("\rIgnoring {:#X} {f}", st.st_mode & libc::S_IFMT);
          _ = ((*(*args.args).fob).close_fd.unwrap())( fd );
          continue;
        }

      }

      let fino = 1+GLOBAL_FINO.fetch_add(1, std::sync::atomic::Ordering::SeqCst);

      let res;

      if f == prefix.as_str() {
        let prefix  = std::path::Path::new(prefix.as_str());
        let f_path = std::path::Path::new(f);
        let strip=prefix.parent().unwrap();
        res = f_path.strip_prefix(strip).unwrap();
      } else {
        let prefix  = std::path::Path::new(prefix.as_str());
        let f_path = std::path::Path::new(f);
        res = f_path.strip_prefix(prefix).unwrap();
      }

      debug!("got fino={fino}, {f}, {prefix} {:?}", res);

      _ = msg_in.send( (res.to_str().unwrap().to_string(), fino, st) );
      file_addfile( fino, fd, 0, st.st_size );
    }
  }

  debug!("iterate_file_worker: exiting");
}


fn iterate_files ( files: Vec<String>, args: dtn_args_wrapper, mut sin: &std::fs::File, dest_path: String, quiet: bool, mut sout: &std::fs::File ) -> i64 {

  // we use clean_path instead of path.canonicalize() because the engines are
  // responsible for implementing there own view of the file system and we can't
  // just plug rust's canonicalize into the engine's io routines.

  let dest_path = clean_path::clean(dest_path).into_os_string().into_string().unwrap();
  let msg_out;

  {
    let (files_in, files_out) = crossbeam_channel::bounded(15000);
    let (dir_in, dir_out) = crossbeam_channel::bounded(10);

    let msg_in;
    (msg_in, msg_out) = crossbeam_channel::bounded(400);

    let _ = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, std::sync::atomic::Ordering::SeqCst);

    for j in 0..GLOBAL_FILEOPEN_COUNT{
      let nam = format!("file_{}", j as i32);
      // let rx = r.clone();
      let a = args.clone();
      let fo = files_out.clone();
      let di = dir_in.clone();
      let mi = msg_in.clone();
      thread::Builder::new().name(nam).spawn(move ||
        iterate_file_worker(fo, di, mi, a)).unwrap();
    }

    for j in 0..GLOBAL_DIROPEN_COUNT{
      let nam = format!("dir_{}", j as i32);
      // let rx = r.clone();
      let a = args.clone();
      let dir_o = dir_out.clone();
      let fi = files_in.clone();

      thread::Builder::new().name(nam).spawn(move ||
        iterate_dir_worker(dir_o, fi, a)).unwrap();
    }

    for fi in files {
      if fi.len() < 1 { continue; };
      let path = clean_path::clean(fi).into_os_string().into_string().unwrap();
      _ = files_in.send((path.clone(),path));
    }
  }

  let _ = GLOBAL_FILEOPEN_TAIL.fetch_add(1, std::sync::atomic::Ordering::SeqCst);


  let mut bytes_total=0;
  let mut files_total=0;

  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(1);

  let mut do_break = false;
  let mut did_init;
  let mut counter: i64;
  let mut vec;

  let start = std::time::Instant::now();
  let mut interval = std::time::Instant::now();

  loop {
    vec = Vec::new();
    did_init = false;
    counter=0;

    if !quiet && (interval.elapsed().as_secs_f32() > 0.25) {
      interval = std::time::Instant::now();

      let a = unsafe { human_write( files_total, true )};
      let b = unsafe { human_write(
        (files_total as f32/start.elapsed().as_secs_f32()) as u64, true) };

      let tot  = unsafe { CStr::from_ptr(a).to_str().unwrap() };
      let rate = unsafe { CStr::from_ptr(b).to_str().unwrap() };

      let l = format!("\rCalculating ... Files: {tot} Rate: {rate}/s ");
      _ = sout.write(l.as_bytes());
      _ = sout.flush();
    }

    loop {

      if !did_init {
        builder = flatbuffers::FlatBufferBuilder::with_capacity(8192);
        vec = Vec::new();
        did_init = true;
      }

      let (fi, fino, st);

      match msg_out.recv_timeout(std::time::Duration::from_millis(50)) {
        Ok((a,b,c)) => { (fi, fino, st) = (a,b,c); }
        Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
          if counter > 0 {
            // Go ahead and send whatever we have now
            break;
          }
          continue;
        }
        Err(_) => {
          debug!("Got an abnormal from msg_out.recv, assuming EOQ");
          do_break = true; break
        }
      }

      files_total += 1;
      bytes_total += st.st_size;

      let name = Some(builder.create_string(fi.as_str()));
      vec.push(
      file_spec::File::create( &mut builder,
        &file_spec::FileArgs{
              fino: fino,
              name: name,
              sz: st.st_size,
              ..Default::default()
        }));

        counter += 1;

        if counter > 128 {
          break;
        }
    }

    if counter > 0 {
      let root = Some(builder.create_string((dest_path).as_str()));
      let fi   = Some( builder.create_vector( &vec ) );
      let bu = file_spec::ESCP_file_list::create(
        &mut builder, &file_spec::ESCP_file_listArgs{
          root: root,
          files: fi,
          complete: do_break,
          ..Default::default()
        }

      );
      builder.finish( bu, None );

      let buf = builder.finished_data();

      debug!("Sending file meta data for {}/{}, size is {}", counter, files_total, buf.len());
      let mut hdr = to_header( buf.len() as u32, msg_file_spec );
      _ = sin.write( &mut hdr );
      _ = sin.write( buf );
      _ = sin.flush();
    }


    if do_break { break; }

  }



  /*
  if file_list.len() > 0 {
    sendmsg_files( &file_list, sin, &dest_path );
  }
  */

  debug!("file_iterate is finished");

  return bytes_total;

}

fn fileopen( queue: std::sync::mpsc::Receiver<String>, args: dtn_args_wrapper ) {
  // println!("Start fileopen thread!");
  // unsafe { println!("{}", (*args.args).do_server ) };

  // - Files opened in rust using threads and mpsc queue
  let mut i:u64 = 0;

  loop {
    let fi = queue.recv().unwrap();
    let mut mode:i32 = 0;

    unsafe {
      if ((*args.args).flags & libc::O_WRONLY) == libc::O_WRONLY {
        mode = 0o644;
      }
    }



    if fi == "".to_string() {
      let val = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
      if val == ((GLOBAL_FILEOPEN_COUNT as u64)-1) {
        unsafe { file_addfile( !(0 as u64), 0, 0, 0 ); }
      }
      return;
    }

    debug!( "RUST fileopen with value: '{}'", fi );

    let fd;
    unsafe {
      let c_str = CString::new(fi).unwrap();
      // let fd = (*(*(*args.args).fob).open)( fi, 0 );
      fd = ((*(*args.args).fob).open.unwrap())( c_str.as_ptr() as *const i8, (*args.args).flags, mode );

      // println!( "RUST Thread opened FD: {}", fd );

      if fd == -1 {
        info!("RUST Got an error trying to open {:?} {}", c_str,
                 std::io::Error::last_os_error().raw_os_error().unwrap() );
        continue;
      }

      let mut st: stat = std::mem::zeroed();

      let res = ((*(*args.args).fob).fstat.unwrap())( fd, &mut st as *mut stat);
      if res == -1 {
        info!("RUST Got an error trying to stat {:?} {}", c_str,
                  std::io::Error::last_os_error().raw_os_error().unwrap() );
        continue;
      }

      i += 1;
      file_addfile( i, fd, 0, st.st_size );

    }
    debug!( "opened fn: {} with fd: {}", i, fd );

  }
}



// Typically used to manually launch DTN transfers or execute disk tests
fn do_dtn( args: *mut dtn_args, flags: DTN_Args) {

  let mut fileopen_chan   = Vec::new();
  let mut fileopen_thread = Vec::new();
  let mut connection_count:usize = 0;
  let safe_args = dtn_args_wrapper{ args: args };

  unsafe impl Send for dtn_args_wrapper {}
  unsafe impl Sync for dtn_args_wrapper {}

  initialize_logging( "/tmp/dtn.log.", safe_args );

  // Decode host arguments

  for host in flags.connect {

    let mut port = host.split('/');
    let mut h = "";
    let mut p = "";

    match port.next() {
      Some(value) => { h=value }
      _ => { }
    }

    match port.next() {
      Some(value) => { p=value }
      _ => { }
    }

    let c_str = CString::new(h.trim()).unwrap();
    let d_str = CString::new(p.trim()).unwrap();

    if connection_count > THREAD_COUNT as usize {
      println!("BADNESS > 100");
    }


    unsafe {
      (*args).sock_store[connection_count] =  dns_lookup( c_str.as_ptr() as *mut i8, d_str.as_ptr() as *mut i8 );
      connection_count += 1;
      (*args).sock_store_count = connection_count as i32;
    }

    debug!("Add connection target: {}:{} ", c_str.into_string().unwrap(), d_str.into_string().unwrap() );


  }


  unsafe {

    // io_engines must be initialized before any other IO

    if (*args).disable_io >= 0 {
      file_iotest( args as *mut ::std::os::raw::c_void );
      // println!("io_test is finished");
    } else if (*args).do_server {
      rx_start (args);
    } else {
      tx_start (args);
    }

  }


  // println!( "flags.file {}", flags.file.len());
  if flags.file.len() > 0 {

    // Parse files from command line

    for j in 0..GLOBAL_FILEOPEN_COUNT{
      let (tx, rx): (mpsc::Sender<String>, mpsc::Receiver<String>) = mpsc::channel();

      fileopen_chan.push( tx  );

      let nam = "fo_".to_owned() +   (j as i32).to_string().as_str();

      fileopen_thread.push(
        thread::Builder::new().name(nam.to_string()).spawn(move ||
          fileopen ( rx, safe_args ) )
      );
    }

    let mut fileopen_id=0;

    for fi in flags.file {
      fileopen_chan[fileopen_id].send(fi.to_string()).unwrap();
      fileopen_id = (fileopen_id + 1) % GLOBAL_FILEOPEN_COUNT;
    }

    for i in 0..GLOBAL_FILEOPEN_COUNT{
      // Send FoD (File of Doom-- Ends transfer session)
      fileopen_chan[i].send("".to_string()).unwrap();
    }

  }

  unsafe {
    if (*args).disable_io >= 0 {
      file_iotest_finish();
    } else {
      finish_transfer( args as *mut dtn_args, 0 );
    }
  }

  // File errors ?

  debug!("Normal exit is called");
  process::exit(0);


}

#[derive(Parser, Debug)]
#[command(  author, version, about, long_about = None )]
struct DTN_Args {
   /// Source Files/Path
   #[arg(required=false)]
   file: Vec<String>,

   #[arg(short='c', long="connect",
     help="connect/bind to HOST/PORT",
     default_values_t=[String::from("::1/50000"),].to_vec(), required=false)]
   connect: Vec<String>,

   #[arg(short='X', long="io_only", default_value_t = String::from("") )]
   io_only: String,

   #[arg(short='s', long="server_mode" )]
   server_mode: bool,

   #[arg(short='P', long="parallel", default_value_t = 4 )]
   threads: u64,

   #[arg(short='b', long="block_size", default_value_t = String::from("1M"))]
   block_sz: String,

   #[arg(short='q', long="queue_depth", default_value_t = 1 )]
   QD: u32,

   #[arg(short='e', long="io_engine", default_value_t = String::from("posix"),
         help="posix,dummy")]
   io_engine: String,

   #[arg(short='w', long="window", default_value_t = String::from("128M"))]
   window: String,

   #[arg(short='v', long="verbose" )]
   verbose: bool,

   #[arg(short='L', long, help="Display License")]
   license: bool,
}


#[derive(Parser, Debug)]
#[command(  author, long_version=build::CLAP_LONG_VERSION, about, long_about = None )]
struct EScp_Args {
   /// Source Files/Path
   #[arg(required=true)]
   source: Vec<String>,

   /// Destination host:<path/file>
   #[arg(required=true, default_value_t=String::from(""))]
   destination: String,

   /// Port
   #[arg(short='P', long="port", default_value_t = 22)]
   ssh_port: u16,

   /// ESCP Port
   #[arg(long="escp_port", default_value_t = 1232)]
   escp_port: u16,

   /// Verbose/Debug output
   #[arg(short, long, num_args=0)]
   verbose: bool,

   /// Quiet
   #[arg(short, long, num_args=0)]
   quiet: bool,

   /// SSH Agent Forwarding
   #[arg(short='A', long="agent")]
   agent: bool,

   /// Pass <CIPHER> Cipher to SSH
   #[arg(short, long, default_value_t=String::from(""))]
   cipher: String,

   /// Use <IDENTITY> Key for SSH authentication
   #[arg(short='i', long, default_value_t=String::from(""))]
   identity: String,

   /// Limit transfer to <LIMIT> Kbit/s ( Todo )
   #[arg(short, long, default_value_t=0)]
   limit: u64,

   /// Pass <OPTION> SSH option to SSH
   #[arg(short, long, default_value_t=String::from(""))]
   option: String,

   /// Preserve source attributes at destination ( Todo )
   #[arg(short, long, num_args=0)]
   preserve: bool,

   /// Copy recursively
   #[arg(short='r', long, num_args=0)]
   recursive: bool,

   /// SSH binary for connecting to remote host
   #[arg(short='S', long="ssh", default_value_t=String::from("ssh"))]
   ssh: String,

   /// EScp binary
   #[arg(short='E', long="escp", default_value_t=String::from("escp"))]
   escp: String,

   #[arg(long="blocksize", default_value_t = String::from("1M"))]
   block_sz: String,

   #[arg(long="ioengine", default_value_t = String::from("posix"),
         help="posix,dummy")]
   io_engine: String,

   /// # of EScp parallel threads
   #[arg( long="parallel", default_value_t = 4 )]
   threads: u32,

   #[arg(long, help="mgmt UDS/IPC connection", default_value_t=String::from(""))]
   mgmt: String,

   #[arg(long, help="Display speed in bits/s")]
   bits: bool,

   #[arg(long, help="Don't enable direct mode")]
   nodirect: bool,

   #[arg(short='L', long, help="Display License")]
   license: bool,

   /// Force Server Mode
   #[arg(long, hide=true )]
   server: bool,

   /// Everything below here ignored; added for compatibility with SCP
   #[arg(short, hide=true)]
   s: bool,

   #[arg(short='D', long="sftp", hide=true, default_value_t=String::from("escp"))]
   D: String,

   #[arg(short='T', hide=true)]
   strict_filename: bool,

   #[arg(short='O', hide=true)]
   O: bool,

   #[arg(short='4', hide=true)]
   ipv4: bool,

   #[arg(short='6', hide=true)]
   ipv6: bool,

   #[arg(short='R', hide=true)]
   ssh_from_origin: bool,

}

/* ToDo:
 *  - -l <limit> Limit Bandwidth to <limit> Kbit/s
 *  - -o <SSH_Option> Passed through
 *  - -p
 *
 *  - 3
 *  - "-F" ssh_config
 *  - "-J" Like -3 ?
 *
 */


fn load_yaml(file_str: &str) -> HashMap<String, String> {
    let file_raw = std::fs::File::open(file_str);
    let mut file;
    let mut map = HashMap::new();

    // If the configuration file doesn't exist, we don't care
    match file_raw {
      Ok(value)  => { file = value; }
      Err(_) => { return map; }
    }

    let mut contents = String::new();

    // If the configuration exists but contains bad data, then error
    file.read_to_string(&mut contents)
        .expect("Unable to read file");

    let docs = yaml_rust::YamlLoader::load_from_str(
      &contents).expect("Error parsing YAML File");
    let doc = &docs[0];

    for i in config_items {
      let res;
      match doc[i].as_str() {
        Some(value)  => { res= value; }
        _ => { continue }
      }
      map.insert(i.to_string(), res.to_string());
    }

    return map;
}

fn main() {


  let config = load_yaml("/etc/escp.conf");

  let args: Vec<String> = env::args().collect();
  let path = std::path::Path::new( &args[0] );
  let cmd = path.file_name().expect("COWS!").to_str().unwrap() ;

  let args =
  unsafe {
    args_new()
  };

  if config.contains_key(&"cpumask".to_string()) {
    unsafe{
      (*args).do_affinity = true;

      // let res = u64::from_str_radix(config["cpumask"].as_str(), 16)...
      let res = hex::decode( config["cpumask"].as_str() ).expect("Bad cpumask");
      let mut len = res.len();
      if len > 31 {
        len = 32;
      }

      std::ptr::copy_nonoverlapping(
        res.as_ptr() as *mut i8,
        (*args).cpumask_bytes.as_ptr() as *mut i8, len);
      (*args).cpumask_len = len as i32;
    }
  }

  if config.contains_key(&"nodemask".to_string()) {
    unsafe{
      (*args).nodemask = u64::from_str_radix(
        config["nodemask"].as_str(), 16).expect("Bad nodemask");
    }
  }

  let io_engine_names = HashMap::from( [
    ("posix", 1),
    ("uring", 2),
    ("dummy", 3),
    ("shmem", 4),
  ]);


  if cmd == "dtn" {
    let flags = DTN_Args::parse();
    // println!("Files={:?}", flags.file );

    if flags.license {
      print_license();
      process::exit(0);
    };

    unsafe {
      if flags.verbose { verbose_logging += 1; }

      (*args).thread_count = flags.threads as i32;
      (*args).do_server = flags.server_mode;
      (*args).block = int_from_human(flags.block_sz.clone()) as i32;
      (*args).QD = flags.QD as i32;
      if flags.io_only.len() > 0 {
        (*args).disable_io = int_from_human(flags.io_only.clone()) as i64;
      } else {
        (*args).disable_io = -1;
      }

      if flags.server_mode || ( (*args).disable_io > 42 ) {
        (*args).flags |= libc::O_CREAT | libc::O_WRONLY | libc::O_TRUNC;
      }

      let io_engine = flags.io_engine.to_lowercase();
      (*args).io_engine = io_engine_names.get(&io_engine.as_str()).cloned().unwrap_or(-1);

      if (*args).io_engine  == -1 {
        info!("io_engine='{}' not in compiled io_engines {:?}", io_engine, io_engine_names.keys());
        process::exit(0);
      }

      (*args).io_engine_name = io_engine.as_ptr() as *mut i8;
      (*args).window = int_from_human(flags.window.clone()) as u32;

      print_args( args );
      do_dtn( args, flags );
    }

  } else {
    let flags = EScp_Args::parse();

    unsafe {

      let io_engine = flags.io_engine.to_lowercase();
      (*args).io_engine = io_engine_names.get(&io_engine.as_str()).cloned().unwrap_or(-1);
      (*args).io_engine_name = io_engine.as_ptr() as *mut i8;

      (*args).block = int_from_human(flags.block_sz.to_string()) as i32;

      if (*args).io_engine  == -1 {
        eprintln!("io_engine='{}' not in compiled io_engines {:?}",
                  io_engine, io_engine_names.keys());
        process::exit(0);
      }


      if flags.verbose   { verbose_logging += 1; }
      if flags.quiet     { verbose_logging = 0; }
      if flags.nodirect  { (*args).nodirect  = true; }
      if flags.recursive { (*args).recursive = true; }
      (*args).window = 512*1024*1024;
      (*args).mtu=8204;
      (*args).thread_count = flags.threads as i32;
      (*args).QD = 4;
      (*args).do_server = flags.server;

      (*args).active_port = flags.escp_port;
      (*args).flags = libc::O_RDONLY;

    }

    if flags.license {
      print_license();
      process::exit(0);
    };

    do_escp( args, flags );
  };

}

