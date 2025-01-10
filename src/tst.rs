use std::{ path::Path, fs::File, io::Write };
// use std::{env, process, thread, collections::HashMap, fs};
use std::{thread, fs};
use super::*;

// Collection of functions used solely in the test framework


pub fn create_files( dir_root: String, dir_count: u32, file_count: u32, file_sz_min: u32, file_sz_max: u32 ) -> bool {

  let new_path = Path::new(&dir_root);

  if new_path.exists() {
    return true;
  }

  _ = fs::create_dir( dir_root.clone() ).unwrap();

  let mut hash = 0u64;
  unsafe {
    file_randrd( &mut hash as *mut ::std::os::raw::c_ulong as *mut ::std::os::raw::c_void , 8 );
  }

  println!("Got a value of: {}", hash);
  
  let mut v = vec![ 0u8; file_sz_max as usize ];


  for subdir in 0..dir_count {
    let d = format!("{:02X}", subdir);
    let np = new_path.join(d);
    fs::create_dir( np ).unwrap();
    for file in 0..file_count {
      hash = unsafe { xorshift64r( hash ) };
      let sz:i64 = ((hash % (file_sz_max-file_sz_min) as u64) + file_sz_min as u64).try_into().unwrap();
      for i in 0..(sz/8) {
        hash = unsafe { xorshift64r( hash ) };
        v[(i*8) as usize] = (hash & 0xff) as u8;
        v[((i*8)+1) as usize] = ((hash >> 8)  & 0xff) as u8;
        v[((i*8)+2) as usize] = ((hash >> 16)  & 0xff) as u8;
        v[((i*8)+3) as usize] = ((hash >> 24)  & 0xff) as u8;
        v[((i*8)+4) as usize] = ((hash >> 32)  & 0xff) as u8;
        v[((i*8)+5) as usize] = ((hash >> 40)  & 0xff) as u8;
        v[((i*8)+6) as usize] = ((hash >> 48)  & 0xff) as u8;
        v[((i*8)+7) as usize] = ((hash >> 56)  & 0xff) as u8;
      }
      let fun = format!("{}/{:02X}/test-{:08X}", dir_root, subdir, file);
      let mut fi = File::create( fun ).unwrap();
      let g = &mut v[..sz as usize];
      _ = fi.write_all( g );
    }
  }

  true
}

pub fn iterate_dir( dir_root: String ) {

  let args = unsafe { args_new() };
  let safe_args = escp::logging::dtn_args_wrapper{ args };


  // Spawn helper threads
  let (files_in, files_out) = crossbeam_channel::bounded(15000);
  let (dir_in, dir_out) = crossbeam_channel::unbounded();
  let (msg_in, msg_out) = crossbeam_channel::bounded(400);


  for j in 0..4 {
    let nam = format!("file_{}", j as i32);
    let a = safe_args;
    let fo = files_out.clone();
    let di = dir_in.clone();
    let mi = msg_in.clone();
    thread::Builder::new().name(nam).spawn(move ||
      escp::sender::iterate_file_worker(fo, di, mi, a)).unwrap();
  }

  for j in 0..2 {
    let nam = format!("dir_{}", j as i32);
    let a = safe_args;
    let dir_o = dir_out.clone();
    let fi = files_in.clone();

    thread::Builder::new().name(nam).spawn(move ||
      escp::sender::iterate_dir_worker(dir_o, fi, a)).unwrap();
  }
  
}

