#pragma once

#include <cstdint>

namespace UI::Modern::MigratedUiRenderLayers
{
// Higher static layers draw later. Dynamic panels draw above normal static
// panels and use their focus order among themselves. Dialog is always modal.
inline constexpr float Dynamic = -1.0F;

inline constexpr float PetHud = Dynamic;
inline constexpr float TopMenu = 4.3F;
inline constexpr float Command = Dynamic;
inline constexpr float Character = Dynamic;
inline constexpr float PetInfo = Dynamic;
inline constexpr float MuHelper = Dynamic;
inline constexpr float PartyHud = Dynamic;
inline constexpr float Chat = 6.1F;
inline constexpr float ChatInput = 6.2F;
inline constexpr float Warp = 8.3F;
// The scene records this overlay before world labels and all panel UI.
inline constexpr float Minimap = 0.0F;
inline constexpr float Option = 10.5F;
inline constexpr float BottomHud = 10.6F;
inline constexpr float Dialog = 10.7F;
inline constexpr float BlockChat = Dynamic;

inline bool IsDynamic(float layer) noexcept
{
    return layer == Dynamic;
}

inline bool IsDialog(float layer) noexcept
{
    return layer == Dialog;
}

inline bool IsBefore(float leftLayer, std::uint64_t leftFocusOrder, float rightLayer,
                     std::uint64_t rightFocusOrder) noexcept
{
    const bool leftDialog = IsDialog(leftLayer);
    const bool rightDialog = IsDialog(rightLayer);
    if (leftDialog != rightDialog)
        return !leftDialog;
    const bool leftDynamic = IsDynamic(leftLayer);
    const bool rightDynamic = IsDynamic(rightLayer);
    if (leftDynamic != rightDynamic)
        return !leftDynamic;
    return leftDynamic ? leftFocusOrder < rightFocusOrder : leftLayer < rightLayer;
}
} // namespace UI::Modern::MigratedUiRenderLayers
