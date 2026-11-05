# duck_asr_interface

## 简介

`duck_asr_interface` 定义鸭子口令意图接口：

- `DuckIntent`：`duck_asr` 把识别文本映射成语义意图后发布；
- 具体动作由 `duck_behavior` / `duck_control` 消费意图后执行。

本包不包含 ASR 事件、模型、电量查询或 TTS 语义；通用识别接口在 `asr_interface`。

生成命名空间为 `duck_asr_interface`，Python 模块为 `duck_asr_interface_msg`。

## 端点约定

- `~/...` 是 endpoint owner（`duck_asr` 节点自身）使用的私有名称。
- 外部消费者必须传入目标 `duck_asr` 节点的完整节点名（FQN）。
- 默认节点 FQN 为 `/duck_asr`。

| 类型 | owner 私有名称 | 外部消费者 helper |
| --- | --- | --- |
| 意图话题 | `~/intent` | `topic_intent(node_fqn)` |

`node_fqn` 必须是非根绝对 ROS 名称，不能包含 ASCII 空白或中间的空路径段；尾部多余 `/` 会自动移除。

`topic_intent(kDefaultNodeFqn, node->get_namespace())` 会自动得到 `<namespace>/duck_asr/intent`。

例如目标节点是 `/robot_a/duck_asr` 时：

```text
/robot_a/duck_asr/intent
```

## 消息

### `DuckIntent`

```msg
std_msgs/Header header
string session_id
string intent
string utterance
float32 confidence
string[] arg_keys
string[] arg_values
```

`intent` 使用字符串而不是枚举：口令集增加时不需要修改接口包并重编所有消费者。已知意图名由
`duck_asr` 的 `config/duck_commands.yaml` 文档化，消费方按字符串分派，并负责校验未知意图。

`arg_keys` 与 `arg_values` 一一对应；没有参数时两者都为空。

## C++ 使用

```cpp
#include "duck_asr_interface/ros_msg.hpp"

const auto private_intent = duck_asr_interface::kPrivateTopicIntent;

constexpr std::string_view duck_asr_fqn = "/robot_a/duck_asr";
const auto intent_topic = duck_asr_interface::topic_intent(duck_asr_fqn);

duck_asr_interface::MsgDuckIntent msg;
```

## Python 使用

```python
from duck_asr_interface_msg.ros_msg import (
    PRIVATE_TOPIC_INTENT,
    topic_intent,
)

duck_asr_fqn = "/robot_a/duck_asr"
print(PRIVATE_TOPIC_INTENT)      # ~/intent
print(topic_intent(duck_asr_fqn))  # /robot_a/duck_asr/intent
```

## 构建与测试

```bash
source /opt/ros/humble/setup.bash

colcon build --base-paths src --packages-select duck_asr_interface
colcon test --base-paths src --packages-select duck_asr_interface
colcon test-result --test-result-base build/duck_asr_interface/test_results --verbose
```

接口依赖：

- `builtin_interfaces`
- `std_msgs`
- `rosidl_default_generators` / `rosidl_default_runtime`

设计文档：[`docs/duck-ros2-asr-architecture.zh-CN.md`](../../docs/duck-ros2-asr-architecture.zh-CN.md)。
