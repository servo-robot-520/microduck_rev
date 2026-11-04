#pragma once

#include <chrono>
#include <optional>

#include "duck_audio/hand_tracker.hpp"

namespace duck_audio {

/// 手部输入抽象：特雷门只关心 closeness，不关心输入是 ToF、深度图还是点云。
class HandSource {
public:
    virtual ~HandSource() = default;

    /// 最近的手样本；没有手或输入过期时返回 nullopt。
    [[nodiscard]] virtual std::optional<HandSample> latest(
            std::chrono::steady_clock::time_point now) = 0;
    /// 输入是否仍在更新；用于启用前检查和状态里的 sensor_ok。
    [[nodiscard]] virtual bool healthy(std::chrono::steady_clock::time_point now) const = 0;
    /// 丢弃 hold 与最近帧。
    virtual void reset() = 0;
};

} // namespace duck_audio
