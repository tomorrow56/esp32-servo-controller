# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tomorrow56
# https://github.com/tomorrow56/esp32-servo-controller

from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package='servo_controller',
            executable='servo_bridge_node.py',
            name='servo_bridge_node',
            output='screen',
        ),
        Node(
            package='servo_controller',
            executable='script_runner_node.py',
            name='script_runner_node',
            output='screen',
        ),
    ])
