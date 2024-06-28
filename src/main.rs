#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

include!("../bindings.rs");
include!("license.rs");

extern crate clap;
extern crate flatbuffers;
extern crate rand;

// use hwloc::Topology;
use clap::Parser;
use std::{env, process, thread, collections::HashMap};
use std::ffi::{CString, CStr};
use regex::Regex;
use subprocess::{Popen, PopenConfig, Redirection};
use std::io;
use std::io::Read;
use std::io::Write;
use std::os::unix::net::{UnixStream,UnixListener};
use std::os::fd::AsRawFd;
use std::os::fd::FromRawFd;
use std::fs;
use std::collections::VecDeque;
use std::slice;

use log::{{debug, info, error}};



#[allow(dead_code, unused_imports)]
#[allow(clippy::all)]
mod file_spec;

#[allow(dead_code, unused_imports)]
#[allow(clippy::all)]
mod session_init;

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

mod logging;
include!("receiver.rs");
include!("sender.rs");

const msg_session_init:u16      = 8;
const msg_file_spec:u16         =16;
const msg_file_stat:u16         =17;
const msg_session_complete:u16  = 1;
const msg_session_terminate:u16 = 9;

const config_items: [&str; 2]= [ "cpumask", "nodemask" ];

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
  let mut divisor = 1;

  let idx = match re.find(str.as_str()) {
    Some(value) => { value.start() }
    _ => { str.len() }
  };

  let (number,unit) = str.split_at(idx);
  let val;

  match number.parse::<u64>() {
    Ok(value)  => { val = value; }
    Err(_) => { println!("Unable to parse {}", str ); return 0 }
  }
  let u = &unit.to_lowercase();

  let unit_names = " kmgtpe";
  let mut pow = 0;

  if !u.is_empty() {
    let (x,_y) = u.split_at(1);
    match unit_names.find(x) {
      Some(value) => { pow = value as u32; }
      _           => { pow = 0; }
    }
  }

  let mut multiplier: u64 = 1024;
  if u.contains("bit") {
    multiplier = 1000;
    divisor = 8;
  }

  val * multiplier.pow(pow) / divisor
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


fn do_escp(args: *mut dtn_args, flags: &EScp_Args) {
  let safe_args = logging::dtn_args_wrapper{ args: args as *mut logging::dtn_args };
  unsafe impl Send for logging::dtn_args_wrapper {}
  unsafe impl Sync for logging::dtn_args_wrapper {}

  if unsafe { (*args).do_server } {
    escp_receiver( safe_args, flags );
  } else {
    escp_sender( safe_args, flags );
  }
}

fn fc_worker(fc_in: crossbeam_channel::Sender<(u64, u64, u32, u32)>) {

  // Once a file is complete, it gets put into the fc ring buffer (libdtn)
  //
  // This function attempts to pop an object from fc ring using fc_pop (libdtn)
  // and, if successful, it stuffs that object into fc_in where it is read
  // by rust main loop and sent to the sender which can use that information
  // to confirm that a file was written successfully.

  loop {
    unsafe {
      let fc = fc_pop();

      if fc.is_null() {
        continue;
      }
      debug!("fc_worker: fn={} bytes={} crc={:#X} complete={}",
             (*fc).file_no, (*fc).bytes, (*fc).crc, (*fc).completion);
      if (*fc).file_no == 0 {
        debug!("fc_worker: returning because file_no == 0");
        return;
      }

      _ = fc_in.send(((*fc).file_no, (*fc).bytes, (*fc).crc, (*fc).completion));
    }
  }
}


#[derive(Parser, Debug)]
#[command(  author, long_version=logging::build::CLAP_LONG_VERSION, about, long_about = None )]
struct EScp_Args {
   /// [ Destination ]
   #[arg()]
   source: Vec<String>,

   /// Destination host:<path/file>
   #[arg(hide=true, long="dest", default_value_t=String::from(""))]
   destination: String,

   /// SSH Port
   #[arg(short='P', long="port", hide_default_value=true, default_value_t = 22)]
   ssh_port: u16,

   /// ESCP Port
   #[arg(long="escp_port", default_value_t = 1232)]
   escp_port: u16,

   /// Verbose/Debug output
   #[arg(short, long, num_args=0)]
   verbose: bool,

   #[arg(short, long, num_args=0)]
   quiet: bool,

   /// Enable SSH Agent Forwarding
   #[arg(short='A', long="agent")]
   agent: bool,

   /// CIPHER used by SSH
   #[arg(short, long, default_value_t=String::from(""))]
   cipher: String,

   /// IDENTITY pubkey for SSH auth
   #[arg(short='i', long, default_value_t=String::from(""))]
   identity: String,

   /// LIMIT/thread (bytes/sec) using SO_MAX_PACING_RATE
   #[arg(short, long, hide_default_value=true, default_value_t = String::from("0"))]
   limit: String,

   /// Preserve source attributes (TODO)
   #[arg(short, long, num_args=0)]
   preserve: bool,

   /// Compression (TODO)
   #[arg(short='C', long)]
   compression: bool,

   /// Copy recursively
   #[arg(short='r', long, num_args=0)]
   recursive: bool,

   /// SSH_OPTION to SSH
   #[arg(short='o', hide_default_value=true, default_value_t=String::from(""))]
   ssh_option: String,

   /// SSH binary
   #[arg(short='S', long="ssh", default_value_t=String::from("ssh"))]
   ssh: String,

   /// EScp binary
   #[arg(short='D', long="escp", default_value_t=String::from("escp"))]
   escp: String,

   #[arg(long="blocksize", default_value_t = String::from("1M"))]
   block_sz: String,

   #[arg(long="ioengine", default_value_t = String::from("posix"),
         help="posix,dummy")]
   io_engine: String,

   /// # of EScp parallel threads
   #[arg(short='t', long="parallel", default_value_t = 4 )]
   threads: u32,

   #[arg(long, hide=true, help="mgmt UDS/IPC connection", default_value_t=String::from(""))]
   mgmt: String,

   #[arg(long, help="Display speed in bits/s")]
   bits: bool,

   #[arg(long, help="Don't enable direct mode")]
   nodirect: bool,

   #[arg(long, help="Don't enable file checksum")]
   nochecksum: bool,

   #[arg(short='L', long, help="Display License")]
   license: bool,

   /// Force Server Mode
   #[arg(long, hide=true )]
   server: bool,

   #[arg(short='O', hide=true )]
   O: bool,

   #[arg(short='B', hide=true )]
   batch_mode: bool,

   /// Everything below here ignored; added for compatibility with SCP
   #[arg(short, hide=true)]
   s: bool,

   #[arg(short='F', long="sftp", hide=true, default_value_t=String::from("escp"))]
   F: String,

   #[arg(short='T', hide=true)]
   strict_filename: bool,

   #[arg(short='4', hide=true)]
   ipv4: bool,

   #[arg(short='6', hide=true)]
   ipv6: bool,

   #[arg(short='R', hide=true)]
   ssh_from_origin: bool,

}

/* ToDo:
 *  - 3
 *  - "-F" ssh_config
 *  - "-J"
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

    let docs = yaml_rust2::YamlLoader::load_from_str(
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

    map
}

fn main() {


  let config = load_yaml("/etc/escp.conf");

  // let args: Vec<String> = env::args().collect();
  // let path = std::path::Path::new( &args[0] );
  // let cmd = path.file_name().expect("COWS!").to_str().unwrap() ;

  let args =
  unsafe {
    args_new()
  };

  if config.contains_key("cpumask") {
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

  if config.contains_key("nodemask") {
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

  /*
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
      // do_dtn( args, flags );
    }

  } else
  */
  {
    let mut flags = EScp_Args::parse();

    let l = flags.source.len();
    flags.destination=flags.source.last().unwrap_or(&String::new()).to_string();
    if l >= 2 {
      flags.source.truncate(l-1);
    }

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
      (*args).nodirect = flags.nodirect;
      (*args).do_hash  = !flags.nochecksum;
      if flags.recursive  { (*args).recursive = true; }

      (*args).pacing = int_from_human(flags.limit.clone());
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

    if l < 2 {
      eprintln!("Error: Not enough arguments\n");

      use clap::CommandFactory;
      let mut cmd = EScp_Args::command();
      let _ = cmd.print_help();

      process::exit(0);
    }

    do_escp( args, &flags );
  };

}

