#![allow(non_snake_case, unused_imports, dead_code, non_camel_case_types, non_upper_case_globals)]
use escp;

#[cfg(test)]
mod tests {
    use ntest::timeout;

    #[test]
    #[timeout(300)]
    fn test_int_conversion() {
      assert_eq!( escp::int_from_human ( "1k".to_string() ), 1024 );
    }

    #[test]
    #[timeout(300000)]
    fn test_file_operations() {
      eprintln!("Creating ~1M files");
      assert_eq!( escp::tst::create_files ("/tmp/test".to_string(), 56, 16384, 0, 4096) , true );

      eprintln!("Iterating directory");
      assert_eq!( escp::tst::iterate_dir("/tmp/test".to_string()), true );

    }

}
