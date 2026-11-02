## [0.2.2]
### 修复
- `testing::HostMock::MakeParams` 的 `module_dir` 指向调用期临时量，返回后即悬垂：模块在 `Start` 里读
  `params.module_dir` 会拿到随机字节，表现为冒烟测试偶发崩溃或 `ConfigIni` 抛 codecvt 异常；
  现在先把目录落到成员再取 `c_str()`。

## [0.2.0]
### 新增
- `include/fgb/kit/osc.hpp`：OSC 1.0 只读解析（单消息与 `#bundle`、4 字节对齐、
  `i/f/d/s/c/b/T/F/N`，深度 / 长度 / 消息数上限），供 ETVR、Babble、SteamLink 一类
  OSC 来源使用。`Parse()` 会把内部失败原因回传给调用方，便于日志给出「为什么丢弃这一包」。
- Kit 自测新增 `kit-osc-tests`。

## [0.1.1]
### 修复
- 复用工作流在 PowerShell 5.1 下把单产品版本输出截成首字符（`0.1.0` → `0`），
  导致 tag 与清单版本比对必然失败；改为强制数组并用 `Add-Content -Encoding utf8` 写输出。

## [0.1.0]
### 新增
- `host.hpp` / `arkit.hpp` / `udp.hpp` / `json.hpp` / `config.hpp` / `module.hpp` /
  `shape_names.hpp` / `testing/host_mock.hpp` / `testing/check.hpp`。
- `cmake/fgb_dependency.cmake`（锁文件解析）、`cmake/FgbModule.cmake` +
  `FgbPackageScript.cmake`（模块构建与按清单打包）。
- 复用工作流 `module-ci.yml`、`sdk-sync.yml`、`kit.yml`；工具 `gen_shape_names.mjs`、
  `sync-sdk.mjs`、`audit.mjs`。
