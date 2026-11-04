# duck_audio：Rust FFI 边界

> 目标：C++23 的 `duck_audio` 在进程内调用原版 Rust `sounds` crate，复用 DSP、配方、乐谱与节拍算法。
> 相关：[duck_audio 架构](duck-ros2-architecture.zh-CN.md)、[Gemini 特雷门](duck-ros2-theremin-gemini.zh-CN.md)。
> 结论先说：**FFI 暴露实时合成、乐谱/节拍和 `duck_render_all`；节点启动时自动检查并生成音库，一次性音效仍走 `AudioClip` 文件。CLI `sounds ensure-bank` 保留作手动修复工具。**

---

## 0. 复用范围

| 能力 | 放在哪 | 理由 |
|---|---|---|
| 音库渲染 7 tag × 变体 | 节点启动时 FFI `duck_render_all`；`sounds ensure-bank` 作 CLI 备用 | 首次启动或版本失配时生成；C++ 不需要合成一次性叫声 |
| 一次性音效播放 | C++ `duck_audio` → `audio_io` `AudioClip` | 只是挑 WAV、发 URI；没有 DSP |
| `wheee` start/loop/end | C++ 读音库 WAV → `AudioStream` | 音频已由 Rust 渲染；C++ 只做 PCM 拼接与 hold 状态 |
| `Personality` / seed / pitch center | Rust FFI | 身份必须与音库一致；C++ 不复制 21 个字段的派生逻辑 |
| 特雷门 `Stream::wheee` | Rust FFI | 实时合成；C++ 只提供 `hz/level/open` |
| 合唱 `Score`/`cast`/`Stream::choral` | Rust FFI | 乐谱语法、tie 合并、声部编排、beat 同步都已有测试 |
| `Conductor` / `Follower` | Rust FFI | 相位平均、回绕、离群值处理不宜重写 |
| 点云/深度 → `closeness` | C++ | ROS 传感器接入与 TF；Rust 不应依赖 ROS |
| `AudioStream` 发布、DDS、线程 | C++ | ROS 2 的职责 |

**不要**为了“少一层 FFI”把 `Personality` 的派生或 `Stream` 的谐波权重在 C++ 重写。那会把两只鸭子的声音身份、音库兼容性和已有测试全部复制一遍，而且第一处浮点差异就会让实时声音与音库不是同一只鸭子。

---

## 1. 方案比较

| 方案 | 评价 |
|---|---|
| **C++ 节点 + Rust C ABI（采用）** | 进程内、无序列化、延迟最低；ROS 侧语言统一；FFI 面可控 |
| Rust `rclrs` 节点 | 少一层 FFI，但维护第二套 ROS 工具链，且与现有 C++ `audio_io` 生态割裂 |
| Rust sidecar + Unix socket/共享内存 | 崩溃隔离好，但 10 ms PCM 与参数更新多一层 IPC；对特雷门延迟不利 |
| C++ 重写 DSP | 无 FFI，但重复实现、重复测试、音色漂移；不支持 |

FFI 的主要成本是**ABI/生命周期/panic 纪律**，不是性能。把接口设计成“少数 opaque 句柄 + 纯数据 struct + 状态码”，成本可控。

---

## 2. Rust 侧组织

### 2.1 目录

```text
src/duck_audio/rust/
├── Cargo.toml                 # workspace
├── Cargo.lock                 # 提交，--locked
├── rust-toolchain.toml
├── sounds/                    # 原版 crate，固定 commit 的副本/submodule
├── robotd-params/             # 只实现 serial::soc_serial 的最小兼容层
└── microduck_sounds_ffi/
    ├── Cargo.toml
    └── src/lib.rs
```

### 2.2 为什么不能直接 `path = "../../../microduck/sounds"`

原版 `sounds/Cargo.toml` 使用 workspace 继承：

```toml
version.workspace = true
edition.workspace = true
rust-version.workspace = true
license.workspace = true

[dependencies]
clap = { workspace = true, features = ["derive"] }
hound.workspace = true
```

它必须在原 workspace 里才能解析。把 `sounds` 作为外部 path dependency 会依赖另一个 workspace 的根，CI/交叉编译/发布都不可复现。正确做法是把 `sounds` 作为本仓库内的副本或 submodule，并在 `rust/Cargo.toml` 提供一个本地 workspace。

### 2.3 workspace

```toml
# src/duck_audio/rust/Cargo.toml
[workspace]
resolver = "2"
members = ["sounds", "robotd-params", "microduck_sounds_ffi"]

[workspace.package]
version = "0.1.0"
edition = "2024"
rust-version = "1.89"
license = "Apache-2.0"

[workspace.dependencies]
clap = { version = "4", features = ["derive"] }
hound = "3.5"

[profile.release]
# catch_unwind 需要 unwind；不要为了体积改成 abort。
panic = "unwind"
```

```toml
# src/duck_audio/rust/microduck_sounds_ffi/Cargo.toml
[package]
name = "microduck_sounds_ffi"
version.workspace = true
edition.workspace = true
rust-version.workspace = true
license.workspace = true

[lib]
name = "microduck_sounds"          # 产物 libmicroduck_sounds.so / libmicroduck_sounds.a
crate-type = ["cdylib", "staticlib"]

[dependencies]
sounds = { path = "../sounds" }
```

`sounds` 的 `src/main.rs` 仍会被 workspace 识别，但 `cargo build -p microduck_sounds_ffi` 只构建 FFI 库；CLI 用 `cargo build -p sounds --bin sounds` 单独构建。若希望去掉 FFI 库对 `clap` 的编译依赖，可在 `sounds` 中把 CLI 依赖改成 optional feature（非必需，先不引入差异）。

### 2.4 版本与音库

- `sounds::BANK_VERSION` 必须通过 FFI 暴露。
- `sounds` 的 `Cargo.lock` 提交；升级 `sounds` 时同时 bump 音库版本并重渲染。
- 原版 `sounds` 的评分/乐谱资源（`scores/*.mid`、`scores/*.duckscore`）随 crate 一起复制；否则 `Score::duck_strut()` 会在运行时 panic。
- 上游最新 `sounds` 通过 `robotd-params::serial::soc_serial()` 读取身份字符串。本仓库的兼容层顺序是：硬件 SoC serial → 本地持久随机值。没有硬件 serial 时生成 `local-<32 hex>`，保存到 `$XDG_CONFIG_HOME/duck_ros2/serial_number`（否则 `$HOME/.config/duck_ros2/serial_number`），`DUCK_SERIAL_PATH` 可覆盖路径；不再读取 `/etc/robot/robotd.toml`。文件以 0600 创建，目录 0700，`create_new` 保证并发进程得到同一个值。
- 完整 `robotd-params` 会拖入参数 schema、`kinematics` 和 `duck-ipc-proto`；兼容层只实现 serial。若以后完整 vendor，替换该兼容层即可。

### 2.5 同步与构建脚本

```bash
# 拉取固定 commit 的 sounds/；--latest 先解析 origin/HEAD 再拉取
src/duck_audio/tools/sync_microduck_sounds.sh
src/duck_audio/tools/sync_microduck_sounds.sh --latest

# 构建 libmicroduck_sounds 并跑 C++ 冒烟测试
src/duck_audio/tools/build_ffi.sh
```

脚本只复制上游 `sounds/`，不会覆盖 `microduck_sounds_ffi/`、`robotd-params/` 或本地测试。

---

## 3. ABI 设计原则

1. **只用 C 兼容类型**：`uint8_t/uint32_t/uint64_t/int32_t/size_t/double/float/char*`；不把 `String`、`Vec`、`Option`、Rust enum 直接跨边界。
2. **所有导出 struct 是 `#[repr(C)]`**，字段顺序在 Rust 与 C 头文件中一致；C++ 侧用 `static_assert`/`offsetof` 测试。
3. **opaque 句柄**：`DuckVoice`、`DuckStream`、`DuckScore`、`DuckConductor`、`DuckFollower` 在头文件里只有前置声明。
4. **Rust 分配，Rust 释放**：句柄有对应的 `*_free`；`duck_voice_render` 的 `float*` 用 `duck_buffer_free(data, frames)` 释放，`frames` 必须与返回时一致。
5. **不抛异常、不 panic 穿过 C**：每个入口 `catch_unwind`；失败返回状态码 + 错误缓冲。
6. **不注册回调**：C++ 拉数据（`block`），Rust 不反向调用 C++，避免线程/生命周期纠缠。
7. **线程模型显式**：`DuckStream` 只能由一个 writer 线程使用；`DuckScore` 创建后只读；`DuckVoice` 只读。
8. **版本可检查**：`duck_abi_version()` 与头文件宏一致；不匹配拒绝启用实时声音。

---

## 4. C 头文件

以下为 `include/duck_audio/ffi/microduck_sounds.h` 的契约。实现可以手写，也可以用 cbindgen 生成后审阅；**提交的是审阅过的头文件**，不要让构建时生成物成为唯一真相。

```c
#ifndef MICRODUCK_SOUNDS_H
#define MICRODUCK_SOUNDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICRODUCK_SOUNDS_ABI_VERSION 1

typedef struct DuckVoice DuckVoice;
typedef struct DuckStream DuckStream;
typedef struct DuckScore DuckScore;
typedef struct DuckConductor DuckConductor;
typedef struct DuckFollower DuckFollower;

typedef enum DuckStatus {
    DUCK_OK = 0,
    DUCK_ERR_INVALID_ARGUMENT = 1,
    DUCK_ERR_BUFFER_TOO_SMALL = 2,
    DUCK_ERR_PARSE = 3,
    DUCK_ERR_IO = 4,
    DUCK_ERR_UNKNOWN_TAG = 5,
    DUCK_ERR_PANIC = 6,
    DUCK_ERR_UNSUPPORTED = 7
} DuckStatus;

typedef enum DuckPart {
    DUCK_PART_BASS = 0,
    DUCK_PART_TENOR = 1,
    DUCK_PART_ALTO = 2,
    DUCK_PART_SOPRANO = 3
} DuckPart;

typedef enum DuckVowel {
    DUCK_VOWEL_AH = 0,
    DUCK_VOWEL_EH = 1,
    DUCK_VOWEL_EE = 2,
    DUCK_VOWEL_OH = 3,
    DUCK_VOWEL_OO = 4,
    DUCK_VOWEL_MM = 5
} DuckVowel;

typedef struct DuckPersonality {
    uint32_t seed;
    uint32_t formant_n;
    double pitch_center_hz;
    double register_value;      /* C++ 的 register 是关键字 */
    double pitch_spread;
    double glide_bias;
    double brightness;
    double tilt;
    double nasal;
    double harmonic_skew;
    double formant_gain;
    double vibrato_rate_hz;
    double vibrato_depth;
    double jitter_depth;
    double breath;
    double quackiness;
    double am_rate_hz;
    double am_depth;
    double warble_hz;
    double warble_depth;
    double attack_sharpness;
    double speed;
} DuckPersonality;

typedef struct DuckNote {
    int32_t part;       /* DuckPart */
    int32_t vowel;      /* DuckVowel */
    uint8_t midi;
    double start_beat;
    double beats;
    double level;
} DuckNote;

typedef struct DuckSinger {
    uint32_t seed;
    int32_t part;       /* DuckPart */
    double pitch_center_hz;
    double detune_cents;
    double onset_offset_s;
} DuckSinger;

uint32_t duck_abi_version(void);
uint32_t duck_bank_version(void);
const char *duck_status_str(DuckStatus status);
const char *duck_last_error(void);

DuckStatus duck_hardware_seed(uint32_t *out, char *err, size_t err_cap);
DuckStatus duck_seed_from_id(const char *id, uint32_t *out,
                             char *err, size_t err_cap);

DuckVoice *duck_voice_new(uint32_t seed);
void duck_voice_free(DuckVoice *voice);
DuckStatus duck_voice_personality(const DuckVoice *voice, DuckPersonality *out);

/* 测试/工具用；生产音效走银行。out 由 Rust 分配，用 duck_buffer_free(out, *frames) 释放。 */
DuckStatus duck_voice_render(const DuckVoice *voice, const char *tag,
                             uint32_t variant, float **out, size_t *frames,
                             char *err, size_t err_cap);
void duck_buffer_free(float *data, size_t frames);

DuckStatus duck_render_tag(uint32_t seed, const char *tag, uint32_t variant,
                           const char *path, char *err, size_t err_cap);
DuckStatus duck_render_all(uint32_t seed, const char *dir,
                           char *err, size_t err_cap);

DuckStream *duck_stream_new(const DuckVoice *voice, const char *tag,
                            uint32_t variant);
DuckStream *duck_stream_new_wheee(const DuckVoice *voice, uint32_t variant);
DuckStream *duck_stream_new_choral(const DuckVoice *voice, DuckPart part);
void duck_stream_free(DuckStream *stream);

DuckStatus duck_stream_set(DuckStream *stream, double hz, double level,
                           double open);
DuckStatus duck_stream_set_level(DuckStream *stream, double level);
DuckStatus duck_stream_set_formant_shift(DuckStream *stream,
                                         double harmonics);
DuckStatus duck_stream_set_speaker_rolloff(DuckStream *stream, double hz);

/* out 必须对齐到 float；frames 为样本数；成功返回 DUCK_OK。 */
DuckStatus duck_stream_block(DuckStream *stream, float *out, size_t frames);
int duck_stream_is_silent(const DuckStream *stream);

double duck_hz_at(const DuckVoice *voice, double position);
void duck_range_hz(const DuckVoice *voice, double *lo, double *hi);

DuckScore *duck_score_wistful(char *err, size_t err_cap);
DuckScore *duck_score_duck_strut(char *err, size_t err_cap);
DuckStatus duck_score_from_text(const char *utf8, size_t len,
                                DuckScore **out, char *err, size_t err_cap);
DuckStatus duck_score_from_midi(const uint8_t *bytes, size_t len,
                                DuckScore **out, char *err, size_t err_cap);
void duck_score_free(DuckScore *score);

const char *duck_score_name(const DuckScore *score); /* 借用，score 释放前有效 */
size_t duck_score_note_count(const DuckScore *score);
DuckStatus duck_score_note(const DuckScore *score, size_t index, DuckNote *out);
double duck_score_beat_s(const DuckScore *score);
double duck_score_duration_s(const DuckScore *score);
uint32_t duck_score_parts(const DuckScore *score); /* bit 0..3 = Part */

DuckStatus duck_cast(const uint32_t *seeds, size_t count,
                     DuckSinger *out, size_t out_cap);
DuckStatus duck_seat_all(const double *registers, size_t count,
                         int32_t *parts_out, size_t out_cap);
DuckStatus duck_seat_for(const DuckVoice *voice, DuckPart part,
                         DuckSinger *out);

DuckConductor *duck_conductor_new(double bpm, double now_s);
void duck_conductor_free(DuckConductor *conductor);
int duck_conductor_due(DuckConductor *conductor, double now_s,
                       uint64_t *beat_out);
double duck_conductor_position_beats(const DuckConductor *conductor, double now_s);
uint8_t duck_conductor_wire_beat(const DuckConductor *conductor);

DuckFollower *duck_follower_new(double bpm);
void duck_follower_free(DuckFollower *follower);
void duck_follower_observe(DuckFollower *follower, uint8_t beat, double at_s);
int duck_follower_locked(const DuckFollower *follower);
int duck_follower_position_beats(const DuckFollower *follower, double now_s,
                                 double *out);
int duck_follower_spread_s(const DuckFollower *follower, double *out);

#ifdef __cplusplus
}
#endif

#endif
```

约定：

- `err` 为 UTF-8、NUL 结尾；`err_cap == 0` 或 `err == NULL` 表示不需要信息。
- `duck_last_error()` 是线程局部缓冲，下一次同线程 FFI 调用可能覆盖；只用于构造失败时取原因。
- `int` 返回值只表示 1/0，不映射 `bool`。
- `duck_score_name` 返回借用指针，`DuckScore` 释放后失效。

---

## 5. Rust 包装骨架

### 5.1 opaque 句柄

```rust
use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use sounds::{Personality, Stream};

pub struct DuckVoice { p: Personality }
pub struct DuckStream { s: Stream }
pub struct DuckScore { score: sounds::chorale::Score }
pub struct DuckConductor { c: sounds::chorale::beat::Conductor }
pub struct DuckFollower { f: sounds::chorale::beat::Follower }
```

### 5.2 错误与 panic

```rust
fn set_last_error(message: &str) { /* thread_local CString */ }

fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|_| {
        set_last_error("rust panic caught at FFI boundary");
        fallback
    })
}

fn write_err(err: *mut c_char, cap: usize, message: &str) {
    if err.is_null() || cap == 0 { return; }
    let bytes = message.as_bytes();
    let n = bytes.len().min(cap - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), err.cast::<u8>(), n);
        *err.add(n) = 0;
    }
}
```

要求：

- **每个 `extern "C"` 入口都包 `guard`**，包括 `free`（`drop` 理论上不 panic，但保持统一）。
- `release` profile 必须是 `panic = "unwind"`；`panic = "abort"` 会让 `catch_unwind` 失效。
- 不要用 `Box::leak` 以外的方式转移所有权；句柄由 `Box::into_raw` 创建，`Box::from_raw` 释放。
- `unsafe` 操作集中、逐条注释；`#![deny(unsafe_op_in_unsafe_fn)]`。

### 5.3 参数校验

`Stream::set` 会把 `hz` clamp 到 30..16000，但 **NaN 不会被拒绝**；NaN 进入相位会污染整条流。FFI 必须在入口拒绝：

```rust
fn finite3(a: f64, b: f64, c: f64) -> bool {
    a.is_finite() && b.is_finite() && c.is_finite()
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_set(
    stream: *mut DuckStream, hz: f64, level: f64, open: f64,
) -> i32 {
    guard(DUCK_ERR_PANIC as i32, || {
        let Some(stream) = (unsafe { stream.as_mut() }) else {
            return DUCK_ERR_INVALID_ARGUMENT as i32;
        };
        if !finite3(hz, level, open) {
            return DUCK_ERR_INVALID_ARGUMENT as i32;
        }
        stream.s.set(hz, level, open);
        DUCK_OK as i32
    })
}
```

同样校验 `level`、`formant_shift`、`rolloff`、beats、score 索引、指针 + 长度。

### 5.4 句柄构造/释放

```rust
#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_new(seed: u32) -> *mut DuckVoice {
    Box::into_raw(Box::new(DuckVoice { p: Personality::from_seed(seed) }))
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_free(voice: *mut DuckVoice) {
    if !voice.is_null() {
        drop(unsafe { Box::from_raw(voice) });
    }
}
```

`duck_stream_new_wheee` 同理：`Stream::wheee(&voice.p, variant)`。`Stream` 不是线程安全的，头文件要写明“一个 stream 只能由一个线程调用 `block/set`”。

### 5.5 `block`

```rust
#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_block(
    stream: *mut DuckStream, out: *mut f32, frames: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC as i32, || {
        let Some(stream) = (unsafe { stream.as_mut() }) else {
            return DUCK_ERR_INVALID_ARGUMENT as i32;
        };
        if frames == 0 { return DUCK_OK as i32; }
        if out.is_null() { return DUCK_ERR_INVALID_ARGUMENT as i32; }
        let out = unsafe { std::slice::from_raw_parts_mut(out, frames) };
        stream.s.block(out);
        DUCK_OK as i32
    })
}
```

`Stream::block` 不按样本分配内存；权重刷新（约每 256 样本）会重建一个 7 元素的 `Vec`，量很小。FFI 每块只做一次 slice 构造，适合 100 Hz。

`duck_voice_render` 返回的是 Rust 分配的 `Box<[f32]>`，不能用 `free()`；实现必须保留长度：

```rust
#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_render(
    voice: *const DuckVoice, tag: *const c_char, variant: u32,
    out: *mut *mut f32, frames: *mut usize, err: *mut c_char, cap: usize,
) -> i32 {
    // ... 校验指针/UTF-8/tag，调用 sounds::render
    let mut buffer = match sounds::render(tag_str, &voice.p, variant) {
        Ok(buffer) => buffer,
        Err(e) => { write_err(err, cap, &e.to_string()); return DUCK_ERR_PARSE as i32; }
    };
    let boxed = buffer.into_boxed_slice();
    unsafe {
        *frames = boxed.len();
        *out = Box::into_raw(boxed).cast::<f32>();
    }
    DUCK_OK as i32
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_buffer_free(data: *mut f32, frames: usize) {
    if data.is_null() || frames == 0 { return; }
    unsafe {
        let slice = std::ptr::slice_from_raw_parts_mut(data, frames);
        drop(Box::from_raw(slice));
    }
}
```

不要用 `Vec::from_raw_parts(data, frames, frames)` 除非能保证 capacity == frames；`into_boxed_slice()` 把容量收成恰好长度，释放才安全。

### 5.6 音高映射

```rust
#[unsafe(no_mangle)]
pub extern "C" fn duck_hz_at(voice: *const DuckVoice, position: f64) -> f64 {
    let Some(voice) = (unsafe { voice.as_ref() }) else { return 0.0; };
    if !position.is_finite() { return 0.0; }
    sounds::stream::hz_at(&voice.p, position)
}
```

C++ 不应复制 `lo * (hi/lo)^position`；这是原版行为的一部分。

### 5.7 `Score`/`cast`/`Beat`

```rust
#[unsafe(no_mangle)]
pub extern "C" fn duck_score_from_text(
    utf8: *const c_char, len: usize, out: *mut *mut DuckScore,
    err: *mut c_char, cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC as i32, || {
        if utf8.is_null() || out.is_null() {
            write_err(err, cap, "null argument");
            return DUCK_ERR_INVALID_ARGUMENT as i32;
        }
        let bytes = unsafe { std::slice::from_raw_parts(utf8.cast::<u8>(), len) };
        let Ok(text) = std::str::from_utf8(bytes) else {
            write_err(err, cap, "score is not UTF-8");
            return DUCK_ERR_PARSE as i32;
        };
        match sounds::chorale::text::parse(text) {
            Ok(score) => {
                unsafe { *out = Box::into_raw(Box::new(DuckScore { score })); }
                DUCK_OK as i32
            }
            Err(e) => {
                write_err(err, cap, &e.to_string());
                DUCK_ERR_PARSE as i32
            }
        }
    })
}
```

`duck_cast`：

```rust
let seeds = unsafe { std::slice::from_raw_parts(seeds, count) };
let personalities: Vec<Personality> = seeds.iter().copied().map(Personality::from_seed).collect();
let singers = sounds::chorale::cast(&personalities);
```

`duck_seat_all` 直接调用原版；`duck_seat_for` 用 `seat_for(&voice.p, part.into())`。

`Conductor`/`Follower` 只是方法转发；`due` 返回 1/0，`position_beats` 写入 out。

---

## 6. 所有权限定

| 句柄 | 所有权 | 线程 |
|---|---|---|
| `DuckVoice` | C++ 持有，`duck_voice_free` | 只读，可跨线程共享（不修改） |
| `DuckStream` | C++ 持有，`duck_stream_free` | **单线程**：一个 writer 线程调用 `set/block` |
| `DuckScore` | C++ 持有，`duck_score_free` | 创建后只读；可被多个读者共享 |
| `DuckConductor` | C++ 持有 | 单线程（控制 tick） |
| `DuckFollower` | C++ 持有 | 单线程（控制 tick） |
| `duck_voice_render` 的 `float*` | Rust 分配 | C++ 用 `duck_buffer_free(data, frames)` 释放，`frames` 必须一致 |
| `duck_score_name` | Rust 借用 | `DuckScore` 释放前有效 |

C++ 用 RAII 包住：

```cpp
struct DuckVoiceDeleter {
    void operator()(DuckVoice* p) const noexcept { duck_voice_free(p); }
};
using DuckVoicePtr = std::unique_ptr<DuckVoice, DuckVoiceDeleter>;

struct DuckStreamDeleter {
    void operator()(DuckStream* p) const noexcept { duck_stream_free(p); }
};
using DuckStreamPtr = std::unique_ptr<DuckStream, DuckStreamDeleter>;
```

不要在 `DuckStream` 仍被 writer 线程使用时释放；先 stop/join，再 reset。

---

## 7. C++ 调用示例

```cpp
DuckVoicePtr voice{duck_voice_new(seed)};
if (!voice) { /* error */ }

DuckPersonality p{};
if (duck_voice_personality(voice.get(), &p) != DUCK_OK) { /* error */ }

DuckStreamPtr stream{duck_stream_new_wheee(voice.get(), 0)};
if (!stream) { /* error */ }

constexpr std::size_t kFrames = 480; // 10 ms @48 kHz
std::array<float, kFrames> block{};
if (duck_stream_set(stream.get(), 320.0, 1.0, 0.5) != DUCK_OK) { /* error */ }
if (duck_stream_block(stream.get(), block.data(), block.size()) != DUCK_OK) { /* error */ }

for (float& sample : block) {
    const auto pcm = static_cast<int16_t>(
        std::lround(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
    // append little-endian pcm to AudioStream.data
}
```

启动时：

```cpp
if (duck_abi_version() != MICRODUCK_SOUNDS_ABI_VERSION) {
    throw std::runtime_error("microduck_sounds ABI mismatch");
}
```

`duck_stream_set` 的 `level` 用 0/1；淡出由 `Stream` 的 30 ms slew 完成，不要在外层做逐样本斜坡，否则会二次平滑。

---

## 8. 构建

### 8.1 Cargo

```bash
cd src/duck_audio/rust
cargo test --locked -p sounds
cargo build --release --locked -p microduck_sounds_ffi
# 产物：target/release/libmicroduck_sounds.so / libmicroduck_sounds.a
```

`cargo test -p sounds` 会跑原版的 RNG、配方、`Stream`、合唱测试；FFI 只做转发，原版测试仍是最强保障。

### 8.2 CMake

`microduck_sounds_ffi/build.rs` 为 cdylib 写入 `SONAME=libmicroduck_sounds.so`；没有它，CMake 会把导入库的构建路径写进 `DT_NEEDED`，安装后找不到库。

```cmake
option(DUCK_AUDIO_BUILD_RUST "Build libmicroduck_sounds from Rust" ON)
option(DUCK_AUDIO_USE_PREBUILT_RUST "Use prebuilt libmicroduck_sounds" OFF)

set(DUCK_RUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust")
set(DUCK_RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
set(DUCK_RUST_LIB "${DUCK_RUST_TARGET_DIR}/release/libmicroduck_sounds.so")

if (DUCK_AUDIO_USE_PREBUILT_RUST)
    find_library(DUCK_VOICE_LIBRARY NAMES microduck_sounds REQUIRED)
    set(DUCK_RUST_LIB "${DUCK_VOICE_LIBRARY}")
    add_library(microduck_sounds SHARED IMPORTED)
    set_target_properties(microduck_sounds PROPERTIES
        IMPORTED_LOCATION "${DUCK_VOICE_LIBRARY}")
elseif (DUCK_AUDIO_BUILD_RUST)
    file(GLOB_RECURSE DUCK_RUST_SOURCES CONFIGURE_DEPENDS
        "${DUCK_RUST_DIR}/sounds/src/*.rs"
        "${DUCK_RUST_DIR}/microduck_sounds_ffi/src/*.rs")
    add_custom_command(
        OUTPUT "${DUCK_RUST_LIB}"
        COMMAND ${CMAKE_COMMAND} -E env
                "CARGO_TARGET_DIR=${DUCK_RUST_TARGET_DIR}"
                cargo build --release --locked -p microduck_sounds_ffi
        WORKING_DIRECTORY "${DUCK_RUST_DIR}"
        DEPENDS ${DUCK_RUST_SOURCES}
                "${DUCK_RUST_DIR}/Cargo.toml"
                "${DUCK_RUST_DIR}/Cargo.lock"
        COMMENT "Building Rust voice FFI"
        VERBATIM)
    add_custom_target(microduck_sounds_rust ALL DEPENDS "${DUCK_RUST_LIB}")
    add_library(microduck_sounds SHARED IMPORTED)
    set_target_properties(microduck_sounds PROPERTIES
        IMPORTED_LOCATION "${DUCK_RUST_LIB}")
    add_dependencies(duck_audio_node microduck_sounds_rust)
endif()

target_include_directories(duck_audio_node PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(duck_audio_node microduck_sounds)

install(FILES "${DUCK_RUST_LIB}" DESTINATION lib)
set_target_properties(duck_audio_node PROPERTIES
    INSTALL_RPATH "$ORIGIN/..")
```

要点：

- `BYPRODUCTS`/`OUTPUT` 必须和 `CARGO_TARGET_DIR` 下真实路径一致；否则 Ninja 每次重跑或找不到文件。
- `DEPENDS` 用 `CONFIGURE_DEPENDS` 的源码列表 + `Cargo.toml/Cargo.lock`；不要只依赖目录 mtime。
- 安装 `.so` 到 `lib/`，节点安装到 `lib/duck_audio/`，设置 `$ORIGIN/..`，不要要求用户设 `LD_LIBRARY_PATH`。
- 交叉编译时把 `cargo build` 加上 `--target ${DUCK_RUST_TRIPLE}`，输出路径变为 `${DUCK_RUST_TARGET_DIR}/${DUCK_RUST_TRIPLE}/release/libmicroduck_sounds.so`；并设置：

```cmake
set(ENV{CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER} aarch64-linux-gnu-gcc)
```

- 如果链接 `staticlib`，产物是 `libmicroduck_sounds.a`，C++ 需要 `-ldl -lpthread -lm`；`cdylib` 更简单，推荐默认。

### 8.3 版本与许可

- `rust-toolchain.toml` 固定 1.89；`Cargo.lock` 提交；CI 用 `--locked`。
- 保留 `sounds` 的 `LICENSE`（Apache-2.0）与 NOTICE；`duck_audio` 的 `package.xml` 声明。
- 升级 `sounds` 时：
  1. 跑原版 `cargo test -p sounds`；
  2. 若 `BANK_VERSION` 变化，安装脚本重渲染音库；
  3. 跑 FFI gtest；
  4. 用固定 seed 对比音库文件数量与 `Personality` 字段（不要求跨架构逐样本一致）。

---

## 9. 测试

### 9.1 C++ gtest

`rust/test/ffi_smoke.cpp` 至少覆盖：

| 测试 | 断言 |
|---|---|
| ABI 版本 | `duck_abi_version() == MICRODUCK_SOUNDS_ABI_VERSION` |
| 身份稳定 | 同 seed 两次 `DuckPersonality` 逐字段相等；不同 seed 不同 |
| 音高映射 | `duck_hz_at(0/1)` 等于 `duck_range_hz` 的端点；越界钳制 |
| stream 块 | 480/4800 样本有限、\|x\|≤1；切块与整块等价（同 seed 两个 stream） |
| 非法参数 | NaN/Inf、空指针、`frames` 过大 → `DUCK_ERR_INVALID_ARGUMENT` |
| 音库版本 | `duck_bank_version()` 与 `.seed` 一致 |
| score | `duck_score_wistful` note count/parts 正确；文本解析错误带行号 |
| cast | 最低 pitch 的 seed 唱 bass；输入顺序不影响结果 |
| beat | conductor 5 秒 5 拍；follower 4 拍锁定 |

### 9.2 边界检查

- `nm -D --defined-only target/release/libmicroduck_sounds.so | grep '^.* duck_'`：确认导出符号。
- `ldd libmicroduck_sounds.so`：只应依赖 libc/libm/libgcc/libpthread/libdl 等，不依赖另一份 Rust 运行时。
- `valgrind --leak-check=full target/release/ffi_smoke`；或 ASan 构建 C++ 侧。
- `static_assert(offsetof(DuckPersonality, pitch_center_hz) == ...)` 不跨平台硬编码；用 `sizeof`/字段顺序测试即可。
- `cargo clippy --all-targets -- -D warnings`；`unsafe` 入口加注释。

### 9.3 常见坑

| 坑 | 症状 | 处理 |
|---|---|---|
| `sounds` 没有 workspace 字段 | `failed to load manifest ... workspace` | 用本地 workspace + `[workspace.package]`/`[workspace.dependencies]` |
| `panic=abort` | FFI 直接 abort 进程 | release 保持 `unwind` |
| NaN 进入 `duck_stream_set` | 输出 NaN、静音或爆音 | 入口 `is_finite` 校验 |
| `DuckStream` 被多线程使用 | 数据竞争/相位错乱 | 一个 writer 线程；stop/join 后释放 |
| 返回值用 `bool` | ABI 大小/值不稳定 | 用 `int`/`uint8_t` |
| 释放后仍用 `duck_score_name` 指针 | use-after-free | 借用生命周期写进头文件 |
| C++ 复制 `duck_stream_block` 的 `float*` | 指针来自 Rust 临时 buffer | 只读本次 `out`；不要跨块保留 |
| 动态库找不到 | 启动 `error while loading shared libraries` | `INSTALL_RPATH "$ORIGIN/.."` |
| 交叉编译 linker 错 | `linker 'cc' not found` | 设置 `CARGO_TARGET_*_LINKER` |
| 音库版本不匹配 | 实时声音与音库不是同一只鸭子 | 启动校验 `duck_bank_version()` 与 `.seed` |

---

## 10. 最小可用顺序

1. 导出 `duck_abi_version`、`duck_voice_new/free`、`duck_voice_personality`。
2. 导出 `duck_stream_new_wheee`、`duck_stream_set`、`duck_stream_block`、`duck_hz_at`。
3. 写 C++ gtest，跑通 480 样本块。
4. 再导出 `Score`/`cast`/`Beat`，供合唱；不要一开始就把整个 `sounds` API 平铺到 C ABI。

FFI 面越小，ABI 越容易长期维护；一次性音效和 `wheee` 的音库读取不需要 FFI。
