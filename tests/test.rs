#![allow(non_snake_case, unused_imports, dead_code, non_camel_case_types, non_upper_case_globals)]
use escp;

#[cfg(test)]
mod tests {

    #[test]
    fn test_int_conversion() {
      assert_eq!( escp::int_from_human ( "1k".to_string() ), 1024 );
    }

    #[test]
    fn test_file_operations() {
      assert_eq!( escp::tst::create_files("/tmp/test".to_string(), 4, 4, 0, 1024) , true );
    }

}
