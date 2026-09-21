#include "render/Textures.h"
#include "app/Application.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/WorldPhysics.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "turbojpeg.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Dialogs/DialogsRender.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/features/World/WorldRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"

namespace Render::Textures
{
namespace
{
constexpr std::size_t HeaderBytes = 18;
constexpr unsigned char RawTrueColor = 2;
constexpr unsigned char RleTrueColor = 10;

std::uint32_t ReadWord(std::span<const unsigned char> bytes, std::size_t offset)
{
    return bytes[offset] | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U);
}

bool DecodePixels(std::span<const unsigned char> bytes, std::size_t components, bool rle,
                  std::span<std::byte> rgba)
{
    std::size_t cursor = 0;
    std::size_t pixel = 0;
    while (pixel < rgba.size() / 4)
    {
        if (cursor >= bytes.size())
            return false;
        const unsigned char packet = rle ? bytes[cursor++] : 0;
        const std::size_t count = rle ? (packet & 0x7fU) + 1U : 1U;
        const bool repeat = rle && (packet & 0x80U);
        const std::size_t encoded = (repeat ? 1 : count) * components;
        if (count > rgba.size() / 4 - pixel || encoded > bytes.size() - cursor)
            return false;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto *color = bytes.data() + cursor + (repeat ? 0 : i * components);
            const std::size_t destination = (pixel + i) * 4;
            rgba[destination] = static_cast<std::byte>(color[2]);
            rgba[destination + 1] = static_cast<std::byte>(color[1]);
            rgba[destination + 2] = static_cast<std::byte>(color[0]);
            rgba[destination + 3] = static_cast<std::byte>(components == 4 ? color[3] : 255);
        }
        cursor += encoded;
        pixel += count;
    }
    return true;
}

void OrientTopLeft(TgaImage &image, unsigned char descriptor)
{
    const std::size_t rowBytes = image.width * 4;
    if (!(descriptor & 0x20U))
        for (std::size_t y = 0; y < image.height / 2; ++y)
            std::swap_ranges(image.rgba.begin() + y * rowBytes,
                             image.rgba.begin() + (y + 1) * rowBytes,
                             image.rgba.begin() + (image.height - 1 - y) * rowBytes);
    if (descriptor & 0x10U)
        for (std::size_t y = 0; y < image.height; ++y)
            for (std::size_t x = 0; x < image.width / 2; ++x)
                for (std::size_t c = 0; c < 4; ++c)
                    std::swap(image.rgba[y * rowBytes + x * 4 + c],
                              image.rgba[y * rowBytes + (image.width - 1 - x) * 4 + c]);
}
} // namespace

std::optional<TgaImage> DecodeTga(std::span<const unsigned char> bytes)
{
    if (bytes.size() < HeaderBytes || bytes[1] != 0 ||
        (bytes[2] != RawTrueColor && bytes[2] != RleTrueColor) ||
        (bytes[16] != 24 && bytes[16] != 32) || (bytes[17] & 0xc0U))
        return std::nullopt;
    TgaImage image;
    image.width = ReadWord(bytes, 12);
    image.height = ReadWord(bytes, 14);
    const std::size_t offset = HeaderBytes + bytes[0];
    const std::uint64_t count = static_cast<std::uint64_t>(image.width) * image.height;
    const std::size_t components = bytes[16] / 8;
    const bool rle = bytes[2] == RleTrueColor;
    const std::uint64_t minimumBytes =
        rle ? ((count + 127) / 128) * (components + 1) : count * components;
    if (!count || offset > bytes.size() || count > image.rgba.max_size() / 4 ||
        minimumBytes > bytes.size() - offset)
        return std::nullopt;
    image.rgba.resize(static_cast<std::size_t>(count) * 4);
    if (!DecodePixels(bytes.subspan(offset), components, rle, image.rgba))
        return std::nullopt;
    OrientTopLeft(image, bytes[17]);
    return image;
}
} // namespace Render::Textures

SessionTextureNamespace::SessionTextureNamespace(CGlobalBitmap &bitmaps) noexcept
    : bitmaps_(bitmaps)
{
}

SessionTextureNamespace::~SessionTextureNamespace()
{
    for (auto &[logical, binding] : bindings_)
    {
        (void)logical;
        binding.lease = {};
        bitmaps_.UnloadImage(binding.legacyIndex);
    }
}

bool SessionTextureNamespace::Load(std::uint32_t logicalIndex, const std::wstring &filename,
                                   CErrorReport &errorReport, LegacyTextureFilter filter,
                                   LegacyTextureWrap wrapMode)
{
    if (!IsValid(filter) || !IsValid(wrapMode))
    {
        return false;
    }
    const auto existing = bindings_.find(logicalIndex);
    if (existing != bindings_.end())
    {
        if (_wcsicmp(existing->second.metadata.FileName.c_str(), filename.c_str()) == 0)
        {
            ++existing->second.references;
            CacheDenseBinding(logicalIndex, &existing->second);
            return true;
        }
    }
    // BITMAP_GUILD is repainted with session-specific guild-mark pixels.
    // Immutable disk assets bind directly to the owner's path+sampler entry.
    const std::uint32_t legacyIndex =
        logicalIndex == BITMAP_GUILD
            ? bitmaps_.LoadSessionImage(filename, errorReport, filter, wrapMode)
            : bitmaps_.LoadImage(filename, errorReport, filter, wrapMode);
    if (legacyIndex == BITMAP_UNKNOWN)
    {
        return false;
    }
    const std::optional<LogicalRenderAssetMetadata> metadata = bitmaps_.TryDescribe(legacyIndex);
    if (!metadata.has_value() || !IsValid(metadata->Asset))
    {
        bitmaps_.UnloadImage(legacyIndex);
        return false;
    }
    const std::optional<LogicalRenderAssetLease> lease = bitmaps_.TryLease(metadata->Asset);
    if (!lease.has_value())
    {
        bitmaps_.UnloadImage(legacyIndex);
        return false;
    }

    if (existing != bindings_.end())
    {
        existing->second.lease = {};
        bitmaps_.UnloadImage(existing->second.legacyIndex);
        existing->second = Binding{legacyIndex,
                                   metadata->Asset,
                                   *metadata,
                                   *lease,
                                   {metadata->Width, metadata->Height, metadata->Components,
                                    metadata->IsSkin, metadata->IsHair},
                                   1};
        CacheDenseBinding(logicalIndex, &existing->second);
    }
    else
    {
        try
        {
            const auto [position, inserted] = bindings_.emplace(
                logicalIndex, Binding{legacyIndex,
                                      metadata->Asset,
                                      *metadata,
                                      *lease,
                                      {metadata->Width, metadata->Height, metadata->Components,
                                       metadata->IsSkin, metadata->IsHair},
                                      1});
            (void)inserted;
            CacheDenseBinding(logicalIndex, &position->second);
        }
        catch (...)
        {
            bitmaps_.UnloadImage(legacyIndex);
            return false;
        }
    }
    return true;
}

std::uint32_t SessionTextureNamespace::LoadUnnamed(const std::wstring &filename,
                                                   CErrorReport &errorReport,
                                                   LegacyTextureFilter filter,
                                                   LegacyTextureWrap wrapMode)
{
    if (!IsValid(filter) || !IsValid(wrapMode))
    {
        return BITMAP_UNKNOWN;
    }
    const std::uint32_t legacyIndex = bitmaps_.LoadImage(filename, errorReport, filter, wrapMode);
    if (legacyIndex == BITMAP_UNKNOWN)
    {
        return BITMAP_UNKNOWN;
    }
    const auto metadata = bitmaps_.TryDescribe(legacyIndex);
    if (!metadata.has_value() || !IsValid(metadata->Asset))
    {
        bitmaps_.UnloadImage(legacyIndex);
        return BITMAP_UNKNOWN;
    }
    const std::optional<LogicalRenderAssetLease> lease = bitmaps_.TryLease(metadata->Asset);
    if (!lease.has_value())
    {
        bitmaps_.UnloadImage(legacyIndex);
        return BITMAP_UNKNOWN;
    }

    const auto existing = bindings_.find(legacyIndex);
    if (existing != bindings_.end())
    {
        if (existing->second.asset == metadata->Asset)
        {
            bitmaps_.UnloadImage(legacyIndex);
            ++existing->second.references;
            CacheDenseBinding(legacyIndex, &existing->second);
            return legacyIndex;
        }
        existing->second.lease = {};
        bitmaps_.UnloadImage(existing->second.legacyIndex);
        existing->second = Binding{legacyIndex,
                                   metadata->Asset,
                                   *metadata,
                                   *lease,
                                   {metadata->Width, metadata->Height, metadata->Components,
                                    metadata->IsSkin, metadata->IsHair},
                                   1};
        CacheDenseBinding(legacyIndex, &existing->second);
        return legacyIndex;
    }

    try
    {
        const auto [position, inserted] = bindings_.emplace(
            legacyIndex, Binding{legacyIndex,
                                 metadata->Asset,
                                 *metadata,
                                 *lease,
                                 {metadata->Width, metadata->Height, metadata->Components,
                                  metadata->IsSkin, metadata->IsHair},
                                 1});
        (void)inserted;
        CacheDenseBinding(legacyIndex, &position->second);
    }
    catch (...)
    {
        bitmaps_.UnloadImage(legacyIndex);
        return BITMAP_UNKNOWN;
    }
    return legacyIndex;
}

void SessionTextureNamespace::Unload(std::uint32_t logicalIndex) noexcept
{
    const auto existing = bindings_.find(logicalIndex);
    if (existing == bindings_.end())
    {
        return;
    }
    if (existing->second.references > 1)
    {
        --existing->second.references;
        return;
    }
    existing->second.lease = {};
    bitmaps_.UnloadImage(existing->second.legacyIndex);
    CacheDenseBinding(logicalIndex, nullptr);
    if (lastBinding_ == &existing->second)
    {
        lastBinding_ = nullptr;
    }
    bindings_.erase(existing);
}

void SessionTextureNamespace::SwapBinding(std::uint32_t logicalIndex,
                                          SessionTextureNamespace &other)
{
    if (!bindings_.contains(logicalIndex) && !other.bindings_.contains(logicalIndex))
        return;
    // Reserve before extracting either node: an allocation failure leaves both owners intact.
    if (!bindings_.contains(logicalIndex) &&
        bindings_.size() + 1 > bindings_.bucket_count() * bindings_.max_load_factor())
        bindings_.reserve(bindings_.size() + 1);
    if (!other.bindings_.contains(logicalIndex) &&
        other.bindings_.size() + 1 >
            other.bindings_.bucket_count() * other.bindings_.max_load_factor())
        other.bindings_.reserve(other.bindings_.size() + 1);
    auto current = bindings_.extract(logicalIndex);
    auto replacement = other.bindings_.extract(logicalIndex);
    lastBinding_ = nullptr;
    other.lastBinding_ = nullptr;
    const Binding *installed = nullptr;
    const Binding *saved = nullptr;
    if (replacement)
        installed = &bindings_.insert(std::move(replacement)).position->second;
    if (current)
        saved = &other.bindings_.insert(std::move(current)).position->second;
    CacheDenseBinding(logicalIndex, installed);
    other.CacheDenseBinding(logicalIndex, saved);
}

std::optional<LogicalRenderAssetRef> SessionTextureNamespace::Resolve(
    std::uint32_t logicalIndex) const noexcept
{
    const Binding *const binding = FindCurrentBinding(logicalIndex);
    return binding != nullptr ? std::optional<LogicalRenderAssetRef>(binding->asset) : std::nullopt;
}

const SessionTextureNamespace::Binding *SessionTextureNamespace::FindCurrentBinding(
    std::uint32_t logicalIndex) const noexcept
{
    if (logicalIndex > BITMAP_UNKNOWN && logicalIndex <= BITMAP_EFFECT_TEXTURE_END)
    {
        const std::size_t denseIndex = static_cast<std::size_t>(logicalIndex - BITMAP_UNKNOWN - 1U);
        if (denseIndex < denseBindings_.size() && denseBindings_[denseIndex] != nullptr)
        {
            return denseBindings_[denseIndex];
        }
    }
    const Binding *binding = nullptr;
    if (lastBinding_ != nullptr && lastLogicalIndex_ == logicalIndex)
    {
        binding = lastBinding_;
    }
    else
    {
        const auto existing = bindings_.find(logicalIndex);
        if (existing == bindings_.end())
        {
            return nullptr;
        }
        lastLogicalIndex_ = logicalIndex;
        lastBinding_ = &existing->second;
        binding = lastBinding_;
    }
    return binding;
}

const SessionTexturePropertiesSlot &SessionTextureNamespace::BindProperties(
    std::uint32_t logicalIndex)
{
    const auto *binding = FindCurrentBinding(logicalIndex);
    if (binding != nullptr && binding->propertySlot != nullptr)
        return *binding->propertySlot;
    const auto [slot, inserted] = propertySlots_.try_emplace(logicalIndex);
    if (inserted)
        RefreshBoundProperties(logicalIndex, binding);
    return slot->second;
}

void SessionTextureNamespace::RefreshBoundProperties(std::uint32_t logicalIndex,
                                                     const Binding *binding) noexcept
{
    const auto slot = propertySlots_.find(logicalIndex);
    // A swapped binding must borrow this namespace's slot, never the old owner's.
    if (binding != nullptr)
        binding->propertySlot = slot != propertySlots_.end() ? &slot->second : nullptr;
    if (slot == propertySlots_.end())
        return;
    slot->second = binding != nullptr
                       ? SessionTexturePropertiesSlot{binding->properties, binding->asset}
                       : SessionTexturePropertiesSlot{};
}

void SessionTextureNamespace::CacheDenseBinding(std::uint32_t logicalIndex,
                                                const Binding *binding) noexcept
{
    RefreshBoundProperties(logicalIndex, binding);
    if (logicalIndex <= BITMAP_UNKNOWN || logicalIndex > BITMAP_EFFECT_TEXTURE_END)
    {
        return;
    }
    const std::size_t denseIndex = static_cast<std::size_t>(logicalIndex - BITMAP_UNKNOWN - 1U);
    try
    {
        if (binding != nullptr && denseIndex >= denseBindings_.size())
            denseBindings_.resize(denseIndex + 1U, nullptr);
        if (denseIndex < denseBindings_.size())
            denseBindings_[denseIndex] = binding;
    }
    catch (...)
    {
        // The dense table is only a cache; the hash binding stays current.
    }
}

std::optional<LogicalRenderAssetLease> SessionTextureNamespace::TryLease(
    std::uint32_t logicalIndex) const noexcept
{
    const Binding *const binding = FindCurrentBinding(logicalIndex);
    return binding != nullptr ? std::optional<LogicalRenderAssetLease>(binding->lease)
                              : std::nullopt;
}

std::optional<LogicalRenderAssetMetadata> SessionTextureNamespace::TryDescribe(
    std::uint32_t logicalIndex) const noexcept
{
    const Binding *const binding = FindCurrentBinding(logicalIndex);
    return binding != nullptr ? std::optional<LogicalRenderAssetMetadata>(binding->metadata)
                              : std::nullopt;
}

std::optional<SessionTextureProperties> SessionTextureNamespace::TryGetProperties(
    std::uint32_t logicalIndex) const noexcept
{
    const Binding *const binding = FindCurrentBinding(logicalIndex);
    return binding != nullptr ? std::optional<SessionTextureProperties>(binding->properties)
                              : std::nullopt;
}

std::optional<LogicalRenderAssetMetadata> SessionTextureNamespace::TryDescribeByName(
    const std::wstring &name) const noexcept
{
    for (const auto &[logicalIndex, binding] : bindings_)
    {
        (void)logicalIndex;
        wchar_t filePart[_MAX_FNAME] = {};
        wchar_t extension[_MAX_EXT] = {};
        _wsplitpath(binding.metadata.FileName.c_str(), nullptr, nullptr, filePart, extension);
        std::wstring filename = filePart;
        filename += extension;
        if (_wcsicmp(filename.c_str(), name.c_str()) == 0)
        {
            return binding.metadata;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> SessionTextureNamespace::RetainByName(
    const std::wstring &name) noexcept
{
    for (auto &[logicalIndex, binding] : bindings_)
    {
        wchar_t filePart[_MAX_FNAME] = {};
        wchar_t extension[_MAX_EXT] = {};
        _wsplitpath(binding.metadata.FileName.c_str(), nullptr, nullptr, filePart, extension);
        std::wstring filename = filePart;
        filename += extension;
        if (_wcsicmp(filename.c_str(), name.c_str()) == 0)
        {
            ++binding.references;
            return logicalIndex;
        }
    }
    return std::nullopt;
}

bool SessionTextureNamespace::CommitOwnerProducedRevision(
    std::uint32_t logicalIndex, LogicalRenderAssetRef asset, std::uint32_t width,
    std::uint32_t height, RenderSamplerIntent sampler,
    std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    const auto existing = bindings_.find(logicalIndex);
    if (existing == bindings_.end() || asset.id != existing->second.asset.id ||
        existing->second.asset.revision == (std::numeric_limits<std::uint64_t>::max)() ||
        asset.revision != existing->second.asset.revision + 1)
    {
        return false;
    }
    if (!bitmaps_.CommitOwnerProducedRevision(asset, width, height, sampler, rgba8))
    {
        return false;
    }
    existing->second.asset = asset;
    existing->second.metadata.Width = static_cast<float>(width);
    existing->second.metadata.Height = static_cast<float>(height);
    existing->second.metadata.Components = 4;
    existing->second.metadata.Asset = asset;
    existing->second.metadata.Sampler = sampler;
    existing->second.lease = {asset,   width,           height, RenderAssetFormat::Rgba8,
                              sampler, std::move(rgba8)};
    existing->second.properties.width = static_cast<float>(width);
    existing->second.properties.height = static_cast<float>(height);
    existing->second.properties.components = 4;
    RefreshBoundProperties(logicalIndex, &existing->second);
    return true;
}

bool SessionTextureNamespace::CommitOwnerProducedRevision(
    LogicalRenderAssetRef asset, std::uint32_t width, std::uint32_t height,
    RenderSamplerIntent sampler, std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept
{
    for (const auto &[logicalIndex, binding] : bindings_)
    {
        if (binding.asset.id == asset.id)
        {
            return CommitOwnerProducedRevision(logicalIndex, asset, width, height, sampler,
                                               std::move(rgba8));
        }
    }
    return false;
}

bool SessionTextureNamespace::IsTerrainLogicalIndex(std::uint32_t logicalIndex) noexcept
{
    return logicalIndex >= BITMAP_MAPTILE_BEGIN && logicalIndex <= BITMAP_RAIN;
}

bool SessionTextureNamespace::IsApplicationSharedLogicalIndex(std::uint32_t logicalIndex) noexcept
{
    const bool isFont = logicalIndex >= BITMAP_FONT_BEGIN && logicalIndex < BITMAP_FONT_END;
    return isFont || logicalIndex == BITMAP_INTERFACE_MACROUI_END ||
           logicalIndex == BITMAP_INTERFACE_MACROUI_END + 1 ||
           logicalIndex == BITMAP_INTERFACE_MACROUI_END + 2 ||
           logicalIndex == UI::Modern::PC::Common::S16PopupTextureIndex ||
           logicalIndex == UI::Modern::PC::Inventory::PrivateStoreTextureIndex ||
           logicalIndex == UI::Modern::PC::HUD::MainFrameTextureIndex ||
           logicalIndex == UI::Modern::PC::HUD::TopMenuTextureIndex ||
           logicalIndex == UI::Modern::PC::HUD::MoveCommandTextureIndex ||
           logicalIndex == UI::Modern::PC::Party::PartyFrameTextureIndex ||
           logicalIndex == UI::Modern::PC::Chat::TextureIndex ||
           logicalIndex == UI::Modern::PC::Textures::FrameTextureIndex ||
           logicalIndex == UI::Modern::PC::Textures::ButtonTextureIndex ||
           logicalIndex == UI::Modern::PC::Textures::ScrollTextureIndex ||
           logicalIndex == UI::Modern::PC::Textures::CheckboxTextureIndex ||
           logicalIndex == UI::Modern::PC::Textures::IconTextureIndex ||
           logicalIndex == UI::Modern::PC::MuHelper::TextureIndex ||
           logicalIndex == UI::Modern::PC::World::StoreLabelTextureIndex;
}

void SessionTextureNamespace::SelectPreparedTexture(std::uint32_t logicalIndex,
                                                    const LogicalRenderAssetLease &texture) noexcept
{
    auto &binding = bindings_.at(logicalIndex);
    binding.asset = texture.asset;
    binding.lease = texture;
    binding.metadata.Asset = texture.asset;
    binding.metadata.Width = static_cast<float>(texture.width);
    binding.metadata.Height = static_cast<float>(texture.height);
    binding.metadata.Components = 4;
    binding.metadata.Sampler = texture.sampler;
    binding.properties.width = binding.metadata.Width;
    binding.properties.height = binding.metadata.Height;
    binding.properties.components = 4;
    RefreshBoundProperties(logicalIndex, &binding);
}

void SessionTextureNamespace::RestorePreparedBinding(std::uint32_t logicalIndex,
                                                     const SessionTextureNamespace &prepared)
{
    auto &binding = bindings_.at(logicalIndex);
    const auto &source = prepared.bindings_.at(logicalIndex);
    binding.asset = source.asset;
    binding.lease = source.lease;
    binding.metadata = source.metadata;
    binding.properties = source.properties;
    RefreshBoundProperties(logicalIndex, &binding);
}

namespace UI::Modern
{
namespace
{
std::set<std::filesystem::path> TexturePaths(const std::filesystem::path &linker, std::string text)
{
    // Read only src declarations at startup. RmlUI owns sprite/RCSS parsing.
    text = std::regex_replace(text, std::regex(R"(/\*[\s\S]*?\*/)"), "");
    const std::regex source(R"rcss(\bsrc\s*:\s*(?:"([^"]+)"|'([^']+)'|([^;\s]+))\s*;)rcss");
    std::set<std::filesystem::path> paths;
    for (std::sregex_iterator it(text.begin(), text.end(), source), end; it != end; ++it)
    {
        const auto &match = *it;
        const auto value = match[1].matched   ? match[1].str()
                           : match[2].matched ? match[2].str()
                                              : match[3].str();
        paths.insert(
            std::filesystem::absolute(linker.parent_path() / std::filesystem::u8path(value))
                .lexically_normal());
    }
    return paths;
}
} // namespace

UiTextureLibrary::UiTextureLibrary(CGlobalBitmap &bitmaps) noexcept : bitmaps_(bitmaps)
{
}

UiTextureLibrary::~UiTextureLibrary()
{
    for (const auto index : textures_)
        bitmaps_.UnloadImage(index);
}

bool UiTextureLibrary::Load(const std::filesystem::path &linker, CErrorReport &errors)
{
    std::ifstream input(linker, std::ios::binary);
    if (!input)
    {
        errors.Write(L"Cannot open UI skin linker: %ls\n", linker.wstring().c_str());
        return false;
    }
    const auto paths = TexturePaths(linker, std::string(std::istreambuf_iterator<char>(input),
                                                        std::istreambuf_iterator<char>()));
    textures_.reserve(textures_.size() + paths.size());
    for (const auto &path : paths)
    {
        const auto index = bitmaps_.LoadImage(path.wstring(), errors, LegacyTextureFilter::Linear,
                                              LegacyTextureWrap::ClampToEdge);
        if (index == BITMAP_UNKNOWN)
        {
            errors.Write(L"Cannot load UI skin texture: %ls\n", path.wstring().c_str());
            return false;
        }
        textures_.push_back(index);
    }
    return true;
}
} // namespace UI::Modern

namespace
{
constexpr WorldTextureDependency CommonTextures[]{
    {BITMAP_BUBBLE, L"Object8/drop01.jpg"},
    {BITMAP_SHINES, L"Object8/light01.jpg", LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat},
};
constexpr WorldTextureDependency Tarkan[]{
    {BITMAP_CHROME + 2, L"Object9/sand01.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Object9/sand02.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_IMPACT, L"Object9/Impack03.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Heaven[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CLOUD + 1, L"Effect/cloudLight.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Cloud[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Crywolf[]{
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Effect/Map_Smoke2.tga", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_MAGIC_CIRCLE, L"Effect/mhoujin_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_EXT_LOG_IN + 2, L"Effect/Impack03.jpg"},
    {BITMAP_EFFECT, L"Logo/chasellight.jpg"},
};
constexpr WorldTextureDependency BattleCastle[]{
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Effect/Map_Smoke2.tga", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_INTERFACE_MAP + 1, L"World31/Map1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_INTERFACE_MAP + 2, L"World31/Map2.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency HuntingGround[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_HGBOSS_PATTERN, L"Monster/bossmap1_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_HGBOSS_WING, L"Monster/bosswing.tga", LegacyTextureFilter::Nearest,
     LegacyTextureWrap::Repeat},
    {BITMAP_FISSURE_FIRE, L"Skill/bossrock1_R.JPG", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Doppelganger4[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_TWINTAIL_WATER, L"effect/water.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Kanturu2[]{
    {BITMAP_KANTURU_2ND_EFFECT1, L"Object39/k_effect_01.JPG", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_KANTURU_2ND_NPC1, L"Npc/khs_kan2gate001.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_KANTURU_2ND_NPC2, L"Npc/khs_kan2gate003.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_KANTURU_2ND_NPC3, L"Npc/khs_kan2gate004.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Kanturu3[]{
    {BITMAP_NIGHTMARE_EFFECT1, L"Monster/nightmare_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_NIGHTMARE_EFFECT2, L"Monster/nightmaresward_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_NIGHTMARE_ROBE, L"Monster/nightmare_cloth.tga"},
    {BITMAP_MAYA_BODY, L"Object40/maya01_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_KANTURU3RD_OBJECT, L"Object40/Mtowereffe.JPG", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_ENERGY_RING, L"Effect/bluering0001_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_ENERGY_FIELD, L"Effect/bluewave0001_R.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency CursedTemple[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CLUD64, L"Effect/clud64.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_GHOST_CLOUD1, L"Effect/ghosteffect01.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_GHOST_CLOUD2, L"Effect/ghosteffect02.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_TORCH_FIRE, L"Effect/torchfire.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_EVENT_CLOUD, L"Effect/clouds2.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Changeup[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_FIRE_RED, L"Effect/firered.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_FIRE_SNUFF, L"Effect/FireSnuff.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Elbeland[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
};
constexpr WorldTextureDependency Doppelganger2[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_FIRE_SNUFF, L"Effect/FireSnuff.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CHROME3, L"Effect/WATERFALL2.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CLUD64, L"Effect/clud64.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency Swamp[]{
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Effect/Map_Smoke2.tga", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_SHADOW_PAWN_RED, L"Monster/red_shadows.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_SHADOW_KINGHT_BLUE, L"Monster/blue_shadows.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_SHADOW_ROOK_GREEN, L"Monster/green_shadows.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
};
constexpr WorldTextureDependency Doppelganger1[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Object9/sand02.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME8, L"Effect/Chrome08.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency IceCity[]{
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME + 3, L"Object9/sand02.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CHROME8, L"Effect/Chrome08.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
constexpr WorldTextureDependency EmpireGuardian[]{
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_FLARE, L"Effect/flare.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
    {BITMAP_CHROME + 2, L"Effect/Map_Smoke1.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
};
constexpr WorldTextureDependency LoginScene[]{
    {BITMAP_LOG_IN + 16, L"Logo/MU-logo.tga", LegacyTextureFilter::Linear},
    {BITMAP_LOG_IN + 17, L"Logo/MU-logo_g.jpg", LegacyTextureFilter::Linear},
    {BITMAP_LOG_IN, L"Interface/cha_bt.tga"},
    {BITMAP_LOG_IN + 1, L"Interface/server_b2_all.tga"},
    {BITMAP_LOG_IN + 2, L"Interface/server_b2_loding.jpg"},
    {BITMAP_LOG_IN + 3, L"Interface/server_deco_all.tga"},
    {BITMAP_LOG_IN + 4, L"Interface/server_menu_b_all.tga"},
    {BITMAP_LOG_IN + 5, L"Interface/server_credit_b_all.tga"},
    {BITMAP_LOG_IN + 6, L"Interface/deco.tga"},
    {BITMAP_LOG_IN + 8, L"Interface/login_me.tga"},
    {BITMAP_LOG_IN + 11, L"Interface/server_ex03.tga", LegacyTextureFilter::Nearest,
     LegacyTextureWrap::Repeat},
    {BITMAP_LOG_IN + 12, L"Interface/server_ex01.tga"},
    {BITMAP_LOG_IN + 13, L"Interface/server_ex02.jpg", LegacyTextureFilter::Nearest,
     LegacyTextureWrap::Repeat},
    {BITMAP_LOG_IN + 14, L"Interface/cr_mu_lo.tga", LegacyTextureFilter::Linear},
};
constexpr WorldTextureDependency CharacterScene[]{
    {BITMAP_LOG_IN + 16, L"Logo/MU-logo.tga", LegacyTextureFilter::Linear},
    {BITMAP_LOG_IN + 17, L"Logo/MU-logo_g.jpg", LegacyTextureFilter::Linear},
    {BITMAP_LOG_IN, L"Interface/cha_id.tga"},
    {BITMAP_LOG_IN + 1, L"Interface/cha_bt.tga"},
    {BITMAP_LOG_IN + 2, L"Interface/deco.tga"},
    {BITMAP_LOG_IN + 3, L"Interface/b_create.tga"},
    {BITMAP_LOG_IN + 4, L"Interface/server_menu_b_all.tga"},
    {BITMAP_LOG_IN + 5, L"Interface/b_connect.tga"},
    {BITMAP_LOG_IN + 6, L"Interface/b_delete.tga"},
    {BITMAP_LOG_IN + 7, L"Interface/character_ex.tga"},
    {BITMAP_LOG_IN + 11, L"Interface/server_ex03.tga", LegacyTextureFilter::Nearest,
     LegacyTextureWrap::Repeat},
    {BITMAP_LOG_IN + 12, L"Interface/server_ex01.tga"},
    {BITMAP_LOG_IN + 13, L"Interface/server_ex02.jpg", LegacyTextureFilter::Nearest,
     LegacyTextureWrap::Repeat},
    {BITMAP_EXT_LOG_IN + 2, L"Effect/Impack03.jpg"},
    {BITMAP_EFFECT, L"Logo/chasellight.jpg"},
};
constexpr WorldTextureDependency Karutan[]{
    {BITMAP_CHROME + 3, L"Object9/sand02.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::Repeat},
    {BITMAP_CLOUD, L"Effect/clouds.jpg", LegacyTextureFilter::Linear,
     LegacyTextureWrap::ClampToEdge},
};
} // namespace

std::span<const WorldTextureDependency> WorldTextureDependency::Common() noexcept
{
    return CommonTextures;
}

std::span<const WorldTextureDependency> WorldTextureDependency::For(int behaviorMap) noexcept
{
    if ((behaviorMap >= WD_18CHAOS_CASTLE && behaviorMap <= WD_18CHAOS_CASTLE_END) ||
        behaviorMap == WD_53CAOSCASTLE_MASTER_LEVEL)
        return Cloud;
    switch (behaviorMap)
    {
    case WD_8TARKAN:
        return Tarkan;
    case WD_10HEAVEN:
        return Heaven;
    case WD_11BLOODCASTLE1:
    case WD_11BLOODCASTLE1 + 1:
    case WD_11BLOODCASTLE1 + 2:
    case WD_11BLOODCASTLE1 + 3:
    case WD_11BLOODCASTLE1 + 4:
    case WD_11BLOODCASTLE1 + 5:
    case WD_11BLOODCASTLE1 + 6:
    case WD_52BLOODCASTLE_MASTER_LEVEL:
        return Cloud;
    case WD_34CRYWOLF_1ST:
        return Crywolf;
    case WD_30BATTLECASTLE:
        return BattleCastle;
    case WD_31HUNTING_GROUND:
        return HuntingGround;
    case WD_33AIDA:
        return Cloud;
    case WD_68DOPPLEGANGER4:
        return Doppelganger4;
    case WD_38KANTURU_2ND:
        return Kanturu2;
    case WD_39KANTURU_3RD:
        return Kanturu3;
    case WD_45CURSEDTEMPLE_LV1:
    case WD_45CURSEDTEMPLE_LV2:
    case WD_45CURSEDTEMPLE_LV3:
    case WD_45CURSEDTEMPLE_LV4:
    case WD_45CURSEDTEMPLE_LV5:
    case WD_45CURSEDTEMPLE_LV6:
        return CursedTemple;
    case WD_41CHANGEUP3RD_1ST:
        return Changeup;
    case WD_42CHANGEUP3RD_2ND:
        return Changeup;
    case WD_51HOME_6TH_CHAR:
        return Elbeland;
    case WD_66DOPPLEGANGER2:
        return Doppelganger2;
    case WD_56MAP_SWAMP_OF_QUIET:
        return Swamp;
    case WD_65DOPPLEGANGER1:
        return Doppelganger1;
    case WD_57ICECITY:
    case WD_58ICECITY_BOSS:
        return IceCity;
    case WD_69EMPIREGUARDIAN1:
    case WD_70EMPIREGUARDIAN2:
    case WD_71EMPIREGUARDIAN3:
    case WD_72EMPIREGUARDIAN4:
        return EmpireGuardian;
    case WD_73NEW_LOGIN_SCENE:
        return LoginScene;
    case WD_74NEW_CHARACTER_SCENE:
        return CharacterScene;
#ifdef ASG_ADD_KARUTAN_MONSTERS
    case WD_80KARUTAN1:
    case WD_81KARUTAN2:
        return Karutan;
#endif
    default:
        return {};
    }
}

bool WorldResources::PrepareModelTextures(SessionKeeper &keeper, Failure &failure)
{
    modelTextures_ = std::make_unique<SessionTextureNamespace>(keeper.BitmapRegistry());
    for (auto &model : models_)
    {
        model.textures.clear();
        model.textures.reserve(model.asset->meshCount);
        for (int index = 0; index < model.asset->meshCount; ++index)
        {
            if (!PrepareModelTexture(keeper, model, index, failure))
                return false;
        }
    }
    return true;
}

bool WorldResources::PrepareModelTexture(SessionKeeper &keeper, PreparedModel &model, int index,
                                         Failure &failure)
{
    const char *name = model.asset->textures[index].FileName;
    if (std::string_view(name).starts_with("hid"))
    {
        model.textures.push_back({static_cast<std::uint32_t>(index),
                                  {},
                                  LegacyTextureFilter::Nearest,
                                  LegacyTextureWrap::Repeat});
        return true;
    }
    wchar_t filename[32]{};
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, filename, 32))
    {
        if (!model.required)
            return true;
        failure.resource = model.path;
        failure.detail = L"Invalid model texture filename";
        return false;
    }
    auto path = model.textureDirectory / filename;
    // The legacy loader keeps TGA nearest even for the linearly sampled logo.
    const auto filter = _wcsicmp(path.extension().c_str(), L".tga") == 0
                            ? LegacyTextureFilter::Nearest
                            : model.textureFilter;
    auto texture = modelTextures_->LoadUnnamed(path.wstring(), keeper.ErrorReport(), filter,
                                               LegacyTextureWrap::Repeat);
    if (texture == BITMAP_UNKNOWN)
    {
        const auto shared = WorldModelDependency::SharedTexturePath(model.path, filename);
        if (!shared.empty())
        {
            path = shared;
            texture = modelTextures_->LoadUnnamed(path.wstring(), keeper.ErrorReport(), filter,
                                                  LegacyTextureWrap::Repeat);
        }
    }
    if (texture == BITMAP_UNKNOWN)
    {
        if (!model.required)
            return true;
        failure.resource = path;
        failure.detail = L"Required model texture is missing or cannot be decoded";
        return false;
    }
    model.textures.push_back(
        {static_cast<std::uint32_t>(index), path.wstring(), filter, LegacyTextureWrap::Repeat});
    return true;
}

bool WorldResources::InstallModelTextures(SessionKeeper &keeper, const PreparedModel &prepared,
                                          BMD &model, Failure &failure) const
{
    for (const auto &texture : prepared.textures)
    {
        if (texture.path.empty())
        {
            model.IndexTexture[texture.slot] = BITMAP_HIDE;
            continue;
        }
        // The candidate pins this exact path/sampler through old-world retirement.
        const auto index = keeper.TextureNamespace().LoadUnnamed(texture.path, keeper.ErrorReport(),
                                                                 texture.filter, texture.wrap);
        if (index == BITMAP_UNKNOWN)
        {
            failure.resource = texture.path;
            failure.detail = L"Model texture installation failed";
            return false;
        }
        model.IndexTexture[texture.slot] = index;
    }
    return true;
}

bool WorldResources::InstallModelVisuals(SessionKeeper &keeper, Failure &failure) const
{
    for (const auto &prepared : models_)
    {
        auto &model = keeper.ModelPoolObject()[prepared.slot];
        if (!InstallModelTextures(keeper, prepared, model, failure))
            return false;
        model.PrepareRigidInstanceMeshes();
    }
    return true;
}

namespace
{
constexpr int TerrainTextureCount = 30;
constexpr int WaterFrameCount = 32;

std::filesystem::path TerrainTexturePath(const MapDefinition &map,
                                         const std::filesystem::path &root, int tile)
{
    static constexpr std::array names{L"TileGrass01.jpg",  L"TileGrass02.jpg",  L"TileGround01.jpg",
                                      L"TileGround02.jpg", L"TileGround03.jpg", L"TileWater01.jpg",
                                      L"TileWood01.jpg",   L"TileRock01.jpg",   L"TileRock02.jpg",
                                      L"TileRock03.jpg",   L"TileRock04.jpg",   L"TileRock05.jpg",
                                      L"TileRock06.jpg",   L"TileRock07.jpg"};
    const auto folder = root / (L"World" + std::to_wstring(map.assetSet));
    const int world = map.BehaviorMap();
    if (tile == 2 && world == WD_51HOME_6TH_CHAR)
        return folder / L"AlphaTileGround01.Tga";
    if (tile == 3 && world == WD_39KANTURU_3RD)
        return folder / L"AlphaTileGround02.Tga";
    if (tile == 4 && map.family == MapDefinition::Family::CursedTemple)
        return folder / L"AlphaTileGround03.Tga";
    if (tile == 10 && (map.family == MapDefinition::Family::EmpireGuardian ||
                       world == WD_73NEW_LOGIN_SCENE || world == WD_74NEW_CHARACTER_SCENE))
        return folder / L"AlphaTile01.Tga";
    if (tile == 11 && (world == WD_63PK_FIELD || world == WD_66DOPPLEGANGER2))
        return root / L"Object64/song_lava1.jpg";
#ifdef ASG_ADD_MAP_KARUTAN
    if (tile == 12 && map.family == MapDefinition::Family::Karutan)
        return folder / L"AlphaTile01.Tga";
#endif
    if (tile < names.size())
        return folder / names[tile];
    const int extension = tile - static_cast<int>(names.size()) + 1;
    return folder / (std::wstring(L"ExtTile") + (extension < 10 ? L"0" : L"") +
                     std::to_wstring(extension) + L".jpg");
}

bool IsDocumentedMissingTile(const MapDefinition &map, std::filesystem::path path)
{
    const auto name = path.filename();
    const bool known =
        (map.assetSet == 40 && (name == L"TileWood01.jpg" || name == L"TileRock05.jpg")) ||
        (map.assetSet == 42 && name == L"TileRock05.jpg") ||
        (map.assetSet == 64 && name == L"TileRock06.jpg");
    // The JPEG loader reads OZJ. A present but malformed replacement is never waived.
    return known && !std::filesystem::exists(path.replace_extension(L".OZJ"));
}

} // namespace

bool WorldResources::TryPrepareTerrainTexture(SessionKeeper &keeper, std::uint32_t slot,
                                              const std::filesystem::path &path,
                                              LegacyTextureFilter filter, LegacyTextureWrap wrap)
{
    if (!terrainTextures_->Load(slot, path.wstring(), keeper.ErrorReport(), filter, wrap))
        return false;
    preparedTextures_.push_back({slot, path.wstring(), filter, wrap});
    return true;
}

bool WorldResources::PrepareTerrainTexture(SessionKeeper &keeper, std::uint32_t slot,
                                           const std::filesystem::path &path,
                                           LegacyTextureFilter filter, LegacyTextureWrap wrap,
                                           bool required, Failure &failure)
{
    if (TryPrepareTerrainTexture(keeper, slot, path, filter, wrap) || !required)
        return true;
    failure.resource = path;
    failure.detail = L"Required terrain texture is missing or cannot be decoded";
    return false;
}

void WorldResources::PrepareSceneryTextures(SessionKeeper &keeper, const MapDefinition &map,
                                            const std::filesystem::path &root)
{
    if (map.family == MapDefinition::Family::Hellas)
        return;
    const auto folder = root / (L"World" + std::to_wstring(map.assetSet));
    const int raw = map.id.RawValue();
    const bool redGrass = raw == WD_63PK_FIELD || raw == WD_66DOPPLEGANGER2;
    const auto add = [&](std::uint32_t slot, const std::filesystem::path &path,
                         LegacyTextureFilter filter, LegacyTextureWrap wrap) {
        return TryPrepareTerrainTexture(keeper, slot, path, filter, wrap);
    };
    using Filter = LegacyTextureFilter;
    using Wrap = LegacyTextureWrap;
    add(BITMAP_MAPGRASS, folder / (redGrass ? L"TileGrass01_R.jpg" : L"TileGrass01.tga"),
        redGrass ? Filter::Linear : Filter::Nearest, Wrap::Repeat);
    add(BITMAP_MAPGRASS + 1, folder / L"TileGrass02.tga", Filter::Nearest, Wrap::Repeat);
    add(BITMAP_MAPGRASS + 2, folder / L"TileGrass03.tga", Filter::Nearest, Wrap::Repeat);
    // The legacy second leaf load preferred JPG except on these three maps.
    // Keep its TGA fallback, but retain only the final selected binding once.
    const bool tgaLeaf = raw == WD_0LORENCIA || raw == WD_3NORIA || raw == WD_63PK_FIELD;
    if (tgaLeaf || !add(BITMAP_LEAF1, folder / L"leaf01.jpg", Filter::Nearest, Wrap::ClampToEdge))
        add(BITMAP_LEAF1, folder / L"leaf01.tga", Filter::Nearest, Wrap::ClampToEdge);
    add(BITMAP_LEAF2, folder / L"leaf02.jpg", Filter::Nearest, Wrap::ClampToEdge);
    add(BITMAP_RAIN, root / L"World1" / (raw == WD_34CRYWOLF_1ST ? L"rain011.tga" : L"rain01.tga"),
        Filter::Nearest, Wrap::ClampToEdge);
    add(BITMAP_RAIN_CIRCLE, root / L"World1/rain02.tga", Filter::Nearest, Wrap::ClampToEdge);
    add(BITMAP_RAIN_CIRCLE + 1, root / L"World10/rain03.tga", Filter::Nearest, Wrap::ClampToEdge);
}

bool WorldResources::PrepareTerrainTextures(SessionKeeper &keeper, const MapDefinition &map,
                                            const std::filesystem::path &root, Failure &failure,
                                            const TerrainMappingData *effectiveMapping)
{
    admittedTiles_.reset();
    terrainTextures_ = std::make_unique<SessionTextureNamespace>(keeper.BitmapRegistry());
    preparedTextures_.clear();
    preparedTextures_.reserve(TerrainTextureCount + WaterFrameCount);
    const bool hellas = map.family == MapDefinition::Family::Hellas;
    auto required = hellas ? std::bitset<256>{}
                           : WorldTerrain::Surface::RequiredTiles(
                                 map, effectiveMapping ? *effectiveMapping : terrain_->surface,
                                 Attributes().Walls());
    for (int tile = TerrainTextureCount; tile < required.size(); ++tile)
    {
        if (!required[tile])
            continue;
        failure.resource = root / (L"World" + std::to_wstring(map.assetSet));
        failure.detail = L"Terrain references an undefined tile texture";
        return false;
    }
    if (hellas)
    {
        if (!PrepareTerrainTexture(keeper, BITMAP_MAPTILE, root / L"Object25/water1.tga",
                                   LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat, true,
                                   failure) ||
            !PrepareTerrainTexture(keeper, BITMAP_MAPTILE + 1, root / L"Object25/water2.jpg",
                                   LegacyTextureFilter::Nearest, LegacyTextureWrap::ClampToEdge,
                                   true, failure))
            return false;
    }
    else
        for (int tile = 0; tile < TerrainTextureCount; ++tile)
        {
            const auto path = TerrainTexturePath(map, root, tile);
            if (!PrepareTerrainTexture(keeper, BITMAP_MAPTILE + tile, path,
                                       LegacyTextureFilter::Nearest, LegacyTextureWrap::Repeat,
                                       required[tile], failure))
            {
                if (!IsDocumentedMissingTile(map, path))
                    return false;
                failure = {};
            }
            admittedTiles_[tile] = terrainTextures_->Resolve(BITMAP_MAPTILE + tile).has_value() ||
                                   IsDocumentedMissingTile(map, path);
        }
    const bool needsWaterFrames = hellas || map.family == MapDefinition::Family::BattleCastle ||
                                  map.BehaviorMap() == WD_7ATLANSE ||
                                  map.BehaviorMap() == WD_67DOPPLEGANGER3;
    for (int frame = 0; frame < WaterFrameCount; ++frame)
    {
        const auto path =
            root / L"Object8" /
            (std::wstring(L"wt") + (frame < 10 ? L"0" : L"") + std::to_wstring(frame) + L".jpg");
        if (!PrepareTerrainTexture(keeper, BITMAP_WATER + frame, path, LegacyTextureFilter::Linear,
                                   LegacyTextureWrap::Repeat, needsWaterFrames, failure))
            return false;
    }
    PrepareSceneryTextures(keeper, map, root);
    PrepareMinimap(keeper, map, root);
    return true;
}

bool WorldResources::InstallTerrainTextures(SessionKeeper &keeper, Failure &failure) const
{
    if (!InstallTextures(keeper, preparedTextures_, failure))
        return false;
    keeper.TerrainStorage().admittedTiles = admittedTiles_;
    return true;
}

bool WorldResources::InstallTextures(SessionKeeper &keeper,
                                     std::span<const PreparedTexture> textures,
                                     Failure &failure) const
{
    for (const auto &texture : textures)
    {
        // The candidate namespace pins the owner's path/sampler entry through activation.
        // Load only binds that existing decoded asset; it cannot reopen its source file.
        const auto current = keeper.TextureNamespace().TryDescribe(texture.slot);
        if (current && _wcsicmp(current->FileName.c_str(), texture.path.c_str()) == 0)
        {
            // A variant replaces the visit's binding, without adding another owner.
            // Restore the prepared base if this slot currently selects an animated frame.
            keeper.TextureNamespace().RestorePreparedBinding(texture.slot, *terrainTextures_);
            continue;
        }
        if (keeper.TextureNamespace().Load(texture.slot, texture.path, keeper.ErrorReport(),
                                           texture.filter, texture.wrap))
            continue;
        failure.resource = texture.path;
        failure.detail = L"World texture installation failed";
        return false;
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

//bool    showShoppingMall = false;

///////////////////////////////////////////////////////////////////////////////
// opengl render util
///////////////////////////////////////////////////////////////////////////////

// Saved camera state for save/restore around item rendering blocks.
// Item rendering calls gluPerspective2 (corrupts PerspectiveX/Y/ScreenCenter)
// and GetOpenGLMatrix(g_Camera.Matrix) (corrupts the camera matrix). Both must
// be restored so ScreenToWorldRay reads correct values for click detection.

// Perspective setup for item/3D-UI rendering. Sets GL perspective AND updates
// g_Camera perspective cache so item rendering can compute screen positions.
// Callers should wrap the entire item-rendering block in SaveCameraPerspective /
// RestoreCameraPerspective to avoid leaking FOV=1 values to ScreenToWorldRay.

///////////////////////////////////////////////////////////////////////////////
// render util
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// collision detect util
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

std::optional<LegacyTextureFilter> ToLegacyTextureFilter(unsigned int value) noexcept
{
    switch (value)
    {
    case GL_NEAREST:
        return LegacyTextureFilter::Nearest;
    case GL_LINEAR:
        return LegacyTextureFilter::Linear;
    default:
        return std::nullopt;
    }
}

std::optional<LegacyTextureWrap> ToLegacyTextureWrap(unsigned int value) noexcept
{
    switch (value)
    {
    case GL_REPEAT:
        return LegacyTextureWrap::Repeat;
    case GL_CLAMP:
        return LegacyTextureWrap::Clamp;
    case GL_CLAMP_TO_EDGE:
        return LegacyTextureWrap::ClampToEdge;
    default:
        return std::nullopt;
    }
}

bool WriteJpeg(const wchar_t *filename, int Width, int Height, unsigned char *Buffer, int quality)
{
    const auto fileCloser = [](FILE *fp) {
        if (fp != nullptr)
        {
            fclose(fp);
        }
    };
    std::unique_ptr<FILE, decltype(fileCloser)> outfile(_wfopen(filename, L"wb"), fileCloser);
    if (!outfile)
    {
        return false;
    }

    const auto jpegDestroyer = [](tjhandle handle) {
        if (handle != nullptr)
        {
            tjDestroy(handle);
        }
    };
    std::unique_ptr<void, decltype(jpegDestroyer)> handle(tjInitCompress(), jpegDestroyer);
    if (!handle)
    {
        return false;
    }

    const auto maxSize = tjBufSize(Width, Height, TJSAMP_444);
    std::vector<unsigned char> outputBuffer(maxSize);
    unsigned long jpegSize = maxSize;
    unsigned char *jpegPtr = outputBuffer.data();
    const int flags = TJFLAG_BOTTOMUP | TJFLAG_NOREALLOC;
    const auto result = tjCompress2(handle.get(), Buffer, Width, 0, Height, TJPF_RGB, &jpegPtr,
                                    &jpegSize, TJSAMP_444, quality, flags);

    if (result != 0)
    {
        return false;
    }

    const auto written = fwrite(jpegPtr, 1, jpegSize, outfile.get());
    return written == jpegSize;
}

namespace
{
std::wstring NormalizeExtension(const wchar_t *Ext)
{
    if (Ext == nullptr || Ext[0] == L'\0')
    {
        return {};
    }
    std::wstring result = Ext;
    if (result.front() != L'.')
    {
        result.insert(result.begin(), L'.');
    }
    return result;
}
} // namespace

void SaveImage(int HeaderSize, wchar_t *Ext, wchar_t *filename, BYTE *PakBuffer, int Size)
{
    if (filename == nullptr || Ext == nullptr)
    {
        return;
    }

    const bool hasExternalBuffer = (PakBuffer != nullptr && Size > 0);
    std::vector<unsigned char> localBuffer;
    if (!hasExternalBuffer)
    {
        std::wstring openFileName = L"Data2\\";
        openFileName += filename;
        const auto fileCloser = [](FILE *f) {
            if (f != nullptr)
            {
                fclose(f);
            }
        };
        std::unique_ptr<FILE, decltype(fileCloser)> fp(_wfopen(openFileName.c_str(), L"rb"),
                                                       fileCloser);
        if (!fp)
        {
            return;
        }
        fseek(fp.get(), 0, SEEK_END);
        const auto fileSize = ftell(fp.get());
        if (fileSize <= 0)
        {
            return;
        }
        fseek(fp.get(), 0, SEEK_SET);
        localBuffer.resize(static_cast<size_t>(fileSize));
        const auto read = fread(localBuffer.data(), 1, localBuffer.size(), fp.get());
        if (read != localBuffer.size())
        {
            return;
        }
    }

    const unsigned char *buffer = hasExternalBuffer ? PakBuffer : localBuffer.data();
    const size_t bufferSize = hasExternalBuffer ? static_cast<size_t>(Size) : localBuffer.size();
    if (buffer == nullptr || bufferSize == 0 || bufferSize < static_cast<size_t>(HeaderSize))
    {
        return;
    }

    std::vector<unsigned char> header(buffer, buffer + HeaderSize);

    std::wstring newFileName = filename;
    const auto normalizedExt = NormalizeExtension(Ext);
    const auto dotPos = newFileName.find_last_of(L'.');
    if (dotPos != std::wstring::npos)
    {
        newFileName = newFileName.substr(0, dotPos);
    }
    newFileName += normalizedExt;

    std::wstring saveFileName = L"Data\\";
    saveFileName += newFileName;

    const auto fileCloser = [](FILE *f) {
        if (f != nullptr)
        {
            fclose(f);
        }
    };
    std::unique_ptr<FILE, decltype(fileCloser)> fp(_wfopen(saveFileName.c_str(), L"wb"),
                                                   fileCloser);
    if (!fp)
    {
        return;
    }

    const auto headerWritten = fwrite(header.data(), 1, HeaderSize, fp.get());
    const auto dataWritten = fwrite(buffer, 1, bufferSize, fp.get());
    if (headerWritten != static_cast<size_t>(HeaderSize) || dataWritten != bufferSize)
    {
        return;
    }
}

bool WorldResources::InstallEffectTextures(SessionKeeper &keeper, Failure &failure) const
{
    auto textures = std::make_unique<SessionTextureNamespace>(keeper.BitmapRegistry());
    std::vector<std::uint32_t> slots;
    slots.reserve(preparedEffectTextures_.size());
    for (const auto &texture : preparedEffectTextures_)
    {
        if (!textures->Load(texture.slot, texture.path, keeper.ErrorReport(), texture.filter,
                            texture.wrap))
        {
            failure.resource = texture.path;
            failure.detail = L"World effect texture installation failed";
            return false;
        }
        slots.push_back(texture.slot);
    }
    keeper.WorldUnit()->InstallEffectTextures(std::move(textures), std::move(slots));
    return true;
}

bool WorldResources::InstallMinimapTexture(SessionKeeper &keeper, Failure &failure) const
{
    if (!minimapTexture_)
        return true;
    const auto &texture = *minimapTexture_;
    if (keeper.TextureNamespace().Load(texture.slot, texture.path, keeper.ErrorReport(),
                                       texture.filter, texture.wrap))
        return true;
    failure.resource = texture.path;
    failure.detail = L"Minimap texture installation failed";
    return false;
}

///////////////////////////////////////////
///////////////////////////////////////////

void SessionRenderUnit::DeleteNpcs()
{
    for (int i = MODEL_NPC_BEGIN; i < MODEL_NPC_END; i++)
    {
        if (BMD *model = Models.Find(i))
            model->Release();
    }

    for (int i = SOUND_NPC; i < SOUND_NPC_END; i++)
        ReleaseBuffer(i);
}

// Maps a monster model type to its enum identifier for diagnostic logging.
// Indexed directly by the dense, 0-based EMonsterModelType value; returns
// L"UNKNOWN" for values outside the table so the log always has a name.
// The static_assert keeps the table in sync if the enum grows.
void SessionRenderUnit::OpenImages()
{
    LoadBitmapW(L"Interface\\newui_number1.tga", BITMAP_INTERFACE_NEW_NUMBER_BEGIN,
                LegacyTextureFilter::Linear);
    LoadBitmapW(L"Interface\\command.jpg", BITMAP_SKILL_INTERFACE + 2);
    LoadBitmapW(L"Interface\\Item_Back01.jpg", BITMAP_INVENTORY);
    LoadBitmapW(L"Interface\\Item_Money.jpg", BITMAP_INVENTORY + 11);
    LoadBitmapW(L"Interface\\Item_box.jpg", BITMAP_INVENTORY + 17);
    LoadBitmapW(L"Interface\\InventoryBox2.jpg", BITMAP_INVENTORY + 18);
    LoadBitmapW(L"Interface\\Trading_line.jpg", BITMAP_INVENTORY + 19);
    LoadBitmapW(L"Interface\\exit_01.jpg", BITMAP_INVENTORY_BUTTON);
    LoadBitmapW(L"Interface\\exit_02.jpg", BITMAP_INVENTORY_BUTTON + 1);
    LoadBitmapW(L"Interface\\accept_box01.jpg", BITMAP_INVENTORY_BUTTON + 10);
    LoadBitmapW(L"Interface\\accept_box02.jpg", BITMAP_INVENTORY_BUTTON + 11);
    LoadBitmapW(L"Interface\\mix_button1.jpg", BITMAP_INVENTORY_BUTTON + 12);
    LoadBitmapW(L"Interface\\mix_button2.jpg", BITMAP_INVENTORY_BUTTON + 13);
    LoadBitmapW(L"Interface\\lock_01.jpg", BITMAP_INVENTORY_BUTTON + 14);
    LoadBitmapW(L"Interface\\lock_02.jpg", BITMAP_INVENTORY_BUTTON + 15);
    LoadBitmapW(L"Interface\\lock_03.jpg", BITMAP_INVENTORY_BUTTON + 16);
    LoadBitmapW(L"Interface\\lock_04.jpg", BITMAP_INVENTORY_BUTTON + 17);
    LoadBitmapW(L"Interface\\guild.tga", BITMAP_GUILD);
}

void SessionRenderUnit::OpenSounds()
{
    bool Enable3DSound = true;

    LoadWaveFile(SOUND_WIND01, L"Data\\Sound\\aWind.wav", 1);
    LoadWaveFile(SOUND_RAIN01, L"Data\\Sound\\aRain.wav", 1);
    LoadWaveFile(SOUND_DUNGEON01, L"Data\\Sound\\aDungeon.wav", 1);
    LoadWaveFile(SOUND_FOREST01, L"Data\\Sound\\aForest.wav", 1);
    LoadWaveFile(SOUND_TOWER01, L"Data\\Sound\\aTower.wav", 1);
    LoadWaveFile(SOUND_WATER01, L"Data\\Sound\\aWater.wav", 1);
    LoadWaveFile(SOUND_DESERT01, L"Data\\Sound\\desert.wav", 1);
    //LoadWaveFile(SOUND_BOSS01		    ,"Data\\Sound\\a쿤둔.wav",1);
    LoadWaveFile(SOUND_HUMAN_WALK_GROUND, L"Data\\Sound\\pWalk(Soil).wav", 2);
    LoadWaveFile(SOUND_HUMAN_WALK_GRASS, L"Data\\Sound\\pWalk(Grass).wav", 2);
    LoadWaveFile(SOUND_HUMAN_WALK_SNOW, L"Data\\Sound\\pWalk(Snow).wav", 2);
    LoadWaveFile(SOUND_HUMAN_WALK_SWIM, L"Data\\Sound\\pSwim.wav", 2);

    LoadWaveFile(SOUND_BIRD01, L"Data\\Sound\\aBird1.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_BIRD02, L"Data\\Sound\\aBird2.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_BAT01, L"Data\\Sound\\aBat.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_RAT01, L"Data\\Sound\\aMouse.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_TRAP01, L"Data\\Sound\\aGrate.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_DOOR01, L"Data\\Sound\\aDoor.wav", 1);
    LoadWaveFile(SOUND_DOOR02, L"Data\\Sound\\aCastleDoor.wav", 1);

    LoadWaveFile(SOUND_HEAVEN01, L"Data\\Sound\\aHeaven.wav", 1);
    LoadWaveFile(SOUND_THUNDERS01, L"Data\\Sound\\aThunder01.wav", 1);
    LoadWaveFile(SOUND_THUNDERS02, L"Data\\Sound\\aThunder02.wav", 1);
    LoadWaveFile(SOUND_THUNDERS03, L"Data\\Sound\\aThunder03.wav", 1);

    //attack
    LoadWaveFile(SOUND_BRANDISH_SWORD01, L"Data\\Sound\\eSwingWeapon1.wav", 2);
    LoadWaveFile(SOUND_BRANDISH_SWORD02, L"Data\\Sound\\eSwingWeapon2.wav", 2);
    LoadWaveFile(SOUND_BRANDISH_SWORD03, L"Data\\Sound\\eSwingLightSword.wav", 2);
    LoadWaveFile(SOUND_BOW01, L"Data\\Sound\\eBow.wav", 2);
    LoadWaveFile(SOUND_CROSSBOW01, L"Data\\Sound\\eCrossbow.wav", 2);
    LoadWaveFile(SOUND_MIX01, L"Data\\Sound\\eMix.wav", 2);

    //player
    LoadWaveFile(SOUND_DRINK01, L"Data\\Sound\\pDrink.wav", 1);
    LoadWaveFile(SOUND_EAT_APPLE01, L"Data\\Sound\\pEatApple.wav", 1);
    LoadWaveFile(SOUND_HEART, L"Data\\Sound\\pHeartBeat.wav", 1);
    LoadWaveFile(SOUND_GET_ENERGY, L"Data\\Sound\\pEnergy.wav", 1);
    LoadWaveFile(SOUND_HUMAN_SCREAM01, L"Data\\Sound\\pMaleScream1.wav", 2);
    LoadWaveFile(SOUND_HUMAN_SCREAM02, L"Data\\Sound\\pMaleScream2.wav", 2);
    LoadWaveFile(SOUND_HUMAN_SCREAM03, L"Data\\Sound\\pMaleScream3.wav", 2);
    LoadWaveFile(SOUND_HUMAN_SCREAM04, L"Data\\Sound\\pMaleDie.wav", 2);
    LoadWaveFile(SOUND_FEMALE_SCREAM01, L"Data\\Sound\\pFemaleScream1.wav", 2);
    LoadWaveFile(SOUND_FEMALE_SCREAM02, L"Data\\Sound\\pFemaleScream2.wav", 2);

    LoadWaveFile(SOUND_DROP_ITEM01, L"Data\\Sound\\pDropItem.wav", 1);
    LoadWaveFile(SOUND_DROP_GOLD01, L"Data\\Sound\\pDropMoney.wav", 1);
    LoadWaveFile(SOUND_JEWEL01, L"Data\\Sound\\eGem.wav", 1);
    LoadWaveFile(SOUND_GET_ITEM01, L"Data\\Sound\\pGetItem.wav", 1);
    //LoadWaveFile(SOUND_SHOUT01    		,"Data\\Sound\\p기합.wav",1);

    //skill
    LoadWaveFile(SOUND_SKILL_DEFENSE, L"Data\\Sound\\sKnightDefense.wav", 1);
    LoadWaveFile(SOUND_SKILL_SWORD1, L"Data\\Sound\\sKnightSkill1.wav", 1);
    LoadWaveFile(SOUND_SKILL_SWORD2, L"Data\\Sound\\sKnightSkill2.wav", 1);
    LoadWaveFile(SOUND_SKILL_SWORD3, L"Data\\Sound\\sKnightSkill3.wav", 1);
    LoadWaveFile(SOUND_SKILL_SWORD4, L"Data\\Sound\\sKnightSkill4.wav", 1);
    LoadWaveFile(SOUND_MONSTER_SHADOWATTACK2, L"Data\\Sound\\mShadowAttack1.wav", 1);

    LoadWaveFile(SOUND_STORM, L"Data\\Sound\\sTornado.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_EVIL, L"Data\\Sound\\sEvil.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_MAGIC, L"Data\\Sound\\sMagic.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_HELLFIRE, L"Data\\Sound\\sHellFire.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_ICE, L"Data\\Sound\\sIce.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_FLAME, L"Data\\Sound\\sFlame.wav", 2, Enable3DSound);
    //LoadWaveFile(SOUND_FLASH            ,"Data\\Sound\\m히드라공격1.wav",2,Enable3DSound);
    LoadWaveFile(SOUND_FLASH, L"Data\\Sound\\sAquaFlash.wav", 2, Enable3DSound);

    LoadWaveFile(SOUND_BREAK01, L"Data\\Sound\\eBreak.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_EXPLOTION01, L"Data\\Sound\\eExplosion.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_METEORITE01, L"Data\\Sound\\eMeteorite.wav", 2, Enable3DSound);
    //LoadWaveFile(SOUND_METEORITE02	    ,"Data\\Sound\\e유성.wav",2,Enable3DSound);
    LoadWaveFile(SOUND_THUNDER01, L"Data\\Sound\\eThunder.wav", 1, Enable3DSound);

    LoadWaveFile(SOUND_BONE1, L"Data\\Sound\\mBone1.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_BONE2, L"Data\\Sound\\mBone2.wav", 2, Enable3DSound);
    LoadWaveFile(SOUND_ASSASSIN, L"Data\\Sound\\mAssassin1.wav", 1, Enable3DSound);

    LoadWaveFile(SOUND_ATTACK_MELEE_HIT1, L"Data\\Sound\\eMeleeHit1.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MELEE_HIT2, L"Data\\Sound\\eMeleeHit2.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MELEE_HIT3, L"Data\\Sound\\eMeleeHit3.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MELEE_HIT4, L"Data\\Sound\\eMeleeHit4.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MELEE_HIT5, L"Data\\Sound\\eMeleeHit5.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MISSILE_HIT1, L"Data\\Sound\\eMissileHit1.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MISSILE_HIT2, L"Data\\Sound\\eMissileHit2.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MISSILE_HIT3, L"Data\\Sound\\eMissileHit3.wav", 2);
    LoadWaveFile(SOUND_ATTACK_MISSILE_HIT4, L"Data\\Sound\\eMissileHit4.wav", 2);

    LoadWaveFile(SOUND_FIRECRACKER1, L"Data\\Sound\\eFirecracker1.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_FIRECRACKER2, L"Data\\Sound\\eFirecracker2.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_MEDAL, L"Data\\Sound\\eMedal.wav", 1, Enable3DSound);
    LoadWaveFile(SOUND_PHOENIXEXP, L"Data\\Sound\\ePhoenixExp.wav", 1, Enable3DSound);
    //	LoadWaveFile(SOUND_PHOENIXFIRE		,"Data\\Sound\\ePhoenixFire.wav",1,Enable3DSound);

    LoadWaveFile(SOUND_RIDINGSPEAR, L"Data\\Sound\\eRidingSpear.wav", 1);
    LoadWaveFile(SOUND_RAIDSHOOT, L"Data\\Sound\\eRaidShoot.wav", 1);
    LoadWaveFile(SOUND_SWELLLIFE, L"Data\\Sound\\eSwellLife.wav", 1);
    LoadWaveFile(SOUND_PIERCING, L"Data\\Sound\\ePiercing.wav", 1);
    LoadWaveFile(SOUND_ICEARROW, L"Data\\Sound\\eIceArrow.wav", 1);
    LoadWaveFile(SOUND_TELEKINESIS, L"Data\\Sound\\eTelekinesis.wav", 1);
    LoadWaveFile(SOUND_SOULBARRIER, L"Data\\Sound\\eSoulBarrier.wav", 1);
    LoadWaveFile(SOUND_BLOODATTACK, L"Data\\Sound\\eBloodAttack.wav", 1);

    LoadWaveFile(SOUND_HIT_GATE, L"Data\\Sound\\eHitGate.wav", 1);
    LoadWaveFile(SOUND_HIT_GATE2, L"Data\\Sound\\eHitGate2.wav", 1);
    LoadWaveFile(SOUND_HIT_CRISTAL, L"Data\\Sound\\eHitCristal.wav", 1);
    LoadWaveFile(SOUND_DOWN_GATE, L"Data\\Sound\\eDownGate.wav", 1);
    LoadWaveFile(SOUND_CROW, L"Data\\Sound\\eCrow.wav", 1);

    LoadWaveFile(SOUND_DEATH_POISON1, L"Data\\Sound\\eBlastPoison_1.wav", 2);
    LoadWaveFile(SOUND_DEATH_POISON2, L"Data\\Sound\\eBlastPoison_2.wav", 2);
    LoadWaveFile(SOUND_SUDDEN_ICE1, L"Data\\Sound\\eSuddenIce_1.wav", 2);
    LoadWaveFile(SOUND_SUDDEN_ICE2, L"Data\\Sound\\eSuddenIce_2.wav", 2);
    LoadWaveFile(SOUND_NUKE1, L"Data\\Sound\\eHellFire2_1.wav", 1);
    LoadWaveFile(SOUND_NUKE2, L"Data\\Sound\\eHellFire2_2.wav", 1);
    LoadWaveFile(SOUND_COMBO, L"Data\\Sound\\eCombo.wav", 1);
    LoadWaveFile(SOUND_FURY_STRIKE1, L"Data\\Sound\\eRageBlow_1.wav", 1);
    LoadWaveFile(SOUND_FURY_STRIKE2, L"Data\\Sound\\eRageBlow_2.wav", 1);
    LoadWaveFile(SOUND_FURY_STRIKE3, L"Data\\Sound\\eRageBlow_3.wav", 1);
    LoadWaveFile(SOUND_LEVEL_UP, L"Data\\Sound\\pLevelUp.wav", 1);
    LoadWaveFile(SOUND_CHANGE_UP, L"Data\\Sound\\nMalonSkillMaster.wav", 1);

    LoadWaveFile(SOUND_CHAOS_ENVIR, L"Data\\Sound\\aChaos.wav", 1);
    LoadWaveFile(SOUND_CHAOS_END, L"Data\\Sound\\aChaosEnd.wav", 1);
    LoadWaveFile(SOUND_CHAOS_FALLING, L"Data\\Sound\\pMaleScream.wav", 1);
    LoadWaveFile(SOUND_CHAOS_FALLING_STONE, L"Data\\Sound\\eWallFall.wav", 1);
    LoadWaveFile(SOUND_CHAOS_MOB_BOOM01, L"Data\\Sound\\eMonsterBoom1.wav", 2);
    LoadWaveFile(SOUND_CHAOS_MOB_BOOM02, L"Data\\Sound\\eMonsterBoom2.wav", 2);
    LoadWaveFile(SOUND_CHAOS_THUNDER01, L"Data\\Sound\\eElec1.wav", 1);
    LoadWaveFile(SOUND_CHAOS_THUNDER02, L"Data\\Sound\\eElec2.wav", 1);

    LoadWaveFile(SOUND_RUN_DARK_HORSE_1, L"Data\\Sound\\pHorseStep1.wav", 1);
    LoadWaveFile(SOUND_RUN_DARK_HORSE_2, L"Data\\Sound\\pHorseStep2.wav", 1);
    LoadWaveFile(SOUND_RUN_DARK_HORSE_3, L"Data\\Sound\\pHorseStep3.wav", 1);
    LoadWaveFile(SOUND_DARKLORD_PAIN, L"Data\\Sound\\pDarkPain.wav", 1);
    LoadWaveFile(SOUND_DARKLORD_DEAD, L"Data\\Sound\\pDarkDeath.wav", 1);
    LoadWaveFile(SOUND_ATTACK_SPEAR, L"Data\\Sound\\sDarkSpear.wav", 1);
    LoadWaveFile(SOUND_ATTACK_FIRE_BUST, L"Data\\Sound\\eFirebust.wav", 1);
    LoadWaveFile(SOUND_ATTACK_FIRE_BUST_EXP, L"Data\\Sound\\eFirebustBoom.wav", 1);
    LoadWaveFile(SOUND_PART_TELEPORT, L"Data\\Sound\\eSummon.wav", 1);
    LoadWaveFile(SOUND_ELEC_STRIKE, L"Data\\Sound\\sDarkElecSpike.wav", 1);
    LoadWaveFile(SOUND_ELEC_STRIKE_READY, L"Data\\Sound\\sDarkElecSpikeReady.wav", 1);
    LoadWaveFile(SOUND_EARTH_QUAKE, L"Data\\Sound\\sDarkEarthQuake.wav", 1);
    LoadWaveFile(SOUND_CRITICAL, L"Data\\Sound\\sDarkCritical.wav", 1);
    LoadWaveFile(SOUND_DSPIRIT_MISSILE, L"Data\\Sound\\DSpirit_Missile.wav", 4);
    LoadWaveFile(SOUND_DSPIRIT_SHOUT, L"Data\\Sound\\DSpirit_Shout.wav", 1);
    LoadWaveFile(SOUND_DSPIRIT_RUSH, L"Data\\Sound\\DSpirit_Rush.wav", 3);

    LoadWaveFile(SOUND_FENRIR_RUN_1, L"Data\\Sound\\pW_run-01.wav", 1);
    LoadWaveFile(SOUND_FENRIR_RUN_2, L"Data\\Sound\\pW_run-02.wav", 1);
    LoadWaveFile(SOUND_FENRIR_RUN_3, L"Data\\Sound\\pW_run-03.wav", 1);
    LoadWaveFile(SOUND_FENRIR_WALK_1, L"Data\\Sound\\pW_step-01.wav", 1);
    LoadWaveFile(SOUND_FENRIR_WALK_2, L"Data\\Sound\\pW_step-02.wav", 1);
    LoadWaveFile(SOUND_FENRIR_DEATH, L"Data\\Sound\\pWdeath.wav", 1);
    LoadWaveFile(SOUND_FENRIR_IDLE_1, L"Data\\Sound\\pWidle1.wav", 1);
    LoadWaveFile(SOUND_FENRIR_IDLE_2, L"Data\\Sound\\pWidle2.wav", 1);
    LoadWaveFile(SOUND_FENRIR_DAMAGE_1, L"Data\\Sound\\pWpain1.wav", 1);
    LoadWaveFile(SOUND_FENRIR_DAMAGE_1, L"Data\\Sound\\pWpain2.wav", 1);
    LoadWaveFile(SOUND_FENRIR_SKILL, L"Data\\Sound\\pWskill.wav", 1);
    LoadWaveFile(SOUND_JEWEL02, L"Data\\Sound\\Jewel_Sound.wav", 1);

    LoadWaveFile(SOUND_KUNDUN_ITEM_SOUND, L"Data\\Sound\\kundunitem.wav", 1);

    gameplay_.XmasEvent().LoadXmasEventSound();
    gameplay_.NewYearsDayEvent().LoadSound();

    LoadWaveFile(SOUND_SHIELDCLASH, L"Data\\Sound\\shieldclash.wav", 1);
    LoadWaveFile(SOUND_INFINITYARROW, L"Data\\Sound\\infinityArrow.wav", 1);
    //SOUND_FIRE_SCREAM
    LoadWaveFile(SOUND_FIRE_SCREAM, L"Data\\Sound\\Darklord_firescream.wav", 1);

    LoadWaveFile(SOUND_MOONRABBIT_WALK, L"Data\\Sound\\SE_Ev_rabbit_walk.wav", 1);
    LoadWaveFile(SOUND_MOONRABBIT_DAMAGE, L"Data\\Sound\\SE_Ev_rabbit_damage.wav", 1);
    LoadWaveFile(SOUND_MOONRABBIT_DEAD, L"Data\\Sound\\SE_Ev_rabbit_death.wav", 1);
    LoadWaveFile(SOUND_MOONRABBIT_EXPLOSION, L"Data\\Sound\\SE_Ev_rabbit_Explosion.wav", 1);

    //	LoadWaveFile(SOUND_SUMMON_CASTING,		"Data\\Sound\\eSummon.wav"	,1);
    LoadWaveFile(SOUND_SUMMON_SAHAMUTT, L"Data\\Sound\\SE_Ch_summoner_skill05_explosion01.wav", 1);
    LoadWaveFile(SOUND_SUMMON_EXPLOSION, L"Data\\Sound\\SE_Ch_summoner_skill05_explosion03.wav", 1);
    LoadWaveFile(SOUND_SUMMON_NEIL, L"Data\\Sound\\SE_Ch_summoner_skill06_requiem01.wav", 1);
    LoadWaveFile(SOUND_SUMMON_REQUIEM, L"Data\\Sound\\SE_Ch_summoner_skill06_requiem02.wav", 1);
    LoadWaveFile(SOUND_SUMMOM_RARGLE, L"Data\\Sound\\Rargle.wav", 1);
    LoadWaveFile(SOUND_SUMMON_SKILL_LIGHTORB,
                 L"Data\\Sound\\SE_Ch_summoner_skill01_lightningof.wav", 1);
    LoadWaveFile(SOUND_SUMMON_SKILL_SLEEP, L"Data\\Sound\\SE_Ch_summoner_skill03_sleep.wav", 1);
    LoadWaveFile(SOUND_SUMMON_SKILL_BLIND, L"Data\\Sound\\SE_Ch_summoner_skill04_blind.wav", 1);
    LoadWaveFile(SOUND_SUMMON_SKILL_THORNS, L"Data\\Sound\\SE_Ch_summoner_skill02_ssonze.wav", 1);
    LoadWaveFile(SOUND_SKILL_CHAIN_LIGHTNING,
                 L"Data\\Sound\\SE_Ch_summoner_skill08_chainlightning.wav", 1);
    LoadWaveFile(SOUND_SKILL_DRAIN_LIFE, L"Data\\Sound\\SE_Ch_summoner_skill07_lifedrain.wav", 1);
    LoadWaveFile(SOUND_SKILL_WEAKNESS, L"Data\\Sound\\SE_Ch_summoner_weakness.wav", 1);
    LoadWaveFile(SOUND_SKILL_ENERVATION, L"Data\\Sound\\SE_Ch_summoner_innovation.wav", 1);
    LoadWaveFile(SOUND_SKILL_BERSERKER, L"Data\\Sound\\Berserker.wav", 1);
    LoadWaveFile(SOUND_CHERRYBLOSSOM_EFFECT0,
                 L"Data\\Sound\\cherryblossom\\Eve_CherryBlossoms01.wav");
    LoadWaveFile(SOUND_CHERRYBLOSSOM_EFFECT1,
                 L"Data\\Sound\\cherryblossom\\Eve_CherryBlossoms02.wav");
    LoadWaveFile(SOUND_SKILL_BLOWOFDESTRUCTION, L"Data\\Sound\\BLOW_OF_DESTRUCTION.wav");
    LoadWaveFile(SOUND_SKILL_FLAME_STRIKE, L"Data\\Sound\\flame_strike.wav");
    LoadWaveFile(SOUND_SKILL_GIGANTIC_STORM, L"Data\\Sound\\gigantic_storm.wav");
    LoadWaveFile(SOUND_SKILL_LIGHTNING_SHOCK, L"Data\\Sound\\lightning_shock.wav");
    LoadWaveFile(SOUND_SKILL_SWELL_OF_MAGICPOWER, L"Data\\Sound\\SwellofMagicPower.wav");
    LoadWaveFile(SOUND_SKILL_MULTI_SHOT, L"Data\\Sound\\multi_shot.wav");
    LoadWaveFile(SOUND_SKILL_RECOVER, L"Data\\Sound\\recover.wav");
    LoadWaveFile(SOUND_SKILL_CAOTIC, L"Data\\Sound\\caotic.wav");

    LoadWaveFile(SOUND_XMAS_FIRECRACKER, L"Data\\Sound\\xmas\\Christmas_Fireworks01.wav");

    gameplay_.SummerEvent().LoadSound();

    LoadWaveFile(SOUND_RAGESKILL_THRUST, L"Data\\Sound\\Ragefighter\\Rage_Thrust.wav");
    LoadWaveFile(SOUND_RAGESKILL_THRUST_ATTACK, L"Data\\Sound\\Ragefighter\\Rage_Thrust_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_STAMP, L"Data\\Sound\\Ragefighter\\Rage_Stamp.wav");
    LoadWaveFile(SOUND_RAGESKILL_STAMP_ATTACK, L"Data\\Sound\\Ragefighter\\Rage_Stamp_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_GIANTSWING, L"Data\\Sound\\Ragefighter\\Rage_Giantswing.wav");
    LoadWaveFile(SOUND_RAGESKILL_GIANTSWING_ATTACK,
                 L"Data\\Sound\\Ragefighter\\Rage_Giantswing_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_DARKSIDE, L"Data\\Sound\\Ragefighter\\Rage_Darkside.wav");
    LoadWaveFile(SOUND_RAGESKILL_DARKSIDE_ATTACK,
                 L"Data\\Sound\\Ragefighter\\Rage_Darkside_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_DRAGONLOWER, L"Data\\Sound\\Ragefighter\\Rage_Dragonlower.wav");
    LoadWaveFile(SOUND_RAGESKILL_DRAGONLOWER_ATTACK,
                 L"Data\\Sound\\Ragefighter\\Rage_Dragonlower_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_DRAGONKICK, L"Data\\Sound\\Ragefighter\\Rage_Dragonkick.wav");
    LoadWaveFile(SOUND_RAGESKILL_DRAGONKICK_ATTACK,
                 L"Data\\Sound\\Ragefighter\\Rage_Dragonkick_Att.wav");
    LoadWaveFile(SOUND_RAGESKILL_BUFF_1, L"Data\\Sound\\Ragefighter\\Rage_Buff_1.wav");
    LoadWaveFile(SOUND_RAGESKILL_BUFF_2, L"Data\\Sound\\Ragefighter\\Rage_Buff_2.wav");
}

bool SessionRenderUnit::OpenFont()
{
    InitPath();

    LoadBitmapW(L"Interface\\FontInput.tga", BITMAP_FONT, LegacyTextureFilter::Nearest,
                LegacyTextureWrap::ClampToEdge);
    LoadBitmapW(L"Interface\\FontTest.tga", BITMAP_FONT + 1);
    LoadBitmapW(L"Interface\\Hit.tga", BITMAP_FONT_HIT, LegacyTextureFilter::Nearest,
                LegacyTextureWrap::ClampToEdge);

    return g_RenderText.Create();
}

void SessionRenderUnit::OpenLegacySystemWindowTextures()
{
    LoadBitmapW(L"Interface\\op1_stone.jpg", BITMAP_SYS_WIN, LegacyTextureFilter::Nearest,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Interface\\op1_back1.tga", BITMAP_SYS_WIN + 1);
    LoadBitmapW(L"Interface\\op1_back2.tga", BITMAP_SYS_WIN + 2);
    LoadBitmapW(L"Interface\\op1_back3.jpg", BITMAP_SYS_WIN + 3, LegacyTextureFilter::Nearest,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Interface\\op1_back4.jpg", BITMAP_SYS_WIN + 4, LegacyTextureFilter::Nearest,
                LegacyTextureWrap::Repeat);
    LoadBitmapW(L"Interface\\op1_b_all.tga", BITMAP_TEXT_BTN);
}

void SessionRenderUnit::ReleaseCharacterSceneData()
{
    if (BMD *model = Models.Find(MODEL_LOGO + 4))
        model->Release();

    for (int i = 0; i < MAX_CLASS; i++)
        if (BMD *model = Models.Find(MODEL_FACE + i))
            model->Release();
}

namespace
{
constexpr std::uint32_t BasicDataLoadStepCount = 10;
}

void SessionRenderUnit::OpenBasicData()
{
    for (std::uint32_t step = 1; step <= BasicDataLoadStepCount; ++step)
    {
        OpenBasicDataStep(step);
    }
}

void SessionRenderUnit::OpenBasicDataStep(std::uint32_t step)
{
    const auto notifyDataLoadFailure = [this](const wchar_t *message) {
        g_ErrorReport.Write(L"%ls\r\n", message);
        sessionKeeper_.WorldUnit()->FinishLoad(false);
        throw std::runtime_error("Required session data failed");
    };

    switch (step)
    {
    case 1: {

        LoadBitmapW(L"Interface\\Cursor.tga", BITMAP_CURSOR, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorPush.tga", BITMAP_CURSOR + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorAttack.tga", BITMAP_CURSOR + 2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorGet.tga", BITMAP_CURSOR + 3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorTalk.tga", BITMAP_CURSOR + 4, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorRepair.tga", BITMAP_CURSOR + 5, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorLeanAgainst.tga", BITMAP_CURSOR + 6, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorSitDown.tga", BITMAP_CURSOR + 7, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorDontMove.tga", BITMAP_CURSOR + 8, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        // interface
        LoadBitmapW(L"Interface\\ok.jpg", BITMAP_INTERFACE + 11);
        LoadBitmapW(L"Interface\\ok2.jpg", BITMAP_INTERFACE + 12);
        LoadBitmapW(L"Interface\\cancel.jpg", BITMAP_INTERFACE + 13);
        LoadBitmapW(L"Interface\\cancel2.jpg", BITMAP_INTERFACE + 14);
        LoadBitmapW(L"Interface\\win_titlebar.jpg", BITMAP_INTERFACE_EX + 8);
        LoadBitmapW(L"Interface\\win_button.tga", BITMAP_INTERFACE_EX + 9);
        LoadBitmapW(L"Interface\\win_size.jpg", BITMAP_INTERFACE_EX + 10);
        LoadBitmapW(L"Interface\\win_resize.tga", BITMAP_INTERFACE_EX + 11);
        LoadBitmapW(L"Interface\\win_scrollbar.jpg", BITMAP_INTERFACE_EX + 12);
        LoadBitmapW(L"Interface\\win_check.tga", BITMAP_INTERFACE_EX + 13);
        LoadBitmapW(L"Interface\\win_mail.tga", BITMAP_INTERFACE_EX + 14);
        LoadBitmapW(L"Interface\\win_mark.tga", BITMAP_INTERFACE_EX + 15);
        LoadBitmapW(L"Interface\\win_letter.jpg", BITMAP_INTERFACE_EX + 16);
        LoadBitmapW(L"Interface\\win_man.jpg", BITMAP_INTERFACE_EX + 17);
        LoadBitmapW(L"Interface\\win_push.jpg", BITMAP_INTERFACE_EX + 18);
        LoadBitmapW(L"Interface\\win_question.tga", BITMAP_INTERFACE_EX + 20);
        LoadBitmapW(L"Local\\Webzenlogo.jpg", BITMAP_INTERFACE_EX + 22);

#ifdef DUEL_SYSTEM
        LoadWaveFile(SOUND_OPEN_DUELWINDOW, L"Data\\Sound\\iDuelWindow.wav", 1);
        LoadWaveFile(SOUND_START_DUEL, L"Data\\Sound\\iDuelStart.wav", 1);
#endif // DUEL_SYSTEM

        LoadBitmapW(L"Interface\\CursorID.tga", BITMAP_INTERFACE_EX + 29);
        LoadBitmapW(L"Interface\\bar.jpg", BITMAP_INTERFACE + 23);
        LoadBitmapW(L"Interface\\back1.jpg", BITMAP_INTERFACE + 24);
        LoadBitmapW(L"Interface\\back2.jpg", BITMAP_INTERFACE + 25);
        LoadBitmapW(L"Interface\\back3.jpg", BITMAP_INTERFACE + 26);

        LoadBitmapW(L"Effect\\Fire01.jpg", BITMAP_FIRE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM3rdChangeUp, GMCrywolf1st,GMHellas,Kanturu 3rd
        LoadBitmapW(L"Effect\\Fire02.jpg", BITMAP_FIRE + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM3rdChangeUp, GMCrywolf1st,GMHellas,Kanturu 3rd
        LoadBitmapW(L"Effect\\Fire03.jpg", BITMAP_FIRE + 2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM3rdChangeUp, GMCrywolf1st,GMHellas,Kanturu 3rd
        LoadBitmapW(L"Effect\\PoundingBall.jpg", BITMAP_POUNDING_BALL, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // Kanturu 2nd
        LoadBitmapW(L"Effect\\fi01.jpg", BITMAP_ADV_SMOKE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM 3rd ChangeUp, CryingWolf2nd
        LoadBitmapW(L"Effect\\fi02.tga", BITMAP_ADV_SMOKE + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM 3rd ChangeUp, CryingWolf2nd
        LoadBitmapW(L"Effect\\fantaF.jpg", BITMAP_TRUE_FIRE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge); // GM Aida, GMBattleCastle, ....
        LoadBitmapW(L"Effect\\fantaB.jpg", BITMAP_TRUE_BLUE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\JointSpirit02.jpg", BITMAP_JOINT_SPIRIT2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Piercing.jpg", BITMAP_PIERCING, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Monster\\iui06.jpg", BITMAP_ROBE + 6);
        LoadBitmapW(L"Effect\\Magic_b.jpg", BITMAP_MAGIC_EMBLEM);
        LoadBitmapW(L"Player\\dark3chima3.tga", BITMAP_DARKLOAD_SKIRT_3RD, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Player\\kaa.tga", BITMAP_DARK_LOAD_SKIRT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\ShockWave.jpg", BITMAP_SHOCK_WAVE, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Effect\\Flame01.jpg", BITMAP_FLAME, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\flare01.jpg", BITMAP_LIGHT, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Magic_Ground1.jpg", BITMAP_MAGIC, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Magic_Ground2.jpg", BITMAP_MAGIC + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Magic_Circle1.jpg", BITMAP_MAGIC + 2, LegacyTextureFilter::Linear);
#ifdef ASG_ADD_INFLUENCE_GROUND_EFFECT
        LoadBitmapW(L"Effect\\guild_ring01.jpg", BITMAP_OUR_INFLUENCE_GROUND, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\enemy_ring02.jpg", BITMAP_ENEMY_INFLUENCE_GROUND, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
#endif // ASG_ADD_INFLUENCE_GROUND_EFFECT
        LoadBitmapW(L"Effect\\Spark02.jpg", BITMAP_SPARK, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Spark03.jpg", BITMAP_SPARK + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\smoke01.jpg", BITMAP_SMOKE, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\smoke02.tga", BITMAP_SMOKE + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\smoke05.tga", BITMAP_SMOKE + 4, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\blood01.tga", BITMAP_BLOOD, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\blood.tga", BITMAP_BLOOD + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Explotion01.jpg", BITMAP_EXPLOTION, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Skill\\twlighthik01.jpg", BITMAP_TWLIGHT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Skill\\2line_gost.jpg", BITMAP_2LINE_GHOST, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\damage01mono.jpg", BITMAP_DAMAGE_01_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\SwordEff_mono.jpg", BITMAP_SWORD_EFFECT_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\flamestani.jpg", BITMAP_FLAMESTANI, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Effect\\Spark.jpg", BITMAP_SPARK + 2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firehik02.jpg", BITMAP_FIRE_CURSEDLICH, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\totemgolem_leaf.tga", BITMAP_LEAF_TOTEMGOLEM, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\empact01.jpg", BITMAP_SUMMON_IMPACT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\loungexflow.jpg", BITMAP_SUMMON_SAHAMUTT_EXPLOSION, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\gostmark01.jpg", BITMAP_DRAIN_LIFE_GHOST, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\mzine_typer2.jpg", BITMAP_MAGIC_ZIN, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\shiny05.jpg", BITMAP_SHINY + 6, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\hikorora.jpg", BITMAP_ORORA, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\lightmarks.jpg", BITMAP_LIGHT_MARKS, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\cursorpin01.jpg", BITMAP_TARGET_POSITION_EFFECT1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\cursorpin02.jpg", BITMAP_TARGET_POSITION_EFFECT2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\smokelines01.jpg", BITMAP_SMOKELINE1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\smokelines02.jpg", BITMAP_SMOKELINE2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\smokelines03.jpg", BITMAP_SMOKELINE3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\lighting_mega01.jpg", BITMAP_LIGHTNING_MEGA1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\lighting_mega02.jpg", BITMAP_LIGHTNING_MEGA2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\lighting_mega03.jpg", BITMAP_LIGHTNING_MEGA3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\firehik01.jpg", BITMAP_FIRE_HIK1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firehik03.jpg", BITMAP_FIRE_HIK3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firehik_mono01.jpg", BITMAP_FIRE_HIK1_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firehik_mono02.jpg", BITMAP_FIRE_HIK2_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firehik_mono03.jpg", BITMAP_FIRE_HIK3_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\clouds3.jpg", BITMAP_RAKLION_CLOUDS, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\icenightlight.jpg", BITMAP_IRONKNIGHT_BODY_BRIGHT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        gameplay_.XmasEvent().LoadXmasEventEffect();

        LoadBitmapW(L"Skill\\younghtest1.tga", BITMAP_GM_HAIR_1);
        LoadBitmapW(L"Skill\\younghtest3.tga", BITMAP_GM_HAIR_3);
        LoadBitmapW(L"Skill\\gmmzine.jpg", BITMAP_GM_AURORA, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\explotion01mono.jpg", BITMAP_EXPLOTION_MONO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\Success_kantru.tga", BITMAP_KANTURU_SUCCESS, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\Failure_kantru.tga", BITMAP_KANTURU_FAILED, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\MonsterCount.tga", BITMAP_KANTURU_COUNTER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Clud64.jpg", BITMAP_CLUD64, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\clouds.jpg", BITMAP_CLOUD, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Skill\\SwordEff.jpg", BITMAP_BLUE_BLUR, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Impack03.jpg", BITMAP_IMPACT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\ScolTail.jpg", BITMAP_SCOLPION_TAIL, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Effect\\FireSnuff.jpg", BITMAP_FIRE_SNUFF, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\coll.jpg", BITMAP_SPOT_WATER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\BowE.jpg", BITMAP_DS_EFFECT, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Shockwave.jpg", BITMAP_DS_SHOCK, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\DinoE.jpg", BITMAP_EXPLOTION + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Shiny01.jpg", BITMAP_SHINY, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Shiny02.jpg", BITMAP_SHINY + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Shiny03.jpg", BITMAP_SHINY + 2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\eye01.jpg", BITMAP_SHINY + 3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\ring.jpg", BITMAP_SHINY + 4, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\shiny04.jpg", BITMAP_SHINY + 5, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Chrome01.jpg", BITMAP_CHROME, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\blur01.jpg", BITMAP_BLUR, LegacyTextureFilter::Nearest, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\bab2.jpg", BITMAP_CHROME + 1, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\motion_blur.jpg", BITMAP_BLUR + 1, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\motion_blur_r.jpg", BITMAP_BLUR + 2, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\motion_mono.jpg", BITMAP_BLUR + 3, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\motion_blur_r3.jpg", BITMAP_BLUR + 6, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\gra.jpg", BITMAP_BLUR + 7, LegacyTextureFilter::Nearest, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\spinmark01.jpg", BITMAP_BLUR + 8, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\flamestani.jpg", BITMAP_BLUR + 9, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\sword_blur.jpg", BITMAP_BLUR + 10, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\joint_sword_red.jpg", BITMAP_BLUR + 11, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\motion_blur_r2.jpg", BITMAP_BLUR + 12, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\motion_blur_r3.jpg", BITMAP_BLUR + 13, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\blur02.jpg", BITMAP_BLUR2, LegacyTextureFilter::Nearest, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\lightning2.jpg", BITMAP_LIGHTNING + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Thunder01.jpg", BITMAP_ENERGY, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\Spark01.jpg", BITMAP_JOINT_SPARK, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\JointThunder01.jpg", BITMAP_JOINT_THUNDER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\JointSpirit01.jpg", BITMAP_JOINT_SPIRIT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\JointLaser01.jpg", BITMAP_JOINT_ENERGY, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Effect\\JointEnergy01.jpg", BITMAP_JOINT_HEALING, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Effect\\JointLaser02.jpg", BITMAP_JOINT_LASER + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Monster\\iui03.jpg", BITMAP_JANUSEXT, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Monster\\magic_H.tga", BITMAP_PHO_R_HAIR, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Item\\t_lower_14m.tga", BITMAP_PANTS_G_SOUL, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Skill\\Skull.jpg", BITMAP_SKULL, LegacyTextureFilter::Nearest);
        LoadBitmapW(L"Effect\\motion_blur_r2.jpg", BITMAP_JOINT_FORCE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\Fire04.jpg", BITMAP_FIRECRACKER, LegacyTextureFilter::Linear);
        LoadBitmapW(L"Effect\\Flare.jpg", BITMAP_FLARE, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Effect\\Chrome02.jpg", BITMAP_CHROME2, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Chrome03.jpg", BITMAP_CHROME3, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Chrome06.jpg", BITMAP_CHROME6, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Chrome07.jpg", BITMAP_CHROME7, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\energy01.jpg", BITMAP_CHROME_ENERGY, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\energy02.jpg", BITMAP_CHROME_ENERGY2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Effect\\firecracker0001.jpg", BITMAP_FIRECRACKER0001, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0002.jpg", BITMAP_FIRECRACKER0002, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0003.jpg", BITMAP_FIRECRACKER0003, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0004.jpg", BITMAP_FIRECRACKER0004, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0005.jpg", BITMAP_FIRECRACKER0005, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0006.jpg", BITMAP_FIRECRACKER0006, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\firecracker0007.jpg", BITMAP_FIRECRACKER0007, LegacyTextureFilter::Nearest,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\shiny05.jpg", BITMAP_SHINY + 5, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Effect\\partCharge1\\bujuckline.jpg", BITMAP_LUCKY_SEAL_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\nfm03.jpg", BITMAP_BLUECHROME, LegacyTextureFilter::Nearest, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Effect\\flareBlue.jpg", BITMAP_FLARE_BLUE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\NSkill.jpg", BITMAP_FLARE_FORCE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Flare02.jpg", BITMAP_FLARE + 1, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        LoadBitmapW(L"Monster\\King11.jpg", BITMAP_WHITE_WIZARD, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\Kni000.jpg", BITMAP_DEST_ORC_WAR0, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\Kni011.jpg", BITMAP_DEST_ORC_WAR1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\Kni022.jpg", BITMAP_DEST_ORC_WAR2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Skill\\pinkWave.jpg", BITMAP_PINK_WAVE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\flareRed.jpg", BITMAP_FLARE_RED, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Fire05.jpg", BITMAP_FIRE + 3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Hole.jpg", BITMAP_HOLE, LegacyTextureFilter::Linear, LegacyTextureWrap::Repeat);
        //	LoadBitmapW( "Monster\\mop011.jpg"        , BITMAP_OTHER_SKIN,    LegacyTextureFilter::Linear,
        //LegacyTextureWrap::Repeat );
        LoadBitmapW(L"Effect\\WATERFALL1.jpg", BITMAP_WATERFALL_1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\WATERFALL2.jpg", BITMAP_WATERFALL_2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\WATERFALL3.jpg", BITMAP_WATERFALL_3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\WATERFALL4.jpg", BITMAP_WATERFALL_4, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\WATERFALL5.jpg", BITMAP_WATERFALL_5, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        /*
        LoadBitmapW(L"Interface\\in_bar.tga"		, BITMAP_MVP_INTERFACE, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_bar2.jpg"		, BITMAP_MVP_INTERFACE + 1,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_deco.tga"		,
        BITMAP_MVP_INTERFACE + 2, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\in_main.tga"		, BITMAP_MVP_INTERFACE + 3, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_icon_bal1.tga"		, BITMAP_MVP_INTERFACE +
        4, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_icon_dl1.tga"
        , BITMAP_MVP_INTERFACE + 5, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\in_main_icon_dl2.tga"		, BITMAP_MVP_INTERFACE + 6, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_number1.tga"		, BITMAP_MVP_INTERFACE + 7,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_number2.tga" ,
        BITMAP_MVP_INTERFACE + 8, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\in_main2.tga"		, BITMAP_MVP_INTERFACE + 9, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_failure.tga"		, BITMAP_MVP_INTERFACE + 10,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_success.tga" ,
        BITMAP_MVP_INTERFACE + 11, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\t_main.tga"		, BITMAP_MVP_INTERFACE + 12, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_no1.tga"		, BITMAP_MVP_INTERFACE + 13,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_no2.tga"		,
        BITMAP_MVP_INTERFACE + 14, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\m_b_no3.tga"		, BITMAP_MVP_INTERFACE + 15, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_ok1.tga"		, BITMAP_MVP_INTERFACE + 16,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_ok2.tga"		,
        BITMAP_MVP_INTERFACE + 17, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\m_b_ok3.tga"		, BITMAP_MVP_INTERFACE + 18, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_yes1.tga"		, BITMAP_MVP_INTERFACE + 19,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_b_yes2.tga"		,
        BITMAP_MVP_INTERFACE + 20, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\m_b_yes3.tga"		, BITMAP_MVP_INTERFACE + 21, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_main.tga"		, BITMAP_MVP_INTERFACE + 22,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_number1_1.tga"
        , BITMAP_MVP_INTERFACE + 23, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\in_main_number2_1.tga"		, BITMAP_MVP_INTERFACE + 24, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\in_main_number0_2.tga"		, BITMAP_MVP_INTERFACE +
        25, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_table.tga"
        , BITMAP_MVP_INTERFACE + 26, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\icon_Rank_rank.tga"		, BITMAP_MVP_INTERFACE + 27, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_D.tga"		, BITMAP_MVP_INTERFACE + 28,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_C.tga"		,
        BITMAP_MVP_INTERFACE + 29, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\icon_Rank_B.tga"		, BITMAP_MVP_INTERFACE + 30, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_A.tga"		, BITMAP_MVP_INTERFACE + 31,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_S.tga"		,
        BITMAP_MVP_INTERFACE + 32, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        //icon_Rank_0
        LoadBitmapW(L"Interface\\icon_Rank_0.tga"		, BITMAP_MVP_INTERFACE + 33, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_1.tga"		, BITMAP_MVP_INTERFACE + 34,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_2.tga"		,
        BITMAP_MVP_INTERFACE + 35, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\icon_Rank_3.tga"		, BITMAP_MVP_INTERFACE + 36, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_4.tga"		, BITMAP_MVP_INTERFACE + 37,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_5.tga"		,
        BITMAP_MVP_INTERFACE + 38, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\icon_Rank_6.tga"		, BITMAP_MVP_INTERFACE + 39, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_7.tga"		, BITMAP_MVP_INTERFACE + 40,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_8.tga"		,
        BITMAP_MVP_INTERFACE + 41, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\icon_Rank_9.tga"		, BITMAP_MVP_INTERFACE + 42, LegacyTextureFilter::Linear,
        LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\icon_Rank_exp.tga"		, BITMAP_MVP_INTERFACE + 43,
        LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge); LoadBitmapW(L"Interface\\m_main_rank.tga"		,
        BITMAP_MVP_INTERFACE + 44, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        */
        // m_main_rank
        // icon_Rank_exp
        LoadBitmapW(L"Interface\\BattleSkill.tga", BITMAP_INTERFACE_EX + 34);
        LoadBitmapW(L"Effect\\Flashing.jpg", BITMAP_FLASH, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\senatusmap.jpg", BITMAP_INTERFACE_EX + 35);
        LoadBitmapW(L"Interface\\gate_button2.jpg", BITMAP_INTERFACE_EX + 36);
        LoadBitmapW(L"Interface\\gate_button1.jpg", BITMAP_INTERFACE_EX + 37);
        LoadBitmapW(L"Interface\\suho_button2.jpg", BITMAP_INTERFACE_EX + 38);
        LoadBitmapW(L"Interface\\suho_button1.jpg", BITMAP_INTERFACE_EX + 39);
        LoadBitmapW(L"Interface\\DoorCL.jpg", BITMAP_INTERFACE_EX + 40);
        LoadBitmapW(L"Interface\\DoorOP.jpg", BITMAP_INTERFACE_EX + 41);
        // OpenJpeg( "Effect\\FireSnuff.jpg"       , BITMAP_FIRE_SNUFF,    GL_LINEAR, GL_CLAMP_TO_EDGE );
        LoadBitmapW(L"Object31\\Flag.tga", BITMAP_INTERFACE_MAP + 0, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\CursorAttack2.tga", BITMAP_CURSOR2);
        LoadBitmapW(L"Effect\\Cratered.tga", BITMAP_CRATER);
        LoadBitmapW(L"Effect\\FormationMark.tga", BITMAP_FORMATION_MARK);
        LoadBitmapW(L"Effect\\Plus.tga", BITMAP_PLUS);
        LoadBitmapW(L"Effect\\eff_lighting.jpg", BITMAP_FENRIR_THUNDER);
        LoadBitmapW(L"Effect\\eff_lightinga01.jpg", BITMAP_FENRIR_FOOT_THUNDER1);
        LoadBitmapW(L"Effect\\eff_lightinga02.jpg", BITMAP_FENRIR_FOOT_THUNDER2);
        LoadBitmapW(L"Effect\\eff_lightinga03.jpg", BITMAP_FENRIR_FOOT_THUNDER3);
        LoadBitmapW(L"Effect\\eff_lightinga04.jpg", BITMAP_FENRIR_FOOT_THUNDER4);
        LoadBitmapW(L"Effect\\eff_lightinga05.jpg", BITMAP_FENRIR_FOOT_THUNDER5);
        LoadBitmapW(L"Interface\\Progress_Back.jpg", BITMAP_INTERFACE_EX + 42);
        LoadBitmapW(L"Interface\\Progress.jpg", BITMAP_INTERFACE_EX + 43);
        LoadBitmapW(L"Effect\\Inferno.jpg", BITMAP_INFERNO, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Lava.jpg", BITMAP_LAVA, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\frame.tga", BITMAP_INTERFACE_MAP + 3);
        LoadBitmapW(L"Interface\\i_attack.tga", BITMAP_INTERFACE_MAP + 4);
        LoadBitmapW(L"Interface\\i_defense.tga", BITMAP_INTERFACE_MAP + 5);
        LoadBitmapW(L"Interface\\i_wait.tga", BITMAP_INTERFACE_MAP + 6);
        LoadBitmapW(L"Interface\\b_command01.jpg", BITMAP_INTERFACE_MAP + 8);
        LoadBitmapW(L"Interface\\b_command02.jpg", BITMAP_INTERFACE_MAP + 9);
        LoadBitmapW(L"Interface\\b_group02.jpg", BITMAP_INTERFACE_MAP + 10);
        LoadBitmapW(L"Interface\\b_zoomout01.jpg", BITMAP_INTERFACE_MAP + 11);
        LoadBitmapW(L"Interface\\hourglass.tga", BITMAP_INTERFACE_MAP + 7);
        LoadBitmapW(L"Interface\\dot.tga", BITMAP_INTERFACE_EX + 44);
        LoadBitmapW(L"Object9\\Impack03.jpg", BITMAP_LIGHT + 1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\buserbody_r.jpg", BITMAP_BERSERK_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\busersword_r.jpg", BITMAP_BERSERK_WP_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        //	LoadBitmapW(L"Monster\\gigantiscorn_R.jpg", BITMAP_GIGANTIS_EFFECT, LegacyTextureFilter::Linear,
        //LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\bladeeff2_r.jpg", BITMAP_BLADEHUNTER_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\illumi_R.jpg", BITMAP_TWINTAIL_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\prsona_R.jpg", BITMAP_PRSONA_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\prsonass_R.jpg", BITMAP_PRSONA_EFFECT2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"effect\\water.jpg", BITMAP_TWINTAIL_WATER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\cra_04.jpg", BITMAP_LIGHT + 2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\Impack01.jpg", BITMAP_LIGHT + 3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\message_ok_b_all.tga", BITMAP_BUTTON);
        LoadBitmapW(L"Interface\\loding_cancel_b_all.tga", BITMAP_BUTTON + 1);
        LoadBitmapW(L"Interface\\message_close_b_all.tga", BITMAP_BUTTON + 2);
        OpenLegacySystemWindowTextures();
        LoadBitmapW(L"Effect\\clouds2.jpg", BITMAP_EVENT_CLOUD, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\pin_lights.jpg", BITMAP_PIN_LIGHT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\deasuler_cloth.tga", BITMAP_DEASULER_CLOTH, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\soketmagic_stape02.jpg", BITMAP_SOCKETSTAFF, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\lightmarks.jpg", BITMAP_LIGHTMARKS, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\lightmarks.jpg", BITMAP_LIGHTMARKS_FOREIGN, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        break;
    }
    case 2: {
        LoadBitmapW(L"Item\\partCharge1\\entrance_R.jpg", BITMAP_FREETICKET_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\juju_R.jpg", BITMAP_CHAOSCARD_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge1\\monmark01a.jpg", BITMAP_LUCKY_SEAL_EFFECT43, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\monmark02a.jpg", BITMAP_LUCKY_SEAL_EFFECT44, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\monmark03a.jpg", BITMAP_LUCKY_SEAL_EFFECT45, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge1\\bujuck01alpa.jpg", BITMAP_LUCKY_CHARM_EFFECT53, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\expensiveitem01_R.jpg", BITMAP_RAREITEM1_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\expensiveitem02a_R.jpg", BITMAP_RAREITEM2_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\expensiveitem02b_R.jpg", BITMAP_RAREITEM3_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\expensiveitem03a_R.jpg", BITMAP_RAREITEM4_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge1\\expensiveitem03b_R.jpg", BITMAP_RAREITEM5_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge3\\alicecard_R.tga", BITMAP_CHARACTERCARD_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\Ingameshop\\kacama_R.jpg", BITMAP_CHARACTERCARD_R_MA, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\Ingameshop\\kacada_R.jpg", BITMAP_CHARACTERCARD_R_DA, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge3\\jujug_R.jpg", BITMAP_NEWCHAOSCARD_GOLD_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge3\\jujul_R.jpg", BITMAP_NEWCHAOSCARD_RARE_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge3\\jujum_R.jpg", BITMAP_NEWCHAOSCARD_MINI_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Effect\\cherryblossom\\sakuras01.jpg", BITMAP_CHERRYBLOSSOM_EVENT_PETAL,
                    LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Effect\\cherryblossom\\sakuras02.jpg", BITMAP_CHERRYBLOSSOM_EVENT_FLOWER,
                    LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Object39\\k_effect_01.JPG", BITMAP_KANTURU_2ND_EFFECT1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\deathbeamstone_R.jpg", BITMAP_ITEM_EFFECT_DBSTONE_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\hellhorn_R.jpg", BITMAP_ITEM_EFFECT_HELLHORN_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\phoenixfeather_R.tga", BITMAP_ITEM_EFFECT_PFEATHER_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\Deye_R.jpg", BITMAP_ITEM_EFFECT_DEYE_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\wing3chaking2.jpg", BITMAP_ITEM_NIGHT_3RDWING_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"NPC\\lumi.jpg", BITMAP_CURSEDTEMPLE_NPC_MESH_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"item\\songko2_R.jpg", BITMAP_CURSEDTEMPLE_HOLYITEM_MESH_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"skill\\unitedsoldier_wing.tga", BITMAP_CURSEDTEMPLE_ALLIED_PHYSICSCLOTH);
        LoadBitmapW(L"skill\\illusionistcloth.tga", BITMAP_CURSEDTEMPLE_ILLUSION_PHYSICSCLOTH);
        LoadBitmapW(L"effect\\masker.jpg", BITMAP_CURSEDTEMPLE_EFFECT_MASKER, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"effect\\wind01.jpg", BITMAP_CURSEDTEMPLE_EFFECT_WIND, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Interface\\menu01_new2_SD.jpg", BITMAP_INTERFACE_EX + 46);

#ifdef ASG_ADD_GENS_SYSTEM
        std::wstring strFileName = L"Local\\" + g_strSelectedML + L"\\ImgsMapName\\MapNameAddStrife.tga";
        LoadBitmapW(strFileName.c_str(), BITMAP_INTERFACE_EX + 47);
#endif // ASG_ADD_GENS_SYSTEM

#ifdef ASG_ADD_GENS_MARK
        LoadBitmapW(L"Interface\\Gens_mark_D_new.tga", BITMAP_GENS_MARK_DUPRIAN);
        LoadBitmapW(L"Interface\\Gens_mark_V_new.tga", BITMAP_GENS_MARK_BARNERT);
#endif // ASG_ADD_GENS_MARK

        LoadBitmapW(L"Monster\\serufanarm_R.jpg", BITMAP_SERUFAN_ARM_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\serufanwand_R.jpg", BITMAP_SERUFAN_WAND_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"npc\\santa.jpg", BITMAP_GOOD_SANTA, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"npc\\santa_baggage.jpg", BITMAP_GOOD_SANTA_BAGGAGE, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

#ifdef PJH_ADD_PANDA_CHANGERING
        LoadBitmapW(L"Item\\pandabody_R.jpg", BITMAP_PANDABODY_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR
#endif                                           // PJH_ADD_PANDA_CHANGERING

        LoadBitmapW(L"Monster\\DGicewalker_body.jpg", BITMAP_DOPPELGANGER_ICEWALKER0, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR
        LoadBitmapW(L"Monster\\DGicewalker_R.jpg", BITMAP_DOPPELGANGER_ICEWALKER1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR
        LoadBitmapW(L"Monster\\Snake1.jpg", BITMAP_DOPPELGANGER_SNAKE01, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR

        LoadBitmapW(L"NPC\\goldboit.jpg", BITMAP_DOPPELGANGER_GOLDENBOX1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR
        LoadBitmapW(L"NPC\\goldline.jpg", BITMAP_DOPPELGANGER_GOLDENBOX2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::Invalid); // OPENGL_ERROR

        // BITMAP_LIGHT_RED
        LoadBitmapW(L"effect\\flare01_red.jpg", BITMAP_LIGHT_RED, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"effect\\gra.jpg", BITMAP_GRA, LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"effect\\ring_of_gradation.jpg", BITMAP_RING_OF_GRADATION, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Interface\\InGameShop\\ingame_pack_check.tga", BITMAP_IGS_CHECK_BUTTON,
                    LegacyTextureFilter::Linear);
        LoadBitmapW(L"Monster\\AssassinLeader_body_R.jpg", BITMAP_ASSASSIN_EFFECT1, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_7_body.jpg", BITMAP_RAREITEM7, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_8_body.jpg", BITMAP_RAREITEM8, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_9_body.jpg", BITMAP_RAREITEM9, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_10_body.jpg", BITMAP_RAREITEM10, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_11_body.jpg", BITMAP_RAREITEM11, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\rareitem_ticket_12_body.jpg", BITMAP_RAREITEM12, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"Item\\partCharge8\\DoppelCard.jpg", BITMAP_DOPPLEGANGGER_FREETICKET, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\BarcaCard.jpg", BITMAP_BARCA_FREETICKET, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\partCharge8\\Barca7Card.jpg", BITMAP_BARCA7TH_FREETICKET, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

#ifdef LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2
        LoadBitmapW(L"Item\\ork_cham_R.jpg", BITMAP_ORK_CHAM_LAYER_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        // LoadBitmapW(L"Item\\maple_cham_R.jpg",			BITMAP_MAPLE_CHAM_LAYER_R,
        // LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Item\\goldenork_cham_R.jpg", BITMAP_GOLDEN_ORK_CHAM_LAYER_R, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        // LoadBitmapW(L"Item\\goldenmaple_cham_R.jpg",	BITMAP_GOLDEN_MAPLE_CHAM_LAYER_R,	LegacyTextureFilter::Linear,
        // LegacyTextureWrap::ClampToEdge);
#endif // LJH_ADD_ITEMS_EQUIPPED_FROM_INVENTORY_SYSTEM_PART_2

        LoadBitmapW(L"Monster\\BoneSE.jpg", BITMAP_BONE_SCORPION_SKIN_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\KryptaBall2.jpg", BITMAP_KRYPTA_BALL_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\bora_golem_effect.jpg", BITMAP_CONDRA_SKIN_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\bora_golem2_effect.jpg", BITMAP_CONDRA_SKIN_EFFECT2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\king_golem01_effect.jpg", BITMAP_NARCONDRA_SKIN_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\king_golem02_effect.jpg", BITMAP_NARCONDRA_SKIN_EFFECT2, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);
        LoadBitmapW(L"Monster\\king_golem03_effect.jpg", BITMAP_NARCONDRA_SKIN_EFFECT3, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        LoadBitmapW(L"NPC\\voloE.jpg", BITMAP_VOLO_SKIN_EFFECT, LegacyTextureFilter::Linear,
                    LegacyTextureWrap::ClampToEdge);

        g_ErrorReport.Write(L"> First Load Files OK.\r\n");

        OpenPlayers();
        break;
    }
    case 3: {
        OpenPlayerTextures();
        break;
    }
    case 4: {
        OpenItems();
        break;
    }
    case 5: {
        OpenItemTextures();
        break;
    }
    case 6: {
        OpenSkills();
        break;
    }
    case 7: {
        OpenImages();
        break;
    }
    case 8: {
        OpenSounds();
        break;
    }
    case 9: {
        wchar_t Text[100];

        if (!g_ServerListManager.LoadServerListScript())
        {
            notifyDataLoadFailure(L"Data\\Local\\ServerList.bmd file not found.\r\n");
        }

        // NPC dialog text used to live in Data\Local\<lang>\Dialog_<lang>.bmd
        // and was loaded into g_DialogScript here. The text and answer labels
        // now live in I18N::Dialog (generated from src/Localization/Dialog.*.resx),
        // and the structural branching data lives in
        // GameLogic::Quests::Dialog::GetEntry, so there is nothing to load at
        // runtime any more.

        mu_swprintf(Text, L"Data\\Local\\%ls\\Item_%ls.bmd", g_strSelectedML.c_str(), g_strSelectedML.c_str());
        if (!LoadItemDataFile(Text, g_ErrorReport, g_hWnd))
            notifyDataLoadFailure(Text);

        mu_swprintf(Text, L"Data\\Local\\%ls\\movereq_%ls.bmd", g_strSelectedML.c_str(), g_strSelectedML.c_str());
        g_MoveCommandData.OpenMoveReqScript(Text);

        mu_swprintf(Text, L"Data\\Local\\%ls\\Quest_%ls.bmd", g_strSelectedML.c_str(), g_strSelectedML.c_str());
        g_csQuest.OpenQuestScript(Text);

        mu_swprintf(Text, L"Data\\Local\\%ls\\Skill_%ls.bmd", g_strSelectedML.c_str(), g_strSelectedML.c_str());
        if (!LoadSkillDataFile(Text, g_ErrorReport, g_hWnd))
            notifyDataLoadFailure(Text);

        mu_swprintf(Text, L"Data\\Local\\%ls\\SocketItem_%ls.bmd", g_strSelectedML.c_str(), g_strSelectedML.c_str());
        if (!g_SocketItemMgr.OpenSocketItemScript(Text))
        {
            wchar_t failureMessage[256]{};
            mu_swprintf(failureMessage, L"%ls - File not exist.", Text);
            notifyDataLoadFailure(failureMessage);
        }

        OpenMacro(L"Data\\Macro.txt");

        if (const auto itemSetFailure = g_csItemOption.OpenItemSetScript(); !itemSetFailure.empty())
        {
            notifyDataLoadFailure(itemSetFailure.c_str());
        }

        for (const auto &failureMessage : g_QuestMng.LoadQuestScript())
        {
            notifyDataLoadFailure(failureMessage.c_str());
        }

        g_MixRecipeMgr.Initialize();

        g_pMasterLevelInterface->OpenMasterSkillTreeData(L"Data\\Local\\MasterSkillTreeData.bmd");
        g_pMasterLevelInterface->OpenMasterSkillTooltip(L"Data\\Local\\Eng\\MasterSkillTooltip_eng.bmd");

        break;
    }
    case 10: {
        LoadWaveFile(SOUND_TITLE01, L"Data\\Sound\\iTitle.wav", 1);
        LoadWaveFile(SOUND_MENU01, L"Data\\Sound\\iButtonMove.wav", 2);
        LoadWaveFile(SOUND_CLICK01, L"Data\\Sound\\iButtonClick.wav", 1);
        LoadWaveFile(SOUND_ERROR01, L"Data\\Sound\\iButtonError.wav", 1);
        LoadWaveFile(SOUND_INTERFACE01, L"Data\\Sound\\iCreateWindow.wav", 1);

        LoadWaveFile(SOUND_REPAIR, L"Data\\Sound\\iRepair.wav", 1);
        LoadWaveFile(SOUND_WHISPER, L"Data\\Sound\\iWhisper.wav", 1);

        LoadWaveFile(SOUND_FRIEND_CHAT_ALERT, L"Data\\Sound\\iFMSGAlert.wav", 1);
        LoadWaveFile(SOUND_FRIEND_MAIL_ALERT, L"Data\\Sound\\iFMailAlert.wav", 1);
        LoadWaveFile(SOUND_FRIEND_LOGIN_ALERT, L"Data\\Sound\\iFLogInAlert.wav", 1);

        LoadWaveFile(SOUND_RING_EVENT_READY, L"Data\\Sound\\iEvent3min.wav", 1);
        LoadWaveFile(SOUND_RING_EVENT_START, L"Data\\Sound\\iEventStart.wav", 1);
        LoadWaveFile(SOUND_RING_EVENT_END, L"Data\\Sound\\iEventEnd.wav", 1);

        break;
    }
    default:
        break;
    }
}

void SessionRenderUnit::ReleaseWorldRenderResources()
{
    terrainGeometryCache_.Invalidate();
    auto &terrain = sessionKeeper_.TerrainStorage();
    terrain.lightSnapshot.reset();
    terrain.lightSnapshots.clear();
    terrain.dynamicLightActive = false;
    terrain.dynamicLightDirty = false;
    for (auto &blur : g_blurs)
        blur = {};
    for (auto &blur : g_objectBlurs)
        blur = {};
    ReleaseGroundItemLabelCache();
    if (g_pNewUIMiniMap != nullptr)
        g_pNewUIMiniMap->UnloadImages();
}

void World::InstallEffectTextures(std::unique_ptr<SessionTextureNamespace> textures,
                                  std::vector<std::uint32_t> slots)
{
    ReleaseEffectTextures();
    previousEffectTextures_ = std::move(textures);
    effectTextureSlots_ = std::move(slots);
    for (const auto slot : effectTextureSlots_)
    {
        sessionKeeper_.TextureNamespace().SwapBinding(slot, *previousEffectTextures_);
        ++installedEffectTextures_;
    }
}

void World::ReleaseEffectTextures() noexcept
{
    // Both namespaces retain their buckets through retirement; restoring nodes needs no allocation.
    while (installedEffectTextures_ != 0)
        sessionKeeper_.TextureNamespace().SwapBinding(
            effectTextureSlots_[--installedEffectTextures_], *previousEffectTextures_);
    previousEffectTextures_.reset();
    effectTextureSlots_.clear();
}
