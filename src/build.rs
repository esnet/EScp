use std::process::Command;
use shadow_rs::BuildPattern;
use shadow_rs::ShadowBuilder;

use std::fs::File;
use std::io::{BufRead, BufReader};

fn iterate_lines( t: &str, file: File ) {
        for line in BufReader::new(file).lines() {
            println!("cargo:rustc-link-arg-{}={}", t, line.unwrap().trim_end());
        }
}

fn main() {

    if let Ok(file) = File::open("target/dtn/link_libraries.txt") {
      iterate_lines( "bins", file );
    } else {
      Command::new("sh").args(["./mk.sh", "argument"]).status().unwrap();
      if let Ok(file) = File::open("target/dtn/link_libraries.txt") {
        iterate_lines( "bins", file );
      } else {
        eprintln!("Error: file.txt not found");
        std::process::exit(-1);
      }
    }


    println!("cargo:rustc-link-arg-bins=-lrt");
    println!("cargo:rustc-link-arg-bins=-lc");
    println!("cargo:rustc-link-arg-bins=-no-pie");
    if let Ok(file) = File::open("target/dtn/link_libraries.txt") {
      iterate_lines( "tests", file );
    }
    println!("cargo:rustc-link-arg-tests=-lrt");
    println!("cargo:rustc-link-arg-tests=-lc");
    println!("cargo:rustc-link-arg-tests=-no-pie");

    Command::new("sh").args(["./mk.sh", "argument"]).status().unwrap();
    println!("cargo:rerun-if-changed=target/dtn/libdtn.a");
    ShadowBuilder::builder()
        .build_pattern(BuildPattern::RealTime)
        .build().unwrap();
}
