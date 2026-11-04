/*
 * duck_ear_ffi.h — pet-detect 的窄 C ABI。
 *
 * `libduck_ear.so` 在 C++ 侧调用这些函数；Rust 静态库被链接进该 .so。
 * 输入固定为 16 kHz mono f32；一个 DuckEar 只能由一个线程使用。
 */
#ifndef DUCK_EAR_FFI_H
#define DUCK_EAR_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct DuckEar DuckEar;

/* 状态码；与 Rust duck_ear_ffi 保持一致。 */
#define DUCK_EAR_OK 0
#define DUCK_EAR_ERR_INVALID_ARGUMENT 1
#define DUCK_EAR_ERR_IO 2
#define DUCK_EAR_ERR_PANIC 3
#define DUCK_EAR_ERR_INFERENCE 4

uint32_t duck_ear_abi_version(void);

/* 加载 pet_detect.onnx；stride=0 使用原版默认（约 250 ms）。
 * 失败返回 NULL，err 写英文原因。 */
DuckEar* duck_ear_new(const char* model_path, float enter_threshold,
                      float exit_threshold, uint32_t stride,
                      char* err, size_t err_cap);

/* 释放句柄；之后不能再调用 push/is_petting。 */
void duck_ear_free(DuckEar* ear);

/* 推入 16 kHz mono f32；事件写成 0=Start, 1=End，event_cap 至少 4。
 * probability 可传 NULL；返回 DUCK_EAR_* 状态码。 */
int duck_ear_push(DuckEar* ear, const float* samples, size_t sample_count,
                  int32_t* events, size_t event_cap, size_t* event_count,
                  float* probability, char* err, size_t err_cap);

/* 当前是否处于摸头状态；1=是，0=否/空句柄。 */
int duck_ear_is_petting(const DuckEar* ear);

#ifdef __cplusplus
}
#endif

#endif
