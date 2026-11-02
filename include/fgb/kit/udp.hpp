#pragma once
// Winsock UDP 接收端：后台线程收包，update() 里只取「最新的若干包」。
//
// 线程模型按 ABI 要求：收包线程只做搬运，解析与提交都发生在宿主的
// update() 调用栈里。包队列有上限，来源刷屏时丢旧包而不是涨内存。

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fgb::kit {

inline std::uint64_t NowNs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

// WSAStartup / WSACleanup 的引用计数封装；同一进程内多个模块实例共用一份。
class WinsockRuntime {
public:
    static bool Acquire(std::string& error) {
        std::lock_guard lock{Mutex()};
        if (Refs() == 0) {
            WSADATA data{};
            const int result = ::WSAStartup(MAKEWORD(2, 2), &data);
            if (result != 0) {
                error = "WSAStartup 失败，错误码 " + std::to_string(result);
                return false;
            }
        }
        ++Refs();
        return true;
    }

    static void Release() {
        std::lock_guard lock{Mutex()};
        if (Refs() > 0 && --Refs() == 0) {
            ::WSACleanup();
        }
    }

private:
    static std::mutex& Mutex() {
        static std::mutex mutex;
        return mutex;
    }
    static int& Refs() {
        static int refs = 0;
        return refs;
    }
};

struct UdpPacket {
    std::vector<std::uint8_t> data;
    std::string remote_address;
    std::uint16_t remote_port = 0;
    std::uint64_t received_ns = 0;
};

class UdpEndpoint {
public:
    static constexpr std::size_t kMaxPacketBytes = 65536;

    UdpEndpoint() = default;
    UdpEndpoint(const UdpEndpoint&) = delete;
    UdpEndpoint& operator=(const UdpEndpoint&) = delete;

    ~UdpEndpoint() { Close(); }

    bool Bind(std::uint16_t port, std::string& error, std::size_t queue_limit = 8) {
        if (socket_ != INVALID_SOCKET) {
            return true;
        }
        if (!WinsockRuntime::Acquire(error)) {
            return false;
        }
        acquired_ = true;

        const SOCKET handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == INVALID_SOCKET) {
            error = "socket() 失败，WSA " + std::to_string(::WSAGetLastError());
            Release();
            return false;
        }

        const int receive_buffer = 512 * 1024;
        ::setsockopt(handle, SOL_SOCKET, SO_RCVBUF,
                     reinterpret_cast<const char*>(&receive_buffer), sizeof(receive_buffer));
        const DWORD timeout = 200; // 让接收线程能周期性检查 stop_
        ::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                     sizeof(timeout));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            const int code = ::WSAGetLastError();
            error = "无法绑定 UDP " + std::to_string(port) + "，WSA " + std::to_string(code) +
                    (code == WSAEADDRINUSE ? "（端口已被占用，先关闭其它接收端）" : "");
            ::closesocket(handle);
            Release();
            return false;
        }

        socket_ = handle;
        port_ = port;
        queue_limit_ = queue_limit;
        stop_.store(false);
        reader_ = std::thread{&UdpEndpoint::ReceiveLoop, this};
        return true;
    }

    void Close() {
        stop_.store(true);
        if (reader_.joinable()) {
            reader_.join();
        }
        if (socket_ != INVALID_SOCKET) {
            ::closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        Release();
        {
            std::lock_guard lock{queue_mutex_};
            queue_.clear();
        }
    }

    bool Bound() const { return socket_ != INVALID_SOCKET; }
    std::uint16_t Port() const { return port_; }

    // 取走一个待处理包；没有就返回 false。
    bool Poll(UdpPacket& out) {
        std::lock_guard lock{queue_mutex_};
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    // 只关心最新一包的场景（高频来源）用它，避免队列积压导致延迟越来越大。
    bool PollLatest(UdpPacket& out) {
        std::lock_guard lock{queue_mutex_};
        if (queue_.empty()) {
            return false;
        }
        out = std::move(queue_.back());
        queue_.clear();
        return true;
    }

    bool SendTo(const void* data, std::size_t size, const std::string& address,
                std::uint16_t port, std::string& error) {
        if (socket_ == INVALID_SOCKET) {
            error = "套接字未绑定";
            return false;
        }
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(port);
        if (::inet_pton(AF_INET, address.c_str(), &target.sin_addr) != 1) {
            error = "目标地址非法：" + address;
            return false;
        }
        const int sent =
            ::sendto(socket_, static_cast<const char*>(data), static_cast<int>(size), 0,
                     reinterpret_cast<sockaddr*>(&target), sizeof(target));
        if (sent == SOCKET_ERROR) {
            error = "sendto 失败，WSA " + std::to_string(::WSAGetLastError());
            return false;
        }
        return true;
    }

    std::uint64_t Packets() const { return packets_.load(); }
    std::uint64_t Bytes() const { return bytes_.load(); }
    std::uint64_t Drops() const { return drops_.load(); }
    std::uint64_t LastPacketNs() const { return last_packet_ns_.load(); }

private:
    void Release() {
        if (acquired_) {
            acquired_ = false;
            WinsockRuntime::Release();
        }
    }

    void ReceiveLoop() {
        std::vector<std::uint8_t> buffer(kMaxPacketBytes);
        while (!stop_.load()) {
            sockaddr_in source{};
            int source_length = sizeof(source);
            const int received =
                ::recvfrom(socket_, reinterpret_cast<char*>(buffer.data()),
                           static_cast<int>(buffer.size()), 0,
                           reinterpret_cast<sockaddr*>(&source), &source_length);
            if (received > 0) {
                UdpPacket packet;
                packet.data.assign(buffer.begin(), buffer.begin() + received);
                char ip[INET_ADDRSTRLEN] = {0};
                ::inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip));
                packet.remote_address = ip;
                packet.remote_port = ntohs(source.sin_port);
                packet.received_ns = NowNs();
                last_packet_ns_.store(packet.received_ns);
                packets_.fetch_add(1);
                bytes_.fetch_add(static_cast<std::uint64_t>(received));
                Push(std::move(packet));
                continue;
            }
            if (received == SOCKET_ERROR) {
                const int code = ::WSAGetLastError();
                if (code != WSAETIMEDOUT) {
                    last_error_.store(static_cast<std::uint64_t>(code));
                }
            }
        }
    }

    void Push(UdpPacket&& packet) {
        std::lock_guard lock{queue_mutex_};
        if (queue_.size() >= queue_limit_) {
            queue_.pop_front();
            drops_.fetch_add(1);
        }
        queue_.push_back(std::move(packet));
    }

    SOCKET socket_ = INVALID_SOCKET;
    std::uint16_t port_ = 0;
    std::size_t queue_limit_ = 8;
    bool acquired_ = false;
    std::atomic<bool> stop_{true};
    std::thread reader_;
    std::mutex queue_mutex_;
    std::deque<UdpPacket> queue_;
    std::atomic<std::uint64_t> packets_{0};
    std::atomic<std::uint64_t> bytes_{0};
    std::atomic<std::uint64_t> drops_{0};
    std::atomic<std::uint64_t> last_packet_ns_{0};
    std::atomic<std::uint64_t> last_error_{0};
};

} // namespace fgb::kit
