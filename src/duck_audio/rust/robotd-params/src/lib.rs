//! vendored `sounds` crate 只用到 `robotd-params` 的一个 API：`serial::soc_serial()`。
//!
//! 真正的 `robotd-params` 带整套参数 schema；复制它会拖入 `kinematics`、`duck-ipc-proto`、
//! serde/toml 和 policy registry，而声音只需要 serial。这个兼容层复现与 voice seed 有关的
//! serial 选择规则：
//!
//! - Zero 3W：`/proc/device-tree/serial-number`（既有名字/声音的来源）。
//! - Beta：`/proc/cpuinfo` 的 `Serial` 行（该板的 U-Boot device-tree 值是批次共用的）。
//! - 先在 `/etc/robot/robotd.toml` 的 `board = "beta"` 声明；没有声明时从 device tree 的
//!   `compatible` 检测；默认 Zero 3W。
//!
//! 如果以后完整 vendor `robotd-params`，可以删掉这个兼容层；`sounds` 只依赖一个函数。

pub mod serial {
    use std::path::Path;

    const DEVICE_TREE_PATH: &str = "/proc/device-tree/serial-number";
    const COMPATIBLE_PATH: &str = "/proc/device-tree/compatible";
    const CPUINFO_PATH: &str = "/proc/cpuinfo";
    const CONFIG_PATH: &str = "/etc/robot/robotd.toml";

    #[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
    enum Board {
        #[default]
        Zero3,
        Beta,
    }

    /// 本机 SoC serial；没有时返回 `None`（笔记本、bootloader 未提供该属性的板、
    /// 或 kernel 不打印 `Serial` 的 beta）。
    pub fn soc_serial() -> Option<String> {
        match board() {
            Board::Zero3 => device_tree_serial(Path::new(DEVICE_TREE_PATH)),
            Board::Beta => cpuinfo_serial(Path::new(CPUINFO_PATH)),
        }
    }

    fn board() -> Board {
        // robotd 配置声明板型；只读一个 key，不需要整套 schema。
        if let Ok(text) = std::fs::read_to_string(CONFIG_PATH)
            && let Some(board) = declared_board(&text)
        {
            return board;
        }
        detect_board(Path::new(COMPATIBLE_PATH)).unwrap_or_default()
    }

    fn declared_board(text: &str) -> Option<Board> {
        for line in text.lines() {
            let line = line.split('#').next().unwrap_or("").trim();
            let Some((key, value)) = line.split_once('=') else {
                continue;
            };
            if key.trim() != "board" {
                continue;
            }
            let value = value.trim().trim_matches('"').trim_matches('\'');
            return match value {
                "beta" => Some(Board::Beta),
                "zero3" => Some(Board::Zero3),
                _ => None,
            };
        }
        None
    }

    fn detect_board(compatible: &Path) -> Option<Board> {
        let raw = std::fs::read(compatible).ok()?;
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
        let raw = std::fs::read(path).ok()?;
        let text = String::from_utf8_lossy(&raw);
        // device-tree 属性以 NUL 结尾；第一个 NUL 即值的结束。
        valid(text.split('\0').next()?)
    }

    fn cpuinfo_serial(path: &Path) -> Option<String> {
        let text = std::fs::read_to_string(path).ok()?;
        let value = text.lines().find_map(|line| {
            let (key, value) = line.split_once(':')?;
            (key.trim() == "Serial").then_some(value)
        })?;
        // kernel 读不到 OTP 时打印全零；那是所有同类板，不是身份。
        valid(value).filter(|serial| serial.chars().any(|c| c != '0'))
    }

    fn valid(value: &str) -> Option<String> {
        let serial = value.trim();
        (!serial.is_empty() && serial.chars().all(|c| c.is_ascii_graphic()))
            .then(|| serial.to_owned())
    }
}
