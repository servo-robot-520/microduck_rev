#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace duck_audio {

struct HandConfig {
    double near_m{0.12};
    double far_m{0.70};
    double lateral_m{0.35};
    double y_min_m{-0.25};
    double y_max_m{0.20};
    double roi_x_frac{0.60};
    double roi_y0_frac{0.20};
    double roi_y1_frac{0.80};
    std::size_t min_points{40};
    double bin_m{0.02};
    std::chrono::milliseconds hold{250};
    std::chrono::milliseconds stale{200};
};

struct HandSample {
    double range_m{0.0};
    double closeness{0.0};
    std::size_t points{0};
    bool held{false};
};

/// 从一组深度值（米）里找“最近的、有足够支撑的深度簇”，并做 hold。
class HandTracker {
public:
    explicit HandTracker(HandConfig config);

    [[nodiscard]] std::optional<HandSample> update(const std::vector<float>& depths,
                                                   std::chrono::steady_clock::time_point now);
    [[nodiscard]] std::optional<HandSample> held(std::chrono::steady_clock::time_point now);
    void reset();

    [[nodiscard]] const HandConfig& config() const { return config_; }

private:
    HandConfig config_;
    std::optional<HandSample> last_;
    std::chrono::steady_clock::time_point last_at_{};
};

} // namespace duck_audio
