#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/module.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <string>

using namespace fgb::kit;

namespace {

struct DemoSource : IModuleSource {
    bool Start(Host& host, const FgbInitParams& params, std::string& error) override {
        tick_hz_ = params.tick_hz;
        dir_ = params.host_version != nullptr ? params.host_version : "";
        index_ = host.ShapeIndex("JawOpen");
        if (index_ < 0) {
            error = "宿主不认识 JawOpen";
            return false;
        }
        host.StatusStarting("等待数据");
        return true;
    }

    bool Read(Host&, FgbFrameSlot& slot, std::uint64_t) override {
        ++reads;
        slot.attributes = FGB_ATTR_EXPRESSION;
        slot.shapes[index_] = 0.5f;
        slot.shape_valid[index_] = 1;
        return true;
    }

    void Stop(Host&) override { stopped = true; }

    std::uint32_t tick_hz_ = 0;
    std::string dir_;
    std::int32_t index_ = -1;
    int reads = 0;
    bool stopped = false;
};

struct FailingSource : IModuleSource {
    bool Start(Host&, const FgbInitParams&, std::string& error) override {
        error = "绑定 UDP 29765 失败：端口已被占用";
        return false;
    }
    bool Read(Host&, FgbFrameSlot&, std::uint64_t) override { return false; }
    void Stop(Host&) override {}
};

struct SilentSource : IModuleSource {
    bool Start(Host&, const FgbInitParams&, std::string&) override { return true; }
    bool Read(Host&, FgbFrameSlot&, std::uint64_t) override { return false; }  // 本帧没数据
    void Stop(Host&) override {}
};

} // namespace

int main() {
    testing::HostMock host;

    // ABI 不匹配必须返回 NULL，宿主据此报「ABI 版本不兼容」
    CHECK(Export<DemoSource>(FGB_MODULE_ABI_V2 + 1, "com.fgbbridge.demo", "Demo",
                             FGB_CAP_EXPRESSION) == nullptr);

    const FgbModule* module = Export<DemoSource>(FGB_MODULE_ABI_V2, "com.fgbbridge.demo", "Demo",
                                                 FGB_CAP_EXPRESSION);
    CHECK(module != nullptr);
    if (module == nullptr) {
        return CheckSummary("module");
    }
    CHECK_EQ(module->abi_version, FGB_MODULE_ABI_V2);
    CHECK(module->id == std::string{"com.fgbbridge.demo"});
    CHECK(module->display_name == std::string{"Demo"});
    CHECK_EQ(module->capabilities, static_cast<std::uint32_t>(FGB_CAP_EXPRESSION));
    CHECK_EQ(module->struct_size, static_cast<std::uint32_t>(sizeof(FgbModule)));

    FgbInitParams params = host.MakeParams(".", 90, "1.0.8");
    void* self = nullptr;
    CHECK_EQ(module->initialize(&self, host.services(), &params), 1);
    CHECK(self != nullptr);
    CHECK(host.HasStatus(FGB_STATUS_STARTING));

    module->update(self);
    module->update(self);
    CHECK_EQ(host.committed().size(), static_cast<std::size_t>(2));
    CHECK_NEAR(host.committed().back().shapes[host.IndexOf("JawOpen")], 0.5, 1e-6);
    CHECK(host.committed().back().attributes == FGB_ATTR_EXPRESSION);
    CHECK(host.committed().back().shape_valid[host.IndexOf("JawLeft")] == 0);

    module->teardown(self);
    module->update(nullptr);  // 空句柄不能崩
    module->teardown(nullptr);

    // 没有新数据的 tick 不提交帧
    testing::HostMock silent_host;
    FgbInitParams silent_params = silent_host.MakeParams();
    const FgbModule* silent = Export<SilentSource>(FGB_MODULE_ABI_V2, "com.fgbbridge.silent",
                                                   "Silent", FGB_CAP_EXPRESSION);
    void* silent_self = nullptr;
    CHECK_EQ(silent->initialize(&silent_self, silent_host.services(), &silent_params), 1);
    silent->update(silent_self);
    CHECK_EQ(silent_host.committed().size(), static_cast<std::size_t>(0));
    silent->teardown(silent_self);

    // 初始化失败：返回 0、不透出句柄、把原因上报到模块卡片
    testing::HostMock failing;
    FgbInitParams fail_params = failing.MakeParams();
    const FgbModule* bad = Export<FailingSource>(FGB_MODULE_ABI_V2, "com.fgbbridge.bad", "Bad",
                                                 FGB_CAP_EXPRESSION);
    void* bad_self = nullptr;
    CHECK_EQ(bad->initialize(&bad_self, failing.services(), &fail_params), 0);
    CHECK(bad_self == nullptr);
    CHECK(failing.HasStatus(FGB_STATUS_ERROR));
    CHECK(failing.LastStatusDetail().find("29765") != std::string::npos);

    // 断流监控：只报一次，恢复后回到活跃
    testing::HostMock stale_host;
    Host logger{stale_host.services()};
    Staleness watcher{10'000'000'000ull, "测试来源"};
    CHECK(!watcher.Check(logger, 0, 1'000'000'000ull));
    CHECK(stale_host.HasStatus(FGB_STATUS_ERROR));
    CHECK(watcher.Check(logger, 2'000'000'000ull, 3'000'000'000ull));
    CHECK(stale_host.HasStatus(FGB_STATUS_ACTIVE));

    CHECK(!watcher.Check(logger, 2'000'000'000ull, 20'000'000'000ull));
    const std::size_t errors_before = stale_host.CountStatuses(FGB_STATUS_ERROR);
    CHECK(!watcher.Check(logger, 2'000'000'000ull, 40'000'000'000ull));
    CHECK_EQ(stale_host.CountStatuses(FGB_STATUS_ERROR), errors_before);

    return CheckSummary("module");
}
