## [0.4.5]
### 变更
- `sdk-sync.yml` 在 SDK 版本变化时会显式派发模块仓库的 `ci` 工作流并等待结果；
  机器人创建的 PR 自带 CI 检查可能停在 `action_required`，现在不再依赖它。
- 同步步骤新增 `actions: write` 权限。
- 版本未变时继续完全跳过同步、开 PR 与构建。

## [0.4.4]
### 修复
- `sdk-sync.yml` 创建 PR 时显式传 `--head`，兼容「远端分支已存在且锁文件已是目标版本、
  本地分支没有 upstream」的情况。

## [0.4.3]
### 修复
- `sdk-sync.yml` 开 PR 步骤改为幂等：分支已存在但 PR 因权限等原因没建成时会补开 PR；
  已有 open PR 时跳过，不再因为分支存在就直接退出。
- 锁文件内容无变化时跳过提交；分支锁文件已是目标版本时不再无意义强推。

## [0.4.2]
### 修复
- `sdk-sync.yml` / `sync-sdk.mjs`：复用工作流从仓库根目录下载脚本时锁文件定位到了错误目录，
  导致每次都误判为「有新版」并且 `--write` 后以退出码 3 失败；现在显式传入 `--root`。
- 只在 SDK **版本号**变化时才更新 `sdk.lock.json`、开 PR 和触发构建；版本未变（即使哈希变化）
  也跳过，避免无意义的同步与 CI。
- `--write` 成功写入后返回 0，不再让工作流步骤失败。

## [0.3.0]
### 新增
- `include/fgb/kit/shm.hpp`：命名共享内存只读映射与命名事件。映射长度先校验再读、
  `ReadAt` 越界即失败且不部分写出、支持 VRCFT 式计数前缀；来源应用没跑起来时错误里
  直接给出「是否已运行并启用该功能」。
- `include/fgb/kit/library.hpp`：厂商运行时动态加载（模块目录 → 系统目录 → PATH），
  符号查表缓存 + 缺失符号记录。厂商 DLL 一律不链接、不打包，只运行时找。
- 自测新增 `kit-shm-tests`、`kit-library-tests`（含写入端/读取端同映射可见、越界、
  长度不足、事件超时与置位、缺失 DLL 与缺失符号的提示文案）。

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
