#pragma once
// 测试用的假宿主：实现 FgbHostServices 的五个回调，把提交帧收集起来。
//
// 形状下标来自 SDK 生成的 kUnifiedExpressionNames，所以「目标名拼错」在测试里
// 会解析成 -1，与真实宿主行为一致。

#include "fgb/kit/shape_names.hpp"
#include "fgb_module_api.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fgb::kit::testing {

class HostMock {
public:
    HostMock() {
        struct_size_limit_ = sizeof(FgbFrameSlot);
        for (std::size_t i = 0; i < kUnifiedExpressionCount; ++i) {
            indices_[std::string{kUnifiedExpressionNames[i]}] = static_cast<std::int32_t>(i);
        }
        std::memset(&services_, 0, sizeof(services_));
        services_.struct_size = sizeof(FgbHostServices);
        services_.abi_version = FGB_MODULE_ABI_V2;
        services_.log = &LogThunk;
        services_.report_status = &StatusThunk;
        services_.acquire_frame_slot = &AcquireThunk;
        services_.commit_frame_slot = &CommitThunk;
        services_.shape_index = &ShapeIndexThunk;
        services_.ctx = this;

        std::memset(&slot_, 0, sizeof(slot_));
        slot_.struct_size = sizeof(FgbFrameSlot);
        slot_.abi_version = FGB_MODULE_ABI_V2;
        slot_.shape_count = FGB_MAX_SHAPES;
    }

    const FgbHostServices* services() const { return &services_; }

    FgbInitParams MakeParams(const std::string& module_dir = ".", std::uint32_t tick_hz = 90,
                             const char* host_version = "1.0.8") {
        std::memset(&params_, 0, sizeof(params_));
        params_.struct_size = sizeof(FgbInitParams);
        params_.abi_version = FGB_MODULE_ABI_V2;
        params_.tick_hz = tick_hz;
        params_.module_dir = module_dir.c_str();
        params_.host_version = host_version;
        module_dir_copy_ = module_dir;
        return params_;
    }

    // 取一帧可写的槽（宿主语义：每次 acquire 前清零）。
    FgbFrameSlot* MutableSlot() {
        std::memset(&slot_, 0, sizeof(slot_));
        slot_.struct_size = struct_size_limit_;
        slot_.abi_version = FGB_MODULE_ABI_V2;
        slot_.shape_count = static_cast<std::uint32_t>(
            shape_count_ == 0 ? FGB_MAX_SHAPES : std::min<std::size_t>(shape_count_, FGB_MAX_SHAPES));
        return &slot_;
    }

    void Commit(const FgbFrameSlot& slot) { committed_.push_back(slot); }

    // 让 struct_size 停在旧宿主的大小，验证模块的兼容读取。
    void LimitStructSize(std::uint32_t bytes) { struct_size_limit_ = bytes; }
    std::uint32_t StructSizeLimit() const { return struct_size_limit_; }
    void LimitShapeCount(std::size_t count) { shape_count_ = count; }

    std::vector<FgbFrameSlot> const& committed() const { return committed_; }
    const std::vector<std::string>& logs() const { return logs_; }
    const std::vector<std::pair<int, std::string>>& statuses() const { return statuses_; }

    float Shape(std::string_view name) const {
        if (committed_.empty()) {
            return -1.0f;
        }
        const auto found = indices_.find(std::string{name});
        if (found == indices_.end() || found->second < 0) {
            return -1.0f;
        }
        const FgbFrameSlot& slot = committed_.back();
        if (static_cast<std::uint32_t>(found->second) >= slot.shape_count ||
            slot.shape_valid[found->second] == 0) {
            return -1.0f;
        }
        return slot.shapes[found->second];
    }

    std::int32_t IndexOf(std::string_view name) const {
        const auto found = indices_.find(std::string{name});
        return found == indices_.end() ? -1 : found->second;
    }

    bool HasStatus(int state) const {
        for (const auto& entry : statuses_) {
            if (entry.first == state) {
                return true;
            }
        }
        return false;
    }

    std::size_t CountStatuses(int state) const {
        std::size_t count = 0;
        for (const auto& entry : statuses_) {
            count += entry.first == state ? 1u : 0u;
        }
        return count;
    }

    std::string LastStatusDetail() const {
        return statuses_.empty() ? std::string{} : statuses_.back().second;
    }

private:
    static void LogThunk(void* ctx, int level, const char* message) {
        static_cast<HostMock*>(ctx)->logs_.push_back(
            "[" + std::to_string(level) + "] " + (message != nullptr ? message : ""));
    }
    static void StatusThunk(void* ctx, int state, const char* detail) {
        static_cast<HostMock*>(ctx)->statuses_.emplace_back(
            state, detail != nullptr ? detail : "");
    }
    static FgbFrameSlot* AcquireThunk(void* ctx) {
        return static_cast<HostMock*>(ctx)->MutableSlot();
    }
    static void CommitThunk(void* ctx, FgbFrameSlot* slot, std::uint64_t capture_ts_ns) {
        if (ctx == nullptr || slot == nullptr) {
            return;
        }
        auto* self = static_cast<HostMock*>(ctx);
        if (capture_ts_ns != 0) {
            slot->capture_ts_ns = capture_ts_ns;
        }
        self->Commit(*slot);
    }
    static std::int32_t ShapeIndexThunk(void* ctx, const char* name) {
        auto* self = static_cast<HostMock*>(ctx);
        if (name == nullptr) {
            return -1;
        }
        const auto found = self->indices_.find(name);
        return found == self->indices_.end() ? -1 : found->second;
    }

    FgbHostServices services_{};
    FgbInitParams params_{};
    FgbFrameSlot slot_{};
    std::string module_dir_copy_;
    std::map<std::string, std::int32_t> indices_;
    std::vector<FgbFrameSlot> committed_;
    std::vector<std::string> logs_;
    std::vector<std::pair<int, std::string>> statuses_;
    std::uint32_t struct_size_limit_ = sizeof(FgbFrameSlot);
    std::size_t shape_count_ = 0;
};

} // namespace fgb::kit::testing
