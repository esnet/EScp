// use scp_test_harness::{save_metrics, TestRunner, TestSuite};
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant, SystemTime};
use walkdir::WalkDir;

#[derive(Debug, Deserialize)]
pub struct TestSuite {
    pub config: TestConfig,
    pub test: Vec<TestCase>,
}

#[derive(Debug, Deserialize)]
pub struct TestConfig {
    pub scp_binary: PathBuf,
    pub remote_scp_binary: Option<PathBuf>,
    pub dest_host: String,
    pub fixtures_base: PathBuf,
}

#[derive(Debug, Deserialize)]
pub struct TestCase {
    pub name: String,
    pub flags: Vec<String>,
    pub source: String,
    pub destination: String,
    pub verify: String,
    #[serde(default)]
    pub preserve_permissions: bool,
    #[serde(default)]
    pub preserve_timestamps: bool,
}

#[derive(Debug, Serialize)]
pub struct TestResult {
    pub name: String,
    pub passed: bool,
    pub duration: Duration,
    pub error: Option<String>,
    pub timestamp: SystemTime,
}

pub struct TestRunner {
    config: TestConfig,
}

impl TestRunner {
    pub fn new(config: TestConfig) -> Self {
        Self { config }
    }

    pub fn run_test(&self, test: &TestCase) -> TestResult {
        let start = Instant::now();
        let timestamp = SystemTime::now();

        match self.run_test_inner(test) {
            Ok(_) => TestResult {
                name: test.name.clone(),
                passed: true,
                duration: start.elapsed(),
                error: None,
                timestamp,
            },
            Err(e) => TestResult {
                name: test.name.clone(),
                passed: false,
                duration: start.elapsed(),
                error: Some(e.to_string()),
                timestamp,
            },
        }
    }

    fn run_test_inner(&self, test: &TestCase) -> Result<()> {
        // Build command
        let mut scp_binary = self.config.scp_binary.clone();
        let p;

        if scp_binary.as_os_str().is_empty() {
            p = format!("{}/target/{}", env!("CARGO_MANIFEST_DIR"), "debug/escp");
            scp_binary = PathBuf::from(p.clone());
        } else {
            p = format!("{:?}", scp_binary.as_os_str());
        }

        println!("--> {:?}", scp_binary);

        let mut cmd = Command::new(scp_binary);

        if let Some(remote_binary) = &self.config.remote_scp_binary {
            cmd.arg("-D").arg(remote_binary);
        } else {
            cmd.arg("-D").arg(p);
        }

        cmd.args(&test.flags);

        let source = self.config.fixtures_base.join(&test.source);
        let dest = test
            .destination
            .replace("<dest_host>", &self.config.dest_host);

        cmd.arg(&source).arg(&dest);

        println!("{:?}", cmd);

        // Execute
        let output = cmd.output()?;

        if !output.status.success() {
            return Err(anyhow!(
                "scp command failed: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        // Extract destination path for verification
        let dest_path = self.extract_dest_path(&dest)?;

        // Run verification
        match test.verify.as_str() {
            "file_match" => self.verify_file_match(&source, &dest_path),
            "directory_match" => self.verify_directory_match(
                &source,
                &dest_path,
                test.preserve_permissions,
                test.preserve_timestamps,
            ),
            _ => Err(anyhow!("Unknown verification type: {}", test.verify)),
        }
    }

    fn extract_dest_path(&self, dest: &str) -> Result<PathBuf> {
        // Parse "host:/path" format and return path portion
        if let Some(colon_pos) = dest.find(':') {
            Ok(PathBuf::from(&dest[colon_pos + 1..]))
        } else {
            Ok(PathBuf::from(dest))
        }
    }

    fn verify_file_match(&self, source: &Path, dest: &Path) -> Result<()> {
        if !self.files_match(source, dest)? {
            return Err(anyhow!("File content mismatch"));
        }
        Ok(())
    }

    fn verify_directory_match(
        &self,
        source: &Path,
        dest: &Path,
        check_permissions: bool,
        check_timestamps: bool,
    ) -> Result<()> {
        let source_files = self.collect_file_tree(source)?;
        let dest_files = self.collect_file_tree(dest)?;

        if source_files.len() != dest_files.len() {
            return Err(anyhow!(
                "File count mismatch: {} vs {}",
                source_files.len(),
                dest_files.len()
            ));
        }

        for (rel_path, source_info) in source_files {
            println!("rel_path: {:?} {:?}", rel_path, dest);
            let dest_info = dest_files
                .get(&rel_path)
                .ok_or_else(|| anyhow!("Missing file: {}", rel_path.display()))?;

            if !self.files_match(&source_info.path, &dest_info.path)? {
                return Err(anyhow!("Content mismatch: {}", rel_path.display()));
            }

            if check_permissions {
                #[cfg(unix)]
                {
                    use std::os::unix::fs::PermissionsExt;
                    let source_perms = source_info.metadata.permissions().mode() & 0o777;
                    let dest_perms = dest_info.metadata.permissions().mode() & 0o777;
                    if source_perms != dest_perms {
                        return Err(anyhow!(
                            "Permission mismatch for {}: {:o} vs {:o}",
                            rel_path.display(),
                            source_perms,
                            dest_perms
                        ));
                    }
                }
            }

            if check_timestamps {
                let source_time = source_info.metadata.modified()?;
                let dest_time = dest_info.metadata.modified()?;
                let diff = if source_time > dest_time {
                    source_time.duration_since(dest_time).unwrap_or_default()
                } else {
                    dest_time.duration_since(source_time).unwrap_or_default()
                };

                if diff > Duration::from_secs(2) {
                    return Err(anyhow!(
                        "Timestamp mismatch for {}: {:?}",
                        rel_path.display(),
                        diff
                    ));
                }
            }
        }

        Ok(())
    }

    fn files_match(&self, a: &Path, b: &Path) -> Result<bool> {
        let hash_a = self.hash_file(a)?;
        let hash_b = self.hash_file(b)?;
        Ok(hash_a == hash_b)
    }

    fn hash_file(&self, path: &Path) -> Result<Vec<u8>> {
        let mut hasher = Sha256::new();
        let mut file = File::open(path)?;
        std::io::copy(&mut file, &mut hasher)?;
        Ok(hasher.finalize().to_vec())
    }

    fn collect_file_tree(&self, base: &Path) -> Result<HashMap<PathBuf, FileInfo>> {
        let mut files = HashMap::new();

        for entry in WalkDir::new(base) {
            let entry = entry?;
            if entry.file_type().is_file() {
                let rel_path = entry.path().strip_prefix(base)?.to_path_buf();
                files.insert(
                    rel_path,
                    FileInfo {
                        path: entry.path().to_path_buf(),
                        metadata: entry.metadata()?,
                    },
                );
            }
        }

        Ok(files)
    }
}

struct FileInfo {
    path: PathBuf,
    metadata: std::fs::Metadata,
}

pub fn save_metrics(results: &[TestResult], path: &str) -> Result<()> {
    fs::create_dir_all("test_results")?;
    let file = File::create(path)?;
    serde_json::to_writer_pretty(file, results)?;
    Ok(())
}

#[test]
fn run_scp_test_suite() {
    // Load test suite
    let suite_path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/test_suite.toml");
    let suite_content = fs::read_to_string(suite_path).expect("Failed to read test suite");
    let suite: TestSuite = toml::from_str(&suite_content).expect("Failed to parse test suite");

    // Create runner
    let runner = TestRunner::new(suite.config);

    // Run all tests
    let mut results = Vec::new();
    for test in suite.test {
        eprintln!("Running: {}", test.name);
        let result = runner.run_test(&test);
        eprintln!(
            "  {} in {:?}",
            if result.passed { "PASSED" } else { "FAILED" },
            result.duration
        );
        if let Some(err) = &result.error {
            eprintln!("  Error: {}", err);
        }
        results.push(result);
    }

    // Save metrics
    save_metrics(&results, "test_results/metrics.json").expect("Failed to save metrics");

    // Print summary
    let passed = results.iter().filter(|r| r.passed).count();
    let total = results.len();
    eprintln!("\n{}/{} tests passed", passed, total);

    // Assert all tests passed
    assert_eq!(passed, total, "Some tests failed");
}
