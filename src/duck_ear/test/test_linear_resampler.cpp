#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "duck_ear/linear_resampler.hpp"

TEST(LinearResampler, Converts48kTo16kAcrossChunks) {
    duck_ear::LinearResampler resampler(48000, 16000);
    std::vector<float> input(4800);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<float>(i) / static_cast<float>(input.size());
    }
    auto first = resampler.push(input.data(), 1600);
    auto second = resampler.push(input.data() + 1600, 1600);
    auto third = resampler.push(input.data() + 3200, 1600);
    first.insert(first.end(), second.begin(), second.end());
    first.insert(first.end(), third.begin(), third.end());
    EXPECT_NEAR(static_cast<double>(first.size()), 1600.0, 2.0);
    EXPECT_NEAR(first.front(), input.front(), 0.01);
    EXPECT_TRUE(std::all_of(first.begin(), first.end(), [](float value) {
        return std::isfinite(value);
    }));
}

TEST(LinearResampler, Passes16kThrough) {
    duck_ear::LinearResampler resampler(16000, 16000);
    const std::vector<float> input{-0.5F, 0.0F, 0.5F};
    const auto output = resampler.push(input.data(), input.size());
    ASSERT_EQ(output.size(), input.size());
    EXPECT_EQ(output, input);
}
