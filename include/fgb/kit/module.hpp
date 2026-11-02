#pragma once
// FGB 原生模块的生命周期脚手架。
//
// 模块只需要实现 IModuleSource（启动、每 tick 读取、停止）；导出符号与
// acquire/commit 的样板由这里统一负责，避免每个仓库各写一份生命周期。

#include "fgb/kit/host.hpp"
#include "fgb/kit/udp.hpp"
#include "fgb_module_api.h"

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

namespace fgb::kit {

class IModuleSource {
public:
    virtual ~IModuleSource() = default;

    // 绑定端口 / 起线程 / 读配置。失败时把可操作的原因写进 error，宿主会显示在模块卡片上。
    virtual bool Start(Host& host, const FgbInitParams& params, std::string& error) = 0;

    // 在宿主 tick 里解析并填帧。返回 true 表示本帧有效（slot.attributes 已设置），
    // 脚手架会 commit；返回 false 表示这一 tick 没有新数据，不提交。
    virtual bool Read(Host& host, FgbFrameSlot& slot, std::uint64_t now_ns) = 0;

    virtual void Stop(Host& host) = 0;
};

namespace detail {

struct Instance {
    Host host;
    IModuleSource* source = nullptr;
};

template <typename Source>
inline int Initialize(void** out, const FgbHostServices* services, const FgbInitParams* params) {
    if (out == nullptr || services == nullptr || params == nullptr) {
        return 0;
    }
    Host host{services};
    auto* source = new (std::nothrow) Source();
    if (source == nullptr) {
        host.StatusError("内存分配失败");
        return 0;
    }
    auto* instance = new (std::nothrow) Instance{Host{services}, source};
    if (instance == nullptr) {
        host.StatusError("内存分配失败");
        delete source;
        return 0;
    }
    std::string error;
    if (!instance->source->Start(instance->host, *params, error)) {
        instance->host.StatusError(error.empty() ? "初始化失败" : error);
        delete instance->source;
        delete instance;
        return 0;
    }
    *out = instance;
    return 1;
}

inline void Update(void* opaque) {
    auto* instance = static_cast<Instance*>(opaque);
    if (instance == nullptr || instance->source == nullptr) {
        return;
    }
    const FgbHostServices* services = instance->host.raw();
    if (services == nullptr || services->acquire_frame_slot == nullptr ||
        services->commit_frame_slot == nullptr) {
        return;
    }
    FgbFrameSlot* slot = services->acquire_frame_slot(services->ctx);
    if (slot == nullptr) {
        return;
    }
    if (instance->source->Read(instance->host, *slot, NowNs())) {
        services->commit_frame_slot(services->ctx, slot, 0);
    }
}

inline void Teardown(void* opaque) {
    auto* instance = static_cast<Instance*>(opaque);
    if (instance == nullptr) {
        return;
    }
    if (instance->source != nullptr) {
        instance->source->Stop(instance->host);
        delete instance->source;
    }
    delete instance;
}

} // namespace detail

// 每个模块的导出入口。host_abi_version 不匹配时返回 NULL，宿主报「ABI 版本不兼容」。
template <typename Source>
inline const FgbModule* Export(std::uint32_t host_abi_version, std::string_view id,
                               std::string_view display_name, std::uint32_t capabilities) {
    if (host_abi_version != FGB_MODULE_ABI_V2) {
        return nullptr;
    }
    static const std::string id_storage{id};
    static const std::string name_storage{display_name};
    static const FgbModule module{
        sizeof(FgbModule), FGB_MODULE_ABI_V2, id_storage.c_str(), name_storage.c_str(),
        capabilities,     &detail::Initialize<Source>, &detail::Update, &detail::Teardown,
    };
    return &module;
}

// 断流监控：超过 timeout_ns 没收到包就报一次错误，数据恢复后回到活跃。
class Staleness {
public:
    explicit Staleness(std::uint64_t timeout_ns, std::string label)
        : timeout_ns_{timeout_ns}, label_{std::move(label)} {}

    bool Check(Host& host, std::uint64_t last_ns, std::uint64_t now_ns) {
        const bool healthy = last_ns != 0 && now_ns >= last_ns && now_ns - last_ns <= timeout_ns_;
        if (healthy) {
            if (!healthy_) {
                host.StatusActive(label_ + " 数据已恢复");
                reported_ = false;
            }
            healthy_ = true;
            return true;
        }
        healthy_ = false;
        if (reported_) {
            return false;
        }
        reported_ = true;
        if (last_ns == 0) {
            host.StatusError(label_ + " 还没收到任何数据包，检查来源应用是否在同一网段");
        } else {
            const std::uint64_t silent_seconds = (now_ns - last_ns) / 1000000000ull;
            host.StatusError(label_ + " 已 " + std::to_string(silent_seconds) +
                             " 秒没有数据，检查来源应用是否在发送");
        }
        return false;
    }

    bool Healthy() const { return healthy_; }

private:
    std::uint64_t timeout_ns_ = 0;
    std::string label_;
    bool healthy_ = false;
    bool reported_ = false;
};

} // namespace fgb::kit

#define FGB_KIT_EXPORT(SourceType, module_id, module_name, module_caps)                  \
    extern "C" FGB_MODULE_EXPORT const FgbModule* fgb_module_query_v2(                   \
        std::uint32_t host_abi_version) {                                                \
        return fgb::kit::Export<SourceType>(host_abi_version, module_id, module_name,     \
                                            module_caps);                                \
    }
