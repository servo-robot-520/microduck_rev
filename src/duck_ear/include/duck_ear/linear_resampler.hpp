#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace duck_ear {

/// 带状态的线性重采样器：块边界不会产生咔哒，48 kHz → 16 kHz 时每 3 个输入出一个输出。
class LinearResampler {
public:
    LinearResampler(uint32_t input_rate, uint32_t output_rate);

    /// 推入一块 mono float；返回本块产生的输出。内部保留跨块插值状态。
    std::vector<float> push(const float* samples, std::size_t count);

    [[nodiscard]] uint32_t input_rate() const { return input_rate_; }
    [[nodiscard]] uint32_t output_rate() const { return output_rate_; }

private:
    uint32_t input_rate_{0};
    uint32_t output_rate_{0};
    double ratio_{1.0};
    std::vector<float> buffer_;
    double cursor_{0.0};
};

} // namespace duck_ear
