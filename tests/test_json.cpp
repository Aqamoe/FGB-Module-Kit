#include "fgb/kit/testing/check.hpp"
#include "fgb/kit/json.hpp"

#include <string>

using namespace fgb::kit;

int main() {
    const char* payload =
        R"({"Timestamp":1700000000,"Hotkey":0,"FaceFound":true,
             "Rotation":{"x":-1.5,"y":2.5,"z":0.25},
             "BlendShapes":[{"k":"jawOpen","v":0.52},{"k":"mouthSmileLeft","v":1.0}]})";

    const auto root = ParseJson(payload);
    CHECK(root.has_value());
    if (!root.has_value()) {
        return CheckSummary("json");
    }
    CHECK(root->IsObject());
    CHECK(root->BoolOr("FaceFound", false));
    CHECK(!root->BoolOr("Hotkey", true));
    // 上游不同版本出现过 timestamp / Timestamp 两种写法
    CHECK_NEAR(root->NumberOr("timestamp", 0.0), 1700000000.0, 1e-3);
    CHECK_NEAR(root->NumberOr("Missing", -7.0), -7.0, 1e-9);

    const JsonValue* rotation = root->Find("Rotation");
    CHECK(rotation != nullptr);
    if (rotation != nullptr) {
        CHECK_NEAR(rotation->NumberOr("x", 0.0), -1.5, 1e-6);
        CHECK_NEAR(rotation->NumberOr("z", 0.0), 0.25, 1e-6);
    }

    const JsonValue* shapes = root->Find("BlendShapes");
    CHECK(shapes != nullptr && shapes->IsArray());
    if (shapes != nullptr) {
        CHECK_EQ(shapes->Size(), 2u);
        const JsonValue* first = shapes->At(0);
        CHECK(first != nullptr);
        if (first != nullptr) {
            CHECK(first->Find("k") != nullptr);
            if (first->Find("k") != nullptr) {
                CHECK(first->Find("k")->AsText() == std::string_view{"jawOpen"});
            }
            CHECK_NEAR(first->NumberOr("v", 0.0), 0.52, 1e-6);
            const JsonValue* value = first->Find("v");
            CHECK(value != nullptr);
            if (value != nullptr) {
                CHECK_NEAR(value->AsNumber(), 0.52, 1e-6);
            }
        }
        CHECK(shapes->At(1) != nullptr && shapes->At(1)->Find("v")->AsNumber() > 0.99);
    }

    // 转义与非法输入
    const auto escaped = ParseJson(R"({"k":"a\"b\\c\/d","v":1e2})");
    CHECK(escaped.has_value());
    if (escaped.has_value()) {
        CHECK(escaped->Find("k")->AsText() == std::string_view{"a\"b\\c/d"});
        CHECK_NEAR(escaped->Find("v")->AsNumber(), 100.0, 1e-9);
    }

    CHECK(!ParseJson("").has_value());
    CHECK(!ParseJson("{").has_value());
    CHECK(!ParseJson("{\"a\":}").has_value());
    CHECK(!ParseJson("{\"a\":1,}").has_value());
    CHECK(!ParseJson("[1 2]").has_value());
    CHECK(!ParseJson("{\"a\":\"unterminated}").has_value());
    CHECK(!ParseJson(std::string(40, '[') + std::string(40, ']').c_str()).has_value());
    CHECK(!ParseJson("{\"a\":0x10}").has_value());
    CHECK(ParseJson("[]")->IsArray());
    CHECK(ParseJson("{}")->IsObject());

    return CheckSummary("json");
}
