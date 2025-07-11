use super::*;

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

fn read_stdin_or_mgmt( mgmt: String ) -> (Vec<u8>, Option<std::os::unix::net::UnixStream>) {

  let mut ret: Option<std::os::unix::net::UnixStream> = None;
  let mut buf = vec![ 0u8; 6 ];
  let result;

  if !mgmt.is_empty() {
    _ = fs::remove_file(mgmt.clone());
    let listener = UnixListener::bind(mgmt.clone()).unwrap();
    let mut stream = listener.accept().unwrap().0 ;
    result = stream.read_exact( &mut buf );
    ret = Some(stream);
  } else {
    result = std::io::stdin().read_exact( &mut buf );
  }

  match result {
    Ok(_)  => { }
    Err(error) => {
      panic!("Failed to read init message {:?}", error );
    }
  }

  let (sz, t) = from_header( buf.to_vec() );
  if t != msg_session_init {
    panic!("Unexpected session init type={}", t);
  }

  buf.resize( sz as usize, 0 );
  let res;

  if !mgmt.is_empty() {
    let mut stream = ret.unwrap();
    res = stream.read_exact( &mut buf );
    ret = Some(stream);
  } else {
    res = std::io::stdin().read_exact( &mut buf );
  }

  match res {
    Ok (_) => {},
    Err (error) => {
      panic!("Bad read from SSH {:?}", error);
    }
  }

  (buf, ret)
}

fn initialize_receiver(safe_args: logging::dtn_args_wrapper, flags: &EScp_Args) {

  let args = safe_args.args;
  let (buf, stream) = read_stdin_or_mgmt( flags.mgmt.clone() );
  let mut port_start = 1232;
  let mut port_end = 10;

  let helo = flatbuffers::root::<session_init::Session_Init>(buf.as_slice()).unwrap();
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

  unsafe { (*args).do_preserve= helo.do_preserve() };

  if helo.do_crypto() {
    unsafe {
      let ptr: Vec<i8> = helo.crypto_key().unwrap().iter().collect() ;

      std::ptr::copy_nonoverlapping( ptr.as_ptr(),
        (*args).crypto_key.as_ptr() as *mut i8, 16 );
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
    (*args).nodirect = helo.no_direct();
    (*args).ip_mode = helo.ip_mode();
  }

  let bind_interface = CString::new( helo.bind_interface().unwrap_or("") ).unwrap();
  logging::initialize_logging( helo.log_file().unwrap_or(""), safe_args);
  debug!("Session init {:016X?} {}", helo.session_id(), helo.thread_count());

  let p = CString::new( port_start.to_string() ).unwrap();
  let mut connection_count=0;

  unsafe {
    (*args).sock_store[connection_count] =  dns_lookup( args, bind_interface.as_ptr() as *mut i8 , p.as_ptr() as *mut i8);
    connection_count += 1;
    (*args).sock_store_count = connection_count as i32;
    (*args).flags |= libc::O_CREAT|libc::O_WRONLY|libc::O_TRUNC;
    (*args).active_port = 0;
  }

  debug!("Spawning receiver");

  _ = thread::Builder::new().name("rcvr".to_string()).spawn(
        move || start_receiver( safe_args ));

  let port = unsafe { file_get_activeport( args as *mut ::std::os::raw::c_void ) } as i64;

  if port > (port+port_end as i64) {
    eprintln!("Couldn't assign a port between {} and {}. Got {}", port_start, port_start+port_end, port);
    error!("Couldn't assign a port between {} and {}. Got {}", port_start, port_start+port_end, port);
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

  let hdr = to_header( buf.len() as u32, msg_session_init );

  _ = match stream {
    Some(mut a) => {
      _ = a.write( &hdr );
      _ = a.write( buf );
      _ = a.flush();
      Some(1)
    },
    None => {
      _ = std::io::stdout().write( &hdr );
      _ = std::io::stdout().write( buf );
      _ = std::io::stdout().flush();
      None
    }
  };

  unsafe {
    dtn_waituntilready(  args as *mut ::std::os::raw::c_void );
  }
  debug!("Finished Session Init bytes={:?}", buf.len() );

}

#[derive(Clone)]
struct FileInformation {
  path: String,
  fino: u64,
  sz: i64,
  blocks: i64,
  fd: i32,
  mode: u32,
  uid: u32,
  gid: u32,
  atim_sec: i64,
  atim_nano: i64,
  mtim_sec: i64,
  mtim_nano: i64,
  crc: u32
}

impl Default for FileInformation {
  fn default () -> FileInformation {
    FileInformation{
      path: String::new(),
      fino: 0,
      sz: 0,
      blocks: -1,
      fd: 0,
      mode: 0,
      uid: 0,
      gid: 0,
      atim_sec: 0,
      atim_nano: 0,
      mtim_sec: 0,
      mtim_nano: 0,
      crc: 0
    }
  }
}

fn add_file( files_hash: &mut HashMap<u64, FileInformation>, files_add: &mut VecDeque<u64>,
             safe_args: logging::dtn_args_wrapper) -> (u64, i64) {
  // Returns (number_of_files added, file_error)

  let (mut filecount, mut last_filecount) = (0,0);
  let args = safe_args.args;

  loop {
    let i = match files_add.front() {
      Some(a) => { a },
      None => { return (filecount, 0); }
    };

    let i = &mut files_hash.get_mut(i).unwrap();

    unsafe{

      let open  = (*(*args).fob).open.unwrap();
      let mut fd;

      let mut fp;
      let mut alt_fp = None;
      let fp_s = String::from(i.path.clone());

      if fp_s.contains("\0") {
        let v: Vec<&str> = fp_s.split("\0").collect();

        let t = std::path::Path::new(v[0]).join(v[1]);
        fp = CString::new( v[0] ).unwrap();
        debug!("Computed alt_fp as {:?} and fp as {:?}", t, fp);
        alt_fp = Some(String::from(t.as_os_str().to_str().unwrap()));
      } else {
        fp = CString::new( i.path.clone() ).unwrap();
      }

      for _ in 1..5 {

        if !(*args).nodirect {
          fd = open( fp.as_ptr(), (*args).flags | libc::O_DIRECT, 0o644 );
          if (fd == -1) && (*libc::__errno_location() == 22) {
            info!("Couldn't open '{}' using O_DIRECT; disabling direct mode", i.path);
            (*args).nodirect = true;
            continue;
          }
        } else {
          fd = open( fp.as_ptr(), (*args).flags, 0o644 );
        }

        if (fd == -1) && (*libc::__errno_location() == 21) {
          debug!("{:?} is a directory;", fp );
          if alt_fp.is_some() {
            let t = CString::new( alt_fp.clone().unwrap() ).unwrap();
            if t != fp {
              debug!("Trying alt_fp");
              fp = t;
              continue;
            }
          }
        }

        if fd < 1 {
          debug!("In error loop");
          let err = io::Error::last_os_error();
          if err.kind() == std::io::ErrorKind::NotFound {

            let path = std::path::Path::new(i.path.as_str());
            let dir_path = path.parent().unwrap();
            let _ = fs::create_dir_all(dir_path);

            debug!("Create directory {dir_path:?}");
            continue;
          }

          if err.raw_os_error().unwrap() != 24 {
            info!("Got an error opening file {:?} {:?}",
                  i.path, err.raw_os_error().unwrap());
            return (filecount, err.raw_os_error().unwrap() as i64);
          } else {
            debug!("Too many open files; waiting until files are closed {:?} {:?}",
                  i.path, err);
            return (filecount, 0);
          }

        }

        i.fd = fd;
        if (*args).do_preserve {
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

        let res = file_addfile( i.fino, fd );

        if res.is_null() {
          let tf = get_threads_finished();
          if tf > 0 {
            info!("Transfer Aborted? Exiting because receivers exited");
            return (filecount, 0);
          }
          debug!("Couldn't add file (fd limit). Will try again.");
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
      return (filecount, 0);
    }
  }

}

fn close_file( files_hash: &mut HashMap<u64, FileInformation>,
               files_close: &mut VecDeque<u64>,
               safe_args: logging::dtn_args_wrapper
             ) -> Vec<FileInformation> {

  let mut ret = Vec::new();
  let args = safe_args.args;
  let (close,preserve,truncate) = unsafe {
    ( (*(*args).fob).close_fd.unwrap(),
      (*(*args).fob).preserve.unwrap(),
      (*(*args).fob).truncate.unwrap(), )
  };

  /*
  if !files_close.is_empty() {
    println!("Checking list: {:?}", files_close);
  }
  */

  loop {
    unsafe {
      let fino = match files_close.front() {
        Some(a) => {a},
        None => { break }
      };

      let stats = file_getstats( *fino );
      if stats.is_null() {
        break;
      }

      let fi = files_hash.get_mut(fino).unwrap();

      if fi.blocks < 0 {
        // File information not available
        break;
      }

      if ((*stats).block_total as i64) < fi.blocks {
        // File incomplete
        break;
      }


      truncate( fi.fd, fi.sz );
      if (*args).do_preserve {
        _ = preserve( fi.fd, fi.mode, fi.uid, fi.gid,
              fi.atim_sec, fi.atim_nano, fi.mtim_sec, fi.mtim_nano );
      }
      _ = close( fi.fd );

      fi.crc = (*stats).crc;
      ret.push((*fi).clone());

      files_hash.remove(fino);
      _ = files_close.pop_front();

      memset_avx( stats as *mut ::std::os::raw::c_void );
      file_incrementtail();
    }
  }

  ret
}

fn send_file_completions( files_complete: &mut Vec<FileInformation> ) -> bool {

  if files_complete.is_empty() {
    return false;
  }

  let mut k=0;

  debug!("send_file_completions: {} files in queue", files_complete.len());
  let mut v = Vec::new();
  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(8192);

  while let Some(fi) = files_complete.pop() {
    v.push(
      file_spec::File::create( &mut builder,
        &file_spec::FileArgs{
          fino: fi.fino,
          crc:  fi.crc,
          ..Default::default()
        }));
    k+=1;
    if k >= 4096 {
      break;
    }
  }

  let fi   = Some( builder.create_vector( &v ) );
  let bu = file_spec::ESCP_file_list::create(
    &mut builder, &file_spec::ESCP_file_listArgs{
      files: fi,
      fc_stat: true,
      ..Default::default()
    });
  builder.finish( bu, None );
  let buf = builder.finished_data();

  let dst:[MaybeUninit<u8>; 100000] = [{ std::mem::MaybeUninit::uninit() }; 100000];
  let mut dst = unsafe { std::mem::transmute::<
    [std::mem::MaybeUninit<u8>; 100000], [u8; 100000]>(dst) };

  let res = zstd_safe::compress( &mut dst, buf, 3 );
  let csz = res.expect("Compression failed");

  let hdr = to_header( csz as u32, msg_file_stat );

  debug!("send_file_completions: files={} compressed={} uncompressed={}", k, csz, buf.len());

  unsafe {
    meta_send( dst.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8,
               csz as i32 );
  }

  true
}

fn send_file_error( fino: u64, name: String, errmsg: String, errno: i32 ) {

  let mut v = Vec::new();
  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(8192);

  let name  = Some(builder.create_string((name).as_str()));
  let errmsg = Some(builder.create_string((errmsg).as_str()));


  v.push(
     file_spec::File::create( &mut builder,
       &file_spec::FileArgs{
         fino, name,
         errmsg, errno,
         ..Default::default()
       }));

  let fi   = Some( builder.create_vector( &v ) );
  let bu = file_spec::ESCP_file_list::create(
    &mut builder, &file_spec::ESCP_file_listArgs{
      files: fi,
      fc_stat: true,
      ..Default::default()
    });

  builder.finish( bu, None );
  let buf = builder.finished_data();

  let dst:[MaybeUninit<u8>; 1000] = [{ std::mem::MaybeUninit::uninit() }; 1000];
  let mut dst = unsafe { std::mem::transmute::<
    [std::mem::MaybeUninit<u8>; 1000], [u8; 1000]>(dst) };

  let res = zstd_safe::compress( &mut dst, buf, 3 );
  let csz = res.expect("Compression failed");

  let hdr = to_header( csz as u32, msg_file_stat );

  debug!("send_file_error: files={} compressed={} uncompressed={}", 1, csz, buf.len());

  unsafe {
    meta_send( dst.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8,
               csz as i32 );
  }

}

pub fn escp_receiver(safe_args: logging::dtn_args_wrapper, flags: &EScp_Args) {

  let args = safe_args.args;

  let mut files_add   = VecDeque::<u64>::new();
  let mut files_close = VecDeque::new();

  let mut files_hash: HashMap<u64, FileInformation> = HashMap::new();
  let mut files_complete = Vec::new();

  let mut file_count_in=0;
  let mut file_count_out=0;

  let mut transfer_complete = false;
  let mut timeout = 100;

  initialize_receiver( safe_args, flags );

  loop {
    let (filecount, file_error) = add_file( &mut files_hash, &mut files_add, safe_args );
    let mut res = close_file( &mut files_hash, &mut files_close, safe_args );

    file_count_out += res.len();
    if !res.is_empty() {
      debug!("Incr file_count_out by {} to {file_count_out}", res.len() );
    }

    files_complete.append(&mut res);

    file_count_in += filecount;

    if filecount > 0 {
      debug!("Incr file_count_in by {filecount} to {file_count_in}");
    }

    if file_error > 0 {
      let fi  = files_add.pop_front().unwrap();
      let i = &mut files_hash.get_mut(&fi).unwrap();
      let name = i.path.clone();

      send_file_error( fi, name, String::new(), file_error as i32 );
      info!("Error with {}:{}", fi, file_error);
    }

    if transfer_complete && (file_count_in==file_count_out as u64) {
      debug!("Transfer complete conditions met");
      break;
    }

    if (filecount > 0) || !res.is_empty() {
      timeout = 100;
      continue;
    }

    let ptr = unsafe{ meta_recv() };
    if ptr.is_null() {
      if send_file_completions( &mut files_complete ) {
        timeout=100;
        continue;
      }

      unsafe {
        if get_threads_finished() >= (*args).thread_count as u64 {
          info!("Exiting because all workers exited.");
          process::exit(-1);
        }
      }

      timeout = (timeout as f64 * 1.337) as u64;
      if timeout > 145000 {
        let hdr = to_header( 0, msg_keepalive );
        unsafe {
          meta_send( std::ptr::null_mut::<i8>(), hdr.as_ptr() as *mut i8, 0_i32 );
        }

        if timeout > 275000 {
          timeout /= 2;
        }
      }
      // We didn't do anything so go ahead and delay a little bit
      thread::sleep(std::time::Duration::from_micros(timeout)); // Wait: queues to clear
      continue;
    }

    timeout=100;
    debug!("Got message from sender");

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

      if !is_filecompletion {
        debug!("Root set to: {}", root);
      } else {
        debug!("Got file completions");
      }

      for entry in fs.files().unwrap() {
        if !is_filecompletion {
          let filename = entry.name().unwrap();
          let full_path = if root.is_empty() {
            filename.to_string()
          } else {
            format!("{}/{}", root, filename)
          };

          // let fi = files_hash.get_mut( &entry.fino() ).unwrap_or_default();
          let fi_o = files_hash.get_mut( &entry.fino() );

          let mut fi = match fi_o {
            Some(&mut ref a) => { debug!("OOO on {}", entry.fino()); a.clone() },
            None => { FileInformation::default() }
          };

          fi.path = full_path;
          if fi.sz == 0 {
            fi.sz = entry.sz();
          }
          fi.fino = entry.fino();
          fi.mode = entry.mode();
          fi.uid = entry.uid();
          fi.gid = entry.gid();
          fi.atim_sec =  entry.atim_sec();
          fi.atim_nano =  entry.atim_nano();
          fi.mtim_sec =  entry.mtim_sec();
          fi.mtim_nano =  entry.mtim_nano();

          files_hash.insert( entry.fino(), fi );
          files_add.push_back( entry.fino() );
        } else {
          let fi_o = files_hash.get_mut( &entry.fino() );

          let mut fi = match fi_o {
            Some(&mut ref a) => { a.clone() },
            None => { debug!("OOO_fi on {}", entry.fino()); FileInformation::default() }
          };

          fi.blocks = entry.blocks();
          if entry.sz() > 0 {
            fi.sz = entry.sz();
          }

          files_hash.insert( entry.fino(), fi );
          files_close.push_back(entry.fino());
        }
      }

      unsafe{ meta_complete() };
      continue;
    }

    /*
    if t == msg_session_terminate {
      info!("Got terminate request sz={sz}, type={t}");
      // XXX: Deprecated?
      transfer_complete=true;
      continue;
    }
    */

    if t == 0x430B {
      let t = t.swap_bytes();
      debug!("Got session complete (BYE) request sz={sz}, type={t:03X}");
      transfer_complete=true;
      unsafe{ meta_complete() };
      continue;
    }

    if (t == 1) && (sz == 0) {
      debug!("Got t=1 sz={sz}");
      transfer_complete=true;
      unsafe{ meta_complete() };
      continue;
    }

    debug!("Got unhandled message from sender sz={sz}, type={t}");
    unsafe{ meta_complete() };
    continue;
  }

  debug!("Transfer Complete. Sending Session Finished Message.");

  let hdr = to_header( 0, msg_session_complete );
  unsafe {
    meta_send( std::ptr::null_mut::<i8>(), hdr.as_ptr() as *mut i8, 0_i32 );
  }

  info!("Transfer Complete!");

  unsafe {
    finish_transfer( args );
  }

  thread::sleep(std::time::Duration::from_millis(150)); // Wait: queues to clear

}


