#include "duck_audio/duck_audio_node.hpp"

#include <chrono>
#include <utility>

#include "duck_audio_interface/ros_msg.hpp"

namespace duck_audio {
namespace {

// 组合节点可能已经被调用方声明过参数；先 has_parameter 再 declare，避免重复声明异常。
template <typename T>
T parameter(const rclcpp::Node::SharedPtr& node, const std::string& name, const T& fallback) {
    if (!node->has_parameter(name)) {
        node->declare_parameter<T>(name, fallback);
    }
    return node->get_parameter(name).get_value<T>();
}

// VoiceState.state 只暴露稳定字符串，客户端不需要知道 C++ enum。
std::string owner_name(const AudioOwner owner) {
    switch (owner) {
        case AudioOwner::Wheee:
            return "wheee";
        case AudioOwner::Theremin:
            return "theremin";
        case AudioOwner::None:
        default:
            return "idle";
    }
}

} // namespace

DuckAudioNode::DuckAudioNode(rclcpp::Node::SharedPtr node) : node_(std::move(node)) {
    if (node_ == nullptr) {
        throw std::invalid_argument("DuckAudioNode requires a non-null node");
    }

    // 音库先于各模块初始化：没有音库时后面全部走“拒绝但不崩溃”的分支。
    VoiceRuntime::Config runtime_config;
    runtime_config.bank = parameter<std::string>(node_, "voice.bank", "/var/lib/robot/sounds");
    runtime_config.seed_override =
            static_cast<uint32_t>(parameter<int>(node_, "voice.seed", 0));
    runtime_ = std::make_unique<VoiceRuntime>(runtime_config, node_->get_logger());
    (void)runtime_->initialize();

    voice_ = std::make_unique<VoiceModule>(*node_, *runtime_, session_);
    wheee_ = std::make_unique<WheeePlayer>(*node_, *runtime_, session_);

    // 深度回调放在独立 group，避免解析大点云时阻塞 service/voice 回调。
    theremin_group_ = node_->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    if (parameter<bool>(node_, "theremin.enabled", true)) {
        GeminiHandSource::Config source;
        source.input = parameter<std::string>(node_, "theremin.input", "depth_image");
        source.depth_topic =
                parameter<std::string>(node_, "theremin.depth_topic", "/camera/depth/image_raw");
        source.cloud_topic =
                parameter<std::string>(node_, "theremin.cloud_topic", "/camera/depth/points");
        source.target_frame = parameter<std::string>(node_, "theremin.target_frame", "");
        source.hand.near_m = parameter<double>(node_, "theremin.near_m", 0.12);
        source.hand.far_m = parameter<double>(node_, "theremin.far_m", 0.70);
        source.hand.lateral_m = parameter<double>(node_, "theremin.lateral_m", 0.35);
        source.hand.y_min_m = parameter<double>(node_, "theremin.y_min_m", -0.25);
        source.hand.y_max_m = parameter<double>(node_, "theremin.y_max_m", 0.20);
        source.hand.roi_x_frac = parameter<double>(node_, "theremin.roi_x_frac", 0.60);
        source.hand.roi_y0_frac = parameter<double>(node_, "theremin.roi_y0_frac", 0.20);
        source.hand.roi_y1_frac = parameter<double>(node_, "theremin.roi_y1_frac", 0.80);
        source.hand.min_points =
                static_cast<std::size_t>(parameter<int>(node_, "theremin.min_points", 40));
        source.hand.bin_m = parameter<double>(node_, "theremin.bin_m", 0.02);
        source.hand.hold = std::chrono::milliseconds(parameter<int>(node_, "theremin.hold_ms", 250));
        source.hand.stale = std::chrono::milliseconds(parameter<int>(node_, "theremin.stale_ms", 200));
        if (source.input != "depth_image" && source.input != "point_cloud") {
            RCLCPP_WARN(node_->get_logger(), "unknown theremin.input '%s'; using depth_image",
                        source.input.c_str());
            source.input = "depth_image";
        }
        const double rolloff = parameter<double>(node_, "theremin.rolloff_hz", 300.0);
        theremin_ = std::make_unique<ThereminModule>(*node_, *runtime_, session_, std::move(source),
                                                     theremin_group_, rolloff);
    } else {
        RCLCPP_INFO(node_->get_logger(), "theremin disabled by parameter");
    }

    // 一次性声音与特雷门共用默认 callback group，保证 AudioSession 的检查/切换串行。
    play_service_ = node_->create_service<duck_audio_interface::srv::PlayVoice>(
            "~/voice/play",
            [this](std::shared_ptr<duck_audio_interface::srv::PlayVoice::Request> request,
                   std::shared_ptr<duck_audio_interface::srv::PlayVoice::Response> response) {
                handle_play_voice(std::move(request), std::move(response));
            });
    theremin_service_ = node_->create_service<duck_audio_interface::srv::SetTheremin>(
            "~/theremin/enable",
            [this](std::shared_ptr<duck_audio_interface::srv::SetTheremin::Request> request,
                   std::shared_ptr<duck_audio_interface::srv::SetTheremin::Response> response) {
                handle_set_theremin(std::move(request), std::move(response));
            });

    hold_subscription_ = node_->create_subscription<duck_audio_interface::msg::WheeeHold>(
            "~/voice/wheee_hold", rclcpp::QoS(rclcpp::KeepLast(1)).reliable(),
            [this](const duck_audio_interface::msg::WheeeHold::ConstSharedPtr& message) {
                if (runtime_->has_voice() && wheee_ != nullptr) {
                    wheee_->on_hold(message->held);
                }
            });

    // transient_local：晚到的客户端也能立即拿到当前音库/seed/状态。
    state_publisher_ = node_->create_publisher<duck_audio_interface::msg::VoiceState>(
            "~/voice/state", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());
    tick_timer_ = node_->create_wall_timer(std::chrono::milliseconds(20), [this] { tick(); });
    state_timer_ =
            node_->create_wall_timer(std::chrono::milliseconds(100), [this] { publish_state(); });

    // greet 是启动提示；失败只影响声音，不影响节点继续运行。
    if (runtime_->has_voice() && parameter<bool>(node_, "voice.greet", true)) {
        (void)voice_->play("greet", 0xFFFFFFFFU);
    }
    publish_state();
}

DuckAudioNode::~DuckAudioNode() {
    // 先停线程，再释放模块和 Rust 句柄；Node 仍存活，writer 还能安全发布 eos。
    if (theremin_ != nullptr) {
        theremin_->stop();
    }
    if (wheee_ != nullptr) {
        wheee_->stop();
    }
    theremin_.reset();
    wheee_.reset();
    voice_.reset();
    runtime_.reset();
}

// 50 Hz：wheee 先处理 lease/落地，再让特雷门更新参数和状态。
void DuckAudioNode::tick() {
    if (wheee_ != nullptr) {
        wheee_->tick();
    }
    if (theremin_ != nullptr) {
        theremin_->tick();
    }
}

void DuckAudioNode::publish_state() {
    // 状态是快照，不读取任何可能阻塞的音频线程状态。
    duck_audio_interface::msg::VoiceState state;
    state.header.stamp = node_->now();
    state.header.frame_id = "duck_audio";
    state.has_voice = runtime_ != nullptr && runtime_->has_voice();
    state.seed = runtime_ != nullptr ? runtime_->seed() : 0;
    state.bank = runtime_ != nullptr ? runtime_->bank() : std::string{};
    state.bank_version = runtime_ != nullptr ? runtime_->bank_version() : 0;
    state.state = state.has_voice ? owner_name(session_.owner()) : "error";
    state.last_tag = voice_ != nullptr ? voice_->last_tag() : std::string{};
    state.last_error = runtime_ != nullptr ? runtime_->last_error() : std::string{};
    state_publisher_->publish(state);
}

void DuckAudioNode::handle_play_voice(
        std::shared_ptr<duck_audio_interface::srv::PlayVoice::Request> request,
        std::shared_ptr<duck_audio_interface::srv::PlayVoice::Response> response) {
    if (runtime_ == nullptr || !runtime_->has_voice()) {
        response->accepted = false;
        response->reason = "this robot has no voice bank";
        return;
    }
    // 一次性音效；wheee 由 ~/voice/wheee_hold 处理，不经过该服务。
    const auto result = voice_->play(request->tag, request->variant);
    response->accepted = result.accepted;
    response->request_id = result.request_id;
    response->reason = result.reason;
    publish_state();
}

void DuckAudioNode::handle_set_theremin(
        std::shared_ptr<duck_audio_interface::srv::SetTheremin::Request> request,
        std::shared_ptr<duck_audio_interface::srv::SetTheremin::Response> response) {
    if (theremin_ == nullptr) {
        response->accepted = false;
        response->reason = "theremin is disabled by parameter";
        return;
    }
    const auto result = theremin_->enable(request->active);
    response->accepted = result.accepted;
    response->reason = result.reason;
    publish_state();
}

} // namespace duck_audio
