#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tomorrow56
# https://github.com/tomorrow56/esp32-servo-controller

"""
servo_bridge_node.py

ROS2 node that bridges HTTP REST API requests to ROS2 topics.
Provides the same REST API interface as web_api for compatibility.

Topics published:
  /servo_command  (std_msgs/Int32MultiArray) [channel, angle]
  /servos_command (std_msgs/Int32MultiArray) [ch0, angle0, ch1, angle1, ...]

Topics subscribed:
  /servo_state    (std_msgs/Int32MultiArray) [angle0..angle9]

HTTP API (compatible with web_api):
  POST /api/servo/{channel}  body: {"angle": <int>}
  POST /api/servos           body: {"servos": [{"channel": <int>, "angle": <int>}, ...]}
  GET  /api/servos           returns current servo angles
"""

import json
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

import rclpy
from rclpy.node import Node
from std_msgs.msg import Int32MultiArray


NUM_SERVOS = 10
HTTP_PORT  = 8080


class ServoBridgeNode(Node):

    def __init__(self):
        super().__init__('servo_bridge_node')

        self.servo_angles = [90] * NUM_SERVOS

        self.servo_pub  = self.create_publisher(Int32MultiArray, '/servo_command',  10)
        self.servos_pub = self.create_publisher(Int32MultiArray, '/servos_command', 10)
        self.state_sub  = self.create_subscription(
            Int32MultiArray, '/servo_state', self._state_callback, 10)

        self.get_logger().info('servo_bridge_node started')
        self.get_logger().info(f'HTTP API listening on port {HTTP_PORT}')

    def _state_callback(self, msg):
        for i, angle in enumerate(msg.data[:NUM_SERVOS]):
            self.servo_angles[i] = int(angle)

    def publish_servo(self, channel: int, angle: int):
        msg = Int32MultiArray()
        msg.data = [channel, angle]
        self.servo_pub.publish(msg)
        self.get_logger().info(f'servo_command: CH{channel} -> {angle}')

    def publish_servos(self, pairs: list):
        msg = Int32MultiArray()
        msg.data = []
        for ch, angle in pairs:
            msg.data.extend([ch, angle])
        self.servos_pub.publish(msg)
        self.get_logger().info(f'servos_command: {pairs}')


# グローバルノード参照（HTTPハンドラから使用）
_node: ServoBridgeNode = None


class ApiHandler(BaseHTTPRequestHandler):

    def log_message(self, format, *args):
        pass  # ROS2ログに任せる

    def _send_json(self, code: int, body: dict):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        if self.path == '/api/servos':
            servos = [
                {'channel': i, 'angle': _node.servo_angles[i]}
                for i in range(NUM_SERVOS)
            ]
            self._send_json(200, {'servos': servos})
        elif self.path == '/api/servo/status':
            servos = [
                {'channel': i, 'angle': _node.servo_angles[i]}
                for i in range(NUM_SERVOS)
            ]
            self._send_json(200, {'servos': servos})
        else:
            self._send_json(404, {'error': 'Not found'})

    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        body   = self.rfile.read(length)
        try:
            data = json.loads(body) if body else {}
        except json.JSONDecodeError:
            self._send_json(400, {'error': 'Invalid JSON'})
            return

        path = self.path.rstrip('/')

        # POST /api/servo/{channel}
        if path.startswith('/api/servo/'):
            parts = path.split('/')
            try:
                ch    = int(parts[-1])
                angle = int(data.get('angle', 90))
            except (ValueError, IndexError):
                self._send_json(400, {'error': 'Invalid parameters'})
                return
            angle = max(0, min(180, angle))
            _node.publish_servo(ch, angle)
            self._send_json(200, {'channel': ch, 'angle': angle, 'status': 'success'})

        # POST /api/servos
        elif path == '/api/servos':
            pairs = []
            for item in data.get('servos', []):
                ch    = int(item.get('channel', 0))
                angle = max(0, min(180, int(item.get('angle', 90))))
                pairs.append((ch, angle))
            if not pairs:
                self._send_json(400, {'error': 'No servo data'})
                return
            _node.publish_servos(pairs)
            result = [{'channel': ch, 'angle': a, 'status': 'success'} for ch, a in pairs]
            self._send_json(200, result)

        else:
            self._send_json(404, {'error': 'Not found'})


def start_http_server():
    server = HTTPServer(('0.0.0.0', HTTP_PORT), ApiHandler)
    server.serve_forever()


def main(args=None):
    global _node
    rclpy.init(args=args)
    _node = ServoBridgeNode()

    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()

    try:
        rclpy.spin(_node)
    except KeyboardInterrupt:
        pass
    finally:
        _node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
