import test from 'node:test';
import assert from 'node:assert/strict';
import { verifiedImage } from '../firmware.js';

globalThis.location = { href: 'https://lsongdev.github.io/tinyradio/', origin: 'https://lsongdev.github.io' };
const bytes = new Uint8Array([0, 1, 255, 32]);
const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
const image = { path: './firmware/daily-test/test.bin', size: bytes.length, sha256: hash };
const fetcher = async url => {
  assert.equal(url.href, 'https://lsongdev.github.io/tinyradio/firmware/daily-test/test.bin');
  return new Response(bytes);
};
test('release images resolve under project Pages path and pass SHA-256', async () => {
  assert.deepEqual(await verifiedImage(image, fetcher), bytes);
});
test('wrong hashes, truncation, HTTP errors and remote images abort before flashing', async () => {
  await assert.rejects(verifiedImage({ ...image, sha256: 'a'.repeat(64) }, fetcher), /校验失败/);
  await assert.rejects(verifiedImage({ ...image, size: 3 }, fetcher), /大小不匹配/);
  await assert.rejects(verifiedImage(image, async () => new Response('', { status: 404 })), /下载失败/);
  await assert.rejects(verifiedImage({ ...image, path: 'https://example.com/file.bin' }, fetcher), /清单无效/);
});
