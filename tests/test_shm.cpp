#include "fgb/kit/shm.hpp"
#include "fgb/kit/testing/check.hpp"

#include <string>

using fgb::kit::NamedEvent;
using fgb::kit::SharedMemory;

namespace {

constexpr wchar_t kMapName[] = L"Local\FGBKitTestMapping";
constexpr wchar_t kEventName[] = L"Local\FGBKitTestEvent";
constexpr std::size_t kMapBytes = 4096;

} // namespace

int main() {
    std::string error;

    // 写入端（模拟厂商应用）
    HANDLE mapping = ::CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                          static_cast<DWORD>(kMapBytes), kMapName);
    CHECK(mapping != nullptr);
    if (mapping == nullptr) {
        std::printf("跳过共享内存用例：CreateFileMapping 失败 %lu\n", ::GetLastError());
        return 0;
    }
    auto* written = static_cast<std::uint8_t*>(
        ::MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, kMapBytes));
    CHECK(written != nullptr);

    std::uint32_t counter = 7;
    float weight = 0.62f;
    std::memcpy(written, &counter, sizeof(counter));
    std::memcpy(written + 64, &weight, sizeof(weight));

    // 读取端
    SharedMemory reader;
    CHECK(reader.Open(kMapName, 128, error));
    CHECK(reader.Opened());
    CHECK(reader.Size() >= 128);
    CHECK_EQ(reader.Counter(), 7u);

    std::uint32_t read_counter = 0;
    float read_weight = 0.0f;
    CHECK(reader.ReadAt(0, read_counter));
    CHECK(reader.ReadAt(64, read_weight));
    CHECK_EQ(read_counter, 7u);
    CHECK_NEAR(read_weight, 0.62, 1e-6);

    // 越界读必须失败，且不能改动出参
    std::uint64_t tail = 0xA5A5A5A5A5A5A5A5ull;
    CHECK(!reader.ReadAt(kMapBytes - 2, tail));
    CHECK(tail == 0xA5A5A5A5A5A5A5A5ull);

    // 写入端更新后，读取端看到新值（同一映射，无需重开）
    counter = 9;
    std::memcpy(written, &counter, sizeof(counter));
    CHECK_EQ(reader.Counter(), 9u);

    // 不存在的名字：报错要能指向「来源应用没跑起来」
    SharedMemory missing;
    CHECK(!missing.Open(L"Local\FGBKitNoSuchMapping", 64, error));
    CHECK(error.find("打不开共享内存") != std::string::npos);
    CHECK(!missing.Opened());

    // 结构体比映射还大：拒绝，而不是按 sizeof 盲读
    SharedMemory too_small;
    CHECK(!too_small.Open(kMapName, kMapBytes * 4, error));
    CHECK(error.find("来源版本可能不匹配") != std::string::npos);

    // 空名字直接拒绝
    SharedMemory empty_name;
    CHECK(!empty_name.Open(L"", 8, error));

    // 命名事件
    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, kEventName);
    CHECK(event != nullptr);
    NamedEvent waiter;
    CHECK(waiter.Open(kEventName, error));
    CHECK(!waiter.Wait(20));  // 没人 SetEvent → 超时
    ::SetEvent(event);
    CHECK(waiter.Wait(500));
    waiter.Close();
    CHECK(!waiter.IsOpen());

    reader.Close();
    CHECK(!reader.Opened());
    // 关闭后仍可重开（来源应用重启的常见场景）
    CHECK(reader.Open(kMapName, 128, error));

    ::UnmapViewOfFile(written);
    ::CloseHandle(event);
    ::CloseHandle(mapping);
    return CheckSummary("shm");
}
