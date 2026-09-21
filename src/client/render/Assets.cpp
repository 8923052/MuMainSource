#include "render/Assets.h"
#include "app/ApplicationKeeper.h"
#include "domain/Guild.h"
#include "render/Sprites.h"
#include "render/Textures.h"
#include "session/SessionKeeper.h"
#include "support/CoreMath.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/session/UiSessionLogic.h"

bool LogicalRenderAssetCatalog::SetTrustedProducerThread(std::thread::id thread) noexcept
{
    if (!IsOwnerThread())
    {
        return false;
    }
    trustedProducerThread_ = thread;
    return true;
}

std::optional<LogicalRenderAssetLease> LogicalRenderAssetCatalog::TryLease(
    LogicalRenderAssetRef asset) const noexcept
{
    if (!IsOwnerThread() || !IsValid(asset))
    {
        return std::nullopt;
    }
    const auto found = catalog_.find(asset.id.value);
    if (found == catalog_.end() || found->second.retired)
    {
        return std::nullopt;
    }
    const auto revision = found->second.revisions.find(asset.revision);
    if (revision == found->second.revisions.end() || revision->second.pixels == nullptr)
    {
        return std::nullopt;
    }
    return LogicalRenderAssetLease{asset,
                                   revision->second.width,
                                   revision->second.height,
                                   RenderAssetFormat::Rgba8,
                                   revision->second.sampler,
                                   revision->second.pixels};
}

std::optional<LogicalRenderAssetId> LogicalRenderAssetCatalog::AllocateDynamicIdentity() noexcept
{
    if (!IsMutationThread() || nextAssetId_ == 0)
    {
        return std::nullopt;
    }
    const std::uint64_t id = nextAssetId_;
    try
    {
        catalog_.emplace(id, CatalogRecord{});
    }
    catch (...)
    {
        return std::nullopt;
    }
    ++nextAssetId_;
    BumpGeneration();
    return LogicalRenderAssetId{id};
}

bool LogicalRenderAssetCatalog::CommitRecordedRevisions(
    std::span<const LogicalRenderAssetRevisionInput> inputs) noexcept
{
    if (!IsOwnerThread())
    {
        return false;
    }
    std::vector<std::shared_ptr<const std::vector<std::byte>>> preparedPixels;
    try
    {
        preparedPixels.reserve(inputs.size());
        for (const LogicalRenderAssetRevisionInput &input : inputs)
        {
            if (!IsValid(input))
            {
                return false;
            }
            preparedPixels.push_back(std::make_shared<const std::vector<std::byte>>(
                input.rgba8.begin(), input.rgba8.end()));
        }
    }
    catch (...)
    {
        return false;
    }

    struct PreparedRevision final
    {
        std::uint64_t id = 0;
        std::map<std::uint64_t, CatalogRecord::RevisionRecord> revisions;
    };
    std::vector<PreparedRevision> preparedRevisions;
    {
        try
        {
            preparedRevisions.reserve(inputs.size());
            for (std::size_t i = 0; i < inputs.size(); ++i)
            {
                for (std::size_t j = i + 1; j < inputs.size(); ++j)
                {
                    if (inputs[i].asset.id == inputs[j].asset.id)
                    {
                        return false; // duplicate target within the same commit
                    }
                }
                const auto found = catalog_.find(inputs[i].asset.id.value);
                if (found == catalog_.end() || found->second.retired ||
                    found->second.currentRevision == (std::numeric_limits<std::uint64_t>::max)() ||
                    inputs[i].asset.revision != found->second.currentRevision + 1)
                {
                    return false;
                }
                PreparedRevision prepared;
                prepared.id = inputs[i].asset.id.value;
                prepared.revisions.emplace(
                    inputs[i].asset.revision,
                    CatalogRecord::RevisionRecord{inputs[i].width, inputs[i].height,
                                                  inputs[i].sampler, std::move(preparedPixels[i])});
                preparedRevisions.push_back(std::move(prepared));
            }
        }
        catch (...)
        {
            return false;
        }
    }

    for (PreparedRevision &prepared : preparedRevisions)
    {
        auto found = catalog_.find(prepared.id);
        found->second.revisions.merge(prepared.revisions);
        found->second.currentRevision = found->second.revisions.rbegin()->first;
        PruneUnleasedRevisions(found->second);
    }
    if (!preparedRevisions.empty())
    {
        BumpGeneration();
    }
    return true;
}

bool LogicalRenderAssetCatalog::RetireLogicalAsset(LogicalRenderAssetRef asset) noexcept
{
    if (!IsMutationThread() || !IsValid(asset))
    {
        return false;
    }
    const auto found = catalog_.find(asset.id.value);
    if (found != catalog_.end() && found->second.currentRevision == 0 &&
        found->second.revisions.empty())
    {
        catalog_.erase(found);
        BumpGeneration();
        return true;
    }
    if (found != catalog_.end() && found->second.currentRevision == asset.revision)
    {
        const bool wasRetired = found->second.retired;
        catalog_.erase(found);
        if (!wasRetired)
        {
            BumpGeneration();
        }
    }
    return true; // no-op for an unknown/already-superseded revision
}

bool LogicalRenderAssetCatalog::CommitOwnerProducedRevision(
    LogicalRenderAssetRef asset, std::uint32_t width, std::uint32_t height,
    RenderSamplerIntent sampler, std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    if (!IsMutationThread())
    {
        return false;
    }
    return PublishRevision(asset, width, height, sampler, std::move(rgba8));
}

bool LogicalRenderAssetCatalog::PublishRevision(
    LogicalRenderAssetRef asset, std::uint32_t width, std::uint32_t height,
    RenderSamplerIntent sampler, std::shared_ptr<const std::vector<std::byte>> pixels) noexcept
{
    if (!IsValid(asset) || pixels == nullptr || width == 0 || height == 0 || !IsValid(sampler) ||
        !IsExactRgba8ByteCount(width, height, pixels->size()))
    {
        return false;
    }
    const auto found = catalog_.find(asset.id.value);
    if (found == catalog_.end() || found->second.retired ||
        found->second.currentRevision == (std::numeric_limits<std::uint64_t>::max)() ||
        asset.revision != found->second.currentRevision + 1)
    {
        return false;
    }
    try
    {
        std::map<std::uint64_t, CatalogRecord::RevisionRecord> prepared;
        prepared.emplace(asset.revision,
                         CatalogRecord::RevisionRecord{width, height, sampler, std::move(pixels)});
        found->second.revisions.merge(prepared);
    }
    catch (...)
    {
        return false;
    }
    found->second.currentRevision = asset.revision;
    PruneUnleasedRevisions(found->second);
    BumpGeneration();
    return true;
}

bool LogicalRenderAssetCatalog::IsCurrent(LogicalRenderAssetRef asset) const noexcept
{
    if (!IsOwnerThread() || !IsValid(asset))
    {
        return false;
    }
    const auto found = catalog_.find(asset.id.value);
    if (found == catalog_.end() || found->second.retired ||
        found->second.currentRevision != asset.revision)
    {
        return false;
    }
    const auto revision = found->second.revisions.find(asset.revision);
    return revision != found->second.revisions.end() && revision->second.pixels != nullptr;
}

std::uint64_t LogicalRenderAssetCatalog::Generation() const noexcept
{
    return IsOwnerThread() ? generation_ : 0;
}

void LogicalRenderAssetCatalog::BumpGeneration() noexcept
{
    ++generation_;
    if (generation_ == 0)
    {
        generation_ = 1;
    }
}

void LogicalRenderAssetCatalog::PruneUnleasedRevisions(CatalogRecord &record) noexcept
{
    // Keep the immediately previous revision as a one-revision handoff
    // window while the asset is live. A held exact lease owns older content
    // through its shared_ptr; unleased history beyond that window is not
    // retained forever. Retirement closes the acquisition gate, so all
    // unleased records can be discarded immediately.
    const std::uint64_t retainedRevision =
        record.currentRevision > 1 ? record.currentRevision - 1 : 0;
    for (auto revision = record.revisions.begin(); revision != record.revisions.end();)
    {
        if ((record.retired ||
             (revision->first != record.currentRevision && revision->first != retainedRevision)) &&
            revision->second.pixels.use_count() == 1)
        {
            revision = record.revisions.erase(revision);
        }
        else
        {
            ++revision;
        }
    }
}

namespace UI::Modern
{
std::optional<LogicalRenderAssetMetadata> ResolveUiAsset(const CGlobalBitmap &bitmaps,
                                                         std::string_view source) noexcept
{
    try
    {
        std::wstring path = std::filesystem::u8path(source).wstring();
        if (const auto registered = bitmaps.TryDescribe(path))
            return registered;

        std::replace(path.begin(), path.end(), L'/', L'\\');

        if (const auto exact =
                bitmaps.TryDescribe(std::filesystem::absolute(path).lexically_normal().wstring()))
            return exact;

        std::wstring lower(path);
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](wchar_t character) { return std::towlower(character); });

        constexpr std::wstring_view dataMarker = L"\\data\\";
        const std::size_t marker = lower.rfind(dataMarker);
        if (marker != std::wstring::npos)
        {
            path.erase(0, marker + dataMarker.size());
        }
        else if (lower.starts_with(L"data\\"))
        {
            path.erase(0, 5);
        }

        if (const auto asset = bitmaps.TryDescribe(path))
            return asset;

        // Explicit file paths must never resolve to another skin's same-named file.
        if (path.find_first_of(L"\\/") != std::wstring::npos)
            return std::nullopt;

        return bitmaps.TryDescribeByName(path);
    }
    catch (...)
    {
        return std::nullopt;
    }
}
} // namespace UI::Modern

// Publish on guild data changes, before worker recording. Each guild owns its pixels.
void CGuildCache::PrepareMark(int index)
{
    static constexpr std::uint32_t colors[] = {0x00000000, 0xff000000, 0xff808080, 0xffffffff,
                                               0xff0000ff, 0xff0080ff, 0xff00ffff, 0xff00ff80,
                                               0xff00ff00, 0xff80ff00, 0xffffff00, 0xffff8000,
                                               0xffff0000, 0xffff0080, 0xffff00ff, 0xff8000ff};
    auto &bitmaps = sessionKeeper_.ApplicationKeeperRef().BitmapRegistry();
    auto &variants = textures_[index];
    for (std::size_t blend = 0; blend < variants.size(); ++blend)
    {
        auto &texture = variants[blend];
        auto id = IsValid(texture.asset) ? std::optional(texture.asset.id)
                                         : bitmaps.AllocateDynamicIdentity();
        if (!id)
            return;
        auto pixels = std::make_shared<std::vector<std::byte>>(GuildMarkConstants::MarkSize * 4);
        for (int pixel = 0; pixel < GuildMarkConstants::MarkSize; ++pixel)
        {
            const auto color = GuildMark[index].Mark[pixel];
            auto rgba = colors[color];
            if (color == 0 && blend == 0)
                rgba = 0x80000000;
            std::memcpy(pixels->data() + pixel * 4, &rgba, sizeof(rgba));
        }
        const LogicalRenderAssetRef asset{*id, texture.asset.revision + 1};
        const RenderSamplerIntent sampler{LegacyTextureFilter::Nearest,
                                          LegacyTextureWrap::ClampToEdge};
        if (!bitmaps.CommitOwnerProducedRevision(asset, 8, 8, sampler, pixels))
            return;
        texture = {asset, 8, 8, RenderAssetFormat::Rgba8, sampler, std::move(pixels)};
    }
}

const LogicalRenderAssetLease *CGuildCache::MarkTexture(int index, bool blend) const
{
    const auto found = textures_.find(index);
    return found == textures_.end() ? nullptr : &found->second[blend ? 1 : 0];
}

// Implementations moved from SessionBitmapView.h.
SessionBitmapView::SessionBitmapView(CGlobalBitmap &bitmaps,
                                     SessionTextureNamespace &textures) noexcept
    : bitmaps_(bitmaps), textures_(textures)
{
}

SessionBitmapMetadata SessionBitmapView::operator[](std::uint32_t logicalIndex) const noexcept
{
    return Describe(logicalIndex).value_or(SessionBitmapMetadata{});
}

std::optional<SessionBitmapMetadata> SessionBitmapView::GetTexture(
    std::uint32_t logicalIndex) const noexcept
{
    return Describe(logicalIndex);
}

std::optional<SessionBitmapMetadata> SessionBitmapView::FindTexture(
    std::uint32_t logicalIndex) const noexcept
{
    return Describe(logicalIndex);
}

std::optional<SessionBitmapMetadata> SessionBitmapView::FindTexture(
    const std::wstring &filename) const noexcept
{
    return bitmaps_.TryDescribe(filename);
}

std::optional<SessionBitmapMetadata> SessionBitmapView::FindTextureByName(
    const std::wstring &name) const noexcept
{
    return textures_.TryDescribeByName(name);
}

std::optional<std::uint32_t> SessionBitmapView::RetainTextureByName(
    const std::wstring &name) noexcept
{
    return textures_.RetainByName(name);
}

std::optional<SessionTextureProperties> SessionBitmapView::GetTextureProperties(
    std::uint32_t logicalIndex) const noexcept
{
    if (!SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        return textures_.TryGetProperties(logicalIndex);
    }
    const auto metadata = bitmaps_.TryDescribe(logicalIndex);
    return metadata.has_value() ? std::optional<SessionTextureProperties>(SessionTextureProperties{
                                      metadata->Width, metadata->Height, metadata->Components,
                                      metadata->IsSkin, metadata->IsHair})
                                : std::nullopt;
}

std::optional<LogicalRenderAssetLease> SessionBitmapView::TryLease(
    std::uint32_t logicalIndex) const noexcept
{
    if (SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        const auto metadata = bitmaps_.TryDescribe(logicalIndex);
        return metadata.has_value() ? bitmaps_.TryLease(metadata->Asset) : std::nullopt;
    }
    return textures_.TryLease(logicalIndex);
}

std::uint32_t SessionBitmapView::LoadImage(const std::wstring &filename, CErrorReport &errorReport,
                                           LegacyTextureFilter filter, LegacyTextureWrap wrapMode)
{
    return textures_.LoadUnnamed(filename, errorReport, filter, wrapMode);
}

bool SessionBitmapView::LoadImage(std::uint32_t logicalIndex, const std::wstring &filename,
                                  CErrorReport &errorReport, LegacyTextureFilter filter,
                                  LegacyTextureWrap wrapMode)
{
    return SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex)
               ? bitmaps_.LoadImage(logicalIndex, filename, errorReport, filter, wrapMode)
               : textures_.Load(logicalIndex, filename, errorReport, filter, wrapMode);
}

void SessionBitmapView::UnloadImage(std::uint32_t logicalIndex, bool) noexcept
{
    if (SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        bitmaps_.UnloadImage(logicalIndex);
    }
    else
    {
        textures_.Unload(logicalIndex);
    }
}

bool SessionBitmapView::Convert_Format(const std::wstring &filename)
{
    return bitmaps_.Convert_Format(filename);
}

void SessionBitmapView::Manage(std::chrono::steady_clock::time_point &startTickTime,
                               CmuConsoleDebug &consoleDebug)
{
    bitmaps_.Manage(startTickTime, consoleDebug);
}

std::uint32_t SessionBitmapView::GetUsedTextureMemory() const
{
    return bitmaps_.GetUsedTextureMemory();
}

size_t SessionBitmapView::GetNumberOfTexture() const
{
    return bitmaps_.GetNumberOfTexture();
}

bool SessionBitmapView::CommitOwnerProducedRevision(
    std::uint32_t logicalIndex, LogicalRenderAssetRef asset, std::uint32_t width,
    std::uint32_t height, RenderSamplerIntent sampler,
    std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    if (!SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        return textures_.CommitOwnerProducedRevision(logicalIndex, asset, width, height, sampler,
                                                     std::move(rgba8));
    }
    return bitmaps_.CommitOwnerProducedRevision(asset, width, height, sampler, std::move(rgba8));
}

std::optional<SessionBitmapMetadata> SessionBitmapView::Describe(
    std::uint32_t logicalIndex) const noexcept
{
    return SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex)
               ? bitmaps_.TryDescribe(logicalIndex)
               : textures_.TryDescribe(logicalIndex);
}
