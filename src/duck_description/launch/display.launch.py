import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    package_dir = get_package_share_directory('duck_description')

    # URDF file path
    urdf_file = os.path.join(package_dir, 'urdf', 'duck_description.urdf')

    # Read URDF file content
    with open(urdf_file, 'r') as file:
        robot_description = file.read()

    # Create LaunchDescription
    return LaunchDescription([

        # Declare urdf_file argument to allow external input
        DeclareLaunchArgument('urdf_file', default_value=urdf_file),

        # Launch joint_state_publisher_gui node
        Node(
            package='joint_state_publisher_gui',
            executable='joint_state_publisher_gui',
            name='joint_state_publisher_gui',
            output='screen'
        ),

        # Launch robot_state_publisher node with robot_description parameter
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            name='robot_state_publisher',
            output='screen',
            parameters=[{'robot_description': robot_description}]
        ),

        # Launch rviz2 node with robot_description parameter
        Node(
            package='rviz2',
            executable='rviz2',
            name='rviz2',
            output='screen',
            parameters=[{'robot_description': robot_description}]
        ),
    ])
    
