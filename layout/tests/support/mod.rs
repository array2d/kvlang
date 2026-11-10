//! 测试用临时目录。
//!
//! **不要用 `/tmp`**：它是 tmpfs，跨进程、跨会话共享，且重启 pod 后消失。
//! 我们已经多次踩到「另一个会话的残留目录让本仓测试红/绿颠倒」——
//! 一次串行跑出 8 条红、清掉 `/tmp/kvlanglayout_test_*` 后全绿。
//!
//! 统一放 `<repo>/workspace/tmp/layout-tests/<tag>/`：
//!   - `/home/` 跨 pod 持久，不会因重启丢失上下文；
//!   - `workspace/` 已在 `.gitignore` 里，不会污染提交；
//!   - 每个 tag 一个子目录，测试之间互不干扰。
//!
//! 目录对象在 `Drop` 时递归删除；同时构造时先删一次同名旧目录，
//! 因为上一次异常退出可能留了残骸。

use std::path::{Path, PathBuf};

/// 仓库根：从 `CARGO_MANIFEST_DIR`（= `<repo>/layout`）上一级。
fn repository_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .expect("layout crate must sit directly under the repository root")
        .to_path_buf()
}

/// 一个独占的临时目录，离开作用域时递归删除。
pub struct TempDirectory(PathBuf);

impl TempDirectory {
    /// `tag` 必须在本文件内唯一（用测试名即可）。
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

    /// `shm://` DSN，直接喂给 `Kv::conn`。
    ///
    /// **唯一入口**：所有测试都走这里拿 DSN，不许各自拼。同一件事两个入口正是
    /// 本轮踩坑的原因——16 个测试文件各改一遍，漏一个就留下 `fs://` 死路径。
    ///
    /// 为什么不是 `fs://`：单库化后 `libkvspace` 的 `kvspace_dsn_path()` 只认
    /// `shm` scheme，其它一律返回 -1。而 shm 本身就是 file-backed mmap，持久化、
    /// 重开、崩溃恢复能力齐全——它只是**换了个后端**，不是降低覆盖。
    pub fn dsn(&self) -> String {
        format!("shm://{}", self.0.join("store").display())
    }

    pub fn path(&self) -> &Path {
        &self.0
    }

    /// 目录内的一个路径（不创建）。
    pub fn join(&self, name: &str) -> PathBuf {
        self.0.join(name)
    }
}

impl Drop for TempDirectory {
    fn drop(&mut self) {
        // shm store 是**五个文件**（`<path>`、`.garb`、`.gen`、`.sbo.data`、
        // `.sbo.head`），不是目录。它们都在本临时目录内，故 `remove_dir_all`
        // 一次覆盖；这里显式列出后缀只是为了把「shm 是多文件」这件事写在
        // 会读到的地方——**若是把 dsn 指到目录外，就必须逐个删**。
        let store = self.0.join("store");
        for suffix in ["", ".garb", ".gen", ".sbo.data", ".sbo.head"] {
            let _ = std::fs::remove_file(format!("{}{suffix}", store.display()));
        }
        let _ = std::fs::remove_dir_all(&self.0);
    }
}
