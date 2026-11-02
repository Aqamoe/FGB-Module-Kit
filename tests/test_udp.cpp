#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/udp.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>
#include <string>
#include <thread>

using namespace fgb::kit;

namespace {

constexpr std::uint16_t kTestPort = 31789;
constexpr std::uint16_t kBusyPort = 31790;

bool SendLoopback(std::uint16_t port, const char* text, std::string& error) {
    std::string acquire_error;
    if (!WinsockRuntime::Acquire(acquire_error)) {
        error = acquire_error;
        return false;
    }
    const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) {
        error = "创建发送套接字失败";
        WinsockRuntime::Release();
        return false;
    }
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int sent = ::sendto(handle, text, static_cast<int>(std::strlen(text)), 0,
                              reinterpret_cast<sockaddr*>(&target), sizeof(target));
    const int code = ::WSAGetLastError();
    ::closesocket(handle);
    WinsockRuntime::Release();
    if (sent == SOCKET_ERROR) {
        error = "sendto 失败，WSA " + std::to_string(code);
        return false;
    }
    return true;
}

// 等待接收线程把包搬进队列；不要用 sleep 假设时序，用轮询直到超时。
bool WaitFor(UdpEndpoint& endpoint, UdpPacket& out, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (endpoint.Poll(out)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

int main() {
    std::string error;

    UdpEndpoint endpoint;
    CHECK(endpoint.Bind(kTestPort, error, 4));
    if (!endpoint.Bound()) {
        std::printf("跳过 UDP 用例：绑定失败 (%s)\n", error.c_str());
        return 0;
    }
    CHECK_EQ(endpoint.Port(), kTestPort);

    CHECK(SendLoopback(kTestPort, "hello", error));
    UdpPacket packet;
    CHECK(WaitFor(endpoint, packet, std::chrono::seconds(2)));
    if (!packet.data.empty()) {
        const std::string body{packet.data.begin(), packet.data.end()};
        CHECK(body == "hello");
        CHECK(packet.remote_address == "127.0.0.1");
        CHECK(packet.remote_port != 0);
        CHECK(packet.received_ns != 0);
    }
    CHECK(endpoint.Packets() >= 1);
    CHECK(endpoint.Bytes() >= 5);

    // 队列有上限：来源刷屏时丢旧包，不无限涨内存
    for (int i = 0; i < 40; ++i) {
        SendLoopback(kTestPort, "x", error);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(endpoint.Drops() >= 1);
    CHECK(endpoint.Packets() >= 40);

    // PollLatest 一次取走全部积压，只留最新一包
    UdpPacket latest;
    CHECK(endpoint.PollLatest(latest));
    CHECK_EQ(latest.data.size(), static_cast<std::size_t>(1));

    // 同一端口第二个绑定必须失败，并给出可操作的提示
    UdpEndpoint second;
    std::string second_error;
    CHECK(second.Bind(kBusyPort, second_error));
    UdpEndpoint conflict;
    std::string conflict_error;
    CHECK(!conflict.Bind(kBusyPort, conflict_error));
    CHECK(conflict_error.find("端口已被占用") != std::string::npos);

    // 关闭后不再收包，且可以重新绑定
    endpoint.Close();
    CHECK(!endpoint.Bound());
    UdpPacket ignored;
    CHECK(!endpoint.Poll(ignored));
    UdpEndpoint again;
    CHECK(again.Bind(kTestPort, error));

    return CheckSummary("udp");
}
