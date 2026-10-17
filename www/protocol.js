// tinyradio's single-character console and framed RGB888 screenshot protocol.
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const footer = encoder.encode('\nATS_SCREENSHOT_END\n');

export function parseStatus(line) {
  if (!line.startsWith('RADIO ')) return null;
  const state = {};
  for (const match of line.matchAll(/(\w+)=\[(.*?)\](?= \w+=|$)|(\w+)=([^\s]+)/g)) {
    const key = match[1] || match[3];
    const value = match[1] ? match[2] : match[4];
    state[key] = match[1] || !/^-?\d+(\.\d+)?$/.test(value) ? value : Number(value);
  }
  state.error = line.match(/ error=(.*)$/)?.[1] ?? '';
  if (!Number.isFinite(state.requested) || !Number.isInteger(state.focus) || state.focus < 0 || state.focus > 9) {
    throw new Error('设备状态格式不兼容，请更新固件。');
  }
  return state;
}

export class FrameParser {
  constructor({ onLine, onFrame }) {
    this.onLine = onLine;
    this.onFrame = onFrame;
    this.buffer = new Uint8Array(0);
    this.frame = null;
  }
  feed(bytes) {
    const joined = new Uint8Array(this.buffer.length + bytes.length);
    joined.set(this.buffer); joined.set(bytes, this.buffer.length);
    this.buffer = joined;
    while (this.buffer.length) {
      if (this.frame) {
        const { width, height, size } = this.frame;
        if (this.buffer.length < size + footer.length) return;
        if (!footer.every((byte, i) => this.buffer[size + i] === byte)) throw new Error('屏幕数据不完整，请重新连接设备。');
        const pixels = this.buffer.slice(0, size);
        this.buffer = this.buffer.slice(size + footer.length);
        this.frame = null;
        this.onFrame({ width, height, pixels });
      } else {
        const end = this.buffer.indexOf(10);
        if (end < 0) {
          if (this.buffer.length > 16384) throw new Error('串口数据无法识别，请检查设备与固件。');
          return;
        }
        const line = decoder.decode(this.buffer.slice(0, end)).replace(/\r$/, '');
        this.buffer = this.buffer.slice(end + 1);
        const header = /^ATS_SCREENSHOT RGB888 (\d+) (\d+) (\d+)$/.exec(line);
        if (header) {
          const [width, height, size] = header.slice(1).map(Number);
          if (!(width > 0 && width <= 1024 && height > 0 && height <= 1024 && size === width * height * 3)) throw new Error('无效的屏幕尺寸。');
          this.frame = { width, height, size };
        } else this.onLine(line);
      }
    }
  }
}

export class RadioConnection {
  constructor(onDisconnect) { this.onDisconnect = onDisconnect; this.pending = null; }
  async open(port) {
    this.port = port;
    await port.open({ baudRate: 115200 });
    // Native USB CDC on the S3 needs DTR, without asserting reset via RTS.
    try { await port.setSignals({ dataTerminalReady: true, requestToSend: false }); } catch { /* Some bridges lack modem signals. */ }
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.parser = new FrameParser({
      onLine: line => {
        if (line.startsWith('SCREENSHOT_ERROR')) this.settle(new Error('设备无法导出屏幕，请检查固件和帧缓冲区。'));
        if (this.pending?.kind === 'status' && line.startsWith('RADIO ')) {
          try { this.settle(null, parseStatus(line)); } catch (error) { this.settle(error); }
        }
      },
      onFrame: frame => { if (this.pending?.kind === 'frame') this.settle(null, frame); },
    });
    this.readTask = this.readLoop();
  }
  async readLoop() {
    try {
      while (!this.closing) {
        const { value, done } = await this.reader.read();
        if (done) break;
        this.parser.feed(value);
      }
    } catch (error) { if (!this.closing) this.readError = error; }
    finally {
      this.settle(this.readError || new Error('设备连接已断开。'));
      // Close outside the read task so close() can await it without a cycle.
      if (!this.closing) queueMicrotask(() => this.close().catch(() => {}));
    }
  }
  settle(error, value) {
    if (!this.pending) return;
    const { resolve, reject, timer } = this.pending;
    this.pending = null; clearTimeout(timer);
    error ? reject(error) : resolve(value);
  }
  async request(command, kind = 'status', timeout = 5000) {
    if (this.closing || !this.writer) throw new Error('请先连接设备。');
    if (this.pending) throw new Error('设备正在处理上一项操作。');
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.settle(new Error('设备响应超时，请关闭其他串口程序后重新连接。'));
        this.close().catch(() => {});
      }, timeout);
      this.pending = { resolve, reject, timer, kind };
      this.writer.write(encoder.encode(command)).catch(error => this.settle(error));
    });
  }
  status(command = '') { return this.request(command + '?'); }
  screenshot() { return this.request('S', 'frame', 30000); }
  async focus(index) {
    let state = await this.status();
    if (state.seek || state.searching) state = await this.status('e');
    const presses = (index - state.focus + 10) % 10;
    if (presses) state = await this.status('e'.repeat(presses));
    if (state.focus !== index) throw new Error('无法选择控制项，请检查设备是否报错。');
    return state;
  }
  async adjust(index, command) { await this.focus(index); return this.status(command); }
  async close() {
    if (this.closeTask) return this.closeTask;
    this.closing = true;
    this.closeTask = (async () => {
      this.settle(new Error('设备连接已关闭。'));
      try { await this.reader?.cancel(); } catch { /* Already disconnected. */ }
      await this.readTask;
      this.reader?.releaseLock();
      this.writer?.releaseLock();
      try { await this.port?.close(); } catch { /* USB removal can close the port first. */ }
      this.writer = null;
      this.onDisconnect?.();
    })();
    return this.closeTask;
  }
}
