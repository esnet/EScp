#![allow(non_snake_case, unused_imports, dead_code, non_camel_case_types, non_upper_case_globals)]

include!("../bindings.rs");

extern crate log4rs;

use log::LevelFilter;
use log4rs::append::file::FileAppender;
use log4rs::encode::pattern::PatternEncoder;
use log4rs::config::{Appender, Config, Root};
use log::{{debug, info, error}};

use shadow_rs::shadow;
shadow!(build);

use std::ffi::CStr;
use std::{env, thread};

#[ derive(Clone, Copy) ]
pub struct dtn_args_wrapper {
  pub args: *mut dtn_args
}

pub fn initialize_logging( base_path: &str, args: dtn_args_wrapper ) {
    let mut log_level = LevelFilter::Info;
    unsafe {
      if verbose_logging != 0 {
        log_level = LevelFilter::Debug;
      }
    }


    let ident = if unsafe { (*args.args).do_server } {
      "server"
    } else  {
      "client"
    };

    let logfile = FileAppender::builder()
        .encoder(Box::new(PatternEncoder::new("{d(%Y%m%d.%H%M%S%.6f)} [{l}] {T} {f}:{L} {m}\n")))
        .build(base_path.to_owned() + ident)
        .unwrap();

    let config = Config::builder()
        .appender(Appender::builder().build("logfile", Box::new(logfile)))
        .build(Root::builder()
                   .appender("logfile")
                   .build(log_level))
                   .unwrap();

    log4rs::init_config(config).unwrap();

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
      let s = format!( "[C] {} ", c_str.to_str().unwrap().trim() );
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
