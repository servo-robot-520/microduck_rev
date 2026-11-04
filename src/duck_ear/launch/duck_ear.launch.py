from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description() -> LaunchDescription:
    default_config = get_package_share_directory("duck_ear") + "/config/duck_ear.yaml"
    return LaunchDescription([
        DeclareLaunchArgument("config", default_value=default_config),
        DeclareLaunchArgument("namespace", default_value=""),
        DeclareLaunchArgument("node_name", default_value="duck_ear"),
        Node(
            package="duck_ear",
            executable="duck_ear_node",
            name=LaunchConfiguration("node_name"),
            namespace=LaunchConfiguration("namespace"),
            parameters=[LaunchConfiguration("config")],
            output="screen",
        ),
    ])
