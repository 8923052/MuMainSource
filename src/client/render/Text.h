#pragma once
#include "render/Assets.h"
#include "support/CoreMath.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#define TEXT_COLOR_WHITE 0
#define TEXT_COLOR_BLUE 1
#define TEXT_COLOR_RED 2
#define TEXT_COLOR_YELLOW 3
#define TEXT_COLOR_GREEN 4
#define TEXT_COLOR_DARKRED 5
#define TEXT_COLOR_PURPLE 6
#define TEXT_COLOR_DARKBLUE 7
#define TEXT_COLOR_DARKYELLOW 8
#define TEXT_COLOR_GREEN_BLUE 9
#define TEXT_COLOR_GRAY 10
#define TEXT_COLOR_REDPURPLE 11
#define TEXT_COLOR_VIOLET 12
#define TEXT_COLOR_ORANGE 13
#define RT3_SORT_LEFT 1
#define RT3_SORT_LEFT_CLIP 2
#define RT3_SORT_CENTER 3
#define RT3_SORT_RIGHT 4
#define RT3_WRITE_RIGHT_TO_LEFT 7
#define RT3_WRITE_CENTER 8

// Word-wrap utilities for wide-character UI text.
// Pure function unit — no project dependencies, can be linked from tests.

/**
 * @brief Splits a wide-character string into multiple lines with word-wrap support.
 * @param lpszText      Source string.
 * @param lpszSeparated Destination 2D buffer (wchar_t[iMaxLine][iLineSize]).
 * @param iMaxLine      Max number of rows.
 * @param iLineSize     Size of each row (including \0).
 * @return int          Total lines created (guaranteed not to exceed iMaxLine).
 */
int SeparateTextIntoLines(const wchar_t *lpszText, wchar_t *lpszSeparated, int iMaxLine,
                          int iLineSize);

/**
 * @brief Splits Text into two parts near its midpoint at a space.
 * @param Text       Source string.
 * @param Text1      Receives the first part (up to maxLength, truncated).
 * @param Text2      Receives the second part, or empty if no space was found.
 * @param maxLength  Capacity of each destination buffer.
 */
void CutText(const wchar_t *Text, wchar_t *Text1, wchar_t *Text2, size_t maxLength);

// SceneCommon.h - Shared utilities used by multiple scenes

// Utility functions

// Utility functions
int SeparateTextIntoLines(const wchar_t *lpszText, wchar_t *lpszSeparated, int iMaxLine,
                          int iLineSize);

// Rendering utilities

// Time utilities

class ApplicationKeeper;
class LegacyRenderFacade;
class SessionKeeper;
struct LogicalGeometryAssetLease;

enum class LegacyFontRole : std::uint8_t
{
    Normal,
    Bold,
    Large,
    Fixed,
};

class IUIRenderText
{
  public:
    virtual ~IUIRenderText() = default;
    virtual bool Create() = 0;
    virtual void Release() = 0;
    virtual bool MeasureText(LegacyFontRole role, const wchar_t *text, int &width,
                             int &height) const = 0;
    virtual bool MeasureText(LegacyFontRole role, const wchar_t *text, std::size_t length,
                             int &width, int &height) const = 0;
    virtual bool RenderText(LegacyFontRole role, DWORD textColor, DWORD backgroundColor, int x,
                            int y, const wchar_t *text, int boxWidth, int boxHeight, int sort,
                            LegacyRenderFacade *facade,
                            const LogicalGeometryAssetLease &quadGeometry, OUT SIZE *textSize) = 0;
    virtual bool HasGlyph(LegacyFontRole role, wchar_t character) const noexcept = 0;
    virtual bool PublishGlyphs(LegacyFontRole role, std::span<const wchar_t> characters) = 0;
};

// Application-owned native text rasterizer facade. It has no caller-selected
// font, color, HDC, or pixel-buffer state; session values are passed for each
// operation by SessionRenderText below.
class CUIRenderText
{
    friend class ApplicationKeeperTestPeer;
    std::unique_ptr<IUIRenderText> m_pRenderText;
    ApplicationKeeper &applicationKeeper_;

  public:
    explicit CUIRenderText(ApplicationKeeper &keeper) noexcept;
    ~CUIRenderText() = default;

    CUIRenderText *operator->() noexcept
    {
        return this;
    }
    const CUIRenderText *operator->() const noexcept
    {
        return this;
    }

    bool Create();
    void Release();
    bool MeasureText(LegacyFontRole role, const wchar_t *text, int &width, int &height) const;
    bool MeasureText(LegacyFontRole role, const wchar_t *text, std::size_t length, int &width,
                     int &height) const;
    bool RenderText(LegacyFontRole role, DWORD textColor, DWORD backgroundColor, int x, int y,
                    const wchar_t *text, int boxWidth = 0, int boxHeight = 0, int sort = 1,
                    LegacyRenderFacade *facade = nullptr,
                    const LogicalGeometryAssetLease *quadGeometry = nullptr,
                    OUT SIZE *textSize = NULL);
    bool HasGlyph(LegacyFontRole role, wchar_t character) const noexcept;
    bool PublishGlyphs(LegacyFontRole role, std::span<const wchar_t> characters);
};

// Exact-session text state. The one native rasterizer above is shared by the
// application owner, while this concrete adapter keeps persistent role/colors
// on its owning session and supplies them explicitly per call.
class SessionRenderText final
{
    struct MissingGlyph final
    {
        LegacyFontRole role;
        wchar_t character;
    };

    SessionKeeper &sessionKeeper_;
    CUIRenderText &rasterizer_;
    LegacyFontRole fontRole_ = LegacyFontRole::Normal;
    DWORD textColor_ = 0xffffffff;
    DWORD backgroundColor_ = 0;
    std::vector<MissingGlyph> missingGlyphs_;

    bool QueueMissingGlyphs(const wchar_t *text);

  public:
    explicit SessionRenderText(SessionKeeper &keeper) noexcept;

    bool Create();
    void Release();
    DWORD GetTextColor() const noexcept
    {
        return textColor_;
    }
    DWORD GetBgColor() const noexcept
    {
        return backgroundColor_;
    }
    void SetTextColor(BYTE red, BYTE green, BYTE blue, BYTE alpha) noexcept;
    void SetTextColor(DWORD color) noexcept
    {
        textColor_ = color;
    }
    void SetBgColor(BYTE red, BYTE green, BYTE blue, BYTE alpha) noexcept;
    void SetBgColor(DWORD color) noexcept
    {
        backgroundColor_ = color;
    }
    void SetFont(LegacyFontRole role) noexcept
    {
        fontRole_ = role;
    }
    bool MeasureText(const wchar_t *text, int &width, int &height) const;
    bool MeasureText(const wchar_t *text, std::size_t, OUT SIZE *size) const;
    bool RenderText(int x, int y, const wchar_t *text, int boxWidth = 0, int boxHeight = 0,
                    int sort = 1, OUT SIZE *textSize = NULL);
    bool PublishQueuedGlyphsOnOwner();
};

int DivideString(LPTSTR alpszDst, int nDstRow, int nDstColumn, LPCTSTR lpszSrc);

BOOL CheckErrString(LPTSTR lpszTarget);

int SeparateTextIntoLines(const wchar_t *lpszText, wchar_t *lpszSeparated, int iMaxLine,
                          int iLineSize);
