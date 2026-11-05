"""检查 duck_audio_interface 的 Python endpoint helper 与消息/服务字段。"""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest


def load_ros_msg():
    path = Path(__file__).parents[1] / "python" / "duck_audio_interface_msg" / "ros_msg.py"
    spec = importlib.util.spec_from_file_location("duck_audio_interface_msg.ros_msg", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_python_endpoint_helpers() -> None:
    ros_msg = load_ros_msg()
    assert ros_msg.service_play_voice() == "/duck_audio/voice/play"
    assert ros_msg.topic_wheee_hold() == "/duck_audio/voice/wheee_hold"
    assert ros_msg.topic_voice_state() == "/duck_audio/voice/state"
    assert ros_msg.service_set_theremin() == "/duck_audio/theremin/enable"
    assert ros_msg.topic_theremin_state() == "/duck_audio/theremin/state"
    assert ros_msg.service_set_chorale() == "/duck_audio/chorale/enable"
    assert ros_msg.topic_chorale_state() == "/duck_audio/chorale/state"
    assert ros_msg.topic_mouth() == "/duck_audio/mouth"
    assert ros_msg.service_play_voice("/robot_a/duck_audio/") == "/robot_a/duck_audio/voice/play"
    assert ros_msg.qualify_node_fqn("/robot_a") == "/robot_a/duck_audio"
    assert ros_msg.topic_petting_event(ros_msg.DEFAULT_NODE_FQN, "/robot_a") == "/robot_a/duck_audio/petting/event"


def test_python_endpoint_helpers_reject_invalid_node_fqn() -> None:
    ros_msg = load_ros_msg()
    for bad in ("", "duck/audio", "/", "/duck//audio", "/duck_audio\t"):
        with pytest.raises(ValueError):
            ros_msg.service_play_voice(bad)


def test_play_voice_fields() -> None:
    path = Path(__file__).parents[1] / "srv" / "PlayVoice.srv"
    request, response = path.read_text(encoding="utf-8").split("---")
    for field in ("string tag", "uint32 variant"):
        assert field in request
    for field in ("bool accepted", "string request_id", "string reason"):
        assert field in response


def test_state_fields() -> None:
    msg_dir = Path(__file__).parents[1] / "msg"
    voice = (msg_dir / "VoiceState.msg").read_text(encoding="utf-8")
    theremin = (msg_dir / "ThereminState.msg").read_text(encoding="utf-8")
    chorale = (msg_dir / "ChoraleState.msg").read_text(encoding="utf-8")
    assert "bool has_voice" in voice and "uint32 seed" in voice
    assert "bool sensor_ok" in theremin and "float32 closeness" in theremin
    assert "uint8 PART_BASS=0" in chorale and "float32 position_beats" in chorale
