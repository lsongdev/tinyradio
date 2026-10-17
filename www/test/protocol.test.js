import test from 'node:test';
import assert from 'node:assert/strict';
import { FrameParser, parseStatus, RadioConnection } from '../protocol.js';

const encode = text => new TextEncoder().encode(text);
const status = 'RADIO band=VHF mode=FM requested=103000000 confirmed=103000000 step=100000 volume=32 brightness=128 focus=1 seek=0 ps=[TEST FM] rt=[text with ] bracket and UTF-8 中文] ert=[] ertgroup=-1 clock=[12:34] ctmjd=0 error=RF front end failed';

test('status retains bracketed UTF-8 values, numeric fields, and multiword errors', () => {
  const state = parseStatus(status);
  assert.equal(state.rt, 'text with ] bracket and UTF-8 中文');
  assert.equal(state.ps, 'TEST FM'); assert.equal(state.ert, '');
  assert.equal(state.error, 'RF front end failed'); assert.equal(state.requested, 103000000);
  assert.equal(parseStatus('Settings: saved'), null);
  assert.throws(() => parseStatus('RADIO requested=1 focus=99'), /不兼容/);
});

test('screenshots survive every possible chunk split, binary newlines and text around frames', () => {
  const pixels = new Uint8Array([10, 13, 255, 0, 128, 65]);
  const bytes = new Uint8Array([...encode('Settings: saved\r\nATS_SCREENSHOT RGB888 2 1 6\n'), ...pixels, ...encode('\nATS_SCREENSHOT_END\n' + status + '\n')]);
  for (let split = 0; split <= bytes.length; split++) {
    const lines = [], frames = [];
    const parser = new FrameParser({ onLine: l => lines.push(l), onFrame: f => frames.push(f) });
    parser.feed(bytes.slice(0, split)); parser.feed(bytes.slice(split));
    assert.equal(frames.length, 1); assert.deepEqual(frames[0].pixels, pixels);
    assert.equal(lines[0], 'Settings: saved'); assert.equal(parseStatus(lines[1]).ps, 'TEST FM');
  }
});

test('one-byte chunks decode UTF-8 correctly and do not publish a partial screenshot', () => {
  const lines = [], frames = [];
  const parser = new FrameParser({ onLine: l => lines.push(l), onFrame: f => frames.push(f) });
  for (const byte of encode(status + '\nATS_SCREENSHOT RGB888 1 1 3\nabc')) parser.feed(new Uint8Array([byte]));
  assert.equal(parseStatus(lines[0]).rt, 'text with ] bracket and UTF-8 中文');
  assert.equal(frames.length, 0);
  parser.feed(encode('\nATS_SCREENSHOT_END\n')); assert.equal(frames.length, 1);
});

test('invalid dimensions, wrong terminator and unbounded console lines fail closed', () => {
  const parser = () => new FrameParser({ onLine() {}, onFrame() {} });
  assert.throws(() => parser().feed(encode('ATS_SCREENSHOT RGB888 0 1 0\n')), /尺寸/);
  assert.throws(() => parser().feed(encode('ATS_SCREENSHOT RGB888 1 1 9\n')), /尺寸/);
  assert.throws(() => parser().feed(encode('ATS_SCREENSHOT RGB888 1 1 3\nabc\nBAD_SCREENSHOT_END\n')), /不完整/);
  assert.throws(() => parser().feed(new Uint8Array(16385)), /无法识别/);
});

function fakePort() {
  let incoming;
  const port = {
    writes: [], closed: false,
    async open() {
      this.readable = new ReadableStream({ start(controller) { incoming = controller; } });
      this.writable = new WritableStream({ write: bytes => { this.writes.push(new TextDecoder().decode(bytes)); this.onWrite?.(this.writes.at(-1)); } });
    },
    async setSignals() {}, async close() { this.closed = true; },
    feed(text) { incoming.enqueue(encode(text)); },
    remove() { incoming.close(); },
  };
  return port;
}

test('connection owns a single reader, serializes requests, handles capture and releases locks', async () => {
  const port = fakePort(); const radio = new RadioConnection(); await radio.open(port);
  const pending = radio.status(); await assert.rejects(radio.status(), /上一项/);
  port.feed(status + '\n'); assert.equal((await pending).volume, 32);
  const snapshot = radio.screenshot();
  port.feed('ATS_SCREENSHOT RGB888 1 1 3\nabc\nATS_SCREENSHOT_END\n');
  assert.equal((await snapshot).pixels.length, 3);
  await radio.close(); assert.equal(port.closed, true);
  assert.equal(port.readable.locked, false); assert.equal(port.writable.locked, false);
});

test('timeout closes the stream instead of reusing a partial binary response', async () => {
  const port = fakePort(); const radio = new RadioConnection(); await radio.open(port);
  const pending = radio.request('S', 'frame', 15);
  port.feed('ATS_SCREENSHOT RGB888 1 1 3\na');
  await assert.rejects(pending, /超时/); await radio.close();
  assert.equal(port.closed, true); await assert.rejects(radio.status(), /先连接/);
});

test('unexpected USB removal rejects pending request and closes port', async () => {
  const port = fakePort(); let disconnected = false;
  const radio = new RadioConnection(() => { disconnected = true; }); await radio.open(port);
  const pending = radio.status(); port.remove();
  await assert.rejects(pending, /断开/); await radio.close();
  assert.equal(disconnected, true); assert.equal(port.closed, true);
});

test('focus stops seeking, re-reads actual focus and cycles to desired item', async () => {
  const radio = new RadioConnection(); const commands = [];
  let focus = 1, seek = 1;
  radio.status = async (cmd = '') => {
    commands.push(cmd);
    if (cmd === 'e' && seek) seek = 0;
    else focus = (focus + cmd.length) % 10;
    return { focus, seek, searching: 0 };
  };
  await radio.adjust(9, '>');
  assert.deepEqual(commands, ['', 'e', 'eeeeeeee', '>']);
});
