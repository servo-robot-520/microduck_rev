import os

from ament_index_python.packages import PackageNotFoundError, get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration


def generate_launch_description() -> LaunchDescription:
    try:
        orbbec_share = get_package_share_directory("orbbec_camera")
        orbbec_launch = os.path.join(orbbec_share, "launch", "ob_camera.launch.py")
        if not os.path.isfile(orbbec_launch):
            raise PackageNotFoundError("orbbec_camera", orbbec_share)
    except PackageNotFoundError as err:
        raise RuntimeError(
            f"No available orbbec_camera package found (resolved to {err}). "
            f"Please install the Orbbec camera driver and source its workspace; "
            f"if already installed, check for any residual old builds obscuring the installation path."
        ) from err

    return LaunchDescription([
        DeclareLaunchArgument("camera_name", default_value="camera"),
        DeclareLaunchArgument("serial_number", default_value=""),
        # Gemini (SV1301S_U3) 本机固件仅提供 Y12 深度 / Y10 IR 格式，
        # 驱动默认的 Y14 / Y8 无法匹配流配置。
        DeclareLaunchArgument("depth_format", default_value="Y12"),
        DeclareLaunchArgument("ir_format", default_value="Y10"),
        # TF 统一由 duck_description 的 URDF（robot_state_publisher）提供；
        # 驱动若同时发布同名出厂标定 TF 会产生双发布者冲突。
        DeclareLaunchArgument("publish_tf", default_value="false"),

        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                os.path.join(orbbec_share, "launch", "ob_camera.launch.py")),
            launch_arguments={
                "camera_name": LaunchConfiguration("camera_name"),
                "serial_number": LaunchConfiguration("serial_number"),
                "depth_format": LaunchConfiguration("depth_format"),
                "ir_format": LaunchConfiguration("ir_format"),
                "publish_tf": LaunchConfiguration("publish_tf"),
            }.items(),
        ),
    ])
