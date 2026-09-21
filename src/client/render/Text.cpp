// Portable backend for the GDI text pipeline (WinGdi.h, issue #462 Phase 4).
// CUIRenderText draws UI text the Win32 way: TextOut rasterizes white-on-black
// into a 24bpp top-down DIB section, and the engine scans those pixels into a
// GL texture. This file reproduces exactly that contract with stb_truetype:
// CreateFont resolves a system TTF by weight, CreateDIBSection allocates the
// pixel buffer with DIB pitch rules, and TextOut composites antialiased glyph
// coverage between the DC's background and text colors.
#include "render/Text.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/ModelResources.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

#ifndef _WIN32

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION

#include "stb_truetype.h"
#include <unistd.h> // readlink (executable path)

namespace
{
enum class MuGdiKind : unsigned
{
    Font = 0x464F4E54,
    Bitmap = 0x424D5031,
    Dc = 0x44432020
};

struct MuGdiObject
{
    MuGdiKind kind;
};

// A loaded TTF face, shared by every font of the same weight class.
struct MuGdiFace
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool valid = false;
};

// One human-readable line per resolved weight, surfaced to the crash log so
// a "no UI text" report shows what was found (or not) on the user's system.
std::string g_fontDiag;
void RecordFontDiag(const std::string &line)
{
    g_fontDiag += line;
    g_fontDiag += '\n';
}

struct MuGdiFont : MuGdiObject
{
    const MuGdiFace *face = nullptr;
    int cellHeight = 0; // requested pixel height (GDI cell height)
    float scale = 0.0f; // stbtt scale factor for cellHeight
    int ascentPx = 0;   // scaled ascent in pixels
};

struct MuGdiBitmap : MuGdiObject
{
    int width = 0;
    int height = 0; // rows; stored top-down like a negative-height DIB
    int pitch = 0;  // bytes per row, DWORD-aligned like a DIB
    std::vector<BYTE> pixels;
};

struct MuGdiDC : MuGdiObject
{
    MuGdiBitmap *bitmap = nullptr;
    MuGdiFont *font = nullptr;
    COLORREF textColor = RGB(255, 255, 255);
    COLORREF bkColor = RGB(0, 0, 0);
};

template <typename T> T *AsGdi(void *handle, MuGdiKind kind)
{
    auto *obj = static_cast<MuGdiObject *>(handle);
    return (obj && obj->kind == kind) ? static_cast<T *>(obj) : nullptr;
}

// Candidate faces per weight class, tried in order. MU_FONT/MU_FONT_BOLD
// override the lookup for systems whose fonts live elsewhere.

// Ask fontconfig for a font file via fc-match. This is the distro-agnostic
// way to locate a system font (paths differ across Debian/Fedora/Arch/etc.),
// and works without linking libfontconfig. `family` is the requested UI font
// name; empty falls back to the generic "sans-serif". Empty result if
// fc-match is unavailable.
std::string FontconfigMatch(const std::string &family, bool bold)
{
    std::string fam = family.empty() ? std::string("sans-serif") : family;
    // Drop single quotes so the name can't break out of the shell-quoted
    // pattern; real font family names never contain them.
    fam.erase(std::remove(fam.begin(), fam.end(), '\''), fam.end());
    const std::string cmd =
        "fc-match -f '%{file}' '" + fam + (bold ? ":bold" : "") + "' 2>/dev/null";
    FILE *p = ::popen(cmd.c_str(), "r");
    if (!p)
        return {};
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0)
        out.append(buf, n);
    ::pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

// UTF-8 copy of a wide string via the WideCharToMultiByte shim - the same
// conversion the rest of the engine uses, rather than a hand-rolled encoder.
std::string ToUtf8(const wchar_t *ws)
{
    if (!ws)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::vector<char> buf(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, buf.data(), n, nullptr, nullptr);
    return std::string(buf.data()); // stops at the trailing NUL
}

// Directory of the running executable (with a trailing '/'), so bundled fonts
// resolve regardless of the working directory. Read via /proc/self/exe, the
// same way Dotnet/Connection.h locates the client library. Empty on failure.
const std::string &ExeDir()
{
    static const std::string dir = []() -> std::string {
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return {};
        buf[n] = '\0';
        const std::string path(buf);
        const auto slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    }();
    return dir;
}

// Curated fonts shipped in the client's ./fonts directory. Resolving these by
// name lets a chosen font work even when it is not installed system-wide, so
// every option the UI offers is guaranteed present. Names match what the
// options UI lists and what config stores.
std::string BundledFontPath(const std::string &family, bool bold)
{
    for (const auto &e : GetBundledFonts())
        if (family == e.family)
            return ExeDir() + (bold ? e.bold : e.regular);
    return {};
}

// Loads (and caches) the TTF face for a UI font family + weight. The cache is
// a std::map so node pointers stay stable: when the user switches fonts a new
// face loads under a new key, and MuGdiFont objects created earlier keep
// pointing at their still-alive face. `family` empty -> system sans-serif.
const MuGdiFace *LoadFace(const std::string &family, bool bold)
{
    static std::map<std::pair<std::string, bool>, MuGdiFace> s_cache;
    const auto key = std::make_pair(family, bold);
    if (auto it = s_cache.find(key); it != s_cache.end())
    {
        if (it->second.valid)
            return &it->second;
        // Already scanned and failed: bold reuses regular, regular gives up.
        return bold ? LoadFace(family, false) : nullptr;
    }
    MuGdiFace &face = s_cache[key]; // default-inserts the (empty) entry

    // Order: explicit override, then fontconfig for the requested family,
    // then well-known paths as a last resort if fontconfig is missing.
    std::vector<std::string> candidates;
    if (const char *envPath = std::getenv(bold ? "MU_FONT_BOLD" : "MU_FONT"))
        candidates.emplace_back(envPath);
    // Prefer a bundled curated font (./fonts) so a chosen name always works,
    // even without a system install. Falls through to fontconfig if absent.
    if (std::string bundled = BundledFontPath(family, bold); !bundled.empty())
        candidates.push_back(std::move(bundled));
    if (std::string fc = FontconfigMatch(family, bold); !fc.empty())
        candidates.push_back(std::move(fc));
    const char *fallbacks[] = {
        bold ? "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
             : "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        bold ? "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"
             : "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        bold ? "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf" : "/usr/share/fonts/TTF/DejaVuSans.ttf",
        bold ? "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
             : "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        bold ? "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf"
             : "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
    };
    for (const char *f : fallbacks)
        candidates.emplace_back(f);

    for (const std::string &path : candidates)
    {
        FILE *fp = std::fopen(path.c_str(), "rb");
        if (!fp)
            continue;
        std::fseek(fp, 0, SEEK_END);
        const long size = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (size > 0)
        {
            face.data.resize(static_cast<size_t>(size));
            if (std::fread(face.data.data(), 1, face.data.size(), fp) == face.data.size() &&
                stbtt_InitFont(&face.info, face.data.data(),
                               stbtt_GetFontOffsetForIndex(face.data.data(), 0)))
            {
                face.valid = true;
            }
        }
        std::fclose(fp);
        if (face.valid)
        {
            RecordFontDiag((bold ? "bold:    " : "regular: ") + path);
            return &face;
        }
        face.data.clear();
    }

    // No bold face found: fall back to the regular one rather than no text.
    if (bold)
    {
        RecordFontDiag("bold:    not found, using regular");
        return LoadFace(family, false);
    }

    RecordFontDiag("regular: NOT FOUND (fontconfig and known paths failed)");
    std::fprintf(stderr, "[GdiText] No usable TTF found; UI text disabled. "
                         "Install a sans-serif font (e.g. fonts-dejavu-core) "
                         "or set MU_FONT to a .ttf path.\n");
    return nullptr;
}

// Width of `text` in pixels at the font's scale, accumulating advances the
// way TextOut steps the pen so measurement and rendering agree.
int MeasureWidth(const MuGdiFont *font, LPCWSTR text, int len)
{
    float pen = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font->face->info, static_cast<int>(text[i]), &advance, &lsb);
        pen += advance * font->scale;
    }
    return static_cast<int>(std::ceil(pen));
}
} // namespace

// External linkage so the crash-log writer can read it. One line per resolved
// font weight; empty until the first font is created.
std::string MuFontDiagnostics()
{
    return g_fontDiag;
}

HFONT CreateFontW(int cHeight, int /*cWidth*/, int /*cEscapement*/, int /*cOrientation*/,
                  int cWeight, DWORD /*bItalic*/, DWORD /*bUnderline*/, DWORD /*bStrikeOut*/,
                  DWORD /*iCharSet*/, DWORD /*iOutPrecision*/, DWORD /*iClipPrecision*/,
                  DWORD /*iQuality*/, DWORD /*iPitchAndFamily*/, LPCWSTR pszFaceName)
{
    // The requested GDI face name is the font selector's channel: the shared font
    // factory passes the configured UI font name here. "Tahoma" is the built-in
    // default and has no Linux equivalent, so map it to the empty family and let
    // fontconfig pick the system sans-serif (unchanged default look). Any other
    // name is resolved as-is via fontconfig.
    std::string family = ToUtf8(pszFaceName);
    if (family == "Tahoma")
        family.clear();

    const MuGdiFace *face = LoadFace(family, cWeight >= FW_SEMIBOLD);
    if (!face)
        return nullptr;

    auto *font = new MuGdiFont();
    font->kind = MuGdiKind::Font;
    font->face = face;
    font->cellHeight = std::max(1, std::abs(cHeight));
    font->scale = stbtt_ScaleForPixelHeight(&face->info, static_cast<float>(font->cellHeight));

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &lineGap);
    font->ascentPx = static_cast<int>(std::lround(ascent * font->scale));

    return reinterpret_cast<HFONT>(font);
}

HBITMAP CreateDIBSection(HDC /*hdc*/, const BITMAPINFO *pbmi, UINT /*usage*/, void **ppvBits,
                         HANDLE /*hSection*/, DWORD /*offset*/)
{
    if (ppvBits)
        *ppvBits = nullptr;
    if (!pbmi || pbmi->bmiHeader.biBitCount != 24)
        return nullptr;

    const int width = static_cast<int>(pbmi->bmiHeader.biWidth);
    const int height = static_cast<int>(std::abs(pbmi->bmiHeader.biHeight));
    if (width <= 0 || height <= 0)
        return nullptr;

    auto *bitmap = new MuGdiBitmap();
    bitmap->kind = MuGdiKind::Bitmap;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->pitch = ((width * 24 + 31) & ~31) >> 3;
    bitmap->pixels.assign(static_cast<size_t>(bitmap->pitch) * height, 0);

    if (ppvBits)
        *ppvBits = bitmap->pixels.data();
    return reinterpret_cast<HBITMAP>(bitmap);
}

HDC CreateCompatibleDC(HDC /*hdc*/)
{
    auto *dc = new MuGdiDC();
    dc->kind = MuGdiKind::Dc;
    return reinterpret_cast<HDC>(dc);
}

BOOL DeleteDC(HDC hdc)
{
    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    if (!dc)
        return FALSE;
    delete dc;
    return TRUE;
}

namespace
{
BOOL DeleteGdiObject(void *handle)
{
    auto *obj = static_cast<MuGdiObject *>(handle);
    if (!obj)
        return FALSE;
    switch (obj->kind)
    {
    case MuGdiKind::Font:
        delete static_cast<MuGdiFont *>(obj);
        return TRUE;
    case MuGdiKind::Bitmap:
        delete static_cast<MuGdiBitmap *>(obj);
        return TRUE;
    // A DC is not a GDI object; deleting it here would be a caller bug.
    case MuGdiKind::Dc:
        return FALSE;
    }
    return FALSE;
}

HGDIOBJ SelectIntoDC(HDC hdc, void *handle)
{
    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    auto *obj = static_cast<MuGdiObject *>(handle);
    if (!dc || !obj)
        return nullptr;
    switch (obj->kind)
    {
    case MuGdiKind::Font: {
        auto *prev = dc->font;
        dc->font = static_cast<MuGdiFont *>(obj);
        return reinterpret_cast<HGDIOBJ>(prev);
    }
    case MuGdiKind::Bitmap: {
        auto *prev = dc->bitmap;
        dc->bitmap = static_cast<MuGdiBitmap *>(obj);
        return reinterpret_cast<HGDIOBJ>(prev);
    }
    case MuGdiKind::Dc:
        return nullptr;
    }
    return nullptr;
}
} // namespace

BOOL DeleteObject(HFONT hObject)
{
    return DeleteGdiObject(hObject);
}
BOOL DeleteObject(HGDIOBJ hObject)
{
    return DeleteGdiObject(hObject);
}
BOOL DeleteObject(HBITMAP hObject)
{
    return DeleteGdiObject(hObject);
}
BOOL DeleteObject(HBRUSH hObject)
{
    return DeleteGdiObject(hObject);
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h)
{
    return SelectIntoDC(hdc, h);
}
HGDIOBJ SelectObject(HDC hdc, HBITMAP h)
{
    return SelectIntoDC(hdc, h);
}
HGDIOBJ SelectObject(HDC hdc, HFONT h)
{
    return SelectIntoDC(hdc, h);
}

COLORREF SetBkColor(HDC hdc, COLORREF color)
{
    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    if (!dc)
        return 0;
    const COLORREF prev = dc->bkColor;
    dc->bkColor = color;
    return prev;
}

COLORREF SetTextColor(HDC hdc, COLORREF color)
{
    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    if (!dc)
        return 0;
    const COLORREF prev = dc->textColor;
    dc->textColor = color;
    return prev;
}

BOOL GetTextExtentPoint32W(HDC hdc, LPCWSTR lpString, int c, LPSIZE psizl)
{
    if (!psizl || (!lpString && c != 0))
        return FALSE;

    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    const MuGdiFont *font = dc ? dc->font : nullptr;
    const int len = (c >= 0) ? c : static_cast<int>(wcslen(lpString));
    if (!font || !font->face)
    {
        // No font selected yet: a small fixed cell keeps layout code moving.
        psizl->cx = len * 8;
        psizl->cy = 16;
        return TRUE;
    }

    psizl->cx = MeasureWidth(font, lpString, len);
    psizl->cy = font->cellHeight;
    return TRUE;
}

BOOL TextOut(HDC hdc, int x, int y, LPCWSTR lpString, int c)
{
    auto *dc = AsGdi<MuGdiDC>(hdc, MuGdiKind::Dc);
    if (!dc || !dc->bitmap || !dc->font || !dc->font->face || !lpString)
        return FALSE;

    MuGdiBitmap *bmp = dc->bitmap;
    const MuGdiFont *font = dc->font;
    const stbtt_fontinfo *info = &font->face->info;
    const int len = (c >= 0) ? c : static_cast<int>(wcslen(lpString));

    // GDI's default OPAQUE mode paints the text cell with the background color
    // first; the engine scans exactly this rectangle out of the buffer.
    const int cellWidth = MeasureWidth(font, lpString, len);
    const BYTE bk[3] = {GetBValue(dc->bkColor), GetGValue(dc->bkColor), GetRValue(dc->bkColor)};
    const BYTE tx[3] = {GetBValue(dc->textColor), GetGValue(dc->textColor),
                        GetRValue(dc->textColor)};

    const int clearX1 = std::min(std::max(x, 0), bmp->width);
    const int clearX2 = std::min(x + cellWidth, bmp->width);
    const int clearY1 = std::min(std::max(y, 0), bmp->height);
    const int clearY2 = std::min(y + font->cellHeight, bmp->height);
    for (int py = clearY1; py < clearY2; ++py)
    {
        BYTE *row = bmp->pixels.data() + static_cast<size_t>(py) * bmp->pitch;
        for (int px = clearX1; px < clearX2; ++px)
            std::memcpy(row + px * 3, bk, 3);
    }

    // Rasterize each glyph and blend its coverage between background and text
    // color. Scratch buffer is reused across glyphs.
    static thread_local std::vector<unsigned char> coverage;
    const int baseline = y + font->ascentPx;
    float pen = static_cast<float>(x);
    for (int i = 0; i < len; ++i)
    {
        const int cp = static_cast<int>(lpString[i]);
        int advance = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(info, cp, &advance, &lsb);

        const float xShift = pen - std::floor(pen);
        int gx1, gy1, gx2, gy2;
        stbtt_GetCodepointBitmapBoxSubpixel(info, cp, font->scale, font->scale, xShift, 0.0f, &gx1,
                                            &gy1, &gx2, &gy2);
        const int gw = gx2 - gx1, gh = gy2 - gy1;
        if (gw > 0 && gh > 0)
        {
            coverage.assign(static_cast<size_t>(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(info, coverage.data(), gw, gh, gw, font->scale,
                                              font->scale, xShift, 0.0f, cp);

            const int originX = static_cast<int>(std::floor(pen)) + gx1;
            const int originY = baseline + gy1;
            for (int gy = 0; gy < gh; ++gy)
            {
                const int py = originY + gy;
                if (py < 0 || py >= bmp->height)
                    continue;
                BYTE *row = bmp->pixels.data() + static_cast<size_t>(py) * bmp->pitch;
                for (int gx = 0; gx < gw; ++gx)
                {
                    const int px = originX + gx;
                    if (px < 0 || px >= bmp->width)
                        continue;
                    const unsigned cov = coverage[static_cast<size_t>(gy) * gw + gx];
                    if (cov == 0)
                        continue;
                    BYTE *p = row + px * 3;
                    for (int ch = 0; ch < 3; ++ch)
                        p[ch] = static_cast<BYTE>((tx[ch] * cov + bk[ch] * (255u - cov)) / 255u);
                }
            }
        }

        // No kerning: GDI TextOut does not kern, and MeasureWidth must step the
        // pen identically so the cleared cell always covers the glyphs.
        pen += advance * font->scale;
    }

    return TRUE;
}

#endif // !_WIN32

namespace
{
// True for codepoints drawn at twice the width of a half-width glyph (CJK
// ideographs, kana, Hangul, full-width Latin/punctuation, ...). A naive
// `c > 255` test misclassifies Cyrillic/Greek/Hebrew/Arabic, which sit above
// U+00FF but render at single width.
bool IsFullWidthCharacter(wchar_t c)
{
    return (c >= 0x1100 && c <= 0x115F)     // Hangul Jamo
           || (c >= 0x2E80 && c <= 0x9FFF)  // CJK radicals / ideographs / kana
           || (c >= 0xA000 && c <= 0xA4CF)  // Yi
           || (c >= 0xAC00 && c <= 0xD7A3)  // Hangul syllables
           || (c >= 0xF900 && c <= 0xFAFF)  // CJK compatibility ideographs
           || (c >= 0xFE30 && c <= 0xFE4F)  // CJK compatibility forms
           || (c >= 0xFF00 && c <= 0xFF60)  // full-width Latin / punctuation
           || (c >= 0xFFE0 && c <= 0xFFE6); // full-width signs
}
} // namespace

int SeparateTextIntoLines(const wchar_t *lpszText, wchar_t *lpszSeparated, int iMaxLine,
                          int iLineSize)
{
    if (!lpszText || !lpszSeparated || iMaxLine <= 0 || iLineSize <= 0)
        return 0;

    int iLine = 0;
    const int iVisualLimit = iLineSize - 1;
    const wchar_t *lpSeek = lpszText;
    const wchar_t *lpLastSpaceInSource = nullptr;
    wchar_t *lpDstInCurrentRow = nullptr;

    int iCurrentWidth = 0;
    wchar_t *lpWrite = lpszSeparated;

    while (*lpSeek && iLine < iMaxLine)
    {
        int iCharWidth = IsFullWidthCharacter(*lpSeek) ? 2 : 1;

        if (*lpSeek == L' ')
        {
            lpLastSpaceInSource = lpSeek;
            lpDstInCurrentRow = lpWrite;
        }

        if (iCurrentWidth + iCharWidth > iVisualLimit)
        {
            // Wrap at the preceding space whenever one was seen on this line.
            // Works for the overflow being half-width or full-width — important
            // for mixed strings like "Hello 你好" where the CJK char triggers
            // the overflow but breaking at the space gives cleaner output.
            if (lpLastSpaceInSource && *lpSeek != L' ')
            {
                *lpDstInCurrentRow = L'\0';
                lpSeek = lpLastSpaceInSource + 1;
            }
            else
            {
                // Forward-progress guard: at the start of a line with no space
                // to wrap at, a character whose width exceeds the visual limit
                // would otherwise loop forever (iLine increments, lpSeek does
                // not). Place the character on its own row anyway — when there
                // is room for any payload — and advance past it so the loop
                // makes progress.
                if (iCurrentWidth == 0)
                {
                    if (iLineSize > 1)
                    {
                        *lpWrite++ = *lpSeek;
                    }
                    ++lpSeek;
                }
                *lpWrite = L'\0';
            }

            iLine++;
            if (iLine >= iMaxLine)
                break;

            lpWrite = lpszSeparated + (iLine * iLineSize);
            iCurrentWidth = 0;
            lpLastSpaceInSource = nullptr;
            lpDstInCurrentRow = nullptr;

            while (*lpSeek == L' ')
                lpSeek++;
            continue;
        }

        *lpWrite++ = *lpSeek++;
        iCurrentWidth += iCharWidth;
    }

    if (iLine < iMaxLine)
    {
        *lpWrite = L'\0';
        return iLine + 1;
    }

    return iMaxLine;
}

void CutText(const wchar_t *Text, wchar_t *Text1, wchar_t *Text2, size_t maxLength)
{
    auto sourceText = std::wstring(Text);
    auto halfLength = sourceText.length() / 2;
    size_t splitOffset = sourceText.find_last_of(L' ', halfLength);

    if (splitOffset == std::wstring::npos)
    {
        splitOffset = sourceText.find_first_of(L' ', halfLength);
    }

    if (splitOffset != std::wstring::npos)
    {
        wcsncpy_s(Text1, maxLength, sourceText.substr(0, splitOffset).c_str(), _TRUNCATE);
        wcsncpy_s(Text2, maxLength, sourceText.substr(splitOffset + 1).c_str(), _TRUNCATE);
    }
    else
    {
        // No spaces found, assign everything to Text1
        wcsncpy_s(Text1, maxLength, sourceText.c_str(), _TRUNCATE);
        Text2[0] = L'\0'; // Empty Text2
    }
}

// Desc: ������ ���� ����.
// producer: Ahn Sang-Kyu

int DivideString(LPTSTR alpszDst, int nDstRow, int nDstColumn, LPCTSTR lpszSrc)
{
    if (NULL == lpszSrc)
        return 0;

    int nSrcLen = ::wcslen(lpszSrc);
    if (0 == nSrcLen)
        return 0;

    int nSrcPos = 0;
    int nDstStart = 0;
    int nDstLen = 1;
    int nLineCount = 0;

    while (TRUE)
    {
        if (0x80 & lpszSrc[nSrcPos])
        {
            ++nSrcPos;
            ++nDstLen;
        }

        if ('/' == lpszSrc[nSrcPos])
        {
            ::wcsncpy(alpszDst + nLineCount * nDstColumn, lpszSrc + nDstStart, nDstLen - 1);
            ++nLineCount;
            nDstStart = nSrcPos + 1;
            nDstLen = 0;
        }
        else if (nDstLen >= nDstColumn)
        {
            nSrcPos -= 2;
            nDstLen -= 2;
            ::wcsncpy(alpszDst + nLineCount * nDstColumn, lpszSrc + nDstStart, nDstLen);
            ++nLineCount;
            nDstStart = nSrcPos + 1;
            nDstLen = 0;
        }
        else if (nSrcPos == nSrcLen - 1)
        {
            ::wcsncpy(alpszDst + nLineCount * nDstColumn, lpszSrc + nDstStart, nDstLen);
            break;
        }
        else if (nDstLen == nDstColumn - 1)
        {
            ::wcsncpy(alpszDst + nLineCount * nDstColumn, lpszSrc + nDstStart, nDstLen);
            ++nLineCount;
            nDstStart = nSrcPos + 1;
            nDstLen = 0;
        }

        if (nDstRow == nLineCount)
            break;

        ++nSrcPos;
        ++nDstLen;
    }

    return nLineCount + 1;
}

BOOL CheckErrString(LPTSTR lpszTarget)
{
    int i = 0;
    int nLen = ::wcslen(lpszTarget);
    while (i < nLen)
    {
        if (0x80 & lpszTarget[i])
        {
            if (i == nLen - 1)
            {
                lpszTarget[i] = 0;
                return FALSE;
            }
            else
                ++i;
        }
        ++i;
    }

    return TRUE;
}

// SceneCommon.cpp - Shared utilities used by multiple scenes
// Extracted from ZzzScene.cpp as part of scene refactoring

// Rendering Functions

void SessionRenderUnit::RenderInfomation3D()
{
    bool Success = false;

    if (((ErrorMessage == MESSAGE_TRADE_CHECK || ErrorMessage == MESSAGE_CHECK) &&
         AskYesOrNo == 1) ||
        ErrorMessage == MESSAGE_USE_STATE || ErrorMessage == MESSAGE_USE_STATE2)
    {
        Success = true;
    }

    if (ErrorMessage == MESSAGE_TRADE_CHECK && AskYesOrNo == 5)
    {
        Success = true;
    }
    if (ErrorMessage == MESSAGE_PERSONALSHOP_WARNING)
    {
        Success = true;
    }

    if (Success)
    {
        const SessionDisplayRect renderRect = sessionKeeper_.Display()->LocalRect();
        glMatrixMode(GL_PROJECTION);
        SaveCameraPerspective();
        glPushMatrix();
        glLoadIdentity();
        glViewport2(0, 0, renderRect.width, renderRect.height);
        gluPerspective2(
            1.f, static_cast<float>(renderRect.width) / static_cast<float>(renderRect.height),
            g_Camera.ViewNear, g_Camera.ViewFar);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
        EnableDepthTest();
        EnableDepthMask();

        float Width, Height;
        float x = (REFERENCE_WIDTH - 150) / 2;
        float y;
        if (ErrorMessage == MESSAGE_TRADE_CHECK)
        {
            y = 60 + 55;
        }
        else
        {
            y = 60 + 55;
        }

        Width = 40.f;
        Height = 60.f;
        int iRenderType = ErrorMessage;
        if (AskYesOrNo == 5)
            iRenderType = MESSAGE_USE_STATE;
        switch (iRenderType)
        {
        case MESSAGE_USE_STATE:
        case MESSAGE_USE_STATE2:
        case MESSAGE_PERSONALSHOP_WARNING:
            RenderItem3D(x, y, Width, Height, TargetItem.Type, TargetItem.Level,
                         TargetItem.ExcellentFlags, TargetItem.AncientDiscriminator, true);
            break;

        default:
            RenderItem3D(x, y, Width, Height, PickItem.Type, PickItem.Level,
                         PickItem.ExcellentFlags, PickItem.AncientDiscriminator, true);
            break;
        }

        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        UpdateMousePositionn();
        RestoreCameraPerspective();
    }
}

void SessionLegacyCalls::RenderInfomation3D()
{
    sessionKeeper_.Renderer()->RenderInfomation3D();
}

void SessionRenderUnit::RenderInfomation()
{
    sessionKeeper_.Ui()->Notices().Render();

    LegacyUiManager().Render();

    RenderInfomation3D();
}

void SessionLegacyCalls::RenderInfomation()
{
    sessionKeeper_.Renderer()->RenderInfomation();
}

class CUIRenderTextOriginal final : public IUIRenderText, protected ApplicationLegacyCalls
{
    HDC fontDc_ = nullptr;

    struct Glyph final
    {
        int width = 0;
        int height = 0;
        int originX = 0;
        int originY = 0;
        int advance = 0;
        std::vector<std::byte> alpha;
        LogicalRenderAssetRef atlas;
        float leftU = 0.0F;
        float rightU = 0.0F;
        float bottomV = 0.0F;
        float topV = 0.0F;
    };

    struct FontCache final
    {
        HFONT font = nullptr;
        int height = 0;
        int ascent = 0;
        std::size_t unpublishedGlyphs = 0;
        std::unordered_map<wchar_t, Glyph> glyphs;
        Glyph *fallbackGlyph = nullptr;
        std::vector<Glyph *> glyphSlots;
        Glyph **glyphData = nullptr;
        std::size_t glyphCount = 0;
        std::vector<LogicalRenderAssetRef> atlases;
    };

    mutable std::array<FontCache, 4> fontCaches_;

    HFONT FontForRole(LegacyFontRole role) const noexcept;
    FontCache *EnsureFontCache(LegacyFontRole role) const;
    bool RasterizeGlyph(FontCache &cache, wchar_t character, Glyph &glyph) const;
    static bool CopyGlyphAlpha(Glyph &glyph, const BYTE *bitmapPixels, std::size_t pitch) noexcept;
    Glyph *EnsureGlyph(FontCache &cache, wchar_t character) const;
    bool MeasureGlyphs(LegacyFontRole role, const wchar_t *text, std::size_t length, int &width,
                       int &height) const;
    bool PublishMissingGlyphs(FontCache &cache);
    bool RenderBackground(DWORD backgroundColor, float boxX, float boxY, float boxWidth,
                          float boxHeight, int viewportHeight, LegacyRenderFacade &facade) const;
    bool RenderGlyphs(FontCache &cache, DWORD textColor, const wchar_t *text, std::size_t length,
                      float boxX, float boxY, int tab, int clipMove, int renderWidth,
                      int viewportHeight, LegacyRenderFacade &facade,
                      const LogicalGeometryAssetLease &quadGeometry) const;
    bool RenderRecorded(LegacyFontRole role, DWORD textColor, DWORD backgroundColor, int x, int y,
                        const wchar_t *text, int boxWidth, int boxHeight, int sort,
                        LegacyRenderFacade &facade, const LogicalGeometryAssetLease &quadGeometry,
                        OUT SIZE *textSize);

  public:
    explicit CUIRenderTextOriginal(ApplicationKeeper &keeper) noexcept;
    ~CUIRenderTextOriginal();

    bool Create() override;
    void Release() override;
    bool MeasureText(LegacyFontRole role, const wchar_t *text, int &width,
                     int &height) const override;
    bool MeasureText(LegacyFontRole role, const wchar_t *text, std::size_t length, int &width,
                     int &height) const override;
    bool RenderText(LegacyFontRole role, DWORD textColor, DWORD backgroundColor, int x, int y,
                    const wchar_t *text, int boxWidth, int boxHeight, int sort,
                    LegacyRenderFacade *facade, const LogicalGeometryAssetLease &quadGeometry,
                    OUT SIZE *textSize) override;
    bool HasGlyph(LegacyFontRole role, wchar_t character) const noexcept override;
    bool PublishGlyphs(LegacyFontRole role, std::span<const wchar_t> characters) override;
};

bool CUIRenderText::Create()
{
    if (m_pRenderText)
    {
        return true;
    }

    m_pRenderText = std::make_unique<CUIRenderTextOriginal>(applicationKeeper_);
    if (!m_pRenderText->Create())
    {
        m_pRenderText.reset();
        return false;
    }
    return true;
}

void CUIRenderText::Release()
{
    m_pRenderText.reset();
}

bool CUIRenderText::MeasureText(LegacyFontRole role, const wchar_t *text, int &width,
                                int &height) const
{
    if (!m_pRenderText)
    {
        width = 0;
        height = 0;
        return false;
    }
    return m_pRenderText->MeasureText(role, text, width, height);
}

bool CUIRenderText::MeasureText(LegacyFontRole role, const wchar_t *text, std::size_t length,
                                int &width, int &height) const
{
    if (!m_pRenderText)
    {
        width = 0;
        height = 0;
        return false;
    }
    return m_pRenderText->MeasureText(role, text, length, width, height);
}

bool CUIRenderText::RenderText(LegacyFontRole role, DWORD textColor, DWORD backgroundColor,
                               int iPos_x, int iPos_y, const wchar_t *pszText,
                               int iBoxWidth /* = 0 */, int iBoxHeight /* = 0 */,
                               int iSort /* = RT3_SORT_LEFT */, LegacyRenderFacade *facade,
                               const LogicalGeometryAssetLease *quadGeometry,
                               OUT SIZE *lpTextSize /* = NULL */)
{
    if (!m_pRenderText || quadGeometry == nullptr)
    {
        if (lpTextSize != nullptr)
        {
            lpTextSize->cx = 0;
            lpTextSize->cy = 0;
        }
        return false;
    }
    return m_pRenderText->RenderText(role, textColor, backgroundColor, iPos_x, iPos_y, pszText,
                                     iBoxWidth, iBoxHeight, iSort, facade, *quadGeometry,
                                     lpTextSize);
}

bool CUIRenderText::HasGlyph(LegacyFontRole role, wchar_t character) const noexcept
{
    return m_pRenderText != nullptr && m_pRenderText->HasGlyph(role, character);
}

bool CUIRenderText::PublishGlyphs(LegacyFontRole role, std::span<const wchar_t> characters)
{
    return m_pRenderText != nullptr && m_pRenderText->PublishGlyphs(role, characters);
}
CUIRenderText::CUIRenderText(ApplicationKeeper &keeper) noexcept : applicationKeeper_(keeper)
{
}

bool SessionRenderText::Create()
{
    return rasterizer_.Create();
}

void SessionRenderText::Release()
{
    // The native rasterizer is application-owned. Session teardown must not
    // release it while another exact session may still be using it.
}

void SessionRenderText::SetTextColor(BYTE red, BYTE green, BYTE blue, BYTE alpha) noexcept
{
    textColor_ = static_cast<DWORD>(red) | (static_cast<DWORD>(green) << 8) |
                 (static_cast<DWORD>(blue) << 16) | (static_cast<DWORD>(alpha) << 24);
}

void SessionRenderText::SetBgColor(BYTE red, BYTE green, BYTE blue, BYTE alpha) noexcept
{
    backgroundColor_ = static_cast<DWORD>(red) | (static_cast<DWORD>(green) << 8) |
                       (static_cast<DWORD>(blue) << 16) | (static_cast<DWORD>(alpha) << 24);
}

bool SessionRenderText::MeasureText(const wchar_t *text, int &width, int &height) const
{
    return rasterizer_.MeasureText(fontRole_, text, width, height);
}

bool SessionRenderText::MeasureText(const wchar_t *text, std::size_t length, OUT SIZE *size) const
{
    if (size == nullptr)
    {
        return false;
    }
    size->cx = 0;
    size->cy = 0;
    if (text == nullptr)
    {
        return false;
    }
    int width = 0;
    int height = 0;
    const bool measured = rasterizer_.MeasureText(fontRole_, text, length, width, height);
    size->cx = width;
    size->cy = height;
    return measured;
}

bool SessionRenderText::RenderText(int x, int y, const wchar_t *text, int boxWidth, int boxHeight,
                                   int sort, OUT SIZE *textSize)
{
    LegacyRenderFacade &sessionFacade = sessionKeeper_.Renderer()->LegacyRender();
    if (!sessionFacade.IsRecording())
    {
        return false;
    }
    if (!QueueMissingGlyphs(text))
    {
        return false;
    }
    const LogicalGeometryAssetLease &quadGeometry = sessionKeeper_.Renderer()->TextQuadGeometry();
    return rasterizer_.RenderText(fontRole_, textColor_, backgroundColor_, x, y, text, boxWidth,
                                  boxHeight, sort, &sessionFacade, &quadGeometry, textSize);
}

bool SessionRenderText::QueueMissingGlyphs(const wchar_t *text)
{
    if (text == nullptr)
    {
        return false;
    }
    try
    {
        for (; *text != L'\0'; ++text)
        {
            if (rasterizer_.HasGlyph(fontRole_, *text))
            {
                continue;
            }
            const auto found =
                std::find_if(missingGlyphs_.begin(), missingGlyphs_.end(),
                             [this, text](const MissingGlyph &missing) {
                                 return missing.role == fontRole_ && missing.character == *text;
                             });
            if (found == missingGlyphs_.end())
            {
                missingGlyphs_.push_back({fontRole_, *text});
            }
        }
    }
    catch (...)
    {
        return false;
    }
    return true;
}

bool SessionRenderText::PublishQueuedGlyphsOnOwner()
{
    std::array<std::vector<wchar_t>, 4> byRole;
    try
    {
        for (const MissingGlyph missing : missingGlyphs_)
        {
            byRole[static_cast<std::size_t>(missing.role)].push_back(missing.character);
        }
    }
    catch (...)
    {
        return false;
    }
    for (std::size_t index = 0; index < byRole.size(); ++index)
    {
        if (!byRole[index].empty() &&
            !rasterizer_.PublishGlyphs(static_cast<LegacyFontRole>(index), byRole[index]))
        {
            return false;
        }
    }
    missingGlyphs_.clear();
    return true;
}

CUIRenderTextOriginal::CUIRenderTextOriginal(ApplicationKeeper &keeper) noexcept
    : ApplicationLegacyCalls(keeper)
{
}
CUIRenderTextOriginal::~CUIRenderTextOriginal()
{
    Release();
}

bool CUIRenderTextOriginal::Create()
{
    if (fontDc_ != nullptr)
    {
        return true;
    }
    fontDc_ = CreateCompatibleDC(applicationKeeper_.PlatformDeviceContext());
    if (fontDc_ == nullptr)
    {
        return false;
    }
    std::vector<wchar_t> common;
    try
    {
        common.reserve(225);
        for (wchar_t character = 32; character <= 255; ++character)
        {
            common.push_back(character);
        }
        common.push_back(L'\x2196');
    }
    catch (...)
    {
        Release();
        return false;
    }
    for (std::size_t index = 0; index < fontCaches_.size(); ++index)
    {
        if (!PublishGlyphs(static_cast<LegacyFontRole>(index), common))
        {
            Release();
            return false;
        }
    }
    return true;
}

void CUIRenderTextOriginal::Release()
{
    for (FontCache &cache : fontCaches_)
    {
        for (LogicalRenderAssetRef atlas : cache.atlases)
        {
            (void)applicationKeeper_.BitmapRegistry().RetireLogicalAsset(atlas);
        }
        cache = {};
    }
    if (fontDc_ != nullptr)
    {
        DeleteDC(fontDc_);
        fontDc_ = nullptr;
    }
}

HFONT CUIRenderTextOriginal::FontForRole(LegacyFontRole role) const noexcept
{
    switch (role)
    {
    case LegacyFontRole::Normal:
        return applicationKeeper_.PlatformUiFont();
    case LegacyFontRole::Bold:
        return applicationKeeper_.PlatformBoldFont();
    case LegacyFontRole::Large:
        return applicationKeeper_.PlatformBigFont();
    case LegacyFontRole::Fixed:
        return applicationKeeper_.PlatformFixedFont();
    }
    return nullptr;
}

CUIRenderTextOriginal::FontCache *CUIRenderTextOriginal::EnsureFontCache(LegacyFontRole role) const
{
    std::size_t index = 0;
    switch (role)
    {
    case LegacyFontRole::Normal:
        index = 0;
        break;
    case LegacyFontRole::Bold:
        index = 1;
        break;
    case LegacyFontRole::Large:
        index = 2;
        break;
    case LegacyFontRole::Fixed:
        index = 3;
        break;
    default:
        return nullptr;
    }
    FontCache &cache = fontCaches_[index];
    const HFONT font = FontForRole(role);
    if (fontDc_ == nullptr || font == nullptr)
    {
        return nullptr;
    }
    if (cache.font != nullptr)
    {
        return cache.font == font ? &cache : nullptr;
    }

    const HGDIOBJ previousFont = SelectObject(fontDc_, font);
    TEXTMETRICW metrics{};
    const bool measured = GetTextMetricsW(fontDc_, &metrics) != FALSE;
    SelectObject(fontDc_, previousFont);
    if (!measured || metrics.tmHeight <= 0)
    {
        return nullptr;
    }
    cache.font = font;
    cache.height = metrics.tmHeight;
    cache.ascent = metrics.tmAscent;
    return &cache;
}

CUIRenderTextOriginal::Glyph *CUIRenderTextOriginal::EnsureGlyph(FontCache &cache,
                                                                 wchar_t character) const
{
    const std::size_t index = static_cast<std::size_t>(character);
    if (index < cache.glyphCount && cache.glyphData[index] != nullptr)
    {
        return cache.glyphData[index];
    }

    Glyph glyph;
    if (!RasterizeGlyph(cache, character, glyph))
    {
        return nullptr;
    }
    try
    {
        if (index >= cache.glyphCount)
        {
            cache.glyphSlots.resize(index + 1U, nullptr);
            cache.glyphData = cache.glyphSlots.data();
            cache.glyphCount = cache.glyphSlots.size();
        }
        const auto [stored, inserted] = cache.glyphs.emplace(character, std::move(glyph));
        Glyph *const result = &stored->second;
        cache.glyphData[index] = result;
        if (inserted && result->width > 0 && result->height > 0)
        {
            ++cache.unpublishedGlyphs;
        }
        if (character == L'?')
        {
            cache.fallbackGlyph = result;
        }
        return result;
    }
    catch (...)
    {
        return nullptr;
    }
}

bool CUIRenderTextOriginal::RasterizeGlyph(FontCache &cache, wchar_t character, Glyph &glyph) const
{
    const HGDIOBJ previousFont = SelectObject(fontDc_, cache.font);
    SIZE extent{};
    if (!GetTextExtentPoint32W(fontDc_, &character, 1, &extent) || extent.cx < 0 || extent.cy < 0)
    {
        SelectObject(fontDc_, previousFont);
        return false;
    }
    glyph.width = extent.cx;
    glyph.height = extent.cy;
    glyph.originY = cache.ascent;
    glyph.advance = extent.cx;
    if (glyph.width == 0 || glyph.height == 0)
    {
        SelectObject(fontDc_, previousFont);
        return true;
    }
    try
    {
        glyph.alpha.resize(static_cast<std::size_t>(glyph.width) * glyph.height);
    }
    catch (...)
    {
        SelectObject(fontDc_, previousFont);
        return false;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = glyph.width;
    bitmapInfo.bmiHeader.biHeight = -glyph.height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 24;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    BYTE *bitmapPixels = nullptr;
    HBITMAP bitmap =
        CreateDIBSection(applicationKeeper_.PlatformDeviceContext(), &bitmapInfo, DIB_RGB_COLORS,
                         reinterpret_cast<void **>(&bitmapPixels), nullptr, 0);
    if (bitmap == nullptr || bitmapPixels == nullptr)
    {
        SelectObject(fontDc_, previousFont);
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        return false;
    }
    const std::size_t pitch = (static_cast<std::size_t>(glyph.width) * 24U + 31U) / 32U * 4U;
    std::memset(bitmapPixels, 0, pitch * glyph.height);
    const HGDIOBJ previousBitmap = SelectObject(fontDc_, bitmap);
    SetBkMode(fontDc_, OPAQUE);
    SetBkColor(fontDc_, RGB(0, 0, 0));
    SetTextColor(fontDc_, RGB(255, 255, 255));
    const bool drawn = TextOutW(fontDc_, 0, 0, &character, 1) != FALSE;
    const bool hasInk = drawn && CopyGlyphAlpha(glyph, bitmapPixels, pitch);
    SelectObject(fontDc_, previousBitmap);
    SelectObject(fontDc_, previousFont);
    DeleteObject(bitmap);
    if (!drawn)
    {
        return false;
    }
    if (!hasInk)
    {
        glyph.width = 0;
        glyph.height = 0;
        glyph.alpha.clear();
    }
    return true;
}

bool CUIRenderTextOriginal::CopyGlyphAlpha(Glyph &glyph, const BYTE *bitmapPixels,
                                           std::size_t pitch) noexcept
{
    bool hasInk = false;
    for (int row = 0; row < glyph.height; ++row)
    {
        const BYTE *source = bitmapPixels + static_cast<std::size_t>(row) * pitch;
        for (int column = 0; column < glyph.width; ++column)
        {
            const unsigned int coverage =
                (static_cast<unsigned int>(source[0]) + source[1] + source[2]) / 3U;
            glyph.alpha[static_cast<std::size_t>(row) * glyph.width +
                        static_cast<std::size_t>(column)] =
                std::byte{static_cast<unsigned char>(coverage)};
            hasInk = hasInk || coverage != 0;
            source += 3;
        }
    }
    return hasInk;
}

bool CUIRenderTextOriginal::MeasureGlyphs(LegacyFontRole role, const wchar_t *text,
                                          std::size_t length, int &width, int &height) const
{
    width = 0;
    height = 0;
    const std::size_t roleIndex = static_cast<std::size_t>(role);
    const FontCache *const cache =
        roleIndex < fontCaches_.size() && fontCaches_[roleIndex].font != nullptr
            ? &fontCaches_[roleIndex]
            : nullptr;
    if (cache == nullptr || text == nullptr)
    {
        return false;
    }
    std::int64_t measuredWidth = 0;
    Glyph *const *const glyphs = cache->glyphData;
    const std::size_t glyphCount = cache->glyphCount;
    const Glyph *const fallbackGlyph = cache->fallbackGlyph;
    for (std::size_t index = 0; index < length; ++index)
    {
        const std::size_t glyphIndex = static_cast<std::size_t>(text[index]);
        const Glyph *glyph = glyphIndex < glyphCount ? glyphs[glyphIndex] : nullptr;
        if (glyph == nullptr)
        {
            glyph = fallbackGlyph;
        }
        if (glyph == nullptr)
        {
            return false;
        }
        measuredWidth += glyph->advance;
        if (measuredWidth < 0 || measuredWidth > (std::numeric_limits<int>::max)())
        {
            return false;
        }
    }
    width = static_cast<int>(measuredWidth);
    height = cache->height;
    return true;
}

bool CUIRenderTextOriginal::MeasureText(LegacyFontRole role, const wchar_t *text, int &width,
                                        int &height) const
{
    return MeasureText(role, text, text == nullptr ? 0 : std::wcslen(text), width, height);
}

bool CUIRenderTextOriginal::MeasureText(LegacyFontRole role, const wchar_t *text,
                                        std::size_t length, int &width, int &height) const
{
    return MeasureGlyphs(role, text, length, width, height);
}

bool CUIRenderTextOriginal::RenderText(LegacyFontRole role, DWORD textColor, DWORD backgroundColor,
                                       int x, int y, const wchar_t *text, int boxWidth,
                                       int boxHeight, int sort, LegacyRenderFacade *facade,
                                       const LogicalGeometryAssetLease &quadGeometry,
                                       OUT SIZE *textSize)
{
    if (textSize != nullptr)
    {
        *textSize = {};
    }
    return facade != nullptr && facade->IsRecording() &&
           RenderRecorded(role, textColor, backgroundColor, x, y, text, boxWidth, boxHeight, sort,
                          *facade, quadGeometry, textSize);
}

bool CUIRenderTextOriginal::HasGlyph(LegacyFontRole role, wchar_t character) const noexcept
{
    const std::size_t index = static_cast<std::size_t>(role);
    if (index >= fontCaches_.size())
    {
        return false;
    }
    const FontCache &cache = fontCaches_[index];
    const std::size_t glyphIndex = static_cast<std::size_t>(character);
    return cache.font != nullptr && glyphIndex < cache.glyphCount &&
           cache.glyphData[glyphIndex] != nullptr;
}

bool CUIRenderTextOriginal::PublishGlyphs(LegacyFontRole role, std::span<const wchar_t> characters)
{
    FontCache *const cache = EnsureFontCache(role);
    if (cache == nullptr)
    {
        return false;
    }
    for (const wchar_t character : characters)
    {
        if (EnsureGlyph(*cache, character) == nullptr)
        {
            return false;
        }
    }
    return cache->unpublishedGlyphs == 0 || PublishMissingGlyphs(*cache);
}

bool CUIRenderTextOriginal::PublishMissingGlyphs(FontCache &cache)
{
    constexpr std::uint32_t GlyphPadding = 1;
    std::vector<Glyph *> pending;
    try
    {
        pending.reserve(cache.unpublishedGlyphs);
        for (auto &[character, glyph] : cache.glyphs)
        {
            (void)character;
            if (glyph.width > 0 && glyph.height > 0 && !IsValid(glyph.atlas))
            {
                pending.push_back(&glyph);
            }
        }
    }
    catch (...)
    {
        return false;
    }
    if (pending.empty())
    {
        return true;
    }

    std::uint64_t pageWidth = 0;
    std::uint32_t pageHeight = 0;
    for (const Glyph *glyph : pending)
    {
        pageWidth += static_cast<std::uint32_t>(glyph->width) + GlyphPadding * 2U;
        pageHeight =
            (std::max)(pageHeight, static_cast<std::uint32_t>(glyph->height) + GlyphPadding * 2U);
    }
    if (pageWidth > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    const auto width = static_cast<std::uint32_t>(pageWidth);
    const std::uint64_t byteCount = pageWidth * pageHeight * 4U;
    if (byteCount > (std::numeric_limits<std::size_t>::max)())
    {
        return false;
    }

    std::shared_ptr<std::vector<std::byte>> pixels;
    try
    {
        pixels = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(byteCount));
    }
    catch (...)
    {
        return false;
    }
    std::uint32_t x = GlyphPadding;
    for (const Glyph *glyph : pending)
    {
        for (int row = 0; row < glyph->height; ++row)
        {
            for (int column = 0; column < glyph->width; ++column)
            {
                const std::size_t source =
                    static_cast<std::size_t>(row) * glyph->width + static_cast<std::size_t>(column);
                const std::size_t destination =
                    (static_cast<std::size_t>(row + GlyphPadding) * width + x +
                     static_cast<std::uint32_t>(column)) *
                    4U;
                (*pixels)[destination] = std::byte{0xff};
                (*pixels)[destination + 1] = std::byte{0xff};
                (*pixels)[destination + 2] = std::byte{0xff};
                (*pixels)[destination + 3] = glyph->alpha[source];
            }
        }
        x += static_cast<std::uint32_t>(glyph->width) + GlyphPadding * 2U;
    }

    const auto id = applicationKeeper_.BitmapRegistry().AllocateDynamicIdentity();
    if (!id.has_value())
    {
        return false;
    }
    const LogicalRenderAssetRef atlas{*id, 1};
    const RenderSamplerIntent sampler{LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge};
    if (!applicationKeeper_.BitmapRegistry().CommitOwnerProducedRevision(atlas, width, pageHeight,
                                                                         sampler, pixels))
    {
        (void)applicationKeeper_.BitmapRegistry().RetireLogicalAsset(atlas);
        return false;
    }
    try
    {
        cache.atlases.push_back(atlas);
    }
    catch (...)
    {
        (void)applicationKeeper_.BitmapRegistry().RetireLogicalAsset(atlas);
        return false;
    }

    x = GlyphPadding;
    for (Glyph *glyph : pending)
    {
        glyph->atlas = atlas;
        glyph->leftU = static_cast<float>(x) / static_cast<float>(width);
        glyph->rightU = static_cast<float>(x + glyph->width) / static_cast<float>(width);
        glyph->bottomV = static_cast<float>(GlyphPadding) / static_cast<float>(pageHeight);
        glyph->topV =
            static_cast<float>(GlyphPadding + glyph->height) / static_cast<float>(pageHeight);
        std::vector<std::byte>().swap(glyph->alpha);
        x += static_cast<std::uint32_t>(glyph->width) + GlyphPadding * 2U;
    }
    cache.unpublishedGlyphs = 0;
    return true;
}

bool CUIRenderTextOriginal::RenderBackground(DWORD backgroundColor, float boxX, float boxY,
                                             float boxWidth, float boxHeight, int viewportHeight,
                                             LegacyRenderFacade &facade) const
{
    if (backgroundColor == 0)
    {
        return true;
    }
    const float top = static_cast<float>(viewportHeight) - (boxY + boxHeight);
    const float right = boxX + boxWidth;
    const float bottom = static_cast<float>(viewportHeight) - boxY;
    (void)facade.SetTextureEnable(false);
    (void)facade.Color4Ub(GetRed(backgroundColor), GetGreen(backgroundColor),
                          GetBlue(backgroundColor), GetAlpha(backgroundColor));
    const bool rendered = facade.Begin(LegacyPrimitive::Quads) && facade.Vertex2(boxX, top) &&
                          facade.Vertex2(boxX, bottom) && facade.Vertex2(right, bottom) &&
                          facade.Vertex2(right, top) && facade.End();
    (void)facade.Color4(1.0F, 1.0F, 1.0F, 1.0F);
    (void)facade.SetTextureEnable(true);
    return rendered;
}

bool CUIRenderTextOriginal::RenderGlyphs(FontCache &cache, DWORD textColor, const wchar_t *text,
                                         std::size_t length, float boxX, float boxY, int tab,
                                         int clipMove, int renderWidth, int viewportHeight,
                                         LegacyRenderFacade &facade,
                                         const LogicalGeometryAssetLease &quadGeometry) const
{
    if (length == 0 || text[0] == L'\n' || renderWidth <= 0)
    {
        return true;
    }
    const std::array<float, 4> color{static_cast<float>(GetRed(textColor)) / 255.0F,
                                     static_cast<float>(GetGreen(textColor)) / 255.0F,
                                     static_cast<float>(GetBlue(textColor)) / 255.0F,
                                     static_cast<float>(GetAlpha(textColor)) / 255.0F};
    const int clipEnd = clipMove + renderWidth;
    const float textTop =
        static_cast<float>(viewportHeight) - (boxY + static_cast<float>(cache.height));
    int pen = 0;
    LogicalRenderAssetRef runAsset{};
    std::uint64_t runId = 0;
    Glyph *const *const glyphs = cache.glyphData;
    const std::size_t glyphCount = cache.glyphCount;
    const Glyph *const fallbackGlyph = cache.fallbackGlyph;
    for (std::size_t index = 0; index < length; ++index)
    {
        const std::size_t glyphIndex = static_cast<std::size_t>(text[index]);
        const Glyph *glyph = glyphIndex < glyphCount ? glyphs[glyphIndex] : nullptr;
        if (glyph == nullptr)
        {
            glyph = fallbackGlyph;
        }
        if (glyph == nullptr)
        {
            return false;
        }
        const int glyphLeft = pen + glyph->originX;
        const int glyphTop = cache.ascent - glyph->originY;
        const int visibleLeft = (std::max)(glyphLeft, clipMove);
        const int visibleRight = (std::min)(glyphLeft + glyph->width, clipEnd);
        const int visibleTop = (std::max)(glyphTop, 0);
        const int visibleBottom = (std::min)(glyphTop + glyph->height, cache.height);
        if (visibleLeft < visibleRight && visibleTop < visibleBottom)
        {
            if (glyph->atlas != runAsset)
            {
                runAsset = glyph->atlas;
                runId = facade.BeginTextGlyphRun(runAsset);
                if (runId == 0)
                {
                    return false;
                }
            }
            const float horizontalScale = (glyph->rightU - glyph->leftU) / glyph->width;
            const float verticalScale = (glyph->topV - glyph->bottomV) / glyph->height;
            const float leftU =
                glyph->leftU + static_cast<float>(visibleLeft - glyphLeft) * horizontalScale;
            const float rightU =
                glyph->rightU -
                static_cast<float>(glyphLeft + glyph->width - visibleRight) * horizontalScale;
            const float topV =
                glyph->topV - static_cast<float>(visibleTop - glyphTop) * verticalScale;
            const float bottomV =
                glyph->bottomV +
                static_cast<float>(glyphTop + glyph->height - visibleBottom) * verticalScale;
            const float left = boxX + static_cast<float>(tab + visibleLeft - clipMove);
            const float right = boxX + static_cast<float>(tab + visibleRight - clipMove);
            const float top = textTop + static_cast<float>(visibleTop);
            const float bottom = textTop + static_cast<float>(visibleBottom);
            RenderTapeQuadInstance instance{};
            instance.corners = {std::array<float, 4>{left, top, 0.0F, leftU},
                                std::array<float, 4>{left, bottom, 0.0F, leftU},
                                std::array<float, 4>{right, bottom, 0.0F, rightU},
                                std::array<float, 4>{right, top, 0.0F, rightU}};
            instance.v = {topV, bottomV, bottomV, topV};
            instance.color = color;
            if (!facade.DrawQuadInstance(quadGeometry, instance, runId))
            {
                return false;
            }
        }
        pen += glyph->advance;
    }
    return true;
}

bool CUIRenderTextOriginal::RenderRecorded(LegacyFontRole role, DWORD textColor,
                                           DWORD backgroundColor, int x, int y, const wchar_t *text,
                                           int boxWidth, int boxHeight, int sort,
                                           LegacyRenderFacade &facade,
                                           const LogicalGeometryAssetLease &quadGeometry,
                                           OUT SIZE *textSize)
{
    if (fontDc_ == nullptr || text == nullptr || (text[0] == L'\0' && boxWidth == 0))
    {
        return false;
    }

    const std::size_t roleIndex = static_cast<std::size_t>(role);
    FontCache *const cache =
        roleIndex < fontCaches_.size() && fontCaches_[roleIndex].font != nullptr
            ? &fontCaches_[roleIndex]
            : nullptr;
    if (cache == nullptr)
    {
        return false;
    }
    int textWidth = 0;
    int textHeight = 0;
    const wchar_t *measuredText = text[0] == L'\0' ? L"0" : text;
    const std::size_t length = std::wcslen(text);
    const std::size_t measuredLength = text[0] == L'\0' ? 1U : length;
    if (!MeasureGlyphs(role, measuredText, measuredLength, textWidth, textHeight))
    {
        return false;
    }

    const RenderTapeRect viewport = facade.Viewport();
    if (viewport.width <= 0 || viewport.height <= 0)
    {
        return false;
    }
    const float rateX = static_cast<float>(viewport.width) / static_cast<float>(REFERENCE_WIDTH);
    const float rateY = static_cast<float>(viewport.height) / static_cast<float>(REFERENCE_HEIGHT);
    float boxX = static_cast<float>(x) * rateX;
    const float boxY = static_cast<float>(y) * rateY;
    float realBoxWidth = static_cast<float>(boxWidth) * rateX;
    float realBoxHeight = static_cast<float>(boxHeight) * rateY;
    int renderWidth = textWidth;
    if (realBoxWidth == 0.0F)
        realBoxWidth = static_cast<float>(textWidth);
    if (realBoxHeight == 0.0F)
        realBoxHeight = static_cast<float>(textHeight);

    int tab = 0;
    int clipMove = 0;
    if (sort == RT3_SORT_LEFT_CLIP)
    {
        if (renderWidth > realBoxWidth)
        {
            clipMove = renderWidth - static_cast<int>(realBoxWidth);
            renderWidth = static_cast<int>(realBoxWidth);
        }
    }
    else if (sort == RT3_SORT_LEFT)
    {
        renderWidth = (std::min)(renderWidth, static_cast<int>(realBoxWidth));
    }
    else if (sort == RT3_SORT_CENTER || sort == RT3_WRITE_CENTER)
    {
        if (renderWidth > realBoxWidth)
        {
            clipMove = (renderWidth - static_cast<int>(realBoxWidth)) / 2;
            renderWidth = static_cast<int>(realBoxWidth);
        }
        else
        {
            tab = (static_cast<int>(realBoxWidth) - renderWidth) / 2;
        }
        if (sort == RT3_WRITE_CENTER)
            boxX -= realBoxWidth / 2.0F;
    }
    else if (sort == RT3_SORT_RIGHT || sort == RT3_WRITE_RIGHT_TO_LEFT)
    {
        if (renderWidth > realBoxWidth)
        {
            clipMove = renderWidth - static_cast<int>(realBoxWidth);
            renderWidth = static_cast<int>(realBoxWidth);
        }
        else
        {
            tab = static_cast<int>(realBoxWidth) - renderWidth;
        }
        if (sort == RT3_WRITE_RIGHT_TO_LEFT)
            boxX -= realBoxWidth;
    }

    if (!RenderBackground(backgroundColor, boxX, boxY, realBoxWidth, realBoxHeight, viewport.height,
                          facade) ||
        !RenderGlyphs(*cache, textColor, text, length, boxX, boxY, tab, clipMove, renderWidth,
                      viewport.height, facade, quadGeometry))
    {
        return false;
    }
    if (textSize != nullptr)
    {
        textSize->cx = static_cast<LONG>(static_cast<float>(renderWidth) / rateX);
        textSize->cy = static_cast<LONG>(static_cast<float>(textHeight) / rateY);
    }
    return true;
}

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.
