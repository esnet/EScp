extern crate log4rs;

use log::LevelFilter;
use log4rs::append::file::FileAppender;
use log4rs::encode::pattern::PatternEncoder;
use log4rs::config::{Appender, Config, Root};

fn initialize_logging( base_path: &str, args: dtn_args_wrapper ) {
    let mut log_level = LevelFilter::Info;
    unsafe {
      if verbose_logging != 0 {
        log_level = LevelFilter::Debug;
      }
    }

    let ident;

    if unsafe { (*args.args).do_server } {
      ident = "server";
    } else  {
      ident = "client";
    }

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

    log::info!("Starting {} {}",
      env!("CARGO_PKG_NAME"),
      env!("CARGO_PKG_VERSION") );

    _ = thread::Builder::new().name("logr".to_string()).spawn(move ||
          initialize_clog() );

}


fn flush_logs() {
  thread::sleep(std::time::Duration::from_millis(250));
}

fn initialize_clog() {
  let mut ret;
  debug!("Start C logging thread");

  loop {
    let mut did_work = false;

    unsafe {
      ret = dtn_log_getnext();
    }

    if !ret.is_null()  {
      did_work = true;
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };
      debug!("[C] {} ", c_str.to_str().unwrap().trim() );
    }

    unsafe {
      ret = dtn_err_getnext();
    }

    if !ret.is_null()  {
      did_work = true;
      let c_str: &CStr = unsafe { CStr::from_ptr(ret) };
      let msg = c_str.to_str().unwrap().trim() ;
      error!("[C] {} ", msg );
      eprintln!("ERROR: {}", msg );
    }

    if !did_work {
      thread::sleep(std::time::Duration::from_millis(1));
    }
  }

}
