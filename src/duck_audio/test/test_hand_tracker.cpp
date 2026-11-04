#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "duck_audio/hand_tracker.hpp"

namespace {

duck_audio::HandConfig config() {
    duck_audio::HandConfig result;
    result.near_m = 0.10;
    result.far_m = 0.70;
    result.min_points = 3;
    result.bin_m = 0.02;
    result.hold = std::chrono::milliseconds(250);
    return result;
}

TEST(HandTracker, FindsTheNearestSupportedCluster) {
    duck_audio::HandTracker tracker(config());
    const auto now = std::chrono::steady_clock::now();
    std::vector<float> depths(3, 0.30F);
    depths.insert(depths.end(), 100, 0.60F);
    const auto hand = tracker.update(depths, now);
    ASSERT_TRUE(hand.has_value());
    EXPECT_NEAR(hand->range_m, 0.30, 0.01);
    EXPECT_GT(hand->closeness, 0.6);
    EXPECT_FALSE(hand->held);
}

TEST(HandTracker, RejectsSparseFliers) {
    duck_audio::HandTracker tracker(config());
    const auto now = std::chrono::steady_clock::now();
    const std::vector<float> depths{0.20F, 0.21F};
    EXPECT_FALSE(tracker.update(depths, now).has_value());
}

TEST(HandTracker, HoldsThroughADropoutAndThenStops) {
    duck_audio::HandTracker tracker(config());
    const auto start = std::chrono::steady_clock::now();
    const std::vector<float> depths(5, 0.30F);
    ASSERT_TRUE(tracker.update(depths, start).has_value());

    const auto held = tracker.update({}, start + std::chrono::milliseconds(100));
    ASSERT_TRUE(held.has_value());
    EXPECT_TRUE(held->held);

    EXPECT_FALSE(tracker.update({}, start + std::chrono::milliseconds(400)).has_value());
    EXPECT_FALSE(tracker.update({}, start + std::chrono::milliseconds(500)).has_value());
}

TEST(HandTracker, ClosenessIsOneAtNearAndZeroAtFar) {
    duck_audio::HandTracker tracker(config());
    const auto now = std::chrono::steady_clock::now();
    const auto near = tracker.update(std::vector<float>(5, 0.10F), now);
    ASSERT_TRUE(near.has_value());
    EXPECT_NEAR(near->closeness, 1.0, 1e-6);

    const auto far = tracker.update(std::vector<float>(5, 0.70F), now);
    ASSERT_TRUE(far.has_value());
    EXPECT_NEAR(far->closeness, 0.0, 1e-6);
}

} // namespace
