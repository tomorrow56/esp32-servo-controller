#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 tomorrow56
# https://github.com/tomorrow56/esp32-servo-controller

"""
script_runner_node.py

ROS2 node that executes servo scripts (same syntax as web_api/serial_api).
Receives script text via HTTP POST and executes it by publishing to ROS2 topics.

HTTP API:
  POST /api/script/upload   body: {"script": "<text>"}
  POST /api/script/execute  starts execution
  POST /api/script/stop     stops execution
  GET  /api/script/status   returns execution status
"""

import json
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

import rclpy
from rclpy.node import Node
from std_msgs.msg import Int32MultiArray


NUM_SERVOS   = 10
HTTP_PORT    = 8081
MAX_IF_DEPTH = 8


class ScriptRunnerNode(Node):

    def __init__(self):
        super().__init__('script_runner_node')

        self.servo_angles  = [90] * NUM_SERVOS
        self.script_lines  = []
        self.is_executing  = False
        self.stop_flag     = False
        self.current_line  = 0
        self._exec_thread  = None
        self._lock         = threading.Lock()

        self.servo_pub  = self.create_publisher(Int32MultiArray, '/servo_command',  10)
        self.servos_pub = self.create_publisher(Int32MultiArray, '/servos_command', 10)
        self.state_sub  = self.create_subscription(
            Int32MultiArray, '/servo_state', self._state_callback, 10)

        self.get_logger().info('script_runner_node started')
        self.get_logger().info(f'Script HTTP API listening on port {HTTP_PORT}')

    def _state_callback(self, msg):
        for i, angle in enumerate(msg.data[:NUM_SERVOS]):
            self.servo_angles[i] = int(angle)

    def upload_script(self, text: str) -> int:
        lines = [l.strip() for l in text.splitlines()]
        with self._lock:
            self.script_lines = lines
            self.current_line = 0
        self.get_logger().info(f'Script uploaded: {len(lines)} lines')
        return len(lines)

    def execute_script(self):
        with self._lock:
            if self.is_executing:
                return False
            self.is_executing = True
            self.stop_flag    = False
            self.current_line = 0
        self._exec_thread = threading.Thread(target=self._run, daemon=True)
        self._exec_thread.start()
        return True

    def stop_script(self):
        self.stop_flag = True

    def _evaluate_condition(self, arg: str) -> bool:
        import re
        m = re.match(r'^servo(\d+)\s*(==|!=|>=|<=|>|<)\s*(\d+)$', arg.strip())
        if not m:
            return False
        ch  = int(m.group(1))
        op  = m.group(2)
        rhs = int(m.group(3))
        lhs = self.servo_angles[ch] if 0 <= ch < NUM_SERVOS else 0
        return {
            '==': lhs == rhs, '!=': lhs != rhs,
            '>':  lhs >  rhs, '>=': lhs >= rhs,
            '<':  lhs <  rhs, '<=': lhs <= rhs,
        }.get(op, False)

    def _run(self):
        lines = self.script_lines[:]
        total = len(lines)

        exec_stack   = [True] + [False] * (MAX_IF_DEPTH - 1)
        else_stack   = [False] * MAX_IF_DEPTH
        depth        = 0

        idx = 0
        while idx < total and not self.stop_flag:
            line = lines[idx].strip()
            idx += 1
            with self._lock:
                self.current_line = idx

            if not line or line.startswith('#') or line.startswith('//'):
                continue

            sp  = line.find(' ')
            cmd = (line[:sp] if sp > 0 else line).lower()
            arg = line[sp + 1:].strip() if sp > 0 else ''
            executing = exec_stack[depth]

            if cmd == 'if':
                if depth < MAX_IF_DEPTH - 1:
                    depth += 1
                exec_stack[depth] = executing and self._evaluate_condition(arg)
                else_stack[depth] = False

            elif cmd == 'else':
                if depth > 0 and not else_stack[depth]:
                    exec_stack[depth] = exec_stack[depth - 1] and not exec_stack[depth]
                    else_stack[depth] = True

            elif cmd == 'endif':
                if depth > 0:
                    depth -= 1

            elif executing:
                if cmd == 'servo':
                    parts = arg.split()
                    if len(parts) >= 2:
                        try:
                            ch    = int(parts[0])
                            angle = max(0, min(180, int(parts[1])))
                        except ValueError:
                            self.get_logger().error(
                                f'servo: invalid argument "{arg}" - skipping')
                            continue
                        msg = Int32MultiArray()
                        msg.data = [ch, angle]
                        self.servo_pub.publish(msg)
                        self.servo_angles[ch] = angle
                        self.get_logger().info(f'servo CH{ch} -> {angle}')

                elif cmd == 'servos':
                    pairs = []
                    data  = []
                    for pair in arg.split():
                        if ':' in pair:
                            try:
                                ch, angle = pair.split(':', 1)
                                ch    = int(ch)
                                angle = max(0, min(180, int(angle)))
                            except ValueError:
                                self.get_logger().error(
                                    f'servos: invalid pair "{pair}" - skipping')
                                continue
                            pairs.append((ch, angle))
                            data.extend([ch, angle])
                    if data:
                        msg = Int32MultiArray()
                        msg.data = data
                        self.servos_pub.publish(msg)
                        for ch, angle in pairs:
                            self.servo_angles[ch] = angle
                        self.get_logger().info(f'servos: {pairs}')

                elif cmd == 'wait':
                    ms = int(arg) if arg.isdigit() else 0
                    end = time.time() + ms / 1000.0
                    while time.time() < end and not self.stop_flag:
                        time.sleep(0.01)

                else:
                    self.get_logger().warn(f'Unknown command: {line}')

        with self._lock:
            self.is_executing = False
            self.stop_flag    = False
        self.get_logger().info('Script execution finished')


# グローバルノード参照
_node: ScriptRunnerNode = None


class ScriptApiHandler(BaseHTTPRequestHandler):

    def log_message(self, format, *args):
        pass

    def _send_json(self, code: int, body):
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
        if self.path == '/api/script/status':
            with _node._lock:
                executing    = _node.is_executing
                current_line = _node.current_line
                total_lines  = len(_node.script_lines)
            self._send_json(200, {
                'is_executing': executing,
                'current_line': current_line,
                'total_lines':  total_lines,
            })
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

        if path == '/api/script/upload':
            script = data.get('script', '')
            lines  = _node.upload_script(script)
            self._send_json(200, {'status': 'ok', 'lines': lines})

        elif path == '/api/script/execute':
            ok = _node.execute_script()
            if ok:
                self._send_json(200, {'status': 'executing'})
            else:
                self._send_json(409, {'error': 'Already executing'})

        elif path == '/api/script/stop':
            _node.stop_script()
            self._send_json(200, {'status': 'stopped'})

        else:
            self._send_json(404, {'error': 'Not found'})


def start_http_server():
    server = HTTPServer(('0.0.0.0', HTTP_PORT), ScriptApiHandler)
    server.serve_forever()


def main(args=None):
    global _node
    rclpy.init(args=args)
    _node = ScriptRunnerNode()

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
