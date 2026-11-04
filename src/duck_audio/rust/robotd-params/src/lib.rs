//! vendored `sounds` crate 只用到 `robotd-params` 的一个 API：`serial::soc_serial()`。
//!
//! 真正的 `robotd-params` 带整套参数 schema；复制它会拖入 `kinematics`、`duck-ipc-proto`、
//! serde/toml 和 policy registry，而声音只需要 serial。这个兼容层只做一件事：给每只鸭子一个
//! 稳定、唯一的身份字符串。
//!
//! 选择顺序：
//!
//! 1. 有硬件 SoC serial 就用硬件值：Zero 3W 读 `/proc/device-tree/serial-number`，Beta 读
//!    `/proc/cpuinfo` 的 `Serial` 行（该板的 U-Boot device-tree 值是批次共用的，不能用）。
//! 2. 没有硬件 serial（笔记本、容器、模拟器、bootloader 不提供）时，生成一个随机值并保存到
//!    `$XDG_CONFIG_HOME/duck_ros2/serial_number`，没有 `XDG_CONFIG_HOME` 时用
//!    `$HOME/.config/duck_ros2/serial_number`。之后每次启动都读回同一个值。
//! 3. `DUCK_SERIAL_PATH` 可以覆盖本地文件路径；上游 `sounds::hardware_seed` 还支持
//!    `DUCK_IDENTITY`，它的优先级更高。
//!
//! 不再读取 `/etc/robot/robotd.toml`：board 只用于选择 serial 来源，从 device tree 的
//! `compatible` 检测即可；检测不到时按顺序尝试两个来源。真正的机器人有硬件 serial，本地
//! 文件只是开发/模拟环境的兜底。

pub mod serial {
    use std::fs::{self, OpenOptions};
    use std::io::{Read, Write};
    use std::path::{Path, PathBuf};

    const DEVICE_TREE_PATH: &str = "/proc/device-tree/serial-number";
    const COMPATIBLE_PATH: &str = "/proc/device-tree/compatible";
    const CPUINFO_PATH: &str = "/proc/cpuinfo";
    const LOCAL_DIR: &str = "duck_ros2";
    const LOCAL_FILE: &str = "serial_number";
    const RANDOM_PREFIX: &str = "local-";
    const RANDOM_BYTES: usize = 16;

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    enum Board {
        Zero3,
        Beta,
    }

    /// 本机身份字符串：优先硬件 SoC serial，没有时使用本地持久化的随机值。
    pub fn soc_serial() -> Option<String> {
        if let Some(serial) = hardware_serial() {
            return Some(serial);
        }
        local_serial_path().and_then(|path| local_serial_at(&path))
    }

    fn hardware_serial() -> Option<String> {
        match detect_board(Path::new(COMPATIBLE_PATH)) {
            Some(Board::Zero3) => device_tree_serial(Path::new(DEVICE_TREE_PATH)),
            Some(Board::Beta) => cpuinfo_serial(Path::new(CPUINFO_PATH)),
            // 没有 compatible 的机器（笔记本/容器）通常两个都没有；顺序保持 Zero 3W 的
            // device-tree 优先，找不到再尝试 kernel 的 Serial。
            None => device_tree_serial(Path::new(DEVICE_TREE_PATH))
                .or_else(|| cpuinfo_serial(Path::new(CPUINFO_PATH))),
        }
    }

    fn detect_board(compatible: &Path) -> Option<Board> {
        let raw = fs::read(compatible).ok()?;
        let mut found = None;
        for name in raw.split(|&b| b == 0) {
            let Ok(name) = std::str::from_utf8(name) else {
                continue;
            };
            if name.starts_with("pollen,microduck") || name.starts_with("seeed,microduck") {
                found = Some(Board::Beta);
                break;
            }
            if name.starts_with("radxa,zero3") || name.starts_with("radxa,zero-3") {
                found = Some(Board::Zero3);
                break;
            }
        }
        found
    }

    fn device_tree_serial(path: &Path) -> Option<String> {
        let raw = fs::read(path).ok()?;
        let text = String::from_utf8_lossy(&raw);
        // device-tree 属性以 NUL 结尾；第一个 NUL 即值的结束。
        valid(text.split('\0').next()?)
    }

    fn cpuinfo_serial(path: &Path) -> Option<String> {
        let text = fs::read_to_string(path).ok()?;
        let value = text.lines().find_map(|line| {
            let (key, value) = line.split_once(':')?;
            (key.trim() == "Serial").then_some(value)
        })?;
        // kernel 读不到 OTP 时打印全零；那是所有同类板，不是身份。
        valid(value).filter(|serial| serial.chars().any(|c| c != '0'))
    }

    /// 本地身份文件路径：`$XDG_CONFIG_HOME/duck_ros2/serial_number`，
    /// 否则 `$HOME/.config/duck_ros2/serial_number`；`DUCK_SERIAL_PATH` 优先。
    fn local_serial_path() -> Option<PathBuf> {
        if let Some(path) = std::env::var_os("DUCK_SERIAL_PATH")
            && !path.is_empty()
        {
            return Some(PathBuf::from(path));
        }
        let base = std::env::var_os("XDG_CONFIG_HOME")
            .filter(|value| !value.is_empty())
            .map(PathBuf::from)
            .or_else(|| {
                std::env::var_os("HOME")
                    .filter(|value| !value.is_empty())
                    .map(|home| PathBuf::from(home).join(".config"))
            })?;
        Some(base.join(LOCAL_DIR).join(LOCAL_FILE))
    }

    /// 读取或创建本地身份文件。并发调用时只有一个进程能 `create_new` 成功，其他进程读回
    /// 同一个值；失败返回 `None`，让 `hardware_seed` 使用 `/etc/machine-id` 或报错。
    fn local_serial_at(path: &Path) -> Option<String> {
        for _ in 0..3 {
            if let Some(existing) = read_local_serial(path) {
                return Some(existing);
            }
            let generated = generate_local_serial()?;
            if create_local_serial(path, &generated) {
                return Some(generated);
            }
            // 另一个进程刚刚创建了文件；下一轮读它。若文件损坏，删除后重试。
            let _ = fs::remove_file(path);
        }
        read_local_serial(path)
    }

    fn read_local_serial(path: &Path) -> Option<String> {
        let text = fs::read_to_string(path).ok()?;
        valid(text.trim())
    }

    fn create_local_serial(path: &Path, serial: &str) -> bool {
        let Some(parent) = path.parent() else {
            return false;
        };
        if fs::create_dir_all(parent).is_err() {
            return false;
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let _ = fs::set_permissions(parent, fs::Permissions::from_mode(0o700));
        }

        let mut options = OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        let Ok(mut file) = options.open(path) else {
            return false;
        };
        file.write_all(serial.as_bytes()).is_ok() && file.sync_all().is_ok()
    }

    fn generate_local_serial() -> Option<String> {
        let mut bytes = [0u8; RANDOM_BYTES];
        fs::File::open("/dev/urandom")
            .and_then(|mut file| file.read_exact(&mut bytes))
            .ok()?;
        let mut text = String::with_capacity(RANDOM_PREFIX.len() + RANDOM_BYTES * 2);
        text.push_str(RANDOM_PREFIX);
        for byte in bytes {
            use std::fmt::Write as _;
            let _ = write!(text, "{byte:02x}");
        }
        Some(text)
    }

    fn valid(value: &str) -> Option<String> {
        let serial = value.trim();
        (!serial.is_empty() && serial.len() <= 128 && serial.chars().all(|c| c.is_ascii_graphic()))
            .then(|| serial.to_owned())
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use std::time::{SystemTime, UNIX_EPOCH};

        fn temp_path() -> PathBuf {
            let nanos = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .expect("clock")
                .as_nanos();
            std::env::temp_dir()
                .join(format!("duck-serial-test-{}-{nanos}", std::process::id()))
                .join("serial_number")
        }

        #[test]
        fn generates_and_reuses_a_local_serial() {
            let path = temp_path();
            let first = local_serial_at(&path).expect("generated");
            let second = local_serial_at(&path).expect("reused");
            assert_eq!(first, second);
            assert!(first.starts_with(RANDOM_PREFIX));
            assert_eq!(first.len(), RANDOM_PREFIX.len() + RANDOM_BYTES * 2);
            let _ = fs::remove_dir_all(path.parent().expect("parent"));
        }

        #[test]
        fn an_existing_serial_wins() {
            let path = temp_path();
            fs::create_dir_all(path.parent().expect("parent")).expect("dir");
            fs::write(&path, "manual-serial-123\n").expect("write");
            assert_eq!(local_serial_at(&path).as_deref(), Some("manual-serial-123"));
            let _ = fs::remove_dir_all(path.parent().expect("parent"));
        }

        #[test]
        fn an_empty_file_is_regenerated() {
            let path = temp_path();
            fs::create_dir_all(path.parent().expect("parent")).expect("dir");
            fs::write(&path, "").expect("write");
            let serial = local_serial_at(&path).expect("regenerated");
            assert!(serial.starts_with(RANDOM_PREFIX));
            let _ = fs::remove_dir_all(path.parent().expect("parent"));
        }
    }
}
