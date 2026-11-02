#include "check.hpp"
#include "fgb/kit/arkit.hpp"
#include "fgb/kit/testing/host_mock.hpp"

#include <cmath>
#include <string>

using namespace fgb::kit;

namespace {

constexpr std::size_t Slot(Arkit slot) { return static_cast<std::size_t>(slot); }

float& Weight(ArkitWeights& weights, Arkit slot) { return weights[Slot(slot)]; }

} // namespace

int main() {
    // ---- 名字归一：不同厂商的拼写都要落到同一个槽 ----
    CHECK(ArkitFromName("browDownLeft") == Arkit::BrowDownLeft);
    CHECK(ArkitFromName("browDown_L") == Arkit::BrowDownLeft);
    CHECK(ArkitFromName("BrowDownL") == Arkit::BrowDownLeft);
    CHECK(ArkitFromName("browdownl") == Arkit::BrowDownLeft);
    CHECK(ArkitFromName("mouthSmile_R") == Arkit::MouthSmileRight);
    CHECK(ArkitFromName("cheekSquint_L") == Arkit::CheekSquintLeft);
    CHECK(ArkitFromName("eyeBlink_R") == Arkit::EyeBlinkRight);
    CHECK(ArkitFromName("mouthClose") == Arkit::MouthClose);
    CHECK(ArkitFromName("tongueOut") == Arkit::TongueOut);
    // MeowFace 把单侧的 browInnerUp 拆成左右两份
    CHECK(ArkitFromName("browInnerUpLeft") == Arkit::BrowInnerUp);
    CHECK(ArkitFromName("browInnerUpRight") == Arkit::BrowInnerUp);
    // 头姿/热键这类不是表情系数，必须落到「未知」，让模块自己处理
    CHECK(!ArkitFromName("headUp").has_value());
    CHECK(!ArkitFromName("Hotkey").has_value());
    CHECK(!ArkitFromName("").has_value());

    CHECK_EQ(kArkitCount, 52u);

    // ---- 写入清洗：NaN/Inf/越界 ----
    ArkitWeights weights{};
    CHECK(SetArkitByName(weights, "jawOpen", 0.5f));
    CHECK_NEAR(Weight(weights, Arkit::JawOpen), 0.5, 1e-6);
    SetArkitByName(weights, "jawOpen", 2.0f);
    CHECK_NEAR(Weight(weights, Arkit::JawOpen), 1.0, 1e-6);
    SetArkitByName(weights, "jawOpen", -3.0f);
    CHECK_NEAR(Weight(weights, Arkit::JawOpen), 1.0, 1e-6);  // max 合并保留历史值
    SetArkitByName(weights, "eyeBlinkLeft", std::nan(""));
    CHECK_NEAR(Weight(weights, Arkit::EyeBlinkLeft), 0.0, 1e-6);
    CHECK(!SetArkitByName(weights, "bogus", 1.0f));

    // ---- ARKit → Unified Expressions ----
    testing::HostMock host;
    ShapeTable table;
    DeclareUnifiedTargets(table);
    Host logger{host.services()};
    CHECK(table.Build(logger));
    CHECK(table.ResolvedCount() == table.Size());  // 通用表里的名字必须都是真实形状

    ArkitWeights mapped{};
    mapped[Slot(Arkit::BrowInnerUp)] = 0.8f;
    mapped[Slot(Arkit::MouthFunnel)] = 0.4f;
    mapped[Slot(Arkit::MouthSmileLeft)] = 0.6f;
    mapped[Slot(Arkit::MouthLeft)] = 0.3f;
    mapped[Slot(Arkit::MouthRollLower)] = 0.7f;
    mapped[Slot(Arkit::TongueOut)] = 1.0f;

    FgbFrameSlot slot{};
    slot.struct_size = sizeof(FgbFrameSlot);
    slot.shape_count = FGB_MAX_SHAPES;
    MapArkitToUnified(mapped, table, &slot);

    CHECK_NEAR(slot.shapes[host.IndexOf("BrowInnerUpLeft")], 0.8, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("BrowInnerUpRight")], 0.8, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("LipFunnelUpperLeft")], 0.4, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("LipFunnelLowerRight")], 0.4, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerPullLeft")], 0.6, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthCornerSlantLeft")], 0.6, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthUpperLeft")], 0.3, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthLowerLeft")], 0.3, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("LipSuckLowerLeft")], 0.7, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("MouthRaiserLower")], 0.7, 1e-6);
    CHECK_NEAR(slot.shapes[host.IndexOf("TongueOut")], 1.0, 1e-6);
    // 没有来源系数时不能被写成有效
    CHECK(slot.shape_valid[host.IndexOf("JawOpen")] == 0);
    CHECK(slot.shape_valid[host.IndexOf("BrowPinchLeft")] == 0);

    // max 合并：两个来源指向同一目标时取较大值
    ArkitWeights both{};
    both[Slot(Arkit::MouthShrugLower)] = 0.2f;
    both[Slot(Arkit::MouthRollLower)] = 0.65f;
    FgbFrameSlot merged{};
    merged.struct_size = sizeof(FgbFrameSlot);
    merged.shape_count = FGB_MAX_SHAPES;
    MapArkitToUnified(both, table, &merged);
    CHECK_NEAR(merged.shapes[host.IndexOf("MouthRaiserLower")], 0.65, 1e-6);

    // ---- 眼睛 ----
    ArkitWeights eyes{};
    eyes[Slot(Arkit::EyeBlinkLeft)] = 0.2f;
    eyes[Slot(Arkit::EyeBlinkRight)] = 0.0f;
    eyes[Slot(Arkit::EyeSquintLeft)] = 0.5f;
    eyes[Slot(Arkit::EyeLookUpLeft)] = 0.4f;
    eyes[Slot(Arkit::EyeLookOutRight)] = 0.3f;

    FgbFrameSlot eye_slot{};
    eye_slot.struct_size = sizeof(FgbFrameSlot);
    const std::uint64_t attributes = ApplyArkitEyes(eyes, &eye_slot, {});
    CHECK((attributes & FGB_ATTR_LEFT_OPENNESS) != 0);
    CHECK((attributes & FGB_ATTR_RIGHT_GAZE) != 0);
    CHECK_NEAR(eye_slot.left_eye.openness, 0.8, 1e-6);
    CHECK_NEAR(eye_slot.right_eye.openness, 1.0, 1e-6);
    CHECK_NEAR(eye_slot.left_eye.gaze_dir[1], 0.4f / std::sqrt(1.0f + 0.16f), 1e-5);
    CHECK(eye_slot.left_eye.gaze_dir[2] < 0.0f);  // -Z 前
    CHECK_NEAR(eye_slot.right_eye.gaze_dir[0], 0.3f / std::sqrt(1.0f + 0.09f), 1e-5);
    CHECK_NEAR(eye_slot.left_eye.gaze_confidence, 0.6, 1e-6);

    ArkitEyeOptions with_squint{.include_squint = true};
    FgbFrameSlot squint_slot{};
    squint_slot.struct_size = sizeof(FgbFrameSlot);
    ApplyArkitEyes(eyes, &squint_slot, with_squint);
    CHECK_NEAR(squint_slot.left_eye.openness, 1.0f - (0.2f + 0.2f * 0.5f), 1e-6);

    // 正前方是合法值：所有 look 权重为 0 时方向应当是 (0,0,-1)
    ArkitWeights neutral{};
    FgbFrameSlot neutral_slot{};
    neutral_slot.struct_size = sizeof(FgbFrameSlot);
    ApplyArkitEyes(neutral, &neutral_slot, {});
    CHECK_NEAR(neutral_slot.left_eye.gaze_dir[0], 0.0, 1e-6);
    CHECK_NEAR(neutral_slot.left_eye.gaze_dir[2], -1.0, 1e-6);

    // ---- ShapeTable 自身 ----
    ShapeTable scoped;
    scoped.Declare("JawOpen");
    scoped.Declare("JawOpen");  // 重复声明只留一份
    scoped.Declare("NotAShape");
    CHECK_EQ(scoped.Size(), 2u);
    CHECK(scoped.Build(logger));
    CHECK_EQ(scoped.ResolvedCount(), 1u);
    FgbFrameSlot scoped_slot{};
    scoped_slot.struct_size = sizeof(FgbFrameSlot);
    scoped_slot.shape_count = FGB_MAX_SHAPES;
    scoped.Write(&scoped_slot, "JawOpen", 0.25f);
    scoped.Write(&scoped_slot, "JawOpen", 0.9f);   // max 合并
    scoped.Write(&scoped_slot, "NotAShape", 0.5f);  // 未知名字静默跳过
    scoped.Write(&scoped_slot, "JawLeft", 0.5f);    // 未声明的名字不写入
    CHECK_NEAR(scoped_slot.shapes[host.IndexOf("JawOpen")], 0.9, 1e-6);
    CHECK(scoped_slot.shape_valid[host.IndexOf("JawLeft")] == 0);

    return CheckSummary("arkit");
}
