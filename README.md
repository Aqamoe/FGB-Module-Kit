# FGB-Module-Kit

[![kit](https://github.com/Aqamoe/FGB-Module-Kit/actions/workflows/kit.yml/badge.svg)](https://github.com/Aqamoe/FGB-Module-Kit/actions/workflows/kit.yml)

[FaceGaze Bridge](https://github.com/Aqamoe/FaceGaze-Bridge) 原生模块的**共享工具链**：把 19 个模块仓库都会重复写的那部分（宿主交互、ARKit 映射、UDP/JSON/配置、CI、发版与上架脚本）集中在这里维护一份。

> 这个仓库本身不是模块，不进 `fgb-registry`。模块仓库通过 `kit.lock.json` 引用它的 tag。

## English

Shared toolchain for FaceGaze Bridge native modules: host plumbing, ARKit → Unified Expressions mapping, UDP/JSON/config helpers, a reusable CI workflow, and the SDK-lock / audit / publish scripts. Not a module itself — module repos pin it by tag through `kit.lock.json`.

## 包含什么

| 位置 | 内容 |
|---|---|
| `include/fgb/kit/host.hpp` | `Host`（日志 / 状态上报 / 形状下标）与 `ShapeTable`（按名解析、max 合并写入） |
| `include/fgb/kit/arkit.hpp` | ARKit 52 系数、跨厂商名字归一、到 Unified Expressions 的通用映射、眼睛推导 |
| `include/fgb/kit/udp.hpp` | Winsock UDP 接收端：后台线程收包、有界队列、断流统计、发送回包 |
| `include/fgb/kit/osc.hpp` | OSC 1.0 只读解析：单消息与 `#bundle`、4 字节对齐、`i/f/d/s/c/b/T/F/N` 参数、深度与长度上限 |
| `include/fgb/kit/json.hpp` | 只读 JSON 解析器，带深度 / 节点数 / 长度上限（上游负载是不可信输入） |
| `include/fgb/kit/config.hpp` | 模块目录下的 `config.ini` 读写，未知键会被记录 |
| `include/fgb/kit/module.hpp` | ABI 生命周期脚手架 + `FGB_KIT_EXPORT` 宏 + `Staleness` 断流监控 |
| `include/fgb/kit/shape_names.hpp` | 由 SDK 生成的 88 个 Unified Expressions 形状名（勿手改） |
| `include/fgb/kit/testing/host_mock.hpp` | 假宿主：给单测与 ABI 冒烟用，形状下标同样来自 SDK 名单 |
| `cmake/fgb_dependency.cmake` | 从 `*.lock.json` 解析 URL + SHA256 并 `FetchContent` 拉依赖 |
| `template/` | 新模块骨架，由 `tools/new-module.mjs` 展开 |
| `.github/workflows/module-ci.yml` | 模块仓库共用的可复用 CI（构建 / 测试 / 打包 / 校验 / 发 Release） |
| `tools/` | `gen_shape_names.mjs`、`sync-sdk.mjs`、`audit.mjs`、`new-module.mjs`、`registry-add.mjs` |

## 设计约束

- **不持有 SDK 头文件副本。** `fgb_module_api.h` 由 `sdk.lock.json` 指向的 Release 包在构建时拉取；接口升级 = 改 lock，不是拷文件。
- **只放通用规则。** 某个来源特有的启发式（例如用微笑近似 CheekSquint）留在该模块里，避免一个来源的经验值污染其它模块。
- **测试宿主的形状下标来自 SDK 生成的名单**，所以「目标名拼错」在单测里就会失败，而不是到用户机器上才发现。
- 全部 header-only，C++20，MSVC x64；除 Winsock 与标准库外无第三方依赖。

## 本地构建

需要 Visual Studio 2022（x64）与 CMake ≥ 3.24。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

首次配置会从 FaceGaze Bridge 的 Release 下载 SDK zip（约 95 KB），并校验 `sdk.lock.json` 里的 SHA256。离线环境可以预先指定：

```powershell
cmake -S . -B build -A x64 -DFGB_SDK_DIR="D:/sdk/FaceGazeBridge-SDK-1.0.8"
```

## 与 SDK 的关系

SDK 的文档要求「固定在 tag 上，不要跟随 main」。这里的做法是：

1. `sdk.lock.json` 固定一个已发布的 SDK 版本与哈希；
2. `tools/sync-sdk.mjs --check` 对比 FaceGaze Bridge 的 `latest` Release；
3. 模块仓库的 `sdk-sync.yml` 定期跑它，发现新版就改 lock 并开 PR，由该 PR 的 CI 决定是否可用。

所以 SDK 更新会自动传导到每个模块，但不会让 19 个仓库同时静默变红。

## 版本约定

- Kit 用 `vX.Y.Z` tag；模块通过 `kit.lock.json` 固定 tag 与归档哈希。
- 模块仓库里的 `uses: Aqamoe/FGB-Module-Kit/.github/workflows/*.yml@vX.Y.Z` **与 `kit.lock.json` 指向同一个 tag**，
  这样「CI 用的规则」和「编译用的代码」永远同版本；升级 Kit 时两处一起改（由 sync-kit 脚本开 PR）。
- 破坏性变更（改 `ShapeTable` 语义、改脚手架接口）必须升 major，并同步所有模块仓库。
- `shape_names.hpp` 跟随 SDK：升级 SDK 后运行
  `node tools/gen_shape_names.mjs --sdk=<SDK 根目录>`，CI 用 `--check` 挡住漂移。

## 许可

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
