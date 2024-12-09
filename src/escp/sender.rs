use super::*;
use std::mem::MaybeUninit;
use std::sync::atomic::AtomicU64;
use std::path::PathBuf;
use std::sync::atomic::Ordering::SeqCst;

static GLOBAL_FILEOPEN_COUNT: usize = 4; // # threads to use for opening files
static GLOBAL_DIROPEN_COUNT: usize = 2;  // # Threads to iterate directory

static GLOBAL_FILEOPEN_CLEANUP: AtomicU64 = AtomicU64::new(0);
static GLOBAL_FILEOPEN_TAIL:    AtomicU64 = AtomicU64::new(0);
static GLOBAL_FILEOPEN_ID:      AtomicU64 = AtomicU64::new(0);
static GLOBAL_FILEOPEN_MASK:    AtomicU64 = AtomicU64::new(0);
static GLOBAL_FINO: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);


pub fn escp_sender(safe_args: logging::dtn_args_wrapper, flags: &EScp_Args) {
  let args = safe_args.args;
  let (host,dest_tmp,dest);
  let mut fc_hash: HashMap<u64, (u64, u32, u32)> = HashMap::new();
  let mut files_ok=0;

  match flags.destination.rfind(':') {
    Some (a) => { (host, dest_tmp) = flags.destination.split_at(a); },
    _        => {
      eprintln!("Expected ':' in argument '{}'; local copy not implemented",
                flags.destination);
      process::exit(-1);
    }
  }

  (_, dest) = dest_tmp.split_at(1);

  logging::initialize_logging(flags.log_file.as_str(), safe_args);
  debug!("Transfer to host: {}, dest_files: {} ", host, dest );

  let (mut sin, mut sout, mut serr, file, proc, stream, fd);

  if !flags.mgmt.is_empty() {
    stream = UnixStream::connect(flags.mgmt.clone())
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

    if !flags.identity.is_empty() {
      ssh_args.extend(["-i", flags.identity.as_str()]);
    }

    if !flags.ssh_option.is_empty() {
      ssh_args.extend(["-o", flags.ssh_option.as_str()]);
    }

    if flags.batch_mode {
      ssh_args.extend(["-o", "BatchMode=True"]);
    }

    if !flags.ssh_config.is_empty() {
      ssh_args.extend(["-F", flags.ssh_config.as_str() ]);
    }

    if !flags.jump_host.is_empty() {
      ssh_args.extend(["-J", flags.jump_host.as_str() ]);
    }

    if !flags.cipher.is_empty() {
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

    ssh_args.extend([ "--server"]);
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
    let (session_id, start_port, do_verbose, crypto_key, io_engine, nodirect,
         thread_count, block_sz, do_hash, do_compression, do_sparse,
         do_preserve, log_file
        );

    crypto_key = vec![ 0i8; 16 ];

    let mut builder = flatbuffers::FlatBufferBuilder::with_capacity(128);

    unsafe {
      (*args).do_crypto = true;
      tx_init(args);
      (*args).session_id = rand::random::<u64>();
      session_id = (*args).session_id;
      start_port = (*args).active_port;
      io_engine  = (*args).io_engine;
      nodirect  = (*args).nodirect;
      do_hash = (*args).do_hash;
      thread_count = (*args).thread_count;
      block_sz = (*args).block;

      do_preserve = (*args).do_preserve;
      do_compression = (*args).compression > 0;
      do_sparse = (*args).sparse > 0;
      do_verbose = verbose_logging  > 0;
      log_file = Some(builder.create_string(flags.log_file.as_str()));

      std::intrinsics::copy_nonoverlapping( (*args).crypto_key.as_ptr() , crypto_key.as_ptr() as *mut u8, 16 );
    }

    let ckey = Some( builder.create_vector( &crypto_key ) );

    let bu = session_init::Session_Init::create(
      &mut builder, &session_init::Session_InitArgs{
        version_major: env!("CARGO_PKG_VERSION_MAJOR").parse::<i32>().unwrap(),
        version_minor: env!("CARGO_PKG_VERSION_MINOR").parse::<i32>().unwrap(),
        session_id,
        port_start: start_port as i32,
        do_verbose,
        do_crypto: true,
        crypto_key: ckey,
        io_engine,
        no_direct: nodirect,
        do_hash,
        thread_count,
        block_sz,
        do_compression,
        do_sparse,
        do_preserve,
        log_file,
        ..Default::default()
      }
    );
    builder.finish( bu, None );
    let buf = builder.finished_data();

    debug!("Sending session_init message of len: {}", buf.len() );

    let hdr  = to_header( buf.len() as u32, msg_session_init );

    _ = sin.write( &hdr );
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
        error!("SSH to remote returned an error: {}", s.trim() );
        eprint!("Remote server returned an error '{}'\n\
                 Is 𝘌𝘚𝘤𝘱 installed on remote host?\n", s.trim()
               );
        return
      }
    }

    let (sz, t) = from_header( buf.to_vec() );
    debug!("Got sz={:?} of type={:?}", sz, t);

    buf.resize( sz as usize, 0 );
    let result = sout.read_exact( &mut buf);
    match result {
      Ok (_) => {}
      Err(error) => {
        error!("SSH session read failed {:?}", error );
        return;
      }
    }

    let helo = flatbuffers::root::<session_init::Session_Init>(buf.as_slice()).unwrap();
    debug!("Got response from receiver");

    if (helo.version_major() == 0) && (helo.version_minor() <= 7) {
      eprintln!("Receiver version {}.{} < required 0.8 and must be updated.",
                helo.version_major(), helo.version_minor());
      error!("Receiver version {}.{} < required 0.8 and must be updated.",
                helo.version_major(), helo.version_minor());
    }


    unsafe {
      // Connect to host using port specified by receiver
      let d_str = CString::new( helo.port_start().to_string() ).unwrap();
      let host_str = CString::new( host ).unwrap();

      debug!("Sender params: {:?} {:?}", host_str, d_str);
      (*(args)).sock_store[0] =  dns_lookup( host_str.as_ptr() as *mut i8 , d_str.as_ptr() as *mut i8);
      (*args).sock_store_count = 1;
      (*args).active_port = helo.port_start() as u16;

      debug!("Starting Sender");
      tx_start(args);
    }
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

  let fc_out;
  {
    let fc_in;
    (fc_in, fc_out) = crossbeam_channel::unbounded();
    let nam = "fc_0".to_string();
    thread::Builder::new().name(nam).spawn(move ||
      fc_worker(fc_in)).unwrap();
  }

  let (bytes_total, files_total) = iterate_files(
    flags, safe_args, dest.to_string(),
    &fi,
    &mut fc_hash,
    &mut files_ok,
    &fc_out );
  debug!("Finished iterating files, total bytes={bytes_total}");

  if (bytes_total <= 0) && (files_total == 0) && (flags.io_engine != "shmem")  {
    eprintln!("Nothing to transfer, exiting.");
    process::exit(1);
  }

  let mut last_update = std::time::Instant::now();
  loop {
    if flags.quiet {
      break;
    }

    {
      // Note the delay below; file_check usually delays for interval specified
      if file_check(
        &mut fc_hash,
        std::time::Instant::now() + std::time::Duration::from_millis(1),
        &mut files_ok,
        &fc_out
      ) != 1 {
        eprintln!("Exiting because of CRC mismatch");
        thread::sleep(std::time::Duration::from_millis(2));
        process::exit(1);
      }
    }

    let bytes_now = unsafe { get_bytes_io( args ) };
    if (last_update.elapsed().as_secs_f32() > 0.2) ||
       (bytes_now>=bytes_total) ||
       (files_ok >= files_total) {

      let duration = start.elapsed();

      let width= ((bytes_now as f32 / bytes_total as f32) * 40.0) as usize ;
      let progress = format!("{1:=>0$}", width, ">");
      let rate = if bytes_now>0 {
        bytes_now as f32/duration.as_secs_f32() } else {0.0 };

      let eta= ((bytes_total - bytes_now) as f32 / rate) as i64;

      let eta_human = if eta > 3600 {
        format!("{:02}:{:02}:{:02}", eta/3600, (eta/60)%60, eta%60)
      } else {
        format!("{:02}:{:02}", eta/60, eta%60)
      };

      let rate_str;
      let tot_str;

      unsafe {
        let tmp = human_write( rate as u64, !flags.bits );
        rate_str= CStr::from_ptr(tmp).to_str().unwrap();

        let tmp = human_write( bytes_now as u64, true );
        tot_str= CStr::from_ptr(tmp).to_str().unwrap();

        debug!("transfer progress: {}/{} {}/{}", bytes_now, bytes_total, files_ok, files_total);
      }

      let units = if flags.bits { "bits" } else { "B" };

      let bar = format!("\r [{: <40}] {}B {}{}/s {: <10}",
                        progress, tot_str, rate_str, units, eta_human);
      _ = fi.write(bar.as_bytes());
      _ = fi.flush();

      if (bytes_now >= bytes_total) || (files_ok >= files_total) {
        let s = format!("\rSent    : {tot_str}B in {files_total} files at {rate_str}{units}/s in {:0.1}s {:18}\r",
          duration.as_secs_f32(), "");
        _ = fi.write(s.as_bytes());
        _ = fi.flush();
        info!("{}", s.trim());
        break;
      }

      last_update = std::time::Instant::now();
    }

  }

  unsafe { fc_push( 0, 0, 0 ); }

  loop {

    if files_ok as i64 >= (files_total as i64) {
      debug!("Exiting because {files_ok} >= {files_total}");
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

    debug!("Loopping {files_ok} / {files_total}");
  }

  // Finished sending data
  {
    // Let receiver know that we think the session is complete

    info!("Send msg_session_complete");
    let hdr = to_header( 0, msg_session_complete );
    unsafe {
      meta_send( std::ptr::null_mut::<i8>(), hdr.as_ptr() as *mut i8, 0_i32 );
    }
  }

  // Tell TX readers to finish
  unsafe {
    file_completetransfer();
  }

  info!("Waiting for ACK");
  // Wait for ACK

  while unsafe{ meta_recv() }.is_null() {
    thread::sleep(std::time::Duration::from_millis(20));
  }

  unsafe { meta_complete(); }

  _ = fi.write("\rComplete\n".as_bytes());
  _ = fi.flush();
  info!("Transfer complete");

  thread::sleep(std::time::Duration::from_millis(20)); // Pause for logs
  process::exit(0); // Don't wait for threads
}

fn file_check(
    hm: &mut HashMap<u64, (u64, u32, u32)>,
    run_until: std::time::Instant,
    files_ok: &mut u64,
    fc_out: &crossbeam_channel::Receiver<(u64, u64, u32, u32)> ) -> u64
{

  let ptr = unsafe { meta_recv() };

  if !ptr.is_null() {

    let b = unsafe { slice::from_raw_parts(ptr, 6).to_vec() };
    let (sz, t) = from_header( b );

    if t != msg_file_stat {
      if t == msg_keepalive {
        debug!("file_check: Got keepalive, ignoring");
      } else {
        info!("file_check: Got unexpected type={t}, ignoring");
      }
      unsafe{ meta_complete(); }
      return 1;
    }

    let dst:[MaybeUninit<u8>; 131072] = [{ std::mem::MaybeUninit::uninit() }; 131072];
    let mut dst = unsafe { std::mem::transmute::
      <[std::mem::MaybeUninit<u8>; 131072], [u8; 131072]>(dst) };

    let res = zstd_safe::decompress(dst.as_mut_slice(),
      unsafe{slice::from_raw_parts(ptr.add(16), sz as usize)} );

    _ = res.expect("decompress failed");

    let fs = flatbuffers::root::<file_spec::ESCP_file_list>(&dst).unwrap();

    for e in fs.files().unwrap() {

      let (rx_fino, rx_sz, rx_crc, rx_complete) = (e.fino(), e.sz(), e.crc(), e.complete());
      debug!("file_check on {} {} {:#X} {}", rx_fino, rx_sz, rx_crc, rx_complete);

      if (rx_fino == 0) && (rx_sz == 0) && (rx_complete == 4) {
        debug!("Receiver confirmed an empty file");
        *files_ok += 1;
        continue;
      }

      if !(*hm).contains_key(&rx_fino) {
        loop {
          // loop until the hm contains key or fc_out returns error
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
        error!("\rsz mismatch on {} {}!={}\n", rx_fino, sz, rx_sz);
        return 0;
      }

      if crc != rx_crc {
        // Should always be able to test CRC because if CRC not enabled
        // entry should be zero
        error!("\rCRC mismatch on {} {:#010X}!={:#010X}\n", rx_fino, crc, rx_crc);
        return 0;
      }

      *files_ok += 1;

      debug!("Matched successfully {}", rx_fino);
      _ = (*hm).remove(&rx_fino);
    }

  }

  if !ptr.is_null() {
    unsafe{ meta_complete(); }
  } else {
    let interval = run_until - std::time::Instant::now();
    if interval.as_secs_f32() > 0.0 {
      debug!("file_check: still have {}s left", interval.as_secs_f32());
      thread::sleep(interval);
    } else {
      debug!("file_check: time is over: {}", interval.as_secs_f32());
    }
  }

  1
}

fn iterate_dir_worker(  dir_out:  crossbeam_channel::Receiver<(PathBuf, PathBuf)>,
                        files_in: crossbeam_channel::Sender<(PathBuf, PathBuf)>,
                        args:     logging::dtn_args_wrapper ) {

  let (opendir, readdir, closedir);
  unsafe {
    opendir = (*(*args.args).fob).opendir.unwrap();
    closedir  = (*(*args.args).fob).closedir.unwrap();
    readdir   = (*(*args.args).fob).readdir.unwrap();
  }

  loop {
    let ( filename, prefix );
    match dir_out.recv() {
      Ok((p,f)) => { (prefix, filename) = (p, f); }
      Err(_) => { debug!("iterate_dir_worker: !dir_out, worker end."); return; }
    }

    let ( fi_str, p_str ) = (filename.to_str().unwrap(), prefix.to_str().unwrap());

    // debug!("iterate_dir_worker: open {filename}, fd={fd}");
    let combined = prefix.join(filename.clone());
    let combined_str = combined.to_str().unwrap();

    let dir = unsafe {
      let c_str = CString::new(combined_str).unwrap();
      opendir( c_str.as_ptr() )
    };

    if dir.is_null() {
      info!("iterate_dir_worker: Got an error opening '{combined_str}'");
      continue;
    }

    debug!("iterate_dir_worker: iterate {combined_str}");

    loop {
      let fi = unsafe { readdir( dir ) };
      if fi.is_null() {
        break;
      }

      unsafe {
        let c_str = CStr::from_ptr((*fi).d_name.as_ptr());
        let s = c_str.to_str().unwrap().to_string();

        if (s == ".") || (s == "..") {
          continue;
        }
        let new_path = filename.join(s);
        let path_name = new_path.to_str().unwrap();

        if ((*fi).d_type as u32 == DT_REG) || ((*fi).d_type as u32 == DT_DIR) {
          debug!("iterate_dir_worker: added {p_str} {path_name}");
          _ = files_in.send((prefix.clone(), new_path));
        } else {
          debug!("iterate_dir_worker: ignoring {p_str} {path_name}");
        }
      }
    }

    let _ = GLOBAL_FILEOPEN_TAIL.fetch_add(1, SeqCst);
    if unsafe{ closedir(dir) } != 0 {
      info!("iterate_dir_worker: error closing directory {p_str} {fi_str}");
    } else {
      debug!("iterate_dir_worker: Finished traversing {p_str} {fi_str}");
    }
  }
}

fn iterate_file_worker(
  files_out: crossbeam_channel::Receiver<(PathBuf, PathBuf)>,
  dir_in:    crossbeam_channel::Sender<(PathBuf, PathBuf)>,
  msg_in:    crossbeam_channel::Sender<(String, u64, stat)>,
  args:      logging::dtn_args_wrapper,
  // hm: &mut HashMap<u64, (u64, u32, u32)>,
  // files_ok: &mut u64
) {

  let mode:i32 = 0;
  let (mut direct_mode, mut recursive) = (true, false);
  let (open, close_fd);
  let mut exit_ready = false;
  let id = GLOBAL_FILEOPEN_ID.fetch_add(1, SeqCst);

  unsafe {
    if (*args.args).nodirect  { direct_mode = false; }
    if (*args.args).recursive { recursive   = true;  }
    open     = (*(*args.args).fob).open.unwrap();
    close_fd = (*(*args.args).fob).close_fd.unwrap();
  }

  let mut fd;
  let (mut filename, mut prefix, mut c_str);

  loop {

    match files_out.recv_timeout(std::time::Duration::from_millis(4)) {
      Ok((p, f)) => { (prefix, filename) = (p, f); }
      Err(crossbeam_channel::RecvTimeoutError::Timeout) => {

        if GLOBAL_FILEOPEN_TAIL.load(SeqCst) ==
           GLOBAL_FILEOPEN_CLEANUP.load(SeqCst) {

          if !exit_ready {
            _ = GLOBAL_FILEOPEN_MASK.fetch_or(1 << id, SeqCst);
            debug!("iterate_file_worker: Worker {} ready to exit", id);
            exit_ready = true;
          }

          let mask = GLOBAL_FILEOPEN_MASK.load(SeqCst);
          if mask ==  ((1 << GLOBAL_FILEOPEN_COUNT)  - 1) {
            debug!("iterate_file_worker: All workers finished, exiting");
            break;
          }

        }
        continue;
      }
      Err(_) => { debug!("iterate_file_worker: !files_out, worker end."); return; }
    }

    if exit_ready {
      _ = GLOBAL_FILEOPEN_MASK.fetch_and(!(1 << id), SeqCst);
      debug!("iterate_file_worker: Worker {} is unready", id);
    }

    let path = prefix.join(filename.clone()).canonicalize().unwrap_or("".into());
    if path.to_str().unwrap() == "" {
      info!("iterate_file_worker: Ignoring {:?}, not on disk", prefix);
      continue;
    }
    if ! path.starts_with(prefix.clone()) {
      info!("iterate_file_worker: Ignoring {:?}, outside of prefix {:?}",
            path, prefix);
      continue;
    }

    c_str = CString::new(path.to_str().unwrap()).unwrap();

    unsafe {
      let mut st: stat = std::mem::zeroed();

      loop {
        if direct_mode {
          fd = open(c_str.as_ptr(), (*args.args).flags | libc::O_DIRECT, mode);
          if (fd == -1) && (*libc::__errno_location() == 22) {
            direct_mode = false;
            fd = open(c_str.as_ptr(), (*args.args).flags, mode);
          }
        } else {
          fd = open(c_str.as_ptr(), (*args.args).flags, mode);
        }

        if fd == -1 {
          if std::io::Error::last_os_error().raw_os_error().unwrap() == 24 {
            debug!("Open failed on {:?} {:?}; retrying.", c_str,
                     std::io::Error::last_os_error() );
            thread::sleep(std::time::Duration::from_millis(20));
            continue;
          }
          error!("trying to open {:?} {:?}", c_str,
                   std::io::Error::last_os_error() );
          eprintln!("trying to open {:?} {:?}", c_str,
                     std::io::Error::last_os_error() );
          return;
        }

        break;
      }

      let res = ((*(*(args.args)).fob).fstat.unwrap())( fd, &mut st as * mut _ );
      if res == -1 {
        error!("trying to stat {:?} {}", c_str,
                  std::io::Error::last_os_error().raw_os_error().unwrap() );
        continue;
      }

      let f = c_str.to_str().unwrap();
      match st.st_mode & libc::S_IFMT {

        libc::S_IFDIR => {
          if recursive {
            let _ = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, SeqCst);
            _ = dir_in.send((prefix, filename));
          } else {
            info!("Ignoring directory {f} because recursive mode not set");
            eprintln!("\rIgnoring directory {f} because recursive mode not set");
          }
          if close_fd(fd) == -1 {
            info!("Error closing file descriptor (directory)");
          }
          continue;
        }
        libc::S_IFLNK => {
          info!("Ignoring link {f}");
          eprintln!("\rIgnoring link {f}");
          if close_fd(fd) == -1 {
            info!("Error closing file descriptor (symlink)");
          }
          _ = close_fd(fd);
          continue;
        }
        libc::S_IFREG => { /* add */ }

        _             => {
          info!("Ignoring {:#X} {f}", st.st_mode & libc::S_IFMT);
          eprintln!("\rIgnoring {:#X} {f}", st.st_mode & libc::S_IFMT);
          if close_fd(fd) == -1 {
            info!("Error closing file descriptor (catch-all)");
          }
          continue;
        }

      }

      let fname = filename.to_str().unwrap();
      if st.st_size > 0 {
        let fino = 1+GLOBAL_FINO.fetch_add(1, SeqCst);
        debug!("addfile:  {fname} fn={fino} sz={:?}", st.st_size);

        _ = msg_in.send( (fname.to_string(), fino, st) );
        loop {
          let res = file_addfile( fino, fd, st.st_size, 0, 0, 0, 0 );
          if !res.is_null() {
            break;
          }
        }
      } else {
        debug!("addfile:  {fname} fn=NONE sz=NIL");
        _ = msg_in.send( (fname.to_string(), 0, st) );
        if close_fd(fd) == -1 {
          info!("Error closing file descriptor (empty file)");
        }
      }

    }
  }

  debug!("iterate_file_worker: exiting");
}

fn iterate_files ( flags: &EScp_Args,
                    args: logging::dtn_args_wrapper,
               dest_path: String,
                mut sout: &std::fs::File,
                 fc_hash: &mut HashMap<u64, (u64, u32, u32)>,
                files_ok: &mut u64,
                  fc_out: &crossbeam_channel::Receiver<(u64, u64, u32, u32)>
                 ) -> (i64,u64) {

  let msg_out;

  {
    // Spawn helper threads
    let (files_in, files_out) = crossbeam_channel::bounded(15000);
    let (dir_in, dir_out) = crossbeam_channel::unbounded();

    let msg_in;
    (msg_in, msg_out) = crossbeam_channel::bounded(400);

    _ = GLOBAL_FILEOPEN_CLEANUP.fetch_add(1, SeqCst);

    for j in 0..GLOBAL_FILEOPEN_COUNT{
      let nam = format!("file_{}", j as i32);
      let a = args;
      let fo = files_out.clone();
      let di = dir_in.clone();
      let mi = msg_in.clone();
      thread::Builder::new().name(nam).spawn(move ||
        iterate_file_worker(fo, di, mi, a)).unwrap();
    }

    for j in 0..GLOBAL_DIROPEN_COUNT{
      let nam = format!("dir_{}", j as i32);
      let a = args;
      let dir_o = dir_out.clone();
      let fi = files_in.clone();

      thread::Builder::new().name(nam).spawn(move ||
        iterate_dir_worker(dir_o, fi, a)).unwrap();
    }

    for fi in &flags.source {
      if fi.is_empty() { continue; };

      let fi_path = match fs::canonicalize(PathBuf::from(fi)) {
        Ok(a) => { a }
        Err(e) => {
          let errmsg = format!("\rCould not open file='{}': {}", fi, e);
          info!("{errmsg}");
          eprintln!("\n{errmsg}");
          fi.into()
        }
      };

      _ = files_in.send(
            (fi_path.parent().unwrap().to_path_buf(),
             std::path::Path::new(fi_path.file_name().unwrap().to_str().unwrap()).to_path_buf()
            ));
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

  let _ = GLOBAL_FILEOPEN_TAIL.fetch_add(1, SeqCst);

  let mut last_update = std::time::Instant::now();

  loop {
    if !flags.quiet && (last_update.elapsed().as_secs_f32() > 0.15) {
      last_update = std::time::Instant::now();

      let a = unsafe { human_write( files_total, true )};
      let mut rate = "0";

      if files_total >= 1 {
        unsafe {
          let b = human_write(
            (files_total as f32/start.elapsed().as_secs_f32()) as u64, true);
          rate = CStr::from_ptr(b).to_str().unwrap();
        }
      }

      let tot  = unsafe { CStr::from_ptr(a).to_str().unwrap() };
      let mut transfer = String::from("");

      let bytes_now = unsafe { get_bytes_io(args.args) };
      if bytes_now > 0 {

        let duration = start.elapsed();
        let rate = bytes_now as f32/duration.as_secs_f32();
        let units = if flags.bits { "bits" } else { "B" };

        let tmp = unsafe { human_write(rate as u64, !flags.bits) };
        let rate_str= unsafe { CStr::from_ptr(tmp).to_str().unwrap() };

        let tmp = unsafe { human_write(bytes_now as u64, true) };
        let tot_str= unsafe { CStr::from_ptr(tmp).to_str().unwrap() };

        transfer = format!("Bytes: {}B {}{}/s",
                           tot_str, rate_str, units);
      }

      let l = format!("\rCalculating ... Files: {tot} ({rate}/s)   {transfer}");
      _ = sout.write(l.as_bytes());
      _ = sout.flush();
    }


    loop {
      let (fi, fino, st);

      if !did_init {
        builder = flatbuffers::FlatBufferBuilder::with_capacity(8192);
        did_init = true;
      }

      match msg_out.recv_timeout(std::time::Duration::from_micros(97)) {
        Ok((a,b,c)) => { (fi, fino, st) = (a,b,c); }
        Err(crossbeam_channel::RecvTimeoutError::Timeout) => {

          if file_check(
            fc_hash,
            std::time::Instant::now() + std::time::Duration::from_millis(2),
            files_ok,
            fc_out
          ) != 1 {
            eprintln!("Exiting because of CRC mismatch");
            thread::sleep(std::time::Duration::from_millis(2));
            process::exit(1);
          }

          break;
        }

        Err(_) => {
          debug!("iterate_files: Got an abnormal from msg_out.recv, assume EOQ");
          do_break = true;
          break;
        }
      }

      vec.push_back( (fino, fi, st) );

      files_total += 1;
      bytes_total += st.st_size;
      counter += 1;

      if counter > counter_max {
        counter_max = 300;
        break;
      }
    }

    if counter > 0 {
      vec.make_contiguous().sort_by_key(|k| k.0);

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
        if flags.preserve {
          v.push(
          file_spec::File::create( &mut builder,
            &file_spec::FileArgs{
                  fino: *a,
                  name,
                  sz: c.st_size,
                  mode: c.st_mode,
                  uid: c.st_uid,
                  gid: c.st_gid,
                  atim_sec: c.st_atim.tv_sec,
                  atim_nano: c.st_atim.tv_nsec,
                  mtim_sec: c.st_mtim.tv_sec,
                  mtim_nano: c.st_mtim.tv_nsec,
                  ..Default::default()
            }));
        } else {
          v.push(
          file_spec::File::create( &mut builder,
            &file_spec::FileArgs{
                  fino: *a,
                  name,
                  sz: c.st_size,
                  ..Default::default()
            }));
        }
      }

      if iterations == 0 {
        continue;
      }

      for _ in 0..iterations {
        vec.pop_front();
      }

      let root = Some(builder.create_string((dest_path).as_str()));
      let fi   = Some(builder.create_vector( &v ));
      let bu = file_spec::ESCP_file_list::create(
        &mut builder, &file_spec::ESCP_file_listArgs{
          root,
          files: fi,
          complete: do_break,
          ..Default::default()
        }
      );
      builder.finish( bu, None );

      let buf = builder.finished_data();

      if buf.len() > 320 {

        let dst:[MaybeUninit<u8>; 49152] = [{ std::mem::MaybeUninit::uninit() }; 49152 ];
        let mut dst = unsafe { std::mem::transmute::
          <[std::mem::MaybeUninit<u8>; 49152], [u8; 49152]>(dst) };

        // let dst = Vec::<u8>::with_capacity(49152);

        let res = zstd_safe::compress( &mut dst, buf, 3 );

        let compressed_sz = res.expect("Compression failed");
        let hdr = to_header( compressed_sz as u32, msg_file_spec | msg_compressed );
        debug!("iterate_files: Sending compressed meta {}/{}, size {}/{} #{}",
               counter, files_total, buf.len(), compressed_sz, iterations);
        unsafe{ meta_send(dst.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8, compressed_sz as i32) };
      } else {
        let hdr = to_header( buf.len() as u32, msg_file_spec );

        debug!("iterate_files: Sending file meta data for {}/{}, size is {} #={}",
               counter, files_total, buf.len(), iterations);
        unsafe{ meta_send(buf.as_ptr() as *mut i8, hdr.as_ptr() as *mut i8, buf.len() as i32) };
      }

      counter -= iterations;
    }

    if do_break {
      debug!("iterate_files: do_break flagged");
      break;
    }
  }

  debug!("iterate_files: is finished");
  (bytes_total, files_total)
}


