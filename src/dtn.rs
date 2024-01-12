fn fileopen( queue: std::sync::mpsc::Receiver<String>, args: dtn_args_wrapper ) {
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
      fd = ((*(*args.args).fob).open.unwrap())( c_str.as_ptr() as *const i8, (*args.args).flags, mode );

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
      // file_iotest( args as *mut ::std::os::raw::c_void );
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
      // file_iotest_finish();
    } else {
      finish_transfer( args as *mut dtn_args, 0 );
    }
  }

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


