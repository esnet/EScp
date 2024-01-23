fn escp_sender(safe_args: logging::dtn_args_wrapper, flags: EScp_Args) {
  let args = safe_args.args;

  let (host,dest_tmp,dest);
  match flags.destination.rfind(':') {
    Some (a) => { (host, dest_tmp) = flags.destination.split_at(a); },
    _        => {
      eprintln!("Expected ':' in argument '{}'; local copy not implemented",
                flags.destination);
      process::exit(-1);
    }
  }

  (_, dest) = dest_tmp.split_at(1);

  logging::initialize_logging("/tmp/escp.log.", safe_args);
  debug!("Transfer to host: {}, dest_files: {} ", host, dest );

  let (mut sin, mut sout, mut serr, file, proc, stream, fd);

  if flags.mgmt.len() > 0 {
    stream = UnixStream::connect(flags.mgmt)
            .expect("Unable to open mgmt connection");
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

    if flags.ssh_option.len() > 0 {
      ssh_args.extend(["-o", flags.ssh_option.as_str()]);
    }

    if flags.batch_mode {
      ssh_args.extend(["-o", "BatchMode=True"]);
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

  {
    let (session_id, start_port, do_verbose, crypto_key, io_engine, nodirect, thread_count, block_sz, do_hash );

    crypto_key = vec![ 0i8; 16 ];

    unsafe {
      (*args).do_crypto = true;
      tx_init(args as *mut dtn_args);
      (*args).session_id = rand::random::<u64>();
      session_id = (*args).session_id;
      start_port = (*args).active_port;
      io_engine  = (*args).io_engine;
      nodirect  = (*args).nodirect;
      do_hash = (*args).do_hash;
      thread_count = (*args).thread_count;
      block_sz = (*args).block;
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
        do_hash: do_hash,
        thread_count: thread_count,
        block_sz: block_sz,
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
  }

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
      (*(args as *mut dtn_args)).sock_store[0] =  dns_lookup( host_str.as_ptr() as *mut i8 , d_str.as_ptr() as *mut i8);
      (*args).sock_store_count = 1;
      (*args).active_port = helo.port_start() as u16;

      debug!("Starting Sender");
      tx_start (args as *mut dtn_args);
    }
  }

  // For the purpose of metrics, we consider this to be the start of the
  // transfer. At this point we have not read any data from disk but have
  // configured the transfer endpoints.

  let start    = std::time::Instant::now();
  let mut fi;
  let mut fc_hash: HashMap<u64, (u64, u32, u32)> = HashMap::new();
  let mut files_ok=0;

  unsafe {
    fi = std::fs::File::from_raw_fd(1);
    if !flags.quiet {
      _ = fi.write(b"\rCalculating ... ");
      _ = fi.flush();
    }
  }

  let fc_out;
  {
    let fc_in;
    (fc_in, fc_out) = crossbeam_channel::unbounded();
    let nam = format!("fc_0");
    thread::Builder::new().name(nam).spawn(move ||
      fc_worker(fc_in)).unwrap();
  }

  let mut files_total = 0;
  let bytes_total = iterate_files( flags.source, safe_args, dest.to_string(),
                                   flags.quiet, &fi, &mut files_total );
  debug!("Finished iterating files, total bytes={bytes_total}");

  if bytes_total <= 0 {
    eprintln!("Nothing to transfer, exiting.");
    process::exit(1);
  }

  loop {
    if flags.quiet {
      break;
    }

    {
      // Note the delay below; file_check usually delays for interval specified
      file_check(
        &mut fc_hash,
        std::time::Instant::now() + std::time::Duration::from_millis(100),
        &mut files_ok,
        &fc_out
      );
    }

    let bytes_now = unsafe { get_bytes_io( args as *mut dtn_args ) };

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
      let s = format!("\rComplete: {tot_str}B in {files_total} files at {rate_str}{units}/s in {:0.1}s {:38}\n",
        duration.as_secs_f32(), "");
      _ = fi.write(s.as_bytes());
      _ = fi.flush();
      break;
    }
  }

  unsafe { fc_push( 0, 0, 0 ); }

  loop {
    let files_now = unsafe { tx_getclosed() };

    if files_ok as i64 >= files_now {
      debug!("Exiting because {files_ok} >= {files_now}");
      break;
    }

    let res = file_check(
      &mut fc_hash,
      std::time::Instant::now() + std::time::Duration::from_millis(200),
      &mut files_ok,
      &fc_out
    );

    if res==0 {
      debug!("Exiting because file_check returned EOQ");
      break;
    }
  }

  // Finished sending data
  {
    // Let receiver know that we think the session is complete

    let hdr = to_header( 0, msg_session_complete );
    unsafe {
      meta_send( 0 as *mut i8, hdr.as_ptr() as *mut i8, 0 as i32 );
    }
  }

  // Wait for ACK

  while unsafe{ meta_recv() }.is_null() == true {
    thread::sleep(std::time::Duration::from_millis(20));
  }
  unsafe { meta_complete(); }

  debug!("Finished transfer");
}

fn file_check(
    hm: &mut HashMap<u64, (u64, u32, u32)>,
    run_until: std::time::Instant,
    files_ok: &mut u64,
    fc_out: &crossbeam_channel::Receiver<(u64, u64, u32, u32)> ) -> u64
{

  let ptr = unsafe { meta_recv() };

  while ptr.is_null() != true {

    let b = unsafe { slice::from_raw_parts(ptr, 6).to_vec() };
    let (sz, t) = from_header( b );

    if t != msg_file_stat {
      info!("file_check: Got unexpected type={t}, ignoring");
      break;
    }

    let c = unsafe { slice::from_raw_parts(ptr.add(16), sz as usize).to_vec() };
    let fs = flatbuffers::root::<file_spec::ESCP_file_list>(c.as_slice()).unwrap();

    for e in fs.files().unwrap() {

      let (rx_fino, rx_sz, rx_crc, rx_complete) = (e.fino(), e.sz(), e.crc(), e.complete());
      debug!("file_check on {} {} {:#X} {}", rx_fino, rx_sz, rx_crc, rx_complete);

      if !(*hm).contains_key(&rx_fino) {
        loop {
          // loop until we hm contains key or fc_out returns error
          let (tx_fino, sz, crc, complete);

          match fc_out.recv_timeout(std::time::Duration::from_millis(2)) {
            Ok((a,b,c,d)) => { (tx_fino, sz, crc, complete) = (a, b, c, d); }
            Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
              continue;
            }
            Err(_) => { debug!("file_check: fc_out empty; returning"); return 0; }
          }

          debug!("fc_pop() returned {} {} {:#X} {}", tx_fino, sz, crc, complete);

          (*hm).insert( tx_fino, (sz,crc,complete) );
          if rx_fino == tx_fino {
            break;
          }
        }
      }

      let (sz,crc,complete) = (*hm).get(&rx_fino).unwrap();
      let sz = *sz;
      let crc = *crc;
      let complete = *complete;


      if complete != 4 {
        debug!("Skipping {} {} {:#X} {}; Not complete.", rx_fino, sz, crc, complete);
        continue;
      }

      if sz as i64 != rx_sz {
        info!("sz mismatch on {} {}!={}", rx_fino, sz, rx_sz);
        continue;
      }

      if crc != rx_crc {
        // Should always be able to test CRC because if CRC not enabled
        // entry should be zero
        info!("sz mismatch on {} {:#X}!={:#X}", rx_fino, crc, rx_crc);
      }

      *files_ok += 1;

      debug!("Matched successfully {}", rx_fino);
      _ = (*hm).remove(&rx_fino);
    }

    break;

  }

  if ptr.is_null() != true {
    unsafe{ meta_complete(); }
  }


  let interval = run_until - std::time::Instant::now();
  if interval.as_secs_f32() > 0.0 {
    debug!("file_check: still have {}s left", interval.as_secs_f32());
    thread::sleep(interval);
  } else {
    debug!("file_check: time is over: {}", interval.as_secs_f32());
  }

  return 1;
}

fn iterate_dir_worker(  dir_out:  crossbeam_channel::Receiver<(String, String, i32)>,
                        files_in: crossbeam_channel::Sender<(String, String)>,
                        args:     logging::dtn_args_wrapper ) {

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
  args:      logging::dtn_args_wrapper) {

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


    match files_out.recv_timeout(std::time::Duration::from_millis(100)) {
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

      let res = ((*(*(args.args as *mut dtn_args)).fob).fstat.unwrap())( fd, &mut st as * mut _ );
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

      let fname = res.to_str().unwrap().to_string();
      if st.st_size > 0 {
        let fino = 1+GLOBAL_FINO.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        debug!("addfile:  {fname}:{fino} sz={:?}", st.st_size);

        _ = msg_in.send( (fname, fino, st) );
        file_addfile( fino, fd, 0, st.st_size );
      } else {
        debug!("addfile:  {fname}:NONE sz=NIL");
        _ = msg_in.send( (fname, 0, st) );
      }

    }
  }

  debug!("iterate_file_worker: exiting");
}


fn iterate_files ( files: Vec<String>, args: logging::dtn_args_wrapper, dest_path: String, quiet: bool, mut sout: &std::fs::File, ft: &mut u64 ) -> i64 {

  // we use clean_path instead of path.canonicalize() because the engines are
  // responsible for implementing there own view of the file system and we can't
  // just plug rust's canonicalize into the engine's io routines.

  let dest_path = clean_path::clean(dest_path).into_os_string().into_string().unwrap();
  let msg_out;

  {
    // Spawn helper threads
    let (files_in, files_out) = crossbeam_channel::bounded(15000);
    let (dir_in, dir_out) = crossbeam_channel::bounded(10);

    let msg_in;
    (msg_in, msg_out) = crossbeam_channel::bounded(400);

    _ = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, std::sync::atomic::Ordering::SeqCst);

    for j in 0..GLOBAL_FILEOPEN_COUNT{
      let nam = format!("file_{}", j as i32);
      let a = args.clone();
      let fo = files_out.clone();
      let di = dir_in.clone();
      let mi = msg_in.clone();
      thread::Builder::new().name(nam).spawn(move ||
        iterate_file_worker(fo, di, mi, a)).unwrap();
    }

    for j in 0..GLOBAL_DIROPEN_COUNT{
      let nam = format!("dir_{}", j as i32);
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


  let mut bytes_total=0;
  let mut files_total=0;
  let mut files_sent=0;

  let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(1);

  let mut do_break = false;
  let mut did_init = false;
  let mut counter: i64 = 0;
  let mut counter_max = 8;
  let mut vec = VecDeque::new();

  let start = std::time::Instant::now();

  let _ = GLOBAL_FILEOPEN_TAIL.fetch_add(1, std::sync::atomic::Ordering::SeqCst);

  loop {
    if !quiet {

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
        did_init = true;
      }

      let (fi, fino, st);

      match msg_out.recv_timeout(std::time::Duration::from_millis(2)) {
        Ok((a,b,c)) => { (fi, fino, st) = (a,b,c); }
        Err(crossbeam_channel::RecvTimeoutError::Timeout) => {
          if counter > 0 {
            // Go ahead and send whatever we have now
            break;
          }
          continue;
        }
        Err(_) => {
          debug!("iterate_files: Got an abnormal from msg_out.recv, assume EOQ");
          do_break = true;
          break;
        }
      }

      files_total += 1;
      bytes_total += st.st_size;

      vec.push_back( (fino, fi, st.st_size ) );

      counter += 1;
      if counter > counter_max {
        counter_max = 100;
        break;
      }
    }

    if counter > 0 {
      vec.make_contiguous().sort();

      let mut v = Vec::new();
      let mut iterations = 0;

      for (a,b,c) in &vec {
        if (*a>0) && (*a != (files_sent+1)) {
          break;
        }

        did_init = false;
        iterations+=1;

        if *a>0 {
          files_sent+=1;
        }


        let name = Some(builder.create_string((*b).as_str()));
        v.push(
        file_spec::File::create( &mut builder,
          &file_spec::FileArgs{
                fino: *a,
                name: name,
                sz: *c,
                ..Default::default()
          }));
      }

      if iterations == 0 {
        continue;
      }

      for _ in 0..iterations {
        vec.pop_front();
      }

      let root = Some(builder.create_string((dest_path).as_str()));
      let fi   = Some( builder.create_vector( &v ) );
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
      let hdr = to_header( buf.len() as u32, msg_file_spec );

      debug!("iterate_files: Sending file meta data for {}/{}, size is {}", counter, files_total, buf.len());
      unsafe {
        meta_send( buf.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8, buf.len() as i32 );
      }

      counter -= iterations;

    }

    if do_break {
      debug!("iterate_files: do_break flagged");
      break;
    }
  }

  *ft = files_total;

  debug!("iterate_files: is finished");
  return bytes_total;
}


