#pragma once
// 由 tools/gen_shape_names.mjs 从 SDK 的 FaceGazeShapeNames.cs 生成，不要手改。
// 顺序必须与 FgbFrameSlot::shapes / shape_valid 一致。

#include <array>
#include <cstddef>
#include <string_view>

namespace fgb::kit {

inline constexpr std::size_t kUnifiedExpressionCount = 88;

inline constexpr std::array<std::string_view, 88> kUnifiedExpressionNames = {
    "EyeSquintRight", "EyeSquintLeft", "EyeWideRight", "EyeWideLeft",
    "BrowPinchRight", "BrowPinchLeft", "BrowLowererRight", "BrowLowererLeft",
    "BrowInnerUpRight", "BrowInnerUpLeft", "BrowOuterUpRight", "BrowOuterUpLeft",
    "NasalDilationRight", "NasalDilationLeft", "NasalConstrictRight", "NasalConstrictLeft",
    "CheekSquintRight", "CheekSquintLeft", "CheekPuffRight", "CheekPuffLeft",
    "CheekSuckRight", "CheekSuckLeft", "JawOpen", "JawRight",
    "JawLeft", "JawForward", "JawBackward", "JawClench",
    "JawMandibleRaise", "MouthClosed", "LipSuckUpperRight", "LipSuckUpperLeft",
    "LipSuckLowerRight", "LipSuckLowerLeft", "LipSuckCornerRight", "LipSuckCornerLeft",
    "LipFunnelUpperRight", "LipFunnelUpperLeft", "LipFunnelLowerRight", "LipFunnelLowerLeft",
    "LipPuckerUpperRight", "LipPuckerUpperLeft", "LipPuckerLowerRight", "LipPuckerLowerLeft",
    "MouthUpperUpRight", "MouthUpperUpLeft", "MouthUpperDeepenRight", "MouthUpperDeepenLeft",
    "NoseSneerRight", "NoseSneerLeft", "MouthLowerDownRight", "MouthLowerDownLeft",
    "MouthUpperRight", "MouthUpperLeft", "MouthLowerRight", "MouthLowerLeft",
    "MouthCornerPullRight", "MouthCornerPullLeft", "MouthCornerSlantRight", "MouthCornerSlantLeft",
    "MouthFrownRight", "MouthFrownLeft", "MouthStretchRight", "MouthStretchLeft",
    "MouthDimpleRight", "MouthDimpleLeft", "MouthRaiserUpper", "MouthRaiserLower",
    "MouthPressRight", "MouthPressLeft", "MouthTightenerRight", "MouthTightenerLeft",
    "TongueOut", "TongueUp", "TongueDown", "TongueRight",
    "TongueLeft", "TongueRoll", "TongueBendDown", "TongueCurlUp",
    "TongueSquish", "TongueFlat", "TongueTwistRight", "TongueTwistLeft",
    "SoftPalateClose", "ThroatSwallow", "NeckFlexRight", "NeckFlexLeft",
};

} // namespace fgb::kit
