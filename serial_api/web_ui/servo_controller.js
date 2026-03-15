// グローバル変数
let port = null;
let reader = null;
let writer = null;
let readableStreamClosed = null;
let writableStreamClosed = null;
let isConnected = false;
let isRunning = false;
let shouldStop = false;
const numServos = 10;
let servoAngles = new Array(numServos).fill(90);

// UI要素
const connectBtn = document.getElementById('connectBtn');
const disconnectBtn = document.getElementById('disconnectBtn');
const runBtn = document.getElementById('runBtn');
const stopBtn = document.getElementById('stopBtn');
const saveBtn = document.getElementById('saveBtn');
const fileInput = document.getElementById('fileInput');
const codeEditor = document.getElementById('codeEditor');
const consoleDiv = document.getElementById('console');
const statusBadge = document.getElementById('statusBadge');
const fileName = document.getElementById('fileName');
const progressFill = document.getElementById('progressFill');
const servoGrid = document.getElementById('servoGrid');

// イベントリスナー
connectBtn.addEventListener('click', connectToESP32);
disconnectBtn.addEventListener('click', disconnectFromESP32);
runBtn.addEventListener('click', runScript);
stopBtn.addEventListener('click', stopScript);
saveBtn.addEventListener('click', saveScript);
fileInput.addEventListener('change', loadScriptFile);

// 初期化
initServoGrid();

// サーボグリッドの初期化
function initServoGrid() {
  servoGrid.innerHTML = '';
  for (let i = 0; i < numServos; i++) {
    const item = document.createElement('div');
    item.className = 'servo-item';
    item.innerHTML = `
      <div class="servo-title">サーボ ${i}</div>
      <div class="servo-value" id="servo_val_${i}">${servoAngles[i]}°</div>
    `;
    servoGrid.appendChild(item);
  }
}

// ESP32に接続
async function connectToESP32() {
  try {
    // Web Serial APIがサポートされているか確認
    if (!('serial' in navigator)) {
      logConsole('error', 'Web Serial APIがサポートされていません。Chrome/Edgeをお使いください。');
      return;
    }

    // シリアルポートを選択
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });

    // Reader と Writer を取得
    const textDecoder = new TextDecoderStream();
    readableStreamClosed = port.readable.pipeTo(textDecoder.writable);
    reader = textDecoder.readable.getReader();

    const textEncoder = new TextEncoderStream();
    writableStreamClosed = textEncoder.readable.pipeTo(port.writable);
    writer = textEncoder.writable.getWriter();

    isConnected = true;
    updateConnectionStatus();
    logConsole('success', 'ESP32に接続しました');

    // ESP32からのメッセージを受信
    readFromSerial();

    // 初期状態を取得
    await sendCommand('getall');

  } catch (error) {
    logConsole('error', `接続エラー: ${error.message}`);
  }
}

// ESP32から切断
async function disconnectFromESP32() {
  try {
    isConnected = false;

    if (reader) {
      await reader.cancel();
      reader = null;
    }
    // pipeTo()のPromiseが解決するまで待つ（readableストリームのロック解放）
    if (readableStreamClosed) {
      await readableStreamClosed.catch(() => {});
      readableStreamClosed = null;
    }

    if (writer) {
      await writer.close();
      writer = null;
    }
    if (writableStreamClosed) {
      await writableStreamClosed.catch(() => {});
      writableStreamClosed = null;
    }

    if (port) {
      await port.close();
      port = null;
    }

    updateConnectionStatus();
    logConsole('info', 'ESP32から切断しました');

  } catch (error) {
    logConsole('error', `切断エラー: ${error.message}`);
  }
}

// シリアルからデータを読み取る
async function readFromSerial() {
  try {
    let buffer = '';
    while (true) {
      const { value, done } = await reader.read();
      if (done) {
        break;
      }
      buffer += value;
      
      // 改行で分割
      const lines = buffer.split('\n');
      buffer = lines.pop(); // 最後の不完全な行を保持
      
      for (const line of lines) {
        handleSerialMessage(line.trim());
      }
    }
  } catch (error) {
    if (error.name !== 'NetworkError') {
      logConsole('error', `読み取りエラー: ${error.message}`);
    }
  }
}

// シリアルメッセージの処理
function handleSerialMessage(message) {
  if (!message) return;

  logConsole('info', `← ${message}`);

  // ANGLES: 応答の処理
  if (message.startsWith('ANGLES:')) {
    const angles = message.substring(7).split(',');
    angles.forEach((angle, index) => {
      if (index < numServos) {
        servoAngles[index] = parseInt(angle);
        updateServoDisplay(index);
      }
    });
  }
  
  // ANGLE: 応答の処理
  if (message.startsWith('ANGLE:')) {
    const parts = message.substring(6).split(':');
    const channel = parseInt(parts[0]);
    const angle = parseInt(parts[1]);
    if (channel >= 0 && channel < numServos) {
      servoAngles[channel] = angle;
      updateServoDisplay(channel);
    }
  }
}

// コマンドを送信
async function sendCommand(command) {
  if (!isConnected || !writer) {
    logConsole('error', 'ESP32に接続されていません');
    return false;
  }

  try {
    logConsole('info', `→ ${command}`);
    await writer.write(command + '\n');
    
    // 応答を待つ（簡易実装）
    await new Promise(resolve => setTimeout(resolve, 50));
    
    return true;
  } catch (error) {
    logConsole('error', `送信エラー: ${error.message}`);
    return false;
  }
}

// スクリプトを実行
async function runScript() {
  const script = codeEditor.value;
  if (!script.trim()) {
    logConsole('warning', 'スクリプトが空です');
    return;
  }

  if (!isConnected) {
    logConsole('error', 'ESP32に接続してください');
    return;
  }

  isRunning = true;
  shouldStop = false;
  updateRunningStatus();
  logConsole('success', '=== スクリプト実行開始 ===');

  try {
    const ast = parseScript(script);
    await executeAST(ast);
    logConsole('success', '=== スクリプト実行完了 ===');
  } catch (error) {
    logConsole('error', `実行エラー: ${error.message}`);
  } finally {
    isRunning = false;
    shouldStop = false;
    updateRunningStatus();
    progressFill.style.width = '0%';
  }
}

// スクリプトを停止
function stopScript() {
  shouldStop = true;
  logConsole('warning', 'スクリプトを停止しています...');
}

// スクリプトをパース
function parseScript(script) {
  const lines = script.split('\n');
  const ast = [];
  const functions = {};
  let currentFunction = null;
  let braceStack = [];

  for (let i = 0; i < lines.length; i++) {
    let line = lines[i].trim();
    
    // コメントを削除
    if (line.startsWith('#') || line.startsWith('//')) {
      continue;
    }
    
    // 空行をスキップ
    if (!line) {
      continue;
    }

    // 関数定義の開始
    if (line.startsWith('function ')) {
      const parts = line.substring(9).split(' ');
      const funcName = parts[0];
      const params = parts.slice(1).filter(p => p !== '{');
      
      currentFunction = {
        name: funcName,
        params: params,
        body: []
      };
      
      if (line.includes('{')) {
        braceStack.push('function');
      }
      continue;
    }

    // ブロックの開始
    if (line.includes('{')) {
      const blockType = line.split(' ')[0];
      braceStack.push(blockType);
      
      if (blockType === 'repeat') {
        const count = parseInt(line.split(' ')[1]);
        const block = { type: 'repeat', count: count, body: [] };
        
        if (currentFunction) {
          currentFunction.body.push(block);
        } else {
          ast.push(block);
        }
        continue;
      } else if (blockType === 'loop') {
        const block = { type: 'loop', body: [] };
        
        if (currentFunction) {
          currentFunction.body.push(block);
        } else {
          ast.push(block);
        }
        continue;
      }
    }

    // ブロックの終了
    if (line === '}') {
      const blockType = braceStack.pop();
      
      if (blockType === 'function') {
        functions[currentFunction.name] = currentFunction;
        currentFunction = null;
      }
      continue;
    }

    // コマンドをパース
    const command = parseCommand(line);
    
    if (currentFunction) {
      currentFunction.body.push(command);
    } else if (braceStack.length > 0) {
      // ブロック内のコマンドを最後のブロックに追加
      addToLastBlock(ast, command);
    } else {
      ast.push(command);
    }
  }

  return { ast, functions };
}

// 最後のブロックにコマンドを追加
function addToLastBlock(ast, command) {
  let current = ast[ast.length - 1];
  
  while (current && current.body && current.body.length > 0) {
    const last = current.body[current.body.length - 1];
    if (last.type === 'repeat' || last.type === 'loop') {
      current = last;
    } else {
      break;
    }
  }
  
  if (current && current.body) {
    current.body.push(command);
  }
}

// コマンドをパース
function parseCommand(line) {
  const parts = line.split(' ');
  const cmd = parts[0];

  if (cmd === 'servo') {
    return {
      type: 'servo',
      channel: parseInt(parts[1]),
      angle: parseInt(parts[2])
    };
  } else if (cmd === 'servos') {
    const servos = [];
    for (let i = 1; i < parts.length; i++) {
      const [channel, angle] = parts[i].split(':');
      servos.push({ channel: parseInt(channel), angle: parseInt(angle) });
    }
    return { type: 'servos', servos: servos };
  } else if (cmd === 'wait') {
    return { type: 'wait', duration: parseInt(parts[1]) };
  } else if (cmd === 'call') {
    return {
      type: 'call',
      function: parts[1],
      args: parts.slice(2)
    };
  } else {
    return { type: 'unknown', line: line };
  }
}

// ASTを実行
async function executeAST(parsed, context = {}) {
  const { ast, functions } = parsed;
  
  for (const node of ast) {
    if (shouldStop) {
      break;
    }
    await executeNode(node, functions, context);
  }
}

// ノードを実行
async function executeNode(node, functions, context) {
  if (shouldStop) return;

  switch (node.type) {
    case 'servo':
      await sendCommand(`servo ${node.channel} ${node.angle}`);
      servoAngles[node.channel] = node.angle;
      updateServoDisplay(node.channel);
      break;

    case 'servos':
      const servoCmd = 'servos ' + node.servos.map(s => `${s.channel}:${s.angle}`).join(' ');
      await sendCommand(servoCmd);
      node.servos.forEach(s => {
        servoAngles[s.channel] = s.angle;
        updateServoDisplay(s.channel);
      });
      break;

    case 'wait':
      await new Promise(resolve => setTimeout(resolve, node.duration));
      break;

    case 'repeat':
      for (let i = 0; i < node.count; i++) {
        if (shouldStop) break;
        for (const childNode of node.body) {
          await executeNode(childNode, functions, context);
        }
      }
      break;

    case 'loop':
      while (!shouldStop) {
        for (const childNode of node.body) {
          if (shouldStop) break;
          await executeNode(childNode, functions, context);
        }
      }
      break;

    case 'call':
      const func = functions[node.function];
      if (func) {
        const newContext = {};
        func.params.forEach((param, index) => {
          newContext['$' + (index + 1)] = node.args[index];
        });
        
        for (const childNode of func.body) {
          if (shouldStop) break;
          await executeNode(replaceParams(childNode, newContext), functions, context);
        }
      } else {
        logConsole('error', `関数 ${node.function} が見つかりません`);
      }
      break;

    default:
      logConsole('warning', `未知のコマンド: ${node.line}`);
  }
}

// パラメータを置換
function replaceParams(node, context) {
  const newNode = JSON.parse(JSON.stringify(node));
  
  if (newNode.angle && typeof newNode.angle === 'string' && newNode.angle.startsWith('$')) {
    newNode.angle = parseInt(context[newNode.angle]);
  }
  
  return newNode;
}

// サーボ表示を更新
function updateServoDisplay(channel) {
  const elem = document.getElementById(`servo_val_${channel}`);
  if (elem) {
    elem.textContent = servoAngles[channel] + '°';
  }
}

// サーボ状態を更新
async function refreshServos() {
  if (!isConnected) {
    logConsole('warning', 'ESP32に接続してください');
    return;
  }
  await sendCommand('getall');
}

// 接続状態を更新
function updateConnectionStatus() {
  if (isConnected) {
    statusBadge.textContent = '接続済み';
    statusBadge.className = 'status-badge status-connected';
    connectBtn.disabled = true;
    disconnectBtn.disabled = false;
    runBtn.disabled = false;
  } else {
    statusBadge.textContent = '未接続';
    statusBadge.className = 'status-badge status-disconnected';
    connectBtn.disabled = false;
    disconnectBtn.disabled = true;
    runBtn.disabled = true;
    stopBtn.disabled = true;
  }
}

// 実行状態を更新
function updateRunningStatus() {
  if (isRunning) {
    statusBadge.textContent = '実行中';
    statusBadge.className = 'status-badge status-running';
    runBtn.disabled = true;
    stopBtn.disabled = false;
  } else {
    updateConnectionStatus();
    stopBtn.disabled = true;
  }
}

// コンソールにログを追加
function logConsole(type, message) {
  const line = document.createElement('div');
  line.className = `console-line console-${type}`;
  const timestamp = new Date().toLocaleTimeString();
  line.textContent = `[${timestamp}] ${message}`;
  consoleDiv.appendChild(line);
  consoleDiv.scrollTop = consoleDiv.scrollHeight;
}

// コンソールをクリア
function clearConsole() {
  consoleDiv.innerHTML = '';
}

// スクリプトファイルを読み込む
function loadScriptFile(event) {
  const file = event.target.files[0];
  if (!file) return;

  const reader = new FileReader();
  reader.onload = function(e) {
    codeEditor.value = e.target.result;
    fileName.textContent = `📄 ${file.name}`;
    logConsole('success', `ファイル "${file.name}" を読み込みました`);
  };
  reader.readAsText(file);
}

// スクリプトを保存
function saveScript() {
  const script = codeEditor.value;
  const blob = new Blob([script], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = 'servo_script.txt';
  a.click();
  URL.revokeObjectURL(url);
  logConsole('success', 'スクリプトを保存しました');
}

// サンプルスクリプトを読み込む
function loadExample(type) {
  let script = '';
  
  switch (type) {
    case 'wave':
      script = `# Wave Example
# サーボ0を左右に振る

repeat 5 {
  servo 0 0
  wait 500
  servo 0 180
  wait 500
}

servo 0 90`;
      break;

    case 'sweep':
      script = `# Sweep Example
# 全サーボを順番にスイープ

repeat 3 {
  servo 0 0
  servo 1 0
  servo 2 0
  wait 500
  
  servo 0 180
  servo 1 180
  servo 2 180
  wait 500
}

servos 0:90 1:90 2:90`;
      break;

    case 'dance':
      script = `# Dance Example
# 複数サーボで踊る

repeat 4 {
  servos 0:45 1:135 2:45 3:135
  wait 300
  servos 0:135 1:45 2:135 3:45
  wait 300
}

servos 0:90 1:90 2:90 3:90`;
      break;

    case 'robot_arm':
      script = `# Robot Arm Example
# ロボットアームの動作

function init_position {
  servos 0:90 1:90 2:90 3:90 4:90
  wait 1000
}

function grab {
  servo 1 45
  wait 500
  servo 2 135
  wait 500
  servo 4 30
  wait 500
}

function release {
  servo 4 90
  wait 500
}

function move_base angle {
  servo 0 $1
  wait 1000
}

# メイン処理
call init_position

repeat 2 {
  call move_base 45
  call grab
  call move_base 135
  call release
  call init_position
  wait 1000
}`;
      break;
  }

  codeEditor.value = script;
  fileName.textContent = `📄 ${type}_example.txt`;
  logConsole('info', `サンプルスクリプト "${type}" を読み込みました`);
}
