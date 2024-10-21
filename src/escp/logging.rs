use super::*;

/*
use log::LevelFilter;
use log4rs::append::file::FileAppender;
use log4rs::encode::pattern::PatternEncoder;
use log4rs::config::{Appender, Config, Root};
*/

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

static mut logger_fd:i32=0;


struct MyLogger {}

impl log::Log for MyLogger {
  fn enabled( &self, _metadata: &Metadata ) -> bool {
    true
  }

  fn log(&self, record: &Record) {
    // use chrono::prelude::*;
    let now = chrono::Local::now();
    unsafe {
      let m = format!("{} [{}] {}\n", now.format("%y%m%d.%H%M%S.%6f").to_string(), record.level(), record.args());
      let msg = CString::new( m.as_str() ).unwrap();
      let res = libc::write( logger_fd, msg.as_ptr() as *const libc::c_void, m.len() );
      if res < 1 {
					eprintln!("Error writing to log: {} {}",
						std::io::Error::last_os_error(), logger_fd);
      }
    }
  }

  fn flush(&self) {}
}

pub fn initialize_logging( base_path: &str, args: dtn_args_wrapper ) {
    let mut log_level = LevelFilter::Info;
    unsafe {
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
        logger_fd =  libc::open(
          file_name.as_ptr(), libc::O_CREAT | libc::O_WRONLY | libc::O_APPEND, 0o644 );

				if logger_fd < 0 {
					eprintln!("Error opening up logfile: {}",
						std::io::Error::last_os_error());
          return;
				}
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

    _ = thread::Builder::new().name("logr".to_string()).spawn( initialize_clog );
}

fn initialize_clog() {
  let mut ret;
  debug!("Start C logging thread");

  loop {
    ret = unsafe { dtn_log_getnext() };

    if !ret.is_null()  {
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };
      let s = format!( "[C] {} ", c_str.to_str().unwrap_or("Error decoding message").trim() );
      if s.contains("[DBG]") {
        debug!("{}", s)
      } else {
        info!("{}", s)
      }
      continue;
    }

    ret = unsafe { dtn_err_getnext() };

    if !ret.is_null()  {
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };
      let msg = c_str.to_str().unwrap().trim() ;
      error!("[C] {} ", msg );
      eprintln!("ERROR: {}", msg );
      continue;
    }

    thread::sleep(std::time::Duration::from_micros(10));
  }

}
