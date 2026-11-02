#include "fgb/kit/osc.hpp"
#include "fgb/kit/testing/check.hpp"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace fgb::kit;

namespace {

void PushU32(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void PushFloat(std::string& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    PushU32(out, bits);
}

// OSC 字符串：内容 + NUL，补齐到 4 字节边界
void PushString(std::string& out, std::string_view text) {
    out.append(text);
    out.push_back('\0');
    while (out.size() % 4 != 0) {
        out.push_back('\0');
    }
}

std::string Message(std::string_view address, std::string_view typetags,
                    const std::vector<float>& floats) {
    std::string out;
    PushString(out, address);
    PushString(out, typetags);
    for (const float value : floats) {
        PushFloat(out, value);
    }
    return out;
}

std::string Bundle(const std::vector<std::string>& elements) {
    std::string out;
    out.append("#bundle\0", 8);
    PushU32(out, 0);
    PushU32(out, 0);  // timetag 的第二个 32 位
    for (const auto& element : elements) {
        PushU32(out, static_cast<std::uint32_t>(element.size()));
        out.append(element);
    }
    return out;
}

} // namespace

int main() {
    // ---- 单消息 ----
    std::string error;
    const std::string single = Message("/jawOpen", ",f", {0.62f});
    const auto one = ParseOsc(single, error);
    CHECK(one.has_value());
    if (one.has_value()) {
        CHECK_EQ(one->size(), static_cast<std::size_t>(1));
        CHECK((*one)[0].address == std::string{"/jawOpen"});
        CHECK((*one)[0].Leaf() == std::string_view{"jawOpen"});
        CHECK_EQ((*one)[0].arguments.size(), static_cast<std::size_t>(1));
        CHECK_NEAR((*one)[0].arguments[0].AsFloat(), 0.62, 1e-6);
    }

    // ---- 多参数与混合类型 ----
    std::string mixed;
    PushString(mixed, "/avatar/parameters/test");
    PushString(mixed, ",ifsTFFN");
    PushU32(mixed, 7);
    PushFloat(mixed, 1.5f);
    PushString(mixed, "hi");
    const auto multi = ParseOsc(mixed, error);
    CHECK(multi.has_value());
    if (multi.has_value()) {
        const auto& args = (*multi)[0].arguments;
        CHECK_EQ(args.size(), static_cast<std::size_t>(7));
        CHECK_EQ(args[0].AsInt(), 7);
        CHECK_NEAR(args[1].AsFloat(), 1.5, 1e-6);
        CHECK(args[2].text == std::string{"hi"});
        CHECK_NEAR(args[3].AsFloat(), 1.0, 1e-6);   // T
        CHECK_NEAR(args[4].AsFloat(), 0.0, 1e-6);   // F
        CHECK_NEAR(args[6].AsFloat(), 0.0, 1e-6);   // N
    }

    // ---- bundle：Valve Steam Link 会把参数拆成 bundle ----
    const std::string packed = Bundle({Message("/a", ",f", {0.25f}), Message("/b", ",ff", {0.5f, 0.75f})});
    const auto bundled = ParseOsc(packed, error);
    CHECK(bundled.has_value());
    if (bundled.has_value()) {
        CHECK_EQ(bundled->size(), static_cast<std::size_t>(2));
        if (bundled->size() == 2) {
            CHECK((*bundled)[0].address == std::string{"/a"});
            CHECK_NEAR((*bundled)[0].arguments[0].AsFloat(), 0.25, 1e-6);
            CHECK((*bundled)[1].address == std::string{"/b"});
            CHECK_NEAR((*bundled)[1].arguments[1].AsFloat(), 0.75, 1e-6);
        }
    }

    // ---- 非法输入必须整包拒绝，不能崩也不能半解 ----
    CHECK(!ParseOsc("", error).has_value());
    CHECK(!ParseOsc("junk", error).has_value());
    CHECK(!ParseOsc(std::string(3, 'x'), error).has_value());

    // 地址不以 / 开头
    std::string bad_address;
    PushString(bad_address, "jawOpen");
    PushString(bad_address, ",f");
    PushFloat(bad_address, 0.1f);
    CHECK(!ParseOsc(bad_address, error).has_value());

    // 声明了一个 float 但数据被截断
    std::string truncated = Message("/jawOpen", ",ff", {0.5f});
    truncated.resize(truncated.size() - 2);
    CHECK(!ParseOsc(truncated, error).has_value());

    // bundle 元素长度越界
    std::string evil;
    evil.append("#bundle\0", 8);
    PushU32(evil, 0);
    PushU32(evil, 0);
    PushU32(evil, 4000);
    evil.append(16, 'x');
    CHECK(!ParseOsc(evil, error).has_value());

    // bundle 无限嵌套
    std::string deep;
    for (int i = 0; i < 4; ++i) {
        deep = Bundle({deep.empty() ? Message("/x", ",f", {0.0f}) : deep});
    }
    CHECK(!ParseOsc(deep, error).has_value());

    // 未支持的类型标记（数组）应当拒绝而不是误解
    std::string array_case = Message("/x", ",af", {1.0f});
    array_case.insert(array_case.size() - 4, std::string(4, '\0'));
    CHECK(!ParseOsc(array_case, error).has_value());

    return CheckSummary("osc");
}
