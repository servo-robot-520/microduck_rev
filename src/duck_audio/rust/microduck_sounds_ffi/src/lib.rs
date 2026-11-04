//! microduck `sounds` crate 的 C ABI。
//!
//! 边界刻意保持窄：不透明句柄 + 纯 C 结构体，没有回调，也没有 Rust 容器跨边界。
//! 每个入口都捕获 panic 并返回状态码；`DuckStream` 按约定只由一个 writer 线程驱动。

// C ABI 由 C/C++ 调用；Rust 的 unsafe 标记对调用方不可见，安全性由头文件契约和入口校验保证。
#![allow(clippy::not_unsafe_ptr_arg_deref)]

use std::cell::RefCell;
use std::ffi::{CStr, CString, c_char};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;

use sounds::chorale::beat::{Conductor, Follower};
use sounds::chorale::{self, Note, Part, Score, Singer, Vowel};
use sounds::{Personality, Stream};

pub const DUCK_OK: i32 = 0;
pub const DUCK_ERR_INVALID_ARGUMENT: i32 = 1;
pub const DUCK_ERR_BUFFER_TOO_SMALL: i32 = 2;
pub const DUCK_ERR_PARSE: i32 = 3;
pub const DUCK_ERR_IO: i32 = 4;
pub const DUCK_ERR_UNKNOWN_TAG: i32 = 5;
pub const DUCK_ERR_PANIC: i32 = 6;
pub const DUCK_ERR_UNSUPPORTED: i32 = 7;

pub const DUCK_PART_BASS: i32 = 0;
pub const DUCK_PART_TENOR: i32 = 1;
pub const DUCK_PART_ALTO: i32 = 2;
pub const DUCK_PART_SOPRANO: i32 = 3;

pub const DUCK_VOWEL_AH: i32 = 0;
pub const DUCK_VOWEL_EH: i32 = 1;
pub const DUCK_VOWEL_EE: i32 = 2;
pub const DUCK_VOWEL_OH: i32 = 3;
pub const DUCK_VOWEL_OO: i32 = 4;
pub const DUCK_VOWEL_MM: i32 = 5;

pub struct DuckVoice {
    p: Personality,
}

pub struct DuckStream {
    s: Stream,
}

pub struct DuckScore {
    score: Score,
    /// `duck_score_name` 返回 C 字符串；`Score::name` 是 `String`，没有 NUL 结尾，
    /// 因此这里保存一份同生命周期的 `CString`。
    name: CString,
}

pub struct DuckConductor {
    c: Conductor,
}

pub struct DuckFollower {
    f: Follower,
}

#[repr(C)]
pub struct DuckPersonality {
    seed: u32,
    formant_n: u32,
    pitch_center_hz: f64,
    register_value: f64,
    pitch_spread: f64,
    glide_bias: f64,
    brightness: f64,
    tilt: f64,
    nasal: f64,
    harmonic_skew: f64,
    formant_gain: f64,
    vibrato_rate_hz: f64,
    vibrato_depth: f64,
    jitter_depth: f64,
    breath: f64,
    quackiness: f64,
    am_rate_hz: f64,
    am_depth: f64,
    warble_hz: f64,
    warble_depth: f64,
    attack_sharpness: f64,
    speed: f64,
}

#[repr(C)]
pub struct DuckNote {
    part: i32,
    vowel: i32,
    midi: u8,
    start_beat: f64,
    beats: f64,
    level: f64,
}

#[repr(C)]
pub struct DuckSinger {
    seed: u32,
    part: i32,
    pitch_center_hz: f64,
    detune_cents: f64,
    onset_offset_s: f64,
}

thread_local! {
    static LAST_ERROR: RefCell<CString> = RefCell::new(CString::new("").expect("empty CString"));
}

fn set_last_error(message: &str) {
    let cleaned = message.replace('\0', " ");
    let value = CString::new(cleaned).unwrap_or_else(|_| CString::new("error").expect("static"));
    LAST_ERROR.with(|slot| *slot.borrow_mut() = value);
}

fn panic_message(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(text) = payload.downcast_ref::<&str>() {
        (*text).to_owned()
    } else if let Some(text) = payload.downcast_ref::<String>() {
        text.clone()
    } else {
        "unknown panic".to_owned()
    }
}

/// 所有 C 入口都经过这里；panic 绝不能穿过 ABI 栈展开。
fn guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or_else(|payload| {
        set_last_error(&format!(
            "panic caught at FFI boundary: {}",
            panic_message(&*payload)
        ));
        fallback
    })
}

fn write_err(err: *mut c_char, cap: usize, message: &str) {
    if err.is_null() || cap == 0 {
        return;
    }
    let bytes = message.as_bytes();
    let count = bytes.len().min(cap - 1);
    // 安全性：调用方提供 `cap` 字节的可写缓冲；`count < cap`。
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), err.cast::<u8>(), count);
        *err.add(count) = 0;
    }
}

fn fail(status: i32, message: &str, err: *mut c_char, cap: usize) -> i32 {
    set_last_error(message);
    write_err(err, cap, message);
    status
}

fn c_str<'a>(value: *const c_char) -> Option<&'a str> {
    if value.is_null() {
        return None;
    }
    // 安全性：按约定为非空、NUL 结尾的 C 字符串。
    let raw = unsafe { CStr::from_ptr(value) };
    raw.to_str().ok()
}

fn part_from(value: i32) -> Option<Part> {
    match value {
        DUCK_PART_BASS => Some(Part::Bass),
        DUCK_PART_TENOR => Some(Part::Tenor),
        DUCK_PART_ALTO => Some(Part::Alto),
        DUCK_PART_SOPRANO => Some(Part::Soprano),
        _ => None,
    }
}

fn part_to(value: Part) -> i32 {
    match value {
        Part::Bass => DUCK_PART_BASS,
        Part::Tenor => DUCK_PART_TENOR,
        Part::Alto => DUCK_PART_ALTO,
        Part::Soprano => DUCK_PART_SOPRANO,
    }
}

fn vowel_to(value: Vowel) -> i32 {
    match value {
        Vowel::Ah => DUCK_VOWEL_AH,
        Vowel::Eh => DUCK_VOWEL_EH,
        Vowel::Ee => DUCK_VOWEL_EE,
        Vowel::Oh => DUCK_VOWEL_OH,
        Vowel::Oo => DUCK_VOWEL_OO,
        Vowel::Mm => DUCK_VOWEL_MM,
    }
}

fn boxed_score(score: Score) -> *mut DuckScore {
    let name = CString::new(score.name.replace('\0', " "))
        .unwrap_or_else(|_| CString::new("score").expect("static"));
    Box::into_raw(Box::new(DuckScore { score, name }))
}

fn fill_singer(singer: &Singer) -> DuckSinger {
    DuckSinger {
        seed: singer.personality.seed,
        part: part_to(singer.part),
        pitch_center_hz: singer.personality.pitch_center_hz,
        detune_cents: singer.detune_cents,
        onset_offset_s: singer.onset_offset_s,
    }
}

fn fill_note(note: &Note) -> DuckNote {
    DuckNote {
        part: part_to(note.part),
        vowel: vowel_to(note.vowel),
        midi: note.midi,
        start_beat: note.start_beat,
        beats: note.beats,
        level: note.level,
    }
}

fn fill_personality(p: &Personality) -> DuckPersonality {
    DuckPersonality {
        seed: p.seed,
        formant_n: p.formant_n as u32,
        pitch_center_hz: p.pitch_center_hz,
        register_value: p.register,
        pitch_spread: p.pitch_spread,
        glide_bias: p.glide_bias,
        brightness: p.brightness,
        tilt: p.tilt,
        nasal: p.nasal,
        harmonic_skew: p.harmonic_skew,
        formant_gain: p.formant_gain,
        vibrato_rate_hz: p.vibrato_rate_hz,
        vibrato_depth: p.vibrato_depth,
        jitter_depth: p.jitter_depth,
        breath: p.breath,
        quackiness: p.quackiness,
        am_rate_hz: p.am_rate_hz,
        am_depth: p.am_depth,
        warble_hz: p.warble_hz,
        warble_depth: p.warble_depth,
        attack_sharpness: p.attack_sharpness,
        speed: p.speed,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_abi_version() -> u32 {
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_bank_version() -> u32 {
    sounds::BANK_VERSION
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_status_str(status: i32) -> *const c_char {
    match status {
        DUCK_OK => c"OK".as_ptr().cast(),
        DUCK_ERR_INVALID_ARGUMENT => c"invalid argument".as_ptr().cast(),
        DUCK_ERR_BUFFER_TOO_SMALL => c"buffer too small".as_ptr().cast(),
        DUCK_ERR_PARSE => c"parse error".as_ptr().cast(),
        DUCK_ERR_IO => c"I/O error".as_ptr().cast(),
        DUCK_ERR_UNKNOWN_TAG => c"unknown tag".as_ptr().cast(),
        DUCK_ERR_PANIC => c"panic caught".as_ptr().cast(),
        DUCK_ERR_UNSUPPORTED => c"unsupported".as_ptr().cast(),
        _ => c"unknown status".as_ptr().cast(),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_last_error() -> *const c_char {
    LAST_ERROR.with(|slot| slot.borrow().as_ptr())
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_hardware_seed(out: *mut u32, err: *mut c_char, err_cap: usize) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if out.is_null() {
            return fail(DUCK_ERR_INVALID_ARGUMENT, "null out", err, err_cap);
        }
        match sounds::hardware_seed() {
            Ok(seed) => {
                // 安全性：上面已检查非空。
                unsafe { *out = seed };
                DUCK_OK
            }
            Err(error) => fail(DUCK_ERR_IO, &error.to_string(), err, err_cap),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_seed_from_id(
    id: *const c_char,
    out: *mut u32,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if out.is_null() {
            return fail(DUCK_ERR_INVALID_ARGUMENT, "null out", err, err_cap);
        }
        let Some(id) = c_str(id) else {
            return fail(
                DUCK_ERR_INVALID_ARGUMENT,
                "null or invalid id",
                err,
                err_cap,
            );
        };
        // 安全性：上面已检查非空。
        unsafe { *out = sounds::seed_from_id(id) };
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_new(seed: u32) -> *mut DuckVoice {
    guard(ptr::null_mut(), || {
        Box::into_raw(Box::new(DuckVoice {
            p: Personality::from_seed(seed),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_free(voice: *mut DuckVoice) {
    if voice.is_null() {
        return;
    }
    guard((), || {
        // 安全性：由 duck_voice_new 创建，按约定只释放一次。
        drop(unsafe { Box::from_raw(voice) });
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_personality(
    voice: *const DuckVoice,
    out: *mut DuckPersonality,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if voice.is_null() || out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        // 安全性：out 非空且可写。
        unsafe { *out = fill_personality(&voice.p) };
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_voice_render(
    voice: *const DuckVoice,
    tag: *const c_char,
    variant: u32,
    out: *mut *mut f32,
    frames: *mut usize,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if voice.is_null() || out.is_null() || frames.is_null() {
            return fail(DUCK_ERR_INVALID_ARGUMENT, "null argument", err, err_cap);
        }
        let Some(tag) = c_str(tag) else {
            return fail(
                DUCK_ERR_INVALID_ARGUMENT,
                "null or invalid tag",
                err,
                err_cap,
            );
        };
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        match sounds::render(tag, &voice.p, variant) {
            Ok(buffer) => {
                let boxed = buffer.into_boxed_slice();
                let len = boxed.len();
                // 安全性：通过非空输出参数写入。
                unsafe {
                    *frames = len;
                    *out = Box::into_raw(boxed).cast::<f32>();
                }
                DUCK_OK
            }
            Err(error) => {
                let status = if error.to_string().contains("unknown tag") {
                    DUCK_ERR_UNKNOWN_TAG
                } else {
                    DUCK_ERR_IO
                };
                fail(status, &error.to_string(), err, err_cap)
            }
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_buffer_free(data: *mut f32, frames: usize) {
    if data.is_null() || frames == 0 {
        return;
    }
    guard((), || {
        // 安全性：由 duck_voice_render 分配为长度恰为 `frames` 的 boxed slice。
        unsafe {
            let slice = ptr::slice_from_raw_parts_mut(data, frames);
            drop(Box::from_raw(slice));
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_render_tag(
    seed: u32,
    tag: *const c_char,
    variant: u32,
    path: *const c_char,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        let (Some(tag), Some(path)) = (c_str(tag), c_str(path)) else {
            return fail(
                DUCK_ERR_INVALID_ARGUMENT,
                "null or invalid tag/path",
                err,
                err_cap,
            );
        };
        let p = Personality::from_seed(seed);
        match sounds::render(tag, &p, variant)
            .and_then(|buffer| sounds::to_wav(&buffer, std::path::Path::new(path)))
        {
            Ok(()) => DUCK_OK,
            Err(error) => fail(DUCK_ERR_IO, &error.to_string(), err, err_cap),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_render_all(
    seed: u32,
    dir: *const c_char,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        let Some(dir) = c_str(dir) else {
            return fail(
                DUCK_ERR_INVALID_ARGUMENT,
                "null or invalid dir",
                err,
                err_cap,
            );
        };
        let p = Personality::from_seed(seed);
        match sounds::render_all(&p, std::path::Path::new(dir)) {
            Ok(_) => DUCK_OK,
            Err(error) => fail(DUCK_ERR_IO, &error.to_string(), err, err_cap),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_new(
    voice: *const DuckVoice,
    tag: *const c_char,
    variant: u32,
) -> *mut DuckStream {
    guard(ptr::null_mut(), || {
        if voice.is_null() {
            set_last_error("null voice");
            return ptr::null_mut();
        }
        let Some(tag) = c_str(tag) else {
            set_last_error("null or invalid tag");
            return ptr::null_mut();
        };
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        Box::into_raw(Box::new(DuckStream {
            s: Stream::new(&voice.p, tag, variant),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_new_wheee(voice: *const DuckVoice, variant: u32) -> *mut DuckStream {
    guard(ptr::null_mut(), || {
        if voice.is_null() {
            set_last_error("null voice");
            return ptr::null_mut();
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        Box::into_raw(Box::new(DuckStream {
            s: Stream::wheee(&voice.p, variant),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_new_choral(voice: *const DuckVoice, part: i32) -> *mut DuckStream {
    guard(ptr::null_mut(), || {
        if voice.is_null() {
            set_last_error("null voice");
            return ptr::null_mut();
        }
        let Some(part) = part_from(part) else {
            set_last_error("invalid part");
            return ptr::null_mut();
        };
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        Box::into_raw(Box::new(DuckStream {
            s: Stream::choral(&voice.p, part as u32),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_free(stream: *mut DuckStream) {
    if stream.is_null() {
        return;
    }
    guard((), || {
        // 安全性：由某个 duck_stream_new_* 创建，只释放一次。
        drop(unsafe { Box::from_raw(stream) });
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_set(stream: *mut DuckStream, hz: f64, level: f64, open: f64) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if stream.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        if !hz.is_finite() || !level.is_finite() || !open.is_finite() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let stream = unsafe { &mut *stream };
        stream.s.set(hz, level, open);
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_set_level(stream: *mut DuckStream, level: f64) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if stream.is_null() || !level.is_finite() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let stream = unsafe { &mut *stream };
        stream.s.set_level(level);
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_set_formant_shift(stream: *mut DuckStream, harmonics: f64) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if stream.is_null() || !harmonics.is_finite() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let stream = unsafe { &mut *stream };
        stream.s.set_formant_shift(harmonics);
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_set_speaker_rolloff(stream: *mut DuckStream, hz: f64) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if stream.is_null() || hz.is_nan() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let stream = unsafe { &mut *stream };
        stream.s.set_speaker_rolloff((hz > 0.0).then_some(hz));
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_block(stream: *mut DuckStream, out: *mut f32, frames: usize) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if stream.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        if frames == 0 {
            return DUCK_OK;
        }
        if out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；调用方保证 `frames` 个可写 float，且只有一个 writer。
        let stream = unsafe { &mut *stream };
        let out = unsafe { std::slice::from_raw_parts_mut(out, frames) };
        stream.s.block(out);
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_stream_is_silent(stream: *const DuckStream) -> i32 {
    guard(0, || {
        if stream.is_null() {
            return 0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let stream = unsafe { &*stream };
        i32::from(stream.s.is_silent())
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_hz_at(voice: *const DuckVoice, position: f64) -> f64 {
    guard(0.0, || {
        if voice.is_null() || !position.is_finite() {
            return 0.0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        sounds::stream::hz_at(&voice.p, position)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_range_hz(voice: *const DuckVoice, lo: *mut f64, hi: *mut f64) {
    guard((), || {
        if voice.is_null() || lo.is_null() || hi.is_null() {
            return;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        let (low, high) = sounds::stream::range_hz(&voice.p);
        // 安全性：已检查非空输出参数。
        unsafe {
            *lo = low;
            *hi = high;
        }
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_wistful(err: *mut c_char, err_cap: usize) -> *mut DuckScore {
    guard(ptr::null_mut(), || match catch_unwind(Score::wistful) {
        Ok(score) => boxed_score(score),
        Err(payload) => {
            let message = format!("wistful score unavailable: {}", panic_message(&*payload));
            set_last_error(&message);
            write_err(err, err_cap, &message);
            ptr::null_mut()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_duck_strut(err: *mut c_char, err_cap: usize) -> *mut DuckScore {
    guard(ptr::null_mut(), || match catch_unwind(Score::duck_strut) {
        Ok(score) => boxed_score(score),
        Err(payload) => {
            let message = format!("duck_strut score unavailable: {}", panic_message(&*payload));
            set_last_error(&message);
            write_err(err, err_cap, &message);
            ptr::null_mut()
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_from_text(
    utf8: *const c_char,
    len: usize,
    out: *mut *mut DuckScore,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if out.is_null() || (utf8.is_null() && len != 0) {
            return fail(DUCK_ERR_INVALID_ARGUMENT, "null argument", err, err_cap);
        }
        let bytes: &[u8] = if len == 0 {
            &[]
        } else {
            // 安全性：已检查非空；调用方保证 `len` 个可读字节。
            unsafe { std::slice::from_raw_parts(utf8.cast::<u8>(), len) }
        };
        let Ok(text) = std::str::from_utf8(bytes) else {
            return fail(DUCK_ERR_PARSE, "score is not UTF-8", err, err_cap);
        };
        match chorale::text::parse(text) {
            Ok(score) => {
                // 安全性：已检查非空输出参数。
                unsafe { *out = boxed_score(score) };
                DUCK_OK
            }
            Err(error) => fail(DUCK_ERR_PARSE, &error.to_string(), err, err_cap),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_from_midi(
    bytes: *const u8,
    len: usize,
    out: *mut *mut DuckScore,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if out.is_null() || (bytes.is_null() && len != 0) {
            return fail(DUCK_ERR_INVALID_ARGUMENT, "null argument", err, err_cap);
        }
        let data: &[u8] = if len == 0 {
            &[]
        } else {
            // 安全性：已检查非空；调用方保证 `len` 个可读字节。
            unsafe { std::slice::from_raw_parts(bytes, len) }
        };
        match chorale::midi::parse(data) {
            Ok(import) => {
                // 安全性：已检查非空输出参数。
                unsafe { *out = boxed_score(import.score) };
                DUCK_OK
            }
            Err(error) => fail(DUCK_ERR_PARSE, &error.to_string(), err, err_cap),
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_free(score: *mut DuckScore) {
    if score.is_null() {
        return;
    }
    guard((), || {
        // 安全性：由某个 duck_score_* 创建，只释放一次。
        drop(unsafe { Box::from_raw(score) });
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_name(score: *const DuckScore) -> *const c_char {
    if score.is_null() {
        return ptr::null();
    }
    // 安全性：已检查非空；按约定句柄不可变。
    unsafe { (*score).name.as_ptr() }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_note_count(score: *const DuckScore) -> usize {
    if score.is_null() {
        return 0;
    }
    // 安全性：已检查非空；按约定句柄不可变。
    unsafe { (*score).score.notes.len() }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_note(
    score: *const DuckScore,
    index: usize,
    out: *mut DuckNote,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if score.is_null() || out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let score = unsafe { &*score };
        let Some(note) = score.score.notes.get(index) else {
            return DUCK_ERR_INVALID_ARGUMENT;
        };
        // 安全性：已检查非空输出参数。
        unsafe { *out = fill_note(note) };
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_beat_s(score: *const DuckScore) -> f64 {
    guard(0.0, || {
        if score.is_null() {
            return 0.0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        unsafe { (*score).score.beat_s() }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_duration_s(score: *const DuckScore) -> f64 {
    guard(0.0, || {
        if score.is_null() {
            return 0.0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        unsafe { (*score).score.duration_s() }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_score_parts(score: *const DuckScore) -> u32 {
    if score.is_null() {
        return 0;
    }
    let mut mask = 0u32;
    // 安全性：已检查非空；按约定句柄不可变。
    for part in unsafe { (*score).score.parts() } {
        mask |= 1u32 << (part as u32);
    }
    mask
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_cast(
    seeds: *const u32,
    count: usize,
    out: *mut DuckSinger,
    out_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if count == 0 {
            return DUCK_OK;
        }
        if seeds.is_null() || out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        if out_cap < count {
            return DUCK_ERR_BUFFER_TOO_SMALL;
        }
        // 安全性：已检查非空；调用方保证 `count` 个可读 seed。
        let seeds = unsafe { std::slice::from_raw_parts(seeds, count) };
        let personalities: Vec<Personality> =
            seeds.iter().copied().map(Personality::from_seed).collect();
        let singers = chorale::cast(&personalities);
        debug_assert_eq!(singers.len(), count);
        for (index, singer) in singers.iter().enumerate() {
            // 安全性：out_cap >= count 且 index < count。
            unsafe { *out.add(index) = fill_singer(singer) };
        }
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_seat_all(
    registers: *const f64,
    count: usize,
    parts_out: *mut i32,
    out_cap: usize,
) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if count == 0 {
            return DUCK_OK;
        }
        if registers.is_null() || parts_out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        if out_cap < count {
            return DUCK_ERR_BUFFER_TOO_SMALL;
        }
        // 安全性：已检查非空；调用方保证 `count` 个可读 register。
        let registers = unsafe { std::slice::from_raw_parts(registers, count) };
        if registers.iter().any(|value| !value.is_finite()) {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        let parts = chorale::seat_all(registers);
        for (index, part) in parts.iter().enumerate() {
            // 安全性：out_cap >= count 且 index < count。
            unsafe { *parts_out.add(index) = part_to(*part) };
        }
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_seat_for(voice: *const DuckVoice, part: i32, out: *mut DuckSinger) -> i32 {
    guard(DUCK_ERR_PANIC, || {
        if voice.is_null() || out.is_null() {
            return DUCK_ERR_INVALID_ARGUMENT;
        }
        let Some(part) = part_from(part) else {
            return DUCK_ERR_INVALID_ARGUMENT;
        };
        // 安全性：已检查非空；按约定句柄不可变。
        let voice = unsafe { &*voice };
        let singer = chorale::seat_for(&voice.p, part);
        // 安全性：已检查非空输出参数。
        unsafe { *out = fill_singer(&singer) };
        DUCK_OK
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_conductor_new(bpm: f64, now_s: f64) -> *mut DuckConductor {
    guard(ptr::null_mut(), || {
        if !bpm.is_finite() || !now_s.is_finite() {
            set_last_error("non-finite bpm/now");
            return ptr::null_mut();
        }
        Box::into_raw(Box::new(DuckConductor {
            c: Conductor::new(bpm, now_s),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_conductor_free(conductor: *mut DuckConductor) {
    if conductor.is_null() {
        return;
    }
    guard((), || {
        // 安全性：由 duck_conductor_new 创建，只释放一次。
        drop(unsafe { Box::from_raw(conductor) });
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_conductor_due(
    conductor: *mut DuckConductor,
    now_s: f64,
    beat_out: *mut u64,
) -> i32 {
    guard(0, || {
        if conductor.is_null() || beat_out.is_null() || !now_s.is_finite() {
            return 0;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let conductor = unsafe { &mut *conductor };
        match conductor.c.due(now_s) {
            Some(beat) => {
                // 安全性：已检查非空输出参数。
                unsafe { *beat_out = beat };
                1
            }
            None => 0,
        }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_conductor_position_beats(
    conductor: *const DuckConductor,
    now_s: f64,
) -> f64 {
    guard(0.0, || {
        if conductor.is_null() || !now_s.is_finite() {
            return 0.0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        unsafe { (*conductor).c.position_beats(now_s) }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_conductor_wire_beat(conductor: *const DuckConductor) -> u8 {
    guard(0, || {
        if conductor.is_null() {
            return 0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        unsafe { (*conductor).c.wire_beat() }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_new(bpm: f64) -> *mut DuckFollower {
    guard(ptr::null_mut(), || {
        if !bpm.is_finite() {
            set_last_error("non-finite bpm");
            return ptr::null_mut();
        }
        Box::into_raw(Box::new(DuckFollower {
            f: Follower::new(bpm),
        }))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_free(follower: *mut DuckFollower) {
    if follower.is_null() {
        return;
    }
    guard((), || {
        // 安全性：由 duck_follower_new 创建，只释放一次。
        drop(unsafe { Box::from_raw(follower) });
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_observe(follower: *mut DuckFollower, beat: u8, at_s: f64) {
    guard((), || {
        if follower.is_null() || !at_s.is_finite() {
            return;
        }
        // 安全性：已检查非空；按约定单线程使用。
        let follower = unsafe { &mut *follower };
        follower.f.observe(beat, at_s);
    });
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_locked(follower: *const DuckFollower) -> i32 {
    guard(0, || {
        if follower.is_null() {
            return 0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        unsafe { i32::from((*follower).f.locked()) }
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_position_beats(
    follower: *const DuckFollower,
    now_s: f64,
    out: *mut f64,
) -> i32 {
    guard(0, || {
        if follower.is_null() || out.is_null() || !now_s.is_finite() {
            return 0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let follower = unsafe { &*follower };
        let Some(position) = follower.f.position_beats(now_s) else {
            return 0;
        };
        // 安全性：已检查非空输出参数。
        unsafe { *out = position };
        1
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_follower_spread_s(follower: *const DuckFollower, out: *mut f64) -> i32 {
    guard(0, || {
        if follower.is_null() || out.is_null() {
            return 0;
        }
        // 安全性：已检查非空；按约定句柄不可变。
        let follower = unsafe { &*follower };
        let Some(spread) = follower.f.spread_s() else {
            return 0;
        };
        // 安全性：已检查非空输出参数。
        unsafe { *out = spread };
        1
    })
}
