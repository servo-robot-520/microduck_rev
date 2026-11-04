#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace duck_audio {

    /// 手部 ROI 与时序参数；深度单位统一为米。
    struct HandConfig {
        double near_m{0.12}; ///< 最近可玩距离
        double far_m{0.70}; ///< 最远可玩距离
        double lateral_m{0.35}; ///< 点云左右半宽
        double y_min_m{-0.25}; ///< 点云上边界（光学系 y 向下）
        double y_max_m{0.20}; ///< 点云下边界，排除地面
        double roi_x_frac{0.60}; ///< 深度图水平 ROI 占比
        double roi_y0_frac{0.20}; ///< 深度图 ROI 起始行比例
        double roi_y1_frac{0.80}; ///< 深度图 ROI 结束行比例
        std::size_t min_points{40}; ///< 一个深度簇的最小支撑点数
        double bin_m{0.02}; ///< 深度直方图 bin 宽
        std::chrono::milliseconds hold{250}; ///< 丢帧保持时间
        std::chrono::milliseconds stale{200}; ///< 输入 watchdog
    };

    /// 一帧的判决；held=true 表示这是 hold 的上一帧，不是本帧新测量。
    struct HandSample {
        double range_m{0.0}; ///< 最近有支撑深度簇的中位数，米
        double closeness{0.0}; ///< 0 远、1 近
        std::size_t points{0}; ///< 支撑点数/像素数
        bool held{false}; ///< 是否来自 hold
    };

    /// 从一组深度值（米）里找“最近的、有足够支撑的深度簇”，并做 hold。
    class HandTracker {
    public:
        explicit HandTracker(const HandConfig& config);

        /// 用本帧深度（米）更新；找最近的有支撑簇，找不到则尝试 hold。
        [[nodiscard]] std::optional<HandSample> update(const std::vector<float>& depths,
                                                       std::chrono::steady_clock::time_point now);
        /// 在 hold 窗口内返回上一帧；超时清空。
        [[nodiscard]] std::optional<HandSample> held(std::chrono::steady_clock::time_point now);
        /// 丢弃 hold 状态。
        void reset();

        /// 只读配置，供状态/诊断使用。
        [[nodiscard]] const HandConfig& config() const {
            return config_;
        }

    private:
        HandConfig config_;
        std::optional<HandSample> last_; ///< 最后一次真实测量
        std::chrono::steady_clock::time_point last_at_{}; ///< 最后一次真实测量时间
    };

} // namespace duck_audio
