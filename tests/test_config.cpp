#include "check.hpp"
#include "fgb/kit/config.hpp"

#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

using namespace fgb::kit;

int main() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            ("fgb-kit-config-" + std::to_string(::_getpid()));
    std::filesystem::create_directories(directory);

    // 文件不存在属于首次运行的正常情况
    {
        ConfigIni missing;
        std::string error;
        CHECK(!missing.Load(std::string{directory.string()}, "config.ini", error));
        CHECK(!missing.Loaded());
        CHECK_EQ(missing.GetInt("port", 12345), 12345);
    }

    {
        std::ofstream stream{directory / "config.ini"};
        stream << "# 注释行\n"
               << "; 另一种注释\n"
               << "[section] 应被忽略\n"
               << "port = 29765\n"
               << "timeout_seconds=12\n"
               << "enable_head = 0\n"
               << "gain = 0.75\n"
               << "label = \"带引号的值\" ; 行尾注释\n"
               << "空值 =\n"
               << "坏行没有等号\n"
               << "unknown_key = 1\n";
    }

    ConfigIni config;
    config.AllowKeys({"port", "timeout_seconds", "enable_head", "gain", "label", "空值"});
    std::string error;
    CHECK(config.Load(std::string{directory.string()}, "config.ini", error));
    CHECK(config.Loaded());
    CHECK_EQ(config.GetInt("port", 0), 29765);
    CHECK_EQ(config.GetInt("timeout_seconds", 0), 12);
    CHECK(!config.GetBool("enable_head", true));
    CHECK_NEAR(config.GetDouble("gain", 0.0), 0.75, 1e-6);
    CHECK(config.Get("label", "") == std::string{"带引号的值"});
    CHECK(config.Get("空值", "fallback") == std::string{"fallback"});
    // 未知键被收集，用户写错键名时日志里能看到
    CHECK_EQ(config.UnknownKeys().size(), static_cast<std::size_t>(1));
    if (!config.UnknownKeys().empty()) {
        CHECK(config.UnknownKeys()[0] == "unknown_key");
    }
    CHECK(error.find("坏行没有等号") != std::string::npos);

    // 非法数值回退到默认值，不抛异常
    {
        std::ofstream stream{directory / "config.ini", std::ios::trunc};
        stream << "port = abc\n"
               << "gain = xyz\n"
               << "flag = maybe\n";
    }
    ConfigIni broken;
    std::string broken_error;
    CHECK(broken.Load(std::string{directory.string()}, "config.ini", broken_error));
    CHECK_EQ(broken.GetInt("port", 12345), 12345);
    CHECK_NEAR(broken.GetDouble("gain", 0.5), 0.5, 1e-9);
    CHECK(broken.GetBool("flag", true));

    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    return CheckSummary("config");
}
