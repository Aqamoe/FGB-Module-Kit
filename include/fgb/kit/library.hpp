#pragma once
// 厂商运行时的动态加载：模块只在用户机器上已装好厂商软件时才工作，
// 所以我们不链接、不打包厂商 DLL，运行时按名字找、找不到就给可操作的提示。

#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace fgb::kit {

class Library {
public:
    Library() = default;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library() { Close(); }

    // 依次尝试：模块目录（用户可以把厂商 DLL 放在旁边）、系统目录、PATH。
    bool Load(const std::string& filename, const std::string& module_dir, std::string& error) {
        Close();
        const std::string wide_source = module_dir.empty() ? filename : module_dir + "\\" + filename;
        HMODULE handle = ::LoadLibraryExA(wide_source.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (handle == nullptr) {
            handle = ::LoadLibraryA(filename.c_str());
        }
        if (handle == nullptr) {
            error = "找不到 " + filename + "（错误码 " + std::to_string(::GetLastError()) +
                    "）。这个模块依赖厂商运行时，请先安装对应软件；"
                    "也可以把该 DLL 放到模块目录旁边。";
            return false;
        }
        handle_ = handle;
        filename_ = filename;
        return true;
    }

    void Close() {
        if (handle_ != nullptr) {
            ::FreeLibrary(handle_);
            handle_ = nullptr;
            symbols_.clear();
        }
    }

    bool Loaded() const { return handle_ != nullptr; }
    const std::string& Name() const { return filename_; }

    // 按名字取导出符号；找不到返回 nullptr 并记录最后一个错误，便于聚合提示。
    void* Symbol(const std::string& name) {
        if (handle_ == nullptr) {
            last_missing_ = name;
            return nullptr;
        }
        const auto found = symbols_.find(name);
        if (found != symbols_.end()) {
            return found->second;
        }
        void* address = reinterpret_cast<void*>(::GetProcAddress(handle_, name.c_str()));
        if (address == nullptr) {
            last_missing_ = name;
            return nullptr;
        }
        symbols_.emplace(name, address);
        return address;
    }

    template <typename T>
    T Function(const std::string& name) {
        return reinterpret_cast<T>(Symbol(name));
    }

    std::string LastMissing() const { return last_missing_; }

private:
    HMODULE handle_ = nullptr;
    std::string filename_;
    std::unordered_map<std::string, void*> symbols_;
    std::string last_missing_;
};

} // namespace fgb::kit
