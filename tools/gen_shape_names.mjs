#!/usr/bin/env node
// 从 SDK 的 Unity 示例里抽出 Unified Expressions 形状名表，生成 C++ 头。
//
// 唯一真源是 SDK：sdk.lock.json 变了就重新生成，CI 用 --check 挡住漂移。
// 手写这份名单的后果是「模块里拼错的名字在测试宿主里也能解析」，等于自测失效。
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const args = process.argv.slice(2);
const checkOnly = args.includes('--check');

function findSource() {
  const explicit = args.find((a) => a.startsWith('--sdk='));
  const candidates = [];
  if (explicit) candidates.push(resolve(explicit.slice('--sdk='.length)));
  const env = process.env.FGB_SDK_DIR;
  if (env) candidates.push(resolve(env));
  for (const dir of candidates) {
    const file = resolve(dir, 'samples/unity-face-gaze/Runtime/FaceGazeShapeNames.cs');
    if (existsSync(file)) return file;
  }
  console.error('找不到 SDK 的 FaceGazeShapeNames.cs，用 --sdk=<SDK 根目录> 或设置 FGB_SDK_DIR');
  process.exit(2);
}

const source = findSource();
const text = readFileSync(source, 'utf8');
const names = [...text.matchAll(/public const int (\w+) = (\d+);/g)].filter(
  ([, name]) => name !== 'Count',
);
const declared = /public const int Count = (\d+);/.exec(text);

if (!declared) {
  console.error('解析失败：没有 Count');
  process.exit(1);
}
const count = Number(declared[1]);
if (names.length !== count) {
  console.error(`解析失败：Count=${count} 但找到 ${names.length} 个形状名`);
  process.exit(1);
}

const byIndex = new Array(count);
for (const [, name, index] of names) {
  const i = Number(index);
  if (byIndex[i]) {
    console.error(`索引 ${i} 重复：${byIndex[i]} 与 ${name}`);
    process.exit(1);
  }
  byIndex[i] = name;
}
if (byIndex.some((n) => !n)) {
  console.error('索引不连续');
  process.exit(1);
}

const wrap = (items, perLine = 4) => {
  const lines = [];
  for (let i = 0; i < items.length; i += perLine) {
    lines.push('    ' + items.slice(i, i + perLine).map((s) => `"${s}",`).join(' '));
  }
  return lines.join('\n');
};

const header = `#pragma once
// 由 tools/gen_shape_names.mjs 从 SDK 的 FaceGazeShapeNames.cs 生成，不要手改。
// 顺序必须与 FgbFrameSlot::shapes / shape_valid 一致。

#include <array>
#include <cstddef>
#include <string_view>

namespace fgb::kit {

inline constexpr std::size_t kUnifiedExpressionCount = ${count};

inline constexpr std::array<std::string_view, ${count}> kUnifiedExpressionNames = {
${wrap(byIndex)}
};

} // namespace fgb::kit
`;

const target = resolve(root, 'include/fgb/kit/shape_names.hpp');
if (checkOnly) {
  const current = existsSync(target) ? readFileSync(target, 'utf8') : '';
  if (current !== header) {
    console.error('shape_names.hpp 与当前 SDK 不一致，请重新运行 tools/gen_shape_names.mjs');
    process.exit(1);
  }
  console.log(`shape_names.hpp 与 SDK 一致（${count} 项）`);
  process.exit(0);
}

writeFileSync(target, header, { encoding: 'utf8' });
console.log(`已生成 shape_names.hpp：${count} 个 Unified Expressions 形状`);
