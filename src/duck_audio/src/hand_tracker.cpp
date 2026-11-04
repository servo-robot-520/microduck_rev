#include "duck_audio/hand_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace duck_audio {

HandTracker::HandTracker(HandConfig config) : config_(std::move(config)) {}

std::optional<HandSample> HandTracker::update(const std::vector<float>& depths,
                                              const std::chrono::steady_clock::time_point now) {
    if (depths.size() < config_.min_points) {
        return held(now);
    }

    const double span = std::max(config_.far_m - config_.near_m, 1e-6);
    const int bins = std::max(1, static_cast<int>(span / std::max(config_.bin_m, 1e-6)));
    std::vector<std::size_t> counts(static_cast<std::size_t>(bins), 0);
    std::vector<int> bin_of(depths.size(), 0);
    for (std::size_t i = 0; i < depths.size(); ++i) {
        const double value = static_cast<double>(depths[i]);
        int bin = static_cast<int>((value - config_.near_m) / config_.bin_m);
        bin = std::clamp(bin, 0, bins - 1);
        bin_of[i] = bin;
        ++counts[static_cast<std::size_t>(bin)];
    }

    int chosen = -1;
    for (int bin = 0; bin < bins; ++bin) {
        if (counts[static_cast<std::size_t>(bin)] >= config_.min_points) {
            chosen = bin;
            break;
        }
    }
    if (chosen < 0) {
        return held(now);
    }

    std::vector<float> picked;
    picked.reserve(depths.size());
    for (std::size_t i = 0; i < depths.size(); ++i) {
        if (std::abs(bin_of[i] - chosen) <= 1) {
            picked.push_back(depths[i]);
        }
    }
    if (picked.empty()) {
        return held(now);
    }
    const auto middle = picked.begin() + static_cast<std::ptrdiff_t>(picked.size() / 2);
    std::nth_element(picked.begin(), middle, picked.end());
    const double range = *middle;
    const double closeness = std::clamp((config_.far_m - range) / span, 0.0, 1.0);
    HandSample sample{range, closeness, picked.size(), false};
    last_ = sample;
    last_at_ = now;
    return sample;
}

std::optional<HandSample> HandTracker::held(const std::chrono::steady_clock::time_point now) {
    if (!last_.has_value()) {
        return std::nullopt;
    }
    if (now - last_at_ > config_.hold) {
        last_.reset();
        return std::nullopt;
    }
    HandSample sample = *last_;
    sample.held = true;
    return sample;
}

void HandTracker::reset() {
    last_.reset();
}

} // namespace duck_audio
