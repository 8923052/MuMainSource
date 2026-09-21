#include "render/Sprites.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationDiagnostics.h"
#include "data/GameData.h"
#include "data/ResourceData.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/WorldSimulation.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "turbojpeg.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

CBitmapCache::CBitmapCache() = default;
CBitmapCache::~CBitmapCache()
{
    Release();
}

namespace
{
constexpr std::uint32_t RangeFor(std::uint32_t begin, std::uint32_t end)
{
    return (end > begin) ? (end - begin) : 0;
}

constexpr std::size_t SharedBitmapSlot(LegacyTextureFilter filter, LegacyTextureWrap wrap) noexcept
{
    return static_cast<std::size_t>(filter) * 3U + static_cast<std::size_t>(wrap);
}

class TurboJpegHandle
{
  public:
    TurboJpegHandle() : handle(tjInitDecompress())
    {
    }
    ~TurboJpegHandle()
    {
        if (handle != nullptr)
        {
            tjDestroy(handle);
        }
    }

    tjhandle get() const
    {
        return handle;
    }
    bool valid() const
    {
        return handle != nullptr;
    }

  private:
    tjhandle handle;
};

void ReportTurboError(CErrorReport &errorReport, const wchar_t *context)
{
    const char *message = tjGetErrorStr();
    if (message == nullptr)
    {
        message = "Unknown TurboJPEG error";
    }
    errorReport.Write(L"[TurboJPEG] %ls: %hs", context, message);
}

std::optional<int> NextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value)
    {
        if (result > (std::numeric_limits<int>::max)() / 2)
        {
            return std::nullopt;
        }
        result <<= 1;
    }
    return result;
}

std::string NarrowPath(const std::wstring &wide)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> conv;
#ifdef _WIN32
    return conv.to_bytes(wide);
#else
    // Asset paths are Windows-spelled (backslashes, mixed case); resolve
    // them against the case-sensitive filesystem.
    return MuResolvePath(conv.to_bytes(wide).c_str());
#endif
}

// Expands tightly packed RGB triples to RGBA8 (opaque alpha), matching
// the catalog's uniform RGBA8 storage (PLAN_P1R5.1.md section 5.2).
std::vector<std::byte> ExpandRgbToRgba8(const std::vector<unsigned char> &rgb,
                                        std::size_t pixelCount)
{
    std::vector<std::byte> rgba(pixelCount * 4u);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        rgba[pixel * 4u] = static_cast<std::byte>(rgb[pixel * 3u]);
        rgba[pixel * 4u + 1u] = static_cast<std::byte>(rgb[pixel * 3u + 1u]);
        rgba[pixel * 4u + 2u] = static_cast<std::byte>(rgb[pixel * 3u + 2u]);
        rgba[pixel * 4u + 3u] = std::byte{0xFFu};
    }
    return rgba;
}
} // namespace

bool CBitmapCache::Create()
{
    Release();

    auto configureCache = [](QUICK_CACHE &cache, std::uint32_t minIndex, std::uint32_t maxIndex) {
        cache.dwBitmapIndexMin = minIndex;
        cache.dwBitmapIndexMax = maxIndex;
        cache.bitmaps.assign(RangeFor(minIndex, maxIndex), nullptr);
    };

    configureCache(m_QuickCache[QUICK_CACHE_MAPTILE], BITMAP_MAPTILE_BEGIN, BITMAP_MAPTILE_END);
    configureCache(m_QuickCache[QUICK_CACHE_MAPGRASS], BITMAP_MAPGRASS_BEGIN, BITMAP_MAPGRASS_END);
    configureCache(m_QuickCache[QUICK_CACHE_WATER], BITMAP_WATER_BEGIN, BITMAP_WATER_END);
    configureCache(m_QuickCache[QUICK_CACHE_CURSOR], BITMAP_CURSOR_BEGIN, BITMAP_CURSOR_END);
    configureCache(m_QuickCache[QUICK_CACHE_FONT], BITMAP_FONT_BEGIN, BITMAP_FONT_END);
    configureCache(m_QuickCache[QUICK_CACHE_MAINFRAME], BITMAP_INTERFACE_NEW_MAINFRAME_BEGIN,
                   BITMAP_INTERFACE_NEW_MAINFRAME_END);
    configureCache(m_QuickCache[QUICK_CACHE_SKILLICON], BITMAP_INTERFACE_NEW_SKILLICON_BEGIN,
                   BITMAP_INTERFACE_NEW_SKILLICON_END);
    configureCache(m_QuickCache[QUICK_CACHE_PLAYER], BITMAP_PLAYER_TEXTURE_BEGIN,
                   BITMAP_PLAYER_TEXTURE_END);

    m_pNullBitmap = new BITMAP_t;
    *m_pNullBitmap = {};

    m_ManageTimer.SetTimer(1500);

    return true;
}
void CBitmapCache::Release()
{
    SAFE_DELETE(m_pNullBitmap);

    RemoveAll();
    for (auto &cache : m_QuickCache)
    {
        cache.bitmaps.clear();
    }
}

void CBitmapCache::Add(std::uint32_t uiBitmapIndex, BITMAP_t *pBitmap)
{
    for (auto &cache : m_QuickCache)
    {
        if (uiBitmapIndex >= cache.dwBitmapIndexMin && uiBitmapIndex < cache.dwBitmapIndexMax)
        {
            const auto index = static_cast<std::size_t>(uiBitmapIndex - cache.dwBitmapIndexMin);
            cache.bitmaps[index] = pBitmap ? pBitmap : m_pNullBitmap;
            return;
        }
    }

    if (pBitmap)
    {
        if (BITMAP_PLAYER_TEXTURE_BEGIN <= uiBitmapIndex &&
            BITMAP_PLAYER_TEXTURE_END >= uiBitmapIndex)
            m_mapCachePlayer.insert(type_cache_map::value_type(uiBitmapIndex, pBitmap));
        else if (BITMAP_INTERFACE_TEXTURE_BEGIN <= uiBitmapIndex &&
                 BITMAP_INTERFACE_TEXTURE_END >= uiBitmapIndex)
            m_mapCacheInterface.insert(type_cache_map::value_type(uiBitmapIndex, pBitmap));
        else if (BITMAP_EFFECT_TEXTURE_BEGIN <= uiBitmapIndex &&
                 BITMAP_EFFECT_TEXTURE_END >= uiBitmapIndex)
            m_mapCacheEffect.insert(type_cache_map::value_type(uiBitmapIndex, pBitmap));
        else
            m_mapCacheMain.insert(type_cache_map::value_type(uiBitmapIndex, pBitmap));
    }
}
void CBitmapCache::Remove(std::uint32_t uiBitmapIndex)
{
    for (int i = 0; i < NUMBER_OF_QUICK_CACHE; i++)
    {
        if (uiBitmapIndex >= m_QuickCache[i].dwBitmapIndexMin &&
            uiBitmapIndex < m_QuickCache[i].dwBitmapIndexMax)
        {
            const auto dwVI =
                static_cast<std::size_t>(uiBitmapIndex - m_QuickCache[i].dwBitmapIndexMin);
            m_QuickCache[i].bitmaps[dwVI] = nullptr;
            return;
        }
    }

    if (BITMAP_PLAYER_TEXTURE_BEGIN <= uiBitmapIndex && BITMAP_PLAYER_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCachePlayer.find(uiBitmapIndex);
        if (mi != m_mapCachePlayer.end())
            m_mapCachePlayer.erase(mi);
    }
    else if (BITMAP_INTERFACE_TEXTURE_BEGIN <= uiBitmapIndex &&
             BITMAP_INTERFACE_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCacheInterface.find(uiBitmapIndex);
        if (mi != m_mapCacheInterface.end())
            m_mapCacheInterface.erase(mi);
    }
    else if (BITMAP_EFFECT_TEXTURE_BEGIN <= uiBitmapIndex &&
             BITMAP_EFFECT_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCacheEffect.find(uiBitmapIndex);
        if (mi != m_mapCacheEffect.end())
            m_mapCacheEffect.erase(mi);
    }
    else
    {
        auto mi = m_mapCacheMain.find(uiBitmapIndex);
        if (mi != m_mapCacheMain.end())
            m_mapCacheMain.erase(mi);
    }
}
void CBitmapCache::RemoveAll()
{
    for (int i = 0; i < NUMBER_OF_QUICK_CACHE; i++)
    {
        std::fill(m_QuickCache[i].bitmaps.begin(), m_QuickCache[i].bitmaps.end(), nullptr);
    }
    m_mapCacheMain.clear();
    m_mapCachePlayer.clear();
    m_mapCacheInterface.clear();
    m_mapCacheEffect.clear();
}

size_t CBitmapCache::GetCacheSize()
{
    return m_mapCacheMain.size() + m_mapCachePlayer.size() + m_mapCacheInterface.size() +
           m_mapCacheEffect.size();
}

void CBitmapCache::Update(CTimer2::StartTickTime &startTickTime, CmuConsoleDebug &consoleDebug)
{
    m_ManageTimer.UpdateTime(startTickTime);

    if (m_ManageTimer.IsTime())
    {
        auto mi = m_mapCacheMain.begin();
        for (; mi != m_mapCacheMain.end();)
        {
            BITMAP_t *pBitmap = (*mi).second;
            if (pBitmap->dwCallCount > 0)
            {
                pBitmap->dwCallCount = 0;
                mi++;
            }
            else
            {
                mi = m_mapCacheMain.erase(mi);
            }
        }

        mi = m_mapCachePlayer.begin();
        for (; mi != m_mapCachePlayer.end();)
        {
            BITMAP_t *pBitmap = (*mi).second;

            if (pBitmap->dwCallCount > 0)
            {
                pBitmap->dwCallCount = 0;
                mi++;
            }
            else
            {
                mi = m_mapCachePlayer.erase(mi);
            }
        }

        mi = m_mapCacheInterface.begin();
        for (; mi != m_mapCacheInterface.end();)
        {
            BITMAP_t *pBitmap = (*mi).second;
            if (pBitmap->dwCallCount > 0)
            {
                pBitmap->dwCallCount = 0;
                mi++;
            }
            else
            {
                mi = m_mapCacheInterface.erase(mi);
            }
        }

        mi = m_mapCacheEffect.begin();
        for (; mi != m_mapCacheEffect.end();)
        {
            BITMAP_t *pBitmap = (*mi).second;
            if (pBitmap->dwCallCount > 0)
            {
                pBitmap->dwCallCount = 0;
                mi++;
            }
            else
            {
                mi = m_mapCacheEffect.erase(mi);
            }
        }

#ifdef DEBUG_BITMAP_CACHE
        consoleDebug.Write(MCD_NORMAL, L"M,P,I,E : (%d, %d, %d, %d)", m_mapCacheMain.size(),
                           m_mapCachePlayer.size(), m_mapCacheInterface.size(),
                           m_mapCacheEffect.size());
#endif // DEBUG_BITMAP_CACHE
    }
}

bool CBitmapCache::Find(std::uint32_t uiBitmapIndex, BITMAP_t **ppBitmap)
{
    for (int i = 0; i < NUMBER_OF_QUICK_CACHE; i++)
    {
        if (uiBitmapIndex >= m_QuickCache[i].dwBitmapIndexMin &&
            uiBitmapIndex < m_QuickCache[i].dwBitmapIndexMax)
        {
            const auto dwVI =
                static_cast<std::size_t>(uiBitmapIndex - m_QuickCache[i].dwBitmapIndexMin);
            BITMAP_t *cached = m_QuickCache[i].bitmaps[dwVI];
            if (cached != nullptr)
            {
                *ppBitmap = (cached == m_pNullBitmap) ? nullptr : cached;
                return true;
            }
            return false;
        }
    }

    if (BITMAP_PLAYER_TEXTURE_BEGIN <= uiBitmapIndex && BITMAP_PLAYER_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCachePlayer.find(uiBitmapIndex);
        if (mi != m_mapCachePlayer.end())
        {
            *ppBitmap = (*mi).second;
            (*ppBitmap)->dwCallCount++;
            return true;
        }
    }
    else if (BITMAP_INTERFACE_TEXTURE_BEGIN <= uiBitmapIndex &&
             BITMAP_INTERFACE_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCacheInterface.find(uiBitmapIndex);
        if (mi != m_mapCacheInterface.end())
        {
            *ppBitmap = (*mi).second;
            (*ppBitmap)->dwCallCount++;
            return true;
        }
    }
    else if (BITMAP_EFFECT_TEXTURE_BEGIN <= uiBitmapIndex &&
             BITMAP_EFFECT_TEXTURE_END >= uiBitmapIndex)
    {
        auto mi = m_mapCacheEffect.find(uiBitmapIndex);
        if (mi != m_mapCacheEffect.end())
        {
            *ppBitmap = (*mi).second;
            (*ppBitmap)->dwCallCount++;
            return true;
        }
    }
    else
    {
        auto mi = m_mapCacheMain.find(uiBitmapIndex);
        if (mi != m_mapCacheMain.end())
        {
            *ppBitmap = (*mi).second;
            (*ppBitmap)->dwCallCount++;
            return true;
        }
    }
    return false;
}

CGlobalBitmap::CGlobalBitmap()
{
    Init();
    m_BitmapCache.Create();

#ifdef DEBUG_BITMAP_CACHE
    m_DebugOutputTimer.SetTimer(5000);
#endif // DEBUG_BITMAP_CACHE
}
CGlobalBitmap::~CGlobalBitmap()
{
    ReleaseImages(nullptr);
}
void CGlobalBitmap::Init()
{
    m_uiAlternate = 0;
    m_uiTextureIndexStream = BITMAP_NONAMED_TEXTURES_BEGIN;
    m_dwUsedTextureMemory = 0;
}

std::uint32_t CGlobalBitmap::LoadImage(const std::wstring &filename, CErrorReport &errorReport,
                                       LegacyTextureFilter uiFilter, LegacyTextureWrap uiWrapMode)
{
    if (!IsValid(uiFilter) || !IsValid(uiWrapMode))
    {
        return BITMAP_UNKNOWN;
    }
    const std::size_t sharedSlot = SharedBitmapSlot(uiFilter, uiWrapMode);
    auto ownerAsset = decodedOwnerAssets_.find(filename);
    if (ownerAsset != decodedOwnerAssets_.end() &&
        ownerAsset->second.sharedBitmapIndices[sharedSlot].has_value())
    {
        const auto bitmap = m_mapBitmap.find(*ownerAsset->second.sharedBitmapIndices[sharedSlot]);
        if (bitmap != m_mapBitmap.end() && bitmap->second->Pixels == ownerAsset->second.pixels &&
            bitmap->second->Sampler == RenderSamplerIntent{uiFilter, uiWrapMode})
        {
            ownerAsset->second.unusedSinceMilliseconds = 0;
            if (!ownerAsset->second.ownerRetainedBitmapSlots[sharedSlot])
            {
                ++bitmap->second->Ref;
                ownerAsset->second.ownerRetainedBitmapSlots[sharedSlot] = true;
            }
            ++bitmap->second->Ref;
            return bitmap->second->BitmapIndex;
        }
        ownerAsset->second.sharedBitmapIndices[sharedSlot].reset();
        ownerAsset->second.ownerRetainedBitmapSlots[sharedSlot] = false;
    }

    std::uint32_t uiNewTextureIndex = GenerateTextureIndex();
    if (true == LoadImage(uiNewTextureIndex, filename, errorReport, uiFilter, uiWrapMode))
    {
        ownerAsset = decodedOwnerAssets_.find(filename);
        if (ownerAsset == decodedOwnerAssets_.end())
        {
            UnloadImage(uiNewTextureIndex, true);
            return BITMAP_UNKNOWN;
        }
        ownerAsset->second.sharedBitmapIndices[sharedSlot] = uiNewTextureIndex;
        ownerAsset->second.ownerRetainedBitmapSlots[sharedSlot] = true;
        ownerAsset->second.unusedSinceMilliseconds = 0;
        ++m_mapBitmap.at(uiNewTextureIndex)->Ref;
        m_listNonamedIndex.push_back(uiNewTextureIndex);
        return uiNewTextureIndex;
    }
    return BITMAP_UNKNOWN;
}

std::uint32_t CGlobalBitmap::LoadSessionImage(const std::wstring &filename,
                                              CErrorReport &errorReport,
                                              LegacyTextureFilter uiFilter,
                                              LegacyTextureWrap uiWrapMode)
{
    if (!IsValid(uiFilter) || !IsValid(uiWrapMode))
    {
        return BITMAP_UNKNOWN;
    }
    const std::uint32_t index = GenerateTextureIndex();
    if (!LoadImage(index, filename, errorReport, uiFilter, uiWrapMode))
    {
        return BITMAP_UNKNOWN;
    }
    m_listNonamedIndex.push_back(index);
    return index;
}

bool CGlobalBitmap::LoadImage(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                              CErrorReport &errorReport, LegacyTextureFilter uiFilter,
                              LegacyTextureWrap uiWrapMode)
{
    if (!IsValid(uiFilter) || !IsValid(uiWrapMode))
    {
        return false;
    }
    auto mi = m_mapBitmap.find(uiBitmapIndex);
    if (mi != m_mapBitmap.end())
    {
        BITMAP_t *pBitmap = mi->second.get();
        if (pBitmap->Ref > 0)
        {
            if (0 == _wcsicmp(pBitmap->FileName, filename.c_str()))
            {
                pBitmap->Ref++;
                return true;
            }
            else
            {
                errorReport.Write(L"File not found %ls (%d)->%ls\r\n", pBitmap->FileName,
                                  uiBitmapIndex, filename.c_str());
            }
        }
    }

    const auto cached = decodedOwnerAssets_.find(filename);
    if (cached != decodedOwnerAssets_.end())
    {
        cached->second.unusedSinceMilliseconds = 0;
        return CommitDecodedBitmap(uiBitmapIndex, filename, cached->second.width,
                                   cached->second.height, cached->second.components, uiFilter,
                                   uiWrapMode, cached->second.pixels);
    }

    std::wstring ext;
    SplitExt(filename, ext, false);

    bool loaded = false;
    if (0 == _wcsicmp(ext.c_str(), L"jpg"))
        loaded = OpenJpegTurbo(uiBitmapIndex, filename, errorReport, uiFilter, uiWrapMode);
    else if (0 == _wcsicmp(ext.c_str(), L"tga"))
        loaded = OpenTga(uiBitmapIndex, filename, uiFilter, uiWrapMode);

    if (!loaded)
    {
        return false;
    }
    if (!CacheDecodedOwnerAsset(filename, uiBitmapIndex))
    {
        UnloadImage(uiBitmapIndex, true);
        return false;
    }
    return true;
}

bool CGlobalBitmap::LoadNamedImage(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                                   CErrorReport &errorReport, LegacyTextureFilter uiFilter,
                                   LegacyTextureWrap uiWrapMode)
{
    return LoadImage(uiBitmapIndex, filename, errorReport, uiFilter, uiWrapMode);
}

bool CGlobalBitmap::RetainImage(std::uint32_t uiBitmapIndex) noexcept
{
    const auto bitmap = m_mapBitmap.find(uiBitmapIndex);
    if (bitmap == m_mapBitmap.end() ||
        bitmap->second->Ref == (std::numeric_limits<std::uint8_t>::max)())
    {
        return false;
    }
    ++bitmap->second->Ref;
    const auto owner = decodedOwnerAssets_.find(bitmap->second->FileName);
    if (owner != decodedOwnerAssets_.end())
    {
        owner->second.unusedSinceMilliseconds = 0;
    }
    return true;
}

void CGlobalBitmap::UnloadImage(std::uint32_t uiBitmapIndex, bool bForce)
{
    auto mi = m_mapBitmap.find(uiBitmapIndex);
    if (mi != m_mapBitmap.end())
    {
        BITMAP_t *pBitmap = mi->second.get();

        if (pBitmap->Ref > 0)
        {
            --pBitmap->Ref;
        }
        if (pBitmap->Ref == 0 || bForce)
        {
            const auto memoryUsed = static_cast<std::uint32_t>(
                pBitmap->Pixels == nullptr ? 0 : pBitmap->Pixels->size());
            const bool pixelsRemain =
                IsOwnerDecodedPixels(*pBitmap) ||
                (pBitmap->Pixels != nullptr &&
                 std::any_of(m_mapBitmap.begin(), m_mapBitmap.end(),
                             [uiBitmapIndex, pBitmap](const auto &entry) {
                                 return entry.first != uiBitmapIndex &&
                                        entry.second->Pixels == pBitmap->Pixels;
                             }));
            if (!pixelsRemain)
            {
                m_dwUsedTextureMemory -= memoryUsed;
            }

            pBitmap->Pixels.reset();
            RetireLogicalAsset(pBitmap->Asset);
            const auto owner = decodedOwnerAssets_.find(pBitmap->FileName);
            if (owner != decodedOwnerAssets_.end())
            {
                const std::size_t sharedSlot =
                    SharedBitmapSlot(pBitmap->Sampler.filter, pBitmap->Sampler.wrap);
                if (owner->second.sharedBitmapIndices[sharedSlot] == uiBitmapIndex)
                {
                    owner->second.sharedBitmapIndices[sharedSlot].reset();
                    owner->second.ownerRetainedBitmapSlots[sharedSlot] = false;
                }
            }
            m_mapBitmap.erase(mi);

            if (uiBitmapIndex >= BITMAP_NONAMED_TEXTURES_BEGIN &&
                uiBitmapIndex <= BITMAP_NONAMED_TEXTURES_END)
            {
                m_listNonamedIndex.remove(uiBitmapIndex);
            }
            m_BitmapCache.Remove(uiBitmapIndex);
        }
    }
}
void CGlobalBitmap::UnloadAllImages(CErrorReport &errorReport)
{
    ReleaseImages(&errorReport);
}

void CGlobalBitmap::CollectIdleOwnerAssets(std::uint64_t nowMilliseconds,
                                           std::uint64_t idleTimeoutMilliseconds) noexcept
{
    std::vector<std::uint32_t> expiredDescriptors;
    for (auto &entry : decodedOwnerAssets_)
    {
        auto &owner = entry.second;
        bool hasOwnerDescriptor = false;
        bool hasSessionReference = false;
        for (std::size_t slot = 0; slot < owner.sharedBitmapIndices.size(); ++slot)
        {
            auto &descriptorIndex = owner.sharedBitmapIndices[slot];
            if (!descriptorIndex.has_value())
            {
                owner.ownerRetainedBitmapSlots[slot] = false;
                continue;
            }
            const auto descriptor = m_mapBitmap.find(*descriptorIndex);
            if (descriptor == m_mapBitmap.end())
            {
                descriptorIndex.reset();
                owner.ownerRetainedBitmapSlots[slot] = false;
                continue;
            }
            if (!owner.ownerRetainedBitmapSlots[slot])
            {
                continue;
            }
            hasOwnerDescriptor = true;
            hasSessionReference |= descriptor->second->Ref > 1;
        }

        const bool unusedOwnerDescriptor = hasOwnerDescriptor && !hasSessionReference;
        const bool unusedDecodedPixels = !hasOwnerDescriptor && owner.pixels.use_count() == 1;
        if (!unusedOwnerDescriptor && !unusedDecodedPixels)
        {
            owner.unusedSinceMilliseconds = 0;
            continue;
        }
        if (owner.unusedSinceMilliseconds == 0)
        {
            owner.unusedSinceMilliseconds = nowMilliseconds;
            continue;
        }
        if (nowMilliseconds - owner.unusedSinceMilliseconds < idleTimeoutMilliseconds)
        {
            continue;
        }
        for (std::size_t slot = 0; slot < owner.sharedBitmapIndices.size(); ++slot)
        {
            if (owner.ownerRetainedBitmapSlots[slot] && owner.sharedBitmapIndices[slot].has_value())
            {
                expiredDescriptors.push_back(*owner.sharedBitmapIndices[slot]);
            }
        }
    }

    for (const std::uint32_t descriptorIndex : expiredDescriptors)
    {
        UnloadImage(descriptorIndex, true);
    }

    for (auto current = decodedOwnerAssets_.begin(); current != decodedOwnerAssets_.end();)
    {
        auto &owner = current->second;
        const bool hasOwnerDescriptor = std::any_of(owner.ownerRetainedBitmapSlots.begin(),
                                                    owner.ownerRetainedBitmapSlots.end(),
                                                    [](const bool retained) { return retained; });
        if (hasOwnerDescriptor || owner.pixels.use_count() != 1)
        {
            if (!hasOwnerDescriptor)
            {
                owner.unusedSinceMilliseconds = 0;
            }
            ++current;
            continue;
        }
        if (owner.unusedSinceMilliseconds == 0 ||
            nowMilliseconds - owner.unusedSinceMilliseconds < idleTimeoutMilliseconds)
        {
            ++current;
            continue;
        }
        m_dwUsedTextureMemory -= static_cast<std::uint32_t>(owner.pixels->size());
        current = decodedOwnerAssets_.erase(current);
    }
}

void CGlobalBitmap::ReleaseImages(CErrorReport *errorReport)
{
#ifdef _DEBUG
    if (errorReport != nullptr && !m_mapBitmap.empty())
        errorReport->Write(L"Unload Images\r\n");
#endif // _DEBUG

    for (auto &pair : m_mapBitmap)
    {
        BITMAP_t *pBitmap = pair.second.get();

        pBitmap->Pixels.reset();
        RetireLogicalAsset(pBitmap->Asset);

#ifdef _DEBUG
        if (errorReport != nullptr && pBitmap->Ref > 1)
        {
            errorReport->Write(L"Bitmap %ls(RefCount= %d)\r\n", pBitmap->FileName, pBitmap->Ref);
        }
#endif // _DEBUG
    }

    m_mapBitmap.clear();
    m_listNonamedIndex.clear();
    decodedOwnerAssets_.clear();
    m_BitmapCache.RemoveAll();

    Init();
}

BITMAP_t *CGlobalBitmap::GetTexture(std::uint32_t uiBitmapIndex)
{
    BITMAP_t *pBitmap = nullptr;
    if (false == m_BitmapCache.Find(uiBitmapIndex, &pBitmap))
    {
        auto mi = m_mapBitmap.find(uiBitmapIndex);
        if (mi != m_mapBitmap.end())
            pBitmap = mi->second.get();
        m_BitmapCache.Add(uiBitmapIndex, pBitmap);
    }
    if (nullptr == pBitmap)
    {
        m_errorTexture = {};
        wcscpy(m_errorTexture.FileName, L"CGlobalBitmap::GetTexture Error!!!");
        pBitmap = &m_errorTexture;
    }
    return pBitmap;
}
BITMAP_t *CGlobalBitmap::FindTexture(std::uint32_t uiBitmapIndex)
{
    BITMAP_t *pBitmap = nullptr;
    if (false == m_BitmapCache.Find(uiBitmapIndex, &pBitmap))
    {
        auto mi = m_mapBitmap.find(uiBitmapIndex);
        if (mi != m_mapBitmap.end())
            pBitmap = mi->second.get();
        if (pBitmap != nullptr)
            m_BitmapCache.Add(uiBitmapIndex, pBitmap);
    }
    return pBitmap;
}

BITMAP_t *CGlobalBitmap::FindTexture(const std::wstring &filename)
{
    for (auto &pair : m_mapBitmap)
    {
        BITMAP_t *pBitmap = pair.second.get();
        if (0 == wcsicmp(filename.c_str(), pBitmap->FileName))
            return pBitmap;
    }
    return nullptr;
}

BITMAP_t *CGlobalBitmap::FindTextureByName(const std::wstring &name)
{
    for (auto &pair : m_mapBitmap)
    {
        BITMAP_t *pBitmap = pair.second.get();
        std::wstring texname;
        SplitFileName(pBitmap->FileName, texname, true);
        if (0 == wcsicmp(texname.c_str(), name.c_str()))
            return pBitmap;
    }
    return nullptr;
}

namespace
{
std::optional<LogicalRenderAssetMetadata> CopyBitmapMetadata(const BITMAP_t &bitmap) noexcept
{
    try
    {
        const auto end = std::find(std::begin(bitmap.FileName), std::end(bitmap.FileName), L'\0');
        if (end == std::end(bitmap.FileName))
        {
            return std::nullopt;
        }
        wchar_t filePart[_MAX_FNAME] = {};
        _wsplitpath(bitmap.FileName, nullptr, nullptr, filePart, nullptr);
        const bool isSkin = bitmap.IsSkin || _wcsnicmp(filePart, L"ski", 3) == 0 ||
                            _wcsnicmp(filePart, L"level", 5) == 0;
        const bool isHair = bitmap.IsHair || _wcsnicmp(filePart, L"hair", 4) == 0;
        return LogicalRenderAssetMetadata{
            bitmap.BitmapIndex,
            std::wstring(std::begin(bitmap.FileName), end),
            bitmap.Width,
            bitmap.Height,
            bitmap.Components,
            bitmap.Asset,
            bitmap.Sampler,
            bitmap.Ref,
            isSkin,
            isHair,
        };
    }
    catch (...)
    {
        return std::nullopt;
    }
}
} // namespace

std::optional<LogicalRenderAssetMetadata> CGlobalBitmap::TryDescribe(
    std::uint32_t uiBitmapIndex) const noexcept
{
    const auto found = m_mapBitmap.find(uiBitmapIndex);
    return found == m_mapBitmap.end() ? std::nullopt : CopyBitmapMetadata(*found->second);
}

std::optional<LogicalRenderAssetMetadata> CGlobalBitmap::TryDescribe(
    const std::wstring &filename) const noexcept
{
    for (const auto &[index, bitmap] : m_mapBitmap)
    {
        (void)index;
        if (_wcsicmp(bitmap->FileName, filename.c_str()) == 0)
        {
            return CopyBitmapMetadata(*bitmap);
        }
    }
    return std::nullopt;
}

std::optional<LogicalRenderAssetMetadata> CGlobalBitmap::TryDescribeByName(
    const std::wstring &name) const noexcept
{
    for (const auto &[index, bitmap] : m_mapBitmap)
    {
        (void)index;
        std::wstring textureName;
        wchar_t filePart[_MAX_FNAME] = {};
        wchar_t extension[_MAX_EXT] = {};
        _wsplitpath(bitmap->FileName, nullptr, nullptr, filePart, extension);
        textureName = filePart;
        textureName += extension;
        if (_wcsicmp(textureName.c_str(), name.c_str()) == 0)
        {
            return CopyBitmapMetadata(*bitmap);
        }
    }
    return std::nullopt;
}

std::uint32_t CGlobalBitmap::GetUsedTextureMemory() const
{
    return m_dwUsedTextureMemory;
}
size_t CGlobalBitmap::GetNumberOfTexture() const
{
    return m_mapBitmap.size();
}

void CGlobalBitmap::Manage(CTimer2::StartTickTime &startTickTime, CmuConsoleDebug &consoleDebug)
{
#ifdef DEBUG_BITMAP_CACHE
    m_DebugOutputTimer.UpdateTime(startTickTime);
    if (m_DebugOutputTimer.IsTime())
    {
        consoleDebug.Write(MCD_NORMAL, L"CacheSize=%d(NumberOfTexture=%d)",
                           m_BitmapCache.GetCacheSize(), GetNumberOfTexture());
    }
#endif // DEBUG_BITMAP_CACHE
    m_BitmapCache.Update(startTickTime, consoleDebug);
}

std::uint32_t CGlobalBitmap::GenerateTextureIndex()
{
    std::uint32_t uiAvailableTextureIndex = FindAvailableTextureIndex(m_uiTextureIndexStream);
    if (uiAvailableTextureIndex >= BITMAP_NONAMED_TEXTURES_END)
    {
        m_uiAlternate++;
        m_uiTextureIndexStream = BITMAP_NONAMED_TEXTURES_BEGIN;
        uiAvailableTextureIndex = FindAvailableTextureIndex(m_uiTextureIndexStream);
    }
    return m_uiTextureIndexStream = uiAvailableTextureIndex;
}
std::uint32_t CGlobalBitmap::FindAvailableTextureIndex(std::uint32_t uiSeed)
{
    if (m_uiAlternate > 0)
    {
        auto li = std::find(m_listNonamedIndex.begin(), m_listNonamedIndex.end(), uiSeed + 1);
        if (li != m_listNonamedIndex.end())
            return FindAvailableTextureIndex(uiSeed + 1);
    }
    return uiSeed + 1;
}

bool CGlobalBitmap::CommitDecodedBitmap(
    std::uint32_t uiBitmapIndex, const std::wstring &filename, std::uint32_t width,
    std::uint32_t height, char components, LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
    std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    const RenderSamplerIntent sampler{filter, wrapMode};
    if (width == 0 || height == 0 || !IsValid(sampler) || rgba8 == nullptr ||
        !IsExactRgba8ByteCount(width, height, rgba8->size()))
    {
        return false;
    }

    try
    {
        const auto existing = m_mapBitmap.find(uiBitmapIndex);
        LogicalRenderAssetRef asset;
        if (existing != m_mapBitmap.end())
        {
            if (!IsValid(existing->second->Asset) ||
                existing->second->Asset.revision == (std::numeric_limits<std::uint64_t>::max)())
            {
                return false;
            }
            asset = {existing->second->Asset.id, existing->second->Asset.revision + 1};
        }
        else
        {
            const std::optional<LogicalRenderAssetId> id = AllocateDynamicIdentity();
            if (!id.has_value())
            {
                return false;
            }
            asset = {*id, 1};
        }

        auto replacement = std::make_unique<BITMAP_t>();
        replacement->BitmapIndex = uiBitmapIndex;
        wcsncpy(replacement->FileName, filename.c_str(), MAX_BITMAP_FILE_NAME - 1);
        replacement->FileName[MAX_BITMAP_FILE_NAME - 1] = L'\0';
        replacement->Width = static_cast<float>(width);
        replacement->Height = static_cast<float>(height);
        replacement->Components = components;
        replacement->Asset = asset;
        replacement->Sampler = sampler;
        replacement->Ref = 1;
        replacement->Pixels = rgba8;

        const auto memoryUsed = static_cast<std::uint32_t>(replacement->Pixels->size());
        const bool newPixelsAlreadyCounted = std::any_of(
            m_mapBitmap.begin(), m_mapBitmap.end(),
            [uiBitmapIndex, &replacement](const auto &entry) {
                return entry.first != uiBitmapIndex && entry.second->Pixels == replacement->Pixels;
            });
        const bool newOwnerPixels = [&]() noexcept {
            const auto owner = decodedOwnerAssets_.find(filename);
            return owner != decodedOwnerAssets_.end() &&
                   owner->second.pixels == replacement->Pixels;
        }();
        const bool oldPixelsRemain =
            existing != m_mapBitmap.end() &&
            (IsOwnerDecodedPixels(*existing->second) ||
             (existing->second->Pixels != nullptr &&
              std::any_of(m_mapBitmap.begin(), m_mapBitmap.end(),
                          [uiBitmapIndex, &existing](const auto &entry) {
                              return entry.first != uiBitmapIndex &&
                                     entry.second->Pixels == existing->second->Pixels;
                          })));
        type_bitmap_map::iterator inserted = m_mapBitmap.end();
        if (existing == m_mapBitmap.end())
        {
            const auto result = m_mapBitmap.emplace(uiBitmapIndex, std::move(replacement));
            if (!result.second)
            {
                return false;
            }
            inserted = result.first;
        }

        if (!m_assetCatalog.CommitOwnerProducedRevision(asset, width, height, sampler, rgba8))
        {
            if (inserted != m_mapBitmap.end())
            {
                m_mapBitmap.erase(inserted);
            }
            return false;
        }

        if (existing == m_mapBitmap.end())
        {
            if (!newPixelsAlreadyCounted && !newOwnerPixels)
            {
                m_dwUsedTextureMemory += memoryUsed;
            }
        }
        else
        {
            const auto oldMemory = static_cast<std::uint32_t>(
                existing->second->Pixels == nullptr ? 0 : existing->second->Pixels->size());
            m_BitmapCache.Remove(uiBitmapIndex);
            existing->second = std::move(replacement);
            if (!oldPixelsRemain)
            {
                m_dwUsedTextureMemory -= oldMemory;
            }
            if (!newPixelsAlreadyCounted && !newOwnerPixels)
            {
                m_dwUsedTextureMemory += memoryUsed;
            }
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool CGlobalBitmap::CacheDecodedOwnerAsset(const std::wstring &filename,
                                           std::uint32_t bitmapIndex) noexcept
{
    const auto bitmap = m_mapBitmap.find(bitmapIndex);
    if (bitmap == m_mapBitmap.end() || bitmap->second->Pixels == nullptr)
    {
        return false;
    }
    try
    {
        DecodedBitmapAsset asset{static_cast<std::uint32_t>(bitmap->second->Width),
                                 static_cast<std::uint32_t>(bitmap->second->Height),
                                 bitmap->second->Components,
                                 bitmap->second->Pixels,
                                 {},
                                 {},
                                 {}};
        const std::size_t sharedSlot =
            SharedBitmapSlot(bitmap->second->Sampler.filter, bitmap->second->Sampler.wrap);
        asset.sharedBitmapIndices[sharedSlot] = bitmapIndex;
        return decodedOwnerAssets_.try_emplace(filename, std::move(asset)).second;
    }
    catch (...)
    {
        return false;
    }
}

bool CGlobalBitmap::IsOwnerDecodedPixels(const BITMAP_t &bitmap) const noexcept
{
    if (bitmap.Pixels == nullptr)
    {
        return false;
    }
    const auto owner = decodedOwnerAssets_.find(bitmap.FileName);
    return owner != decodedOwnerAssets_.end() && owner->second.pixels == bitmap.Pixels;
}

bool CGlobalBitmap::OpenJpegTurbo(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                                  CErrorReport &errorReport, LegacyTextureFilter uiFilter,
                                  LegacyTextureWrap uiWrapMode)
{
    std::wstring filename_ozj;
    ExchangeExt(filename, L"OZJ", filename_ozj);

    std::ifstream compressedFile(NarrowPath(filename_ozj), std::ios::binary);
    if (!compressedFile)
    {
        return false;
    }

    std::vector<unsigned char> jpegBuf((std::istreambuf_iterator<char>(compressedFile)),
                                       std::istreambuf_iterator<char>());
    compressedFile.close();

    if (jpegBuf.size() <= 24)
    {
        return false;
    }

    // Skip first 24 bytes (OZJ header)
    const unsigned char *jpegData = jpegBuf.data() + 24;
    const auto jpegSize = static_cast<unsigned long>(jpegBuf.size() - 24);

    int jpegWidth = 0, jpegHeight = 0;
    int jpegSubsamp = TJSAMP_444;
    int jpegColorspace = TJCS_RGB;

    TurboJpegHandle tjHandle;
    if (!tjHandle.valid())
    {
        ReportTurboError(errorReport, L"tjInitDecompress");
        return false;
    }

    auto headerResult = tjDecompressHeader3(tjHandle.get(), jpegData, jpegSize, &jpegWidth,
                                            &jpegHeight, &jpegSubsamp, &jpegColorspace);
    if (headerResult != 0 || jpegWidth <= 0 || jpegHeight <= 0)
    {
        ReportTurboError(errorReport, L"tjDecompressHeader3");
        return false;
    }

    const std::size_t jpegPixels =
        static_cast<std::size_t>(jpegWidth) * static_cast<std::size_t>(jpegHeight);
    if (jpegPixels > (std::numeric_limits<std::size_t>::max)() / 3U)
    {
        return false;
    }
    std::vector<unsigned char> decompressedBuffer(jpegPixels * 3U);
    auto decompressResult =
        tjDecompress2(tjHandle.get(), jpegData, jpegSize, decompressedBuffer.data(), jpegWidth, 0,
                      jpegHeight, TJPF_RGB, TJFLAG_FASTDCT);
    if (decompressResult != 0)
    {
        ReportTurboError(errorReport, L"tjDecompress2");
        return false;
    }

    const std::optional<int> textureWidth = NextPowerOfTwo(jpegWidth);
    const std::optional<int> textureHeight = NextPowerOfTwo(jpegHeight);
    if (!textureWidth.has_value() || !textureHeight.has_value() ||
        static_cast<std::size_t>(*textureWidth) > (std::numeric_limits<std::size_t>::max)() /
                                                      static_cast<std::size_t>(*textureHeight) / 3U)
    {
        return false;
    }

    // Pad to the power-of-two texture size with zero, matching the legacy
    // upload buffer's zero-initialized padding.
    std::vector<unsigned char> paddedRgb(
        static_cast<std::size_t>(*textureWidth) * static_cast<std::size_t>(*textureHeight) * 3U, 0);
    const std::size_t jpegRowSize = static_cast<std::size_t>(jpegWidth) * 3U;
    const std::size_t textureRowSize = static_cast<std::size_t>(*textureWidth) * 3U;
    const int rows = (std::min)(jpegHeight, *textureHeight);

    std::size_t offset = 0;
    if (jpegWidth != *textureWidth)
    {
        for (int row = 0; row < rows; ++row)
        {
            memcpy(&paddedRgb[offset],
                   &decompressedBuffer[static_cast<std::size_t>(row) * jpegRowSize],
                   static_cast<std::size_t>(jpegRowSize));
            offset += static_cast<std::size_t>(textureRowSize);
        }
    }
    else
    {
        memcpy(paddedRgb.data(), decompressedBuffer.data(),
               static_cast<std::size_t>(jpegHeight) * static_cast<std::size_t>(jpegWidth) * 3u);
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(*textureWidth) * static_cast<std::size_t>(*textureHeight);
    return CommitDecodedBitmap(
        uiBitmapIndex, filename, static_cast<std::uint32_t>(*textureWidth),
        static_cast<std::uint32_t>(*textureHeight), 3, uiFilter, uiWrapMode,
        std::make_shared<const std::vector<std::byte>>(ExpandRgbToRgba8(paddedRgb, pixelCount)));
}

bool CGlobalBitmap::OpenTga(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                            LegacyTextureFilter uiFilter, LegacyTextureWrap uiWrapMode)
{
    std::ifstream input(NarrowPath(filename), std::ios::binary);
    if (!input)
        return OpenLegacyOzt(uiBitmapIndex, filename, uiFilter, uiWrapMode);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                           std::istreambuf_iterator<char>());
    auto image = Render::Textures::DecodeTga(bytes);
    if (!image)
        return false;
    return CommitDecodedBitmap(
        uiBitmapIndex, filename, image->width, image->height, 4, uiFilter, uiWrapMode,
        std::make_shared<const std::vector<std::byte>>(std::move(image->rgba)));
}

bool CGlobalBitmap::OpenLegacyOzt(std::uint32_t uiBitmapIndex, const std::wstring &filename,
                                  LegacyTextureFilter uiFilter, LegacyTextureWrap uiWrapMode)
{
    std::wstring filename_ozt;
    ExchangeExt(filename, L"OZT", filename_ozt);

    std::ifstream input(NarrowPath(filename_ozt), std::ios::binary);
    if (!input)
    {
        return false;
    }

    std::vector<unsigned char> pakBuffer((std::istreambuf_iterator<char>(input)),
                                         std::istreambuf_iterator<char>());
    input.close();

    constexpr std::size_t HeaderBytes = 22;
    if (pakBuffer.size() < HeaderBytes)
    {
        return false;
    }

    std::size_t index = 12;
    index += 4;
    std::int16_t nx, ny;
    std::memcpy(&nx, &pakBuffer[index], sizeof(nx));
    index += 2;
    std::memcpy(&ny, &pakBuffer[index], sizeof(ny));
    index += 2;
    const char bit = pakBuffer[index];
    index += 1;
    index += 1;

    if (bit != 32 || nx <= 0 || ny <= 0)
    {
        return false;
    }

    const std::size_t sourceBytes =
        static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) * 4u;
    if (sourceBytes > pakBuffer.size() - index)
    {
        return false;
    }

    const std::optional<int> width = NextPowerOfTwo(nx);
    const std::optional<int> height = NextPowerOfTwo(ny);
    if (!width.has_value() || !height.has_value() ||
        static_cast<std::size_t>(*width) >
            (std::numeric_limits<std::size_t>::max)() / static_cast<std::size_t>(*height) / 4U)
    {
        return false;
    }
    const int Width = *width;
    const int Height = *height;

    // Zero-padded to the power-of-two texture size, matching the legacy
    // upload buffer's zero-initialized padding.
    constexpr int Components = 4;
    std::vector<std::byte> rgba(
        static_cast<std::size_t>(Width) * static_cast<std::size_t>(Height) * 4u, std::byte{0});

    for (int y = 0; y < ny; y++)
    {
        const unsigned char *src = &pakBuffer[index];
        index += nx * 4;
        std::byte *dst = &rgba[static_cast<std::size_t>(ny - 1 - y) * Width * Components];

        for (int x = 0; x < nx; x++)
        {
            dst[0] = static_cast<std::byte>(src[2]);
            dst[1] = static_cast<std::byte>(src[1]);
            dst[2] = static_cast<std::byte>(src[0]);
            dst[3] = static_cast<std::byte>(src[3]);
            src += 4;
            dst += Components;
        }
    }

    return CommitDecodedBitmap(uiBitmapIndex, filename, static_cast<std::uint32_t>(Width),
                               static_cast<std::uint32_t>(Height), Components, uiFilter, uiWrapMode,
                               std::make_shared<const std::vector<std::byte>>(std::move(rgba)));
}

// Thin forwarders to the portable catalog (PLAN_P1R5.1.md section 5.2); see
// LogicalRenderAssetCatalog for the actual identity/revision logic.
bool CGlobalBitmap::SetTrustedAssetProducerThread(std::thread::id thread) noexcept
{
    return m_assetCatalog.SetTrustedProducerThread(thread);
}

std::optional<LogicalRenderAssetLease> CGlobalBitmap::TryLease(
    LogicalRenderAssetRef asset) const noexcept
{
    return m_assetCatalog.TryLease(asset);
}

bool CGlobalBitmap::IsCurrentAsset(LogicalRenderAssetRef asset) const noexcept
{
    return m_assetCatalog.IsCurrent(asset);
}

std::uint64_t CGlobalBitmap::AssetCatalogGeneration() const noexcept
{
    return m_assetCatalog.Generation();
}

std::optional<LogicalRenderAssetId> CGlobalBitmap::AllocateDynamicIdentity() noexcept
{
    return m_assetCatalog.AllocateDynamicIdentity();
}

bool CGlobalBitmap::CommitRecordedRevisions(
    std::span<const LogicalRenderAssetRevisionInput> inputs) noexcept
{
    if (!m_assetCatalog.CommitRecordedRevisions(inputs))
    {
        return false;
    }
    for (const LogicalRenderAssetRevisionInput &input : inputs)
    {
        for (auto &[index, bitmap] : m_mapBitmap)
        {
            (void)index;
            if (bitmap->Asset.id == input.asset.id &&
                bitmap->Asset.revision != (std::numeric_limits<std::uint64_t>::max)() &&
                bitmap->Asset.revision + 1 == input.asset.revision)
            {
                bitmap->Asset = input.asset;
                if (const auto lease = m_assetCatalog.TryLease(input.asset))
                {
                    bitmap->Pixels = lease->bytes;
                    bitmap->Sampler = input.sampler;
                }
            }
        }
    }
    return true;
}

bool CGlobalBitmap::RetireLogicalAsset(LogicalRenderAssetRef asset) noexcept
{
    return m_assetCatalog.RetireLogicalAsset(asset);
}

bool CGlobalBitmap::CommitOwnerProducedRevision(
    LogicalRenderAssetRef asset, std::uint32_t width, std::uint32_t height,
    RenderSamplerIntent sampler, std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    if (!m_assetCatalog.CommitOwnerProducedRevision(asset, width, height, sampler, rgba8))
    {
        return false;
    }
    for (auto &[index, bitmap] : m_mapBitmap)
    {
        (void)index;
        if (bitmap->Asset.id == asset.id &&
            bitmap->Asset.revision != (std::numeric_limits<std::uint64_t>::max)() &&
            bitmap->Asset.revision + 1 == asset.revision)
        {
            bitmap->Asset = asset;
            bitmap->Pixels = rgba8;
            bitmap->Sampler = sampler;
        }
    }
    return true;
}

void CGlobalBitmap::SplitFileName(IN const std::wstring &filepath, OUT std::wstring &filename,
                                  bool bIncludeExt)
{
    wchar_t __fname[_MAX_FNAME] = {
        0,
    };
    wchar_t __ext[_MAX_EXT] = {
        0,
    };
    _wsplitpath(filepath.c_str(), NULL, NULL, __fname, __ext);
    filename = __fname;
    if (bIncludeExt)
        filename += __ext;
}
void CGlobalBitmap::SplitExt(IN const std::wstring &filepath, OUT std::wstring &ext,
                             bool bIncludeDot)
{
    wchar_t __ext[_MAX_EXT] = {
        0,
    };
    _wsplitpath(filepath.c_str(), NULL, NULL, NULL, __ext);
    if (bIncludeDot)
    {
        ext = __ext;
    }
    else
    {
        if ((__ext[0] == '.') && __ext[1])
            ext = __ext + 1;
    }
}
void CGlobalBitmap::ExchangeExt(IN const std::wstring &in_filepath, IN const std::wstring &ext,
                                OUT std::wstring &out_filepath)
{
    wchar_t __drive[_MAX_DRIVE] = {
        0,
    };
    wchar_t __dir[_MAX_DIR] = {
        0,
    };
    wchar_t __fname[_MAX_FNAME] = {
        0,
    };
    _wsplitpath(in_filepath.c_str(), __drive, __dir, __fname, NULL);

    out_filepath = __drive;
    out_filepath += __dir;
    out_filepath += __fname;
    out_filepath += '.';
    out_filepath += ext;
}

bool CGlobalBitmap::Convert_Format(const std::wstring &filename)
{
    wchar_t drive[_MAX_DRIVE];
    wchar_t dir[_MAX_DIR];
    wchar_t fname[_MAX_FNAME];
    wchar_t ext[_MAX_EXT];

    ::_wsplitpath(filename.c_str(), drive, dir, fname, ext);

    std::wstring strPath = drive;
    strPath += dir;
    std::wstring strName = fname;

    if (_wcsicmp(ext, L".jpg") == 0)
    {
        auto strSaveName = strPath + strName + L".OZJ";
        return Save_Image(filename, strSaveName.c_str(), 24);
    }
    else if (_wcsicmp(ext, L".tga") == 0)
    {
        auto strSaveName = strPath + strName + L".OZT";
        return Save_Image(filename, strSaveName.c_str(), 4);
    }
    else if (_wcsicmp(ext, L".bmp") == 0)
    {
        auto strSaveName = strPath + strName + L".OZB";
        return Save_Image(filename, strSaveName.c_str(), 4);
    }
    else
    {
    }

    return false;
}

bool CGlobalBitmap::Save_Image(const std::wstring &src, const std::wstring &dest, int cDumpHeader)
{
    const auto srcPath = NarrowPath(src);
    const auto destPath = NarrowPath(dest);

    std::ifstream input(srcPath, std::ios::binary);
    if (!input)
    {
        return false;
    }

    std::vector<char> buffer((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
    input.close();

    if (buffer.empty())
    {
        return false;
    }

    const auto headerBytes = static_cast<std::size_t>(std::max<int>(0, cDumpHeader));
    const auto headerCount = std::min<std::size_t>(headerBytes, buffer.size());

    std::ofstream output(destPath, std::ios::binary);
    if (!output)
    {
        return false;
    }

    output.write(buffer.data(), static_cast<std::streamsize>(headerCount));
    output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    output.flush();

    return output.good();
}

CSprite::CSprite(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
    m_aFrameTexCoord = NULL;
    m_pTexture.reset();
}

CSprite::~CSprite()
{
    Release();
}

void CSprite::Release()
{
    m_pTexture.reset();
    SAFE_DELETE_ARRAY(m_aFrameTexCoord);
}

void CSprite::Create(int nOrgWidth, int nOrgHeight, int nTexID, int nMaxFrame,
                     SFrameCoord *aFrameCoord, int nDatumX, int nDatumY, bool bTile,
                     int nSizingDatums, float fScaleX, float fScaleY)
{
    Release();

    m_fOrgWidth = (float)nOrgWidth;
    m_fOrgHeight = (float)nOrgHeight;
    m_nTexID = nTexID;
    m_pTexture = Bitmaps.FindTexture(m_nTexID);

    m_fScrHeight = (float)WindowHeight / fScaleY;

    m_aScrCoord[LT].fX = 0.0f;
    m_aScrCoord[LT].fY = m_fScrHeight;
    m_aScrCoord[LB].fX = 0.0f;
    m_aScrCoord[LB].fY = m_fScrHeight - m_fOrgHeight;
    m_aScrCoord[RB].fX = m_fOrgWidth;
    m_aScrCoord[RB].fY = m_fScrHeight - m_fOrgHeight;
    m_aScrCoord[RT].fX = m_fOrgWidth;
    m_aScrCoord[RT].fY = m_fScrHeight;

    m_nNowFrame = -1;

    if (-1 < m_nTexID && m_pTexture.has_value())
    {
        m_aTexCoord[LT].fTU = 0.5f / m_pTexture->Width;
        m_aTexCoord[LT].fTV = 0.5f / m_pTexture->Height;
        m_aTexCoord[LB].fTU = 0.5f / m_pTexture->Width;
        m_aTexCoord[LB].fTV = (m_fOrgHeight - 0.5f) / m_pTexture->Height;
        m_aTexCoord[RB].fTU = (m_fOrgWidth - 0.5f) / m_pTexture->Width;
        m_aTexCoord[RB].fTV = (m_fOrgHeight - 0.5f) / m_pTexture->Height;
        m_aTexCoord[RT].fTU = (m_fOrgWidth - 0.5f) / m_pTexture->Width;
        m_aTexCoord[RT].fTV = 0.5f / m_pTexture->Height;

        if (NULL != aFrameCoord)
        {
            _ASSERT(0 < nMaxFrame);

            m_nMaxFrame = nMaxFrame;

            m_aFrameTexCoord = new STexCoord[m_nMaxFrame];

            for (int i = 0; i < m_nMaxFrame; ++i)
            {
                m_aFrameTexCoord[i].fTU = ((float)aFrameCoord[i].nX + 0.5f) / m_pTexture->Width;
                m_aFrameTexCoord[i].fTV = ((float)aFrameCoord[i].nY + 0.5f) / m_pTexture->Height;
            }

            m_nStartFrame = m_nEndFrame = 0;
            SetNowFrame(0);
            m_bTile = false;
        }
        else
        {
            m_nMaxFrame = 0;
            m_nStartFrame = m_nEndFrame = -1;
            m_bTile = bTile;
        }
    }
    else
    {
        ::memset(m_aTexCoord, 0, sizeof(STexCoord) * POS_MAX);

        m_nMaxFrame = 0;
        m_nStartFrame = m_nEndFrame = -1;
        m_bTile = false;
    }

    m_byAlpha = m_byRed = m_byGreen = m_byBlue = 255;

    m_fDatumX = (float)nDatumX;
    m_fDatumY = (float)nDatumY;

    m_bRepeat = false;
    m_dDelayTime = 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
    m_dDeltaTickSum = 0.0;
    m_nSizingDatums = nSizingDatums;
    m_fScaleX = fScaleX;
    m_fScaleY = fScaleY;
    m_bShow = false;
}

void CSprite::Create(SImgInfo *pImgInfo, int nDatumX, int nDatumY, bool bTile, int nSizingDatums,
                     float fScaleX, float fScaleY)
{
    if (pImgInfo->nX == 0 && pImgInfo->nY == 0)
        Create(pImgInfo->nWidth, pImgInfo->nHeight, pImgInfo->nTexID, 0, NULL, nDatumX, nDatumY,
               bTile, nSizingDatums, fScaleX, fScaleY);
    else
    {
        SFrameCoord frameCoord = {pImgInfo->nX, pImgInfo->nY};
        Create(pImgInfo->nWidth, pImgInfo->nHeight, pImgInfo->nTexID, 1, &frameCoord, nDatumX,
               nDatumY, bTile, nSizingDatums, fScaleX, fScaleY);
    }
}

void CSprite::SetPosition(int nXCoord, int nYCoord, CHANGE_PRAM eChangedPram)
{
    if (eChangedPram & X)
    {
        float fWidth = m_aScrCoord[RT].fX - m_aScrCoord[LT].fX;

        if (IS_SIZING_DATUMS_R(m_nSizingDatums))
        {
            m_aScrCoord[RT].fX = m_aScrCoord[RB].fX = (float)nXCoord + m_fOrgWidth - m_fDatumX;
            m_aScrCoord[LT].fX = m_aScrCoord[LB].fX = m_aScrCoord[RT].fX - fWidth;
        }
        else
        {
            m_aScrCoord[LT].fX = m_aScrCoord[LB].fX = (float)nXCoord - m_fDatumX;
            m_aScrCoord[RT].fX = m_aScrCoord[RB].fX = m_aScrCoord[LT].fX + fWidth;
        }
    }

    if (eChangedPram & Y)
    {
        float fHeight = m_aScrCoord[LT].fY - m_aScrCoord[LB].fY;

        if (IS_SIZING_DATUMS_B(m_nSizingDatums))
        {
            m_aScrCoord[LB].fY = m_aScrCoord[RB].fY =
                m_fScrHeight - (float)nYCoord - m_fOrgHeight + m_fDatumY;

            m_aScrCoord[LT].fY = m_aScrCoord[RT].fY = m_aScrCoord[LB].fY + fHeight;
        }
        else
        {
            m_aScrCoord[LT].fY = m_aScrCoord[RT].fY = m_fScrHeight - (float)nYCoord + m_fDatumY;
            m_aScrCoord[LB].fY = m_aScrCoord[RB].fY = m_aScrCoord[LT].fY - fHeight;
        }
    }
}

void CSprite::SetSize(int nWidth, int nHeight, CHANGE_PRAM eChangedPram)
{
    if (eChangedPram & X)
    {
        if (IS_SIZING_DATUMS_R(m_nSizingDatums))
        {
            m_aScrCoord[LT].fX = m_aScrCoord[LB].fX = m_aScrCoord[RT].fX - (float)nWidth;
            if (m_bTile)
                m_aTexCoord[LT].fTU = m_aTexCoord[LB].fTU =
                    m_aTexCoord[RT].fTU - nWidth / m_pTexture->Width;
        }
        else
        {
            m_aScrCoord[RT].fX = m_aScrCoord[RB].fX = m_aScrCoord[LT].fX + (float)nWidth;
            if (m_bTile)
                m_aTexCoord[RT].fTU = m_aTexCoord[RB].fTU = nWidth / m_pTexture->Width;
        }
    }
    if (eChangedPram & Y)
    {
        if (IS_SIZING_DATUMS_B(m_nSizingDatums))
        {
            m_aScrCoord[LT].fY = m_aScrCoord[RT].fY = m_aScrCoord[LB].fY + (float)nHeight;
            if (m_bTile)
                m_aTexCoord[LT].fTV = m_aTexCoord[RT].fTV =
                    m_aTexCoord[LB].fTV - nHeight / m_pTexture->Height;
        }
        else
        {
            m_aScrCoord[LB].fY = m_aScrCoord[RB].fY = m_aScrCoord[LT].fY - (float)nHeight;
            if (m_bTile)
                m_aTexCoord[LB].fTV = m_aTexCoord[RB].fTV = nHeight / m_pTexture->Height;
        }
    }
}

BOOL CSprite::PtInSprite(long lXPos, long lYPos)
{
    if (!m_bShow)
        return FALSE;

    POINT pt = {lXPos, lYPos};

    RECT rc = {long(m_aScrCoord[LT].fX * m_fScaleX),
               long((m_fScrHeight - m_aScrCoord[LT].fY) * m_fScaleY),
               long(m_aScrCoord[RB].fX * m_fScaleX),
               long((m_fScrHeight - m_aScrCoord[RB].fY) * m_fScaleY)};

    return ::PtInRect(&rc, pt);
}

BOOL CSprite::CursorInObject()
{
    return PtInSprite(static_cast<long>(MouseX * g_fScreenRate_x),
                      static_cast<long>(MouseY * g_fScreenRate_y));
}

void CSprite::SetAction(int nStartFrame, int nEndFrame, double dDelayTime, bool bRepeat)
{
    if (1 >= m_nMaxFrame)
        return;

    _ASSERT(nStartFrame <= nEndFrame && nStartFrame >= 0 && nEndFrame < m_nMaxFrame);

    m_nStartFrame = m_nNowFrame = nStartFrame;
    m_nEndFrame = nEndFrame;
    m_bRepeat = bRepeat;
    m_dDelayTime = dDelayTime > 0.0
                       ? dDelayTime
                       : 1000.0 / sessionKeeper_.ApplicationConfig().legacyReferenceFps;
}

void CSprite::SetNowFrame(int nFrame)
{
    if (NULL == m_aFrameTexCoord || nFrame == m_nNowFrame)
        return;

    if (nFrame < m_nStartFrame || nFrame > m_nEndFrame)
        return;

    m_nNowFrame = nFrame;

    float fTUWidth = m_aTexCoord[RT].fTU - m_aTexCoord[LT].fTU;
    float fTVHeight = m_aTexCoord[LB].fTV - m_aTexCoord[LT].fTV;

    m_aTexCoord[LT] = m_aFrameTexCoord[m_nNowFrame];

    m_aTexCoord[RT].fTU = m_aFrameTexCoord[m_nNowFrame].fTU + fTUWidth;
    m_aTexCoord[RT].fTV = m_aFrameTexCoord[m_nNowFrame].fTV;

    m_aTexCoord[LB].fTU = m_aFrameTexCoord[m_nNowFrame].fTU;
    m_aTexCoord[LB].fTV = m_aFrameTexCoord[m_nNowFrame].fTV + fTVHeight;

    m_aTexCoord[RB].fTU = m_aTexCoord[RT].fTU;
    m_aTexCoord[RB].fTV = m_aTexCoord[LB].fTV;
}

void CSprite::Update(double dDeltaTick)
{
    if (!m_bShow)
        return;

    if (1 >= m_nMaxFrame)
        return;

    m_dDeltaTickSum += dDeltaTick;

    if (m_dDeltaTickSum >= m_dDelayTime)
    {
        const int frames = static_cast<int>(m_dDeltaTickSum / m_dDelayTime);
        const int count = m_nEndFrame - m_nStartFrame + 1;
        const int next = m_bRepeat ? m_nStartFrame + (m_nNowFrame - m_nStartFrame + frames) % count
                                   : (std::min)(m_nNowFrame + frames, m_nEndFrame);
        SetNowFrame(next);
        m_dDeltaTickSum -= frames * m_dDelayTime;
    }
}

void CSprite::Render()
{
    if (!m_bShow)
        return;

    if (-1 < m_nTexID)
    {
        if (!TextureEnable)
        {
            TextureEnable = true;
            glEnable(GL_TEXTURE_2D);
        }

        BindTexture(m_nTexID);

        glBegin(GL_TRIANGLE_FAN);

        glColor4ub(m_byRed, m_byGreen, m_byBlue, m_byAlpha);

        for (int i = LT; i < POS_MAX; ++i)
        {
            glTexCoord2f(m_aTexCoord[i].fTU, m_aTexCoord[i].fTV);
            glVertex2f(m_aScrCoord[i].fX * m_fScaleX, m_aScrCoord[i].fY * m_fScaleY);
        }

        glEnd();
    }
    else
    {
        if (TextureEnable)
        {
            TextureEnable = false;
            glDisable(GL_TEXTURE_2D);
        }

        glBegin(GL_TRIANGLE_FAN);

        glColor4ub(m_byRed, m_byGreen, m_byBlue, m_byAlpha);
        for (int i = LT; i < POS_MAX; ++i)
            glVertex2f(m_aScrCoord[i].fX * m_fScaleX, m_aScrCoord[i].fY * m_fScaleY);

        glEnd();
    }
}

void TextureScript::setScript(const TextureScript &rhs)
{
    m_bBright = rhs.m_bBright;
    m_bHiddenMesh = rhs.m_bHiddenMesh;
    m_bStreamMesh = rhs.m_bStreamMesh;
    m_bNoneBlendMesh = rhs.m_bNoneBlendMesh;
    m_byShadowMesh = rhs.m_byShadowMesh;
}

bool TextureScriptParsing::parsingTScriptA(char *filename)
{
    int ch = '_';
    char str[] = "RHSN";
    char *strDest;
    char *strTokenFile;
    char strFileName[32];

    memcpy(strFileName, filename, 32);
    strTokenFile = strchr(strFileName, ch);
    if (strTokenFile != NULL)
    {
        strDest = strtok(strTokenFile, ".");
        int length = std::min<int>(5, strlen(strDest));

        int result = strcspn(strDest, str);
        if (result) //if ( m_strDest!=NULL )
        {
            for (int i = 1; i < length; ++i)
            {
                switch (strTokenFile[i])
                {
                case 'R':
                    m_bBright = true;
                    m_bBeScript = true;
                    break;

                case 'H':
                    m_bHiddenMesh = true;
                    m_bBeScript = true;
                    break;

                case 'S':
                    m_bStreamMesh = true;
                    m_bBeScript = true;
                    break;

                case 'N':
                    m_bNoneBlendMesh = true;
                    m_bBeScript = true;
                    break;

                default:
                    m_bBeScript = false;
#ifdef PJH_ADD_PANDA_CHANGERING
                    if (strcmp("mu_rgb_lights.jpg", filename) == 0)
                    {
                        m_bBright = true;
                        m_bBeScript = true;
                    }
#endif //PJH_ADD_PANDA_CHANGERING
                    return m_bBeScript;
                }
            }
        }
    }
    return m_bBeScript;
}

// WebzenScene.cpp - Webzen title/intro scene implementation

/**
 * @brief Determines which background theme to use based on configured probability.
 * @return BackgroundTheme::Classic or BackgroundTheme::Season5
 */
SplashSceneDetail::BackgroundTheme SessionRenderUnit::SelectBackgroundTheme()
{
    const int roll = rand() % SplashSceneDetail::BACKGROUND_SELECTION_PERCENTAGE;
    const bool classic = sessionKeeper_.ApplicationKeeperRef().SelectLoginBackgroundClassic(
        roll <= SplashSceneDetail::CLASSIC_BACKGROUND_PROBABILITY);
    return classic ? SplashSceneDetail::BackgroundTheme::Classic
                   : SplashSceneDetail::BackgroundTheme::Season5;
}

/**
 * @brief Loads the appropriate background image set based on theme.
 * @param theme The background theme to load (Classic or Season5)
 */
void SessionRenderUnit::LoadBackgroundTheme(SplashSceneDetail::BackgroundTheme theme)
{
    if (theme == SplashSceneDetail::BackgroundTheme::Classic)
    {
        LoadBitmapW(L"Interface\\lo_back_im01.jpg", BITMAP_TITLE + 8, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_im02.jpg", BITMAP_TITLE + 9, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_im03.jpg", BITMAP_TITLE + 10, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_im04.jpg", BITMAP_TITLE + 11, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_im05.jpg", BITMAP_TITLE + 12, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_im06.jpg", BITMAP_TITLE + 13, LegacyTextureFilter::Linear);
    }
    else
    {
        LoadBitmapW(L"Interface\\lo_back_s5_im01.jpg", BITMAP_TITLE + 8,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_s5_im02.jpg", BITMAP_TITLE + 9,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_s5_im03.jpg", BITMAP_TITLE + 10,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_s5_im04.jpg", BITMAP_TITLE + 11,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_s5_im05.jpg", BITMAP_TITLE + 12,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Interface\\lo_back_s5_im06.jpg", BITMAP_TITLE + 13,
                    LegacyTextureFilter::Linear);
    }
}

/**
 * @brief Loads all common title scene bitmaps.
 */
void SessionRenderUnit::LoadCommonTitleBitmaps()
{
    LoadBitmapW(L"Interface\\New_lo_back_01.jpg", BITMAP_TITLE, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\New_lo_back_02.jpg", BITMAP_TITLE + 1, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\lo_121518.tga", BITMAP_TITLE + 3, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\lo_lo.jpg", BITMAP_TITLE + 5, LegacyTextureFilter::Linear,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Interface\\lo_back_s5_03.jpg", BITMAP_TITLE + 6, LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\lo_back_s5_04.jpg", BITMAP_TITLE + 7, LegacyTextureFilter::Linear);
}

/**
 * @brief Unloads all title scene bitmaps.
 */
void SessionRenderUnit::UnloadTitleBitmaps()
{
    DeleteBitmap(SplashSceneDetail::TITLE_BITMAP_BASE);
    DeleteBitmap(SplashSceneDetail::TITLE_BITMAP_BACK_02);
    DeleteBitmap(SplashSceneDetail::TITLE_BITMAP_LOGO);
    DeleteBitmap(SplashSceneDetail::TITLE_BITMAP_PATTERN);

    for (int i = SplashSceneDetail::TITLE_BITMAP_DYNAMIC_START;
         i < SplashSceneDetail::TITLE_BITMAP_DYNAMIC_END; ++i)
    {
        DeleteBitmap(i);
    }
}

namespace CharacterLabelDetail
{

DWORD ResolveNameColor(std::uint8_t controlCode)
{
    if (controlCode & CTLCODE_01BLOCKCHAR)
        return ARGB(255, 0, 255, 255);
    if (controlCode & (CTLCODE_02BLOCKITEM | CTLCODE_10ACCOUNT_BLOCKITEM))
        return CLRDW_BR_ORANGE;
    if (controlCode & CTLCODE_04FORTV)
        return CLRDW_WHITE;
    if (controlCode & (CTLCODE_08OPERATOR | CTLCODE_20OPERATOR))
        return ARGB(255, 255, 0, 0);

    return CLRDW_WHITE;
}

int ResolveGuildTextIndex(std::uint8_t guildStatus)
{
    const auto it = std::lower_bound(CharacterLabelDetail::kGuildStatusTexts.begin(),
                                     CharacterLabelDetail::kGuildStatusTexts.end(), guildStatus,
                                     [](const CharacterLabelDetail::GuildStatusText &entry,
                                        std::uint8_t status) { return entry.status < status; });

    return (it != CharacterLabelDetail::kGuildStatusTexts.end() && it->status == guildStatus)
               ? it->textIndex
               : 0;
}
} // namespace CharacterLabelDetail

namespace Render::Sprites
{
int AppendPreparedSprite(SessionSpriteStorage &output, int Type, const vec3_t Position, float Scale,
                         const vec3_t Light, const OBJECT *Owner, float Rotation, int SubType)
{
    const int index = static_cast<int>(output.used);
    auto &sprite = output.Append();
    sprite.Live = true;
    sprite.Visible = true;
    sprite.Type = Type;
    sprite.SubType = SubType;
    sprite.Owner = Owner;
    sprite.AnimationFrame = 1.f;
    sprite.Scale = Scale;
    sprite.Angle[2] = Rotation;
    VectorCopy(Position, sprite.Position);
    VectorCopy(Position, sprite.StartPosition);
    VectorCopy(Light, sprite.Light);
    return index;
}
} // namespace Render::Sprites
