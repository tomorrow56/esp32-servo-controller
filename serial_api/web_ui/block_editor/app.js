// SPDX-License-Identifier: MIT
// Copyright (c) 2026 tomorrow56
// https://github.com/tomorrow56/esp32-servo-controller

'use strict';

// ===== 定数 =====
const NUM_SERVOS  = 10;
const BAUD_RATE   = 115200;
const CMD_TIMEOUT   = 500;
const SERVO_TIMEOUT = 300;

// ===== 状態 =====
let serialPort      = null;
let serialWriter    = null;
let serialReader    = null;
let isConnected     = false;
let isExecuting     = false;
let stopRequested   = false;
let dragCanvasBlock = null;

// サーボ角度キャッシュ（ブラウザ側でif条件評価に使用）
const servoAngles = new Array(NUM_SERVOS).fill(90);

// ===== DOM =====
const connectBtn      = document.getElementById('connectBtn');
const disconnectBtn   = document.getElementById('disconnectBtn');
const executeBtn      = document.getElementById('executeBtn');
const stopBtn         = document.getElementById('stopBtn');
const clearBtn        = document.getElementById('clearBtn');
const loadExampleBtn  = document.getElementById('loadExampleBtn');
const exampleSelect   = document.getElementById('exampleSelect');
const blockCanvas     = document.getElementById('blockCanvas');
const canvasEmpty     = document.getElementById('canvasEmpty');
const scriptPreview   = document.getElementById('scriptPreview');
const consoleDiv      = document.getElementById('consoleDiv');
const statusBadge     = document.getElementById('statusBadge');
const progressFill    = document.getElementById('progressFill');
const scriptStatus    = document.getElementById('scriptStatus');
const servoGrid       = document.getElementById('servoGrid');
const clearConsoleBtn = document.getElementById('clearConsoleBtn');
const openFileBtn     = document.getElementById('openFileBtn');
const saveFileBtn     = document.getElementById('saveFileBtn');
const fileInput       = document.getElementById('fileInput');
const serialWarning   = document.getElementById('serialWarning');

// ===== 初期化 =====
initServoGrid();
setupCanvasDrop();
checkWebSerialSupport();

connectBtn.addEventListener('click', connectSerial);
disconnectBtn.addEventListener('click', disconnectSerial);
executeBtn.addEventListener('click', executeScript);
stopBtn.addEventListener('click', () => { stopRequested = true; });
clearBtn.addEventListener('click', clearCanvas);
clearConsoleBtn.addEventListener('click', clearConsole);
loadExampleBtn.addEventListener('click', () => loadExample(exampleSelect.value));
openFileBtn.addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', onFileSelected);
saveFileBtn.addEventListener('click', saveScriptToFile);

document.querySelectorAll('.palette-block').forEach(el => {
  el.addEventListener('dragstart', e => {
    dragCanvasBlock = null;
    e.dataTransfer.setData('blockType', el.dataset.type);
    e.dataTransfer.effectAllowed = 'copy';
  });
});

// ===== Web Serial サポートチェック =====
function checkWebSerialSupport() {
  if (!('serial' in navigator)) {
    serialWarning.style.display = 'block';
    connectBtn.disabled = true;
    logConsole('error', 'Web Serial APIに対応していません。Chrome/Edge/Operaをご使用ください。');
  }
}

// ===== シリアル接続 =====
async function connectSerial() {
  try {
    serialPort = await navigator.serial.requestPort();
    await serialPort.open({ baudRate: BAUD_RATE });

    const textDecoder = new TextDecoderStream();
    serialPort.readable.pipeTo(textDecoder.writable);
    serialReader = textDecoder.readable.getReader();

    const textEncoder = new TextEncoderStream();
    textEncoder.readable.pipeTo(serialPort.writable);
    serialWriter = textEncoder.writable.getWriter();

    isConnected = true;
    connectBtn.disabled    = true;
    disconnectBtn.disabled = false;
    executeBtn.disabled    = false;
    statusBadge.textContent = '接続済み';
    statusBadge.className   = 'status-badge status-connected';
    logConsole('success', 'ESP32に接続しました');

    readLineWithTimeout(2000).then(line => {
      if (line && line.includes('READY')) {
        logConsole('info', 'ESP32 READY');
        fetchAllServoAngles();
      }
    });
  } catch (err) {
    logConsole('error', `接続失敗: ${err.message}`);
    serialPort = null;
  }
}

// ===== シリアル切断 =====
async function disconnectSerial() {
  stopRequested = true;
  try {
    if (serialReader) { await serialReader.cancel(); serialReader = null; }
    if (serialWriter) { await serialWriter.close();  serialWriter = null; }
    if (serialPort)   { await serialPort.close();    serialPort   = null; }
  } catch (_) { /* 無視 */ }

  isConnected = false;
  connectBtn.disabled    = false;
  disconnectBtn.disabled = true;
  executeBtn.disabled    = true;
  stopBtn.disabled       = true;
  statusBadge.textContent = '未接続';
  statusBadge.className   = 'status-badge status-disconnected';
  logConsole('warning', 'ESP32との接続を切断しました');
}

// ===== シリアル送受信ヘルパー =====
async function sendLine(line) {
  if (!serialWriter) throw new Error('未接続');
  await serialWriter.write(line + '\n');
}

async function readLineWithTimeout(timeoutMs = CMD_TIMEOUT) {
  if (!serialReader) return null;
  let buf = '';
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const { value, done } = await Promise.race([
      serialReader.read(),
      new Promise(resolve => setTimeout(() => resolve({ value: null, done: false }), 50)),
    ]);
    if (done) return buf || null;
    if (value) {
      buf += value;
      const nl = buf.indexOf('\n');
      if (nl !== -1) return buf.slice(0, nl).trim();
    }
  }
  return buf.trim() || null;
}

async function sendCommand(cmd) {
  await sendLine(cmd);
  return (await readLineWithTimeout(CMD_TIMEOUT)) ?? '';
}

// ===== サーボ角度取得 =====
async function fetchAllServoAngles() {
  if (!isConnected) return;
  try {
    const resp = await sendCommand('getall');
    if (resp.startsWith('ANGLES:')) {
      resp.slice(7).split(',').forEach((v, i) => {
        if (i < NUM_SERVOS) {
          servoAngles[i] = parseInt(v, 10) || 0;
          const el = document.getElementById(`servo-val-${i}`);
          if (el) el.textContent = `${servoAngles[i]}°`;
        }
      });
    }
  } catch (_) { /* 無視 */ }
}

// ===== スクリプト実行エンジン（ブラウザ側） =====
async function executeScript() {
  if (!isConnected) { logConsole('error', '未接続です'); return; }
  if (isExecuting)  { logConsole('warning', '実行中です'); return; }

  const script = generateScript();
  if (!script.trim()) { logConsole('warning', 'スクリプトが空です'); return; }

  const lines = script.split('\n').map(l => l.trim()).filter(l => l.length > 0);
  if (lines.length === 0) return;

  isExecuting   = true;
  stopRequested = false;
  executeBtn.disabled = true;
  stopBtn.disabled    = false;
  progressFill.style.width = '0%';
  logConsole('info', `スクリプト実行開始 (${lines.length} 行)`);

  const MAX_NEST    = 8;
  const execStack   = new Array(MAX_NEST).fill(false);
  const inElseStack = new Array(MAX_NEST).fill(false);
  execStack[0] = true;
  let nestDepth = 0;

  let lineIdx = 0;
  while (lineIdx < lines.length && !stopRequested) {
    const line = lines[lineIdx++];
    const pct  = Math.round((lineIdx / lines.length) * 100);
    progressFill.style.width  = `${pct}%`;
    scriptStatus.textContent  = `実行中: ${lineIdx} / ${lines.length} 行 (${pct}%)`;

    if (line.startsWith('#') || line.startsWith('//')) continue;

    const sp  = line.indexOf(' ');
    const cmd = (sp > 0 ? line.slice(0, sp) : line).toLowerCase();
    const arg = sp > 0 ? line.slice(sp + 1).trim() : '';
    const executing = execStack[nestDepth];

    if (cmd === 'if') {
      if (nestDepth < MAX_NEST - 1) nestDepth++;
      execStack[nestDepth]   = executing ? evaluateCondition(arg) : false;
      inElseStack[nestDepth] = false;
    } else if (cmd === 'else') {
      if (nestDepth > 0 && !inElseStack[nestDepth]) {
        execStack[nestDepth]   = execStack[nestDepth - 1] && !execStack[nestDepth];
        inElseStack[nestDepth] = true;
      }
    } else if (cmd === 'endif') {
      if (nestDepth > 0) nestDepth--;
    } else if (executing) {
      if (cmd === 'servo') {
        const parts = arg.split(/\s+/);
        const ch    = parseInt(parts[0], 10);
        const angle = parseInt(parts[1], 10);
        await sendLine(`servo ${ch} ${angle}`);
        const resp = await readLineWithTimeout(SERVO_TIMEOUT);
        logConsole(resp && resp.startsWith('OK') ? 'success' : 'info', `servo ${ch} → ${angle}°`);
        if (ch >= 0 && ch < NUM_SERVOS) {
          servoAngles[ch] = angle;
          const el = document.getElementById(`servo-val-${ch}`);
          if (el) el.textContent = `${angle}°`;
        }
      } else if (cmd === 'servos') {
        await sendLine(`servos ${arg}`);
        const resp = await readLineWithTimeout(SERVO_TIMEOUT);
        logConsole(resp && resp.startsWith('OK') ? 'success' : 'info', `servos ${arg}`);
        arg.split(/\s+/).forEach(pair => {
          const [ch, angle] = pair.split(':').map(Number);
          if (!isNaN(ch) && !isNaN(angle) && ch >= 0 && ch < NUM_SERVOS) {
            servoAngles[ch] = angle;
            const el = document.getElementById(`servo-val-${ch}`);
            if (el) el.textContent = `${angle}°`;
          }
        });
      } else if (cmd === 'wait') {
        const ms = parseInt(arg, 10) || 0;
        scriptStatus.textContent = `wait ${ms}ms... (${lineIdx}/${lines.length})`;
        await sleep(ms);
      } else {
        logConsole('warning', `不明なコマンド: ${line}`);
      }
    }
  }

  isExecuting = false;
  executeBtn.disabled = false;
  stopBtn.disabled    = true;
  progressFill.style.width = stopRequested ? '0%' : '100%';
  scriptStatus.textContent = stopRequested ? '停止しました' : '実行完了';
  logConsole(stopRequested ? 'warning' : 'success',
    stopRequested ? 'スクリプトを停止しました' : 'スクリプト実行が完了しました');
  stopRequested = false;
}

// ===== 条件評価（ブラウザ側） =====
function evaluateCondition(arg) {
  const m = arg.match(/^servo(\d+)\s*(==|!=|>=|<=|>|<|=)\s*(\d+)$/i);
  if (!m) return false;
  const lhsVal = servoAngles[parseInt(m[1], 10)] ?? 0;
  const op     = m[2] === '=' ? '==' : m[2];
  const rhs    = parseInt(m[3], 10);
  switch (op) {
    case '==': return lhsVal === rhs;
    case '!=': return lhsVal !== rhs;
    case '>':  return lhsVal >   rhs;
    case '>=': return lhsVal >=  rhs;
    case '<':  return lhsVal <   rhs;
    case '<=': return lhsVal <=  rhs;
    default:   return false;
  }
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

// ===== ファイル読込・保存 =====
function onFileSelected(e) {
  const file = e.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = ev => importScriptText(ev.target.result, file.name);
  reader.readAsText(file, 'UTF-8');
  fileInput.value = '';
}

function saveScriptToFile() {
  const script = generateScript();
  if (!script.trim()) { logConsole('warning', '保存するスクリプトがありません'); return; }
  const blob = new Blob([script], { type: 'text/plain;charset=utf-8' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  const ts   = new Date().toISOString().slice(0, 19).replace(/[T:]/g, '-');
  a.href     = url;
  a.download = `script_${ts}.txt`;
  a.click();
  URL.revokeObjectURL(url);
  logConsole('success', `スクリプトを保存しました: ${a.download}`);
}

// ===== スクリプトテキスト → ブロック変換 =====
function importScriptText(text, filename) {
  clearCanvas();
  const rawLines = text.split(/\r?\n/);
  const { defs } = parseLinesToBlockDefs(rawLines, 0);
  defs.forEach(def => blockCanvas.appendChild(createCanvasBlock(def.type, def)));
  updateCanvasEmpty();
  updatePreview();
  updateAllCallSelects();
  logConsole('success', `"${filename}" を読み込みました (${rawLines.length} 行)`);
}

function parseLinesToBlockDefs(lines, start) {
  const defs = [];
  let i = start;
  while (i < lines.length) {
    const raw = lines[i].trim();
    i++;
    if (raw === '' || raw.startsWith('//')) continue;
    if (raw.startsWith('#')) {
      const text = raw.slice(1).trim();
      if (text.startsWith('--- function:') || text.startsWith('--- end:')) continue;
      if (text.startsWith('call:')) {
        defs.push({ type: 'call', callname: text.slice(5).replace(/\s*\(.*\)$/, '').trim() });
        continue;
      }
      defs.push({ type: 'comment', text });
      continue;
    }
    const sp  = raw.indexOf(' ');
    const cmd = (sp > 0 ? raw.slice(0, sp) : raw).toLowerCase();
    const arg = sp > 0 ? raw.slice(sp + 1).trim() : '';
    switch (cmd) {
      case 'servo': {
        const parts = arg.split(/\s+/);
        defs.push({ type: 'servo', channel: parseInt(parts[0] ?? 0, 10), angle: parseInt(parts[1] ?? 90, 10) });
        break;
      }
      case 'servos':
        defs.push({ type: 'servos', pairs: arg });
        break;
      case 'wait':
        defs.push({ type: 'wait', ms: parseInt(arg, 10) || 500 });
        break;
      case 'if': {
        const thenLines = [], elseLines = [];
        let inElse = false, depth = 1;
        while (i < lines.length) {
          const inner    = lines[i].trim(); i++;
          const innerCmd = inner.split(/\s+/)[0]?.toLowerCase();
          if (innerCmd === 'if') { depth++; (inElse ? elseLines : thenLines).push(inner); }
          else if (innerCmd === 'endif') {
            depth--;
            if (depth === 0) break;
            (inElse ? elseLines : thenLines).push(inner);
          } else if (innerCmd === 'else' && depth === 1) { inElse = true; }
          else { (inElse ? elseLines : thenLines).push(inner); }
        }
        const m   = arg.match(/^(\S+)\s*(==|!=|>=|<=|>|<|=)\s*(\d+)$/);
        const lhs = m ? m[1] : 'servo0';
        const op  = m ? (m[2] === '=' ? '==' : m[2]) : '==';
        const rhs = m ? parseInt(m[3], 10) : 90;
        defs.push({ type: 'if', lhs, op, rhs,
          thenChildren: parseLinesToBlockDefs(thenLines, 0).defs,
          elseChildren: parseLinesToBlockDefs(elseLines, 0).defs });
        break;
      }
      default:
        defs.push({ type: 'comment', text: raw });
        break;
    }
  }
  return { defs, nextIndex: i };
}

// ===== サーボグリッド =====
function initServoGrid() {
  servoGrid.innerHTML = '';
  for (let i = 0; i < NUM_SERVOS; i++) {
    const item = document.createElement('div');
    item.className = 'servo-item';
    item.id = `servo-item-${i}`;
    item.innerHTML = `<div class="servo-title">CH${i}</div><div class="servo-value" id="servo-val-${i}">90°</div>`;
    servoGrid.appendChild(item);
  }
}

// ===== コンソール =====
function logConsole(level, message) {
  const line = document.createElement('div');
  line.className = `console-line console-${level}`;
  const now = new Date();
  const ts  = `${now.getHours().toString().padStart(2,'0')}:${now.getMinutes().toString().padStart(2,'0')}:${now.getSeconds().toString().padStart(2,'0')}`;
  line.textContent = `[${ts}] ${message}`;
  consoleDiv.appendChild(line);
  consoleDiv.scrollTop = consoleDiv.scrollHeight;
}

function clearConsole() { consoleDiv.innerHTML = ''; }

// ===== キャンバスドロップ設定 =====
function setupCanvasDrop() {
  blockCanvas.addEventListener('dragover', e => {
    e.preventDefault();
    e.dataTransfer.dropEffect = dragCanvasBlock ? 'move' : 'copy';
    blockCanvas.classList.add('drag-over');
    clearDropIndicators();
    const target = getTopBlockUnderCursor(e.clientY);
    if (target) {
      const r = target.getBoundingClientRect();
      target.classList.add(e.clientY < r.top + r.height / 2 ? 'drag-target-above' : 'drag-target-below');
    }
  });
  blockCanvas.addEventListener('dragleave', e => {
    if (!blockCanvas.contains(e.relatedTarget)) {
      blockCanvas.classList.remove('drag-over');
      clearDropIndicators();
    }
  });
  blockCanvas.addEventListener('drop', e => {
    e.preventDefault();
    blockCanvas.classList.remove('drag-over');
    const blockType = e.dataTransfer.getData('blockType');
    const target    = getTopBlockUnderCursor(e.clientY);
    if (dragCanvasBlock) {
      if (target && target !== dragCanvasBlock) {
        const r   = target.getBoundingClientRect();
        const ref = e.clientY < r.top + r.height / 2 ? target : target.nextSibling;
        blockCanvas.insertBefore(dragCanvasBlock, ref);
      }
      dragCanvasBlock = null;
    } else if (blockType) {
      const newBlock = createCanvasBlock(blockType);
      if (target) {
        const r   = target.getBoundingClientRect();
        const ref = e.clientY < r.top + r.height / 2 ? target : target.nextSibling;
        blockCanvas.insertBefore(newBlock, ref);
      } else {
        blockCanvas.appendChild(newBlock);
      }
    }
    clearDropIndicators();
    updateCanvasEmpty();
    updatePreview();
  });
}

function getTopBlockUnderCursor(clientY) {
  return [...blockCanvas.querySelectorAll(':scope > .canvas-block')].find(b => {
    const r = b.getBoundingClientRect();
    return clientY >= r.top && clientY <= r.bottom;
  }) || null;
}

function clearDropIndicators() {
  document.querySelectorAll('.drag-target-above, .drag-target-below').forEach(el => {
    el.classList.remove('drag-target-above', 'drag-target-below');
  });
}

// ===== ブロック生成 =====
function escapeAttr(str) {
  return String(str).replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function createCanvasBlock(type, params = {}) {
  const block = document.createElement('div');
  block.className    = `canvas-block block-${type}`;
  block.dataset.type = type;
  block.draggable    = true;

  block.addEventListener('dragstart', e => {
    dragCanvasBlock = block;
    e.dataTransfer.setData('blockType', '');
    e.dataTransfer.effectAllowed = 'move';
    setTimeout(() => block.classList.add('dragging'), 0);
  });
  block.addEventListener('dragend', () => {
    block.classList.remove('dragging');
    dragCanvasBlock = null;
    clearDropIndicators();
  });

  let html = '';
  switch (type) {
    case 'servo':
      html = `<span class="block-icon">🎯</span><span class="block-label">servo</span>
        <div class="block-params">
          <label>CH</label>
          <input type="number" class="p-channel" min="0" max="${NUM_SERVOS - 1}" value="${params.channel ?? 0}">
          <label>角度</label>
          <input type="number" class="p-angle" min="0" max="180" value="${params.angle ?? 90}">
          <label style="font-size:0.75em;color:#888">°</label>
        </div>`;
      break;
    case 'servos':
      html = `<span class="block-icon">🎯🎯</span><span class="block-label">servos</span>
        <div class="block-params">
          <label>CH:角度 ...</label>
          <input type="text" class="p-pairs" placeholder="0:90 1:45" value="${params.pairs ?? '0:90 1:90'}">
        </div>`;
      break;
    case 'wait':
      html = `<span class="block-icon">⏱️</span><span class="block-label">wait</span>
        <div class="block-params">
          <input type="number" class="p-ms" min="0" max="60000" step="100" value="${params.ms ?? 500}">
          <label>ms</label>
        </div>`;
      break;
    case 'repeat':
      html = `<span class="block-icon">🔁</span><span class="block-label">repeat</span>
        <div class="block-params">
          <input type="number" class="p-count" min="1" max="999" value="${params.count ?? 3}">
          <label>回</label>
        </div>`;
      break;
    case 'comment':
      html = `<span class="block-icon">💬</span><span class="block-label">#</span>
        <div class="block-params">
          <input type="text" class="p-text" placeholder="コメントを入力" value="${escapeAttr(params.text ?? 'コメント')}">
        </div>`;
      break;
    case 'function':
      html = `<span class="block-icon">📦</span><span class="block-label">function</span>
        <div class="block-params">
          <label>名前</label>
          <input type="text" class="p-fname" placeholder="myFunc" value="${escapeAttr(params.fname ?? 'myFunc')}">
        </div>`;
      break;
    case 'call':
      html = `<span class="block-icon">▶️</span><span class="block-label">call</span>
        <div class="block-params">
          <label>関数名</label>
          <select class="p-callname"></select>
        </div>`;
      break;
    case 'if':
      html = `<span class="block-icon">❓</span><span class="block-label">if</span>
        <div class="block-params">
          <select class="p-if-lhs">
            ${Array.from({ length: NUM_SERVOS }, (_, i) => `<option value="servo${i}">サーボCH${i}</option>`).join('')}
          </select>
          <select class="p-if-op">
            <option value="==">=</option>
            <option value=">">&gt;</option>
            <option value=">=">&gt;=</option>
            <option value="<">&lt;</option>
            <option value="<=">&lt;=</option>
            <option value="!=">≠</option>
          </select>
          <input type="number" class="p-if-rhs" min="0" max="180" value="${params.rhs ?? 90}">
          <label style="font-size:0.72em;color:#888">°</label>
        </div>`;
      break;
  }
  html += `<button class="block-delete" title="削除">✕</button>`;
  block.innerHTML = html;

  block.querySelector('.block-delete').addEventListener('click', () => {
    block.remove();
    updateCanvasEmpty();
    updatePreview();
    updateAllCallSelects();
  });
  block.querySelectorAll('input').forEach(el => {
    el.addEventListener('input', () => {
      updatePreview();
      if (type === 'function') updateAllCallSelects();
    });
    el.addEventListener('mousedown', e => e.stopPropagation());
  });
  block.querySelectorAll('select').forEach(el => {
    el.addEventListener('change', updatePreview);
    el.addEventListener('mousedown', e => e.stopPropagation());
  });

  if (type === 'call') {
    refreshCallSelect(block.querySelector('.p-callname'), params.callname ?? '');
  }
  if (type === 'if') {
    const lhsSel = block.querySelector('.p-if-lhs');
    const opSel  = block.querySelector('.p-if-op');
    if (params.lhs) lhsSel.value = params.lhs;
    if (params.op)  opSel.value  = params.op;
  }

  if (type === 'repeat' || type === 'function') {
    const body = document.createElement('div');
    body.className = 'repeat-body';
    body.addEventListener('dragover', e => {
      e.preventDefault(); e.stopPropagation();
      e.dataTransfer.dropEffect = 'copy';
      body.classList.add('drag-over');
    });
    body.addEventListener('dragleave', e => {
      if (!body.contains(e.relatedTarget)) body.classList.remove('drag-over');
    });
    body.addEventListener('drop', e => {
      e.preventDefault(); e.stopPropagation();
      body.classList.remove('drag-over');
      const bt = e.dataTransfer.getData('blockType');
      if (bt && bt !== 'repeat' && bt !== 'function') {
        body.appendChild(createCanvasBlock(bt));
        updatePreview();
      }
    });
    if (params.children) {
      params.children.forEach(c => body.appendChild(createCanvasBlock(c.type, c)));
    }
    block.appendChild(body);
  }

  if (type === 'if') {
    const makeDropArea = (cls, label) => {
      const wrap = document.createElement('div');
      wrap.style.width = '100%';
      const lbl  = document.createElement('div');
      lbl.className   = 'if-section-label';
      lbl.textContent = label;
      const area = document.createElement('div');
      area.className  = cls;
      area.addEventListener('dragover', e => {
        e.preventDefault(); e.stopPropagation();
        e.dataTransfer.dropEffect = 'copy';
        area.classList.add('drag-over');
      });
      area.addEventListener('dragleave', e => {
        if (!area.contains(e.relatedTarget)) area.classList.remove('drag-over');
      });
      area.addEventListener('drop', e => {
        e.preventDefault(); e.stopPropagation();
        area.classList.remove('drag-over');
        const bt = e.dataTransfer.getData('blockType');
        if (bt && bt !== 'function') {
          area.appendChild(createCanvasBlock(bt));
          updatePreview();
        }
      });
      wrap.appendChild(lbl);
      wrap.appendChild(area);
      return { wrap, area };
    };

    const { wrap: thenWrap, area: thenArea } = makeDropArea('if-then-body', '▶ then');
    const { wrap: elseWrap, area: elseArea } = makeDropArea('if-else-body', '▷ else (省略可)');
    if (params.thenChildren) {
      params.thenChildren.forEach(c => thenArea.appendChild(createCanvasBlock(c.type, c)));
    }
    if (params.elseChildren) {
      params.elseChildren.forEach(c => elseArea.appendChild(createCanvasBlock(c.type, c)));
    }
    block.appendChild(thenWrap);
    block.appendChild(elseWrap);
  }

  return block;
}

// ===== 関数定義マップ構築 =====
function buildFunctionMap() {
  const map = new Map();
  [...blockCanvas.querySelectorAll(':scope > .canvas-block')].forEach(block => {
    if (block.dataset.type !== 'function') return;
    const name = (block.querySelector('.p-fname')?.value ?? '').trim();
    if (!name) return;
    const body  = block.querySelector('.repeat-body');
    const lines = body
      ? [...body.querySelectorAll(':scope > .canvas-block')].flatMap(b => blockToLines(b, map))
      : [];
    map.set(name, lines);
  });
  return map;
}

// ===== スクリプト生成 =====
function blockToLines(block, funcMap = null) {
  const type = block.dataset.type;
  switch (type) {
    case 'servo': {
      const ch  = block.querySelector('.p-channel')?.value ?? 0;
      const ang = block.querySelector('.p-angle')?.value   ?? 90;
      return [`servo ${ch} ${ang}`];
    }
    case 'servos': {
      const pairs = (block.querySelector('.p-pairs')?.value ?? '').trim();
      return pairs ? [`servos ${pairs}`] : [];
    }
    case 'wait': {
      const ms = block.querySelector('.p-ms')?.value ?? 500;
      return [`wait ${ms}`];
    }
    case 'repeat': {
      const count = parseInt(block.querySelector('.p-count')?.value ?? 3, 10);
      const body  = block.querySelector('.repeat-body');
      const inner = body
        ? [...body.querySelectorAll(':scope > .canvas-block')].flatMap(b => blockToLines(b, funcMap))
        : [];
      const lines = [];
      for (let i = 0; i < count; i++) lines.push(...inner);
      return lines;
    }
    case 'function': {
      const name  = (block.querySelector('.p-fname')?.value ?? '').trim();
      const body  = block.querySelector('.repeat-body');
      const inner = body
        ? [...body.querySelectorAll(':scope > .canvas-block')].flatMap(b => blockToLines(b, funcMap))
        : [];
      return [`# --- function: ${name} ---`, ...inner, `# --- end: ${name} ---`];
    }
    case 'call': {
      const name = (block.querySelector('.p-callname')?.value ?? '').trim();
      if (!name) return [];
      if (funcMap && funcMap.has(name)) return [`# call: ${name}`, ...funcMap.get(name)];
      return [`# call: ${name} (未定義)`];
    }
    case 'if': {
      const lhs = block.querySelector('.p-if-lhs')?.value ?? 'servo0';
      const op  = block.querySelector('.p-if-op')?.value  ?? '==';
      const rhs = block.querySelector('.p-if-rhs')?.value ?? '90';
      const thenArea  = block.querySelector('.if-then-body');
      const elseArea  = block.querySelector('.if-else-body');
      const thenLines = thenArea
        ? [...thenArea.querySelectorAll(':scope > .canvas-block')].flatMap(b => blockToLines(b, funcMap))
        : [];
      const elseLines = elseArea
        ? [...elseArea.querySelectorAll(':scope > .canvas-block')].flatMap(b => blockToLines(b, funcMap))
        : [];
      const lines = [`if ${lhs} ${op} ${rhs}`, ...thenLines];
      if (elseLines.length > 0) lines.push('else', ...elseLines);
      lines.push('endif');
      return lines;
    }
    case 'comment': {
      const text = block.querySelector('.p-text')?.value ?? '';
      return [`# ${text}`];
    }
    default:
      return [];
  }
}

function generateScript() {
  const funcMap = buildFunctionMap();
  return [...blockCanvas.querySelectorAll(':scope > .canvas-block')]
    .flatMap(b => blockToLines(b, funcMap)).join('\n');
}

// ===== call セレクト更新 =====
function getFunctionNames() {
  return [...blockCanvas.querySelectorAll(':scope > .canvas-block[data-type="function"]')]
    .map(b => (b.querySelector('.p-fname')?.value ?? '').trim())
    .filter(n => n.length > 0);
}

function refreshCallSelect(sel, currentValue) {
  const names = getFunctionNames();
  sel.innerHTML = names.length === 0
    ? '<option value="">(関数なし)</option>'
    : names.map(n => `<option value="${escapeAttr(n)}"${n === currentValue ? ' selected' : ''}>${escapeAttr(n)}</option>`).join('');
}

function updateAllCallSelects() {
  document.querySelectorAll('.canvas-block[data-type="call"] .p-callname').forEach(sel => {
    refreshCallSelect(sel, sel.value);
  });
  updatePreview();
}

function updatePreview() {
  scriptPreview.textContent = generateScript() || '# ブロックを追加するとここに表示されます';
}

function updateCanvasEmpty() {
  const hasBlocks = blockCanvas.querySelectorAll(':scope > .canvas-block').length > 0;
  canvasEmpty.style.display = hasBlocks ? 'none' : 'block';
}

function clearCanvas() {
  [...blockCanvas.querySelectorAll(':scope > .canvas-block')].forEach(b => b.remove());
  updateCanvasEmpty();
  updatePreview();
}

// ===== サンプル読込 =====
function loadExample(type) {
  clearCanvas();
  const defs = {
    wave: [
      { type: 'comment', text: 'Wave Example - サーボ0を左右に振る' },
      { type: 'servo',   channel: 0, angle: 0   },
      { type: 'wait',    ms: 500 },
      { type: 'servo',   channel: 0, angle: 180 },
      { type: 'wait',    ms: 500 },
      { type: 'servo',   channel: 0, angle: 90  },
      { type: 'wait',    ms: 500 },
    ],
    sweep: [
      { type: 'comment', text: 'Sweep Example - 全サーボをスイープ' },
      { type: 'servos',  pairs: '0:0 1:0 2:0 3:0 4:0' },
      { type: 'wait',    ms: 1000 },
      { type: 'servos',  pairs: '0:90 1:90 2:90 3:90 4:90' },
      { type: 'wait',    ms: 1000 },
      { type: 'servos',  pairs: '0:180 1:180 2:180 3:180 4:180' },
      { type: 'wait',    ms: 1000 },
      { type: 'servos',  pairs: '0:90 1:90 2:90 3:90 4:90' },
    ],
    dance: [
      { type: 'comment', text: 'Dance Example - repeatブロックで繰り返し' },
      { type: 'repeat',  count: 4, children: [
        { type: 'servos', pairs: '0:45 1:135 2:45 3:135' },
        { type: 'wait',   ms: 300 },
        { type: 'servos', pairs: '0:135 1:45 2:135 3:45' },
        { type: 'wait',   ms: 300 },
      ]},
      { type: 'servos', pairs: '0:90 1:90 2:90 3:90' },
    ],
    sequence: [
      { type: 'comment', text: 'Sequence Example - 順番にサーボを動かす' },
      { type: 'servo',   channel: 0, angle: 180 },
      { type: 'wait',    ms: 200 },
      { type: 'servo',   channel: 1, angle: 180 },
      { type: 'wait',    ms: 200 },
      { type: 'servo',   channel: 2, angle: 180 },
      { type: 'wait',    ms: 200 },
      { type: 'servo',   channel: 3, angle: 180 },
      { type: 'wait',    ms: 200 },
      { type: 'servo',   channel: 4, angle: 180 },
      { type: 'wait',    ms: 1000 },
      { type: 'servos',  pairs: '0:90 1:90 2:90 3:90 4:90' },
    ],
  };
  const blocks = defs[type];
  if (!blocks) return;
  blocks.forEach(def => blockCanvas.appendChild(createCanvasBlock(def.type, def)));
  updateCanvasEmpty();
  updatePreview();
  logConsole('info', `サンプル "${type}" を読み込みました`);
}
