#include "duck_ear/linear_resampler.hpp"

#include <algorithm>
#include <cmath>

namespace duck_ear {

LinearResampler::LinearResampler(const uint32_t input_rate, const uint32_t output_rate)
    : input_rate_(input_rate), output_rate_(output_rate) {
    ratio_ = output_rate_ == 0 ? 1.0 : static_cast<double>(input_rate_) / output_rate_;
}

std::vector<float> LinearResampler::push(const float* samples, const std::size_t count) {
    std::vector<float> output;
    if (samples == nullptr || count == 0) {
        return output;
    }
    if (input_rate_ == output_rate_) {
        output.assign(samples, samples + count);
        return output;
    }
    // 保留上一块的末尾，插值跨块连续；cursor_ 是 buffer_ 中的位置。
    buffer_.insert(buffer_.end(), samples, samples + count);
    output.reserve(static_cast<std::size_t>(static_cast<double>(count) / ratio_) + 1);
    while (true) {
        const auto index = static_cast<std::size_t>(cursor_);
        if (index + 1 >= buffer_.size()) {
            break;
        }
        const double fraction = cursor_ - static_cast<double>(index);
        const float a = buffer_[index];
        const float b = buffer_[index + 1];
        output.push_back(static_cast<float>(a + (b - a) * fraction));
        cursor_ += ratio_;
    }
    // 丢掉已消费的前缀，但保留 cursor 所在样本供下一块插值。
    const auto drop = static_cast<std::size_t>(cursor_);
    if (drop > 4096) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(drop));
        cursor_ -= static_cast<double>(drop);
    }
    return output;
}

} // namespace duck_ear
