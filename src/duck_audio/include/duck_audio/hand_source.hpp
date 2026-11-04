#pragma once

#include <chrono>
#include <optional>

#include "duck_audio/hand_tracker.hpp"

namespace duck_audio {

/// 手部输入抽象：特雷门只关心 closeness，不关心输入是 ToF、深度图还是点云。
class HandSource {
public:
    virtual ~HandSource() = default;

    [[nodiscard]] virtual std::optional<HandSample> latest(
            std::chrono::steady_clock::time_point now) = 0;
    [[nodiscard]] virtual bool healthy(std::chrono::steady_clock::time_point now) const = 0;
    virtual void reset() = 0;
};

} // namespace duck_audio
