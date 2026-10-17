export const BOARDS = {
  'ats-mini': { name: 'ATS Mini', chip: 'ESP32-S3', size: 16 * 1024 * 1024, frequency: '80m' },
  'v5a': { name: 'GeekNest V5A', chip: 'ESP32', size: 4 * 1024 * 1024, frequency: '40m' },
};

export async function verifiedImage(image, fetcher = fetch) {
  const url = new URL(image.path, new URL('./', location.href));
  if (url.origin !== location.origin || !/^[a-f0-9]{64}$/.test(image.sha256)) throw new Error('固件清单无效。');
  const response = await fetcher(url);
  if (!response.ok) throw new Error(`固件下载失败 (${response.status})，请稍后重试。`);
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (bytes.length !== image.size) throw new Error('固件文件大小不匹配，请刷新页面重试。');
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
  if (hash !== image.sha256) throw new Error('固件 SHA-256 校验失败，已停止刷机。');
  return bytes;
}

export async function flash(port, boardId, image, { log, progress }) {
  let transport;
  try {
    log('加载刷机工具…');
    const [{ ESPLoader, Transport }, { default: md5 }] = await Promise.all([
      import('https://unpkg.com/esptool-js@0.7.0/bundle.js'),
      import('https://esm.sh/js-md5@0.8.3'),
    ]);
    const data = await verifiedImage(image);
    const board = BOARDS[boardId];
    if (!board || ![0, 0x10000].includes(image.address) || (image.address === 0 && data.length !== board.size) || data.length + image.address > board.size) throw new Error('固件布局与所选设备不符。');
    log('SHA-256 校验通过。正在连接引导程序…');
    transport = new Transport(port, true);
    const loader = new ESPLoader({ transport, baudrate: 115200, terminal: { clean() {}, write: log, writeLine: log } });
    await loader.main();
    if (loader.chip.CHIP_NAME !== board.chip) throw new Error(`检测到 ${loader.chip.CHIP_NAME}，所选设备需要 ${board.chip}，已停止刷机。`);
    const flashSize = await loader.detectFlashSize();
    if (!flashSize || loader.flashSizeBytes(flashSize) < board.size) throw new Error('设备 Flash 容量与所选型号不匹配，已停止刷机。');
    log('设备型号检查通过，开始写入。请勿断开 USB。');
    await loader.writeFlash({
      fileArray: [{ data, address: image.address }], flashMode: 'keep', flashFreq: 'keep', flashSize: 'keep',
      eraseAll: false, compress: true, calculateMD5Hash: bytes => md5(bytes),
      reportProgress: (_index, written, total) => progress(Math.floor(written / total * 100)),
    });
    log('写入和 MD5 校验完成，正在重启设备…');
    try { await loader.after('hard_reset'); }
    catch { log('自动重启未完成。请手动重新开机后连接控制台。'); }
  } finally {
    if (transport) {
      try { await transport.disconnect(); }
      catch { log('USB 串口已断开；再次连接前可重新插拔设备。'); }
    }
  }
}
