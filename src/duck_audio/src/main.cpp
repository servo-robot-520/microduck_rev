#include <exception>
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "common_cpp/logging/rclcpp_log_handler.hpp"
#include "duck_audio/duck_audio_node.hpp"

int main(const int argc, char** argv) {
    rclcpp::init(argc, argv);
    const auto node = std::make_shared<rclcpp::Node>("duck_audio");
    if (!logging::ros::RclcppLogHandler::try_init_from_node(node)) {
        return 1;
    }

    try {
        const auto audio = std::make_shared<duck_audio::DuckAudioNode>(node);
        rclcpp::executors::MultiThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
        executor.remove_node(node);
    } catch (const std::exception& exception) {
        RCLCPP_FATAL(node->get_logger(), "duck_audio terminated: %s", exception.what());
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    } catch (...) {
        RCLCPP_FATAL(node->get_logger(), "duck_audio terminated with an unknown error");
        logging::ros::RclcppLogHandler::shutdown();
        rclcpp::shutdown();
        return 1;
    }

    logging::ros::RclcppLogHandler::shutdown();
    rclcpp::shutdown();
    return 0;
}
