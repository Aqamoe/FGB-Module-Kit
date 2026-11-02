#include "fgb/kit/library.hpp"
#include "fgb/kit/testing/check.hpp"

#include <string>

using fgb::kit::Library;

int main() {
    std::string error;

    Library lib;
    CHECK(lib.Load("kernel32.dll", "", error));
    CHECK(lib.Loaded());
    CHECK(lib.Name() == std::string{"kernel32.dll"});

    using GetTickCount64Fn = std::uint64_t (*)();
    auto get_tick = lib.Function<GetTickCount64Fn>("GetTickCount64");
    CHECK(get_tick != nullptr);
    if (get_tick != nullptr) {
        CHECK(get_tick() > 0);
    }
    // 不存在的导出：记录名字，供模块聚合出一条「厂商运行时版本不符」的提示
    CHECK(lib.Symbol("FgbNoSuchSymbolHere") == nullptr);
    CHECK(lib.LastMissing() == std::string{"FgbNoSuchSymbolHere"});

    Library missing;
    CHECK(!missing.Load("definitely-not-installed-runtime.dll", "C:\does\not\exist", error));
    CHECK(error.find("definitely-not-installed-runtime.dll") != std::string::npos);
    CHECK(error.find("厂商运行时") != std::string::npos);
    CHECK(!missing.Loaded());

    lib.Close();
    CHECK(!lib.Loaded());
    return CheckSummary("library");
}
