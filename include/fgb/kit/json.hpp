#pragma once
// 只读的极简 JSON 解析器：够吃上游 App 发来的扁平负载，不做写回。
//
// 上游 JSON 是不可信输入，所以这里有三条硬限制：深度、节点数、字符串长度。
// 任何一条超了就整体失败，模块按「本包无效」处理，而不是崩溃。

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <system_error>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fgb::kit {

enum class JsonKind : std::uint8_t { Null, Bool, Number, String, Array, Object };

class JsonValue {
public:
    JsonKind Kind() const { return kind_; }
    bool IsObject() const { return kind_ == JsonKind::Object; }
    bool IsArray() const { return kind_ == JsonKind::Array; }

    double AsNumber(double fallback = 0.0) const {
        return kind_ == JsonKind::Number ? number_ : fallback;
    }
    // 上游有的字段把布尔写成 0/1（FaceFound:false、hotkey:0），这里都按布尔接受。
    bool AsBool(bool fallback = false) const {
        switch (kind_) {
            case JsonKind::Bool:
                return boolean_;
            case JsonKind::Number:
                return number_ != 0.0;
            default:
                return fallback;
        }
    }
    std::string_view AsText() const {
        return kind_ == JsonKind::String ? std::string_view{text_} : std::string_view{};
    }

    std::size_t Size() const {
        if (kind_ == JsonKind::Array) {
            return items_.size();
        }
        if (kind_ == JsonKind::Object) {
            return members_.size();
        }
        return 0;
    }

    const JsonValue* At(std::size_t index) const {
        if (kind_ != JsonKind::Array || index >= items_.size()) {
            return nullptr;
        }
        return &items_[index];
    }

    const JsonValue* Find(std::string_view name) const {
        for (const auto& member : members_) {
            if (member.first == name) {
                return &member.second;
            }
        }
        return nullptr;
    }

    // 大小写不敏感的对象查值：上游字段名在不同版本里出现过 Timestamp / timestamp 两种写法。
    const JsonValue* FindInsensitive(std::string_view name) const {
        std::string lowered = ToLower(name);
        for (const auto& member : members_) {
            if (ToLower(member.first) == lowered) {
                return &member.second;
            }
        }
        return nullptr;
    }

    double NumberOr(std::string_view name, double fallback) const {
        const JsonValue* value = FindInsensitive(name);
        return value != nullptr ? value->AsNumber(fallback) : fallback;
    }

    std::string_view TextOr(std::string_view name, std::string_view fallback = {}) const {
        const JsonValue* value = FindInsensitive(name);
        return value != nullptr && value->Kind() == JsonKind::String ? value->AsText() : fallback;
    }

    bool BoolOr(std::string_view name, bool fallback) const {
        const JsonValue* value = FindInsensitive(name);
        return value != nullptr ? value->AsBool(fallback) : fallback;
    }

private:
    friend class JsonParser;

    static std::string ToLower(std::string_view text) {
        std::string out{text};
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return out;
    }

    JsonKind kind_ = JsonKind::Null;
    double number_ = 0.0;
    bool boolean_ = false;
    std::string text_;
    std::vector<JsonValue> items_;
    std::vector<std::pair<std::string, JsonValue>> members_;
};

class JsonParser {
public:
    static constexpr std::size_t kMaxDepth = 32;
    static constexpr std::size_t kMaxNodes = 8192;
    static constexpr std::size_t kMaxTextLength = 4096;

    explicit JsonParser(std::string_view text) : text_{text} {}

    std::optional<JsonValue> Parse() {
        SkipSpace();
        JsonValue root{};
        if (!ParseValue(root)) {
            return std::nullopt;
        }
        SkipSpace();
        if (position_ != text_.size()) {
            error_ = "JSON 结尾有多余内容";
            return std::nullopt;
        }
        return root;
    }

    std::string_view Error() const { return error_; }

private:
    struct DepthGuard {
        explicit DepthGuard(std::size_t& depth) : depth_{depth} { ++depth_; }
        ~DepthGuard() { --depth_; }
        std::size_t& depth_;
    };

    bool ParseValue(JsonValue& out) {
        DepthGuard guard{depth_};
        if (depth_ > kMaxDepth || ++nodes_ > kMaxNodes) {
            error_ = "JSON 层级或节点数超限";
            return false;
        }

        if (position_ >= text_.size()) {
            error_ = "JSON 意外结束";
            return false;
        }
        const char c = text_[position_];
        switch (c) {
            case '{': return ParseObject(out);
            case '[': return ParseArray(out);
            case '"': {
                std::string text;
                if (!ParseString(text)) {
                    return false;
                }
                out.kind_ = JsonKind::String;
                out.text_ = std::move(text);
                return true;
            }
            case 't':
            case 'f': return ParseBool(out);
            case 'n': return ParseNull(out);
            default: return ParseNumber(out);
        }
    }

    bool ParseObject(JsonValue& out) {
        out.kind_ = JsonKind::Object;
        ++position_; // {
        SkipSpace();
        if (Peek() == '}') {
            ++position_;
            return true;
        }
        while (true) {
            SkipSpace();
            if (Peek() != '"') {
                error_ = "对象键名必须是字符串";
                return false;
            }
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipSpace();
            if (Peek() != ':') {
                error_ = "键名后缺少冒号";
                return false;
            }
            ++position_;
            SkipSpace();
            JsonValue value{};
            if (!ParseValue(value)) {
                return false;
            }
            out.members_.emplace_back(std::move(key), std::move(value));
            SkipSpace();
            const char next = Peek();
            if (next == ',') {
                ++position_;
                continue;
            }
            if (next == '}') {
                ++position_;
                return true;
            }
            error_ = "对象成员后缺少逗号或右花括号";
            return false;
        }
    }

    bool ParseArray(JsonValue& out) {
        out.kind_ = JsonKind::Array;
        ++position_; // [
        SkipSpace();
        if (Peek() == ']') {
            ++position_;
            return true;
        }
        while (true) {
            SkipSpace();
            JsonValue value{};
            if (!ParseValue(value)) {
                return false;
            }
            out.items_.push_back(std::move(value));
            SkipSpace();
            const char next = Peek();
            if (next == ',') {
                ++position_;
                continue;
            }
            if (next == ']') {
                ++position_;
                return true;
            }
            error_ = "数组元素后缺少逗号或右方括号";
            return false;
        }
    }

    bool ParseString(std::string& out) {
        ++position_; // 开引号
        while (true) {
            if (position_ >= text_.size()) {
                error_ = "字符串未闭合";
                return false;
            }
            const char c = text_[position_++];
            if (c == '"') {
                return true;
            }
            if (out.size() >= kMaxTextLength) {
                error_ = "字符串过长";
                return false;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) {
                error_ = "转义字符未结束";
                return false;
            }
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': return ParseUnicodeEscape(out);
                default:
                    error_ = "未知转义序列";
                    return false;
            }
        }
    }

    // \uXXXX：只处理 BMP，非 ASCII 走 UTF-8 编码；代理对不做合并（上游不会出现）。
    bool ParseUnicodeEscape(std::string& out) {
        std::uint32_t code = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ >= text_.size()) {
                error_ = "\\u 转义长度不足";
                return false;
            }
            const char c = text_[position_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                error_ = "\\u 转义含非法十六进制字符";
                return false;
            }
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        return true;
    }

    bool ParseNumber(JsonValue& out) {
        const std::size_t start = position_;
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++position_;
                continue;
            }
            break;
        }
        if (position_ == start) {
            error_ = "无法识别的 JSON 值";
            return false;
        }
        const std::string_view token = text_.substr(start, position_ - start);
        double value = 0.0;
        const char* begin = token.data();
        const char* end = begin + token.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            error_ = "数字字段非法";
            return false;
        }
        out.kind_ = JsonKind::Number;
        out.number_ = value;
        return true;
    }

    bool ParseBool(JsonValue& out) {
        if (text_.substr(position_, 4) == "true") {
            position_ += 4;
            out.kind_ = JsonKind::Bool;
            out.boolean_ = true;
            return true;
        }
        if (text_.substr(position_, 5) == "false") {
            position_ += 5;
            out.kind_ = JsonKind::Bool;
            out.boolean_ = false;
            return true;
        }
        error_ = "布尔字段非法";
        return false;
    }

    bool ParseNull(JsonValue& out) {
        if (text_.substr(position_, 4) == "null") {
            position_ += 4;
            out.kind_ = JsonKind::Null;
            return true;
        }
        error_ = "null 字段非法";
        return false;
    }

    char Peek() const { return position_ < text_.size() ? text_[position_] : '\0'; }

    void SkipSpace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
                continue;
            }
            break;
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t depth_ = 0;
    std::size_t nodes_ = 0;
    std::string_view error_;
};

inline std::optional<JsonValue> ParseJson(std::string_view text) {
    JsonParser parser{text};
    auto root = parser.Parse();
    if (!root.has_value()) {
        return std::nullopt;
    }
    return root;
}

} // namespace fgb::kit
