#pragma once
// OSC 1.0 只读解析：单消息与 #bundle，够吃上游面捕/眼动 App 的常见写法。
//
// 输入是不可信网络数据，所以每条规则都带边界：
//   - 4 字节对齐；对齐不上的包直接判非法
//   - 地址、字符串、blob 都有长度上限
//   - bundle 嵌套深度限制，防「一层套一层」把解析器拖死
// 解析失败永远返回 nullopt，不抛异常、不越界。

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fgb::kit {

enum class OscType : std::uint8_t { Nil, True, False, Int, Float, Double, String, Blob, Character };

struct OscArgument {
    OscType type = OscType::Nil;
    double number = 0.0;      // Int / Float / Double 统一成 double；True/False 记 1/0
    std::string text;         // String / Character
    std::vector<std::uint8_t> blob;

    float AsFloat(float fallback = 0.0f) const {
        switch (type) {
            case OscType::Int:
            case OscType::Float:
            case OscType::Double:
                return static_cast<float>(number);
            case OscType::True:
                return 1.0f;
            case OscType::False:
            case OscType::Nil:
                return 0.0f;
            default:
                return fallback;
        }
    }

    int AsInt(int fallback = 0) const {
        return type == OscType::Int || type == OscType::Float || type == OscType::Double
                   ? static_cast<int>(number)
                   : fallback;
    }
};

struct OscMessage {
    std::string address;
    std::vector<OscArgument> arguments;

    // 地址末段（/avatar/parameters/jawOpen -> jawOpen）；面捕模块大多只关心末段。
    std::string_view Leaf() const {
        const std::size_t slash = address.find_last_of('/');
        return slash == std::string_view::npos ? std::string_view{address}
                                               : std::string_view{address}.substr(slash + 1);
    }
};

class OscReader {
public:
    static constexpr std::size_t kMaxMessages = 256;
    static constexpr std::size_t kMaxArguments = 128;
    static constexpr std::size_t kMaxTextBytes = 4096;
    static constexpr std::size_t kMaxBlobBytes = 1u << 20;
    static constexpr std::size_t kMaxBundleDepth = 2;

    // 解析整个 OSC 包：单消息返回 1 条，bundle 返回其中全部消息。
    static std::optional<std::vector<OscMessage>> Parse(const std::uint8_t* data,
                                                        std::size_t size, std::string& error) {
        if (data == nullptr || size < 8) {
            error = "OSC 包过短";
            return std::nullopt;
        }
        OscReader reader{std::string_view{reinterpret_cast<const char*>(data), size}};
        std::vector<OscMessage> messages;
        if (!reader.ReadPacket(messages, 0)) {
            // 内部原因必须回传给调用方，否则「解析失败」和「为什么失败」会脱节。
            error = std::string{reader.Error()};
            return std::nullopt;
        }
        if (messages.empty()) {
            error = "OSC 包里没有任何消息";
            return std::nullopt;
        }
        return messages;
    }

    static std::optional<std::vector<OscMessage>> Parse(std::string_view packet,
                                                        std::string& error) {
        return Parse(reinterpret_cast<const std::uint8_t*>(packet.data()), packet.size(), error);
    }

private:
    // "#bundle" + 结尾 NUL 共 8 字节；用数字 0 而不是转义字面量，避免源码里出现裸 NUL。
    static constexpr char kBundleBytes[8] = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0};
    static constexpr std::string_view kBundleTag{kBundleBytes, 8};

    explicit OscReader(std::string_view data) : data_{data} {}

    std::string_view Error() const { return error_; }

    bool AtBundleTag() const {
        return data_.size() - position_ >= kBundleTag.size() &&
               data_.compare(position_, kBundleTag.size(), kBundleTag) == 0;
    }

    bool ReadString(std::string& out) {
        const std::size_t start = position_;
        while (position_ < data_.size() && data_[position_] != '\0') {
            if (data_[position_] < 0x20 && data_[position_] != '\t') {
                error_ = "OSC 字符串含控制字符";
                return false;
            }
            ++position_;
        }
        if (position_ >= data_.size()) {
            error_ = "OSC 字符串未终止";
            return false;
        }
        const std::size_t length = position_ - start;
        if (length > kMaxTextBytes) {
            error_ = "OSC 字符串过长";
            return false;
        }
        out.assign(data_.substr(start, length));
        ++position_;  // 结尾的 \0
        Align();
        return true;
    }

    bool ReadInt32(std::int32_t& out) {
        if (position_ + 4 > data_.size()) {
            error_ = "OSC 数据被截断（读 Int32）";
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data_.data() + position_);
        out = static_cast<std::int32_t>(
            (static_cast<std::uint32_t>(bytes[0]) << 24) |
            (static_cast<std::uint32_t>(bytes[1]) << 16) |
            (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]));
        position_ += 4;
        return true;
    }

    bool ReadFloat(float& out) {
        std::int32_t bits = 0;
        if (!ReadInt32(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool ReadDouble(double& out) {
        if (position_ + 8 > data_.size()) {
            error_ = "OSC 数据被截断（读 Double）";
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data_.data() + position_);
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits = (bits << 8) | bytes[i];
        }
        std::memcpy(&out, &bits, sizeof(out));
        position_ += 8;
        return true;
    }

    // 跳到下一个 4 字节边界；越界即非法。
    void Align() {
        const std::size_t remainder = position_ % 4;
        if (remainder != 0) {
            position_ += 4 - remainder;
        }
    }

    bool ReadArguments(const std::string& typetags, std::vector<OscArgument>& out) {
        for (const char tag : typetags) {
            if (tag == ',') {
                continue;  // 逗号之后的部分仍是类型标记，不额外处理
            }
            if (out.size() >= kMaxArguments) {
                error_ = "OSC 参数过多";
                return false;
            }
            OscArgument argument;
            switch (tag) {
                case 'i': {
                    std::int32_t value = 0;
                    if (!ReadInt32(value)) {
                        return false;
                    }
                    argument.type = OscType::Int;
                    argument.number = value;
                    break;
                }
                case 'f': {
                    float value = 0.0f;
                    if (!ReadFloat(value)) {
                        return false;
                    }
                    argument.type = OscType::Float;
                    argument.number = value;
                    break;
                }
                case 'd': {
                    double value = 0.0;
                    if (!ReadDouble(value)) {
                        return false;
                    }
                    argument.type = OscType::Double;
                    argument.number = value;
                    break;
                }
                case 's': {
                    std::string value;
                    if (!ReadString(value)) {
                        return false;
                    }
                    argument.type = OscType::String;
                    argument.text = std::move(value);
                    break;
                }
                case 'c': {
                    std::int32_t value = 0;
                    if (!ReadInt32(value)) {
                        return false;
                    }
                    argument.type = OscType::Character;
                    argument.text.assign(1, static_cast<char>(value));
                    break;
                }
                case 'b': {
                    std::int32_t size = 0;
                    if (!ReadInt32(size) || size < 0) {
                        error_ = "OSC blob 长度非法";
                        return false;
                    }
                    if (static_cast<std::size_t>(size) > kMaxBlobBytes) {
                        error_ = "OSC blob 过大";
                        return false;
                    }
                    const std::size_t payload = static_cast<std::size_t>(size);
                    if (position_ + payload > data_.size()) {
                        error_ = "OSC 数据被截断（读 Blob）";
                        return false;
                    }
                    argument.type = OscType::Blob;
                    argument.blob.assign(data_.begin() + static_cast<std::ptrdiff_t>(position_),
                                        data_.begin() +
                                            static_cast<std::ptrdiff_t>(position_ + payload));
                    position_ += payload;
                    Align();
                    break;
                }
                case 'T':
                    argument.type = OscType::True;
                    argument.number = 1.0;
                    break;
                case 'F':
                    argument.type = OscType::False;
                    argument.number = 0.0;
                    break;
                case 'N':
                    argument.type = OscType::Nil;
                    break;
                default:
                    // 'a'（数组）等在 v1.0 里语义不一，上游面捕模块基本不用；
                    // 遇到未知标记就停下，把已解析的部分交回去。
                    error_ = std::string{"OSC 暂不支持的类型标记 "} + tag;
                    return false;
            }
            out.push_back(std::move(argument));
        }
        return true;
    }

    bool ReadMessage(std::vector<OscMessage>& out) {
        if (out.size() >= kMaxMessages) {
            error_ = "OSC 消息过多";
            return false;
        }
        OscMessage message;
        if (!ReadString(message.address)) {
            return false;
        }
        if (message.address.empty() || message.address[0] != '/') {
            error_ = "OSC 地址必须以 / 开头";
            return false;
        }
        std::string typetags;
        if (!ReadString(typetags)) {
            return false;
        }
        if (!ReadArguments(typetags, message.arguments)) {
            return false;
        }
        out.push_back(std::move(message));
        return true;
    }

    bool ReadPacket(std::vector<OscMessage>& out, std::size_t depth) {
        if (depth > kMaxBundleDepth) {
            error_ = "OSC bundle 嵌套过深";
            return false;
        }
        Align();
        if (position_ + 4 > data_.size()) {
            error_ = "OSC 数据被截断";
            return false;
        }
        if (AtBundleTag()) {
            position_ += kBundleTag.size();  // 标记含结尾 NUL，共 8 字节
            std::int64_t timetag = 0;
            if (!ReadInt64(timetag)) {
                return false;
            }
            while (position_ < data_.size()) {
                std::int32_t element_size = 0;
                if (!ReadInt32(element_size) || element_size <= 0) {
                    error_ = "OSC bundle 元素长度非法";
                    return false;
                }
                const std::size_t end = position_ + static_cast<std::size_t>(element_size);
                if (end > data_.size()) {
                    error_ = "OSC bundle 元素越界";
                    return false;
                }
                if (!ReadElement(out, end, depth)) {
                    return false;
                }
                position_ = end;
            }
            return true;
        }
        return ReadMessage(out);
    }

    bool ReadElement(std::vector<OscMessage>& out, std::size_t end, std::size_t depth) {
        if (end - position_ >= kBundleTag.size() && AtBundleTag()) {
            return ReadPacket(out, depth + 1);
        }
        return ReadMessage(out);
    }

    bool ReadInt64(std::int64_t& out) {
        if (position_ + 8 > data_.size()) {
            error_ = "OSC 数据被截断（读时间戳）";
            return false;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(data_.data() + position_);
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits = (bits << 8) | bytes[i];
        }
        out = static_cast<std::int64_t>(bits);
        position_ += 8;
        return true;
    }

    std::string_view data_;
    std::size_t position_ = 0;
    std::string error_;
};

inline std::optional<std::vector<OscMessage>> ParseOsc(std::string_view packet,
                                                       std::string& error) {
    return OscReader::Parse(packet, error);
}

} // namespace fgb::kit
