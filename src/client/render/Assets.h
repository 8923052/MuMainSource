#pragma once
#define MAX_BONES 200
#define MAX_MESH 50
#define MAX_VERTICES 15000

#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace LogicalAssetPath
{
constexpr wchar_t Fold(const wchar_t character) noexcept
{
    if (character == L'\\')
        return L'/';
    if (character >= L'A' && character <= L'Z')
        return character + (L'a' - L'A');
    return character;
}

struct Hash final
{
    std::size_t operator()(const std::wstring &path) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(2166136261U);
        for (const wchar_t character : path)
        {
            hash ^= static_cast<std::size_t>(Fold(character));
            hash *= static_cast<std::size_t>(16777619U);
        }
        return hash;
    }
};

struct Equal final
{
    bool operator()(const std::wstring &first, const std::wstring &second) const noexcept
    {
        if (first.size() != second.size())
            return false;

        for (std::size_t index = 0; index < first.size(); ++index)
        {
            if (Fold(first[index]) != Fold(second[index]))
                return false;
        }
        return true;
    }
};
} // namespace LogicalAssetPath

// Portable CPU-owned render asset values (PLAN_P1R5.1.md section 5.2). This
// header must never include an OpenGL, SDL, Windows, render-backend, or
// native-window header, and must never store a GPU name, native handle, or
// mutable pixel pointer.

enum class RenderAssetFormat : std::uint8_t
{
    Rgba8,
};

constexpr bool IsValid(RenderAssetFormat value) noexcept
{
    switch (value)
    {
    case RenderAssetFormat::Rgba8:
        return true;
    }
    return false;
}

// Portable replacement for the legacy GL_NEAREST/GL_LINEAR sampler filter
// constants (PLAN_P1R5.1.md section 5.2).
enum class LegacyTextureFilter : std::uint8_t
{
    Nearest,
    Linear,
    Invalid = 0xff,
};

constexpr bool IsValid(LegacyTextureFilter value) noexcept
{
    switch (value)
    {
    case LegacyTextureFilter::Nearest:
    case LegacyTextureFilter::Linear:
        return true;
    }
    return false;
}

// Portable replacement for the legacy GL_CLAMP_TO_EDGE/GL_REPEAT/GL_CLAMP
// sampler wrap constants (PLAN_P1R5.1.md section 5.2).
enum class LegacyTextureWrap : std::uint8_t
{
    ClampToEdge,
    Repeat,
    Clamp,
    Invalid = 0xff,
};

constexpr bool IsValid(LegacyTextureWrap value) noexcept
{
    switch (value)
    {
    case LegacyTextureWrap::ClampToEdge:
    case LegacyTextureWrap::Repeat:
    case LegacyTextureWrap::Clamp:
        return true;
    }
    return false;
}

struct RenderSamplerIntent final
{
    LegacyTextureFilter filter = LegacyTextureFilter::Invalid;
    LegacyTextureWrap wrap = LegacyTextureWrap::Invalid;
};

constexpr bool IsValid(RenderSamplerIntent value) noexcept
{
    return IsValid(value.filter) && IsValid(value.wrap);
}

constexpr bool operator==(RenderSamplerIntent lhs, RenderSamplerIntent rhs) noexcept
{
    return lhs.filter == rhs.filter && lhs.wrap == rhs.wrap;
}

constexpr bool operator!=(RenderSamplerIntent lhs, RenderSamplerIntent rhs) noexcept
{
    return !(lhs == rhs);
}

struct LogicalRenderAssetId final
{
    std::uint64_t value = 0;
    // comparison only; zero is invalid
};

constexpr bool IsValid(LogicalRenderAssetId id) noexcept
{
    return id.value != 0;
}

constexpr bool operator==(LogicalRenderAssetId lhs, LogicalRenderAssetId rhs) noexcept
{
    return lhs.value == rhs.value;
}

constexpr bool operator!=(LogicalRenderAssetId lhs, LogicalRenderAssetId rhs) noexcept
{
    return !(lhs == rhs);
}

struct LogicalRenderAssetRef final
{
    LogicalRenderAssetId id;
    std::uint64_t revision = 0;
    // both fields must be nonzero
};

constexpr bool IsValid(LogicalRenderAssetRef ref) noexcept
{
    return IsValid(ref.id) && ref.revision != 0;
}

// Needed to deduplicate the tape's distinct logical-asset table (section
// 5.6/5.8): a draw's asset reference is looked up by exact value equality.
constexpr bool operator==(const LogicalRenderAssetRef &lhs,
                          const LogicalRenderAssetRef &rhs) noexcept
{
    return lhs.id == rhs.id && lhs.revision == rhs.revision;
}

constexpr bool operator!=(const LogicalRenderAssetRef &lhs,
                          const LogicalRenderAssetRef &rhs) noexcept
{
    return !(lhs == rhs);
}

struct LogicalRenderAssetRefHash final
{
    std::size_t operator()(LogicalRenderAssetRef value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.id.value ^ std::rotl(value.revision, 1));
    }
};

// Exact byte count for a tightly packed RGBA8 image, checked without
// overflow. Shared by every asset value below whose dimensions must agree
// with an immutable RGBA8 payload.
constexpr bool IsExactRgba8ByteCount(std::uint32_t width, std::uint32_t height,
                                     std::size_t byteCount) noexcept
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (width != 0 && pixels / width != height)
    {
        return false;
    }
    constexpr std::uint64_t MaxPixels = (std::numeric_limits<std::uint64_t>::max)() / 4;
    if (pixels > MaxPixels)
    {
        return false;
    }
    return byteCount == pixels * 4;
}

struct LogicalRenderAssetLease final
{
    LogicalRenderAssetRef asset;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RenderAssetFormat format = RenderAssetFormat::Rgba8;
    RenderSamplerIntent sampler;
    std::shared_ptr<const std::vector<std::byte>> bytes;
};

inline bool IsValid(const LogicalRenderAssetLease &lease) noexcept
{
    return IsValid(lease.asset) && IsValid(lease.format) && lease.width != 0 && lease.height != 0 &&
           IsValid(lease.sampler) && lease.bytes != nullptr &&
           IsExactRgba8ByteCount(lease.width, lease.height, lease.bytes->size());
}

// Immutable value copy used by session/application boundaries. It contains
// no pointer to the mutable legacy bitmap object or to mutable pixel storage.
struct LogicalRenderAssetMetadata final
{
    std::uint32_t BitmapIndex = 0;
    std::wstring FileName;
    float Width = 0.0F;
    float Height = 0.0F;
    char Components = 0;
    LogicalRenderAssetRef Asset;
    RenderSamplerIntent Sampler;
    std::uint8_t Ref = 0;
    bool IsSkin = false;
    bool IsHair = false;
};

struct LogicalRenderAssetRevisionInput final
{
    LogicalRenderAssetRef asset;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RenderSamplerIntent sampler;
    std::span<const std::byte> rgba8;
};

constexpr bool IsValid(const LogicalRenderAssetRevisionInput &input) noexcept
{
    return IsValid(input.asset) && input.width != 0 && input.height != 0 &&
           IsValid(input.sampler) &&
           IsExactRgba8ByteCount(input.width, input.height, input.rgba8.size());
}

class CGlobalBitmap;

// Portable identity/revision registry backing CGlobalBitmap's catalog API
// (PLAN_P1R5.1.md section 5.2). Held by CGlobalBitmap as a private member so
// the catalog logic itself stays testable without CGlobalBitmap's decoder
// dependencies (turbojpeg, stdafx.h). Never includes an OpenGL, SDL,
// Windows, or native-window header.
class LogicalRenderAssetCatalog final
{
  public:
    bool SetTrustedProducerThread(std::thread::id thread) noexcept;
    std::optional<LogicalRenderAssetLease> TryLease(LogicalRenderAssetRef asset) const noexcept;
    std::optional<LogicalRenderAssetId> AllocateDynamicIdentity() noexcept;
    bool CommitRecordedRevisions(std::span<const LogicalRenderAssetRevisionInput> inputs) noexcept;
    bool RetireLogicalAsset(LogicalRenderAssetRef asset) noexcept;
    bool CommitOwnerProducedRevision(LogicalRenderAssetRef asset, std::uint32_t width,
                                     std::uint32_t height, RenderSamplerIntent sampler,
                                     std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept;

  private:
    friend class CGlobalBitmap;

    struct CatalogRecord final
    {
        struct RevisionRecord final
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            RenderSamplerIntent sampler;
            std::shared_ptr<const std::vector<std::byte>> pixels;
        };

        std::uint64_t currentRevision = 0;
        bool retired = false;
        std::map<std::uint64_t, RevisionRecord> revisions;
    };

    // Shared publish path for CommitRecordedRevisions and
    // CommitOwnerProducedRevision. Takes an already-owned immutable buffer.
    bool PublishRevision(LogicalRenderAssetRef asset, std::uint32_t width, std::uint32_t height,
                         RenderSamplerIntent sampler,
                         std::shared_ptr<const std::vector<std::byte>> pixels) noexcept;
    static void PruneUnleasedRevisions(CatalogRecord &record) noexcept;
    bool IsOwnerThread() const noexcept
    {
        return std::this_thread::get_id() == ownerThread_;
    }
    bool IsMutationThread() const noexcept
    {
        const std::thread::id current = std::this_thread::get_id();
        return current == ownerThread_ || current == trustedProducerThread_;
    }
    bool IsCurrent(LogicalRenderAssetRef asset) const noexcept;
    std::uint64_t Generation() const noexcept;
    void BumpGeneration() noexcept;

    std::unordered_map<std::uint64_t, CatalogRecord> catalog_;
    std::thread::id ownerThread_ = std::this_thread::get_id();
    std::thread::id trustedProducerThread_;
    std::uint64_t nextAssetId_ = 1;
    std::uint64_t generation_ = 1;
};

class CGlobalBitmap;

namespace UI::Modern
{
std::optional<LogicalRenderAssetMetadata> ResolveUiAsset(const CGlobalBitmap &bitmaps,
                                                         std::string_view source) noexcept;
}

// Session-facing metadata view; publication and lookup implementations stay in this module.
class CErrorReport;
class CmuConsoleDebug;
class SessionTextureNamespace;
struct SessionTextureProperties final
{
    float width = 0.0F;
    float height = 0.0F;
    char components = 0;
    bool isSkin = false;
    bool isHair = false;
};
using SessionBitmapMetadata = LogicalRenderAssetMetadata;
class SessionBitmapView final
{
  public:
    SessionBitmapView(CGlobalBitmap &bitmaps, SessionTextureNamespace &textures) noexcept;

    SessionBitmapMetadata operator[](std::uint32_t logicalIndex) const noexcept;

    std::optional<SessionBitmapMetadata> GetTexture(std::uint32_t logicalIndex) const noexcept;

    std::optional<SessionBitmapMetadata> FindTexture(std::uint32_t logicalIndex) const noexcept;

    std::optional<SessionBitmapMetadata> FindTexture(const std::wstring &filename) const noexcept;

    std::optional<SessionBitmapMetadata> FindTextureByName(const std::wstring &name) const noexcept;

    std::optional<std::uint32_t> RetainTextureByName(const std::wstring &name) noexcept;

    std::optional<SessionTextureProperties> GetTextureProperties(
        std::uint32_t logicalIndex) const noexcept;

    std::optional<LogicalRenderAssetLease> TryLease(std::uint32_t logicalIndex) const noexcept;

    std::uint32_t LoadImage(const std::wstring &filename, CErrorReport &errorReport,
                            LegacyTextureFilter filter = LegacyTextureFilter::Nearest,
                            LegacyTextureWrap wrapMode = LegacyTextureWrap::ClampToEdge);

    bool LoadImage(std::uint32_t logicalIndex, const std::wstring &filename,
                   CErrorReport &errorReport,
                   LegacyTextureFilter filter = LegacyTextureFilter::Nearest,
                   LegacyTextureWrap wrapMode = LegacyTextureWrap::ClampToEdge);

    void UnloadImage(std::uint32_t logicalIndex, bool = false) noexcept;

    bool Convert_Format(const std::wstring &filename);

    void Manage(std::chrono::steady_clock::time_point &startTickTime,
                CmuConsoleDebug &consoleDebug);

    std::uint32_t GetUsedTextureMemory() const;

    size_t GetNumberOfTexture() const;

    // Controlled procedural publication keeps session consumers from
    // receiving the mutable application bitmap catalog or BITMAP_t objects.
    bool CommitOwnerProducedRevision(std::uint32_t logicalIndex, LogicalRenderAssetRef asset,
                                     std::uint32_t width, std::uint32_t height,
                                     RenderSamplerIntent sampler,
                                     std::shared_ptr<const std::vector<std::byte>> rgba8) noexcept;

  private:
    std::optional<SessionBitmapMetadata> Describe(std::uint32_t logicalIndex) const noexcept;

    CGlobalBitmap &bitmaps_;
    SessionTextureNamespace &textures_;
};
