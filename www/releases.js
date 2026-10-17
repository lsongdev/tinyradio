const API = 'https://api.github.com/repos/lsongdev/tinyradio';
const TARGETS = { 'ats-mini': ['ats-mini', 16 * 1024 * 1024], v5a: ['geeknest-v5a', 4 * 1024 * 1024] };

export function parseReleases(data) {
  if (!Array.isArray(data)) throw new Error('GitHub 返回了无效的版本列表。');
  return data.filter(release => !release.draft).map(release => {
    const boards = {};
    for (const [board, [prefix, size]] of Object.entries(TARGETS)) {
      const images = {};
      for (const [kind, suffix, address] of [['update', 'firmware', 0x10000], ['full', 'full', 0]]) {
        const asset = release.assets?.find(asset => asset.name === `${prefix}-${suffix}.bin` && asset.state === 'uploaded');
        const sha256 = /^sha256:([a-f0-9]{64})$/.exec(asset?.digest)?.[1];
        if (!asset || !sha256 || !Number.isSafeInteger(asset.id) || !(asset.size > 0 && asset.size <= size - address) || kind === 'full' && asset.size !== size) continue;
        images[kind] = {
          name: asset.name, path: `${API}/releases/assets/${asset.id}`,
          download: `https://github.com/lsongdev/tinyradio/releases/download/${encodeURIComponent(release.tag_name)}/${asset.name}`,
          size: asset.size, sha256, address,
        };
      }
      if (Object.keys(images).length) boards[board] = images;
    }
    return {
      tag: release.tag_name, url: `https://github.com/lsongdev/tinyradio/releases/tag/${encodeURIComponent(release.tag_name)}`,
      prerelease: release.prerelease, boards,
    };
  }).filter(release => Object.keys(release.boards).length);
}

export async function fetchReleases(fetcher = fetch) {
  const response = await fetcher(`${API}/releases?per_page=100`, {
    headers: { Accept: 'application/vnd.github+json' }, signal: AbortSignal.timeout(20000),
  });
  if (!response.ok) {
    if ([403, 429].includes(response.status)) throw new Error('GitHub API 请求受限，请稍后重试，或直接前往 Releases 下载固件。');
    throw new Error(`版本列表加载失败 (${response.status})。`);
  }
  return parseReleases(await response.json());
}
