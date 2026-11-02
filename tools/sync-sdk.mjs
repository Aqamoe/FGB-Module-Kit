#!/usr/bin/env node
// 检查 FaceGaze Bridge 是否发布了新 SDK，并把 sdk.lock.json 升到新版。
//
//   node tools/sync-sdk.mjs --check          只查询，有新版时退出码 3
//   node tools/sync-sdk.mjs --write          写入 lock 文件
//   node tools/sync-sdk.mjs --check --json   把结果写成 JSON（供 CI 读取）
//
// 走的是 Release 资产 + SHA256SUMS.txt，所以「最新 SDK」始终是已经发布并被哈希过的那一版，
// 不会跟着 main 分支跑。GitHub API 匿名限流很紧，CI 里必须带 GITHUB_TOKEN。
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const args = process.argv.slice(2);

const flag = (name) => args.includes(`--${name}`);
const option = (name, fallback) => {
  const hit = args.find((a) => a.startsWith(`--${name}=`));
  return hit ? hit.slice(name.length + 3) : fallback;
};

const lockPath = resolve(root, option('lock', 'sdk.lock.json'));
const repository = option('repo', 'Aqamoe/FaceGaze-Bridge');
const assetPattern = /^FaceGazeBridge-SDK-(\d+\.\d+\.\d+)\.zip$/;

const token = process.env.GITHUB_TOKEN || process.env.GH_TOKEN || '';
const headers = {
  Accept: 'application/vnd.github+json',
  'X-GitHub-Api-Version': '2022-11-28',
  'User-Agent': 'fgb-module-kit/sync-sdk',
};
if (token) headers.Authorization = `Bearer ${token}`;

async function api(path) {
  const response = await fetch(`https://api.github.com${path}`, { headers });
  if (response.status === 403 || response.status === 429) {
    throw new Error(`GitHub API 限流或鉴权失败（${response.status}），请确认已提供 GITHUB_TOKEN`);
  }
  if (!response.ok) {
    throw new Error(`GitHub API ${path} 返回 ${response.status} ${response.statusText}`);
  }
  return response.json();
}

async function assetText(release, name) {
  const asset = release.assets.find((a) => a.name === name);
  if (!asset) return null;
  const response = await fetch(asset.url, { headers: { ...headers, Accept: 'application/octet-stream' } });
  if (!response.ok) throw new Error(`下载 ${name} 返回 ${response.status}`);
  return response.text();
}

async function sha256FromSums(release, zipName) {
  const sums = await assetText(release, 'SHA256SUMS.txt');
  if (!sums) return null;
  for (const line of sums.split(/\r?\n/)) {
    const [hash, file] = line.trim().split(/\s+/);
    if (file === zipName && /^[a-f0-9]{64}$/.test(hash || '')) return hash.toLowerCase();
  }
  return null;
}

function readLock() {
  if (!existsSync(lockPath)) return null;
  try {
    return JSON.parse(readFileSync(lockPath, 'utf8'));
  } catch (error) {
    throw new Error(`${lockPath} 解析失败：${error.message}`);
  }
}

function writeLock(next) {
  writeFileSync(lockPath, `${JSON.stringify(next, null, 2)}\n`, 'utf8');
}

const release = await api(`/repos/${repository}/releases/latest`);
if (release.prerelease || release.draft) {
  console.error(`latest 指向预发布 ${release.tag_name}，不做自动升级`);
  process.exit(2);
}

const zip = release.assets.find((a) => assetPattern.test(a.name));
if (!zip) {
  console.error(`Release ${release.tag_name} 没有 FaceGazeBridge-SDK-*.zip 资产`);
  process.exit(2);
}
const version = assetPattern.exec(zip.name)[1];
const sha256 = await sha256FromSums(release, zip.name);
if (!sha256) {
  console.error(`${release.tag_name} 的 SHA256SUMS.txt 里没有 ${zip.name} 的哈希`);
  process.exit(2);
}

const current = readLock();
const updated = !current || current.version !== version || current.sha256 !== sha256;
const next = {
  repository,
  version,
  url: zip.browser_download_url,
  sha256,
};

const report = {
  updated,
  current: current ? current.version : null,
  version,
  sha256,
  tag: release.tag_name,
  publishedAt: release.published_at,
  url: zip.browser_download_url,
};

if (flag('json')) {
  writeFileSync(resolve(root, 'sdk-update.json'), `${JSON.stringify(report, null, 2)}\n`, 'utf8');
}

console.log(
  updated
    ? `SDK 有新版：${report.current ?? '(无 lock)'} → ${version}（${release.tag_name}）`
    : `SDK 已是最新：${version}`,
);

if (flag('write') && updated) {
  writeLock(next);
  console.log(`已写入 ${lockPath}`);
}

if (updated && !flag('write')) {
  console.log('提示：加 --write 才会真正更新 lock 文件');
}

process.exit(updated ? 3 : 0);
