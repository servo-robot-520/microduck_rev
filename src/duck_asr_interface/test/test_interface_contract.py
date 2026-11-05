"""验证 duck_asr_interface 的接口字段与端点命名契约。"""

import importlib.util
from pathlib import Path

import pytest

PACKAGE_ROOT = Path(__file__).resolve().parents[1]


def lines(relative: str) -> list[str]:
    """返回接口文件中去掉空行、注释与分隔符后的内容行。"""
    return [
        line.strip()
        for line in (PACKAGE_ROOT / relative).read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#") and line.strip() != "---"
    ]


def fields(relative: str) -> list[str]:
    """返回接口文件中的字段行，排除常量定义。"""
    return [line for line in lines(relative) if "=" not in line]


def load_ros_msg():
    """直接加载 Python 辅助模块，避免依赖安装后的生成包。"""
    module_path = PACKAGE_ROOT / "python/duck_asr_interface_msg/ros_msg.py"
    spec = importlib.util.spec_from_file_location("duck_asr_interface_ros_msg", module_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_duck_intent_contract() -> None:
    """DuckIntent 必须能表达意图、原始文本与参数。"""
    assert fields("msg/DuckIntent.msg") == [
        "std_msgs/Header header",
        "string session_id",
        "string intent",
        "string utterance",
        "float32 confidence",
        "string[] arg_keys",
        "string[] arg_values",
    ]


def test_python_endpoint_helpers() -> None:
    """Python helper 必须区分私有名称与目标节点 FQN。"""
    ros_msg = load_ros_msg()

    assert ros_msg.DEFAULT_NODE_FQN == "/duck_asr"
    assert ros_msg.PRIVATE_TOPIC_INTENT == "~/intent"
    assert ros_msg.topic_intent() == "/duck_asr/intent"
    assert ros_msg.topic_intent("/robot_a/duck_asr") == "/robot_a/duck_asr/intent"
    assert ros_msg.topic_intent("/robot_a/duck_asr///") == "/robot_a/duck_asr/intent"
    assert ros_msg.qualify_node_fqn("/robot_a") == "/robot_a/duck_asr"
    assert ros_msg.topic_intent(ros_msg.DEFAULT_NODE_FQN, "/robot_a") == "/robot_a/duck_asr/intent"


def test_python_endpoint_helpers_reject_invalid_node_fqn() -> None:
    """Python helper 必须拒绝空、相对、根路径与含空白的节点名。"""
    ros_msg = load_ros_msg()
    for invalid in ("", "duck_asr", "/", "/duck//asr", "/duck_asr\t"):
        with pytest.raises(ValueError, match="node_fqn"):
            ros_msg.topic_intent(invalid)


def test_helper_files_expose_consistent_private_names() -> None:
    """C++ 与 Python helper 必须使用同一套私有 endpoint 名称。"""
    helper_files = (
        "include/duck_asr_interface/ros_msg.hpp",
        "python/duck_asr_interface_msg/ros_msg.py",
    )
    required = (
        "~/intent",
        "topic_intent",
    )
    for relative in helper_files:
        content = (PACKAGE_ROOT / relative).read_text(encoding="utf-8")
        assert all(item in content for item in required), relative
