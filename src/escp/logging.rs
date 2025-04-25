use super::*;

extern crate log;
extern crate chrono;

use syslog::{Facility, Formatter3164, BasicLogger};
use log::{SetLoggerError, LevelFilter, info, Record, Metadata};

use shadow_rs::shadow;
shadow!(build);

#[ derive(Clone, Copy) ]
pub struct dtn_args_wrapper {
  pub args: *mut dtn_args
}

// static mut logger_fd:i32=0;
static mut dtn_aw:Option<dtn_args_wrapper> = None;

struct MyLogger {}

impl log::Log for MyLogger {
  fn enabled( &self, _metadata: &Metadata ) -> bool {
    true
  }

  fn log(&self, record: &Record) {
    // use chrono::prelude::*;
    let now = chrono::Local::now();
    unsafe {
      let a = dtn_aw.unwrap();
      let logger_fd = (*a.args).logging_fd;
      let m = format!("{} [{:08X}] [{}] {}\n",
          now.format("%y%m%d.%H%M%S.%6f"),
					(*a.args).session_id & 0xFFFFFFFF,
					record.level(), record.args());
      let msg = CString::new( m.as_str() );
      if msg.is_ok() {
				let res = libc::write( logger_fd, msg.unwrap().as_ptr() as *const libc::c_void, m.len() );
				if res < 1 {
						eprintln!("Error writing to log: {} {}",
							std::io::Error::last_os_error(), logger_fd);
				}
      }
    }
  }

  fn flush(&self) {}
}

pub fn initialize_logging( base_path: &str, args: dtn_args_wrapper ) {
    let mut log_level = LevelFilter::Info;

    unsafe {
      dtn_aw = Some( args );
      if verbose_logging != 0 {
        log_level = LevelFilter::Debug;
      }
    }

    if base_path.is_empty() { return; }

    let ident = if unsafe { (*args.args).do_server } {
      ".server"
    } else  {
      ".client"
    };

    let re = Regex::new(r"^LOCAL[0-7]$").unwrap();

    let idx = match re.find(base_path) {
      Some(value) => { value.as_str() }
      _ => {""}
    };

    let mut slog = -1;
    if !idx.is_empty() {
      let number = idx.split_at(5).1;
      slog = number.parse::<i64>().unwrap();
    }

    let formatter;
    if slog>=0 {
			let ar = [ Facility::LOG_LOCAL0,
								 Facility::LOG_LOCAL1,
								 Facility::LOG_LOCAL2,
								 Facility::LOG_LOCAL3,
								 Facility::LOG_LOCAL4,
								 Facility::LOG_LOCAL5,
								 Facility::LOG_LOCAL6,
								 Facility::LOG_LOCAL7,
							 ];

			formatter = Formatter3164 {
					facility: ar[slog as usize],
					hostname: None,
					process: "escp".to_owned()+ ident,
					pid: process::id(),
			};

			let a = match syslog::unix(formatter) {
					Err(e) => { eprintln!("impossible to connect to syslog: {:?}", e); return; },
					Ok(logger) => logger,
			};
      let _ = log::set_boxed_logger(Box::new(BasicLogger::new(a) ))
            .map(|()| log::set_max_level(log_level));
    } else {
      let file_name = CString::new( base_path.to_owned() + ident ).unwrap();

      unsafe {
        let logger_fd =  libc::open(
          file_name.as_ptr(), libc::O_CREAT | libc::O_WRONLY | libc::O_APPEND, 0o644 );

				if logger_fd < 0 {
					eprintln!("Error opening up logfile: {}",
						std::io::Error::last_os_error());
          return;
				}

        (*args.args).logging_fd = logger_fd;
      }

      static MY_LOGGER: MyLogger = MyLogger{};
      let _ = log::set_boxed_logger(Box::new(&MY_LOGGER))
            .map(|()| log::set_max_level(log_level));
    }

    log::info!("Starting {} {}. GIT {} {}",
      env!("CARGO_PKG_NAME"),
      env!("CARGO_PKG_VERSION"),
      build::SHORT_COMMIT,
      build::BUILD_TIME,
    );

    _ = thread::Builder::new().name("logr".to_string()).spawn( move ||
          initialize_clog (args) );
}

fn parse_error( errmsg: &str ) {

  let re_err = Regex::new(r"\[en: [(0-9)]+").unwrap();
  let re_fn = Regex::new(r"\[fn: [(0-9)]+").unwrap();
  let mut end=0;

  let errno = match re_err.find(errmsg) {
    Some(res) => {
      end = std::cmp::max( end, res.end() );
      let s = res.as_str().to_string();
      let (_, sy) = s.split_at(5);
      sy.parse::<i32>().unwrap()
    }
    None => { 0 }
  };

  let fino = match re_fn.find(errmsg) {

     Some(res) => {
      end = std::cmp::max( end, res.end() );
      let s = res.as_str().to_string();
      let (_, sy) = s.split_at(5);
      sy.parse::<i32>().unwrap()
    }
    None => { 0 }
  };

  if end > 0 {
    end += 2;
  }

  let (_, errmsg) = errmsg.split_at(end);

  let mut v = Vec::new();
  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(1024);
  let errmsg = Some( builder.create_string(errmsg) );
  let mut en = Vec::new();
  en.push(fino as i64);
  en.push(errno as i64);
  let code = Some(builder.create_vector( &en ));

  v.push( message::Message::create (&mut builder,
    &message::MessageArgs {
     is_error: true,
     message: errmsg,
     code,
     ..Default::default()
    }));

  let msg= Some(builder.create_vector( &v ));
  let bu = message::Message_list::create(
    &mut builder, &message::Message_listArgs{
      messages: msg,
      ..Default::default()
    }
  );

  builder.finish( bu, None );
  let buf = builder.finished_data();

  let dst:[MaybeUninit<u8>; 8192] = [{ std::mem::MaybeUninit::uninit() }; 8192 ];
  let mut dst = unsafe { std::mem::transmute::
    <[std::mem::MaybeUninit<u8>; 8192], [u8; 8192]>(dst) };

  let res = zstd_safe::compress( &mut dst, buf, 3 );

  let csz = res.expect("Compression failed");
  let hdr = to_header( csz as u32, msg_message|msg_compressed );

  unsafe {
    meta_send( dst.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8,
         csz as i32 );
  }
}

fn initialize_clog( args: dtn_args_wrapper ) {
  let mut ret;
  debug!("Start C logging thread");

  let mut delay = 10.0;

  loop {
    ret = unsafe { dtn_log_getnext() };

    if !ret.is_null()  {
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };
      let d = c_str.to_str().unwrap_or("Error decoding message").trim();
      let (a,b) = d.split_at(6);
      let s = format!( "[C] {} ", b);
      if a.contains("[DBG]") {
        debug!("{}", s)
      } else {
        info!(" {}", s)
      }
      delay=10.0;
      continue;
    }

    ret = unsafe { dtn_err_getnext() };

    if !ret.is_null()  {
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };

      let d = c_str.to_str().unwrap_or("Error decoding message").trim();
      let (_a,b) = d.split_at(6);
      let s = format!( "[C] {} ", b);

      let do_server = unsafe { (*args.args).do_server };
      if do_server {
        parse_error(b);
      }

      error!("{}", s);
      eprintln!("ERROR: {}", s);
      delay = 10.0;
      continue;
    }

    thread::sleep(std::time::Duration::from_micros(delay as u64));
    delay *= 1.293;
    if delay > 15000.0 {
      delay /= 2.0 ;
    }
  }
}
