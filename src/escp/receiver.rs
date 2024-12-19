use super::*;
use std::mem::MaybeUninit;

fn start_receiver( args: logging::dtn_args_wrapper ) {
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

fn initialize_receiver(safe_args: logging::dtn_args_wrapper, flags: &EScp_Args) -> bool {

  let args = safe_args.args;
  let (mut sin, mut sout, file, file2, listener, stream);
  let mut buf = vec![ 0u8; 6 ];
  let mut direct_mode = true;

  if !flags.mgmt.is_empty() {
    _ = fs::remove_file(flags.mgmt.clone());
    listener = UnixListener::bind(flags.mgmt.clone()).unwrap();
    stream = listener.accept().unwrap().0 ;

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
      panic!("Failed to read init message {:?}", error );
    }
  }

  let (sz, t) = from_header( buf.to_vec() );
  let helo;
  let mut port_start = 1232;
  let mut port_end = 10;
  let bind_interface;

  debug!("Got header of type {}", t);
  if t == msg_session_init {

    buf.resize( sz as usize, 0 );
    let res = sin.read_exact( &mut buf);
    match res {
      Ok (_) => {},
      Err (error) => {
        panic!("Bad read from SSH {:?}", error);
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

    if helo.do_compression() {
        unsafe { (*args).compression= 1 };
    }

    if helo.do_sparse() {
        unsafe { (*args).sparse = 1 };
    }

    if helo.do_preserve() {
        unsafe { (*args).do_preserve= true };
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
        (*args).io_engine_name = "RCVER".as_ptr() as *mut i8; // five letter
                                                              // engine name
      }
    }

    unsafe {
      (*args).block = helo.block_sz();
      (*args).thread_count = helo.thread_count();
      (*args).do_hash = helo.do_hash();
    }

    if helo.no_direct() { direct_mode = false; }

    bind_interface = CString::new( helo.bind_interface().unwrap_or("") ).unwrap();

    logging::initialize_logging( helo.log_file().unwrap_or(""), safe_args);

     debug!("Session init {:016X?} {}", helo.session_id(), helo.thread_count());
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
        move || start_receiver( safe_args ));

  let port;
  unsafe {
    port = file_get_activeport( args as *mut ::std::os::raw::c_void );
  }

  if port > (port+port_end) {
    eprintln!("Couldn't assign a port between {} and {}. Got {}", port_start, port_start+port_end, port);
    error!("Couldn't assign a port between {} and {}. Got {}", port_start, port_start+port_end, port);
    process::exit(-1);
  }

  debug!("Receiver spawned on port {}", port);

  let builder = sess_init!( {
        version_major: env!("CARGO_PKG_VERSION_MAJOR").parse::<i32>().unwrap(),
        version_minor: env!("CARGO_PKG_VERSION_MINOR").parse::<i32>().unwrap(),
        port_start: port,
        ..Default::default()
    }
  );
  let buf = builder.finished_data();

  let hdr = to_header( buf.len() as u32, msg_session_init );
  _ = sout.write( &hdr );
  _ = sout.write( buf );
  _ = sout.flush();


  unsafe {
    dtn_waituntilready(  args as *mut ::std::os::raw::c_void );
  }
  debug!("Finished Session Init bytes={:?}", buf.len() );

  direct_mode

}

struct FileInformation {
  path: String,
  fino: u64,
  sz: i64,
  mode: u32,
  uid: u32,
  gid: u32,
  atim_sec: i64,
  atim_nano: i64,
  mtim_sec: i64,
  mtim_nano: i64
}

fn add_file( mut files_add: VecDeque<FileInformation>, do_preserve: bool,
             safe_args: logging::dtn_args_wrapper, direct_mode: &mut bool) -> u64 {
  let (mut filecount, mut last_filecount) = (0,0);
  let args = safe_args.args;

  loop {
    let i = match files_add.front() {
      Some(a) => { a },
      None => { break filecount }
    };

    unsafe{

      let open  = (*(*args).fob).open.unwrap();
      let close = (*(*args).fob).close_fd.unwrap();
      let mut fd;

      let fp = CString::new( i.path.clone() ).unwrap();

      for _ in 1..4 {
        if *direct_mode {
          fd = open( fp.as_ptr(), (*args).flags | libc::O_DIRECT, 0o644 );
          if (fd == -1) && (*libc::__errno_location() == 22) {
            info!("Couldn't open '{}' using O_DIRECT; disabling direct mode", i.path);
            *direct_mode = false;
            continue;
          }
        } else {
          fd = open( fp.as_ptr(), (*args).flags, 0o644 );
        }

        if fd < 1 {
          let err = io::Error::last_os_error();
          if err.kind() == std::io::ErrorKind::NotFound {

            let path = std::path::Path::new(i.path.as_str());
            let dir_path = path.parent().unwrap();
            let _ = fs::create_dir_all(dir_path);

            info!("Create directory {dir_path:?}");
            continue;
          }
          info!("Got an error opening file {:?} {:?}",
                i.path, err);
          return filecount;
        }

        if do_preserve {
          let preserve = (*(*args).fob).preserve.unwrap();

          let res = preserve( fd, i.mode, i.uid, i.gid,
            i.atim_sec, i.atim_nano, i.mtim_sec, i.mtim_nano );
          debug!("Preserve: fn={} mode={} uid={} gid={} atim_s={} atim_ns={}",
            i.fino, i.mode, i.uid, i.gid,
            i.atim_sec, i.atim_nano );
          if !res.is_null() {
            let err = CStr::from_ptr(
              libc::strerror( *libc::__errno_location())).to_str().unwrap()
              ;
            info!("Preserve error on {}; {err} {:?}", i.path, res);
          }
        }

        debug!("Add file {}:{fino} with sz={} fd={fd}",
               i.path, i.sz, fino=i.fino );

        let res = file_addfile( i.fino, fd, i.sz,
                    i.atim_sec, i.atim_nano, i.mtim_sec, i.mtim_nano );

        if res.is_null() {
          let tf = get_threads_finished();
          if tf > 0 {
            info!("Transfer Aborted? Exiting because receivers exited");
            return filecount;
          }
          dbg!("Couldn't add file (fd limit). Will try again.");
          break;
        }
        filecount += 1;
        _ = files_add.pop_front();

        break;
      }
    }

    if filecount != last_filecount {
      last_filecount = filecount;
    } else {
      break filecount;
    }
  }
}


pub fn escp_receiver(safe_args: logging::dtn_args_wrapper, flags: &EScp_Args) {

  let args = safe_args.args;
  let mut filecount=0;
  let mut last_send = std::time::Instant::now();

  let mut files_add   = VecDeque::new();
  //  let mut files_close = VecDeque::new();

  let mut direct_mode = initialize_receiver( safe_args, flags );


  loop {

    // We loop here to handle both RX & TX of transfer meta data until
    // our transfer is marked complete.

    // let mut v = Vec::new();

    /*
    if ptr.is_null() {
      // If file completion queue has data for us;
      //   - send completion notice to sender

      let mut fct = 5; // Our timeout here is intentionally short as long
                       // timeouts interfere with our ability to open files
      let loop_start = std::time::Instant::now();
      let mut did_init= false;
      let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(16384);

      loop {
        let (mut file_no, mut crc) = (0,0);
        let mut finish_fc = false;

        match fc_out.recv_timeout(std::time::Duration::from_millis(fct)) {
          Ok((a,b)) => {
            (file_no,crc) = (a,b);
            debug!("fc: fc_pop returned {} {:#X}", file_no, crc);
          }
          Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
            finish_fc = true;
          }
          Err(_) => { error!("receive_main: error receiving file completion notifications"); return; }
        }

        fct = 1;

        if !did_init && !finish_fc {
          debug!("fc: Setting did_init=true because data was received");
          did_init = true;
        }

        if did_init && !finish_fc {
          debug!("fc: pack {}", file_no);
          v.push(
            file_spec::File::create( &mut builder,
              &file_spec::FileArgs{
                fino:     file_no,
                crc,
                ..Default::default()
              }));

          if loop_start.elapsed().as_secs_f32() > ((fct as f32)/1001.0) {
            debug!("fc: setting finish_fc because loop timeout is exceeded");
            finish_fc = true;
          }
        }

        if last_send.elapsed().as_secs_f32() > 2.0 {
          let hdr = to_header( 0, msg_keepalive );
          debug!("fc: Sending heartbeat");
          unsafe {
            meta_send( std::ptr::null_mut::<i8>(), hdr.as_ptr() as *mut i8, 0 );
          }
          last_send = std::time::Instant::now();
        }

        if did_init && finish_fc {
          let fi   = Some( builder.create_vector( &v ) );
          let bu = file_spec::ESCP_file_list::create(
            &mut builder, &file_spec::ESCP_file_listArgs{
              files: fi,
              fc_stat: true,
              ..Default::default()
            });
          builder.finish( bu, None );
          let buf = builder.finished_data();

          let dst:[MaybeUninit<u8>; 49152] = [{ std::mem::MaybeUninit::uninit() }; 49152];
          let mut dst = unsafe { std::mem::transmute::<
            [std::mem::MaybeUninit<u8>; 49152], [u8; 49152]>(dst) };

          let res = zstd_safe::compress( &mut dst, buf, 3 );
          let csz = res.expect("Compression failed");

          let hdr = to_header( csz as u32, msg_file_stat );

          debug!("fc: Sending fc_state data for {} files, size is {}/{csz}",
                 v.len(), buf.len());
          unsafe {
            meta_send( dst.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8,
                       csz as i32 );
          }
          last_send = std::time::Instant::now();
          did_init = false;
        }

        if finish_fc {
          debug!("fc: loop finished, exit loop");
          break;
        }
      }

      continue;
    }
    */

    let ptr = unsafe{ meta_recv() };
    if ptr.is_null() {
      continue;
    }
    debug!("Handle message from sender");

    // Handle message from sender

    let b = unsafe { slice::from_raw_parts(ptr, 6).to_vec() };
    let (sz, mut t) = from_header( b );
    let mut c = unsafe { slice::from_raw_parts(ptr.add(16), sz as usize).to_vec() };

    if (t & msg_compressed) == msg_compressed {
      let dst:[MaybeUninit<u8>; 131072] = [{ std::mem::MaybeUninit::uninit() }; 131072];
      let mut dst = unsafe { std::mem::transmute::
        <[std::mem::MaybeUninit<u8>; 131072], [u8; 131072]>(dst) };

      let res = zstd_safe::decompress(dst.as_mut_slice(), c.as_mut_slice());
      let decompressed_sz = res.expect("decompress failed");
      c = Vec::from(dst);
      c.truncate(decompressed_sz);
      t &= !msg_compressed;
      debug!("Decompressed message {}/{}", sz, decompressed_sz);
    }

    if t == msg_file_spec {

      let fs = flatbuffers::root::<file_spec::ESCP_file_list>(c.as_slice()).unwrap();
      let mut is_filecompletion = false;

      let root = match fs.root() {
        Some(a) => { a },
        None => { is_filecompletion = true; "" },
      };
      debug!("Root set to: {}", root);

      for entry in fs.files().unwrap() {
        if !is_filecompletion {

          let filename = entry.name().unwrap();
          let full_path = if root.is_empty() {
            filename.to_string()
          } else {
            format!("{}/{}", root, filename)
          };

          let fi = FileInformation {
            path: full_path,
            sz: entry.sz(),
            fino: entry.fino(),
            mode: entry.mode(),
            uid: entry.uid(),
            gid: entry.gid(),
            atim_sec: entry.atim_sec(),
            atim_nano: entry.atim_nano(),
            mtim_sec: entry.mtim_sec(),
            mtim_nano: entry.mtim_nano()
          };

          files_add.push_back(fi);


        } else {
          info!("Moo Moo");
        }
      }

      unsafe{ meta_complete() };
      continue;
    }

    if t == msg_session_terminate {
      info!("Got terminate request sz={sz}, type={t}");
      // XXX: Deprecated?
      break;
    }

    if t == 0x430B {
      info!("Got session complete request sz={sz}, type={t}");
      break;
    }

    if (t == 1) && (sz == 0) {
      debug!("Got t=1 sz={sz}");
      break;
    }

    info!("Got unhandled message from sender sz={sz}, type={t}");
    break;
  }

  unsafe {
    debug!("Calling finish transfer");
    finish_transfer( args, filecount );
  }

  debug!("Transfer Complete. Sending Session Finished Message.");

  let hdr = to_header( 0, msg_session_complete );
  unsafe {
    meta_send( std::ptr::null_mut::<i8>(), hdr.as_ptr() as *mut i8, 0_i32 );
  }

  info!("Transfer Complete!");
  thread::sleep(std::time::Duration::from_millis(500)); // Wait: queues to clear

}


