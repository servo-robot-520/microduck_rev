//! `pet-detect` 的窄 C ABI：C++ 的 `libduck_ear.so` 只通过这里使用分类器。
//!
//! 输入固定为 16 kHz mono f32；重采样、AudioStream 解析和 ROS 发布都在 C++ 侧。
//! `DuckEar` 不是线程安全的，一个 detector 只能由一个音频线程调用。

use std::ffi::{c_char, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use pet_detect::{PettingDetector, PettingDetectorConfig, PettingEvent, WINDOW_SAMPLES};

pub const DUCK_EAR_OK: i32 = 0;
pub const DUCK_EAR_ERR_INVALID_ARGUMENT: i32 = 1;
pub const DUCK_EAR_ERR_IO: i32 = 2;
pub const DUCK_EAR_ERR_PANIC: i32 = 3;
pub const DUCK_EAR_ERR_INFERENCE: i32 = 4;

pub struct DuckEar {
    detector: PettingDetector,
}

fn write_err(err: *mut c_char, cap: usize, message: &str) {
    if err.is_null() || cap == 0 {
        return;
    }
    let bytes = message.as_bytes();
    let n = bytes.len().min(cap - 1);
    // SAFETY: caller provides `cap` writable bytes; n < cap.
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), err.cast::<u8>(), n);
        *err.add(n) = 0;
    }
}

fn fail(status: i32, message: &str, err: *mut c_char, cap: usize) -> i32 {
    write_err(err, cap, message);
    status
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_ear_abi_version() -> u32 {
    1
}

/// 创建分类器。model_path 为 pet_detect.onnx；stride=0 使用原版默认（约 250 ms）。
#[unsafe(no_mangle)]
pub extern "C" fn duck_ear_new(
    model_path: *const c_char,
    enter_threshold: f32,
    exit_threshold: f32,
    stride: u32,
    err: *mut c_char,
    err_cap: usize,
) -> *mut DuckEar {
    if model_path.is_null() || !enter_threshold.is_finite() || !exit_threshold.is_finite() {
        write_err(err, err_cap, "invalid model path or threshold");
        return ptr::null_mut();
    }
    let path = unsafe { CStr::from_ptr(model_path) };
    let Ok(path) = path.to_str() else {
        write_err(err, err_cap, "model path is not UTF-8");
        return ptr::null_mut();
    };
    let config = PettingDetectorConfig {
        stride: if stride == 0 {
            WINDOW_SAMPLES / 4
        } else {
            stride as usize
        },
        enter_threshold,
        exit_threshold,
    };
    match catch_unwind(AssertUnwindSafe(|| {
        PettingDetector::new(std::path::Path::new(path), config)
    })) {
        Ok(Ok(detector)) => Box::into_raw(Box::new(DuckEar { detector })),
        Ok(Err(error)) => {
            let text = format!("cannot load petting detector: {error}");
            write_err(err, err_cap, &text);
            ptr::null_mut()
        }
        Err(payload) => {
            let text = format!("panic creating petting detector: {:?}", payload);
            write_err(err, err_cap, &text);
            ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_ear_free(ear: *mut DuckEar) {
    if ear.is_null() {
        return;
    }
    let _ = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: created by duck_ear_new, freed once by the caller.
        drop(unsafe { Box::from_raw(ear) });
    }));
}

/// 推入 16 kHz mono f32；事件写成 0=Start, 1=End。返回状态码。
#[unsafe(no_mangle)]
pub extern "C" fn duck_ear_push(
    ear: *mut DuckEar,
    samples: *const f32,
    sample_count: usize,
    events: *mut i32,
    event_cap: usize,
    event_count: *mut usize,
    probability: *mut f32,
    err: *mut c_char,
    err_cap: usize,
) -> i32 {
    if ear.is_null() || event_count.is_null() {
        return fail(DUCK_EAR_ERR_INVALID_ARGUMENT, "null ear/event_count", err, err_cap);
    }
    if sample_count != 0 && samples.is_null() {
        return fail(DUCK_EAR_ERR_INVALID_ARGUMENT, "null samples", err, err_cap);
    }
    if events.is_null() && event_cap != 0 {
        return fail(DUCK_EAR_ERR_INVALID_ARGUMENT, "null events", err, err_cap);
    }
    let samples = if sample_count == 0 {
        &[]
    } else {
        // SAFETY: caller provides sample_count readable f32 values.
        unsafe { std::slice::from_raw_parts(samples, sample_count) }
    };
    let result = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: checked non-null; single-threaded by contract.
        let ear = unsafe { &mut *ear };
        ear.detector.push_samples(samples)
    }));
    match result {
        Ok(Ok((found, p))) => {
            if found.len() > event_cap {
                return fail(DUCK_EAR_ERR_INVALID_ARGUMENT, "event buffer too small", err, err_cap);
            }
            for (index, event) in found.iter().enumerate() {
                // SAFETY: event_cap >= found.len() and index < found.len().
                unsafe {
                    *events.add(index) = match event {
                        PettingEvent::Start => 0,
                        PettingEvent::End => 1,
                    };
                }
            }
            // SAFETY: out-params checked non-null.
            unsafe {
                *event_count = found.len();
                if !probability.is_null() {
                    *probability = p.unwrap_or(-1.0);
                }
            }
            DUCK_EAR_OK
        }
        Ok(Err(error)) => fail(DUCK_EAR_ERR_INFERENCE, &error.to_string(), err, err_cap),
        Err(_) => fail(DUCK_EAR_ERR_PANIC, "panic in petting detector", err, err_cap),
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn duck_ear_is_petting(ear: *const DuckEar) -> i32 {
    if ear.is_null() {
        return 0;
    }
    // SAFETY: checked non-null, immutable handle by contract.
    let ear = unsafe { &*ear };
    i32::from(ear.detector.is_petting())
}
