#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace duck_ear {

    /// 带状态的线性重采样器：块边界不会产生咔哒，48 kHz → 16 kHz 时每 3 个输入出一个输出。
    class LinearResampler {
    public:
        /// input_rate == output_rate 时直接拷贝；否则按线性插值降/升采样。
        LinearResampler(uint32_t input_rate, uint32_t output_rate);

        /// 推入一块 mono float；返回本块产生的输出。内部保留跨块插值状态。
        std::vector<float> push(const float* samples, std::size_t count);

        /// 构造时的输入采样率，单位 Hz。
        [[nodiscard]] uint32_t input_rate() const {
            return input_rate_;
        }

        /// 构造时的输出采样率，单位 Hz。
        [[nodiscard]] uint32_t output_rate() const {
            return output_rate_;
        }

    private:
        uint32_t input_rate_{0};  ///< 输入采样率
        uint32_t output_rate_{0}; ///< 输出采样率
        double ratio_{1.0};       ///< input_rate / output_rate
        std::vector<float> buffer_; ///< 跨块保留的尾部样本
        double cursor_{0.0};        ///< 下一个输出在 buffer_ 中的位置
    };

} // namespace duck_ear
