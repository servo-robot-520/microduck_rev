#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <iterator>
#include <vector>

#include "duck_ear/ffi/duck_ear_ffi.h"
#include "duck_ear/linear_resampler.hpp"

namespace duck_ear {

struct EarEvent {
    enum class Type {
        Start,
        End,
    };
    Type type{Type::Start};
    float confidence{0.0F};
};

/// 把任意采样率的 int16 PCM 变成 16 kHz mono f32，调用 Rust pet-detect 并返回事件。
class EarProcessor {
public:
    struct Config {
        std::filesystem::path model;
        float enter_threshold{0.95F};
        float exit_threshold{0.85F};
        uint32_t stride{0}; ///< 0 = pet-detect 默认（约 250 ms）
    };

    EarProcessor() = default;
    ~EarProcessor();

    EarProcessor(const EarProcessor&) = delete;
    EarProcessor& operator=(const EarProcessor&) = delete;

    /// 加载 ONNX 模型；失败时 error 写英文原因，后续 push 返回空。
    [[nodiscard]] bool initialize(const Config& config, std::string* error);
    [[nodiscard]] bool ok() const { return ear_ != nullptr; }

    /// 推入一块 s16le PCM；事件为 Start/End，confidence 为最近一次推理概率。
    [[nodiscard]] std::vector<EarEvent> push(const int16_t* samples, std::size_t frames,
                                             uint32_t sample_rate, uint16_t channels);
    [[nodiscard]] bool is_petting() const;

private:
    DuckEar* ear_{nullptr};
    std::unique_ptr<LinearResampler> resampler_;
    std::vector<float> mono_;
    std::uint32_t input_rate_{0};
    std::uint16_t input_channels_{0};
    std::string last_error_;
};

} // namespace duck_ear
