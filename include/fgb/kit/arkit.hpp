#pragma once
// ARKit 标准系数 → FaceGaze Bridge Unified Expressions 的共享映射层。
//
// 这里只放「机械对应」关系（各来源通用的那部分）。某个厂商特有的启发式
// ——比如用微笑近似 CheekSquint、用 MouthLeft/Right 拆分 CheekPuff——
// 留在各自模块里显式写出来，不塞进公共表，避免一个来源的经验值污染其它来源。

#include "fgb/kit/host.hpp"

#include <algorithm>
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fgb::kit {

// Apple ARKit 的 52 个标准面部系数。各厂商的拼写差异由 ArkitFromName 归一。
enum class Arkit : std::uint8_t {
    BrowDownLeft,
    BrowDownRight,
    BrowInnerUp,
    BrowOuterUpLeft,
    BrowOuterUpRight,
    CheekPuff,
    CheekSquintLeft,
    CheekSquintRight,
    EyeBlinkLeft,
    EyeBlinkRight,
    EyeLookDownLeft,
    EyeLookDownRight,
    EyeLookInLeft,
    EyeLookInRight,
    EyeLookOutLeft,
    EyeLookOutRight,
    EyeLookUpLeft,
    EyeLookUpRight,
    EyeSquintLeft,
    EyeSquintRight,
    EyeWideLeft,
    EyeWideRight,
    JawForward,
    JawLeft,
    JawOpen,
    JawRight,
    MouthClose,
    MouthDimpleLeft,
    MouthDimpleRight,
    MouthFrownLeft,
    MouthFrownRight,
    MouthFunnel,
    MouthLeft,
    MouthLowerDownLeft,
    MouthLowerDownRight,
    MouthPressLeft,
    MouthPressRight,
    MouthPucker,
    MouthRight,
    MouthRollLower,
    MouthRollUpper,
    MouthShrugLower,
    MouthShrugUpper,
    MouthSmileLeft,
    MouthSmileRight,
    MouthStretchLeft,
    MouthStretchRight,
    MouthUpperUpLeft,
    MouthUpperUpRight,
    NoseSneerLeft,
    NoseSneerRight,
    TongueOut,
    Count,
};

inline constexpr std::size_t kArkitCount = static_cast<std::size_t>(Arkit::Count);

inline constexpr std::array<std::string_view, kArkitCount> kArkitNames = {
    "browDownLeft",      "browDownRight",     "browInnerUp",       "browOuterUpLeft",
    "browOuterUpRight",  "cheekPuff",         "cheekSquintLeft",   "cheekSquintRight",
    "eyeBlinkLeft",      "eyeBlinkRight",     "eyeLookDownLeft",   "eyeLookDownRight",
    "eyeLookInLeft",     "eyeLookInRight",    "eyeLookOutLeft",    "eyeLookOutRight",
    "eyeLookUpLeft",     "eyeLookUpRight",    "eyeSquintLeft",     "eyeSquintRight",
    "eyeWideLeft",       "eyeWideRight",      "jawForward",        "jawLeft",
    "jawOpen",           "jawRight",          "mouthClose",        "mouthDimpleLeft",
    "mouthDimpleRight",  "mouthFrownLeft",    "mouthFrownRight",   "mouthFunnel",
    "mouthLeft",         "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft",
    "mouthPressRight",   "mouthPucker",       "mouthRight",        "mouthRollLower",
    "mouthRollUpper",    "mouthShrugLower",   "mouthShrugUpper",   "mouthSmileLeft",
    "mouthSmileRight",   "mouthStretchLeft",  "mouthStretchRight", "mouthUpperUpLeft",
    "mouthUpperUpRight", "noseSneerLeft",     "noseSneerRight",    "tongueOut",
};

using ArkitWeights = std::array<float, kArkitCount>;

inline std::string NormalizeShapeName(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (c == '_' || c == '-' || c == ' ' || c == '\t' || c == '.') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// 归一后的名字 → ARKit 槽位。同时收录几种常见的左右缩写：
//   browDown_L / browDownL / BrowDownLeft / browdownleft 都落到 BrowDownLeft。
inline const std::map<std::string, Arkit>& ArkitAliasTable() {
    static const std::map<std::string, Arkit> table = [] {
        std::map<std::string, Arkit> entries;
        for (std::size_t i = 0; i < kArkitCount; ++i) {
            const Arkit slot = static_cast<Arkit>(i);
            const std::string canonical{kArkitNames[i]};
            entries[NormalizeShapeName(canonical)] = slot;

            const std::string lower = NormalizeShapeName(canonical);
            if (lower.size() > 4 && lower.ends_with("left")) {
                entries[lower.substr(0, lower.size() - 4) + "l"] = slot;
            } else if (lower.size() > 5 && lower.ends_with("right")) {
                entries[lower.substr(0, lower.size() - 5) + "r"] = slot;
            }
        }
        // 一些来源把单侧的 browInnerUp 拆成左右两份，语义相同。
        entries["browinnerupleft"] = Arkit::BrowInnerUp;
        entries["browinnerupright"] = Arkit::BrowInnerUp;
        entries["mouthclosed"] = Arkit::MouthClose;
        return entries;
    }();
    return table;
}

inline std::optional<Arkit> ArkitFromName(std::string_view raw) {
    const auto found = ArkitAliasTable().find(NormalizeShapeName(raw));
    if (found == ArkitAliasTable().end()) {
        return std::nullopt;
    }
    return found->second;
}

// NaN/Inf 一律当 0，越界裁剪到 [0,1]；上游偶发脏值不应该污染整帧。
inline float CleanUnit(float value) {
    if (std::isnan(value) || std::isinf(value)) {
        return 0.0f;
    }
    return Clamp01(value);
}

// 设置一个来源系数；未知名字返回 false，方便模块统计「收到了多少不认识的名字」。
inline bool SetArkitByName(ArkitWeights& weights, std::string_view name, float value) {
    const std::optional<Arkit> slot = ArkitFromName(name);
    if (!slot.has_value()) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(*slot);
    // 同名重复上报时取较大值：多数来源一份系数只出现一次，取 max 只是防御分帧上报。
    weights[index] = std::max(weights[index], CleanUnit(value));
    return true;
}

struct UnifiedRule {
    Arkit source;
    std::string_view target;
};

// ARKit → Unified Expressions 的通用对应关系。
// 多个 ARKit 系数指向同一个目标时由 ShapeTable::Write 取 max，不互相覆盖。
inline constexpr UnifiedRule kUnifiedRules[] = {
    {Arkit::BrowInnerUp, "BrowInnerUpLeft"},
    {Arkit::BrowInnerUp, "BrowInnerUpRight"},
    {Arkit::BrowDownLeft, "BrowLowererLeft"},
    {Arkit::BrowDownLeft, "BrowPinchLeft"},
    {Arkit::BrowDownRight, "BrowLowererRight"},
    {Arkit::BrowDownRight, "BrowPinchRight"},
    {Arkit::BrowOuterUpLeft, "BrowOuterUpLeft"},
    {Arkit::BrowOuterUpRight, "BrowOuterUpRight"},
    {Arkit::CheekPuff, "CheekPuffLeft"},
    {Arkit::CheekPuff, "CheekPuffRight"},
    {Arkit::CheekSquintLeft, "CheekSquintLeft"},
    {Arkit::CheekSquintRight, "CheekSquintRight"},
    {Arkit::EyeSquintLeft, "EyeSquintLeft"},
    {Arkit::EyeSquintRight, "EyeSquintRight"},
    {Arkit::EyeWideLeft, "EyeWideLeft"},
    {Arkit::EyeWideRight, "EyeWideRight"},
    {Arkit::JawForward, "JawForward"},
    {Arkit::JawLeft, "JawLeft"},
    {Arkit::JawRight, "JawRight"},
    {Arkit::JawOpen, "JawOpen"},
    {Arkit::MouthClose, "MouthClosed"},
    {Arkit::MouthDimpleLeft, "MouthDimpleLeft"},
    {Arkit::MouthDimpleRight, "MouthDimpleRight"},
    {Arkit::MouthFrownLeft, "MouthFrownLeft"},
    {Arkit::MouthFrownRight, "MouthFrownRight"},
    {Arkit::MouthFunnel, "LipFunnelUpperLeft"},
    {Arkit::MouthFunnel, "LipFunnelUpperRight"},
    {Arkit::MouthFunnel, "LipFunnelLowerLeft"},
    {Arkit::MouthFunnel, "LipFunnelLowerRight"},
    {Arkit::MouthLeft, "MouthUpperLeft"},
    {Arkit::MouthLeft, "MouthLowerLeft"},
    {Arkit::MouthRight, "MouthUpperRight"},
    {Arkit::MouthRight, "MouthLowerRight"},
    {Arkit::MouthLowerDownLeft, "MouthLowerDownLeft"},
    {Arkit::MouthLowerDownRight, "MouthLowerDownRight"},
    {Arkit::MouthPressLeft, "MouthPressLeft"},
    {Arkit::MouthPressRight, "MouthPressRight"},
    {Arkit::MouthPucker, "LipPuckerUpperLeft"},
    {Arkit::MouthPucker, "LipPuckerUpperRight"},
    {Arkit::MouthPucker, "LipPuckerLowerLeft"},
    {Arkit::MouthPucker, "LipPuckerLowerRight"},
    {Arkit::MouthRollLower, "LipSuckLowerLeft"},
    {Arkit::MouthRollLower, "LipSuckLowerRight"},
    {Arkit::MouthRollUpper, "LipSuckUpperLeft"},
    {Arkit::MouthRollUpper, "LipSuckUpperRight"},
    {Arkit::MouthShrugLower, "MouthRaiserLower"},
    {Arkit::MouthShrugUpper, "MouthRaiserUpper"},
    {Arkit::MouthSmileLeft, "MouthCornerPullLeft"},
    {Arkit::MouthSmileLeft, "MouthCornerSlantLeft"},
    {Arkit::MouthSmileRight, "MouthCornerPullRight"},
    {Arkit::MouthSmileRight, "MouthCornerSlantRight"},
    {Arkit::MouthStretchLeft, "MouthStretchLeft"},
    {Arkit::MouthStretchRight, "MouthStretchRight"},
    {Arkit::MouthUpperUpLeft, "MouthUpperUpLeft"},
    {Arkit::MouthUpperUpRight, "MouthUpperUpRight"},
    {Arkit::NoseSneerLeft, "NoseSneerLeft"},
    {Arkit::NoseSneerRight, "NoseSneerRight"},
    {Arkit::TongueOut, "TongueOut"},
    // MouthRoll* 同时承担「上/下唇内翻」与对应的 Raiser 形状。
    {Arkit::MouthRollLower, "MouthRaiserLower"},
    {Arkit::MouthRollUpper, "MouthRaiserUpper"},
};

// 模块在 initialize 里调用一次，把通用表涉及的目标名登记进 ShapeTable。
inline void DeclareUnifiedTargets(ShapeTable& table) {
    for (const auto& rule : kUnifiedRules) {
        table.Declare(rule.target);
    }
}

// 按通用表写表情形状。返回实际写入的形状数量。
inline std::size_t MapArkitToUnified(const ArkitWeights& weights, const ShapeTable& table,
                                     FgbFrameSlot* slot) {
    std::size_t written = 0;
    for (const auto& rule : kUnifiedRules) {
        const float value = weights[static_cast<std::size_t>(rule.source)];
        if (value <= 0.0f) {
            continue;
        }
        const std::int32_t index = table.IndexOf(rule.target);
        if (index < 0 || static_cast<std::uint32_t>(index) >= slot->shape_count) {
            continue;
        }
        if (slot->shape_valid[index] == 0) {
            ++written;
        }
        table.Write(slot, rule.target, value);
    }
    return written;
}

// 由表情系数推注视方向：正前方 (0,0,-1) 是合法结果，不是「没有数据」。
inline void GazeFromLookWeights(float horizontal, float vertical, float (&direction)[3]) {
    const float length = std::sqrt(horizontal * horizontal + vertical * vertical + 1.0f);
    direction[0] = horizontal / length;
    direction[1] = vertical / length;
    direction[2] = -1.0f / length;
}

struct ArkitEyeOptions {
    // 睁眼度是否把 eyeSquint 也算进去（iFacialMocap / MeowFace 的上游行为）。
    bool include_squint = false;
    // 由表情系数推导的方向置信度；真注视源应该更高，这里默认按「近似」对待。
    float gaze_confidence = 0.6f;
};

// 填双眼的睁眼度与方向，返回这一帧贡献的属性位。
inline std::uint64_t ApplyArkitEyes(const ArkitWeights& weights, FgbFrameSlot* slot,
                                   const ArkitEyeOptions& options = {}) {
    const auto w = [&weights](Arkit slot_name) {
        return weights[static_cast<std::size_t>(slot_name)];
    };

    const float blink_left = w(Arkit::EyeBlinkLeft);
    const float blink_right = w(Arkit::EyeBlinkRight);
    float open_left = 1.0f - blink_left;
    float open_right = 1.0f - blink_right;
    if (options.include_squint) {
        open_left = 1.0f - Clamp01(blink_left + blink_left * w(Arkit::EyeSquintLeft));
        open_right = 1.0f - Clamp01(blink_right + blink_right * w(Arkit::EyeSquintRight));
    }

    // 左眼：向内看是 +x 还是 -x 由各来源约定决定，这里沿用 VRCFT 上游模块的口径。
    const float left_x = w(Arkit::EyeLookInLeft) - w(Arkit::EyeLookOutLeft);
    const float right_x = w(Arkit::EyeLookOutRight) - w(Arkit::EyeLookInRight);
    const float left_y = w(Arkit::EyeLookUpLeft) - w(Arkit::EyeLookDownLeft);
    const float right_y = w(Arkit::EyeLookUpRight) - w(Arkit::EyeLookDownRight);

    slot->left_eye.openness = Clamp01(open_left);
    slot->left_eye.openness_confidence = 1.0f;
    GazeFromLookWeights(left_x, left_y, slot->left_eye.gaze_dir);
    slot->left_eye.gaze_confidence = options.gaze_confidence;

    slot->right_eye.openness = Clamp01(open_right);
    slot->right_eye.openness_confidence = 1.0f;
    GazeFromLookWeights(right_x, right_y, slot->right_eye.gaze_dir);
    slot->right_eye.gaze_confidence = options.gaze_confidence;

    return FGB_ATTR_LEFT_OPENNESS | FGB_ATTR_RIGHT_OPENNESS | FGB_ATTR_LEFT_GAZE |
           FGB_ATTR_RIGHT_GAZE;
}

} // namespace fgb::kit
