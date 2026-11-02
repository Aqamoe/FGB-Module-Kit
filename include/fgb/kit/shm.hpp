#pragma once
// 命名共享内存读取端：用于 Virtual Desktop、Bigscreen Beyond 这类
// 「厂商应用写、我们读」的本地数据源。
//
// 三条来自现实的约束：
//   1. 结构体常常没有版本号，只有字节布局 —— 所以映射大小必须校验，读之前
//      确认宿主给的长度够用，不能按 sizeof 盲读。
//   2. 写入方可能正在改中间字段 —— 用 VRCFT 惯例的「计数前缀 + 快照」读法，
//      并在计数变化时重读一次，避免读到半更新帧。
//   3. 名字是不可信输入的一部分：只接受已登记的映射名，越界即失败。

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace fgb::kit {

// 一个只读的文件映射视图。生命周期 = RAII，Close() 可重复调用。
class SharedMemory {
public:
    SharedMemory() = default;
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    ~SharedMemory() { Close(); }

    bool Open(const std::wstring& name, std::size_t min_bytes, std::string& error) {
        Close();
        if (name.empty()) {
            error = "共享内存名为空";
            return false;
        }
        HANDLE handle = ::OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        if (handle == nullptr) {
            error = "打不开共享内存 " + Narrow(name) + "，错误码 " +
                    std::to_string(::GetLastError()) + "（来源应用是否在运行并已启用该功能？）";
            return false;
        }
        void* view = ::MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
        if (view == nullptr) {
            error = "映射视图失败，错误码 " + std::to_string(::GetLastError());
            ::CloseHandle(handle);
            return false;
        }
        std::size_t mapped = 0;
        MEMORY_BASIC_INFORMATION info{};
        if (::VirtualQuery(view, &info, sizeof(info)) != 0) {
            mapped = static_cast<std::size_t>(info.RegionSize);
        }
        if (mapped < min_bytes) {
            error = "共享内存只有 " + std::to_string(mapped) + " 字节，少于结构体所需的 " +
                    std::to_string(min_bytes) + " 字节（来源版本可能不匹配）";
            ::UnmapViewOfFile(view);
            ::CloseHandle(handle);
            return false;
        }
        handle_ = handle;
        view_ = static_cast<const std::uint8_t*>(view);
        size_ = mapped;
        return true;
    }

    void Close() {
        if (view_ != nullptr) {
            ::UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
        size_ = 0;
    }

    bool Opened() const { return view_ != nullptr; }
    std::size_t Size() const { return size_; }
    const std::uint8_t* data() const { return view_; }

    // 拷贝一段到调用方的 POD；越界返回 false，绝不部分写入。
    template <typename T>
    bool ReadAt(std::size_t offset, T& out) const {
        if (view_ == nullptr || offset + sizeof(T) > size_) {
            return false;
        }
        std::memcpy(&out, view_ + offset, sizeof(T));
        return true;
    }

    // VRCFT 惯例：头 4 字节是写入方的自增计数。
    std::uint32_t Counter() const {
        std::uint32_t value = 0;
        ReadAt(0, value);
        return value;
    }

    static std::string Narrow(const std::wstring& text) {
        std::string out;
        out.reserve(text.size());
        for (const wchar_t c : text) {
            out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
        }
        return out;
    }

private:
    HANDLE handle_ = nullptr;
    const std::uint8_t* view_ = nullptr;
    std::size_t size_ = 0;
};

// 命名事件：来源应用写完一帧就 SetEvent，用它代替轮询更省 CPU。
class NamedEvent {
public:
    bool Open(const std::wstring& name, std::string& error) {
        Close();
        handle_ = ::OpenEventW(SYNCHRONIZE, FALSE, name.c_str());
        if (handle_ == nullptr) {
            error = "打不开事件 " + SharedMemory::Narrow(name) + "，错误码 " +
                    std::to_string(::GetLastError());
            return false;
        }
        return true;
    }

    void Close() {
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    bool IsOpen() const { return handle_ != nullptr; }

    // 返回 true 表示等到了（或本来就有信号）；false 表示超时。
    bool Wait(DWORD timeout_ms) const {
        if (handle_ == nullptr) {
            return false;
        }
        return ::WaitForSingleObject(handle_, timeout_ms) == WAIT_OBJECT_0;
    }

private:
    HANDLE handle_ = nullptr;
};

} // namespace fgb::kit
