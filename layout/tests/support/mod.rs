
use std::path::{Path, PathBuf};

fn repository_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("layout crate must sit directly under the repository root")
        .to_path_buf()
}

pub struct TempDirectory(PathBuf);

impl TempDirectory {
    
    pub fn new(tag: &str) -> Self {
        let directory = repository_root()
            .join("workspace")
            .join("tmp")
            .join("layout-tests")
            .join(format!("{tag}_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&directory);
        std::fs::create_dir_all(&directory).expect("create test temporary directory");
        TempDirectory(directory)
    }

    pub fn dsn(&self) -> String {
        format!("shm://{}", self.0.join("store").display())
    }

    pub fn path(&self) -> &Path {
        &self.0
    }

    pub fn join(&self, name: &str) -> PathBuf {
        self.0.join(name)
    }
}

impl Drop for TempDirectory {
    fn drop(&mut self) {
        
        let store = self.0.join("store");
        for suffix in ["", ".garb", ".gen", ".sbo.data", ".sbo.head"] {
            let _ = std::fs::remove_file(format!("{}{suffix}", store.display()));
        }
        let _ = std::fs::remove_dir_all(&self.0);
    }
}
