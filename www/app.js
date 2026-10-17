import { h, render } from 'preact';
import { useEffect, useRef, useState } from 'preact/hooks';
import htm from 'htm';
import { RadioConnection } from './protocol.js';
import { flash } from './firmware.js?v=20261017-releases';
import { fetchReleases } from './releases.js';

const html = htm.bind(h);
const REPO = 'https://github.com/lsongdev/tinyradio';
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const frequency = value => Number.isFinite(value) ? (value / 1e6).toFixed(3) : '—';
const fields = [
  { index: 6, name: '调谐步进', value: s => `${s.step} Hz` },
  { index: 3, name: '接收带宽', value: s => `${s.bw / 1000} kHz` },
  { index: 4, name: '增益 / 衰减', value: s => s.gainset < 0 ? 'AUTO' : `ATT ${s.gainset}` },
  { index: 5, name: '软静音', value: s => `${s.smset} dB` },
  { index: 8, name: '音量', value: s => `${Math.round(s.volume / 63 * 100)}%` },
  { index: 9, name: '屏幕亮度', value: s => `${Math.round(s.brightness / 255 * 100)}%` },
];

function App() {
  const [board, setBoard] = useState('ats-mini');
  const [releases, setReleases] = useState([]);
  const [releaseId, setReleaseId] = useState('');
  const [loading, setLoading] = useState(true);
  const [releaseError, setReleaseError] = useState('');
  const [install, setInstall] = useState('update');
  const [localFile, setLocalFile] = useState(null);
  const fileInput = useRef(null);
  const [acknowledged, setAcknowledged] = useState(false);
  const [connected, setConnected] = useState(false);
  const [state, setState] = useState(null);
  const [busy, setBusy] = useState('');
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');
  const [logs, setLogs] = useState([]);
  const [progress, setProgress] = useState(0);
  const [flashed, setFlashed] = useState(false);
  const [live, setLive] = useState(false);
  const [captured, setCaptured] = useState('');
  const connection = useRef(null);
  const lock = useRef(false);
  const canvas = useRef(null);
  const supported = typeof navigator !== 'undefined' && 'serial' in navigator && isSecureContext;
  const release = releases.find(r => r.tag === releaseId);
  const images = release?.boards[board];
  const image = images?.[install];
  const available = releases.filter(r => r.boards[board]);

  async function loadReleases() {
    setLoading(true); setReleaseError('');
    try {
      const data = await fetchReleases();
      setReleases(data);
      setReleaseId(data.find(r => r.boards[board])?.tag || '');
    } catch (error) { setReleaseError(error.message || '版本列表暂时无法加载，请检查网络后重试。'); }
    finally { setLoading(false); }
  }
  useEffect(() => { loadReleases(); }, []);
  useEffect(() => { setLocalFile(null); if (fileInput.current) fileInput.current.value = ''; }, [board, releaseId, install]);
  useEffect(() => {
    if (!connected) return;
    const onDisconnect = event => {
      if (event.target === connection.current?.port) connection.current.close().catch(() => {});
    };
    navigator.serial.addEventListener('disconnect', onDisconnect);
    return () => navigator.serial.removeEventListener('disconnect', onDisconnect);
  }, [connected]);
  useEffect(() => {
    if (!connected) return;
    const timer = setInterval(() => {
      if (!lock.current && !document.hidden) run(live ? '更新屏幕…' : '读取状态…', async () => {
        setState(await connection.current.status());
        if (live) await capture();
      }, true);
    }, live ? 3000 : 2500);
    return () => clearInterval(timer);
  }, [connected, live]);
  useEffect(() => {
    const beforeUnload = event => {
      if (lock.current) { event.preventDefault(); event.returnValue = ''; }
    };
    window.addEventListener('beforeunload', beforeUnload);
    return () => window.removeEventListener('beforeunload', beforeUnload);
  }, []);

  async function run(label, action, quiet = false) {
    if (lock.current) return;
    lock.current = true; setBusy(label);
    if (!quiet) { setError(''); setMessage(''); }
    try { await action(); }
    catch (err) {
      if (err.name !== 'NotFoundError') setError(err.message || String(err));
    } finally { lock.current = false; setBusy(''); }
  }
  async function connect() {
    const port = await navigator.serial.requestPort();
    const radio = new RadioConnection(() => {
      if (connection.current === radio) { connection.current = null; setConnected(false); setLive(false); }
    });
    connection.current = radio;
    try {
      await radio.open(port);
      await wait(1500);
      setState(await radio.status());
      setConnected(true); setCaptured('');
      setMessage('已连接。可以调谐电台或读取设备屏幕。');
    } catch (err) { await radio.close(); throw err; }
  }
  async function capture() {
    const frame = await connection.current.screenshot();
    const context = canvas.current.getContext('2d');
    canvas.current.width = frame.width; canvas.current.height = frame.height;
    const rgba = context.createImageData(frame.width, frame.height);
    for (let i = 0, j = 0; i < frame.pixels.length; i += 3, j += 4) {
      rgba.data[j] = frame.pixels[i]; rgba.data[j + 1] = frame.pixels[i + 1]; rgba.data[j + 2] = frame.pixels[i + 2]; rgba.data[j + 3] = 255;
    }
    context.putImageData(rgba, 0, 0);
    setCaptured(new Date().toLocaleTimeString('zh-CN'));
  }
  async function command(text, index) {
    const radio = connection.current;
    setState(index === undefined ? await radio.status(text) : await radio.adjust(index, text));
    // A tune/configuration is asynchronous in firmware; refresh the completed result.
    await wait(180);
    setState(await radio.status());
  }
  async function startFlash() {
    // The chooser must be called in the click gesture, before downloads/imports.
    const port = await navigator.serial.requestPort();
    setFlashed(false); setLogs([]); setProgress(0);
    await flash(port, board, image, {
      log: line => setLogs(lines => [...lines.slice(-79), String(line)]),
      progress: setProgress, file: localFile,
    });
    setProgress(100); setFlashed(true);
    setMessage('刷机完成。等待设备启动，点击「连接设备」进入控制台；USB 端口名称可能会变化。');
  }
  function changeBoard(id) {
    setBoard(id); setAcknowledged(false); setFlashed(false);
    if (!releases.find(r => r.tag === releaseId)?.boards[id]) setReleaseId(releases.find(r => r.boards[id])?.tag || '');
  }
  const disabled = !connected || Boolean(busy);
  const radioState = state || {};
  return html`<div class="app-shell">
    <header class="site-header"><a class="brand" href="#" aria-label="tinyradio 首页"><span class="brand-mark" aria-hidden="true">▥</span><span class="brand-tiny">tiny</span><span class="brand-radio">radio</span></a>
      <nav aria-label="主导航"><a href="#about">项目</a><a href="#install">刷机</a><a href="#console">控制台</a><a href=${REPO} target="_blank" rel="noreferrer">GitHub ↗</a></nav>
    </header>
    <main>
      <section class="hero" id="about">
        <div class="hero-copy"><p class="eyebrow"><span class="dot"></span> OPEN SOURCE · SI4732 RECEIVER</p>
          <h1>把世界，<br />调到<span>耳边。</span></h1>
          <p class="intro">一台小收音机，一整个无线电世界。<br />为 SI4732 打造的开源固件，从长波到短波，再到 FM，让每一次调谐都简单自然。</p>
          <div class="actions"><a class="button primary" href="#install">安装固件 <span>↓</span></a><a class="text-link" href="#console">连接我的收音机 ↗</a></div>
          <p class="hero-note">ATS Mini · GeekNest V5A</p>
        </div>
        <div class="radio-visual" aria-label="tinyradio 固件实际屏幕示例">
          <div class="antenna"></div><div class="radio-case"><div class="radio-top"><span>tinyradio</span><span>SI4732 · AM / FM / SSB</span></div>
          <img src="./assets/screen-frequency.png" width="320" height="170" alt="设备实际屏幕示例：频率、接收参数和调谐刻度" />
          <div class="radio-bottom"><div class="speaker" aria-hidden="true"></div><span class="radio-label">LISTEN TO THE WORLD</span><div class="knob" aria-hidden="true"></div></div></div>
          <p class="visual-caption"><span class="dot"></span> 固件实际屏幕 · 外观为示意</p>
        </div>
      </section>
      <div class="feature-strip"><div><strong>LW / MW / SW / FM</strong><span>跨波段，自由探索</span></div><div><strong>AM / FM / LSB / USB</strong><span>多种解调模式</span></div><div><strong>RDS & eRT</strong><span>电台名称与广播文字</span></div><div><strong>为你的设备而生</strong><span>保存设置 · 明暗主题</span></div></div>
      <section id="install" class="section">
        <div class="section-heading"><div><p class="eyebrow">01 / FIRMWARE</p><h2>新的电波，从这里开始。</h2></div><p>一根 USB 数据线，在浏览器中完成安装。</p></div>
        ${!supported && html`<div class="notice">当前浏览器不支持 Web Serial。本站仍可浏览和下载固件；USB 刷机与控制请使用支持 Web Serial 的桌面 Chrome / Edge，通过 HTTPS 或 localhost 打开。手机端支持取决于浏览器、系统与 USB 能力，iOS Safari 暂不支持。</div>`}
        <div class="install-grid"><div class="panel install-panel">
          <div class="form-grid"><label>设备型号<select value=${board} disabled=${Boolean(busy) || connected} onChange=${e => changeBoard(e.target.value)}><option value="ats-mini">ATS Mini / ESP32-S3</option><option value="v5a">GeekNest V5A / ESP32</option></select></label>
          <label>固件版本<select value=${releaseId} disabled=${loading || !available.length || Boolean(busy)} onChange=${e => { setReleaseId(e.target.value); setFlashed(false); }}>${!available.length && html`<option value="">${loading ? '正在加载…' : '暂无可用版本'}</option>`}${available.map(r => html`<option key=${r.tag} value=${r.tag}>${r.tag}${r.prerelease ? ' · 预发布' : ''}</option>`)}</select></label></div>
          ${releaseError && html`<p class="error">${releaseError} <button class="text-button" onClick=${loadReleases}>重新加载</button></p>`}
          ${!loading && !releaseError && !available.length && html`<p class="muted">暂无带 SHA-256 校验值的可用固件。可前往 <a href=${REPO + '/releases'}>GitHub Releases</a> 查看下载。</p>`}
          <fieldset disabled=${Boolean(busy)}><legend>安装方式</legend><label class="choice"><input type="radio" name="install" checked=${install === 'update'} onChange=${() => { setInstall('update'); setAcknowledged(false); }} /><span><strong>更新固件</strong><small>已经安装 tinyradio，保留兼容的设置。</small></span><span class="tag">日常更新</span></label>
          <label class="choice"><input type="radio" name="install" checked=${install === 'full'} onChange=${() => { setInstall('full'); setAcknowledged(false); }} /><span><strong>完整安装</strong><small>首次安装或恢复设备，重置所有设置与数据。</small></span></label></fieldset>
          ${(install === 'full' || board === 'v5a') && html`<label class="ack"><input type="checkbox" checked=${acknowledged} disabled=${Boolean(busy)} onChange=${e => setAcknowledged(e.target.checked)} /><span>${board === 'v5a' ? '我已备份原厂固件，确认设备为 GeekNest V5A，扬声器开关已切至 Analog。' : '我确认设备型号正确，并了解完整安装会覆盖设备上的设置与数据。'}</span></label>`}
          <div class="install-actions"><button class="primary" disabled=${!supported || !image || Boolean(busy) || connected || ((install === 'full' || board === 'v5a') && !acknowledged)} onClick=${() => run('刷机进行中，请勿拔线…', startFlash)}>↧ 连接并${install === 'full' ? '安装' : '更新'}固件</button>${image && html`<a href=${image.download} target="_blank" rel="noreferrer">下载 .bin ↗</a>`}</div>
          ${image && html`<label class="local-firmware">本地固件（可选）<input ref=${fileInput} type="file" accept=".bin,application/octet-stream" disabled=${Boolean(busy)} onChange=${e => setLocalFile(e.target.files[0] || null)} /><small>若 GitHub 自动下载被浏览器拦截，请先点击「下载 .bin」，再选择下载的文件。文件必须与当前型号、版本及安装方式匹配。</small></label>`}
          ${connected && html`<p class="muted">刷机前请先断开下方控制台的连接。</p>`}
          ${release && html`<p class="build-meta"><a href=${release.url} target="_blank" rel="noreferrer">${release.tag} ↗</a>${images && html`<span> · SHA-256 校验</span>`}</p>`}
          ${logs.length > 0 && html`<div class="flash-progress"><div class="row"><strong>${flashed ? '刷机完成 ✓' : busy ? '正在处理固件' : '刷机未完成'}</strong><span>${progress}%</span></div><progress max="100" value=${progress} aria-label="固件写入进度"></progress><details open=${!flashed}><summary>刷机日志</summary><pre class="log">${logs.join('\n')}</pre></details></div>`}
        </div><aside class="install-guide"><span class="guide-icon" aria-hidden="true">↗</span><h3>准备好，就连接。</h3><ol><li><strong>确认设备</strong><p>选择与你的硬件一致的型号。V5A 首次安装前请备份原厂固件。</p></li><li><strong>插上数据线</strong><p>使用可传输数据的 USB 线，并关闭串口监视器等占用端口的程序。</p></li><li><strong>选择版本，开始安装</strong><p>允许浏览器访问串口。若无法进入引导程序，按设备说明进入 BOOT 模式后重试。</p></li><li><strong>重新连接，开始收听</strong><p>刷机后等待设备启动，再连接下方控制台。</p></li></ol><a href=${REPO + '/blob/master/docs/user-manual.md'} target="_blank" rel="noreferrer">阅读完整使用手册 ↗</a></aside></div>
      </section>
      <section id="console" class="section">
        <div class="section-heading"><div><p class="eyebrow">02 / DEVICE CONSOLE</p><h2>你的收音机，现在在线。</h2></div><span class=${'connection-badge ' + (connected ? 'online' : '')}><span class="dot"></span>${connected ? '已连接设备' : '等待连接'}</span></div>
        <div class="console-toolbar"><p>USB 本地连接，屏幕和操作数据只在浏览器与设备之间传输。</p><button disabled=${!supported || Boolean(busy)} class=${connected ? '' : 'primary'} onClick=${() => run(connected ? '断开连接…' : '正在连接设备…', () => connected ? connection.current.close() : connect())}>${connected ? '断开连接' : '↗ 连接设备'}</button></div>
        <div class="console-grid"><div class="panel preview-panel"><div class="row"><h3>屏幕预览</h3><span class="tag">${captured ? connected ? '设备画面' : '上次画面 · 已断开' : '未读取'}</span></div>
          <div class="screen-wrap"><canvas ref=${canvas} width="320" height="170" hidden=${!captured} aria-label="连接设备的实际屏幕截图"></canvas>${!captured && html`<div class="screen-placeholder"><span aria-hidden="true">▥</span><strong>${connected ? '设备已就绪' : '等待你的收音机'}</strong><p>${connected ? '点击「刷新屏幕」读取实际画面' : '连接后，即可读取真实屏幕画面'}</p></div>`}</div>
          <div class="preview-actions"><button disabled=${disabled} onClick=${() => run('读取屏幕中…', capture)}>↻ 刷新屏幕</button><label><input type="checkbox" checked=${live} disabled=${disabled} onChange=${e => setLive(e.target.checked)} />连续预览</label><button class="text-button" disabled=${!captured || Boolean(busy)} onClick=${() => { const link = document.createElement('a'); link.href = canvas.current.toDataURL('image/png'); link.download = 'tinyradio-screen.png'; link.click(); }}>保存 PNG ↓</button></div>
          <p class="hint">${captured ? `最后读取 ${captured}。` : ''}屏幕按完整帧传输，读取期间设备 UI 会短暂暂停，可能需要约 15–25 秒；连续预览在每帧完成后继续。</p>
          <dl class="telemetry"><div><dt>RSSI</dt><dd>${connected ? radioState.RSSI : '—'} <small>dBµV</small></dd></div><div><dt>SNR</dt><dd>${connected ? radioState.SNR : '—'} <small>dB</small></dd></div><div><dt>电源</dt><dd>${connected ? radioState.supply : '—'} <small>V</small></dd></div></dl>
        </div><div class="panel tuning-panel"><div class="row"><h3>调谐与控制</h3><span class="tag">${connected ? radioState.mode : 'USB SERIAL'}</span></div>
          <div class="frequency"><span>${connected ? radioState.band : 'FREQUENCY'}</span><strong>${connected ? frequency(radioState.requested) : '—.———'}</strong><span>MHz</span></div>
          <div class="station"><strong>${connected ? radioState.ps || '等待电台信息' : '连接后开始调谐'}</strong><p>${connected ? radioState.ert || radioState.rt || '伸展天线，寻找属于你的频率。' : '已安装 tinyradio 的设备可直接连接。'}</p></div>
          <fieldset disabled=${disabled}><legend class="sr-only">波段与模式</legend><div class="segments">${[['LW', '0'], ['MW', '1'], ['SW', '2'], ['VHF', '3'], ...(board === 'v5a' ? [['AIR', '4']] : [])].map(([name, cmd]) => html`<button class=${radioState.band === name && connected ? 'selected' : ''} onClick=${() => run('切换波段…', () => command(cmd))}>${name === 'VHF' ? 'FM / VHF' : name}</button>`)}</div><div class="segments modes">${[['AM', 'a'], ['FM', 'f'], ['LSB', 'l'], ['USB', 'u']].map(([name, cmd]) => html`<button disabled=${connected && ((radioState.band === 'VHF' && name !== 'FM') || (radioState.band !== 'VHF' && name === 'FM') || (radioState.band === 'AIR' && name !== 'AM'))} class=${radioState.mode === name && connected ? 'selected' : ''} onClick=${() => run('切换模式…', () => command(cmd))}>${name}</button>`)}</div></fieldset>
          <div class="tune-buttons"><button disabled=${disabled} aria-label="降低一个频率步进" onClick=${() => run('调谐…', () => command('-', 1))}>−</button><span>${connected ? `步进 ${radioState.step} Hz` : '手动调谐'}</span><button disabled=${disabled} aria-label="升高一个频率步进" onClick=${() => run('调谐…', () => command('+', 1))}>+</button></div>
          <div class="control-actions"><button disabled=${disabled || radioState.mode !== 'FM' && radioState.mode !== 'AM' || radioState.band === 'AIR'} onClick=${() => run('向下搜台…', async () => { await connection.current.focus(1); await command('h-'); })}>↤ 搜台</button><button disabled=${disabled} onClick=${() => run('停止搜台…', async () => { await connection.current.focus(1); setState(await connection.current.status()); })}>停止</button><button disabled=${disabled || radioState.mode !== 'FM' && radioState.mode !== 'AM' || radioState.band === 'AIR'} onClick=${() => run('向上搜台…', async () => { await connection.current.focus(1); await command('h+'); })}>搜台 ↦</button><button disabled=${disabled} aria-pressed=${Boolean(radioState.mute)} onClick=${() => run('切换静音…', () => command('m'))}>${connected && radioState.mute ? '取消静音' : '静音'}</button></div>
          ${connected && (radioState.searching || radioState.seek) ? html`<p class="hint">搜台模式${radioState.searching ? ' · 正在搜索' : ''}。点击停止或手动调谐退出。</p>` : null}
        </div></div>
        <details class="advanced"><summary>更多设备操作 <span>带宽、增益、音量与显示</span></summary><div class="parameter-grid">${fields.map(field => html`<div class="parameter" key=${field.index}><div><label>${field.name}</label><strong>${connected ? field.value(radioState) : '—'}</strong></div><button disabled=${disabled} aria-label=${'减小' + field.name} onClick=${() => run('调整' + field.name + '…', () => command('<', field.index))}>−</button><button disabled=${disabled} aria-label=${'增大' + field.name} onClick=${() => run('调整' + field.name + '…', () => command('>', field.index))}>+</button></div>`)}</div><div class="control-actions"><button disabled=${disabled} onClick=${() => run('切换主题…', () => command('h', 9))}>切换设备明暗主题</button><button disabled=${disabled} onClick=${() => run('切换波段锁定…', () => command('h', 0))}>${connected && radioState.locked ? '解除波段锁定' : '锁定当前波段'}</button><button disabled=${disabled} onClick=${() => run('重试接收芯片…', () => command('r'))}>重试接收芯片</button><button disabled=${disabled} onClick=${() => run('刷新设备状态…', () => command(''))}>刷新状态</button></div>${connected && html`<p class="hint">设备状态：${radioState.error} · 保存${radioState.save ? '中' : '完成'} · 已确认频率 ${frequency(radioState.confirmed)} MHz</p>`}</details>
      </section>
      <section class="closing"><div><p class="eyebrow">SMALL RADIO. OPEN POSSIBILITIES.</p><h2>保持好奇，继续收听。</h2><p>从硬件到固件，一起探索无线电。</p></div><div class="closing-links"><a href=${REPO}>浏览源代码 ↗</a><a href=${REPO + '/blob/master/docs/user-manual.md'}>使用手册 ↗</a><a href=${REPO + '/issues'}>反馈问题 ↗</a></div></section>
    </main>
    <footer class="site-footer"><a class="brand" href="#" aria-label="tinyradio 首页"><span class="brand-tiny">tiny</span><span class="brand-radio">radio</span></a><p>Made for the joy of listening.</p><a href=${REPO + '/blob/master/LICENSE'}>License ↗</a></footer>
    <div class="status-area" aria-live="polite">${busy && html`<p class="status busy">${busy}</p>`}${message && html`<p class="status success">${message}</p>`}${error && html`<p class="status error" role="alert">${error}<button aria-label="关闭错误提示" onClick=${() => setError('')}>×</button></p>`}</div>
  </div>`;
}
render(html`<${App} />`, document.getElementById('app'));
