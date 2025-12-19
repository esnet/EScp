use serde::{Deserialize, Serialize};
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::SystemTime;
use anyhow::{Context, Result};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TransferState {
    pub session_id: String,
    pub bytes_transferred: u64,
    pub total_bytes: u64,
    pub checksum_state: ChecksumState,
    pub timestamp: SystemTime,
    pub source_path: String,
    pub destination_path: String,
    pub config: TransferConfig,
    pub completed_blocks: Vec<BlockRange>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChecksumState {
    pub algorithm: String,
    pub partial_state: Vec<u8>,
    pub block_checksums: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TransferConfig {
    pub blocksize: u64,
    pub threads: usize,
    pub compress: bool,
    pub cipher: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BlockRange {
    pub start: u64,
    pub end: u64,
}

impl TransferState {
    pub fn new(
        source: impl AsRef<Path>,
        destination: impl AsRef<str>,
        total_bytes: u64,
        config: TransferConfig,
    ) -> Self {
        use uuid::Uuid;
        
        Self {
            session_id: Uuid::new_v4().to_string(),
            bytes_transferred: 0,
            total_bytes,
            checksum_state: ChecksumState::new(),
            timestamp: SystemTime::now(),
            source_path: source.as_ref().display().to_string(),
            destination_path: destination.as_ref().to_string(),
            config,
            completed_blocks: Vec::new(),
        }
    }
    
    pub fn save(&self, path: &Path) -> Result<()> {
        let json = serde_json::to_string_pretty(self)
            .context("Failed to serialize transfer state")?;
        
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)
                .context("Failed to create state directory")?;
        }
        
        let temp_path = path.with_extension("tmp");
        let mut file = File::create(&temp_path)
            .context("Failed to create temporary state file")?;
        
        file.write_all(json.as_bytes())
            .context("Failed to write state data")?;
        
        file.sync_all()
            .context("Failed to sync state file")?;
        
        fs::rename(&temp_path, path)
            .context("Failed to save state file")?;
        
        log::debug!("Saved transfer state to {}", path.display());
        Ok(())
    }
    
    pub fn load(path: &Path) -> Result<Self> {
        let mut file = File::open(path)
            .context("Failed to open state file")?;
        
        let mut contents = String::new();
        file.read_to_string(&mut contents)
            .context("Failed to read state file")?;
        
        let state: Self = serde_json::from_str(&contents)
            .context("Failed to parse state file")?;
        
        log::info!("Loaded transfer state from {}", path.display());
        log::debug!(
            "Session {}: {}/{} bytes transferred",
            state.session_id,
            state.bytes_transferred,
            state.total_bytes
        );
        
        Ok(state)
    }
    
    pub fn default_state_path(source: &Path) -> PathBuf {
        source.with_extension("escp.state")
    }
    
    pub fn state_directory() -> Result<PathBuf> {
        let state_dir = dirs::data_local_dir()
            .ok_or_else(|| anyhow::anyhow!("Could not determine data directory"))?
            .join("escp")
            .join("states");
        
        fs::create_dir_all(&state_dir)
            .context("Failed to create state directory")?;
        
        Ok(state_dir)
    }
    
    pub fn save_with_backup(&self) -> Result<()> {
        let primary_path = Self::default_state_path(Path::new(&self.source_path));
        self.save(&primary_path)?;
        
        let backup_path = Self::state_directory()?
            .join(format!("{}.state", self.session_id));
        self.save(&backup_path)?;
        
        Ok(())
    }
    
    pub fn load_with_fallback(source: &Path, session_id: Option<&str>) -> Result<Self> {
        let primary_path = Self::default_state_path(source);
        if primary_path.exists() {
            return Self::load(&primary_path);
        }
        
        if let Some(id) = session_id {
            let backup_path = Self::state_directory()?
                .join(format!("{}.state", id));
            if backup_path.exists() {
                return Self::load(&backup_path);
            }
        }
        
        Err(anyhow::anyhow!("No transfer state found"))
    }
    
    pub fn cleanup(&self) -> Result<()> {
        let primary_path = Self::default_state_path(Path::new(&self.source_path));
        if primary_path.exists() {
            fs::remove_file(&primary_path)
                .context("Failed to remove primary state file")?;
        }
        
        let backup_path = Self::state_directory()?
            .join(format!("{}.state", self.session_id));
        if backup_path.exists() {
            fs::remove_file(&backup_path)
                .context("Failed to remove backup state file")?;
        }
        
        log::debug!("Cleaned up state files for session {}", self.session_id);
        Ok(())
    }
    
    pub fn update_progress(&mut self, bytes: u64, block: Option<BlockRange>) {
        self.bytes_transferred += bytes;
        self.timestamp = SystemTime::now();
        
        if let Some(block) = block {
            self.completed_blocks.push(block);
        }
    }
    
    pub fn is_complete(&self) -> bool {
        self.bytes_transferred >= self.total_bytes
    }
    
    pub fn progress_percentage(&self) -> f64 {
        if self.total_bytes == 0 {
            return 0.0;
        }
        (self.bytes_transferred as f64 / self.total_bytes as f64) * 100.0
    }
}

impl ChecksumState {
    pub fn new() -> Self {
        Self {
            algorithm: "BLAKE3".to_string(),
            partial_state: Vec::new(),
            block_checksums: Vec::new(),
        }
    }
    
    pub fn update(&mut self, data: &[u8]) {
    }
}

impl Default for ChecksumState {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;
    
    #[test]
    fn test_save_and_load() {
        let temp_dir = TempDir::new().unwrap();
        let state_path = temp_dir.path().join("test.state");
        
        let config = TransferConfig {
            blocksize: 1024 * 1024,
            threads: 4,
            compress: true,
            cipher: "aes256-gcm".to_string(),
        };
        
        let original = TransferState::new(
            "/path/to/source",
            "user@host:/path/to/dest",
            1_000_000,
            config,
        );
        
        original.save(&state_path).unwrap();
        assert!(state_path.exists());
        
        let loaded = TransferState::load(&state_path).unwrap();
        assert_eq!(original.session_id, loaded.session_id);
        assert_eq!(original.total_bytes, loaded.total_bytes);
    }
    
    #[test]
    fn test_progress_tracking() {
        let config = TransferConfig {
            blocksize: 1024,
            threads: 4,
            compress: false,
            cipher: "aes256-gcm".to_string(),
        };
        
        let mut state = TransferState::new(
            "/source",
            "host:/dest",
            10_000,
            config,
        );
        
        assert_eq!(state.progress_percentage(), 0.0);
        
        state.update_progress(5_000, None);
        assert_eq!(state.progress_percentage(), 50.0);
        
        state.update_progress(5_000, None);
        assert!(state.is_complete());
        assert_eq!(state.progress_percentage(), 100.0);
    }
}
