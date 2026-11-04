/*
 * microduck_sounds.h — microduck `sounds` crate 的 C ABI。
 *
 * 由 `src/duck_audio/rust/microduck_sounds_ffi` 构建为 `libmicroduck_sounds.so` / `.a`。
 *
 * 约定：
 *  - 不透明句柄由本库创建和释放；
 *  - `DuckStream` 单线程使用：一个 writer 线程调用 set/block；
 *  - `DuckVoice`、`DuckScore` 创建后不可变；
 *  - 不允许 Rust panic 穿过 C 边界，失败通过 `DuckStatus` 返回；
 *  - 所有 `double` 参数必须有限，NaN 会被拒绝。
 */
#ifndef MICRODUCK_SOUNDS_H
#define MICRODUCK_SOUNDS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MICRODUCK_SOUNDS_ABI_VERSION 1u

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

/* 与 Rust `#[repr(C)]` 字段顺序一致；`register` 是 C++ 关键字，故用 `register_value`。 */
typedef struct DuckPersonality {
    uint32_t seed;
    uint32_t formant_n;
    double pitch_center_hz;
    double register_value;
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
    int32_t part;   /* DuckPart */
    int32_t vowel;  /* DuckVowel */
    uint8_t midi;
    double start_beat;
    double beats;
    double level;
} DuckNote;

typedef struct DuckSinger {
    uint32_t seed;
    int32_t part; /* DuckPart */
    double pitch_center_hz;
    double detune_cents;
    double onset_offset_s;
} DuckSinger;

uint32_t duck_abi_version(void);
uint32_t duck_bank_version(void);
const char *duck_status_str(DuckStatus status);
/* 线程局部缓冲；同一线程下一次 FFI 调用前有效。 */
const char *duck_last_error(void);

/* 读取硬件/环境 seed；没有 SoC serial 时按 `sounds` 的规则回退。 */
DuckStatus duck_hardware_seed(uint32_t *out, char *err, size_t err_cap);
DuckStatus duck_seed_from_id(const char *id, uint32_t *out,
                             char *err, size_t err_cap);

/* 创建/释放这只鸭子的音色身份；句柄创建后只读。 */
DuckVoice *duck_voice_new(uint32_t seed);
void duck_voice_free(DuckVoice *voice);
DuckStatus duck_voice_personality(const DuckVoice *voice, DuckPersonality *out);

/* 测试/工具用。`*out` 由本库分配，用 duck_buffer_free(*out, *frames) 释放。
 * 生产音效走预渲染音库，不调用这里。 */
DuckStatus duck_voice_render(const DuckVoice *voice, const char *tag,
                             uint32_t variant, float **out, size_t *frames,
                             char *err, size_t err_cap);
void duck_buffer_free(float *data, size_t frames);

/* 音库渲染；生产上优先用 `sounds ensure-bank` CLI。 */
DuckStatus duck_render_tag(uint32_t seed, const char *tag, uint32_t variant,
                           const char *path, char *err, size_t err_cap);
DuckStatus duck_render_all(uint32_t seed, const char *dir,
                           char *err, size_t err_cap);

/* 实时合成流。`DuckStream` 不能跨线程并发使用。 */
DuckStream *duck_stream_new(const DuckVoice *voice, const char *tag,
                            uint32_t variant);
DuckStream *duck_stream_new_wheee(const DuckVoice *voice, uint32_t variant);
DuckStream *duck_stream_new_choral(const DuckVoice *voice, DuckPart part);
void duck_stream_free(DuckStream *stream);

DuckStatus duck_stream_set(DuckStream *stream, double hz, double level,
                           double open);
DuckStatus duck_stream_set_level(DuckStream *stream, double level);
DuckStatus duck_stream_set_formant_shift(DuckStream *stream, double harmonics);
/* hz <= 0 表示全频扬声器；NaN 非法。 */
DuckStatus duck_stream_set_speaker_rolloff(DuckStream *stream, double hz);

/* `out` 必须指向 `frames` 个可写 float。 */
DuckStatus duck_stream_block(DuckStream *stream, float *out, size_t frames);
int duck_stream_is_silent(const DuckStream *stream);

/* 原版的几何音高映射：position 0..1，越界钳制。 */
double duck_hz_at(const DuckVoice *voice, double position);
void duck_range_hz(const DuckVoice *voice, double *lo, double *hi);

/* 乐谱前端：内置 wistful、duck_strut，以及文本/MIDI 导入。 */
DuckScore *duck_score_wistful(char *err, size_t err_cap);
DuckScore *duck_score_duck_strut(char *err, size_t err_cap);
DuckStatus duck_score_from_text(const char *utf8, size_t len,
                                DuckScore **out, char *err, size_t err_cap);
DuckStatus duck_score_from_midi(const uint8_t *bytes, size_t len,
                                DuckScore **out, char *err, size_t err_cap);
void duck_score_free(DuckScore *score);

/* `duck_score_name` 返回借用指针，直到 score 被释放。 */
const char *duck_score_name(const DuckScore *score);
size_t duck_score_note_count(const DuckScore *score);
DuckStatus duck_score_note(const DuckScore *score, size_t index, DuckNote *out);
double duck_score_beat_s(const DuckScore *score);
double duck_score_duration_s(const DuckScore *score);
/* bit 0..3 对应 DuckPart。 */
uint32_t duck_score_parts(const DuckScore *score);

/* 声部编排：整组 cast、按 register 分配、单只鸭子取 detune/onset。 */
DuckStatus duck_cast(const uint32_t *seeds, size_t count,
                     DuckSinger *out, size_t out_cap);
DuckStatus duck_seat_all(const double *registers, size_t count,
                         int32_t *parts_out, size_t out_cap);
DuckStatus duck_seat_for(const DuckVoice *voice, DuckPart part,
                         DuckSinger *out);

/* 无共享时钟的节拍：conductor 产生 beat，follower 平均相位。 */
DuckConductor *duck_conductor_new(double bpm, double now_s);
void duck_conductor_free(DuckConductor *conductor);
/* 返回 1 表示 beat 变化并写入 *beat_out，0 表示没有新拍。 */
int duck_conductor_due(DuckConductor *conductor, double now_s,
                       uint64_t *beat_out);
double duck_conductor_position_beats(const DuckConductor *conductor, double now_s);
uint8_t duck_conductor_wire_beat(const DuckConductor *conductor);

DuckFollower *duck_follower_new(double bpm);
void duck_follower_free(DuckFollower *follower);
void duck_follower_observe(DuckFollower *follower, uint8_t beat, double at_s);
int duck_follower_locked(const DuckFollower *follower);
/* 锁定后返回 1 并写入 *out；否则返回 0。 */
int duck_follower_position_beats(const DuckFollower *follower, double now_s,
                                 double *out);
int duck_follower_spread_s(const DuckFollower *follower, double *out);

#ifdef __cplusplus
}
#endif

#endif
