#pragma once
// FGB 原生模块的宿主交互封装：日志、限频日志、状态上报、形状表。

#include "fgb_module_api.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace fgb::kit {

// 把宿主回调包一层，模块只面对 std::string_view。
class Host {
public:
    explicit Host(const FgbHostServices* services) : services_(services) {}

    void Log(int level, std::string_view message) const {
        if (services_ == nullptr || services_->log == nullptr) {
            return;
        }
        services_->log(services_->ctx, level, ToBuffer(message));
    }

    void Trace(std::string_view message) const { Log(FGB_LOG_TRACE, message); }
    void Debug(std::string_view message) const { Log(FGB_LOG_DEBUG, message); }
    void Info(std::string_view message) const { Log(FGB_LOG_INFO, message); }
    void Warn(std::string_view message) const { Log(FGB_LOG_WARN, message); }
    void Error(std::string_view message) const { Log(FGB_LOG_ERROR, message); }

    // 同一条告警最多每 interval_ns 记一次，避免刷屏。
    void WarnThrottled(std::uint64_t& last_ns, std::uint64_t now_ns, std::uint64_t interval_ns,
                       std::string_view message) const {
        if (last_ns != 0 && now_ns - last_ns < interval_ns) {
            return;
        }
        last_ns = now_ns;
        Warn(message);
    }

    void Status(int state, std::string_view detail) const {
        if (services_ == nullptr || services_->report_status == nullptr) {
            return;
        }
        services_->report_status(services_->ctx, state, ToBuffer(detail));
    }

    void StatusStarting(std::string_view detail) const { Status(FGB_STATUS_STARTING, detail); }
    void StatusActive(std::string_view detail) const { Status(FGB_STATUS_ACTIVE, detail); }
    void StatusError(std::string_view detail) const { Status(FGB_STATUS_ERROR, detail); }
    void StatusStopped(std::string_view detail) const { Status(FGB_STATUS_STOPPED, detail); }

    int32_t ShapeIndex(std::string_view name) const {
        if (services_ == nullptr || services_->shape_index == nullptr) {
            return -1;
        }
        return services_->shape_index(services_->ctx, ToBuffer(name));
    }

    const FgbHostServices* raw() const { return services_; }

private:
    // 宿主接口是 C ABI 的 const char*，这里用一个线程局部缓冲做转换。
    const char* ToBuffer(std::string_view message) const {
        thread_local std::string buffer;
        buffer.assign(message);
        return buffer.c_str();
    }

    const FgbHostServices* services_ = nullptr;
};

// 先 Declare 目标形状名，Build 时一次性向宿主查下标；Write 用 max 合并，
// 这样多个来源（比如 mouthFunnel → 上下左右四个唇形）不会互相覆盖。
class ShapeTable {
public:
    void Declare(std::string_view name) {
        for (const auto& existing : entries_) {
            if (existing.first == name) {
                return;
            }
        }
        entries_.emplace_back(std::string(name), -1);
    }

    bool Build(const Host& host) {
        bool any = false;
        for (auto& entry : entries_) {
            entry.second = host.ShapeIndex(entry.first);
            any = any || entry.second >= 0;
        }
        return any;
    }

    std::size_t ResolvedCount() const {
        std::size_t count = 0;
        for (const auto& entry : entries_) {
            count += entry.second >= 0 ? 1u : 0u;
        }
        return count;
    }

    std::size_t Size() const { return entries_.size(); }

    void Write(FgbFrameSlot* slot, std::string_view name, float value) const {
        const std::int32_t index = IndexOf(name);
        if (index < 0 || slot == nullptr) {
            return;
        }
        if (static_cast<std::uint32_t>(index) >= slot->shape_count ||
            static_cast<std::uint32_t>(index) >= FGB_MAX_SHAPES) {
            return;
        }
        const float clamped = Clamp01(value);
        if (slot->shape_valid[index] == 0) {
            slot->shapes[index] = clamped;
            slot->shape_valid[index] = 1;
            return;
        }
        if (clamped > slot->shapes[index]) {
            slot->shapes[index] = clamped;
        }
    }

    std::int32_t IndexOf(std::string_view name) const {
        for (const auto& entry : entries_) {
            if (entry.first == name) {
                return entry.second;
            }
        }
        return -1;
    }

private:
    static float Clamp01(float value) {
        if (!(value > 0.0f)) {
            return 0.0f;
        }
        return value > 1.0f ? 1.0f : value;
    }

    std::vector<std::pair<std::string, std::int32_t>> entries_;
};

inline float Clamp01(float value) {
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

} // namespace fgb::kit
