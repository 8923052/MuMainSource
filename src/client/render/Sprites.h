#pragma once
#include "app/ApplicationConfigScheduling.h"
#include "render/Assets.h"
#include "render/Textures.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <setjmp.h>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define CLRDW_WHITE ARGB(255, 255, 255, 255)
#define CLRDW_BR_GRAY ARGB(255, 226, 226, 226)
#define CLRDW_GRAY ARGB(255, 119, 119, 119)
#define CLRDW_YELLOW ARGB(255, 255, 255, 121)
#define CLRDW_BR_YELLOW ARGB(255, 255, 238, 193)
#define CLRDW_BR_ORANGE ARGB(255, 255, 217, 39)
#define CLRDW_ORANGE ARGB(255, 255, 180, 0)
#define CLRDW_DARKYELLOW ARGB(255, 255, 255, 0)

#define CLRREF_WHITE RGB(240, 240, 240)
#define CLRREF_BR_GRAY RGB(210, 210, 210)
#define CLRREF_GRAY RGB(127, 127, 127)
#define CLRREF_BLACK RGB(0, 0, 0)
#define CLRREF_RED RGB(252, 60, 60)
#define CLRREF_BR_RED RGB(252, 128, 128)
#define CLRREF_ORANGE RGB(220, 170, 100)
#define CLRREF_YELLOW RGB(210, 210, 100)
#define CLRREF_BR_YELLOW RGB(240, 240, 170)
#define CLRREF_GREEN RGB(100, 230, 100)
#define CLRREF_DK_BLUE RGB(50, 40, 255)
#define CLRREF_BLUE RGB(101, 89, 255)
#define CLRREF_BR_BLUE RGB(168, 148, 255)
#define CLRREF_VIOLET RGB(210, 100, 210)
#define CLRREF_BR_OCHER RGB(250, 225, 200)

enum VERTEX_POS
{
    LT,
    LB,
    RB,
    RT,
    POS_MAX
};
enum CHANGE_PRAM
{
    X = 1,
    Y,
    XY
};

struct SScrCoord
{
    float fX, fY;
};

struct STexCoord
{
    float fTU, fTV;
};

struct SFrameCoord
{
    int nX, nY;
};

struct SImgInfo
{
    int nTexID;
    int nX, nY;
    int nWidth, nHeight;
};

#define MAX_BITMAP_FILE_NAME 256
#define MAX_SPRITES 1000

#pragma warning(disable : 4786)

class CErrorReport;
class CmuConsoleDebug;
class SessionTextureNamespace;

#pragma pack(push, 1)
struct BITMAP_t
{
    std::uint32_t BitmapIndex;
    wchar_t FileName[MAX_BITMAP_FILE_NAME];
    float Width;
    float Height;
    char Components;
    LogicalRenderAssetRef Asset;
    RenderSamplerIntent Sampler;
    std::uint8_t Ref;
    bool IsSkin;
    bool IsHair;
    // Immutable decoded RGBA8 pixels; the current published catalog revision
    // for Asset. Never written through - a mutation caller builds a new local
    // vector and publishes it via CGlobalBitmap::CommitRecordedRevisions.
    std::shared_ptr<const std::vector<std::byte>> Pixels;

  private:
    friend class CBitmapCache;
    std::uint32_t dwCallCount;
};
#pragma pack(pop)

class CBitmapCache
{
    enum
    {
        QUICK_CACHE_MAPTILE = 0,
        QUICK_CACHE_MAPGRASS,
        QUICK_CACHE_WATER,
        QUICK_CACHE_CURSOR,
        QUICK_CACHE_FONT,
        QUICK_CACHE_MAINFRAME,
        QUICK_CACHE_SKILLICON,
        QUICK_CACHE_PLAYER,

        NUMBER_OF_QUICK_CACHE,
    };

    struct QUICK_CACHE
    {
        std::uint32_t dwBitmapIndexMin = 0;
        std::uint32_t dwBitmapIndexMax = 0;
        std::vector<BITMAP_t *> bitmaps;
    };
    using type_cache_map = std::map<std::uint32_t, BITMAP_t *>;

    type_cache_map m_mapCacheMain;
    type_cache_map m_mapCachePlayer;
    type_cache_map m_mapCacheInterface;
    type_cache_map m_mapCacheEffect;

    QUICK_CACHE m_QuickCache[NUMBER_OF_QUICK_CACHE];
    BITMAP_t *m_pNullBitmap = nullptr;

    CTimer2 m_ManageTimer;

  public:
    CBitmapCache();
    ~CBitmapCache();

    bool Create();
    void Release();

    void Add(std::uint32_t uiBitmapIndex, BITMAP_t *pBitmap);
    void Remove(std::uint32_t uiBitmapIndex);
    void RemoveAll();

    size_t GetCacheSize();

    void Update(CTimer2::StartTickTime &startTickTime, CmuConsoleDebug &consoleDebug);

    bool Find(std::uint32_t uiBitmapIndex, BITMAP_t **ppBitmap);
};

class CGlobalBitmap
{
    struct DecodedBitmapAsset final
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        char components = 0;
        std::shared_ptr<const std::vector<std::byte>> pixels;
        std::array<std::optional<std::uint32_t>, 6> sharedBitmapIndices;
        std::array<bool, 6> ownerRetainedBitmapSlots{};
        std::uint64_t unusedSinceMilliseconds = 0;
    };

    using BitmapPtr = std::unique_ptr<BITMAP_t>;
    using type_bitmap_map = std::map<std::uint32_t, BitmapPtr>;
    using type_index_list = std::list<std::uint32_t>;

    type_bitmap_map m_mapBitmap;
    type_index_list m_listNonamedIndex;
    std::unordered_map<std::wstring, DecodedBitmapAsset, LogicalAssetPath::Hash,
                       LogicalAssetPath::Equal>
        decodedOwnerAssets_;

    std::uint32_t m_uiAlternate, m_uiTextureIndexStream;
    std::uint32_t m_dwUsedTextureMemory;
    BITMAP_t m_errorTexture{};

    CBitmapCache m_BitmapCache;
#ifdef DEBUG_BITMAP_CACHE
    CTimer2 m_DebugOutputTimer;
#endif // DEBUG_BITMAP_CACHE

    // Logical-asset catalog (PLAN_P1R5.1.md section 5.2): identity/revision
    // registry backing TryLease/AllocateDynamicIdentity/CommitRecordedRevisions
    // /RetireLogicalAsset/CommitOwnerProducedRevision. Independent of the
    // legacy BitmapIndex map above - dynamic assets never have one. Kept as
    // its own portable type so the catalog logic is testable without this
    // class's decoder dependencies.
    LogicalRenderAssetCatalog m_assetCatalog;

    void Init();

  public:
    CGlobalBitmap();
    virtual ~CGlobalBitmap();

    std::uint32_t LoadImage(const std::wstring &filename, CErrorReport &errorReport,
                            LegacyTextureFilter uiFilter = LegacyTextureFilter::Nearest,
                            LegacyTextureWrap uiWrapMode = LegacyTextureWrap::ClampToEdge);
    std::uint32_t LoadSessionImage(const std::wstring &filename, CErrorReport &errorReport,
                                   LegacyTextureFilter uiFilter, LegacyTextureWrap uiWrapMode);
    bool LoadImage(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                   CErrorReport &errorReport,
                   LegacyTextureFilter uiFilter = LegacyTextureFilter::Nearest,
                   LegacyTextureWrap uiWrapMode = LegacyTextureWrap::ClampToEdge);
    bool LoadNamedImage(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                        CErrorReport &errorReport,
                        LegacyTextureFilter uiFilter = LegacyTextureFilter::Nearest,
                        LegacyTextureWrap uiWrapMode = LegacyTextureWrap::ClampToEdge);
    // Adds one descriptor owner; pair it with UnloadImage.
    bool RetainImage(std::uint32_t uiBitmapIndex) noexcept;
    void UnloadImage(std::uint32_t uiBitmapIndex, bool bForce = false);
    void UnloadAllImages(CErrorReport &errorReport);
    void CollectIdleOwnerAssets(std::uint64_t nowMilliseconds,
                                std::uint64_t idleTimeoutMilliseconds) noexcept;

    BITMAP_t *GetTexture(std::uint32_t uiBitmapIndex);
    BITMAP_t *FindTexture(std::uint32_t uiBitmapIndex);
    BITMAP_t *FindTexture(const std::wstring &filename);
    BITMAP_t *FindTextureByName(const std::wstring &name);
    std::optional<LogicalRenderAssetMetadata> TryDescribe(
        std::uint32_t uiBitmapIndex) const noexcept;
    std::optional<LogicalRenderAssetMetadata> TryDescribe(
        const std::wstring &filename) const noexcept;
    std::optional<LogicalRenderAssetMetadata> TryDescribeByName(
        const std::wstring &name) const noexcept;

    std::uint32_t GetUsedTextureMemory() const;
    size_t GetNumberOfTexture() const;

    bool Convert_Format(const std::wstring &filename);

    void Manage(CTimer2::StartTickTime &startTickTime, CmuConsoleDebug &consoleDebug);

    inline BITMAP_t &operator[](std::uint32_t uiBitmapIndex)
    {
        return *GetTexture(uiBitmapIndex);
    }

    // Logical-asset catalog API (PLAN_P1R5.1.md section 5.2). Implemented
    // exactly - do not add further public catalog entry points here.
    bool SetTrustedAssetProducerThread(std::thread::id thread) noexcept;
    std::optional<LogicalRenderAssetLease> TryLease(LogicalRenderAssetRef asset) const noexcept;
    std::optional<LogicalRenderAssetId> AllocateDynamicIdentity() noexcept;
    bool CommitRecordedRevisions(std::span<const LogicalRenderAssetRevisionInput> inputs) noexcept;
    bool RetireLogicalAsset(LogicalRenderAssetRef asset) noexcept;
    bool CommitOwnerProducedRevision(LogicalRenderAssetRef asset, std::uint32_t width,
                                     std::uint32_t height, RenderSamplerIntent sampler,
                                     std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept;

  protected:
    std::uint32_t GenerateTextureIndex();
    std::uint32_t FindAvailableTextureIndex(std::uint32_t uiSeed);

    bool OpenJpegTurbo(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                       CErrorReport &errorReport,
                       LegacyTextureFilter uiFilter = LegacyTextureFilter::Nearest,
                       LegacyTextureWrap uiWrapMode = LegacyTextureWrap::ClampToEdge);
    bool OpenTga(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                 LegacyTextureFilter uiFilter = LegacyTextureFilter::Nearest,
                 LegacyTextureWrap uiWrapMode = LegacyTextureWrap::ClampToEdge);
    bool OpenLegacyOzt(std::uint32_t index, const std::wstring &filename,
                       LegacyTextureFilter filter, LegacyTextureWrap wrap);
    void SplitFileName(IN const std::wstring &filepath, OUT std::wstring &filename,
                       bool bIncludeExt);
    void SplitExt(IN const std::wstring &filepath, OUT std::wstring &ext, bool bIncludeDot);
    void ExchangeExt(IN const std::wstring &in_filepath, IN const std::wstring &ext,
                     OUT std::wstring &out_filepath);

    bool Save_Image(const std::wstring &src, const std::wstring &dest, int cDumpHeader);
    void ReleaseImages(CErrorReport *errorReport);

  private:
    friend class SessionTextureNamespace;

    bool IsCurrentAsset(LogicalRenderAssetRef asset) const noexcept;
    std::uint64_t AssetCatalogGeneration() const noexcept;
    bool CommitDecodedBitmap(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                             std::uint32_t width, std::uint32_t height, char components,
                             LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
                             std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept;
    bool CacheDecodedOwnerAsset(const std::wstring &filename, std::uint32_t bitmapIndex) noexcept;
    bool IsOwnerDecodedPixels(const BITMAP_t &bitmap) const noexcept;
};

#define SPR_SIZING_DATUMS_LT 0x00
#define SPR_SIZING_DATUMS_LB 0x01
#define SPR_SIZING_DATUMS_RT 0x02
#define SPR_SIZING_DATUMS_RB 0x03
#define IS_SIZING_DATUMS_R(v) (v & 0x02)
#define IS_SIZING_DATUMS_B(v) (v & 0x01)

class CSprite : protected SessionUiLegacyBindings
{
  protected:
    float m_fScrHeight;

    std::optional<SessionBitmapMetadata> m_pTexture;
    int m_nTexID;
    float m_fOrgWidth;
    float m_fOrgHeight;

    SScrCoord m_aScrCoord[POS_MAX];
    STexCoord m_aTexCoord[POS_MAX];
    float m_fDatumX;
    float m_fDatumY;

    BYTE m_byAlpha;
    BYTE m_byRed;
    BYTE m_byGreen;
    BYTE m_byBlue;

    int m_nMaxFrame;
    int m_nNowFrame;
    int m_nStartFrame;
    int m_nEndFrame;
    STexCoord *m_aFrameTexCoord;
    bool m_bRepeat;
    double m_dDelayTime;
    double m_dDeltaTickSum;

    float m_fScaleX;
    float m_fScaleY;
    bool m_bTile;
    int m_nSizingDatums;
    bool m_bShow;

  public:
    explicit CSprite(SessionKeeper &keeper);
    virtual ~CSprite();

    void Release();
    void Create(int nOrgWidth, int nOrgHeight, int nTexID = -1, int nMaxFrame = 0,
                SFrameCoord *aFrameCoord = NULL, int nDatumX = 0, int nDatumY = 0,
                bool bTile = false, int nSizingDatums = SPR_SIZING_DATUMS_LT, float fScaleX = 1.0f,
                float fScaleY = 1.0f);
    void Create(SImgInfo *pImgInfo, int nDatumX = 0, int nDatumY = 0, bool bTile = false,
                int nSizingDatums = SPR_SIZING_DATUMS_LT, float fScaleX = 1.0f,
                float fScaleY = 1.0f);
    void SetPosition(int nXCoord, int nYCoord, CHANGE_PRAM eChangedPram = XY);
    int GetXPos()
    {
        return (int)m_aScrCoord[LT].fX;
    }
    int GetYPos()
    {
        return int(m_fScrHeight - m_aScrCoord[LT].fY);
    }
    void SetSize(int nWidth, int nHeight, CHANGE_PRAM eChangedPram = XY);
    int GetWidth()
    {
        return int(m_aScrCoord[RT].fX - m_aScrCoord[LT].fX);
    }
    int GetHeight()
    {
        return int(m_aScrCoord[LT].fY - m_aScrCoord[LB].fY);
    }
    int GetTexID()
    {
        return m_nTexID;
    };
    int GetTexWidth()
    {
        return -1 < m_nTexID && m_pTexture.has_value() ? (int)m_pTexture->Width : 0;
    }
    int GetTexHeight()
    {
        return -1 < m_nTexID && m_pTexture.has_value() ? (int)m_pTexture->Height : 0;
    }

    float GetScaleX()
    {
        return m_fScaleX;
    }
    float GetScaleY()
    {
        return m_fScaleY;
    }
    void Show(bool bShow = true)
    {
        m_bShow = bShow;
    }
    bool IsShow() const
    {
        return m_bShow;
    }
    int GetSizingDatums()
    {
        return m_nSizingDatums;
    }
    BOOL PtInSprite(long lXPos, long lYPos);
    BOOL CursorInObject();
    void SetAlpha(BYTE byAlpha)
    {
        m_byAlpha = byAlpha;
    }
    BYTE GetAlpha()
    {
        return m_byAlpha;
    }
    void SetColor(BYTE byRed, BYTE byGreen, BYTE byBlue)
    {
        m_byRed = byRed;
        m_byGreen = byGreen;
        m_byBlue = byBlue;
    }
    void SetAction(int nStartFrame, int nEndFrame, double dDelayTime = 0.0, bool bRepeat = true);
    void SetNowFrame(int nFrame);
    void Update(double dDeltaTick = 0.0);
    void Render();
};

//  DEFINE.
const BYTE SHADOW_NONE = 0;
const BYTE SHADOW_RENDER_COLOR = 1;
const BYTE SHADOW_RENDER_TEXTURE = 2;

//  CLASS.
class TextureScript
{
  protected:
    bool m_bBright;        //
    bool m_bHiddenMesh;    //
    bool m_bStreamMesh;    //
    bool m_bNoneBlendMesh; //
    BYTE m_byShadowMesh;

  public:
    TextureScript(void)
        : m_bBright(false), m_bHiddenMesh(false), m_bStreamMesh(false), m_bNoneBlendMesh(false),
          m_byShadowMesh(0) {};
    ~TextureScript(void) {};
    TextureScript(const TextureScript &r)
    {
        m_bBright = r.m_bBright;
        m_bHiddenMesh = r.m_bHiddenMesh;
        m_bStreamMesh = r.m_bStreamMesh;
        m_bNoneBlendMesh = r.m_bNoneBlendMesh;
        m_byShadowMesh = r.m_byShadowMesh;
    }

    inline bool getHiddenMesh(void)
    {
        return m_bHiddenMesh;
    }
    inline bool getBright(void)
    {
        return m_bBright;
    }
    inline bool getStreamMesh(void)
    {
        return m_bStreamMesh;
    }
    inline bool getNoneBlendMesh(void)
    {
        return m_bNoneBlendMesh;
    }
    inline BYTE getShadowMesh(void)
    {
        return m_byShadowMesh;
    }

    void setScript(const TextureScript &rhs);
};

class TextureScriptParsing : public TextureScript
{
  private:
    bool m_bBeScript;

  public:
    TextureScriptParsing(void) : m_bBeScript(false) {};
    ~TextureScriptParsing(void) {};

    bool IsScript(void)
    {
        return m_bBeScript;
    }
    bool parsingTScriptA(char *filename);
};

class OBJECT;

// Prepared billboard data. Owner is an identity for retirement, never a writer.
struct SessionSprite final
{
    bool Live = false;
    bool Visible = false;
    int Type = 0;
    int SubType = 0;
    float AnimationFrame = 1.f;
    float Scale = 1.f;
    vec3_t Angle{};
    vec3_t Position{};
    vec3_t StartPosition{};
    vec3_t Light{};
    const OBJECT *Owner = nullptr;
};

// Draw-only additions follow the tick batch; retained capacity serves later frames.
struct SessionSpriteStorage final
{
    std::deque<SessionSprite> objects{1};
    std::size_t used = 0;
    std::size_t simulated = 0;

    SessionSprite &operator[](std::size_t index) noexcept
    {
        return objects[index];
    }
    const SessionSprite &operator[](std::size_t index) const noexcept
    {
        return objects[index];
    }
    SessionSprite &Append()
    {
        if (used == objects.size())
            objects.emplace_back();
        return objects[used++];
    }
    void BeginTick() noexcept
    {
        used = 0;
    }
    void EndTick() noexcept
    {
        simulated = used;
    }
    void BeginDraw() noexcept
    {
        used = simulated;
    }
    void Clear() noexcept
    {
        for (auto &object : objects)
        {
            object.Live = false;
            object.Owner = nullptr;
        }
        used = simulated = 0;
    }
};

namespace Render::Sprites
{
int AppendPreparedSprite(SessionSpriteStorage &output, int Type, const vec3_t Position, float Scale,
                         const vec3_t Light, const OBJECT *Owner, float Rotation, int SubType);
}
