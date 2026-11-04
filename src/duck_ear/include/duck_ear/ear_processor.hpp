#pragma once

#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "duck_ear/linear_resampler.hpp"
#include "duck_ear/ffi/duck_ear_ffi.h"

namespace duck_ear {

    /// 分类器的一次状态变化。
    struct EarEvent {
        enum class Type {
            Start, ///< 进入摸头状态
            End,   ///< 离开摸头状态
        };

        Type type{Type::Start};
        float confidence{0.0F}; ///< 触发本次变化时的推理概率
    };

    /// 把任意采样率的 int16 PCM 变成 16 kHz mono f32，调用 Rust pet-detect 并返回事件。
    class EarProcessor {
    public:
        struct Config {
            std::filesystem::path model;      ///< pet_detect.onnx 路径
            float enter_threshold{0.95F};    ///< 高于此值进入摸头
            float exit_threshold{0.85F};     ///< 低于此值结束摸头（滞回）
            uint32_t stride{0};              ///< 两次推理间隔样本数；0 = 默认约 250 ms
        };

        EarProcessor() = default;
        /// 释放 Rust DuckEar 句柄；不删除模型文件。
        ~EarProcessor();

        EarProcessor(const EarProcessor&) = delete;
        EarProcessor& operator=(const EarProcessor&) = delete;

        /// 加载 ONNX 模型；失败时 error 写原因，后续 push 返回空。
        [[nodiscard]] bool initialize(const Config& config, std::string* error);

        /// 分类器是否已成功加载；false 时 push 不做任何事。
        [[nodiscard]] bool ok() const {
            return ear_ != nullptr;
        }

        /// 推入一块 s16le PCM；任意采样率/1–2 声道会先下混并重采样到 16 kHz。
        /// 事件为 Start/End，confidence 为最近一次推理概率。
        [[nodiscard]] std::vector<EarEvent> push(const int16_t* samples, std::size_t frames,
                                                 uint32_t sample_rate, uint16_t channels);
        /// Rust 分类器当前是否处于摸头状态。
        [[nodiscard]] bool is_petting() const;

    private:
        DuckEar* ear_{nullptr}; ///< Rust pet-detect 句柄；只由一个音频线程使用
        std::unique_ptr<LinearResampler> resampler_; ///< 输入采样率变化时重建
        std::vector<float> mono_;       ///< 下混后的复用缓冲
        std::uint32_t input_rate_{0};    ///< 当前重采样器的输入采样率
        std::uint16_t input_channels_{0};///< 当前输入声道数
        std::string last_error_;         ///< 最近一次 FFI 错误，供诊断
    };

} // namespace duck_ear
