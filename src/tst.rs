#![allow(non_snake_case, unused_imports, dead_code, non_camel_case_types, non_upper_case_globals)]
include!("escp/bindings.rs");


use std::path::Path;
use std::fs;


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


  for subdir in 1..dir_count {
    let d = format!("{:02X}", subdir);
    let np = new_path.join(d);
    fs::create_dir( np ).unwrap();
    for file in 1..file_count {
      println!("{}/{}/test-{:08X}:{}-{}", dir_root, subdir, file, file_sz_min, file_sz_max);
    }
  }

  true
}

fn main() {
  create_files("/tmp/test".to_string(), 4, 4, 0, 1024);
}
