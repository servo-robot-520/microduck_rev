import launch
import launch_ros
from ament_index_python.packages import get_package_share_directory
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import Command, LaunchConfiguration
from launch_ros.parameter_descriptions import ParameterValue
import os


def generate_launch_description():
    # Get default path
    robot_name_in_model = "duck_description"
    urdf_tutorial_path = get_package_share_directory('duck_description')
    default_model_path = os.path.join(
        urdf_tutorial_path, 'urdf', 'duck_description.urdf.xacro')

    # 由 xacro 生成 robot_description（拼装 duck_description.urdf + gemini.urdf）
    robot_description = ParameterValue(
        Command(['xacro ', LaunchConfiguration('urdf_file')]), value_type=str)

    robot_state_publisher_node = launch_ros.actions.Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{'robot_description': robot_description}]
    )

    # Include another launch file for Gazebo
    launch_gazebo = launch.actions.IncludeLaunchDescription(
        PythonLaunchDescriptionSource([get_package_share_directory(
            'gazebo_ros'), '/launch', '/gazebo.launch.py']),
    )

    # Request Gazebo to spawn the robot
    spawn_entity_node = launch_ros.actions.Node(
        package='gazebo_ros',
        executable='spawn_entity.py',
        arguments=['-topic', '/robot_description',
                   '-entity', robot_name_in_model])

    return launch.LaunchDescription([
        launch.actions.DeclareLaunchArgument('urdf_file', default_value=default_model_path),
        robot_state_publisher_node,
        launch_gazebo,
        spawn_entity_node
    ])
