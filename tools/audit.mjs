#!/usr/bin/env node
// 公开前的专项审计：密钥、个人路径、未公开代号、不该提交的二进制、清单一致性。
//
//   node tools/audit.mjs --module            审计一个模块仓库（含清单校验）
//   node tools/audit.mjs --registry-entry ../fgb-registry/modules/x.json --tag v0.1.0
//
// 泛化的密钥扫描不够：真正出过事的是「未公开产品的代号与版本号」进了公开 README，
// 所以除了常规模式，这里还带一组项目相关的敏感词。
import { readFileSync, writeFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { resolve, dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const args = process.argv.slice(2);
const flag = (n) => args.includes(`--${n}`);
const option = (n, d) => {
  const hit = args.find((a) => a.startsWith(`--${n}=`));
  return hit ? hit.slice(n.length + 3) : d;
};
const target = resolve(process.cwd(), option('dir', '.'));

const SECRET_PATTERNS = [
  { id: 'github-token', re: /\b(ghp|gho|ghu|ghs)_[A-Za-z0-9]{20,}\b/, why: 'GitHub 令牌' },
  { id: 'github-fine-grained', re: /\bgithub_pat_[A-Za-z0-9_]{20,}\b/, why: 'GitHub 细粒度令牌' },
  { id: 'private-key', re: /-----BEGIN (RSA |EC |OPENSSH |PGP )?PRIVATE KEY-----/, why: '私钥' },
  { id: 'aws-key', re: /\bAKIA[0-9A-Z]{16}\b/, why: 'AWS 访问密钥' },
  { id: 'slack-token', re: /\bxox[baprs]-[A-Za-z0-9-]{10,}\b/, why: 'Slack 令牌' },
  { id: 'basic-auth-url', re: /\/\/[^\s/@:]+:[^\s/@]+@/, why: 'URL 内嵌凭据' },
];

const PRIVACY_PATTERNS = [
  { id: 'user-home', re: /(?:[A-Za-z]:\\Users\\|\/home\/[a-z]{2,}|\/Users\/)[^\s"'`)]*/i, why: '个人主目录路径' },
  { id: 'dev-checkout', re: /\b[A-Za-z]:\\code\\/i, why: '开发机仓库路径' },
  { id: 'account-name', re: /\bGYSS\b/, why: '本机账户名' },
  { id: 'private-email', re: /[\w.+-]+@(?:qq|163|126|foxmail|gmail|outlook|hotmail)\.com/i, why: '个人邮箱' },
];

// 未公开构建的代号与可识别版本号，不允许出现在任何公开文件里。
const CODENAME_PATTERNS = [
  { id: 'tmp-build', re: /PICO Connect\s*TMP/i, why: '未公开的 PICO 构建代号' },
  { id: 'tmp-dir', re: /Connect\s*TMP[\\/]/i, why: '未公开的 PICO 目录名' },
];

const BINARY_EXTENSIONS = new Set([
  '.dll', '.lib', '.pdb', '.obj', '.exe', '.zip', '.7z', '.rar', '.pcap', '.pcapng', '.etl',
]);

const SKIP_DIRS = new Set(['.git', 'build', 'out', 'node_modules', '.vs', '.sdk-cache', 'dist']);

function listFiles(dir) {
  let tracked = null;
  try {
    tracked = execFileSync('git', ['ls-files'], { cwd: dir, encoding: 'utf8' })
      .split(/\r?\n/)
      .filter(Boolean);
  } catch {
    tracked = null; // 还没 git init，退化为目录遍历
  }
  if (tracked) return tracked.map((f) => join(dir, f));

  const out = [];
  const walk = (current) => {
    for (const entry of readdirSync(current)) {
      if (SKIP_DIRS.has(entry)) continue;
      const full = join(current, entry);
      const stat = statSync(full);
      if (stat.isDirectory()) walk(full);
      else out.push(full);
    }
  };
  walk(dir);
  return out;
}

const findings = [];
const files = listFiles(target);

for (const file of files) {
  const rel = relative(target, file).replace(/\\/g, '/');
  const ext = (rel.match(/\.[a-z0-9]+$/i) || [''])[0].toLowerCase();
  if (BINARY_EXTENSIONS.has(ext)) {
    findings.push({ rule: 'committed-binary', file: rel, why: `不该提交的二进制产物 ${ext}` });
    continue;
  }
  let text;
  try {
    text = readFileSync(file, 'utf8');
  } catch {
    continue; // 非 UTF-8 文本按二进制处理
  }
  if (/\0/.test(text.slice(0, 2048))) {
    findings.push({ rule: 'committed-binary', file: rel, why: '文本路径里出现二进制内容' });
    continue;
  }
  const lines = text.split(/\r?\n/);
  for (const group of [SECRET_PATTERNS, PRIVACY_PATTERNS, CODENAME_PATTERNS]) {
    for (const pattern of group) {
      lines.forEach((line, index) => {
        if (pattern.re.test(line)) {
          findings.push({
            rule: pattern.id,
            file: rel,
            line: index + 1,
            why: pattern.why,
            // 只报片段，避免审计输出本身泄露密钥
            sample: line.trim().slice(0, 60),
          });
        }
      });
    }
  }
}

// ---- 必备文件 ----
const REQUIRED_BASE = ['LICENSE', 'NOTICE', 'README.md', '.gitignore', '.gitattributes'];
const REQUIRED_MODULE = [
  ...REQUIRED_BASE,
  'sdk.lock.json',
  'kit.lock.json',
  'CMakeLists.txt',
  'module/fgb-native.json',
  '.github/workflows/ci.yml',
];
const required = flag('module') ? REQUIRED_MODULE : REQUIRED_BASE;
for (const name of required) {
  if (!existsSync(join(target, name))) {
    findings.push({ rule: 'missing-file', file: name, why: '公开发布所需的文件不存在' });
  }
}

// ---- 清单一致性 ----
const manifestPath = join(target, 'module/fgb-native.json');
if (flag('module') && existsSync(manifestPath)) {
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
  const problems = [];
  // 新模块统一 com.fgbbridge.<slug>；com.picogaze.pico-connect-face 是已有用户的遗留 ID，
  // 换掉会让已安装用户收不到升级，所以这里作为唯一豁免记录在案。
  const LEGACY_MODULE_IDS = new Set(['com.picogaze.pico-connect-face']);
  const moduleId = manifest.ModuleId || '';
  if (!LEGACY_MODULE_IDS.has(moduleId) && !/^com\.fgbbridge\.[a-z0-9][a-z0-9-]*$/.test(moduleId)) {
    problems.push(`ModuleId 不符合 com.fgbbridge.<slug>：${moduleId}`);
  }
  if (manifest.AbiVersion !== 2) problems.push(`AbiVersion 必须是 2：${manifest.AbiVersion}`);
  if (!/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(manifest.Version || '')) {
    problems.push(`Version 不是 semver：${manifest.Version}`);
  }
  if (!/^[a-z0-9-]+\.dll$/.test(manifest.DllFileName || '')) {
    problems.push(`DllFileName 必须是小写 kebab-case 的 .dll：${manifest.DllFileName}`);
  }
  const allowed = new Set(['expression', 'eyes', 'gaze', 'head']);
  for (const capability of manifest.Capabilities || []) {
    if (!allowed.has(capability)) problems.push(`未知能力：${capability}`);
  }
  if (!manifest.Capabilities || manifest.Capabilities.length === 0) {
    problems.push('Capabilities 不能为空');
  }
  if (manifest.ModuleIcon && /\.\./.test(manifest.ModuleIcon)) {
    problems.push(`ModuleIcon 不能越出模块目录：${manifest.ModuleIcon}`);
  }
  const tag = option('tag', '');
  if (tag && `v${manifest.Version}` !== tag) {
    problems.push(`tag ${tag} 与清单 Version ${manifest.Version} 不一致`);
  }
  for (const problem of problems) {
    findings.push({ rule: 'manifest', file: 'module/fgb-native.json', why: problem });
  }

  // 上游归因：NOTICE 里必须出现所参考的项目名
  const notice = readFileSync(join(target, 'NOTICE'), 'utf8');
  if (!/upstream|参考|informed|implements/i.test(notice) && manifest.Upstream) {
    findings.push({ rule: 'notice', file: 'NOTICE', why: '声明了 Upstream 但 NOTICE 没有归因' });
  }
}

// ---- registry 条目 ----
const entryPath = option('registry-entry', '');
if (entryPath) {
  const full = resolve(target, entryPath);
  if (!existsSync(full)) {
    findings.push({ rule: 'registry-entry', file: entryPath, why: '条目文件不存在' });
  } else {
    const entry = JSON.parse(readFileSync(full, 'utf8'));
    const problems = [];
    if (entry.verified !== false) problems.push('首期条目必须 verified:false');
    if (!(entry.tags || []).includes('untested')) problems.push('首期条目必须带 untested 标签');
    if (entry.requires?.abi !== 2) problems.push('requires.abi 必须是 2');
    if (!/^v$/.test(entry.tagPrefix || '')) problems.push('tagPrefix 必须是 v');
    if (!(entry.requires?.os || []).includes('windows')) problems.push('requires.os 必须含 windows');
    if (!(entry.requires?.arch || []).includes('x64')) problems.push('requires.arch 必须含 x64');
    if (!entry.assetPattern || !entry.entry) problems.push('缺少 assetPattern 或 entry');
    if (!entry.i18n?.en?.summary) problems.push('缺少 i18n.en（公开仓库需要英文摘要）');
    if (flag('module') && existsSync(manifestPath)) {
      const packageManifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
      if (entry.id !== packageManifest.ModuleId) problems.push('条目 id 与包内 ModuleId 不一致');
      if (entry.entry !== packageManifest.DllFileName) {
        problems.push('条目 entry 与包内 DllFileName 不一致');
      }
    }
    for (const problem of problems) {
      findings.push({ rule: 'registry-entry', file: entryPath, why: problem });
    }
  }
}

const report = {
  target: relative(process.cwd(), target) || '.',
  files: files.length,
  findings,
};
if (option('json-out', '')) {
  writeFileSync(resolve(process.cwd(), option('json-out')), `${JSON.stringify(report, null, 2)}\n`, 'utf8');
}

if (findings.length === 0) {
  console.log(`审计通过：${files.length} 个文件，无发现`);
  process.exit(0);
}

console.error(`审计发现 ${findings.length} 个问题：`);
for (const finding of findings) {
  const where = finding.line ? `${finding.file}:${finding.line}` : finding.file;
  console.error(`  [${finding.rule}] ${where} — ${finding.why}`);
}
process.exit(1);
