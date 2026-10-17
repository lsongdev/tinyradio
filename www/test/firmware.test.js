import test from 'node:test';
import assert from 'node:assert/strict';
import { verifiedImage } from '../firmware.js';

const bytes = new Uint8Array([0, 1, 255, 32]);
const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
const image = { path: 'https://api.github.com/repos/lsongdev/tinyradio/releases/assets/123', size: bytes.length, sha256: hash };
const fetcher = async (url, options) => {
  assert.equal(url, image.path);
  assert.equal(options.headers.Accept, 'application/octet-stream');
  return new Response(bytes);
};

test('release downloads request binary content and verify SHA-256', async () => {
  assert.deepEqual(await verifiedImage(image, { fetcher }), bytes);
});
test('local files use the selected release hash and do not make a network request', async () => {
  const file = new File([bytes], 'firmware.bin');
  assert.deepEqual(await verifiedImage(image, { file, fetcher: () => assert.fail('unexpected network request') }), bytes);
  await assert.rejects(verifiedImage(image, { file: new File([bytes.slice(1)], 'firmware.bin') }), /大小不匹配/);
  await assert.rejects(verifiedImage(image, { file: new File([new Uint8Array(4)], 'firmware.bin') }), /校验失败/);
});
test('wrong hashes, truncation, HTTP errors and untrusted asset URLs abort before flashing', async () => {
  await assert.rejects(verifiedImage({ ...image, sha256: 'a'.repeat(64) }, { fetcher }), /校验失败/);
  await assert.rejects(verifiedImage({ ...image, size: 3 }, { fetcher }), /大小不匹配/);
  await assert.rejects(verifiedImage(image, { fetcher: async () => new Response('', { status: 404 }) }), /下载失败/);
  await assert.rejects(verifiedImage({ ...image, path: 'https://example.com/file.bin' }, { fetcher }), /信息无效/);
});
test('CORS and network failure explain the local-file recovery path', async () => {
  await assert.rejects(verifiedImage(image, { fetcher: async () => { throw new TypeError('Failed to fetch'); } }), /本地固件/);
});
