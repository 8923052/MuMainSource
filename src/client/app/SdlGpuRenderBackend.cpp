#include "app/SdlGpuRenderBackend.h"
#include "app/Application.h"
#include "app/ApplicationKeeper.h"
#include "app/AppWindow.h"
#include "App/Render/Shaders/RenderTapeShaderArtifacts.generated.h"
#include "I18N/All.h"
#include "render/FrameTape.h"
#include "render/Terrain.h"
#include "session/SessionWorkspace.h"
#include "support/CoreMath.h"

namespace
{
constexpr std::size_t QuadVertexCount = 4;
constexpr std::size_t QuadIndexCount = 6;
constexpr std::size_t Rgba8BytesPerPixel = 4;
constexpr std::uint32_t CanonicalVertexLayout = 1;
constexpr std::uint32_t RenderTapeVertexStorageBufferCount = 4;
constexpr std::uint8_t ColorWriteMaskAll = 0x0F;

struct RenderTapeVertexUniformData final
{
    std::array<float, 16> modelView;
    std::array<float, 16> projection;
    std::array<float, 16> textureMatrix;
    std::array<float, 4> bmdScale{};
    std::array<float, 4> bmdBodyOrigin{};
    std::array<float, 4> bmdBodyLight{};
    std::array<float, 4> bmdBaseColor{};
    std::array<float, 4> bmdLightPosition{};
    std::array<float, 4> bmdUvAnimation{};
    std::array<float, 4> bmdChromeLight{};
    std::array<float, 4> bmdLegacyLight{};
    std::array<std::uint32_t, 4> bmdMode{};
    RenderTapeBoneMatrix rigidTransform{};
};

struct RenderTapeFragmentUniformData final
{
    std::array<float, 4> fogColor;
    std::array<float, 4> fogRange;
    std::array<float, 4> ambientLight;
    std::array<float, 4> mode;
    std::array<float, 4> textureTint{1.0F, 0.0F, 0.0F, 0.0F};
};

struct RenderTapeUniformData final
{
    RenderTapeVertexUniformData vertex;
    RenderTapeFragmentUniformData fragment;
};

class FrameAssetLookup final
{
  public:
    bool Initialize(std::span<const LogicalRenderAssetLease> leases) noexcept
    {
        try
        {
            leases_.clear();
            used_.clear();
            frameOnly_.clear();
            leases_.reserve(leases.size());
            used_.reserve(leases.size());
            frameOnly_.reserve(leases.size());
            for (const LogicalRenderAssetLease &lease : leases)
            {
                leases_.emplace(lease.asset, &lease);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Add(const SessionRenderTape &tape) noexcept
    {
        try
        {
            used_.insert(tape.LogicalAssets().begin(), tape.LogicalAssets().end());
            for (const RenderOwnerRequest &request : tape.OwnerRequests())
            {
                if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
                {
                    used_.insert(upload->destination);
                    if (upload->retention == RenderAssetRetention::FrameOnly)
                    {
                        frameOnly_.insert(upload->destination);
                    }
                }
                else if (const auto *copy =
                             std::get_if<CopyTargetToLogicalTextureRequest>(&request))
                {
                    used_.insert(copy->destination);
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    const LogicalRenderAssetLease *Find(LogicalRenderAssetRef asset) const noexcept
    {
        const auto found = leases_.find(asset);
        return found == leases_.end() ? nullptr : found->second;
    }

    bool IsUsed(LogicalRenderAssetRef asset) const noexcept
    {
        return used_.contains(asset);
    }

    bool IsFrameOnly(LogicalRenderAssetRef asset) const noexcept
    {
        return frameOnly_.contains(asset);
    }

  private:
    std::unordered_map<LogicalRenderAssetRef, const LogicalRenderAssetLease *,
                       LogicalRenderAssetRefHash>
        leases_;
    std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> used_;
    std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> frameOnly_;
};

enum class ReplayOperationKind : std::uint8_t
{
    Upload = 1,
    Copy = 2,
    Download = 3,
    Clear = 4,
    Draw = 5,
    Composition = 6,
    OverlayRect = 7,
    Label = 8,
    ClearDraw = 9,
};

template <typename T> std::optional<T> CheckedAdd(T left, T right) noexcept
{
    if (left > (std::numeric_limits<T>::max)() - right)
    {
        return std::nullopt;
    }
    return left + right;
}

template <typename T> std::optional<T> CheckedMultiply(T left, T right) noexcept
{
    if (right != 0 && left > (std::numeric_limits<T>::max)() / right)
    {
        return std::nullopt;
    }
    return left * right;
}

constexpr bool IsSdrSwapchainFormat(SDL_GPUTextureFormat format) noexcept
{
    return format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
           format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
}

constexpr std::size_t DepthFormatBytes(SDL_GPUTextureFormat format) noexcept
{
    return format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ? 8 : 4;
}

bool IsDriverShaderPair(const char *driver, SDL_GPUShaderFormat formats) noexcept
{
    if (driver == nullptr)
    {
        return false;
    }
    const std::string_view name(driver);
    if (name == "direct3d12")
    {
        return (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0;
    }
    if (name == "vulkan")
    {
        return (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    }
    if (name == "metal")
    {
        return (formats & SDL_GPU_SHADERFORMAT_MSL) != 0;
    }
    return false;
}

void *TestHandle(std::uintptr_t value) noexcept
{
    return reinterpret_cast<void *>(value);
}
} // namespace

struct SdlGpuRenderBackend::Impl final
{
    struct SessionTargetKey final
    {
        std::uint64_t id = 0;
        std::uint64_t generation = 0;
        std::uint64_t surfaceGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t deviceGeneration = 0;

        bool operator==(const SessionTargetKey &) const noexcept = default;
    };

    struct TextureKey final
    {
        LogicalRenderAssetRef asset;
        std::uint64_t deviceGeneration = 0;

        bool operator==(const TextureKey &) const noexcept = default;
    };

    struct SessionTargetRecord final
    {
        bool live = false;
        SessionTargetKey key;
        void *color = nullptr;
        void *depth = nullptr;
        std::uint64_t lastFence = 0;
    };

    struct TextureRecord final
    {
        bool live = false;
        TextureKey key;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::size_t residentBytes = 0;
        void *native = nullptr;
        std::uint64_t lastFence = 0;
        bool frameOnly = false;
    };

    struct SamplerRecord final
    {
        bool live = false;
        RenderSamplerIntent key;
        std::uint64_t deviceGeneration = 0;
        void *native = nullptr;
        std::uint64_t lastFence = 0;
    };

    struct PipelineRecord final
    {
        bool live = false;
        SdlGpuRenderBackend::PipelineKey key;
        SdlGpuRenderBackend::PipelinePolicy policy;
        void *native = nullptr;
        std::uint64_t lastFence = 0;
    };

    struct GeometryRecord final
    {
        bool live = false;
        LogicalGeometryAssetRef asset;
        void *vertices = nullptr;
        void *indices = nullptr;
        void *terrainCells = nullptr;
        void *terrainInstances = nullptr;
        std::uint32_t vertexCapacity = 0;
        std::uint32_t indexCapacity = 0;
        std::size_t terrainCellCapacity = 0;
        std::size_t terrainInstanceCapacity = 0;
        std::uint64_t lastFence = 0;
        std::uint64_t reservationStamp = 0;
        SDL_GPUBuffer *lightBuffer = nullptr;
        SDL_GPUTransferBuffer *lightTransfer = nullptr;
        std::uint64_t lightRevision = 0;
        std::size_t lightBytes = 0;
    };

    bool ready = false;
    bool testMode = false;
    bool windowClaimed = false;
    bool vsyncAvailable = false;
    bool vsyncEnabled = false;
    std::optional<bool> pendingVSync;
    AppWindow *window = nullptr;
    SDL_Window *sdlWindow = nullptr;
    SDL_GPUDevice *device = nullptr;
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTextureFormat swapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
    SDL_GPUTransferBuffer *uploadArena = nullptr;
    std::size_t uploadArenaBytes = 0;
    SDL_GPUTransferBuffer *downloadArena = nullptr;
    SDL_GPUBuffer *geometryVertexBuffer = nullptr;
    std::size_t geometryVertexBufferBytes = 0;
    SDL_GPUBuffer *geometryIndexBuffer = nullptr;
    std::size_t geometryIndexBufferBytes = 0;
    SDL_GPUBuffer *bonePaletteBuffer = nullptr;
    std::size_t bonePaletteBufferBytes = 0;
    SDL_GPUBuffer *drawInstanceBuffer = nullptr;
    std::size_t drawInstanceBufferBytes = 0;
    std::size_t geometryBufferAllocationCount = 0;
    std::size_t drawInstanceBufferAllocationCount = 0;
    void *overlayAtlas = nullptr;
    void *overlaySampler = nullptr;
    std::uint64_t deviceGeneration = 0;
    std::uint64_t nextDeviceGeneration = 1;
    SDL_GPUTextureFormat queriedSwapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    bool swapchainRefreshPending = false;
    std::uint64_t retiredFence = 0;
    SDL_GPUFence *pendingFence = nullptr;
    std::uint64_t pendingFrameSequence = 0;
    std::uint64_t lastFrameSequence = 0;
    std::size_t replayAcquireCount = 0;
    std::size_t replaySubmitCount = 0;
    std::size_t replayCancelCount = 0;
    std::size_t residentTextureBytes = 0;
    SdlGpuRenderBackend::AllocationLedger ledger;
    SdlGpuRenderBackend::NativeTestConfig nativeTestConfig;
    ApplicationRenderCompletionBatch completions;
    std::vector<SessionTargetRecord> sessionTargets;
    std::vector<TextureRecord> textures;
    std::vector<std::size_t> textureSlotsByAssetId;
    std::vector<SamplerRecord> samplers;
    std::vector<PipelineRecord> pipelines;
    std::vector<GeometryRecord> geometries;
    std::unordered_map<LogicalGeometryAssetRef, std::size_t, LogicalGeometryAssetRefHash>
        geometrySlots;
    std::uint64_t nextGeometryReservationStamp = 1;
    std::vector<SdlGpuRenderBackend::CandidatePlan> framePlans;
    std::vector<Uint32> frameVertexOffsets;
    std::vector<Uint32> frameIndexOffsets;
    std::vector<Uint32> framePaletteOffsets;
    std::vector<Uint32> frameQuadInstanceOffsets;
    std::vector<Uint32> frameRigidInstanceOffsets;
    std::vector<Uint32> frameParticleInstanceOffsets;
    std::vector<Uint32> frameTrailSampleOffsets;
    std::vector<Uint32> frameTrailInstanceOffsets;
    std::unique_ptr<SdlGpuRenderBackend::FrameReservation> reservation;
    SdlGpuReplayPhaseTimings lastReplayPhaseTimings;
};

struct SdlGpuRenderBackend::FrameReservation final
{
    struct AssetFrameSlot final
    {
        const LogicalRenderAssetLease *lease = nullptr;
        std::uint64_t leaseStamp = 0;
        std::uint64_t usedStamp = 0;
        std::uint64_t frameOnlyStamp = 0;
        std::uint64_t pendingStamp = 0;
        std::size_t pendingIndex = 0;
    };

    struct Target final
    {
        std::size_t slot = 0;
        std::uint64_t sessionId = 0;
        std::uint64_t sessionGeneration = 0;
        std::uint64_t surfaceGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        void *color = nullptr;
        void *depth = nullptr;
        bool reuse = false;
    };

    struct Texture final
    {
        std::size_t slot = 0;
        LogicalRenderAssetRef asset;
        std::uint64_t deviceGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::size_t residentBytes = 0;
        void *native = nullptr;
        bool reuse = false;
        std::uint32_t uploadOffset = 0;
        std::uint32_t uploadBytes = 0;
        bool frameOnly = false;
        bool recycled = false;
    };

    struct Sampler final
    {
        std::size_t slot = 0;
        RenderSamplerIntent key;
        std::uint64_t deviceGeneration = 0;
        void *native = nullptr;
        bool reuse = false;
    };

    struct Pipeline final
    {
        std::size_t slot = 0;
        SdlGpuRenderBackend::PipelineKey key;
        SdlGpuRenderBackend::PipelinePolicy policy;
        void *native = nullptr;
        bool reuse = false;
    };

    struct Geometry final
    {
        std::size_t slot = 0;
        LogicalGeometryAssetRef asset;
        const LogicalGeometryAssetLease *lease = nullptr;
        void *vertices = nullptr;
        void *indices = nullptr;
        void *terrainCells = nullptr;
        void *terrainInstances = nullptr;
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t vertexUploadOffset = 0;
        std::uint32_t indexUploadOffset = 0;
        std::uint32_t terrainUploadOffset = 0;
        std::uint32_t terrainInstanceUploadOffset = 0;
        bool reuse = false;
        bool recycled = false;
        SDL_GPUBuffer *lightBuffer = nullptr;
        SDL_GPUTransferBuffer *lightTransfer = nullptr;
        bool reuseLight = true;
        bool uploadLight = false;
    };

    struct SessionCompletion final
    {
        std::optional<SessionReplayCompletion> value;
        std::uint64_t deviceGeneration = 0;
        std::uint32_t targetWidth = 0;
        std::uint32_t targetHeight = 0;
    };

    struct RequestCompletion final
    {
        std::optional<RenderOwnerRequestCompletion> value;
        std::uint32_t downloadOffset = 0;
        std::uint32_t downloadBytes = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t deviceGeneration = 0;
        std::uint32_t targetWidth = 0;
        std::uint32_t targetHeight = 0;
        bool download = false;
        bool verticallyFlipped = false;
    };

    SDL_GPUCommandBuffer *commandBuffer = nullptr;
    SDL_GPUFence *fence = nullptr;
    bool swapchainAcquired = false;
    bool cancelSucceeded = true;
    ApplicationRenderCompletionBatch::Checkpoint completionCheckpoint;
    bool completionPrepared = false;
    std::vector<Target> targets;
    std::size_t targetCount = 0;
    std::vector<Texture> textures;
    std::size_t textureCount = 0;
    std::vector<Sampler> samplers;
    std::size_t samplerCount = 0;
    std::vector<Pipeline> pipelines;
    std::size_t pipelineCount = 0;
    std::vector<Geometry> geometries;
    std::size_t geometryCount = 0;
    std::vector<std::size_t> textureEvictions;
    std::size_t textureEvictionCount = 0;
    std::size_t plannedResidentBytes = 0;
    SDL_GPUBuffer *vertexBuffer = nullptr;
    SDL_GPUBuffer *indexBuffer = nullptr;
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    std::size_t clearVertexOffset = 0;
    std::size_t clearIndexOffset = 0;
    std::size_t clearQuadCount = 0;
    std::vector<SdlGpuRenderBackend::ClearDrawDescriptor> clearDraws;
    std::size_t clearDrawCount = 0;
    std::vector<SessionCompletion> sessionCompletions;
    std::size_t sessionCompletionCount = 0;
    std::vector<RequestCompletion> requestCompletions;
    std::size_t requestCompletionCount = 0;
    std::size_t downloadBytes = 0;
    std::size_t uploadBytes = 0;
    std::size_t uploadWriteOffset = 0;
    std::size_t compositionVertexOffset = 0;
    std::size_t compositionIndexOffset = 0;
    std::size_t compositionQuadCount = 0;
    std::vector<std::uint8_t> operationKinds;
    std::vector<std::uint8_t> textureSlotsInUse;
    std::vector<AssetFrameSlot> assetFrameSlots;
    std::uint64_t assetFrameStamp = 0;
    std::size_t operationCount = 0;
    std::vector<std::byte> testReadback;
    std::size_t testReadbackBytes = 0;

    const RequestCompletion *FindDownloadCompletion(
        const DownloadTargetRgba8Request &request) const noexcept
    {
        for (const auto &pending : std::span(requestCompletions).first(requestCompletionCount))
        {
            const auto &completion = *pending.value;
            if (pending.download && completion.sessionId == request.sourceSession &&
                completion.generation == request.sourceGeneration &&
                completion.requestId == request.requestId)
                return &pending;
        }
        return nullptr;
    }
};

void SdlGpuRenderBackend::ResetReservationMetadata(FrameReservation &reservation) noexcept
{
    reservation.commandBuffer = nullptr;
    reservation.fence = nullptr;
    reservation.swapchainAcquired = false;
    reservation.cancelSucceeded = true;
    reservation.completionPrepared = false;
    reservation.targetCount = 0;
    reservation.textureCount = 0;
    reservation.samplerCount = 0;
    reservation.pipelineCount = 0;
    reservation.geometryCount = 0;
    reservation.textureEvictionCount = 0;
    reservation.plannedResidentBytes = 0;
    reservation.vertexBuffer = nullptr;
    reservation.indexBuffer = nullptr;
    reservation.vertexBytes = 0;
    reservation.indexBytes = 0;
    reservation.clearVertexOffset = 0;
    reservation.clearIndexOffset = 0;
    reservation.clearQuadCount = 0;
    reservation.clearDrawCount = 0;
    reservation.sessionCompletionCount = 0;
    reservation.requestCompletionCount = 0;
    reservation.downloadBytes = 0;
    reservation.uploadBytes = 0;
    reservation.uploadWriteOffset = 0;
    reservation.compositionVertexOffset = 0;
    reservation.compositionIndexOffset = 0;
    reservation.compositionQuadCount = 0;
    reservation.operationCount = 0;
    reservation.testReadbackBytes = 0;
}

namespace
{
void ReleaseSessionTarget(SdlGpuRenderBackend::Impl &impl,
                          SdlGpuRenderBackend::Impl::SessionTargetRecord &record) noexcept
{
    if (!impl.testMode && impl.device != nullptr)
    {
        if (record.color != nullptr)
        {
            SDL_ReleaseGPUTexture(impl.device, static_cast<SDL_GPUTexture *>(record.color));
        }
        if (record.depth != nullptr)
        {
            SDL_ReleaseGPUTexture(impl.device, static_cast<SDL_GPUTexture *>(record.depth));
        }
    }
    record = {};
}

void ReleaseTexture(SdlGpuRenderBackend::Impl &impl,
                    SdlGpuRenderBackend::Impl::TextureRecord &record) noexcept
{
    if (!impl.testMode && impl.device != nullptr && record.native != nullptr)
    {
        SDL_ReleaseGPUTexture(impl.device, static_cast<SDL_GPUTexture *>(record.native));
    }
    record = {};
}

void ReleaseSampler(SdlGpuRenderBackend::Impl &impl,
                    SdlGpuRenderBackend::Impl::SamplerRecord &record) noexcept
{
    if (!impl.testMode && impl.device != nullptr && record.native != nullptr)
    {
        SDL_ReleaseGPUSampler(impl.device, static_cast<SDL_GPUSampler *>(record.native));
    }
    record = {};
}

void ReleasePipeline(SdlGpuRenderBackend::Impl &impl,
                     SdlGpuRenderBackend::Impl::PipelineRecord &record) noexcept
{
    if (!impl.testMode && impl.device != nullptr && record.native != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(impl.device,
                                       static_cast<SDL_GPUGraphicsPipeline *>(record.native));
    }
    record = {};
}

void ReleaseGeometry(SdlGpuRenderBackend::Impl &impl,
                     SdlGpuRenderBackend::Impl::GeometryRecord &record) noexcept
{
    if (!impl.testMode && impl.device != nullptr)
    {
        if (record.vertices != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, static_cast<SDL_GPUBuffer *>(record.vertices));
        }
        if (record.indices != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, static_cast<SDL_GPUBuffer *>(record.indices));
        }
        if (record.terrainCells != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, static_cast<SDL_GPUBuffer *>(record.terrainCells));
        }
        if (record.terrainInstances != nullptr)
            SDL_ReleaseGPUBuffer(impl.device,
                                 static_cast<SDL_GPUBuffer *>(record.terrainInstances));
        if (record.lightBuffer)
            SDL_ReleaseGPUBuffer(impl.device, record.lightBuffer);
        if (record.lightTransfer)
            SDL_ReleaseGPUTransferBuffer(impl.device, record.lightTransfer);
    }
    record = {};
}

void ClearStores(SdlGpuRenderBackend::Impl &impl) noexcept
{
    for (auto &record : impl.sessionTargets)
    {
        ReleaseSessionTarget(impl, record);
    }
    for (auto &record : impl.textures)
    {
        ReleaseTexture(impl, record);
    }
    impl.textureSlotsByAssetId.clear();
    for (auto &record : impl.samplers)
    {
        ReleaseSampler(impl, record);
    }
    for (auto &record : impl.pipelines)
    {
        ReleasePipeline(impl, record);
    }
    for (auto &record : impl.geometries)
    {
        ReleaseGeometry(impl, record);
    }
    impl.geometrySlots.clear();
    impl.residentTextureBytes = 0;
}

void ClearNativeState(SdlGpuRenderBackend::Impl &impl) noexcept
{
    if (!impl.testMode && impl.device != nullptr && impl.pendingFence != nullptr)
    {
        SDL_ReleaseGPUFence(impl.device, impl.pendingFence);
    }
    ClearStores(impl);
    if (!impl.testMode && impl.device != nullptr)
    {
        if (impl.uploadArena != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(impl.device, impl.uploadArena);
        }
        if (impl.downloadArena != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(impl.device, impl.downloadArena);
        }
        if (impl.geometryVertexBuffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, impl.geometryVertexBuffer);
        }
        if (impl.geometryIndexBuffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, impl.geometryIndexBuffer);
        }
        if (impl.bonePaletteBuffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, impl.bonePaletteBuffer);
        }
        if (impl.drawInstanceBuffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl.device, impl.drawInstanceBuffer);
        }
        if (impl.overlaySampler != nullptr)
        {
            SDL_ReleaseGPUSampler(impl.device, static_cast<SDL_GPUSampler *>(impl.overlaySampler));
        }
        if (impl.overlayAtlas != nullptr)
        {
            SDL_ReleaseGPUTexture(impl.device, static_cast<SDL_GPUTexture *>(impl.overlayAtlas));
        }
        if (impl.windowClaimed && impl.sdlWindow != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(impl.device, impl.sdlWindow);
        }
        SDL_DestroyGPUDevice(impl.device);
    }
    impl.uploadArena = nullptr;
    impl.uploadArenaBytes = 0;
    impl.downloadArena = nullptr;
    impl.geometryVertexBuffer = nullptr;
    impl.geometryVertexBufferBytes = 0;
    impl.geometryIndexBuffer = nullptr;
    impl.geometryIndexBufferBytes = 0;
    impl.bonePaletteBuffer = nullptr;
    impl.bonePaletteBufferBytes = 0;
    impl.drawInstanceBuffer = nullptr;
    impl.drawInstanceBufferBytes = 0;
    impl.geometryBufferAllocationCount = 0;
    impl.drawInstanceBufferAllocationCount = 0;
    impl.overlayAtlas = nullptr;
    impl.overlaySampler = nullptr;
    impl.windowClaimed = false;
    impl.ready = false;
    impl.vsyncAvailable = false;
    impl.vsyncEnabled = false;
    impl.pendingVSync.reset();
    impl.window = nullptr;
    impl.sdlWindow = nullptr;
    impl.device = nullptr;
    impl.depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    impl.swapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    impl.shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
    impl.deviceGeneration = 0;
    impl.retiredFence = 0;
    impl.pendingFence = nullptr;
    impl.pendingFrameSequence = 0;
    // lastFrameSequence is intentionally preserved: sequence
    // monotonicity survives a device generation change so retained
    // pre-loss frames can never repair the new generation.
    impl.swapchainRefreshPending = false;
    impl.ledger = {};
    impl.completions.Clear();
}

bool IsFenceFree(const SdlGpuRenderBackend::Impl &impl, std::uint64_t fence) noexcept
{
    return fence == 0 || fence <= impl.retiredFence;
}

bool CanRecycleGeometryBuffers(const SdlGpuRenderBackend::Impl &impl,
                               const SdlGpuRenderBackend::Impl::GeometryRecord &record,
                               const LogicalGeometryAssetLease &lease) noexcept
{
    return record.live && IsFenceFree(impl, record.lastFence) &&
           record.vertexCapacity >= lease.vertices->size() &&
           record.indexCapacity >= lease.indices->size() &&
           (lease.terrainCells == nullptr ||
            (record.terrainCells != nullptr &&
             record.terrainCellCapacity >= lease.terrainCells->size())) &&
           (lease.terrainInstances == nullptr ||
            (record.terrainInstances != nullptr &&
             record.terrainInstanceCapacity >= lease.terrainInstances->size()));
}

bool IsValidDepthFormat(std::uint32_t format) noexcept
{
    return format == static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT) ||
           format == static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT);
}

bool IsValidColorFormat(std::uint32_t format) noexcept
{
    return format == static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) ||
           format == static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM);
}

SDL_GPUCompareOp ToGpuCompare(RenderCompareFunction value) noexcept
{
    switch (value)
    {
    case RenderCompareFunction::Less:
        return SDL_GPU_COMPAREOP_LESS;
    case RenderCompareFunction::LessOrEqual:
        return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case RenderCompareFunction::Greater:
        return SDL_GPU_COMPAREOP_GREATER;
    case RenderCompareFunction::Always:
        return SDL_GPU_COMPAREOP_ALWAYS;
    case RenderCompareFunction::Equal:
        return SDL_GPU_COMPAREOP_EQUAL;
    }
    return SDL_GPU_COMPAREOP_INVALID;
}

SDL_GPUBlendFactor ToGpuBlend(RenderBlendFactor value) noexcept
{
    switch (value)
    {
    case RenderBlendFactor::Zero:
        return SDL_GPU_BLENDFACTOR_ZERO;
    case RenderBlendFactor::One:
        return SDL_GPU_BLENDFACTOR_ONE;
    case RenderBlendFactor::SrcAlpha:
        return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case RenderBlendFactor::OneMinusSrcAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case RenderBlendFactor::SrcColor:
        return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case RenderBlendFactor::OneMinusSrcColor:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_INVALID;
}

SDL_GPUStencilOp ToGpuStencil(RenderStencilOperation value) noexcept
{
    switch (value)
    {
    case RenderStencilOperation::Keep:
        return SDL_GPU_STENCILOP_KEEP;
    case RenderStencilOperation::Incr:
        return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
    case RenderStencilOperation::Decr:
        return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
    }
    return SDL_GPU_STENCILOP_INVALID;
}

void ApplyGpuStencilReference(SDL_GPURenderPass *pass,
                              const RenderTapeConstants &constants) noexcept
{
    if (constants.stencilEnable)
    {
        SDL_SetGPUStencilReference(pass, static_cast<Uint8>(constants.stencilReference));
    }
}

SDL_GPUCullMode ToGpuCull(bool enabled, RenderCullFace value) noexcept
{
    if (!enabled)
    {
        return SDL_GPU_CULLMODE_NONE;
    }
    return value == RenderCullFace::Front ? SDL_GPU_CULLMODE_FRONT : SDL_GPU_CULLMODE_BACK;
}

SDL_GPUPrimitiveType ToGpuPrimitive(RenderIndexTopology value) noexcept
{
    switch (value)
    {
    case RenderIndexTopology::Lines:
        return SDL_GPU_PRIMITIVETYPE_LINELIST;
    case RenderIndexTopology::Triangles:
        return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

std::optional<std::uint32_t> ToGpuColorWriteMask(std::uint8_t value) noexcept
{
    if ((value & static_cast<std::uint8_t>(~ColorWriteMaskAll)) != 0)
    {
        return std::nullopt;
    }
    std::uint32_t result = 0;
    if ((value & (1u << 0)) != 0)
    {
        result |= SDL_GPU_COLORCOMPONENT_R;
    }
    if ((value & (1u << 1)) != 0)
    {
        result |= SDL_GPU_COLORCOMPONENT_G;
    }
    if ((value & (1u << 2)) != 0)
    {
        result |= SDL_GPU_COLORCOMPONENT_B;
    }
    if ((value & (1u << 3)) != 0)
    {
        result |= SDL_GPU_COLORCOMPONENT_A;
    }
    return result;
}

bool IsRectInside(SessionDisplayRect outer, SessionDisplayRect inner) noexcept
{
    const std::int64_t outerRight = static_cast<std::int64_t>(outer.x) + outer.width;
    const std::int64_t outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    const std::int64_t innerRight = static_cast<std::int64_t>(inner.x) + inner.width;
    const std::int64_t innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    return inner.width != 0 && inner.height != 0 && inner.x >= outer.x && inner.y >= outer.y &&
           innerRight <= outerRight && innerBottom <= outerBottom;
}

bool IsRectInside(RenderTapeRect outer, RenderTapeRect inner) noexcept
{
    const std::int64_t outerRight = static_cast<std::int64_t>(outer.x) + outer.width;
    const std::int64_t outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    const std::int64_t innerRight = static_cast<std::int64_t>(inner.x) + inner.width;
    const std::int64_t innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    return inner.width != 0 && inner.height != 0 && inner.x >= outer.x && inner.y >= outer.y &&
           innerRight <= outerRight && innerBottom <= outerBottom;
}

std::optional<std::size_t> Rgba8ByteCount(std::uint32_t width, std::uint32_t height) noexcept
{
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (width != 0 && pixels / width != height ||
        pixels > (std::numeric_limits<std::size_t>::max)() / 4)
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(pixels * 4);
}

std::optional<SdlGpuRenderBackend::GpuRect> MapGpuRect(RenderTapeRect rect,
                                                       std::uint32_t surfaceHeight, float minDepth,
                                                       float maxDepth) noexcept
{
    if (rect.width == 0 || rect.height == 0 || surfaceHeight == 0 || rect.x < 0 || rect.y < 0 ||
        static_cast<std::uint64_t>(rect.y) + rect.height > surfaceHeight ||
        !std::isfinite(minDepth) || !std::isfinite(maxDepth) || minDepth < 0.0F ||
        minDepth > 1.0F || maxDepth < 0.0F || maxDepth > 1.0F)
    {
        return std::nullopt;
    }
    return SdlGpuRenderBackend::GpuRect{
        rect.x,     static_cast<std::int32_t>(surfaceHeight - rect.y - rect.height),
        rect.width, rect.height,
        minDepth,   maxDepth};
}

std::uint8_t AlphaCompareCode(RenderCompareFunction compare) noexcept
{
    switch (compare)
    {
    case RenderCompareFunction::Less:
        return 1;
    case RenderCompareFunction::LessOrEqual:
        return 2;
    case RenderCompareFunction::Greater:
        return 3;
    case RenderCompareFunction::Always:
        return 4;
    case RenderCompareFunction::Equal:
        return 5;
    }
    return 0;
}

std::array<float, 4> ApplyTextureEnvironment(RenderTextureEnvironment environment,
                                             std::array<float, 4> color,
                                             std::array<float, 4> sampled) noexcept
{
    if (environment == RenderTextureEnvironment::Add)
    {
        color[0] += sampled[0];
        color[1] += sampled[1];
        color[2] += sampled[2];
        color[3] *= sampled[3];
        return color;
    }
    for (std::size_t index = 0; index < color.size(); ++index)
    {
        color[index] *= sampled[index];
    }
    return color;
}

std::array<float, 4> LightingColor(bool enabled) noexcept
{
    return enabled ? std::array<float, 4>{0.04F, 0.04F, 0.04F, 1.0F}
                   : std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
}

std::uint8_t ClearEncoding(const RenderTapeClear &clear, RenderTapeRect target) noexcept
{
    const bool unscissored = !clear.scissorEnable;
    const bool fullColor = clear.clearColor && unscissored &&
                           std::all_of(clear.colorWriteMask.begin(), clear.colorWriteMask.end(),
                                       [](bool enabled) noexcept { return enabled; });
    const bool fullDepth = clear.clearDepth && unscissored && clear.depthWriteEnable;
    const bool fullStencil = clear.clearStencil && unscissored;
    const std::uint8_t requested =
        static_cast<std::uint8_t>((clear.clearColor ? 1u : 0u) | (clear.clearDepth ? 2u : 0u) |
                                  (clear.clearStencil ? 4u : 0u));
    const std::uint8_t load = static_cast<std::uint8_t>(
        (fullColor ? 1u : 0u) | (fullDepth ? 2u : 0u) | (fullStencil ? 4u : 0u));
    (void)target;
    return static_cast<std::uint8_t>(load | static_cast<std::uint8_t>((requested & ~load) << 4));
}

std::uint8_t ClearEncodingAtPosition(const RenderTapeClear &clear, RenderTapeRect target,
                                     bool allowLoad) noexcept
{
    const std::uint8_t encoding = ClearEncoding(clear, target);
    if (allowLoad)
    {
        return encoding;
    }
    const std::uint8_t load = static_cast<std::uint8_t>(encoding & 0x07u);
    return static_cast<std::uint8_t>((encoding & 0xF0u) | static_cast<std::uint8_t>(load << 4));
}

std::array<float, 4> SessionCompositionUv() noexcept
{
    return {0.0F, 0.0F, 1.0F, 1.0F};
}

std::array<float, 4> GlyphUv(wchar_t character) noexcept
{
    const std::size_t slot = WorkspaceGlyphIndex(character);
    const float left = static_cast<float>((slot % 10) * 5) / 50.0F;
    const float top = static_cast<float>((slot / 10) * 7) / 35.0F;
    return {left, top, left + 5.0F / 50.0F, top + 7.0F / 35.0F};
}

std::optional<float> ConvertClipDepth(float clipZ, float clipW) noexcept
{
    if (!std::isfinite(clipZ) || !std::isfinite(clipW))
    {
        return std::nullopt;
    }
    const float result = (clipZ + clipW) * 0.5F;
    return std::isfinite(result) ? std::optional<float>(result) : std::nullopt;
}

std::array<float, 16> ConvertProjectionDepth(const std::array<float, 16> &projection) noexcept
{
    std::array<float, 16> converted = projection;
    for (std::size_t row = 0; row < 4; ++row)
    {
        converted[row * 4 + 2] = (projection[row * 4 + 2] + projection[row * 4 + 3]) * 0.5F;
        converted[row * 4 + 3] = projection[row * 4 + 3];
    }
    return converted;
}
} // namespace

SdlGpuRenderBackend::SdlGpuRenderBackend() noexcept
    : ownerThread_(std::this_thread::get_id()), impl_(new(std::nothrow) Impl())
{
    if (impl_ != nullptr)
    {
        impl_->reservation.reset(new (std::nothrow) FrameReservation());
    }
}

SdlGpuRenderBackend::~SdlGpuRenderBackend()
{
    Shutdown();
}

std::optional<SdlGpuRenderBackend::AllocationLedger> SdlGpuRenderBackend::CalculateLedger(
    std::size_t depthBytes) noexcept
{
    if (depthBytes == 0)
    {
        return std::nullopt;
    }
    return AllocationLedger{0, 0, 0, 0, 0, 0, 0};
}

bool SdlGpuRenderBackend::EnsureUploadArenaCapacity(std::size_t geometryUploadBytes,
                                                    std::size_t skinningUploadBytes,
                                                    std::size_t drawInstanceUploadBytes,
                                                    std::size_t ownerUploadBytes,
                                                    std::size_t overlayDrawBytes) noexcept
{
    if (impl_ == nullptr || !impl_->ready)
    {
        return false;
    }
    const auto geometryAndSkinning = CheckedAdd(geometryUploadBytes, skinningUploadBytes);
    const auto geometrySkinningAndInstances =
        geometryAndSkinning ? CheckedAdd(*geometryAndSkinning, drawInstanceUploadBytes)
                            : std::nullopt;
    const auto base = geometrySkinningAndInstances
                          ? CheckedAdd(*geometrySkinningAndInstances, ownerUploadBytes)
                          : std::nullopt;
    const auto required = base ? CheckedAdd(*base, overlayDrawBytes) : std::nullopt;
    if (!required || *required > (std::numeric_limits<Uint32>::max)())
    {
        return false;
    }
    if (*required == 0)
    {
        impl_->ledger.geometryUploadBytes = 0;
        impl_->ledger.skinningUploadBytes = skinningUploadBytes;
        impl_->ledger.drawInstanceUploadBytes = drawInstanceUploadBytes;
        impl_->ledger.ownerUploadBytes = ownerUploadBytes;
        impl_->ledger.overlayDrawBytes = 0;
        impl_->ledger.clearDrawBytes = 0;
        return true;
    }
    if (*required <= impl_->uploadArenaBytes)
    {
        impl_->ledger.geometryUploadBytes = geometryUploadBytes;
        impl_->ledger.skinningUploadBytes = skinningUploadBytes;
        impl_->ledger.drawInstanceUploadBytes = drawInstanceUploadBytes;
        impl_->ledger.ownerUploadBytes = ownerUploadBytes;
        impl_->ledger.overlayDrawBytes = overlayDrawBytes;
        return true;
    }
    if (impl_->testMode)
    {
        impl_->uploadArena = static_cast<SDL_GPUTransferBuffer *>(TestHandle(3));
        impl_->uploadArenaBytes = *required;
        impl_->ledger.geometryUploadBytes = geometryUploadBytes;
        impl_->ledger.skinningUploadBytes = skinningUploadBytes;
        impl_->ledger.drawInstanceUploadBytes = drawInstanceUploadBytes;
        impl_->ledger.ownerUploadBytes = ownerUploadBytes;
        impl_->ledger.overlayDrawBytes = overlayDrawBytes;
        return true;
    }

    const SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                               static_cast<Uint32>(*required), 0};
    SDL_GPUTransferBuffer *const replacement = SDL_CreateGPUTransferBuffer(impl_->device, &info);
    if (replacement == nullptr)
    {
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->uploadArena);
    impl_->uploadArena = replacement;
    impl_->uploadArenaBytes = *required;
    impl_->ledger.geometryUploadBytes = geometryUploadBytes;
    impl_->ledger.skinningUploadBytes = skinningUploadBytes;
    impl_->ledger.drawInstanceUploadBytes = drawInstanceUploadBytes;
    impl_->ledger.ownerUploadBytes = ownerUploadBytes;
    impl_->ledger.overlayDrawBytes = overlayDrawBytes;
    return true;
}

bool SdlGpuRenderBackend::EnsureDownloadArenaCapacity(std::size_t bytes) noexcept
{
    if (impl_ == nullptr || !impl_->ready || bytes > (std::numeric_limits<Uint32>::max)())
    {
        return false;
    }
    if (bytes <= impl_->ledger.downloadTransferBytes)
    {
        return true;
    }
    if (impl_->testMode)
    {
        if (!impl_->nativeTestConfig.downloadArena)
        {
            return false;
        }
        impl_->ledger.downloadTransferBytes = bytes;
        return true;
    }

    const SDL_GPUTransferBufferCreateInfo info{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                                               static_cast<Uint32>(bytes), 0};
    SDL_GPUTransferBuffer *const replacement = SDL_CreateGPUTransferBuffer(impl_->device, &info);
    if (replacement == nullptr)
    {
        return false;
    }
    if (impl_->downloadArena != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->downloadArena);
    }
    impl_->downloadArena = replacement;
    impl_->ledger.downloadTransferBytes = bytes;
    return true;
}

bool SdlGpuRenderBackend::EnsureGeometryBufferCapacity(std::size_t vertexBytes,
                                                       std::size_t indexBytes) noexcept
{
    if (impl_ == nullptr || !impl_->ready || vertexBytes > (std::numeric_limits<Uint32>::max)() ||
        indexBytes > (std::numeric_limits<Uint32>::max)())
    {
        return false;
    }

    const auto ensure = [&](SDL_GPUBuffer *&buffer, std::size_t &capacity, std::size_t required,
                            SDL_GPUBufferUsageFlags usage, std::uintptr_t testHandle) noexcept {
        if (required == 0 || (buffer != nullptr && required <= capacity))
        {
            return true;
        }

        SDL_GPUBuffer *replacement = nullptr;
        if (impl_->testMode)
        {
            replacement = static_cast<SDL_GPUBuffer *>(TestHandle(testHandle));
        }
        else
        {
            const SDL_GPUBufferCreateInfo info{usage, static_cast<Uint32>(required), 0};
            replacement = SDL_CreateGPUBuffer(impl_->device, &info);
        }
        if (replacement == nullptr)
        {
            return false;
        }
        if (!impl_->testMode && buffer != nullptr)
        {
            SDL_ReleaseGPUBuffer(impl_->device, buffer);
        }
        buffer = replacement;
        capacity = required;
        ++impl_->geometryBufferAllocationCount;
        return true;
    };

    return ensure(impl_->geometryVertexBuffer, impl_->geometryVertexBufferBytes, vertexBytes,
                  SDL_GPU_BUFFERUSAGE_VERTEX, 0x8600) &&
           ensure(impl_->geometryIndexBuffer, impl_->geometryIndexBufferBytes, indexBytes,
                  SDL_GPU_BUFFERUSAGE_INDEX, 0x8601);
}

bool SdlGpuRenderBackend::EnsureBonePaletteBufferCapacity(std::size_t bytes) noexcept
{
    if (impl_ == nullptr || !impl_->ready || bytes > (std::numeric_limits<Uint32>::max)())
    {
        return false;
    }
    const std::size_t required = (std::max)(bytes, sizeof(RenderTapeBoneMatrix));
    if (impl_->bonePaletteBuffer != nullptr && required <= impl_->bonePaletteBufferBytes)
    {
        return true;
    }
    SDL_GPUBuffer *replacement = nullptr;
    if (impl_->testMode)
    {
        replacement = static_cast<SDL_GPUBuffer *>(TestHandle(0x8602));
    }
    else
    {
        const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                           static_cast<Uint32>(required), 0};
        replacement = SDL_CreateGPUBuffer(impl_->device, &info);
    }
    if (replacement == nullptr)
    {
        return false;
    }
    if (!impl_->testMode && impl_->bonePaletteBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(impl_->device, impl_->bonePaletteBuffer);
    }
    impl_->bonePaletteBuffer = replacement;
    impl_->bonePaletteBufferBytes = required;
    return true;
}

bool SdlGpuRenderBackend::EnsureDrawInstanceBufferCapacity(std::size_t bytes) noexcept
{
    if (impl_ == nullptr || !impl_->ready || bytes > (std::numeric_limits<Uint32>::max)())
    {
        return false;
    }
    static_assert(sizeof(RenderTapeQuadInstance) == sizeof(RenderTapeRigidInstance));
    const std::size_t required = (std::max)(bytes, sizeof(std::array<float, 4>));
    if (impl_->drawInstanceBuffer != nullptr && required <= impl_->drawInstanceBufferBytes)
    {
        return true;
    }
    SDL_GPUBuffer *replacement = nullptr;
    if (impl_->testMode)
    {
        replacement = static_cast<SDL_GPUBuffer *>(TestHandle(0x8603));
    }
    else
    {
        const SDL_GPUBufferCreateInfo info{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                           static_cast<Uint32>(required), 0};
        replacement = SDL_CreateGPUBuffer(impl_->device, &info);
    }
    if (replacement == nullptr)
    {
        return false;
    }
    if (!impl_->testMode && impl_->drawInstanceBuffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(impl_->device, impl_->drawInstanceBuffer);
    }
    impl_->drawInstanceBuffer = replacement;
    impl_->drawInstanceBufferBytes = required;
    ++impl_->drawInstanceBufferAllocationCount;
    return true;
}

std::optional<SdlGpuRenderBackend::SamplerPolicy> SdlGpuRenderBackend::MakeSamplerPolicy(
    RenderSamplerIntent intent) noexcept
{
    if (!IsValid(intent))
    {
        return std::nullopt;
    }

    SamplerPolicy policy;
    policy.minFilter = policy.magFilter = intent.filter == LegacyTextureFilter::Nearest ? 0 : 1;
    policy.addressModeU = policy.addressModeV = intent.wrap == LegacyTextureWrap::Repeat ? 0 : 2;
    policy.addressModeW = 2;
    return policy;
}

std::optional<SdlGpuRenderBackend::PipelinePolicy> SdlGpuRenderBackend::MakePipelinePolicy(
    const PipelineKey &key) const noexcept
{
    bool validFamily = false;
    switch (key.shaderFamily)
    {
    case ShaderFamily::RenderTape:
    case ShaderFamily::Composition:
    case ShaderFamily::OverlayRect:
    case ShaderFamily::Label:
        validFamily = true;
        break;
    }
    if (impl_ == nullptr || !impl_->ready || key.deviceGeneration != impl_->deviceGeneration ||
        key.vertexLayout != CanonicalVertexLayout ||
        (key.topology != RenderIndexTopology::Lines &&
         key.topology != RenderIndexTopology::Triangles) ||
        !validFamily || !ToGpuColorWriteMask(key.colorWriteMask) || !IsValid(key.blendSource) ||
        !IsValid(key.blendDestination) || !IsValid(key.depthCompare) ||
        !IsValid(key.stencilCompare) || !IsValid(key.stencilFail) ||
        !IsValid(key.stencilDepthFail) || !IsValid(key.stencilPass) || !IsValid(key.cullFace) ||
        !IsValid(key.frontFace) || (key.stencilReplace && !key.stencilEnable) ||
        !IsValidColorFormat(key.colorFormat) ||
        (key.hasDepthStencilTarget &&
         (!IsValidDepthFormat(key.depthFormat) ||
          key.depthFormat != static_cast<std::uint32_t>(impl_->depthFormat))) ||
        (!key.hasDepthStencilTarget && (key.shaderFamily != ShaderFamily::Composition &&
                                            key.shaderFamily != ShaderFamily::OverlayRect &&
                                            key.shaderFamily != ShaderFamily::Label ||
                                        key.depthFormat != 0)))
    {
        return std::nullopt;
    }
    const std::uint32_t sessionColorFormat =
        static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    const std::uint32_t requiredColorFormat =
        (key.shaderFamily == ShaderFamily::Composition ||
         key.shaderFamily == ShaderFamily::OverlayRect || key.shaderFamily == ShaderFamily::Label)
            ? static_cast<std::uint32_t>(impl_->swapchainFormat)
            : sessionColorFormat;
    if (key.colorFormat != requiredColorFormat)
    {
        return std::nullopt;
    }

    return PipelinePolicy{};
}

bool SdlGpuRenderBackend::CreatePipelineNative(const PipelineKey &key, const PipelinePolicy &policy,
                                               void *&native) noexcept
{
    native = nullptr;
    if (impl_ == nullptr || impl_->testMode || impl_->device == nullptr)
    {
        return false;
    }
    const auto colorWriteMask = ToGpuColorWriteMask(key.colorWriteMask);
    if (!colorWriteMask)
    {
        return false;
    }

    const Uint8 *vertexCode = nullptr;
    std::size_t vertexSize = 0;
    const Uint8 *fragmentCode = nullptr;
    std::size_t fragmentSize = 0;
    switch (impl_->shaderFormat)
    {
    case SDL_GPU_SHADERFORMAT_DXIL:
        vertexCode = RenderTapeShaderArtifacts::RenderTapeVertexDxil.data();
        vertexSize = RenderTapeShaderArtifacts::RenderTapeVertexDxil.size();
        fragmentCode = RenderTapeShaderArtifacts::RenderTapeFragmentDxil.data();
        fragmentSize = RenderTapeShaderArtifacts::RenderTapeFragmentDxil.size();
        break;
    case SDL_GPU_SHADERFORMAT_SPIRV:
        vertexCode = RenderTapeShaderArtifacts::RenderTapeVertexSpirv.data();
        vertexSize = RenderTapeShaderArtifacts::RenderTapeVertexSpirv.size();
        fragmentCode = RenderTapeShaderArtifacts::RenderTapeFragmentSpirv.data();
        fragmentSize = RenderTapeShaderArtifacts::RenderTapeFragmentSpirv.size();
        break;
    case SDL_GPU_SHADERFORMAT_MSL:
        vertexCode = RenderTapeShaderArtifacts::RenderTapeVertexMsl.data();
        vertexSize = RenderTapeShaderArtifacts::RenderTapeVertexMsl.size();
        fragmentCode = RenderTapeShaderArtifacts::RenderTapeFragmentMsl.data();
        fragmentSize = RenderTapeShaderArtifacts::RenderTapeFragmentMsl.size();
        break;
    default:
        return false;
    }

    const SDL_GPUShaderCreateInfo vertexShaderInfo{vertexSize,
                                                   vertexCode,
                                                   "main",
                                                   impl_->shaderFormat,
                                                   SDL_GPU_SHADERSTAGE_VERTEX,
                                                   0,
                                                   0,
                                                   RenderTapeVertexStorageBufferCount,
                                                   1,
                                                   0};
    const SDL_GPUShaderCreateInfo fragmentShaderInfo{fragmentSize,
                                                     fragmentCode,
                                                     "main",
                                                     impl_->shaderFormat,
                                                     SDL_GPU_SHADERSTAGE_FRAGMENT,
                                                     1,
                                                     0,
                                                     0,
                                                     1,
                                                     0};
    SDL_GPUShader *const vertexShader = SDL_CreateGPUShader(impl_->device, &vertexShaderInfo);
    if (vertexShader == nullptr)
    {
        return false;
    }
    SDL_GPUShader *const fragmentShader = SDL_CreateGPUShader(impl_->device, &fragmentShaderInfo);
    if (fragmentShader == nullptr)
    {
        SDL_ReleaseGPUShader(impl_->device, vertexShader);
        return false;
    }

    const SDL_GPUVertexBufferDescription vertexBufferDescription{
        0, static_cast<Uint32>(sizeof(RenderTapeVertex)), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    const SDL_GPUVertexAttribute vertexAttributes[] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         static_cast<Uint32>(offsetof(RenderTapeVertex, position))},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         static_cast<Uint32>(offsetof(RenderTapeVertex, color))},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         static_cast<Uint32>(offsetof(RenderTapeVertex, normal))},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         static_cast<Uint32>(offsetof(RenderTapeVertex, textureCoordinate))},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT4,
         static_cast<Uint32>(offsetof(RenderTapeVertex, bmdSource))},
    };
    const SDL_GPUVertexInputState vertexInputState{
        &vertexBufferDescription, 1, vertexAttributes,
        static_cast<Uint32>(std::size(vertexAttributes))};

    SDL_GPURasterizerState rasterizer{};
    rasterizer.fill_mode = SDL_GPU_FILLMODE_FILL;
    rasterizer.cull_mode = ToGpuCull(key.cullEnable, key.cullFace);
    rasterizer.front_face = key.frontFace == RenderFrontFace::Clockwise
                                ? SDL_GPU_FRONTFACE_CLOCKWISE
                                : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rasterizer.depth_bias_constant_factor = policy.depthBiasConstant;
    rasterizer.depth_bias_clamp = policy.depthBiasClamp;
    rasterizer.depth_bias_slope_factor = policy.depthBiasSlope;
    rasterizer.enable_depth_bias = policy.depthBiasEnabled;
    rasterizer.enable_depth_clip = policy.depthClip;

    SDL_GPUDepthStencilState depthStencil{};
    depthStencil.compare_op = ToGpuCompare(key.depthCompare);
    depthStencil.back_stencil_state = {
        ToGpuStencil(key.stencilFail),
        key.stencilReplace ? SDL_GPU_STENCILOP_REPLACE : ToGpuStencil(key.stencilPass),
        ToGpuStencil(key.stencilDepthFail), ToGpuCompare(key.stencilCompare)};
    depthStencil.front_stencil_state = depthStencil.back_stencil_state;
    depthStencil.compare_mask = key.stencilReadMask;
    depthStencil.write_mask = key.stencilWriteMask;
    depthStencil.enable_depth_test = key.depthTestEnable;
    depthStencil.enable_depth_write = key.depthWriteEnable;
    depthStencil.enable_stencil_test = key.stencilEnable;

    SDL_GPUColorTargetBlendState blend{};
    blend.src_color_blendfactor = ToGpuBlend(key.blendSource);
    blend.dst_color_blendfactor = ToGpuBlend(key.blendDestination);
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = ToGpuBlend(key.blendSource);
    blend.dst_alpha_blendfactor = ToGpuBlend(key.blendDestination);
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.color_write_mask = *colorWriteMask;
    blend.enable_blend = key.blendEnable;
    blend.enable_color_write_mask = true;
    const SDL_GPUColorTargetDescription colorTarget{
        static_cast<SDL_GPUTextureFormat>(key.colorFormat), blend};
    const SDL_GPUGraphicsPipelineTargetInfo targetInfo{
        &colorTarget,
        1,
        static_cast<SDL_GPUTextureFormat>(key.depthFormat),
        key.hasDepthStencilTarget,
        0,
        0,
        0};
    const SDL_GPUMultisampleState multisample{SDL_GPU_SAMPLECOUNT_1, 0, false, false, 0, 0};
    const SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{vertexShader,
                                                         fragmentShader,
                                                         vertexInputState,
                                                         ToGpuPrimitive(key.topology),
                                                         rasterizer,
                                                         multisample,
                                                         depthStencil,
                                                         targetInfo,
                                                         0};
    SDL_GPUGraphicsPipeline *const pipeline =
        SDL_CreateGPUGraphicsPipeline(impl_->device, &pipelineInfo);
    SDL_ReleaseGPUShader(impl_->device, vertexShader);
    SDL_ReleaseGPUShader(impl_->device, fragmentShader);
    if (pipeline == nullptr)
        return false;
    native = pipeline;
    return true;
}

SdlGpuRenderBackend::PipelineKey SdlGpuRenderBackend::ClearPipelineKeyForPeer(
    const RenderTapeClear &clear) const noexcept
{
    PipelineKey key = DefaultPipelineKeyForPeer();
    key.shaderFamily = ShaderFamily::RenderTape;
    key.topology = RenderIndexTopology::Triangles;
    key.vertexLayout = CanonicalVertexLayout;
    key.samplerUse = false;
    key.blendEnable = false;
    key.blendSource = RenderBlendFactor::One;
    key.blendDestination = RenderBlendFactor::Zero;
    key.depthTestEnable = clear.clearDepth && clear.depthWriteEnable;
    key.depthWriteEnable = clear.clearDepth && clear.depthWriteEnable;
    key.depthCompare = RenderCompareFunction::Always;
    key.stencilEnable = clear.clearStencil;
    key.stencilCompare = RenderCompareFunction::Always;
    key.stencilFail = RenderStencilOperation::Keep;
    key.stencilDepthFail = RenderStencilOperation::Keep;
    key.stencilPass = RenderStencilOperation::Keep;
    key.stencilReplace = clear.clearStencil;
    key.stencilReadMask = 0xFF;
    key.stencilWriteMask = 0xFF;
    key.cullEnable = false;
    key.colorWriteMask = clear.clearColor
                             ? static_cast<std::uint8_t>((clear.colorWriteMask[0] ? 1u : 0u) |
                                                         (clear.colorWriteMask[1] ? 2u : 0u) |
                                                         (clear.colorWriteMask[2] ? 4u : 0u) |
                                                         (clear.colorWriteMask[3] ? 8u : 0u))
                             : 0;
    key.hasDepthStencilTarget = true;
    key.colorFormat = static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
    if (impl_ != nullptr)
    {
        key.depthFormat = static_cast<std::uint32_t>(impl_->depthFormat);
        key.deviceGeneration = impl_->deviceGeneration;
    }
    return key;
}

std::optional<SdlGpuRenderBackend::ClearDrawDescriptor> SdlGpuRenderBackend::
    BuildClearDrawDescriptorForPeer(const RenderTapeClear &clear, std::uint8_t encoding,
                                    std::uint32_t width, std::uint32_t height,
                                    std::size_t vertexOffset,
                                    std::size_t indexOffset) const noexcept
{
    const std::uint8_t attachmentMask = static_cast<std::uint8_t>((encoding >> 4) & 0x0F);
    if (impl_ == nullptr || attachmentMask == 0 || width == 0 || height == 0)
    {
        return std::nullopt;
    }
    const auto scissor = clear.scissorEnable
                             ? ScissorForPeer(clear.scissor, height)
                             : std::optional<GpuRect>(GpuRect{0, 0, width, height, 0.0F, 1.0F});
    if (!scissor)
    {
        return std::nullopt;
    }
    ClearDrawDescriptor descriptor;
    descriptor.pipeline = ClearPipelineKeyForPeer(clear);
    descriptor.scissor = *scissor;
    descriptor.targetWidth = width;
    descriptor.targetHeight = height;
    descriptor.color = clear.color;
    descriptor.depth = clear.depth;
    descriptor.stencil = clear.stencil;
    descriptor.attachmentMask = attachmentMask;
    descriptor.vertexOffset = vertexOffset;
    descriptor.indexOffset = indexOffset;
    return descriptor;
}

bool SdlGpuRenderBackend::PreflightFrame(const ApplicationRenderFrame &frame,
                                         std::vector<CandidatePlan> &plans, std::size_t &planCount,
                                         std::size_t &completionBytes) const noexcept
{
    planCount = 0;
    completionBytes = 0;
    const bool validatedByApplication = frame.WasValidatedByApplication();
    if (impl_ == nullptr || !impl_->ready || frame.FrameSequence() == 0 ||
        frame.WindowRect().width == 0 || frame.WindowRect().height == 0)
    {
        return false;
    }

    // Application frames already preserve the render-tape invariant. For
    // that production path, collect only the small facts required by native
    // allocation. Do not walk commands, rebuild asset hash sets, or validate
    // tape payloads a second time.
    if (validatedByApplication)
    {
        try
        {
            plans.clear();
            plans.reserve(frame.SessionCount());

            std::size_t maxAssetId = 0;
            for (const LogicalRenderAssetLease &lease : frame.Assets())
            {
                if (lease.asset.id.value >= (std::numeric_limits<std::size_t>::max)())
                {
                    return false;
                }
                maxAssetId = (std::max)(maxAssetId, static_cast<std::size_t>(lease.asset.id.value));
            }
            if (!frame.Assets().empty() && maxAssetId >= impl_->textureSlotsByAssetId.size())
            {
                impl_->textureSlotsByAssetId.resize(maxAssetId + 1,
                                                    (std::numeric_limits<std::size_t>::max)());
            }

            const auto residentTexture = [&](LogicalRenderAssetRef asset) {
                if (asset.id.value >= impl_->textureSlotsByAssetId.size())
                {
                    return false;
                }
                const std::size_t slot = impl_->textureSlotsByAssetId[asset.id.value];
                return slot < impl_->textures.size() && impl_->textures[slot].live &&
                       impl_->textures[slot].key.deviceGeneration == impl_->deviceGeneration &&
                       impl_->textures[slot].key.asset == asset;
            };

            std::size_t ownerUploadBytes = 0;
            for (const LogicalRenderAssetLease &lease : frame.Assets())
            {
                if (residentTexture(lease.asset))
                {
                    continue;
                }
                const auto total = CheckedAdd(ownerUploadBytes, lease.bytes->size());
                if (!total)
                {
                    return false;
                }
                ownerUploadBytes = *total;
            }

            for (std::size_t sessionIndex = 0; sessionIndex < frame.SessionCount(); ++sessionIndex)
            {
                const auto *session = frame.Session(sessionIndex);
                if (session == nullptr)
                {
                    return false;
                }
                CandidatePlan plan;
                plan.valid = true;
                plan.sessionIndex = sessionIndex;
                plan.sessionId = session->id.RawValue();
                plan.sessionGeneration = session->generation.RawValue();
                plan.surfaceGeneration = session->tape.SurfaceGeneration();
                plan.width = session->tape.ViewportWidth();
                plan.height = session->tape.ViewportHeight();

                for (const LogicalGeometryAssetLease &lease : session->tape.GeometryAssets())
                {
                    if (impl_->geometrySlots.contains(lease.asset))
                    {
                        continue;
                    }
                    const auto vertexBytes =
                        CheckedMultiply(lease.vertices->size(), sizeof(RenderTapeVertex));
                    const auto indexBytes =
                        CheckedMultiply(lease.indices->size(), sizeof(std::uint32_t));
                    const auto terrainBytes = lease.terrainCells == nullptr
                                                  ? std::optional<std::size_t>{0}
                                                  : CheckedMultiply(lease.terrainCells->size(),
                                                                    sizeof(RenderTapeTerrainCell));
                    const auto instanceBytes =
                        lease.terrainInstances == nullptr
                            ? std::optional<std::size_t>{0}
                            : CheckedMultiply(lease.terrainInstances->size(),
                                              sizeof(RenderTapeTerrainInstance));
                    const auto geometryBytes = vertexBytes && indexBytes
                                                   ? CheckedAdd(*vertexBytes, *indexBytes)
                                                   : std::nullopt;
                    const auto cellsBytes = geometryBytes && terrainBytes
                                                ? CheckedAdd(*geometryBytes, *terrainBytes)
                                                : std::nullopt;
                    const auto bytes = cellsBytes && instanceBytes
                                           ? CheckedAdd(*cellsBytes, *instanceBytes)
                                           : std::nullopt;
                    const auto total = bytes
                                           ? CheckedAdd(plan.persistentGeometryUploadBytes, *bytes)
                                           : std::nullopt;
                    if (!total)
                    {
                        return false;
                    }
                    plan.persistentGeometryUploadBytes = *total;
                }
                for (const RenderOwnerRequest &request : session->tape.OwnerRequests())
                {
                    const auto *download = std::get_if<DownloadTargetRgba8Request>(&request);
                    if (download == nullptr)
                    {
                        continue;
                    }
                    const auto bytes = Rgba8ByteCount(download->rect.width, download->rect.height);
                    const auto total =
                        bytes ? CheckedAdd(plan.completionBytes, *bytes) : std::nullopt;
                    if (!total)
                    {
                        return false;
                    }
                    plan.completionBytes = *total;
                }
                const auto totalCompletion = CheckedAdd(completionBytes, plan.completionBytes);
                if (!totalCompletion)
                {
                    return false;
                }
                completionBytes = *totalCompletion;
                plans.push_back(plan);
                ++planCount;
            }
            if (planCount != 0)
            {
                plans[0].uploadBytes = ownerUploadBytes;
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    FrameAssetLookup assetLookup;
    std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> plannedAssets;
    std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> residentAssets;
    std::vector<RenderSamplerIntent> plannedSamplers;
    try
    {
        plans.clear();
        plans.reserve(frame.SessionCount());
        plannedAssets.reserve(frame.Assets().size());
        if (!assetLookup.Initialize(frame.Assets()))
        {
            return false;
        }
        residentAssets.reserve(impl_->textures.size());
        for (const auto &texture : impl_->textures)
        {
            if (texture.live && texture.key.deviceGeneration == impl_->deviceGeneration)
            {
                residentAssets.insert(texture.key.asset);
            }
        }
        plannedSamplers.push_back({LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge});
    }
    catch (...)
    {
        return false;
    }
    const SessionDisplayRect window = frame.WindowRect();
    const auto findLease =
        [&](LogicalRenderAssetRef asset) noexcept -> const LogicalRenderAssetLease * {
        const LogicalRenderAssetLease *const lease = assetLookup.Find(asset);
        return lease != nullptr && IsValid(*lease) ? lease : nullptr;
    };
    const auto appendPipeline = [](CandidatePlan &plan, const PipelineKey &key) noexcept {
        for (std::size_t index = 0; index < plan.pipelineCount; ++index)
        {
            if (plan.pipelines[index] == key)
            {
                return true;
            }
        }
        try
        {
            plan.pipelines.push_back(key);
            ++plan.pipelineCount;
            return true;
        }
        catch (...)
        {
            return false;
        }
    };
    const auto pipelineFor = [&](const RenderTapeDraw &draw,
                                 const RenderTapeConstants &constants) noexcept {
        PipelineKey key;
        key.topology = draw.topology;
        key.vertexLayout = 1;
        key.samplerUse = draw.asset.id.value != 0 || draw.asset.revision != 0;
        key.blendEnable = constants.blendEnable;
        key.blendSource = constants.blendSrc;
        key.blendDestination = constants.blendDst;
        key.depthTestEnable = constants.depthTestEnable;
        key.depthWriteEnable = constants.depthWriteEnable;
        key.depthCompare = constants.depthCompare;
        key.stencilEnable = constants.stencilEnable;
        key.stencilCompare = constants.stencilCompare;
        key.stencilFail = constants.stencilFailOp;
        key.stencilDepthFail = constants.stencilDepthFailOp;
        key.stencilPass = constants.stencilPassOp;
        key.stencilReadMask = static_cast<std::uint8_t>(constants.stencilReadMask);
        key.stencilWriteMask = static_cast<std::uint8_t>(constants.stencilReadMask);
        key.cullEnable = constants.cullEnable;
        key.cullFace = constants.cullFace;
        key.frontFace = constants.frontFace;
        key.colorWriteMask = static_cast<std::uint8_t>(
            (constants.colorWriteMask[0] ? 1u : 0u) | (constants.colorWriteMask[1] ? 2u : 0u) |
            (constants.colorWriteMask[2] ? 4u : 0u) | (constants.colorWriteMask[3] ? 8u : 0u));
        key.colorFormat = static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        key.depthFormat = static_cast<std::uint32_t>(impl_->depthFormat);
        key.deviceGeneration = impl_->deviceGeneration;
        return key;
    };
    const auto plannedAsset = [&](LogicalRenderAssetRef asset) noexcept {
        return plannedAssets.contains(asset);
    };
    const auto plannedSampler = [&](RenderSamplerIntent sampler) noexcept {
        for (const RenderSamplerIntent planned : plannedSamplers)
        {
            if (planned == sampler)
            {
                return true;
            }
        }
        return false;
    };
    for (std::size_t sessionIndex = 0; sessionIndex < frame.SessionCount(); ++sessionIndex)
    {
        CandidatePlan plan;
        plan.sessionIndex = sessionIndex;
        std::vector<LogicalRenderAssetRef> candidateAssets;
        std::unordered_map<LogicalRenderAssetRef, std::size_t, LogicalRenderAssetRefHash>
            candidateAssetBytes;
        std::vector<RenderSamplerIntent> candidateSamplers;
        try
        {
            const auto *candidateSession = frame.Session(sessionIndex);
            if (candidateSession != nullptr)
            {
                const std::size_t candidates = candidateSession->tape.LogicalAssets().size() +
                                               candidateSession->tape.OwnerRequests().size();
                candidateAssets.reserve(candidates);
                if (!validatedByApplication)
                {
                    candidateAssetBytes.reserve(candidates);
                }
                candidateSamplers.reserve(candidates);
            }
        }
        catch (...)
        {
            return false;
        }
        const auto appendCandidateAsset = [&](LogicalRenderAssetRef asset,
                                              std::size_t bytes) noexcept {
            try
            {
                const auto [found, inserted] = candidateAssetBytes.emplace(asset, bytes);
                if (!inserted)
                {
                    return found->second == bytes;
                }
                candidateAssets.push_back(asset);
                return true;
            }
            catch (...)
            {
                return false;
            }
        };
        const auto appendCandidateSampler = [&](RenderSamplerIntent sampler) noexcept {
            for (const RenderSamplerIntent candidate : candidateSamplers)
            {
                if (candidate == sampler)
                {
                    return true;
                }
            }
            try
            {
                candidateSamplers.push_back(sampler);
                return true;
            }
            catch (...)
            {
                return false;
            }
        };
        const ApplicationRenderFrame::SessionRecord *const session = frame.Session(sessionIndex);
        if (session == nullptr ||
            (!validatedByApplication &&
             (session->id.RawValue() == 0 || session->generation.RawValue() == 0 ||
              session->id != session->tape.Id() ||
              session->generation != session->tape.Generation() ||
              session->tape.FrameSequence() != frame.FrameSequence() ||
              session->tape.SurfaceGeneration() == 0 || session->tape.ViewportWidth() == 0 ||
              session->tape.ViewportHeight() == 0 || !IsRectInside(window, session->destination) ||
              !session->tape.HasValidStorageCounts())))
        {
            continue;
        }
        plan.sessionId = session->id.RawValue();
        plan.sessionGeneration = session->generation.RawValue();
        plan.surfaceGeneration = session->tape.SurfaceGeneration();
        plan.width = session->tape.ViewportWidth();
        plan.height = session->tape.ViewportHeight();
        bool valid = true;
        bool firstClear = true;
        std::uint64_t lastStableOrder = 0;
        const RenderTapeRect viewport{0, 0, plan.width, plan.height};
        for (const LogicalGeometryAssetLease &lease : session->tape.GeometryAssets())
        {
            if (!validatedByApplication &&
                (!IsValid(lease) || lease.asset.sessionId != session->id.RawValue() ||
                 lease.asset.generation != session->generation.RawValue()))
            {
                valid = false;
                break;
            }
            if (impl_->geometrySlots.contains(lease.asset))
            {
                continue;
            }
            const auto vertexBytes =
                CheckedMultiply(lease.vertices->size(), sizeof(RenderTapeVertex));
            const auto indexBytes = CheckedMultiply(lease.indices->size(), sizeof(std::uint32_t));
            const auto terrainBytes =
                lease.terrainCells == nullptr
                    ? std::optional<std::size_t>{0}
                    : CheckedMultiply(lease.terrainCells->size(), sizeof(RenderTapeTerrainCell));
            const auto instanceBytes =
                lease.terrainInstances == nullptr
                    ? std::optional<std::size_t>{0}
                    : CheckedMultiply(lease.terrainInstances->size(),
                                      sizeof(RenderTapeTerrainInstance));
            const auto geometryBytes =
                vertexBytes && indexBytes ? CheckedAdd(*vertexBytes, *indexBytes) : std::nullopt;
            const auto cellsBytes = geometryBytes && terrainBytes
                                        ? CheckedAdd(*geometryBytes, *terrainBytes)
                                        : std::nullopt;
            const auto bytes = cellsBytes && instanceBytes
                                   ? CheckedAdd(*cellsBytes, *instanceBytes)
                                   : std::nullopt;
            const auto total =
                bytes ? CheckedAdd(plan.persistentGeometryUploadBytes, *bytes) : std::nullopt;
            if (!total)
            {
                valid = false;
                break;
            }
            plan.persistentGeometryUploadBytes = *total;
        }
        if (!valid)
        {
            continue;
        }
        if (validatedByApplication)
        {
            // Recording and ApplicationRenderFrame already established the
            // portable draw invariant. Pipelines are resolved from the cache
            // on first use during encoding, so a second draw/clear scan here
            // only repeats trusted work on the owner thread.
            for (const LogicalRenderAssetRef asset : session->tape.LogicalAssets())
            {
                if (!valid)
                {
                    break;
                }
                const LogicalRenderAssetLease *const lease = findLease(asset);
                if (lease == nullptr)
                {
                    valid = false;
                    break;
                }
                try
                {
                    candidateAssets.push_back(asset);
                }
                catch (...)
                {
                    valid = false;
                    break;
                }
                valid = appendCandidateSampler(lease->sampler);
            }
            for (const RenderOwnerRequest &request : session->tape.OwnerRequests())
            {
                if (!valid)
                {
                    break;
                }
                const auto *download = std::get_if<DownloadTargetRgba8Request>(&request);
                if (download == nullptr)
                {
                    continue;
                }
                const auto bytes = Rgba8ByteCount(download->rect.width, download->rect.height);
                const auto next = bytes ? CheckedAdd(plan.completionBytes, *bytes) : std::nullopt;
                valid = next.has_value();
                if (valid)
                {
                    plan.completionBytes = *next;
                }
            }
        }
        else
        {
            for (const RenderTapeEntry &entry : session->tape.Entries())
            {
                if (!IsValid(entry.kind) || !IsValid(entry.pass) || entry.stableOrder == 0 ||
                    entry.stableOrder <= lastStableOrder)
                {
                    valid = false;
                    break;
                }
                lastStableOrder = entry.stableOrder;
                if (entry.kind == RenderTapeEntryKind::Clear)
                {
                    valid = entry.index < session->tape.Clears().size() &&
                            IsValid(session->tape.Clears()[entry.index]) &&
                            (!session->tape.Clears()[entry.index].scissorEnable ||
                             IsRectInside(viewport, session->tape.Clears()[entry.index].scissor));
                    if (!valid)
                    {
                        break;
                    }
                    const RenderTapeClear &clear = session->tape.Clears()[entry.index];
                    const std::uint8_t encoding =
                        ClearEncodingAtPosition(clear, viewport, firstClear);
                    if ((encoding & 0xF0u) != 0)
                    {
                        const PipelineKey key = ClearPipelineKeyForPeer(clear);
                        valid = MakePipelinePolicy(key).has_value() && appendPipeline(plan, key);
                        if (!valid)
                        {
                            break;
                        }
                    }
                    firstClear = false;
                    continue;
                }
                if (entry.kind == RenderTapeEntryKind::Draw)
                {
                    if (entry.index >= session->tape.Draws().size())
                    {
                        valid = false;
                        break;
                    }
                    const RenderTapeDraw &draw = session->tape.Draws()[entry.index];
                    const bool hasGeometry = IsValid(draw.geometry);
                    const LogicalGeometryAssetLease *const geometry =
                        hasGeometry ? session->tape.FindGeometryAsset(draw.geometry) : nullptr;
                    const std::size_t vertexSize = hasGeometry && geometry
                                                       ? geometry->vertices->size()
                                                       : session->tape.Vertices().size();
                    const std::size_t indexSize = hasGeometry && geometry
                                                      ? geometry->indices->size()
                                                      : session->tape.Indices().size();
                    if (draw.topology == RenderIndexTopology::Points ||
                        (!validatedByApplication && !IsValid(draw.topology)) ||
                        (!validatedByApplication && (!IsValid(draw.pipeline.positionSpace) ||
                                                     !IsValid(draw.pipeline.geometryMode))) ||
                        draw.constantsIndex >= session->tape.Constants().size() ||
                        (!validatedByApplication &&
                         !IsValid(session->tape.Constants()[draw.constantsIndex])) ||
                        (hasGeometry && geometry == nullptr) || draw.vertexOffset > vertexSize ||
                        draw.vertexCount > vertexSize - draw.vertexOffset ||
                        draw.indexOffset > indexSize ||
                        draw.indexCount > indexSize - draw.indexOffset ||
                        (draw.topology == RenderIndexTopology::Lines && draw.indexCount % 2 != 0) ||
                        (draw.topology == RenderIndexTopology::Triangles &&
                         draw.indexCount % 3 != 0))
                    {
                        valid = false;
                        break;
                    }
                    for (std::size_t index = 0; !validatedByApplication && index < draw.vertexCount;
                         ++index)
                    {
                        const RenderTapeVertex &vertex =
                            hasGeometry ? (*geometry->vertices)[draw.vertexOffset + index]
                                        : session->tape.Vertices()[draw.vertexOffset + index];
                        if (!IsValid(vertex))
                        {
                            valid = false;
                            break;
                        }
                    }
                    for (std::size_t index = 0;
                         !validatedByApplication && valid && index < draw.indexCount; ++index)
                    {
                        const std::uint32_t vertexIndex =
                            hasGeometry ? (*geometry->indices)[draw.indexOffset + index]
                                        : session->tape.Indices()[draw.indexOffset + index];
                        if (vertexIndex >= draw.vertexCount)
                        {
                            valid = false;
                        }
                    }
                    const RenderTapeConstants &constants =
                        session->tape.Constants()[draw.constantsIndex];
                    if (!validatedByApplication)
                    {
                        const auto &bmd = constants.bmd;
                        const bool rigid =
                            draw.pipeline.geometryMode == RenderGeometryMode::RigidInstances;
                        if (rigid)
                        {
                            valid =
                                valid && hasGeometry && bmd.enabled && bmd.rigid && !bmd.shadow &&
                                bmd.rigidInstanceCount != 0 &&
                                bmd.rigidInstanceOffset <= session->tape.RigidInstances().size() &&
                                bmd.rigidInstanceCount <=
                                    session->tape.RigidInstances().size() - bmd.rigidInstanceOffset;
                            for (std::size_t index = 0; valid && index < bmd.rigidInstanceCount;
                                 ++index)
                                valid =
                                    IsValid(session->tape
                                                .RigidInstances()[bmd.rigidInstanceOffset + index]);
                        }
                        else if (bmd.rigidInstanceOffset != 0 || bmd.rigidInstanceCount != 0)
                            valid = false;
                        if (!valid)
                            break;
                    }
                    if (!validatedByApplication)
                    {
                        const bool particles =
                            draw.pipeline.geometryMode == RenderGeometryMode::ParticleInstances;
                        const bool terrainInstances =
                            draw.pipeline.geometryMode == RenderGeometryMode::Terrain ||
                            draw.pipeline.geometryMode == RenderGeometryMode::Grass;
                        const bool usesInstances =
                            particles || UsesQuadInstanceStorage(draw.pipeline.geometryMode);
                        const auto available = particles ? session->tape.ParticleInstances().size()
                                                         : session->tape.QuadInstances().size();
                        const auto &run = constants.quad;
                        if (UsesTrailInstanceStorage(draw.pipeline.geometryMode))
                            valid = hasGeometry && session->tape.HasValidExternalTrailDraw(
                                                       draw.pipeline.geometryMode, constants);
                        else if (usesInstances)
                        {
                            valid = hasGeometry && run.runId != 0 && run.instanceCount != 0 &&
                                    run.instanceOffset <= available &&
                                    run.instanceCount <= available - run.instanceOffset;
                            if (particles)
                                valid = valid && IsValid(constants.bmd.rigidTransform);
                            for (std::size_t index = 0; valid && index < run.instanceCount; ++index)
                                valid =
                                    particles
                                        ? IsValid(
                                              session->tape
                                                  .ParticleInstances()[run.instanceOffset + index])
                                        : IsValid(session->tape
                                                      .QuadInstances()[run.instanceOffset + index]);
                        }
                        else if (terrainInstances)
                        {
                            valid = hasGeometry && geometry->terrainInstances != nullptr &&
                                    run.instanceCount != 0 &&
                                    run.instanceOffset <= geometry->terrainInstances->size() &&
                                    run.instanceCount <=
                                        geometry->terrainInstances->size() - run.instanceOffset &&
                                    (draw.pipeline.geometryMode != RenderGeometryMode::Grass ||
                                     run.runId != 0);
                        }
                        else if (run.instanceOffset != 0 || run.instanceCount != 0 ||
                                 run.runId != 0)
                            valid = false;
                        if (!valid)
                            break;
                    }
                    if (!IsRectInside(viewport, constants.viewport) ||
                        (constants.scissorEnable &&
                         !IsRectInside(constants.viewport, constants.scissor)))
                    {
                        valid = false;
                    }
                    if (draw.asset.id.value != 0 || draw.asset.revision != 0)
                    {
                        const LogicalRenderAssetLease *const lease = findLease(draw.asset);
                        valid = IsValid(draw.asset) && lease != nullptr;
                        if (valid)
                        {
                            valid = appendCandidateAsset(draw.asset, lease->bytes->size()) &&
                                    appendCandidateSampler(lease->sampler);
                        }
                    }
                    if (valid)
                    {
                        const PipelineKey key = pipelineFor(draw, constants);
                        valid = MakePipelinePolicy(key).has_value() && appendPipeline(plan, key);
                    }
                    if (!valid)
                    {
                        break;
                    }
                    continue;
                }
                if (entry.index >= session->tape.OwnerRequests().size())
                {
                    valid = false;
                    break;
                }
                const RenderOwnerRequest &request = session->tape.OwnerRequests()[entry.index];
                if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
                {
                    const auto payload = session->tape.PayloadBytes();
                    valid = IsValid(upload->destination) && IsValid(upload->sampler) &&
                            upload->width != 0 && upload->height != 0 &&
                            IsValid(upload->retention) && upload->payloadOffset <= payload.size() &&
                            upload->payloadByteCount <= payload.size() - upload->payloadOffset &&
                            IsExactRgba8ByteCount(upload->width, upload->height,
                                                  upload->payloadByteCount);
                    if (valid)
                    {
                        valid =
                            appendCandidateAsset(upload->destination, upload->payloadByteCount) &&
                            appendCandidateSampler(upload->sampler);
                    }
                }
                else if (const auto *copy =
                             std::get_if<CopyTargetToLogicalTextureRequest>(&request))
                {
                    valid = IsValid(copy->destination) && IsValid(copy->sampler) &&
                            copy->sourceSession == session->tape.Id() &&
                            copy->sourceGeneration == session->tape.Generation() &&
                            copy->sourceSurfaceGeneration == session->tape.SurfaceGeneration() &&
                            IsRectInside(viewport, copy->sourceRect);
                    if (valid)
                    {
                        const auto bytes =
                            Rgba8ByteCount(copy->sourceRect.width, copy->sourceRect.height);
                        valid = bytes.has_value() &&
                                appendCandidateAsset(copy->destination, *bytes) &&
                                appendCandidateSampler(copy->sampler);
                    }
                }
                else if (const auto *download = std::get_if<DownloadTargetRgba8Request>(&request))
                {
                    valid =
                        download->requestId != 0 && download->sourceFrameSequence != 0 &&
                        download->sourceFrameSequence < session->tape.FrameSequence() &&
                        download->sourceSession == session->tape.Id() &&
                        download->sourceGeneration == session->tape.Generation() &&
                        download->sourceSurfaceGeneration == session->tape.SurfaceGeneration() &&
                        IsRectInside(viewport, download->rect);
                    if (valid)
                    {
                        const auto bytes =
                            Rgba8ByteCount(download->rect.width, download->rect.height);
                        const auto next =
                            bytes ? CheckedAdd(plan.completionBytes, *bytes) : std::nullopt;
                        valid = next.has_value();
                        if (valid)
                        {
                            plan.completionBytes = *next;
                        }
                    }
                }
                else
                {
                    valid = false;
                }
                if (!valid)
                {
                    break;
                }
            }
        }
        if (!valid)
        {
            continue;
        }
        plan.uploadBytes = 0;
        for (std::size_t index = 0; index < candidateAssets.size(); ++index)
        {
            const bool live = residentAssets.contains(candidateAssets[index]);
            if (plannedAsset(candidateAssets[index]) || live)
            {
                continue;
            }
            const LogicalRenderAssetLease *const lease = findLease(candidateAssets[index]);
            if (lease != nullptr)
            {
                const auto next = CheckedAdd(plan.uploadBytes, lease->bytes->size());
                if (!next)
                {
                    return false;
                }
                plan.uploadBytes = *next;
            }
        }
        try
        {
            for (const LogicalRenderAssetRef asset : candidateAssets)
            {
                plannedAssets.insert(asset);
            }
            for (const RenderSamplerIntent sampler : candidateSamplers)
            {
                if (!plannedSampler(sampler))
                {
                    plannedSamplers.push_back(sampler);
                }
            }
        }
        catch (...)
        {
            return false;
        }

        const auto nextCompletionBytes = CheckedAdd(completionBytes, plan.completionBytes);
        if (!nextCompletionBytes)
        {
            return false;
        }
        try
        {
            plans.push_back(std::move(plan));
            ++planCount;
        }
        catch (...)
        {
            return false;
        }
        completionBytes = *nextCompletionBytes;
    }
    return true;
}

bool SdlGpuRenderBackend::CreatePersistentGeometryBuffers(const LogicalGeometryAssetLease &lease,
                                                          std::size_t slot, void *&vertices,
                                                          void *&indices,
                                                          void *&terrainCells,
                                                          void *&terrainInstances) noexcept
{
    vertices = nullptr;
    indices = nullptr;
    terrainCells = nullptr;
    terrainInstances = nullptr;
    if (impl_->testMode)
    {
        if (!impl_->nativeTestConfig.textureCreate)
        {
            return false;
        }
        vertices = TestHandle(0x8600 + slot * 4);
        indices = TestHandle(0x8601 + slot * 4);
        terrainCells = lease.terrainCells == nullptr ? nullptr : TestHandle(0x8602 + slot * 4);
        terrainInstances =
            lease.terrainInstances == nullptr ? nullptr : TestHandle(0x8603 + slot * 4);
        return true;
    }

    const SDL_GPUBufferCreateInfo vertexInfo{
        SDL_GPU_BUFFERUSAGE_VERTEX,
        static_cast<Uint32>(lease.vertices->size() * sizeof(RenderTapeVertex)), 0};
    const SDL_GPUBufferCreateInfo indexInfo{
        SDL_GPU_BUFFERUSAGE_INDEX,
        static_cast<Uint32>(lease.indices->size() * sizeof(std::uint32_t)), 0};
    vertices = SDL_CreateGPUBuffer(impl_->device, &vertexInfo);
    if (vertices == nullptr)
    {
        return false;
    }
    indices = SDL_CreateGPUBuffer(impl_->device, &indexInfo);
    if (indices == nullptr)
    {
        SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(vertices));
        vertices = nullptr;
        return false;
    }
    if (lease.terrainCells != nullptr)
    {
        const SDL_GPUBufferCreateInfo terrainInfo{
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            static_cast<Uint32>(lease.terrainCells->size() * sizeof(RenderTapeTerrainCell)), 0};
        terrainCells = SDL_CreateGPUBuffer(impl_->device, &terrainInfo);
        if (terrainCells == nullptr)
        {
            SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(vertices));
            SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(indices));
            vertices = nullptr;
            indices = nullptr;
            return false;
        }
    }
    if (lease.terrainInstances != nullptr)
    {
        const SDL_GPUBufferCreateInfo instanceInfo{
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            static_cast<Uint32>(lease.terrainInstances->size() *
                                sizeof(RenderTapeTerrainInstance)),
            0};
        terrainInstances = SDL_CreateGPUBuffer(impl_->device, &instanceInfo);
        if (terrainInstances == nullptr)
        {
            SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(vertices));
            SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(indices));
            if (terrainCells != nullptr)
                SDL_ReleaseGPUBuffer(impl_->device,
                                     static_cast<SDL_GPUBuffer *>(terrainCells));
            vertices = indices = terrainCells = nullptr;
            return false;
        }
    }
    return true;
}

bool SdlGpuRenderBackend::StageTerrainLights(FrameReservation &reservation) noexcept
{
    impl_->ledger.terrainLightUploadBytes = 0;
    for (std::size_t index = 0; index < reservation.geometryCount; ++index)
    {
        auto &pending = reservation.geometries[index];
        const auto &light = pending.lease->terrainLight;
        if (!light)
            continue;
        const auto &record = impl_->geometries[pending.slot];
        const std::size_t bytes = light->values.size() * sizeof(float);
        if (pending.reuse && record.lightBuffer && record.lightBytes == bytes)
        {
            pending.lightBuffer = record.lightBuffer;
            pending.lightTransfer = record.lightTransfer;
            pending.uploadLight = record.lightRevision != light->revision;
            if (pending.uploadLight)
                impl_->ledger.terrainLightUploadBytes += bytes;
            continue;
        }
        if (bytes > (std::numeric_limits<Uint32>::max)())
            return false;
        pending.reuseLight = false;
        pending.uploadLight = true;
        impl_->ledger.terrainLightUploadBytes += bytes;
        if (impl_->testMode)
        {
            pending.lightBuffer = static_cast<SDL_GPUBuffer *>(TestHandle(0x8700 + index));
            pending.lightTransfer =
                static_cast<SDL_GPUTransferBuffer *>(TestHandle(0x8800 + index));
            continue;
        }
        const SDL_GPUBufferCreateInfo bufferInfo{SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                                                 static_cast<Uint32>(bytes), 0};
        pending.lightBuffer = SDL_CreateGPUBuffer(impl_->device, &bufferInfo);
        const SDL_GPUTransferBufferCreateInfo transferInfo{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                           static_cast<Uint32>(bytes), 0};
        pending.lightTransfer = SDL_CreateGPUTransferBuffer(impl_->device, &transferInfo);
        if (!pending.lightBuffer || !pending.lightTransfer)
            return false;
    }
    return true;
}

bool SdlGpuRenderBackend::UploadTerrainLights(FrameReservation &reservation) noexcept
{
    for (std::size_t index = 0; index < reservation.geometryCount; ++index)
    {
        const auto &pending = reservation.geometries[index];
        if (!pending.uploadLight)
            continue;
        const auto &values = pending.lease->terrainLight->values;
        void *mapped = SDL_MapGPUTransferBuffer(impl_->device, pending.lightTransfer, true);
        if (!mapped)
            return false;
        std::memcpy(mapped, values.data(), values.size() * sizeof(float));
        SDL_UnmapGPUTransferBuffer(impl_->device, pending.lightTransfer);
        auto *pass = SDL_BeginGPUCopyPass(reservation.commandBuffer);
        if (!pass)
            return false;
        const SDL_GPUTransferBufferLocation source{pending.lightTransfer, 0};
        const SDL_GPUBufferRegion destination{pending.lightBuffer, 0,
                                              static_cast<Uint32>(values.size() * sizeof(float))};
        SDL_UploadToGPUBuffer(pass, &source, &destination, true);
        SDL_EndGPUCopyPass(pass);
    }
    return true;
}

bool SdlGpuRenderBackend::StagePersistentGeometry(const ApplicationRenderFrame &frame,
                                                  const std::vector<CandidatePlan> &plans,
                                                  std::size_t planCount,
                                                  FrameReservation &reservation) noexcept
{
    constexpr std::size_t UnassignedSlot = (std::numeric_limits<std::size_t>::max)();
    const std::uint64_t reservationStamp = impl_->nextGeometryReservationStamp++;

    // Protect every resident mesh before missing meshes recycle cache slots.
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        for (const LogicalGeometryAssetLease &lease : session->tape.GeometryAssets())
        {
            const auto resident = impl_->geometrySlots.find(lease.asset);
            if (resident != impl_->geometrySlots.end())
            {
                Impl::GeometryRecord &record = impl_->geometries[resident->second];
                record.reservationStamp = reservationStamp;
                reservation.geometries[reservation.geometryCount++] = {
                    resident->second,
                    lease.asset,
                    &lease,
                    record.vertices,
                    record.indices,
                    record.terrainCells,
                    record.terrainInstances,
                    static_cast<std::uint32_t>(lease.vertices->size()),
                    static_cast<std::uint32_t>(lease.indices->size()),
                    0,
                    0,
                    0,
                    0,
                    true,
                    false};
                continue;
            }
            reservation.geometries[reservation.geometryCount++] = {
                UnassignedSlot,
                lease.asset,
                &lease,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                static_cast<std::uint32_t>(lease.vertices->size()),
                static_cast<std::uint32_t>(lease.indices->size()),
                0,
                0,
                0,
                0,
                true,
                false};
        }
    }

    std::size_t recycleCursor = 0;
    for (std::size_t pendingIndex = 0; pendingIndex < reservation.geometryCount; ++pendingIndex)
    {
        auto &pending = reservation.geometries[pendingIndex];
        if (pending.slot != UnassignedSlot)
        {
            continue;
        }
        while (recycleCursor < impl_->geometries.size())
        {
            const Impl::GeometryRecord &record = impl_->geometries[recycleCursor];
            if (record.reservationStamp != reservationStamp &&
                (!record.live || (record.asset.sessionId == pending.asset.sessionId &&
                                  record.asset.generation == pending.asset.generation)))
            {
                break;
            }
            ++recycleCursor;
        }
        const std::size_t slot = recycleCursor;
        if (slot == impl_->geometries.size())
        {
            try
            {
                impl_->geometries.emplace_back();
            }
            catch (...)
            {
                return false;
            }
        }
        else
        {
            ++recycleCursor;
        }
        impl_->geometries[slot].reservationStamp = reservationStamp;
        void *vertices = nullptr;
        void *indices = nullptr;
        void *terrainCells = nullptr;
        void *terrainInstances = nullptr;
        const bool recycled =
            slot < impl_->geometries.size() &&
            CanRecycleGeometryBuffers(*impl_, impl_->geometries[slot], *pending.lease);
        if (recycled)
        {
            const Impl::GeometryRecord &record = impl_->geometries[slot];
            vertices = record.vertices;
            indices = record.indices;
            terrainCells = record.terrainCells;
            terrainInstances = record.terrainInstances;
        }
        else if (!CreatePersistentGeometryBuffers(*pending.lease, slot, vertices, indices,
                                                  terrainCells, terrainInstances))
        {
            return false;
        }
        try
        {
            impl_->geometrySlots.emplace(pending.asset, slot);
        }
        catch (...)
        {
            if (!recycled && !impl_->testMode)
            {
                SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(vertices));
                SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(indices));
                if (terrainCells != nullptr)
                {
                    SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(terrainCells));
                }
                if (terrainInstances != nullptr)
                    SDL_ReleaseGPUBuffer(
                        impl_->device, static_cast<SDL_GPUBuffer *>(terrainInstances));
            }
            return false;
        }
        pending.slot = slot;
        pending.vertices = vertices;
        pending.indices = indices;
        pending.terrainCells = terrainCells;
        pending.terrainInstances = terrainInstances;
        pending.reuse = false;
        pending.recycled = recycled;
    }
    return StageTerrainLights(reservation);
}

bool SdlGpuRenderBackend::WritePoseAndInstanceUploads(const ApplicationRenderFrame &frame,
                                                      std::span<const CandidatePlan> plans,
                                                      std::byte *mapped) noexcept
{
    std::size_t paletteCursor = impl_->ledger.geometryUploadBytes;
    std::size_t instanceCursor = paletteCursor + impl_->ledger.skinningUploadBytes;
    const auto copy = [mapped](auto values, std::size_t &cursor) {
        if (values.empty())
            return;
        std::memcpy(mapped + cursor, values.data(), values.size_bytes());
        cursor += values.size_bytes();
    };
    for (const auto &plan : plans)
    {
        const auto &tape = frame.Session(plan.sessionIndex)->tape;
        copy(tape.BoneMatrices(), paletteCursor);
        copy(tape.QuadInstances(), instanceCursor);
        copy(tape.RigidInstances(), instanceCursor);
        copy(tape.ParticleInstances(), instanceCursor);
        copy(tape.TrailSamples(), instanceCursor);
        copy(tape.TrailInstances(), instanceCursor);
    }
    return paletteCursor == impl_->ledger.geometryUploadBytes + impl_->ledger.skinningUploadBytes &&
           instanceCursor == paletteCursor + impl_->ledger.drawInstanceUploadBytes;
}

static Uint32 QuadShaderMode(RenderGeometryMode mode) noexcept
{
    // Values match the compact-geometry branches in RenderTape.vert.hlsl.
    constexpr Uint32 QuadMode = 2, SpriteMode = 3;
    return mode == RenderGeometryMode::SpriteInstances ? SpriteMode : QuadMode;
}

bool SdlGpuRenderBackend::EncodeFrame(const ApplicationRenderFrame &frame,
                                      const std::vector<CandidatePlan> &plans,
                                      std::size_t planCount, FrameReservation &reservation) noexcept
{
    if (impl_ == nullptr || reservation.commandBuffer == nullptr ||
        (impl_->testMode && !impl_->nativeTestConfig.encode))
    {
        return false;
    }
    ++reservation.assetFrameStamp;
    if (reservation.assetFrameStamp == 0)
    {
        for (auto &slot : reservation.assetFrameSlots)
        {
            slot = {};
        }
        reservation.assetFrameStamp = 1;
    }
    const std::uint64_t assetFrameStamp = reservation.assetFrameStamp;
    const auto prepareAssetSlot =
        [&](LogicalRenderAssetRef asset) noexcept -> FrameReservation::AssetFrameSlot * {
        if (asset.id.value >= (std::numeric_limits<std::size_t>::max)())
        {
            return nullptr;
        }
        const std::size_t index = static_cast<std::size_t>(asset.id.value);
        try
        {
            if (index >= reservation.assetFrameSlots.size())
            {
                reservation.assetFrameSlots.resize(index + 1);
            }
            if (index >= impl_->textureSlotsByAssetId.size())
            {
                impl_->textureSlotsByAssetId.resize(index + 1,
                                                    (std::numeric_limits<std::size_t>::max)());
            }
        }
        catch (...)
        {
            return nullptr;
        }
        return &reservation.assetFrameSlots[index];
    };
    for (const LogicalRenderAssetLease &lease : frame.Assets())
    {
        auto *const slot = prepareAssetSlot(lease.asset);
        if (slot == nullptr)
        {
            return false;
        }
        slot->lease = &lease;
        slot->leaseStamp = assetFrameStamp;
    }
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        for (const LogicalRenderAssetRef asset : session->tape.LogicalAssets())
        {
            auto *const slot = prepareAssetSlot(asset);
            if (slot == nullptr)
            {
                return false;
            }
            slot->usedStamp = assetFrameStamp;
        }
        for (const RenderOwnerRequest &request : session->tape.OwnerRequests())
        {
            LogicalRenderAssetRef destination;
            bool frameOnly = false;
            if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
            {
                destination = upload->destination;
                frameOnly = upload->retention == RenderAssetRetention::FrameOnly;
            }
            else if (const auto *copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request))
            {
                destination = copy->destination;
            }
            else
            {
                continue;
            }
            auto *const slot = prepareAssetSlot(destination);
            if (slot == nullptr)
            {
                return false;
            }
            slot->usedStamp = assetFrameStamp;
            if (frameOnly)
            {
                slot->frameOnlyStamp = assetFrameStamp;
            }
        }
    }
    const auto assetUsed = [&](LogicalRenderAssetRef asset) noexcept {
        return asset.id.value < reservation.assetFrameSlots.size() &&
               reservation.assetFrameSlots[asset.id.value].usedStamp == assetFrameStamp;
    };
    const auto assetFrameOnly = [&](LogicalRenderAssetRef asset) noexcept {
        return asset.id.value < reservation.assetFrameSlots.size() &&
               reservation.assetFrameSlots[asset.id.value].frameOnlyStamp == assetFrameStamp;
    };
    const auto findAssetLease =
        [&](LogicalRenderAssetRef asset) noexcept -> const LogicalRenderAssetLease * {
        if (asset.id.value >= reservation.assetFrameSlots.size())
        {
            return nullptr;
        }
        const auto &slot = reservation.assetFrameSlots[asset.id.value];
        return slot.leaseStamp == assetFrameStamp && slot.lease != nullptr &&
                       slot.lease->asset == asset
                   ? slot.lease
                   : nullptr;
    };
    std::size_t pipelineSlots = 3;
    std::size_t textureSlots = frame.Assets().size();
    std::size_t operationSlots = planCount;
    std::size_t requestCompletionSlots = 0;
    std::size_t geometrySlots = 0;
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        const auto commandPipelines =
            CheckedAdd(session->tape.Draws().size(), session->tape.Clears().size());
        const auto nextPipelines =
            commandPipelines ? CheckedAdd(pipelineSlots, *commandPipelines) : std::nullopt;
        const auto nextTextures = CheckedAdd(textureSlots, session->tape.OwnerRequests().size());
        const auto entryOperations =
            CheckedMultiply(session->tape.Entries().size(), std::size_t{2});
        const auto nextOperations =
            entryOperations ? CheckedAdd(operationSlots, *entryOperations) : std::nullopt;
        const auto nextRequestCompletions =
            CheckedAdd(requestCompletionSlots, session->tape.OwnerRequests().size());
        const auto nextGeometries =
            CheckedAdd(geometrySlots, session->tape.GeometryAssets().size());
        if (!nextPipelines || !nextTextures || !nextOperations || !nextRequestCompletions ||
            !nextGeometries)
        {
            return false;
        }
        pipelineSlots = *nextPipelines;
        textureSlots = *nextTextures;
        operationSlots = *nextOperations;
        requestCompletionSlots = *nextRequestCompletions;
        geometrySlots = *nextGeometries;
    }
    const auto samplerSlots = CheckedAdd(textureSlots, std::size_t{1});
    if (!samplerSlots)
    {
        return false;
    }
    const auto appendResourceSlot = [](auto &records) noexcept -> std::optional<std::size_t> {
        try
        {
            const std::size_t slot = records.size();
            records.emplace_back();
            return slot;
        }
        catch (...)
        {
            return std::nullopt;
        }
    };
    try
    {
        reservation.targets.resize(planCount);
        reservation.textures.resize(textureSlots);
        reservation.samplers.resize(*samplerSlots);
        reservation.pipelines.resize(pipelineSlots);
        reservation.textureEvictions.resize(textureSlots);
        reservation.operationKinds.resize(operationSlots);
        reservation.sessionCompletions.resize(planCount);
        reservation.requestCompletions.resize(requestCompletionSlots);
        reservation.geometries.resize(geometrySlots);
        impl_->geometrySlots.reserve(impl_->geometrySlots.size() + geometrySlots);
    }
    catch (...)
    {
        return false;
    }
    auto &textureSlotsInUse = reservation.textureSlotsInUse;
    try
    {
        textureSlotsInUse.assign(impl_->textures.size(), 0);
    }
    catch (...)
    {
        return false;
    }
    const auto addPendingTexture = [&](LogicalRenderAssetRef asset, std::size_t index) noexcept {
        auto *const slot = prepareAssetSlot(asset);
        if (slot == nullptr || slot->pendingStamp == assetFrameStamp)
        {
            return false;
        }
        slot->pendingStamp = assetFrameStamp;
        slot->pendingIndex = index;
        return true;
    };
    const auto pendingTextureIndex =
        [&](LogicalRenderAssetRef asset) noexcept -> std::optional<std::size_t> {
        if (asset.id.value >= reservation.assetFrameSlots.size())
        {
            return std::nullopt;
        }
        const auto &slot = reservation.assetFrameSlots[asset.id.value];
        return slot.pendingStamp == assetFrameStamp ? std::optional<std::size_t>{slot.pendingIndex}
                                                    : std::nullopt;
    };
    reservation.plannedResidentBytes = impl_->residentTextureBytes;
    const auto targetSlotUsed = [&](std::size_t slot) noexcept {
        for (std::size_t index = 0; index < reservation.targetCount; ++index)
        {
            if (reservation.targets[index].slot == slot)
            {
                return true;
            }
        }
        return false;
    };
    const auto pipelineSlotUsed = [&](std::size_t slot) noexcept {
        for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
        {
            if (reservation.pipelines[index].slot == slot)
            {
                return true;
            }
        }
        return false;
    };
    const auto samplerSlotUsed = [&](std::size_t slot) noexcept {
        for (std::size_t index = 0; index < reservation.samplerCount; ++index)
        {
            if (reservation.samplers[index].slot == slot)
            {
                return true;
            }
        }
        return false;
    };
    const auto textureSlotUsed = [&](std::size_t slot) noexcept {
        return slot < textureSlotsInUse.size() && textureSlotsInUse[slot] != 0;
    };
    const auto residentTextureSlot =
        [&](LogicalRenderAssetRef asset) noexcept -> std::optional<std::size_t> {
        if (asset.id.value >= impl_->textureSlotsByAssetId.size())
        {
            return std::nullopt;
        }
        const std::size_t slot = impl_->textureSlotsByAssetId[asset.id.value];
        if (slot >= impl_->textures.size() || textureSlotUsed(slot))
        {
            return std::nullopt;
        }
        const auto &record = impl_->textures[slot];
        if (!record.live || record.key.deviceGeneration != impl_->deviceGeneration ||
            record.key.asset != asset)
        {
            return std::nullopt;
        }
        return slot;
    };
    const auto protectedAsset = [&](LogicalRenderAssetRef asset) noexcept {
        return assetUsed(asset);
    };
    const auto createTarget = [&](std::uint32_t width, std::uint32_t height, void *&color,
                                  void *&depth) noexcept {
        color = nullptr;
        depth = nullptr;
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.targetColorCreate ||
                !impl_->nativeTestConfig.targetDepthCreate)
            {
                return false;
            }
            color = TestHandle(0x8000 + reservation.targetCount * 2);
            depth = TestHandle(0x8001 + reservation.targetCount * 2);
            return true;
        }
        const SDL_GPUTextureCreateInfo colorInfo{SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                                     SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                 width,
                                                 height,
                                                 1,
                                                 1,
                                                 SDL_GPU_SAMPLECOUNT_1,
                                                 0};
        const SDL_GPUTextureCreateInfo depthInfo{SDL_GPU_TEXTURETYPE_2D,
                                                 impl_->depthFormat,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                                 width,
                                                 height,
                                                 1,
                                                 1,
                                                 SDL_GPU_SAMPLECOUNT_1,
                                                 0};
        color = SDL_CreateGPUTexture(impl_->device, &colorInfo);
        if (color == nullptr)
        {
            return false;
        }
        depth = SDL_CreateGPUTexture(impl_->device, &depthInfo);
        if (depth == nullptr)
        {
            SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(color));
            color = nullptr;
            return false;
        }
        return true;
    };
    const auto createSampler = [&](RenderSamplerIntent intent, std::size_t slot,
                                   void *&native) noexcept {
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.textureCreate)
            {
                return false;
            }
            native = TestHandle(0x8100 + slot);
            return true;
        }
        const SamplerPolicy policy = *MakeSamplerPolicy(intent);
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = static_cast<SDL_GPUFilter>(policy.minFilter);
        info.mag_filter = static_cast<SDL_GPUFilter>(policy.magFilter);
        info.mipmap_mode = static_cast<SDL_GPUSamplerMipmapMode>(policy.mipmapMode);
        info.address_mode_u = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeU);
        info.address_mode_v = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeV);
        info.address_mode_w = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeW);
        info.mip_lod_bias = policy.mipLodBias;
        info.max_anisotropy = policy.maxAnisotropy;
        info.compare_op = static_cast<SDL_GPUCompareOp>(policy.compareOp);
        info.min_lod = policy.minLod;
        info.max_lod = policy.maxLod;
        info.enable_anisotropy = policy.enableAnisotropy;
        info.enable_compare = policy.enableCompare;
        info.props = static_cast<SDL_PropertiesID>(policy.props);
        native = SDL_CreateGPUSampler(impl_->device, &info);
        return native != nullptr;
    };
    const auto createTexture = [&](const LogicalRenderAssetLease &lease, std::size_t slot,
                                   void *&native) noexcept {
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.textureCreate)
            {
                return false;
            }
            native = TestHandle(0x8200 + slot);
            return true;
        }
        const SDL_GPUTextureCreateInfo info{SDL_GPU_TEXTURETYPE_2D,
                                            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                            SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                            lease.width,
                                            lease.height,
                                            1,
                                            1,
                                            SDL_GPU_SAMPLECOUNT_1,
                                            0};
        native = SDL_CreateGPUTexture(impl_->device, &info);
        return native != nullptr;
    };
    const auto createTextureShape = [&](std::uint32_t width, std::uint32_t height, std::size_t slot,
                                        void *&native) noexcept {
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.textureCreate)
            {
                return false;
            }
            native = TestHandle(0x8200 + slot);
            return true;
        }
        const SDL_GPUTextureCreateInfo info{SDL_GPU_TEXTURETYPE_2D,
                                            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                            SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                            width,
                                            height,
                                            1,
                                            1,
                                            SDL_GPU_SAMPLECOUNT_1,
                                            0};
        native = SDL_CreateGPUTexture(impl_->device, &info);
        return native != nullptr;
    };

    const auto stageTarget = [&](const CandidatePlan &plan) noexcept {
        for (std::size_t index = 0; index < reservation.targetCount; ++index)
        {
            const auto &target = reservation.targets[index];
            if (target.sessionId == plan.sessionId &&
                target.sessionGeneration == plan.sessionGeneration &&
                target.surfaceGeneration == plan.surfaceGeneration && target.width == plan.width &&
                target.height == plan.height)
            {
                return true;
            }
        }
        for (std::size_t index = 0; index < impl_->sessionTargets.size(); ++index)
        {
            const auto &existing = impl_->sessionTargets[index];
            if (!targetSlotUsed(index) && existing.live && existing.key.id == plan.sessionId &&
                existing.key.generation == plan.sessionGeneration &&
                existing.key.surfaceGeneration == plan.surfaceGeneration &&
                existing.key.width == plan.width && existing.key.height == plan.height &&
                existing.key.deviceGeneration == impl_->deviceGeneration)
            {
                if (reservation.targetCount >= reservation.targets.size())
                {
                    return false;
                }
                reservation.targets[reservation.targetCount++] = {
                    index,      plan.sessionId, plan.sessionGeneration, plan.surfaceGeneration,
                    plan.width, plan.height,    existing.color,         existing.depth,
                    true};
                return true;
            }
        }
        std::size_t selected = impl_->sessionTargets.size();
        for (std::size_t index = 0; index < impl_->sessionTargets.size(); ++index)
        {
            if (!targetSlotUsed(index) && !impl_->sessionTargets[index].live)
            {
                selected = index;
                break;
            }
        }
        if (reservation.targetCount >= reservation.targets.size())
        {
            return false;
        }
        if (selected == impl_->sessionTargets.size())
        {
            const auto added = appendResourceSlot(impl_->sessionTargets);
            if (!added)
            {
                return false;
            }
            selected = *added;
        }
        void *color = nullptr;
        void *depth = nullptr;
        if (!createTarget(plan.width, plan.height, color, depth))
        {
            return false;
        }
        reservation.targets[reservation.targetCount++] = {selected,
                                                          plan.sessionId,
                                                          plan.sessionGeneration,
                                                          plan.surfaceGeneration,
                                                          plan.width,
                                                          plan.height,
                                                          color,
                                                          depth,
                                                          false};
        return true;
    };

    for (std::size_t index = 0; index < planCount; ++index)
    {
        if (!stageTarget(plans[index]))
        {
            return false;
        }
    }

    if (!StagePersistentGeometry(frame, plans, planCount, reservation))
    {
        return false;
    }

    const auto stagePipeline = [&](const PipelineKey &key) noexcept {
        const auto policy = MakePipelinePolicy(key);
        if (!policy)
        {
            return false;
        }
        for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
        {
            if (reservation.pipelines[index].key == key)
            {
                return true;
            }
        }
        for (std::size_t index = 0; index < impl_->pipelines.size(); ++index)
        {
            if (!pipelineSlotUsed(index) && impl_->pipelines[index].live &&
                impl_->pipelines[index].key == key)
            {
                reservation.pipelines[reservation.pipelineCount++] = {
                    index, key, *policy, impl_->pipelines[index].native, true};
                return true;
            }
        }
        std::size_t selected = impl_->pipelines.size();
        for (std::size_t index = 0; index < impl_->pipelines.size(); ++index)
        {
            if (!pipelineSlotUsed(index) && !impl_->pipelines[index].live)
            {
                selected = index;
                break;
            }
        }
        if (reservation.pipelineCount >= reservation.pipelines.size() ||
            !impl_->nativeTestConfig.pipelineCreate)
        {
            return false;
        }
        if (selected == impl_->pipelines.size())
        {
            const auto added = appendResourceSlot(impl_->pipelines);
            if (!added)
            {
                return false;
            }
            selected = *added;
        }
        void *native = nullptr;
        const PipelinePolicy nativePolicy = *policy;
        if (impl_->testMode)
        {
            native = TestHandle(0x8300 + selected);
        }
        else if (!CreatePipelineNative(key, nativePolicy, native))
        {
            return false;
        }
        reservation.pipelines[reservation.pipelineCount++] = {selected, key, nativePolicy, native,
                                                              false};
        return true;
    };
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        for (std::size_t pipeline = 0; pipeline < plans[planIndex].pipelineCount; ++pipeline)
        {
            if (!stagePipeline(plans[planIndex].pipelines[pipeline]))
            {
                return false;
            }
        }
    }
    if (!stagePipeline(CompositionPipelineKeyForPeer()))
    {
        return false;
    }
    PipelineKey overlayPipeline = OverlayPipelineKeyForPeer();
    if (impl_ != nullptr)
    {
        overlayPipeline.colorFormat = static_cast<std::uint32_t>(impl_->swapchainFormat);
    }
    if (!stagePipeline(overlayPipeline))
    {
        return false;
    }
    PipelineKey labelPipeline = overlayPipeline;
    labelPipeline.shaderFamily = ShaderFamily::Label;
    if (!stagePipeline(labelPipeline))
    {
        return false;
    }

    const auto assetUsedByPlans = [&](LogicalRenderAssetRef asset) noexcept {
        return assetUsed(asset);
    };

    const auto isFrameOnlyAsset = [&](LogicalRenderAssetRef asset) noexcept {
        return assetFrameOnly(asset);
    };

    std::size_t nextTextureSlot = 0;
    for (const LogicalRenderAssetLease &lease : frame.Assets())
    {
        if (!assetUsedByPlans(lease.asset))
        {
            continue;
        }
        std::size_t samplerSlot = impl_->samplers.size();
        for (std::size_t index = 0; index < reservation.samplerCount; ++index)
        {
            if (reservation.samplers[index].key == lease.sampler)
            {
                samplerSlot = reservation.samplers[index].slot;
                break;
            }
        }
        if (samplerSlot == impl_->samplers.size())
        {
            for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
            {
                if (!samplerSlotUsed(index) && impl_->samplers[index].live &&
                    impl_->samplers[index].key == lease.sampler)
                {
                    samplerSlot = index;
                    break;
                }
            }
        }
        if (samplerSlot == impl_->samplers.size())
        {
            for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
            {
                if (!samplerSlotUsed(index) && !impl_->samplers[index].live)
                {
                    samplerSlot = index;
                    break;
                }
            }
            if (reservation.samplerCount >= reservation.samplers.size())
            {
                return false;
            }
            if (samplerSlot == impl_->samplers.size())
            {
                const auto added = appendResourceSlot(impl_->samplers);
                if (!added)
                {
                    return false;
                }
                samplerSlot = *added;
            }
            void *native = nullptr;
            if (!createSampler(lease.sampler, samplerSlot, native))
            {
                return false;
            }
            reservation.samplers[reservation.samplerCount++] = {
                samplerSlot, lease.sampler, impl_->deviceGeneration, native, false};
        }
        if (pendingTextureIndex(lease.asset))
        {
            continue;
        }
        const auto resident = residentTextureSlot(lease.asset);
        if (resident)
        {
            continue;
        }
        const bool frameOnly = isFrameOnlyAsset(lease.asset);
        std::size_t selected = impl_->textures.size();
        while (nextTextureSlot < impl_->textures.size())
        {
            const std::size_t index = nextTextureSlot++;
            const auto &candidate = impl_->textures[index];
            if (textureSlotUsed(index) || (candidate.live && protectedAsset(candidate.key.asset)))
            {
                continue;
            }
            if (!candidate.live ||
                (frameOnly && candidate.frameOnly && IsFenceFree(*impl_, candidate.lastFence)))
            {
                selected = index;
                break;
            }
        }
        if (reservation.textureCount >= reservation.textures.size())
        {
            return false;
        }
        if (selected == impl_->textures.size())
        {
            const auto added = appendResourceSlot(impl_->textures);
            if (!added)
            {
                return false;
            }
            selected = *added;
            try
            {
                textureSlotsInUse.push_back(0);
            }
            catch (...)
            {
                return false;
            }
        }
        const auto &selectedRecord = impl_->textures[selected];
        const bool recycled = frameOnly && selectedRecord.live && selectedRecord.frameOnly &&
                              selectedRecord.width == lease.width &&
                              selectedRecord.height == lease.height &&
                              IsFenceFree(*impl_, selectedRecord.lastFence);
        void *native = recycled ? selectedRecord.native : nullptr;
        if (!recycled && !createTexture(lease, selected, native))
        {
            return false;
        }
        if (selectedRecord.live && !recycled)
        {
            reservation.textureEvictions[reservation.textureEvictionCount++] = selected;
        }
        textureSlotsInUse[selected] = 1;
        if (!addPendingTexture(lease.asset, reservation.textureCount))
        {
            return false;
        }
        reservation.textures[reservation.textureCount++] = {
            selected,    lease.asset,  impl_->deviceGeneration,
            lease.width, lease.height, lease.bytes->size(),
            native,      false,        0,
            0,           frameOnly,    recycled};
    }

    const auto stageRequestSampler = [&](RenderSamplerIntent intent) noexcept {
        for (std::size_t index = 0; index < reservation.samplerCount; ++index)
        {
            if (reservation.samplers[index].key == intent)
            {
                return true;
            }
        }
        for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
        {
            if (!samplerSlotUsed(index) && impl_->samplers[index].live &&
                impl_->samplers[index].key == intent)
            {
                if (reservation.samplerCount >= reservation.samplers.size())
                {
                    return false;
                }
                reservation.samplers[reservation.samplerCount++] = {
                    index, intent, impl_->deviceGeneration, impl_->samplers[index].native, true};
                return true;
            }
        }
        std::size_t selected = impl_->samplers.size();
        for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
        {
            if (!samplerSlotUsed(index) && !impl_->samplers[index].live)
            {
                selected = index;
                break;
            }
        }
        if (reservation.samplerCount >= reservation.samplers.size())
        {
            return false;
        }
        if (selected == impl_->samplers.size())
        {
            const auto added = appendResourceSlot(impl_->samplers);
            if (!added)
            {
                return false;
            }
            selected = *added;
        }
        void *native = nullptr;
        if (!createSampler(intent, selected, native))
        {
            return false;
        }
        reservation.samplers[reservation.samplerCount++] = {selected, intent,
                                                            impl_->deviceGeneration, native, false};
        return true;
    };
    if (!stageRequestSampler({LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge}))
    {
        return false;
    }

    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        for (const RenderOwnerRequest &request : session->tape.OwnerRequests())
        {
            const auto *copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request);
            if (copy == nullptr)
            {
                continue;
            }
            if (!stageRequestSampler(copy->sampler))
            {
                return false;
            }
            if (pendingTextureIndex(copy->destination))
            {
                continue;
            }
            const auto resident = residentTextureSlot(copy->destination);
            if (resident)
            {
                if (reservation.textureCount >= reservation.textures.size())
                {
                    return false;
                }
                const std::size_t recordIndex = *resident;
                const auto &existing = impl_->textures[recordIndex];
                textureSlotsInUse[recordIndex] = 1;
                if (!addPendingTexture(copy->destination, reservation.textureCount))
                {
                    return false;
                }
                reservation.textures[reservation.textureCount++] = {recordIndex,
                                                                    copy->destination,
                                                                    impl_->deviceGeneration,
                                                                    copy->sourceRect.width,
                                                                    copy->sourceRect.height,
                                                                    existing.residentBytes,
                                                                    existing.native,
                                                                    true,
                                                                    0,
                                                                    0};
                continue;
            }
            std::size_t selected = impl_->textures.size();
            while (nextTextureSlot < impl_->textures.size())
            {
                const std::size_t index = nextTextureSlot++;
                const auto &candidate = impl_->textures[index];
                if (!textureSlotUsed(index) && !candidate.live)
                {
                    selected = index;
                    break;
                }
            }
            const auto bytes = Rgba8ByteCount(copy->sourceRect.width, copy->sourceRect.height);
            if (!bytes || reservation.textureCount >= reservation.textures.size())
            {
                return false;
            }
            if (selected == impl_->textures.size())
            {
                const auto added = appendResourceSlot(impl_->textures);
                if (!added)
                {
                    return false;
                }
                selected = *added;
                try
                {
                    textureSlotsInUse.push_back(0);
                }
                catch (...)
                {
                    return false;
                }
            }
            void *native = nullptr;
            if (!createTextureShape(copy->sourceRect.width, copy->sourceRect.height, selected,
                                    native))
            {
                return false;
            }
            if (impl_->textures[selected].live)
            {
                if (reservation.textureEvictionCount >= reservation.textureEvictions.size())
                {
                    return false;
                }
                reservation.textureEvictions[reservation.textureEvictionCount++] = selected;
            }
            textureSlotsInUse[selected] = 1;
            if (!addPendingTexture(copy->destination, reservation.textureCount))
            {
                return false;
            }
            reservation.textures[reservation.textureCount++] = {selected,
                                                                copy->destination,
                                                                impl_->deviceGeneration,
                                                                copy->sourceRect.width,
                                                                copy->sourceRect.height,
                                                                *bytes,
                                                                native,
                                                                false,
                                                                0,
                                                                0};
        }
    }

    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        bool firstClear = true;
        for (const RenderTapeEntry &entry : session->tape.Entries())
        {
            ReplayOperationKind kind = ReplayOperationKind::Draw;
            if (entry.kind == RenderTapeEntryKind::Clear)
            {
                kind = ReplayOperationKind::Clear;
            }
            else if (entry.kind == RenderTapeEntryKind::OwnerRequest)
            {
                const RenderOwnerRequest &request = session->tape.OwnerRequests()[entry.index];
                if (std::holds_alternative<UploadLogicalAssetRgba8Request>(request))
                {
                    kind = ReplayOperationKind::Upload;
                }
                else if (std::holds_alternative<CopyTargetToLogicalTextureRequest>(request))
                {
                    kind = ReplayOperationKind::Copy;
                }
                else
                {
                    kind = ReplayOperationKind::Download;
                }
            }
            if (reservation.operationCount >= reservation.operationKinds.size())
            {
                return false;
            }
            reservation.operationKinds[reservation.operationCount++] =
                static_cast<std::uint8_t>(kind);
            if (entry.kind == RenderTapeEntryKind::Clear)
            {
                const RenderTapeClear &clear = session->tape.Clears()[entry.index];
                const std::uint8_t encoding = ClearEncodingAtPosition(
                    clear, {0, 0, plans[planIndex].width, plans[planIndex].height}, firstClear);
                if ((encoding & 0xF0u) != 0)
                {
                    if (reservation.operationCount >= reservation.operationKinds.size())
                    {
                        return false;
                    }
                    reservation.operationKinds[reservation.operationCount++] =
                        static_cast<std::uint8_t>(ReplayOperationKind::ClearDraw);
                }
                firstClear = false;
            }
        }
    }

    const auto findLease =
        [&](LogicalRenderAssetRef asset) noexcept -> const LogicalRenderAssetLease * {
        return findAssetLease(asset);
    };
    const auto geometryAndSkinning =
        CheckedAdd(impl_->ledger.geometryUploadBytes, impl_->ledger.skinningUploadBytes);
    const auto ownerUploadBase =
        geometryAndSkinning
            ? CheckedAdd(*geometryAndSkinning, impl_->ledger.drawInstanceUploadBytes)
            : std::nullopt;
    if (!ownerUploadBase)
    {
        return false;
    }
    const auto ownerUploadLimit = CheckedAdd(*ownerUploadBase, impl_->ledger.ownerUploadBytes);
    if (!ownerUploadLimit)
    {
        return false;
    }
    std::size_t uploadCursor = *ownerUploadBase;
    for (std::size_t index = 0; index < reservation.textureCount; ++index)
    {
        auto &pending = reservation.textures[index];
        if (pending.reuse || pending.frameOnly)
        {
            continue;
        }
        const LogicalRenderAssetLease *const lease = findLease(pending.asset);
        if (lease == nullptr)
        {
            continue;
        }
        const auto end = CheckedAdd(uploadCursor, lease->bytes->size());
        const auto uploadLimit = ownerUploadLimit;
        if (!end || !uploadLimit || *end > *uploadLimit ||
            *end > (std::numeric_limits<Uint32>::max)())
        {
            return false;
        }
        pending.uploadOffset = static_cast<std::uint32_t>(uploadCursor);
        pending.uploadBytes = static_cast<std::uint32_t>(lease->bytes->size());
        reservation.uploadBytes += lease->bytes->size();
        if (impl_->testMode)
        {
            if (reservation.testReadbackBytes == 0)
            {
                try
                {
                    reservation.testReadback.assign(lease->bytes->begin(), lease->bytes->end());
                    reservation.testReadbackBytes = lease->bytes->size();
                }
                catch (...)
                {
                    return false;
                }
            }
        }
        uploadCursor = *end;
    }
    reservation.uploadWriteOffset = uploadCursor;
    if (impl_->testMode && reservation.testReadbackBytes == 0)
    {
        // A frame whose textures are all cache reuses still owns downloads;
        // seed the injected readback from any leased texture content.
        for (const LogicalRenderAssetLease &lease : frame.Assets())
        {
            try
            {
                reservation.testReadback.assign(lease.bytes->begin(), lease.bytes->end());
                reservation.testReadbackBytes = lease.bytes->size();
                break;
            }
            catch (...)
            {
                return false;
            }
        }
    }
    if (!impl_->testMode && reservation.uploadBytes != 0)
    {
        void *mapped = SDL_MapGPUTransferBuffer(impl_->device, impl_->uploadArena, false);
        if (mapped == nullptr)
        {
            return false;
        }
        for (std::size_t index = 0; index < reservation.textureCount; ++index)
        {
            const auto &pending = reservation.textures[index];
            if (pending.reuse || pending.uploadBytes == 0)
            {
                continue;
            }
            const LogicalRenderAssetLease *const lease = findLease(pending.asset);
            if (lease == nullptr)
            {
                SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
                return false;
            }
            std::memcpy(static_cast<std::byte *>(mapped) + pending.uploadOffset,
                        lease->bytes->data(), pending.uploadBytes);
        }
        SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
        SDL_GPUCopyPass *const uploadPass = SDL_BeginGPUCopyPass(reservation.commandBuffer);
        if (uploadPass == nullptr)
        {
            return false;
        }
        for (std::size_t index = 0; index < reservation.textureCount; ++index)
        {
            const auto &pending = reservation.textures[index];
            if (pending.reuse || pending.uploadBytes == 0)
            {
                continue;
            }
            const SDL_GPUTextureTransferInfo source{impl_->uploadArena, pending.uploadOffset,
                                                    pending.width, pending.height};
            const SDL_GPUTextureRegion destination{static_cast<SDL_GPUTexture *>(pending.native),
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   pending.width,
                                                   pending.height,
                                                   1};
            SDL_UploadToGPUTexture(uploadPass, &source, &destination, false);
        }
        SDL_EndGPUCopyPass(uploadPass);
    }
    std::size_t plannedLabelQuads = 0;
    for (const WorkspaceOverlayLabel &label : frame.OverlayLabels())
    {
        const auto next = CheckedAdd(plannedLabelQuads, label.text.size());
        if (!next)
        {
            return false;
        }
        plannedLabelQuads = *next;
    }
    const auto plannedOverlayQuads =
        CheckedAdd(frame.CompositionCount(), frame.OverlayRects().size());
    const auto plannedCompositionQuads =
        plannedOverlayQuads ? CheckedAdd(*plannedOverlayQuads, plannedLabelQuads) : std::nullopt;
    if (!plannedCompositionQuads)
    {
        return false;
    }
    reservation.compositionQuadCount = *plannedCompositionQuads;
    const auto operationCapacity =
        CheckedAdd(reservation.operationCount, reservation.compositionQuadCount);
    if (!operationCapacity)
    {
        return false;
    }
    try
    {
        if (reservation.operationKinds.size() < *operationCapacity)
        {
            reservation.operationKinds.resize(*operationCapacity);
        }
    }
    catch (...)
    {
        return false;
    }

    const bool needsOverlayAtlas =
        frame.CompositionCount() != 0 || !frame.OverlayRects().empty() ||
        std::any_of(
            frame.OverlayLabels().begin(), frame.OverlayLabels().end(),
            [](const WorkspaceOverlayLabel &label) noexcept { return !label.text.empty(); });
    if (needsOverlayAtlas && impl_->overlayAtlas == nullptr)
    {
        if (impl_->testMode)
        {
            impl_->overlayAtlas = TestHandle(0x8500);
            impl_->overlaySampler = TestHandle(0x8501);
        }
        else
        {
            const SamplerPolicy samplerPolicy =
                *MakeSamplerPolicy({LegacyTextureFilter::Nearest, LegacyTextureWrap::ClampToEdge});
            SDL_GPUSamplerCreateInfo samplerInfo{};
            samplerInfo.min_filter = static_cast<SDL_GPUFilter>(samplerPolicy.minFilter);
            samplerInfo.mag_filter = static_cast<SDL_GPUFilter>(samplerPolicy.magFilter);
            samplerInfo.mipmap_mode =
                static_cast<SDL_GPUSamplerMipmapMode>(samplerPolicy.mipmapMode);
            samplerInfo.address_mode_u =
                static_cast<SDL_GPUSamplerAddressMode>(samplerPolicy.addressModeU);
            samplerInfo.address_mode_v =
                static_cast<SDL_GPUSamplerAddressMode>(samplerPolicy.addressModeV);
            samplerInfo.address_mode_w =
                static_cast<SDL_GPUSamplerAddressMode>(samplerPolicy.addressModeW);
            samplerInfo.max_anisotropy = samplerPolicy.maxAnisotropy;
            samplerInfo.compare_op = static_cast<SDL_GPUCompareOp>(samplerPolicy.compareOp);
            samplerInfo.min_lod = samplerPolicy.minLod;
            samplerInfo.max_lod = samplerPolicy.maxLod;
            samplerInfo.props = static_cast<SDL_PropertiesID>(samplerPolicy.props);
            impl_->overlaySampler = SDL_CreateGPUSampler(impl_->device, &samplerInfo);
            const SDL_GPUTextureCreateInfo atlasInfo{SDL_GPU_TEXTURETYPE_2D,
                                                     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                     SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                     50,
                                                     35,
                                                     1,
                                                     1,
                                                     SDL_GPU_SAMPLECOUNT_1,
                                                     0};
            impl_->overlayAtlas = SDL_CreateGPUTexture(impl_->device, &atlasInfo);
            if (impl_->overlaySampler == nullptr || impl_->overlayAtlas == nullptr)
            {
                if (impl_->overlaySampler != nullptr)
                {
                    SDL_ReleaseGPUSampler(impl_->device,
                                          static_cast<SDL_GPUSampler *>(impl_->overlaySampler));
                }
                if (impl_->overlayAtlas != nullptr)
                {
                    SDL_ReleaseGPUTexture(impl_->device,
                                          static_cast<SDL_GPUTexture *>(impl_->overlayAtlas));
                }
                impl_->overlaySampler = nullptr;
                impl_->overlayAtlas = nullptr;
                return false;
            }
            std::array<std::byte, 50 * 35 * 4> atlasBytes{};
            for (std::size_t slot = 0; slot <= WorkspaceGlyphFallbackSlot; ++slot)
            {
                const auto rows = slot < WorkspaceGlyphOrder.size()
                                      ? WorkspaceGlyphRows(WorkspaceGlyphOrder[slot])
                                      : std::array<std::uint8_t, 7>{};
                for (std::size_t row = 0; row < 7; ++row)
                {
                    for (std::size_t column = 0; column < 5; ++column)
                    {
                        if ((rows[row] & (1u << (4u - column))) == 0)
                        {
                            continue;
                        }
                        const std::size_t pixel =
                            ((slot / 10) * 7 + row) * 50 + (slot % 10) * 5 + column;
                        atlasBytes[pixel * 4 + 0] = std::byte{0xFF};
                        atlasBytes[pixel * 4 + 1] = std::byte{0xFF};
                        atlasBytes[pixel * 4 + 2] = std::byte{0xFF};
                        atlasBytes[pixel * 4 + 3] = std::byte{0xFF};
                    }
                }
            }
            const auto compositionVertexCount =
                CheckedMultiply(reservation.compositionQuadCount, QuadVertexCount);
            const auto compositionIndexCount =
                CheckedMultiply(reservation.compositionQuadCount, QuadIndexCount);
            const auto compositionVertexBytes =
                compositionVertexCount
                    ? CheckedMultiply(*compositionVertexCount, sizeof(RenderTapeVertex))
                    : std::nullopt;
            const auto compositionIndexBytes =
                compositionIndexCount
                    ? CheckedMultiply(*compositionIndexCount, sizeof(std::uint32_t))
                    : std::nullopt;
            const auto geometryAndSkinning =
                CheckedAdd(impl_->ledger.geometryUploadBytes, impl_->ledger.skinningUploadBytes);
            const auto geometrySkinningAndInstances =
                geometryAndSkinning
                    ? CheckedAdd(*geometryAndSkinning, impl_->ledger.drawInstanceUploadBytes)
                    : std::nullopt;
            const auto compositionTransferBase =
                geometrySkinningAndInstances
                    ? CheckedAdd(*geometrySkinningAndInstances, impl_->ledger.ownerUploadBytes)
                    : std::nullopt;
            const auto compositionIndexOffset =
                compositionTransferBase && compositionVertexBytes
                    ? CheckedAdd(*compositionTransferBase, *compositionVertexBytes)
                    : std::nullopt;
            const auto atlasOffset =
                compositionIndexOffset && compositionIndexBytes
                    ? CheckedAdd(*compositionIndexOffset, *compositionIndexBytes)
                    : std::nullopt;
            const auto atlasEnd =
                atlasOffset ? CheckedAdd(*atlasOffset, atlasBytes.size()) : std::nullopt;
            const auto uploadLimit =
                compositionTransferBase
                    ? CheckedAdd(*compositionTransferBase, impl_->ledger.overlayDrawBytes)
                    : std::nullopt;
            if (!atlasOffset || !atlasEnd || !uploadLimit || *atlasEnd > *uploadLimit ||
                *atlasEnd > (std::numeric_limits<Uint32>::max)())
            {
                return false;
            }
            void *mapped = SDL_MapGPUTransferBuffer(impl_->device, impl_->uploadArena, false);
            if (mapped == nullptr)
            {
                return false;
            }
            std::memcpy(static_cast<std::byte *>(mapped) + *atlasOffset, atlasBytes.data(),
                        atlasBytes.size());
            SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
            SDL_GPUCopyPass *const atlasPass = SDL_BeginGPUCopyPass(reservation.commandBuffer);
            if (atlasPass == nullptr)
            {
                return false;
            }
            const SDL_GPUTextureTransferInfo source{impl_->uploadArena,
                                                    static_cast<Uint32>(*atlasOffset), 50, 35};
            const SDL_GPUTextureRegion destination{
                static_cast<SDL_GPUTexture *>(impl_->overlayAtlas), 0, 0, 0, 0, 0, 50, 35, 1};
            SDL_UploadToGPUTexture(atlasPass, &source, &destination, false);
            SDL_EndGPUCopyPass(atlasPass);
        }
    }

    if (reservation.sessionCompletionCount + planCount > reservation.sessionCompletions.size())
    {
        return false;
    }
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        auto &sessionCompletion =
            reservation.sessionCompletions[reservation.sessionCompletionCount++];
        sessionCompletion.value =
            SessionReplayCompletion{session->id, session->generation, frame.FrameSequence(),
                                    session->tape.SurfaceGeneration(), true};
        sessionCompletion.deviceGeneration = impl_->deviceGeneration;
        sessionCompletion.targetWidth = session->tape.ViewportWidth();
        sessionCompletion.targetHeight = session->tape.ViewportHeight();
        for (const RenderTapeEntry &entry : session->tape.Entries())
        {
            if (entry.kind != RenderTapeEntryKind::OwnerRequest ||
                reservation.requestCompletionCount >= reservation.requestCompletions.size())
            {
                if (entry.kind == RenderTapeEntryKind::OwnerRequest)
                {
                    return false;
                }
                continue;
            }
            const RenderOwnerRequest &request = session->tape.OwnerRequests()[entry.index];
            RenderOwnerRequestKind kind = RenderOwnerRequestKind::DownloadTargetRgba8;
            std::uint64_t requestId = entry.stableOrder;
            LogicalRenderAssetRef destination;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            RenderSamplerIntent sampler;
            bool download = false;
            bool verticallyFlipped = false;
            if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
            {
                (void)upload;
                continue;
            }
            else if (const auto *copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request))
            {
                kind = RenderOwnerRequestKind::CopyTargetToLogicalTexture;
                destination = copy->destination;
                width = copy->sourceRect.width;
                height = copy->sourceRect.height;
                sampler = copy->sampler;
            }
            else if (const auto *downloadRequest =
                         std::get_if<DownloadTargetRgba8Request>(&request))
            {
                kind = RenderOwnerRequestKind::DownloadTargetRgba8;
                requestId = downloadRequest->requestId;
                width = downloadRequest->rect.width;
                height = downloadRequest->rect.height;
                download = true;
                verticallyFlipped = downloadRequest->verticallyFlipped;
            }
            const auto bytes = Rgba8ByteCount(width, height);
            if (!bytes)
            {
                return false;
            }
            const RenderOwnerRequestCompletion completion{kind,
                                                          requestId,
                                                          session->id,
                                                          session->generation,
                                                          frame.FrameSequence(),
                                                          session->tape.SurfaceGeneration(),
                                                          true,
                                                          destination,
                                                          0,
                                                          0,
                                                          width,
                                                          height,
                                                          sampler};
            auto &pending = reservation.requestCompletions[reservation.requestCompletionCount++];
            pending.value = completion;
            pending.deviceGeneration = impl_->deviceGeneration;
            pending.targetWidth = session->tape.ViewportWidth();
            pending.targetHeight = session->tape.ViewportHeight();
            pending.download = download;
            pending.width = width;
            pending.height = height;
            pending.verticallyFlipped = verticallyFlipped;
            if (download)
            {
                if (reservation.downloadBytes >
                    (std::numeric_limits<std::uint32_t>::max)() - *bytes)
                {
                    return false;
                }
                pending.downloadOffset = static_cast<std::uint32_t>(reservation.downloadBytes);
                pending.downloadBytes = static_cast<std::uint32_t>(*bytes);
                reservation.downloadBytes += *bytes;
            }
        }
    }

    for (std::size_t quad = 0; quad < reservation.compositionQuadCount; ++quad)
    {
        if (reservation.operationCount >= reservation.operationKinds.size())
        {
            return false;
        }
        const bool sessionQuad = quad < frame.CompositionCount();
        const bool rectQuad =
            !sessionQuad && quad < frame.CompositionCount() + frame.OverlayRects().size();
        reservation.operationKinds[reservation.operationCount++] =
            static_cast<std::uint8_t>(sessionQuad ? ReplayOperationKind::Composition
                                      : rectQuad  ? ReplayOperationKind::OverlayRect
                                                  : ReplayOperationKind::Label);
    }

    std::size_t clearQuadCount = 0;
    for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
    {
        const auto *session = frame.Session(plans[planIndex].sessionIndex);
        if (session == nullptr)
        {
            return false;
        }
        bool firstClear = true;
        for (const RenderTapeEntry &entry : session->tape.Entries())
        {
            if (entry.kind != RenderTapeEntryKind::Clear)
            {
                continue;
            }
            const RenderTapeClear &clear = session->tape.Clears()[entry.index];
            const std::uint8_t encoding = ClearEncodingAtPosition(
                clear, {0, 0, plans[planIndex].width, plans[planIndex].height}, firstClear);
            if ((encoding & 0xF0u) != 0)
            {
                ++clearQuadCount;
            }
            firstClear = false;
        }
    }
    reservation.clearQuadCount = clearQuadCount;
    try
    {
        reservation.clearDraws.resize(clearQuadCount);
    }
    catch (...)
    {
        return false;
    }
    const auto buildClearDraws = [&](std::size_t vertexOffset, std::size_t indexOffset) noexcept {
        reservation.clearDrawCount = 0;
        std::size_t quad = 0;
        for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
        {
            const auto *session = frame.Session(plans[planIndex].sessionIndex);
            if (session == nullptr)
            {
                return false;
            }
            bool firstClear = true;
            for (const RenderTapeEntry &entry : session->tape.Entries())
            {
                if (entry.kind != RenderTapeEntryKind::Clear)
                {
                    continue;
                }
                const RenderTapeClear &clear = session->tape.Clears()[entry.index];
                const std::uint8_t encoding = ClearEncodingAtPosition(
                    clear, {0, 0, plans[planIndex].width, plans[planIndex].height}, firstClear);
                if ((encoding & 0xF0u) == 0)
                {
                    firstClear = false;
                    continue;
                }
                if (reservation.clearDrawCount >= reservation.clearDraws.size())
                {
                    return false;
                }
                const auto descriptor = BuildClearDrawDescriptorForPeer(
                    clear, encoding, plans[planIndex].width, plans[planIndex].height,
                    vertexOffset + quad * QuadVertexCount, indexOffset + quad * QuadIndexCount);
                if (!descriptor)
                {
                    return false;
                }
                reservation.clearDraws[reservation.clearDrawCount++] = *descriptor;
                if (impl_->testMode)
                {
                    auto &captured = reservation.clearDraws[reservation.clearDrawCount - 1];
                    captured.pipelineBound = true;
                    captured.vertexBound = true;
                    captured.indexBound = true;
                    captured.uniformPushed = true;
                    captured.stencilReferenceSet = (captured.attachmentMask & 0x04u) != 0;
                    captured.drawIssued = true;
                }
                ++quad;
                firstClear = false;
            }
        }
        return quad == clearQuadCount;
    };
    if (impl_->testMode && !buildClearDraws(0, 0))
    {
        return false;
    }

    if (!impl_->testMode)
    {
        auto &vertexOffsets = impl_->frameVertexOffsets;
        auto &indexOffsets = impl_->frameIndexOffsets;
        auto &paletteOffsets = impl_->framePaletteOffsets;
        auto &quadInstanceOffsets = impl_->frameQuadInstanceOffsets;
        auto &rigidInstanceOffsets = impl_->frameRigidInstanceOffsets;
        auto &particleInstanceOffsets = impl_->frameParticleInstanceOffsets;
        auto &trailSampleOffsets = impl_->frameTrailSampleOffsets;
        auto &trailInstanceOffsets = impl_->frameTrailInstanceOffsets;
        try
        {
            vertexOffsets.resize(planCount);
            indexOffsets.resize(planCount);
            paletteOffsets.resize(planCount);
            quadInstanceOffsets.resize(planCount);
            rigidInstanceOffsets.resize(planCount);
            particleInstanceOffsets.resize(planCount);
            trailSampleOffsets.resize(planCount);
            trailInstanceOffsets.resize(planCount);
        }
        catch (...)
        {
            return false;
        }
        std::size_t totalVertexBytes = 0;
        std::size_t totalIndexBytes = 0;
        std::size_t totalBoneMatrices = 0;
        std::size_t totalInstanceRows = 0;
        for (std::size_t index = 0; index < planCount; ++index)
        {
            const auto *session = frame.Session(plans[index].sessionIndex);
            if (session == nullptr)
            {
                return false;
            }
            const std::size_t vertexBytes =
                session->tape.Vertices().size() * sizeof(RenderTapeVertex);
            const std::size_t indexBytes = session->tape.Indices().size() * sizeof(std::uint32_t);
            if (totalVertexBytes > (std::numeric_limits<Uint32>::max)() - vertexBytes ||
                totalIndexBytes > (std::numeric_limits<Uint32>::max)() - indexBytes)
            {
                return false;
            }
            vertexOffsets[index] = static_cast<Uint32>(totalVertexBytes / sizeof(RenderTapeVertex));
            indexOffsets[index] = static_cast<Uint32>(totalIndexBytes / sizeof(std::uint32_t));
            if (totalBoneMatrices >
                (std::numeric_limits<Uint32>::max)() - session->tape.BoneMatrices().size())
            {
                return false;
            }
            paletteOffsets[index] = static_cast<Uint32>(totalBoneMatrices);
            // The accepted upload reservation already bounded the total bytes.
            // The shared storage buffer is addressed in float4 rows, not records.
            quadInstanceOffsets[index] = static_cast<Uint32>(totalInstanceRows);
            totalInstanceRows += session->tape.QuadInstances().size() * 6;
            rigidInstanceOffsets[index] = static_cast<Uint32>(totalInstanceRows);
            totalInstanceRows += session->tape.RigidInstances().size() * 6;
            particleInstanceOffsets[index] = static_cast<Uint32>(totalInstanceRows);
            totalInstanceRows += session->tape.ParticleInstances().size() * 4;
            trailSampleOffsets[index] = static_cast<Uint32>(totalInstanceRows);
            totalInstanceRows += session->tape.TrailSamples().size() * 2;
            trailInstanceOffsets[index] = static_cast<Uint32>(totalInstanceRows);
            totalInstanceRows += session->tape.TrailInstances().size() * 2;
            totalVertexBytes += vertexBytes;
            totalIndexBytes += indexBytes;
            totalBoneMatrices += session->tape.BoneMatrices().size();
        }
        const auto compositionVertexCount =
            CheckedMultiply(reservation.compositionQuadCount, QuadVertexCount);
        const auto compositionIndexCount =
            CheckedMultiply(reservation.compositionQuadCount, QuadIndexCount);
        const auto compositionVertexBytes =
            compositionVertexCount
                ? CheckedMultiply(*compositionVertexCount, sizeof(RenderTapeVertex))
                : std::nullopt;
        const auto compositionIndexBytes =
            compositionIndexCount ? CheckedMultiply(*compositionIndexCount, sizeof(std::uint32_t))
                                  : std::nullopt;
        const auto clearVertexCount = CheckedMultiply(clearQuadCount, QuadVertexCount);
        const auto clearIndexCount = CheckedMultiply(clearQuadCount, QuadIndexCount);
        const auto clearVertexBytes =
            clearVertexCount ? CheckedMultiply(*clearVertexCount, sizeof(RenderTapeVertex))
                             : std::nullopt;
        const auto clearIndexBytes = clearIndexCount
                                         ? CheckedMultiply(*clearIndexCount, sizeof(std::uint32_t))
                                         : std::nullopt;
        const auto sessionAndClearVertexBytes =
            clearVertexBytes ? CheckedAdd(totalVertexBytes, *clearVertexBytes) : std::nullopt;
        const auto sessionAndClearIndexBytes =
            clearIndexBytes ? CheckedAdd(totalIndexBytes, *clearIndexBytes) : std::nullopt;
        const auto geometryAndSkinning =
            CheckedAdd(impl_->ledger.geometryUploadBytes, impl_->ledger.skinningUploadBytes);
        const auto geometrySkinningAndInstances =
            geometryAndSkinning
                ? CheckedAdd(*geometryAndSkinning, impl_->ledger.drawInstanceUploadBytes)
                : std::nullopt;
        const auto compositionTransferBase =
            geometrySkinningAndInstances
                ? CheckedAdd(*geometrySkinningAndInstances, impl_->ledger.ownerUploadBytes)
                : std::nullopt;
        const auto compositionIndexTransferOffset =
            compositionTransferBase && compositionVertexBytes
                ? CheckedAdd(*compositionTransferBase, *compositionVertexBytes)
                : std::nullopt;
        const auto compositionTransferEnd =
            compositionIndexTransferOffset && compositionIndexBytes
                ? CheckedAdd(*compositionIndexTransferOffset, *compositionIndexBytes)
                : std::nullopt;
        const auto overlayUploadLimit =
            compositionTransferBase
                ? CheckedAdd(*compositionTransferBase, impl_->ledger.overlayDrawBytes)
                : std::nullopt;
        if (!compositionVertexCount || !compositionIndexCount || !compositionVertexBytes ||
            !compositionIndexBytes || !clearVertexCount || !clearIndexCount || !clearVertexBytes ||
            !clearIndexBytes || !sessionAndClearVertexBytes || !sessionAndClearIndexBytes ||
            !compositionTransferBase || !compositionIndexTransferOffset ||
            !compositionTransferEnd || !overlayUploadLimit ||
            *compositionTransferEnd > *overlayUploadLimit ||
            *sessionAndClearVertexBytes >
                (std::numeric_limits<Uint32>::max)() - *compositionVertexBytes ||
            *sessionAndClearIndexBytes >
                (std::numeric_limits<Uint32>::max)() - *compositionIndexBytes)
        {
            return false;
        }
        const std::size_t sessionVertexBytes = totalVertexBytes;
        const std::size_t sessionIndexBytes = totalIndexBytes;
        const std::size_t clearVertexTransferOffset = sessionVertexBytes;
        const std::size_t clearIndexTransferOffset =
            sessionVertexBytes + *clearVertexBytes + sessionIndexBytes;
        const std::size_t compositionVertexBufferOffset = sessionVertexBytes + *clearVertexBytes;
        const std::size_t compositionIndexBufferOffset = sessionIndexBytes + *clearIndexBytes;
        std::size_t persistentTransferCursor =
            sessionVertexBytes + *clearVertexBytes + sessionIndexBytes + *clearIndexBytes;
        for (std::size_t index = 0; index < reservation.geometryCount; ++index)
        {
            auto &pending = reservation.geometries[index];
            if (pending.reuse)
            {
                continue;
            }
            const LogicalGeometryAssetLease &lease = *pending.lease;
            if (persistentTransferCursor > (std::numeric_limits<Uint32>::max)())
            {
                return false;
            }
            pending.vertexUploadOffset = static_cast<std::uint32_t>(persistentTransferCursor);
            persistentTransferCursor += lease.vertices->size() * sizeof(RenderTapeVertex);
            if (persistentTransferCursor > (std::numeric_limits<Uint32>::max)())
            {
                return false;
            }
            pending.indexUploadOffset = static_cast<std::uint32_t>(persistentTransferCursor);
            persistentTransferCursor += lease.indices->size() * sizeof(std::uint32_t);
            if (lease.terrainCells != nullptr)
            {
                if (persistentTransferCursor > (std::numeric_limits<Uint32>::max)())
                {
                    return false;
                }
                pending.terrainUploadOffset = static_cast<std::uint32_t>(persistentTransferCursor);
                persistentTransferCursor +=
                    lease.terrainCells->size() * sizeof(RenderTapeTerrainCell);
            }
            if (lease.terrainInstances != nullptr)
            {
                if (persistentTransferCursor > (std::numeric_limits<Uint32>::max)())
                    return false;
                pending.terrainInstanceUploadOffset =
                    static_cast<std::uint32_t>(persistentTransferCursor);
                persistentTransferCursor +=
                    lease.terrainInstances->size() * sizeof(RenderTapeTerrainInstance);
            }
        }
        if (persistentTransferCursor != impl_->ledger.geometryUploadBytes)
        {
            return false;
        }
        reservation.clearVertexOffset = sessionVertexBytes / sizeof(RenderTapeVertex);
        reservation.clearIndexOffset = sessionIndexBytes / sizeof(std::uint32_t);
        reservation.compositionVertexOffset =
            compositionVertexBufferOffset / sizeof(RenderTapeVertex);
        reservation.compositionIndexOffset = compositionIndexBufferOffset / sizeof(std::uint32_t);
        totalVertexBytes = *sessionAndClearVertexBytes;
        totalIndexBytes = *sessionAndClearIndexBytes;
        totalVertexBytes += *compositionVertexBytes;
        totalIndexBytes += *compositionIndexBytes;
        if (!buildClearDraws(reservation.clearVertexOffset, reservation.clearIndexOffset))
        {
            return false;
        }
        if (!EnsureGeometryBufferCapacity(totalVertexBytes, totalIndexBytes))
        {
            return false;
        }
        reservation.vertexBuffer = totalVertexBytes == 0 ? nullptr : impl_->geometryVertexBuffer;
        reservation.indexBuffer = totalIndexBytes == 0 ? nullptr : impl_->geometryIndexBuffer;
        if (totalVertexBytes != 0 || totalIndexBytes != 0 ||
            impl_->ledger.skinningUploadBytes != 0 || impl_->ledger.drawInstanceUploadBytes != 0)
        {
            const auto geometryTransferBytes =
                CheckedAdd(*sessionAndClearVertexBytes, sessionIndexBytes);
            const auto geometryTransferEnd =
                geometryTransferBytes ? CheckedAdd(*geometryTransferBytes, *clearIndexBytes)
                                      : std::nullopt;
            if (!geometryTransferBytes || !geometryTransferEnd ||
                *geometryTransferEnd > impl_->ledger.geometryUploadBytes ||
                *geometryTransferEnd > (std::numeric_limits<Uint32>::max)())
            {
                return false;
            }
            void *mapped = SDL_MapGPUTransferBuffer(impl_->device, impl_->uploadArena, false);
            if (mapped == nullptr)
            {
                return false;
            }
            std::size_t vertexCursor = 0;
            std::size_t indexCursor = sessionVertexBytes + *clearVertexBytes;
            for (std::size_t index = 0; index < planCount; ++index)
            {
                const auto *session = frame.Session(plans[index].sessionIndex);
                const auto vertices = session->tape.Vertices();
                const auto indices = session->tape.Indices();
                if (!vertices.empty())
                {
                    std::memcpy(static_cast<std::byte *>(mapped) + vertexCursor, vertices.data(),
                                vertices.size() * sizeof(RenderTapeVertex));
                    vertexCursor += vertices.size() * sizeof(RenderTapeVertex);
                }
                if (!indices.empty())
                {
                    std::memcpy(static_cast<std::byte *>(mapped) + indexCursor, indices.data(),
                                indices.size() * sizeof(std::uint32_t));
                    indexCursor += indices.size() * sizeof(std::uint32_t);
                }
            }
            for (std::size_t clearIndex = 0; clearIndex < reservation.clearDrawCount; ++clearIndex)
            {
                const auto &descriptor = reservation.clearDraws[clearIndex];
                const float clipDepth = descriptor.depth * 2.0F - 1.0F;
                const std::array<RenderTapeVertex, QuadVertexCount> vertices{
                    RenderTapeVertex{{-1.0F, 1.0F, clipDepth, 1.0F},
                                     {0.0F, 0.0F},
                                     descriptor.color,
                                     {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{{1.0F, 1.0F, clipDepth, 1.0F},
                                     {1.0F, 0.0F},
                                     descriptor.color,
                                     {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{{1.0F, -1.0F, clipDepth, 1.0F},
                                     {1.0F, 1.0F},
                                     descriptor.color,
                                     {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{{-1.0F, -1.0F, clipDepth, 1.0F},
                                     {0.0F, 1.0F},
                                     descriptor.color,
                                     {0.0F, 0.0F, 1.0F}}};
                const std::size_t vertexByteOffset =
                    clearVertexTransferOffset +
                    clearIndex * QuadVertexCount * sizeof(RenderTapeVertex);
                const std::size_t indexByteOffset =
                    clearIndexTransferOffset + clearIndex * QuadIndexCount * sizeof(std::uint32_t);
                std::memcpy(static_cast<std::byte *>(mapped) + vertexByteOffset, vertices.data(),
                            sizeof(vertices));
                std::memcpy(static_cast<std::byte *>(mapped) + indexByteOffset,
                            descriptor.indices.data(),
                            descriptor.indices.size() * sizeof(descriptor.indices[0]));
            }
            for (std::size_t index = 0; index < reservation.geometryCount; ++index)
            {
                const auto &pending = reservation.geometries[index];
                if (pending.reuse)
                {
                    continue;
                }
                const LogicalGeometryAssetLease &lease = *pending.lease;
                std::memcpy(static_cast<std::byte *>(mapped) + pending.vertexUploadOffset,
                            lease.vertices->data(),
                            lease.vertices->size() * sizeof(RenderTapeVertex));
                std::memcpy(static_cast<std::byte *>(mapped) + pending.indexUploadOffset,
                            lease.indices->data(), lease.indices->size() * sizeof(std::uint32_t));
                if (lease.terrainCells != nullptr)
                {
                    std::memcpy(static_cast<std::byte *>(mapped) + pending.terrainUploadOffset,
                                lease.terrainCells->data(),
                                lease.terrainCells->size() * sizeof(RenderTapeTerrainCell));
                }
                if (lease.terrainInstances != nullptr)
                {
                    std::memcpy(static_cast<std::byte *>(mapped) +
                                    pending.terrainInstanceUploadOffset,
                                lease.terrainInstances->data(),
                                lease.terrainInstances->size() *
                                    sizeof(RenderTapeTerrainInstance));
                }
            }
            if (!WritePoseAndInstanceUploads(frame, std::span(plans).first(planCount),
                                             static_cast<std::byte *>(mapped)))
            {
                SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
                return false;
            }
            const auto appendQuad = [&](std::size_t quadIndex, float left, float top, float right,
                                        float bottom, std::array<float, 4> color,
                                        std::array<float, 4> uv) noexcept {
                const SessionDisplayRect window = frame.WindowRect();
                const float windowWidth = static_cast<float>(window.width);
                const float windowHeight = static_cast<float>(window.height);
                const float ndcLeft =
                    ((left - static_cast<float>(window.x)) / windowWidth) * 2.0F - 1.0F;
                const float ndcRight =
                    ((right - static_cast<float>(window.x)) / windowWidth) * 2.0F - 1.0F;
                const float ndcTop =
                    1.0F - ((top - static_cast<float>(window.y)) / windowHeight) * 2.0F;
                const float ndcBottom =
                    1.0F - ((bottom - static_cast<float>(window.y)) / windowHeight) * 2.0F;
                const std::array<RenderTapeVertex, QuadVertexCount> vertices{
                    RenderTapeVertex{
                        {ndcLeft, ndcTop, 0.0F, 1.0F}, {uv[0], uv[1]}, color, {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{
                        {ndcRight, ndcTop, 0.0F, 1.0F}, {uv[2], uv[1]}, color, {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{{ndcRight, ndcBottom, 0.0F, 1.0F},
                                     {uv[2], uv[3]},
                                     color,
                                     {0.0F, 0.0F, 1.0F}},
                    RenderTapeVertex{{ndcLeft, ndcBottom, 0.0F, 1.0F},
                                     {uv[0], uv[3]},
                                     color,
                                     {0.0F, 0.0F, 1.0F}}};
                const std::size_t vertexOffset =
                    reservation.compositionVertexOffset * sizeof(RenderTapeVertex) +
                    quadIndex * QuadVertexCount * sizeof(RenderTapeVertex);
                const std::array<std::uint32_t, QuadIndexCount> indices{
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 0),
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 1),
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 2),
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 0),
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 2),
                    static_cast<std::uint32_t>(reservation.compositionVertexOffset +
                                               quadIndex * QuadVertexCount + 3)};
                std::memcpy(static_cast<std::byte *>(mapped) + compositionTransferBase.value() +
                                quadIndex * QuadVertexCount * sizeof(RenderTapeVertex),
                            vertices.data(), sizeof(vertices));
                std::memcpy(static_cast<std::byte *>(mapped) +
                                compositionIndexTransferOffset.value() +
                                quadIndex * QuadIndexCount * sizeof(std::uint32_t),
                            indices.data(), sizeof(indices));
                (void)vertexOffset;
            };
            const auto colorFromPacked = [](std::uint32_t value) {
                constexpr float Scale = 1.0F / 255.0F;
                return std::array<float, 4>{static_cast<float>((value >> 24) & 0xFFu) * Scale,
                                            static_cast<float>((value >> 16) & 0xFFu) * Scale,
                                            static_cast<float>((value >> 8) & 0xFFu) * Scale,
                                            static_cast<float>(value & 0xFFu) * Scale};
            };
            std::size_t compositionQuad = 0;
            for (std::size_t compositionIndex = 0; compositionIndex < frame.CompositionCount();
                 ++compositionIndex)
            {
                const auto *composition = frame.Composition(compositionIndex);
                if (composition == nullptr)
                {
                    SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
                    return false;
                }
                const SessionDisplayRect destination = composition->destination;
                appendQuad(compositionQuad++, static_cast<float>(destination.x),
                           static_cast<float>(destination.y),
                           static_cast<float>(destination.x + destination.width),
                           static_cast<float>(destination.y + destination.height),
                           {1.0F, 1.0F, 1.0F, 1.0F}, SessionCompositionUv());
            }
            for (const WorkspaceOverlayRect &overlay : frame.OverlayRects())
            {
                appendQuad(compositionQuad++, static_cast<float>(overlay.rect.x),
                           static_cast<float>(overlay.rect.y),
                           static_cast<float>(overlay.rect.x + overlay.rect.width),
                           static_cast<float>(overlay.rect.y + overlay.rect.height),
                           colorFromPacked(overlay.color), SessionCompositionUv());
            }
            for (const WorkspaceOverlayLabel &label : frame.OverlayLabels())
            {
                std::size_t characterIndex = 0;
                for (const wchar_t character : label.text)
                {
                    appendQuad(compositionQuad++,
                               static_cast<float>(label.x) +
                                   static_cast<float>(characterIndex) * 12.0F * label.scale,
                               static_cast<float>(label.y),
                               static_cast<float>(label.x) +
                                   static_cast<float>(characterIndex) * 12.0F * label.scale +
                                   10.0F * label.scale,
                               static_cast<float>(label.y) + 14.0F * label.scale,
                               colorFromPacked(label.color), GlyphUv(character));
                    ++characterIndex;
                }
            }
            SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
            SDL_GPUCopyPass *const copyPass = SDL_BeginGPUCopyPass(reservation.commandBuffer);
            if (copyPass == nullptr)
            {
                return false;
            }
            if (reservation.vertexBuffer != nullptr)
            {
                const SDL_GPUTransferBufferLocation source{impl_->uploadArena, 0};
                const SDL_GPUBufferRegion destination{reservation.vertexBuffer, 0,
                                                      static_cast<Uint32>(sessionVertexBytes)};
                SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
                if (*clearVertexBytes != 0)
                {
                    const SDL_GPUTransferBufferLocation clearSource{
                        impl_->uploadArena, static_cast<Uint32>(clearVertexTransferOffset)};
                    const SDL_GPUBufferRegion clearDestination{
                        reservation.vertexBuffer, static_cast<Uint32>(sessionVertexBytes),
                        static_cast<Uint32>(*clearVertexBytes)};
                    SDL_UploadToGPUBuffer(copyPass, &clearSource, &clearDestination, false);
                }
                const SDL_GPUTransferBufferLocation compositionSource{
                    impl_->uploadArena, static_cast<Uint32>(compositionTransferBase.value())};
                const SDL_GPUBufferRegion compositionDestination{
                    reservation.vertexBuffer, static_cast<Uint32>(compositionVertexBufferOffset),
                    static_cast<Uint32>(*compositionVertexBytes)};
                SDL_UploadToGPUBuffer(copyPass, &compositionSource, &compositionDestination, false);
            }
            if (reservation.indexBuffer != nullptr)
            {
                const SDL_GPUTransferBufferLocation source{
                    impl_->uploadArena,
                    static_cast<Uint32>(sessionVertexBytes + *clearVertexBytes)};
                const SDL_GPUBufferRegion destination{reservation.indexBuffer, 0,
                                                      static_cast<Uint32>(sessionIndexBytes)};
                SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
                if (*clearIndexBytes != 0)
                {
                    const SDL_GPUTransferBufferLocation clearSource{
                        impl_->uploadArena, static_cast<Uint32>(clearIndexTransferOffset)};
                    const SDL_GPUBufferRegion clearDestination{
                        reservation.indexBuffer, static_cast<Uint32>(sessionIndexBytes),
                        static_cast<Uint32>(*clearIndexBytes)};
                    SDL_UploadToGPUBuffer(copyPass, &clearSource, &clearDestination, false);
                }
                const SDL_GPUTransferBufferLocation compositionSource{
                    impl_->uploadArena,
                    static_cast<Uint32>(compositionIndexTransferOffset.value())};
                const SDL_GPUBufferRegion compositionDestination{
                    reservation.indexBuffer, static_cast<Uint32>(compositionIndexBufferOffset),
                    static_cast<Uint32>(*compositionIndexBytes)};
                SDL_UploadToGPUBuffer(copyPass, &compositionSource, &compositionDestination, false);
            }
            for (std::size_t index = 0; index < reservation.geometryCount; ++index)
            {
                const auto &pending = reservation.geometries[index];
                if (pending.reuse)
                {
                    continue;
                }
                const SDL_GPUTransferBufferLocation vertexSource{impl_->uploadArena,
                                                                 pending.vertexUploadOffset};
                const SDL_GPUBufferRegion vertexDestination{
                    static_cast<SDL_GPUBuffer *>(pending.vertices), 0,
                    static_cast<Uint32>(pending.vertexCount * sizeof(RenderTapeVertex))};
                SDL_UploadToGPUBuffer(copyPass, &vertexSource, &vertexDestination, false);
                const SDL_GPUTransferBufferLocation indexSource{impl_->uploadArena,
                                                                pending.indexUploadOffset};
                const SDL_GPUBufferRegion indexDestination{
                    static_cast<SDL_GPUBuffer *>(pending.indices), 0,
                    static_cast<Uint32>(pending.indexCount * sizeof(std::uint32_t))};
                SDL_UploadToGPUBuffer(copyPass, &indexSource, &indexDestination, false);
                if (pending.lease->terrainCells != nullptr)
                {
                    const SDL_GPUTransferBufferLocation terrainSource{impl_->uploadArena,
                                                                      pending.terrainUploadOffset};
                    const SDL_GPUBufferRegion terrainDestination{
                        static_cast<SDL_GPUBuffer *>(pending.terrainCells), 0,
                        static_cast<Uint32>(pending.lease->terrainCells->size() *
                                            sizeof(RenderTapeTerrainCell))};
                    SDL_UploadToGPUBuffer(copyPass, &terrainSource, &terrainDestination, false);
                }
                if (pending.lease->terrainInstances != nullptr)
                {
                    const SDL_GPUTransferBufferLocation instanceSource{
                        impl_->uploadArena, pending.terrainInstanceUploadOffset};
                    const SDL_GPUBufferRegion instanceDestination{
                        static_cast<SDL_GPUBuffer *>(pending.terrainInstances), 0,
                        static_cast<Uint32>(pending.lease->terrainInstances->size() *
                                            sizeof(RenderTapeTerrainInstance))};
                    SDL_UploadToGPUBuffer(copyPass, &instanceSource, &instanceDestination, false);
                }
            }
            if (impl_->ledger.skinningUploadBytes != 0)
            {
                const SDL_GPUTransferBufferLocation paletteSource{
                    impl_->uploadArena, static_cast<Uint32>(impl_->ledger.geometryUploadBytes)};
                const SDL_GPUBufferRegion paletteDestination{
                    impl_->bonePaletteBuffer, 0,
                    static_cast<Uint32>(impl_->ledger.skinningUploadBytes)};
                SDL_UploadToGPUBuffer(copyPass, &paletteSource, &paletteDestination, false);
            }
            if (impl_->ledger.drawInstanceUploadBytes != 0)
            {
                const SDL_GPUTransferBufferLocation instanceSource{
                    impl_->uploadArena, static_cast<Uint32>(impl_->ledger.geometryUploadBytes +
                                                            impl_->ledger.skinningUploadBytes)};
                const SDL_GPUBufferRegion instanceDestination{
                    impl_->drawInstanceBuffer, 0,
                    static_cast<Uint32>(impl_->ledger.drawInstanceUploadBytes)};
                SDL_UploadToGPUBuffer(copyPass, &instanceSource, &instanceDestination, false);
            }
            SDL_EndGPUCopyPass(copyPass);
        }

        if (!UploadTerrainLights(reservation))
            return false;

        const auto pipelineForDraw = [&](const RenderTapeDraw &draw,
                                         const RenderTapeConstants &constants) noexcept {
            PipelineKey key;
            key.topology = draw.topology;
            key.vertexLayout = 1;
            key.samplerUse = draw.asset.id.value != 0 || draw.asset.revision != 0;
            key.blendEnable = constants.blendEnable;
            key.blendSource = constants.blendSrc;
            key.blendDestination = constants.blendDst;
            key.depthTestEnable = constants.depthTestEnable;
            key.depthWriteEnable = constants.depthWriteEnable;
            key.depthCompare = constants.depthCompare;
            key.stencilEnable = constants.stencilEnable;
            key.stencilCompare = constants.stencilCompare;
            key.stencilFail = constants.stencilFailOp;
            key.stencilDepthFail = constants.stencilDepthFailOp;
            key.stencilPass = constants.stencilPassOp;
            key.stencilReadMask = static_cast<std::uint8_t>(constants.stencilReadMask);
            key.stencilWriteMask = static_cast<std::uint8_t>(constants.stencilReadMask);
            key.cullEnable = constants.cullEnable;
            key.cullFace = constants.cullFace;
            key.frontFace = constants.frontFace;
            key.colorWriteMask = static_cast<std::uint8_t>(
                (constants.colorWriteMask[0] ? 1u : 0u) | (constants.colorWriteMask[1] ? 2u : 0u) |
                (constants.colorWriteMask[2] ? 4u : 0u) | (constants.colorWriteMask[3] ? 8u : 0u));
            key.colorFormat = static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            key.depthFormat = static_cast<std::uint32_t>(impl_->depthFormat);
            key.deviceGeneration = impl_->deviceGeneration;
            return key;
        };
        const auto findPipeline =
            [&](const PipelineKey &key) noexcept -> SDL_GPUGraphicsPipeline * {
            for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
            {
                if (reservation.pipelines[index].key == key)
                {
                    return static_cast<SDL_GPUGraphicsPipeline *>(
                        reservation.pipelines[index].native);
                }
            }
            if (!stagePipeline(key))
            {
                return nullptr;
            }
            for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
            {
                if (reservation.pipelines[index].key == key)
                {
                    return static_cast<SDL_GPUGraphicsPipeline *>(
                        reservation.pipelines[index].native);
                }
            }
            return nullptr;
        };
        const auto findTexture = [&](LogicalRenderAssetRef asset) noexcept -> SDL_GPUTexture * {
            const auto pending = pendingTextureIndex(asset);
            if (pending)
            {
                return static_cast<SDL_GPUTexture *>(reservation.textures[*pending].native);
            }
            const auto resident = residentTextureSlot(asset);
            if (!resident)
            {
                return nullptr;
            }
            return static_cast<SDL_GPUTexture *>(impl_->textures[*resident].native);
        };
        const auto findSampler = [&](RenderSamplerIntent intent) noexcept -> SDL_GPUSampler * {
            for (std::size_t index = 0; index < reservation.samplerCount; ++index)
            {
                if (reservation.samplers[index].key == intent)
                {
                    return static_cast<SDL_GPUSampler *>(reservation.samplers[index].native);
                }
            }
            for (const auto &record : impl_->samplers)
            {
                if (record.live && record.key == intent &&
                    record.deviceGeneration == impl_->deviceGeneration)
                {
                    return static_cast<SDL_GPUSampler *>(record.native);
                }
            }
            return nullptr;
        };
        bool projectionCached = false;
        std::array<float, 16> cachedProjection{};
        std::array<float, 16> cachedConvertedProjection{};
        const auto makeUniform = [&](const RenderTapeConstants &constants,
                                     std::uint32_t paletteBase) {
            if (!projectionCached || std::memcmp(&cachedProjection, &constants.projection,
                                                 sizeof(cachedProjection)) != 0)
            {
                cachedProjection = constants.projection;
                cachedConvertedProjection = ConvertProjectionDepth(constants.projection);
                projectionCached = true;
            }
            RenderTapeUniformData uniform{
                {constants.modelView, cachedConvertedProjection, constants.textureMatrix},
                {constants.fogColor,
                 {constants.fogStart, constants.fogEnd, constants.fogDensity,
                  constants.fogEnable ? 1.0F : 0.0F},
                 {0.0F, 0.0F, 0.0F, constants.lightingEnable ? 1.0F : 0.0F},
                 {constants.textureEnable ? 1.0F : 0.0F,
                  constants.textureEnvironment == RenderTextureEnvironment::GfxTint ? 2.0F
                  : constants.textureEnvironment == RenderTextureEnvironment::Add   ? 1.0F
                                                                                    : 0.0F,
                  constants.alphaTestEnable
                      ? static_cast<float>(AlphaCompareCode(constants.alphaCompare))
                      : -1.0F,
                  constants.alphaReference},
                 constants.textureTint}};
            const RenderTapeBmdConstants &bmd = constants.bmd;
            if (bmd.enabled)
            {
                uniform.vertex.bmdScale = {bmd.positionScale, bmd.boneScale, bmd.bodyScale,
                                           bmd.waveTime};
                uniform.vertex.bmdBodyOrigin = {bmd.bodyOrigin[0], bmd.bodyOrigin[1],
                                                bmd.bodyOrigin[2], bmd.alpha};
                uniform.vertex.bmdBodyLight = {
                    bmd.shadow ? bmd.shadowScaleX : bmd.bodyLight[0],
                    bmd.shadow ? bmd.shadowScaleY : bmd.bodyLight[1],
                    bmd.shadow ? bmd.terrainSpecialHeight : bmd.bodyLight[2], bmd.wavePhase};
                uniform.vertex.bmdBaseColor = bmd.baseColor;
                uniform.vertex.bmdLightPosition = {bmd.lightPosition[0], bmd.lightPosition[1],
                                                   bmd.lightPosition[2], 0.0F};
                uniform.vertex.bmdUvAnimation = {bmd.uvOffset[0], bmd.uvOffset[1], bmd.wavePhase,
                                                 bmd.chromeWave};
                uniform.vertex.bmdChromeLight = {bmd.chromeLight[0], bmd.chromeLight[1],
                                                 bmd.chromeLight[2], bmd.chromeTime};
                uniform.vertex.bmdLegacyLight = {bmd.legacyLight[0], bmd.legacyLight[1],
                                                 bmd.legacyLight[2], 0.0F};
                const std::uint32_t options = (bmd.translate ? 1U : 0U) | (bmd.lighting ? 2U : 0U) |
                                              (bmd.uvScroll ? 4U : 0U) | (bmd.wave ? 8U : 0U) |
                                              (bmd.scaledBone ? 16U : 0U) | (bmd.rigid ? 64U : 0U);
                uniform.vertex.rigidTransform = bmd.rigidTransform;
                uniform.vertex.bmdMode = {bmd.shadow ? 5U : 1U, paletteBase + bmd.paletteOffset,
                                          static_cast<std::uint32_t>(bmd.uvMode), options};
            }
            return uniform;
        };
        const auto beginTargetPass = [&](SDL_GPUTexture *color, SDL_GPUTexture *depth,
                                         RenderTapeRect targetRect, const RenderTapeClear *clear,
                                         bool allowLoad) -> SDL_GPURenderPass * {
            const std::uint8_t encoding =
                clear == nullptr ? (allowLoad ? 0 : 0x07u)
                                 : ClearEncodingAtPosition(*clear, targetRect, allowLoad);
            const SDL_FColor clearColor = clear == nullptr
                                              ? SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F}
                                              : SDL_FColor{clear->color[0], clear->color[1],
                                                           clear->color[2], clear->color[3]};
            const SDL_GPUColorTargetInfo colorInfo{color,
                                                   0,
                                                   0,
                                                   clearColor,
                                                   (encoding & 0x01u) != 0 ? SDL_GPU_LOADOP_CLEAR
                                                                           : SDL_GPU_LOADOP_LOAD,
                                                   SDL_GPU_STOREOP_STORE,
                                                   nullptr,
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   0,
                                                   0};
            const SDL_GPUDepthStencilTargetInfo depthInfo{
                depth,
                clear == nullptr ? 1.0F : clear->depth,
                (encoding & 0x02u) != 0 ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                SDL_GPU_STOREOP_STORE,
                (encoding & 0x04u) != 0 ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD,
                SDL_GPU_STOREOP_STORE,
                false,
                clear == nullptr ? 0 : static_cast<Uint8>(clear->stencil),
                0,
                0};
            SDL_GPURenderPass *const pass =
                SDL_BeginGPURenderPass(reservation.commandBuffer, &colorInfo, 1, &depthInfo);
            if (pass != nullptr)
            {
                SDL_GPUBuffer *const storageBuffers[]{
                    impl_->bonePaletteBuffer, impl_->drawInstanceBuffer, impl_->bonePaletteBuffer,
                    impl_->bonePaletteBuffer};
                SDL_BindGPUVertexStorageBuffers(pass, 0, storageBuffers,
                                                static_cast<Uint32>(std::size(storageBuffers)));
            }
            return pass;
        };
        const RenderTapeUniformData clearUniform{
            {RenderTapeIdentityMatrix4x4, RenderTapeIdentityMatrix4x4, RenderTapeIdentityMatrix4x4},
            {{0.0F, 0.0F, 0.0F, 0.0F},
             {0.0F, 0.0F, 0.0F, 0.0F},
             {0.0F, 0.0F, 0.0F, 0.0F},
             {0.0F, 0.0F, -1.0F, 0.0F}}};
        const auto applyClearDrawState = [&](SDL_GPURenderPass *pass, const RenderTapeClear &clear,
                                             ClearDrawDescriptor *descriptor) noexcept {
            if (descriptor == nullptr)
            {
                return true;
            }
            const SDL_Rect scissorInfo{descriptor->scissor.x, descriptor->scissor.y,
                                       static_cast<int>(descriptor->scissor.width),
                                       static_cast<int>(descriptor->scissor.height)};
            SDL_SetGPUScissor(pass, &scissorInfo);
            const SDL_GPUViewport viewportInfo{0.0F,
                                               0.0F,
                                               static_cast<float>(descriptor->targetWidth),
                                               static_cast<float>(descriptor->targetHeight),
                                               0.0F,
                                               1.0F};
            SDL_SetGPUViewport(pass, &viewportInfo);
            SDL_GPUGraphicsPipeline *const pipeline = findPipeline(descriptor->pipeline);
            if (pipeline == nullptr || reservation.vertexBuffer == nullptr ||
                reservation.indexBuffer == nullptr)
            {
                return false;
            }
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            descriptor->pipelineBound = true;
            const SDL_GPUBufferBinding vertexBinding{reservation.vertexBuffer, 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
            descriptor->vertexBound = true;
            const SDL_GPUBufferBinding indexBinding{reservation.indexBuffer, 0};
            SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            descriptor->indexBound = true;
            SDL_PushGPUVertexUniformData(reservation.commandBuffer, 0, &clearUniform.vertex,
                                         sizeof(clearUniform.vertex));
            SDL_PushGPUFragmentUniformData(reservation.commandBuffer, 0, &clearUniform.fragment,
                                           sizeof(clearUniform.fragment));
            descriptor->uniformPushed = true;
            const SDL_GPUTextureSamplerBinding fallbackBinding{
                static_cast<SDL_GPUTexture *>(impl_->overlayAtlas),
                static_cast<SDL_GPUSampler *>(impl_->overlaySampler)};
            if (fallbackBinding.texture == nullptr || fallbackBinding.sampler == nullptr)
            {
                return false;
            }
            SDL_BindGPUFragmentSamplers(pass, 0, &fallbackBinding, 1);
            if ((descriptor->attachmentMask & 0x04u) != 0)
            {
                SDL_SetGPUStencilReference(pass, static_cast<Uint8>(clear.stencil & 0xFFu));
                descriptor->stencilReferenceSet = true;
            }
            SDL_DrawGPUIndexedPrimitives(pass, QuadIndexCount, 1,
                                         static_cast<Uint32>(descriptor->indexOffset),
                                         static_cast<Sint32>(descriptor->vertexOffset), 0);
            descriptor->drawIssued = true;
            return true;
        };
        std::size_t clearDrawCursor = 0;
        std::size_t geometryReservationBase = 0;
        for (std::size_t planIndex = 0; planIndex < planCount; ++planIndex)
        {
            const auto *session = frame.Session(plans[planIndex].sessionIndex);
            const FrameReservation::Target *target = nullptr;
            for (std::size_t index = 0; index < reservation.targetCount; ++index)
            {
                if (reservation.targets[index].sessionId == plans[planIndex].sessionId)
                {
                    target = &reservation.targets[index];
                    break;
                }
            }
            if (session == nullptr || target == nullptr)
            {
                return false;
            }
            const RenderTapeClear *initialClear = nullptr;
            if (!session->tape.Entries().empty() &&
                session->tape.Entries()[0].kind == RenderTapeEntryKind::Clear)
            {
                const RenderTapeClear &clear =
                    session->tape.Clears()[session->tape.Entries()[0].index];
                initialClear = &clear;
            }
            SDL_GPURenderPass *pass =
                beginTargetPass(static_cast<SDL_GPUTexture *>(target->color),
                                static_cast<SDL_GPUTexture *>(target->depth),
                                {0, 0, plans[planIndex].width, plans[planIndex].height},
                                // An explicit first clear uses attachment clear operations even
                                // for a new target; loading its undefined depth can hide all draws.
                                initialClear, initialClear != nullptr || target->reuse);
            if (pass == nullptr)
            {
                return false;
            }
            bool firstEntry = true;
            bool firstClear = true;
            bool geometryBound = false;
            bool pipelineBound = false;
            bool samplerBound = false;
            bool viewportBound = false;
            bool scissorBound = false;
            bool persistentGeometryBound = false;
            bool shadowTerrainBound = false;
            SDL_GPUBuffer *boundTerrainLight = nullptr;
            SDL_GPUBuffer *boundTerrainCells = nullptr;
            SDL_GPUBuffer *boundInstanceBuffer = impl_->drawInstanceBuffer;
            PipelineKey boundPipelineKey;
            LogicalRenderAssetRef boundAsset;
            LogicalGeometryAssetRef boundGeometry;
            LogicalGeometryAssetRef boundShadowTerrain;
            GpuRect boundViewport;
            SDL_Rect boundScissor{};
            for (const RenderTapeEntry &entry : session->tape.Entries())
            {
                if (entry.kind == RenderTapeEntryKind::Clear)
                {
                    const RenderTapeClear &clear = session->tape.Clears()[entry.index];
                    const std::uint8_t encoding = ClearEncodingAtPosition(
                        clear, {0, 0, plans[planIndex].width, plans[planIndex].height}, firstClear);
                    const bool allowLoad = firstClear;
                    firstClear = false;
                    if (firstEntry && initialClear == &clear)
                    {
                        firstEntry = false;
                        ClearDrawDescriptor *descriptor = nullptr;
                        if ((encoding & 0xF0u) != 0)
                        {
                            if (clearDrawCursor >= reservation.clearDrawCount)
                            {
                                SDL_EndGPURenderPass(pass);
                                return false;
                            }
                            descriptor = &reservation.clearDraws[clearDrawCursor++];
                        }
                        if (!applyClearDrawState(pass, clear, descriptor))
                        {
                            SDL_EndGPURenderPass(pass);
                            return false;
                        }
                        geometryBound = false;
                        pipelineBound = false;
                        samplerBound = false;
                        viewportBound = false;
                        scissorBound = false;
                        continue;
                    }
                    // Later full clears are draw-based. Keep them in this pass so
                    // repeated stencil resets do not reopen the attachments.
                    if ((encoding & 0x07u) != 0)
                    {
                        SDL_EndGPURenderPass(pass);
                        pass = beginTargetPass(static_cast<SDL_GPUTexture *>(target->color),
                                               static_cast<SDL_GPUTexture *>(target->depth),
                                               {0, 0, plans[planIndex].width, plans[planIndex].height},
                                               &clear, allowLoad);
                        if (pass == nullptr)
                            return false;
                        shadowTerrainBound = false;
                        boundTerrainLight = nullptr;
                    }
                    ClearDrawDescriptor *descriptor = nullptr;
                    if ((encoding & 0xF0u) != 0)
                    {
                        if (clearDrawCursor >= reservation.clearDrawCount)
                        {
                            SDL_EndGPURenderPass(pass);
                            return false;
                        }
                        descriptor = &reservation.clearDraws[clearDrawCursor++];
                    }
                    if (!applyClearDrawState(pass, clear, descriptor))
                    {
                        SDL_EndGPURenderPass(pass);
                        return false;
                    }
                    geometryBound = false;
                    pipelineBound = false;
                    samplerBound = false;
                    viewportBound = false;
                    scissorBound = false;
                    firstEntry = false;
                    continue;
                }
                if (entry.kind == RenderTapeEntryKind::OwnerRequest)
                {
                    const RenderOwnerRequest &request = session->tape.OwnerRequests()[entry.index];
                    SDL_EndGPURenderPass(pass);
                    SDL_GPUCopyPass *const copyPass =
                        SDL_BeginGPUCopyPass(reservation.commandBuffer);
                    if (copyPass == nullptr)
                    {
                        return false;
                    }
                    if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
                    {
                        const auto payload = session->tape.PayloadBytes();
                        const auto payloadEnd =
                            CheckedAdd(static_cast<std::size_t>(upload->payloadOffset),
                                       static_cast<std::size_t>(upload->payloadByteCount));
                        const auto uploadEnd =
                            CheckedAdd(reservation.uploadWriteOffset,
                                       static_cast<std::size_t>(upload->payloadByteCount));
                        SDL_GPUTexture *const destination = findTexture(upload->destination);
                        if (!payloadEnd || *payloadEnd > payload.size() || !uploadEnd ||
                            *uploadEnd > *ownerUploadLimit || destination == nullptr)
                        {
                            SDL_EndGPUCopyPass(copyPass);
                            return false;
                        }
                        void *mapped =
                            SDL_MapGPUTransferBuffer(impl_->device, impl_->uploadArena, false);
                        if (mapped == nullptr)
                        {
                            SDL_EndGPUCopyPass(copyPass);
                            return false;
                        }
                        std::memcpy(
                            static_cast<std::byte *>(mapped) + reservation.uploadWriteOffset,
                            payload.data() + upload->payloadOffset, upload->payloadByteCount);
                        SDL_UnmapGPUTransferBuffer(impl_->device, impl_->uploadArena);
                        const SDL_GPUTextureTransferInfo source{
                            impl_->uploadArena, static_cast<Uint32>(reservation.uploadWriteOffset),
                            upload->width, upload->height};
                        const SDL_GPUTextureRegion destinationRegion{
                            destination, 0, 0, 0, 0, 0, upload->width, upload->height, 1};
                        SDL_UploadToGPUTexture(copyPass, &source, &destinationRegion, false);
                        reservation.uploadWriteOffset = *uploadEnd;
                    }
                    else if (const auto *copy =
                                 std::get_if<CopyTargetToLogicalTextureRequest>(&request))
                    {
                        const auto sourceRect = MapGpuRect(
                            copy->sourceRect, session->tape.ViewportHeight(), 0.0F, 1.0F);
                        const SDL_GPUTexture *source = static_cast<SDL_GPUTexture *>(target->color);
                        SDL_GPUTexture *const destination = findTexture(copy->destination);
                        if (destination == nullptr || !sourceRect)
                        {
                            SDL_EndGPUCopyPass(copyPass);
                            return false;
                        }
                        const SDL_GPUTextureLocation sourceLocation{
                            const_cast<SDL_GPUTexture *>(source),
                            0,
                            0,
                            static_cast<Uint32>(sourceRect->x),
                            static_cast<Uint32>(sourceRect->y),
                            0};
                        const SDL_GPUTextureLocation destinationLocation{destination, 0, 0,
                                                                         0,           0, 0};
                        SDL_CopyGPUTextureToTexture(copyPass, &sourceLocation, &destinationLocation,
                                                    copy->sourceRect.width, copy->sourceRect.height,
                                                    1, false);
                    }
                    else if (const auto *download =
                                 std::get_if<DownloadTargetRgba8Request>(&request))
                    {
                        const auto sourceRect =
                            MapGpuRect(download->rect, session->tape.ViewportHeight(), 0.0F, 1.0F);
                        const auto bytes =
                            Rgba8ByteCount(download->rect.width, download->rect.height);
                        const auto *pending = reservation.FindDownloadCompletion(*download);
                        if (!bytes || pending == nullptr || !sourceRect)
                        {
                            SDL_EndGPUCopyPass(copyPass);
                            return false;
                        }
                        const SDL_GPUTextureRegion sourceRegion{
                            static_cast<SDL_GPUTexture *>(target->color),
                            0,
                            0,
                            static_cast<Uint32>(sourceRect->x),
                            static_cast<Uint32>(sourceRect->y),
                            0,
                            download->rect.width,
                            download->rect.height,
                            1};
                        const SDL_GPUTextureTransferInfo destinationInfo{
                            impl_->downloadArena, pending->downloadOffset, download->rect.width,
                            download->rect.height};
                        SDL_DownloadFromGPUTexture(copyPass, &sourceRegion, &destinationInfo);
                    }
                    SDL_EndGPUCopyPass(copyPass);
                    pass = beginTargetPass(static_cast<SDL_GPUTexture *>(target->color),
                                           static_cast<SDL_GPUTexture *>(target->depth),
                                           {0, 0, plans[planIndex].width, plans[planIndex].height},
                                           nullptr, true);
                    if (pass == nullptr)
                    {
                        return false;
                    }
                    geometryBound = false;
                    pipelineBound = false;
                    samplerBound = false;
                    viewportBound = false;
                    scissorBound = false;
                    shadowTerrainBound = false;
                    boundTerrainLight = nullptr;
                    boundTerrainCells = nullptr;
                    boundInstanceBuffer = impl_->drawInstanceBuffer;
                    firstEntry = false;
                    continue;
                }
                if (entry.kind != RenderTapeEntryKind::Draw)
                {
                    continue;
                }
                const RenderTapeDraw &draw = session->tape.Draws()[entry.index];
                const RenderTapeConstants &constants =
                    session->tape.Constants()[draw.constantsIndex];
                if (constants.bmd.shadow &&
                    (!shadowTerrainBound ||
                     boundShadowTerrain != constants.bmd.shadowTerrainGeometry))
                {
                    const FrameReservation::Geometry &terrain =
                        reservation.geometries[geometryReservationBase +
                                               session->tape.GeometryAssetIndex(
                                                   constants.bmd.shadowTerrainGeometry)];
                    SDL_GPUBuffer *const terrainBuffer =
                        static_cast<SDL_GPUBuffer *>(terrain.terrainCells);
                    SDL_BindGPUVertexStorageBuffers(pass, 2, &terrainBuffer, 1);
                    boundTerrainCells = terrainBuffer;
                    boundShadowTerrain = constants.bmd.shadowTerrainGeometry;
                    shadowTerrainBound = true;
                }
                const PipelineKey pipelineKey = pipelineForDraw(draw, constants);
                if (!pipelineBound || !(boundPipelineKey == pipelineKey))
                {
                    SDL_GPUGraphicsPipeline *const pipeline = findPipeline(pipelineKey);
                    if (pipeline == nullptr)
                    {
                        SDL_EndGPURenderPass(pass);
                        return false;
                    }
                    SDL_BindGPUGraphicsPipeline(pass, pipeline);
                    boundPipelineKey = pipelineKey;
                    pipelineBound = true;
                }
                ApplyGpuStencilReference(pass, constants);
                const bool usesPersistentGeometry = IsValid(draw.geometry);
                if (usesPersistentGeometry && (!geometryBound || !persistentGeometryBound ||
                                               !(boundGeometry == draw.geometry)))
                {
                    const FrameReservation::Geometry &geometry =
                        reservation.geometries[geometryReservationBase +
                                               session->tape.GeometryAssetIndex(draw.geometry)];
                    const SDL_GPUBufferBinding vertexBinding{
                        static_cast<SDL_GPUBuffer *>(geometry.vertices), 0};
                    const SDL_GPUBufferBinding indexBinding{
                        static_cast<SDL_GPUBuffer *>(geometry.indices), 0};
                    SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    geometryBound = true;
                    persistentGeometryBound = true;
                    boundGeometry = draw.geometry;
                }
                if (draw.pipeline.geometryMode == RenderGeometryMode::Terrain ||
                    draw.pipeline.geometryMode == RenderGeometryMode::Grass)
                {
                    const FrameReservation::Geometry &geometry =
                        reservation.geometries[geometryReservationBase +
                                               session->tape.GeometryAssetIndex(draw.geometry)];
                    SDL_GPUBuffer *const instanceBuffer =
                        static_cast<SDL_GPUBuffer *>(geometry.terrainInstances);
                    SDL_GPUBuffer *const terrainCells =
                        static_cast<SDL_GPUBuffer *>(geometry.terrainCells);
                    if (instanceBuffer == nullptr || terrainCells == nullptr)
                    {
                        SDL_EndGPURenderPass(pass);
                        return false;
                    }
                    if (boundInstanceBuffer != instanceBuffer)
                    {
                        SDL_BindGPUVertexStorageBuffers(pass, 1, &instanceBuffer, 1);
                        boundInstanceBuffer = instanceBuffer;
                    }
                    if (boundTerrainCells != terrainCells)
                    {
                        SDL_BindGPUVertexStorageBuffers(pass, 2, &terrainCells, 1);
                        boundTerrainCells = terrainCells;
                    }
                }
                else if (boundInstanceBuffer != impl_->drawInstanceBuffer)
                {
                    SDL_GPUBuffer *const instanceBuffer = impl_->drawInstanceBuffer;
                    SDL_BindGPUVertexStorageBuffers(pass, 1, &instanceBuffer, 1);
                    boundInstanceBuffer = instanceBuffer;
                }
                if (!usesPersistentGeometry && (!geometryBound || persistentGeometryBound))
                {
                    if (reservation.vertexBuffer != nullptr)
                    {
                        const SDL_GPUBufferBinding vertexBinding{
                            reservation.vertexBuffer,
                            static_cast<Uint32>(vertexOffsets[planIndex] *
                                                sizeof(RenderTapeVertex))};
                        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                    }
                    if (reservation.indexBuffer != nullptr)
                    {
                        const SDL_GPUBufferBinding indexBinding{
                            reservation.indexBuffer,
                            static_cast<Uint32>(indexOffsets[planIndex] * sizeof(std::uint32_t))};
                        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    }
                    geometryBound = true;
                    persistentGeometryBound = false;
                }
                const GpuRect viewport =
                    *MapGpuRect(constants.viewport, session->tape.ViewportHeight(),
                                constants.depthRangeNear, constants.depthRangeFar);
                if (!viewportBound || viewport.x != boundViewport.x ||
                    viewport.y != boundViewport.y || viewport.width != boundViewport.width ||
                    viewport.height != boundViewport.height ||
                    viewport.minDepth != boundViewport.minDepth ||
                    viewport.maxDepth != boundViewport.maxDepth)
                {
                    const SDL_GPUViewport viewportInfo{static_cast<float>(viewport.x),
                                                       static_cast<float>(viewport.y),
                                                       static_cast<float>(viewport.width),
                                                       static_cast<float>(viewport.height),
                                                       viewport.minDepth,
                                                       viewport.maxDepth};
                    SDL_SetGPUViewport(pass, &viewportInfo);
                    boundViewport = viewport;
                    viewportBound = true;
                }
                SDL_Rect scissorInfo{0, 0, static_cast<int>(plans[planIndex].width),
                                     static_cast<int>(plans[planIndex].height)};
                if (constants.scissorEnable)
                {
                    const GpuRect scissor =
                        *MapGpuRect(constants.scissor, session->tape.ViewportHeight(), 0.0F, 1.0F);
                    scissorInfo = {scissor.x, scissor.y, static_cast<int>(scissor.width),
                                   static_cast<int>(scissor.height)};
                }
                if (!scissorBound || scissorInfo.x != boundScissor.x ||
                    scissorInfo.y != boundScissor.y || scissorInfo.w != boundScissor.w ||
                    scissorInfo.h != boundScissor.h)
                {
                    SDL_SetGPUScissor(pass, &scissorInfo);
                    boundScissor = scissorInfo;
                    scissorBound = true;
                }
                RenderTapeUniformData uniform = makeUniform(constants, paletteOffsets[planIndex]);
                const bool usesQuadInstances = UsesQuadInstanceStorage(draw.pipeline.geometryMode);
                if (usesQuadInstances)
                {
                    uniform.vertex.bmdMode = {QuadShaderMode(draw.pipeline.geometryMode),
                                              quadInstanceOffsets[planIndex] +
                                                  constants.quad.instanceOffset * 6U,
                                              0U, 0U};
                }
                else if (draw.pipeline.geometryMode == RenderGeometryMode::RigidInstances)
                {
                    uniform.vertex.bmdMode[0] = 6U;
                    uniform.vertex.bmdMode[1] =
                        rigidInstanceOffsets[planIndex] + constants.bmd.rigidInstanceOffset * 6U;
                }
                else if (draw.pipeline.geometryMode == RenderGeometryMode::ParticleInstances)
                {
                    uniform.vertex.bmdMode = {
                        7U, particleInstanceOffsets[planIndex] + constants.quad.instanceOffset * 4U,
                        0U, 0U};
                    uniform.vertex.rigidTransform = constants.bmd.rigidTransform;
                }
                else if (UsesTrailInstanceStorage(draw.pipeline.geometryMode))
                {
                    uniform.vertex.bmdMode = {
                        draw.pipeline.geometryMode == RenderGeometryMode::BlurInstances ? 9U : 8U,
                        trailInstanceOffsets[planIndex] + constants.quad.instanceOffset * 2U,
                        trailSampleOffsets[planIndex] + constants.trail.sampleOffset * 2U,
                        constants.trail.faceMask};
                    uniform.vertex.bmdUvAnimation[0] = constants.trail.secondFaceUOffset;
                }
                else if (draw.pipeline.geometryMode == RenderGeometryMode::Grass)
                {
                    uniform.vertex.bmdScale = {constants.grass.windSpeed, constants.grass.windScale,
                                               constants.grass.windFrequency, 0.0F};
                    uniform.vertex.bmdBodyOrigin[3] = constants.grass.specialHeight;
                    uniform.vertex.bmdMode = {4U, constants.quad.instanceOffset, 0U, 0U};
                }
                else if (draw.pipeline.geometryMode == RenderGeometryMode::Terrain)
                {
                    uniform.vertex.bmdScale = {constants.terrain.uvScale[0],
                                               constants.terrain.uvScale[1],
                                               constants.terrain.waterMove,
                                               constants.terrain.waterWindScale};
                    uniform.vertex.bmdBodyOrigin = {constants.terrain.windSpeed,
                                                    constants.terrain.windScale,
                                                    constants.terrain.windFrequency,
                                                    constants.terrain.specialHeight};
                    uniform.vertex.bmdMode = {10U, constants.quad.instanceOffset,
                                              constants.terrain.flags, 0U};
                }
                if (usesPersistentGeometry &&
                    (draw.pipeline.geometryMode == RenderGeometryMode::Terrain ||
                     draw.pipeline.geometryMode == RenderGeometryMode::Grass))
                {
                    const auto &geometry =
                        reservation.geometries[geometryReservationBase +
                                               session->tape.GeometryAssetIndex(draw.geometry)];
                    if (geometry.lease->terrainLight)
                    {
                        SDL_GPUBuffer *lightBuffer = geometry.lightBuffer;
                        if (lightBuffer != boundTerrainLight)
                        {
                            SDL_BindGPUVertexStorageBuffers(pass, 3, &lightBuffer, 1);
                            boundTerrainLight = lightBuffer;
                        }
                    }
                }
                SDL_PushGPUVertexUniformData(reservation.commandBuffer, 0, &uniform.vertex,
                                             sizeof(uniform.vertex));
                SDL_PushGPUFragmentUniformData(reservation.commandBuffer, 0, &uniform.fragment,
                                               sizeof(uniform.fragment));
                if (!samplerBound || !(draw.asset == boundAsset))
                {
                    if (draw.asset.id.value != 0 || draw.asset.revision != 0)
                    {
                        const LogicalRenderAssetLease *const lease = findLease(draw.asset);
                        const SDL_GPUSampler *const drawSampler =
                            lease == nullptr ? nullptr : findSampler(lease->sampler);
                        const SDL_GPUTextureSamplerBinding binding{
                            findTexture(draw.asset), const_cast<SDL_GPUSampler *>(drawSampler)};
                        if (binding.texture == nullptr || binding.sampler == nullptr)
                        {
                            SDL_EndGPURenderPass(pass);
                            return false;
                        }
                        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                    }
                    else
                    {
                        const SDL_GPUTextureSamplerBinding binding{
                            static_cast<SDL_GPUTexture *>(impl_->overlayAtlas),
                            static_cast<SDL_GPUSampler *>(impl_->overlaySampler)};
                        if (binding.texture == nullptr || binding.sampler == nullptr)
                        {
                            SDL_EndGPURenderPass(pass);
                            return false;
                        }
                        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
                    }
                    boundAsset = draw.asset;
                    samplerBound = true;
                }
                SDL_DrawGPUIndexedPrimitives(
                    pass, draw.indexCount,
                    (usesQuadInstances || draw.pipeline.geometryMode == RenderGeometryMode::Terrain ||
                     draw.pipeline.geometryMode == RenderGeometryMode::Grass ||
                      draw.pipeline.geometryMode == RenderGeometryMode::ParticleInstances)
                        ? constants.quad.instanceCount
                    : UsesTrailInstanceStorage(draw.pipeline.geometryMode)
                        ? constants.quad.instanceCount * (constants.trail.faceMask == 3 ? 2U : 1U)
                    : draw.pipeline.geometryMode == RenderGeometryMode::RigidInstances
                        ? constants.bmd.rigidInstanceCount
                        : 1,
                    usesPersistentGeometry ? draw.indexOffset : draw.indexOffset,
                    static_cast<Sint32>(usesPersistentGeometry ? draw.vertexOffset
                                                               : draw.vertexOffset),
                    0);
                firstEntry = false;
            }
            SDL_EndGPURenderPass(pass);
            geometryReservationBase += session->tape.GeometryAssets().size();
        }
    }

    return true;
}

void SdlGpuRenderBackend::CancelReservation(FrameReservation &reservation) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    bool cancelSucceeded = true;
    if (reservation.commandBuffer != nullptr && !reservation.swapchainAcquired)
    {
        ++impl_->replayCancelCount;
        if (impl_->testMode)
        {
            cancelSucceeded = impl_->nativeTestConfig.cancelCommandBuffer;
        }
        else
        {
            cancelSucceeded = SDL_CancelGPUCommandBuffer(reservation.commandBuffer);
        }
    }
    if (!impl_->testMode && impl_->device != nullptr)
    {
        for (std::size_t index = 0; index < reservation.targetCount; ++index)
        {
            if (!reservation.targets[index].reuse)
            {
                if (reservation.targets[index].color != nullptr)
                {
                    SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(
                                                             reservation.targets[index].color));
                }
                if (reservation.targets[index].depth != nullptr)
                {
                    SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(
                                                             reservation.targets[index].depth));
                }
            }
        }
        for (std::size_t index = 0; index < reservation.textureCount; ++index)
        {
            if (!reservation.textures[index].reuse && !reservation.textures[index].recycled)
            {
                SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(
                                                         reservation.textures[index].native));
            }
        }
        for (std::size_t index = 0; index < reservation.samplerCount; ++index)
        {
            if (!reservation.samplers[index].reuse)
            {
                SDL_ReleaseGPUSampler(impl_->device, static_cast<SDL_GPUSampler *>(
                                                         reservation.samplers[index].native));
            }
        }
        for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
        {
            if (!reservation.pipelines[index].reuse)
            {
                SDL_ReleaseGPUGraphicsPipeline(
                    impl_->device,
                    static_cast<SDL_GPUGraphicsPipeline *>(reservation.pipelines[index].native));
            }
        }
        for (std::size_t index = 0; index < reservation.geometryCount; ++index)
        {
            auto &light = reservation.geometries[index];
            if (!light.reuseLight)
            {
                if (light.lightBuffer)
                    SDL_ReleaseGPUBuffer(impl_->device, light.lightBuffer);
                if (light.lightTransfer)
                    SDL_ReleaseGPUTransferBuffer(impl_->device, light.lightTransfer);
            }
            if (reservation.geometries[index].reuse || reservation.geometries[index].recycled)
            {
                continue;
            }
            SDL_ReleaseGPUBuffer(impl_->device, static_cast<SDL_GPUBuffer *>(
                                                    reservation.geometries[index].vertices));
            SDL_ReleaseGPUBuffer(
                impl_->device, static_cast<SDL_GPUBuffer *>(reservation.geometries[index].indices));
            if (reservation.geometries[index].terrainCells != nullptr)
            {
                SDL_ReleaseGPUBuffer(
                    impl_->device,
                    static_cast<SDL_GPUBuffer *>(reservation.geometries[index].terrainCells));
            }
            if (reservation.geometries[index].terrainInstances != nullptr)
                SDL_ReleaseGPUBuffer(
                    impl_->device,
                    static_cast<SDL_GPUBuffer *>(reservation.geometries[index].terrainInstances));
        }
    }
    for (std::size_t index = 0; index < reservation.geometryCount; ++index)
    {
        const auto &pending = reservation.geometries[index];
        if (pending.uploadLight && pending.reuseLight)
        {
            // A canceled submission may already have cycled this buffer.
            impl_->geometries[pending.slot].lightRevision = 0;
        }
        if (!reservation.geometries[index].reuse)
        {
            impl_->geometrySlots.erase(reservation.geometries[index].asset);
        }
    }
    if (reservation.completionPrepared)
    {
        impl_->completions.Rollback(reservation.completionCheckpoint);
    }
    ResetReservationMetadata(reservation);
    reservation.cancelSucceeded = cancelSucceeded;
}

void SdlGpuRenderBackend::PublishReservation(const ApplicationRenderFrame &frame,
                                             const std::vector<CandidatePlan> &, std::size_t,
                                             FrameReservation &reservation,
                                             std::uint64_t fenceToken) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    for (std::size_t index = 0; index < reservation.targetCount; ++index)
    {
        auto &pending = reservation.targets[index];
        auto &record = impl_->sessionTargets[pending.slot];
        if (!pending.reuse && record.live)
        {
            ReleaseSessionTarget(*impl_, record);
        }
        record.live = true;
        record.key = {
            pending.sessionId, pending.sessionGeneration, pending.surfaceGeneration, pending.width,
            pending.height,    impl_->deviceGeneration};
        record.color = pending.color;
        record.depth = pending.depth;
        record.lastFence = fenceToken;
    }
    for (std::size_t compositionIndex = 0; compositionIndex < frame.CompositionCount();
         ++compositionIndex)
    {
        const auto *const composition = frame.Composition(compositionIndex);
        if (composition == nullptr)
        {
            continue;
        }
        for (auto &record : impl_->sessionTargets)
        {
            if (record.live && record.key.id == composition->id.RawValue() &&
                record.key.generation == composition->generation.RawValue() &&
                record.key.surfaceGeneration == composition->surfaceGeneration &&
                record.key.width == composition->destination.width &&
                record.key.height == composition->destination.height &&
                record.key.deviceGeneration == impl_->deviceGeneration)
            {
                record.lastFence = fenceToken;
                break;
            }
        }
    }
    std::size_t residentBytes = impl_->residentTextureBytes;
    for (std::size_t index = 0; index < reservation.textureEvictionCount; ++index)
    {
        auto &record = impl_->textures[reservation.textureEvictions[index]];
        if (record.live)
        {
            residentBytes -= record.residentBytes;
            if (record.key.asset.id.value < impl_->textureSlotsByAssetId.size())
            {
                impl_->textureSlotsByAssetId[record.key.asset.id.value] =
                    (std::numeric_limits<std::size_t>::max)();
            }
            ReleaseTexture(*impl_, record);
        }
    }
    for (std::size_t index = 0; index < reservation.textureCount; ++index)
    {
        auto &pending = reservation.textures[index];
        auto &record = impl_->textures[pending.slot];
        if (pending.reuse)
        {
            record.lastFence = fenceToken;
            continue;
        }
        if (record.live && record.key.asset != pending.asset &&
            record.key.asset.id.value < impl_->textureSlotsByAssetId.size())
        {
            impl_->textureSlotsByAssetId[record.key.asset.id.value] =
                (std::numeric_limits<std::size_t>::max)();
        }
        record.live = true;
        record.key = {pending.asset, impl_->deviceGeneration};
        record.width = pending.width;
        record.height = pending.height;
        record.residentBytes = pending.residentBytes;
        record.native = pending.native;
        record.lastFence = fenceToken;
        record.frameOnly = pending.frameOnly;
        impl_->textureSlotsByAssetId[pending.asset.id.value] = pending.slot;
    }
    for (std::size_t index = 0; index < reservation.textureCount; ++index)
    {
        if (!reservation.textures[index].reuse && !reservation.textures[index].recycled)
        {
            residentBytes += reservation.textures[index].residentBytes;
        }
    }
    impl_->residentTextureBytes = residentBytes;
    for (std::size_t index = 0; index < reservation.samplerCount; ++index)
    {
        auto &pending = reservation.samplers[index];
        auto &record = impl_->samplers[pending.slot];
        if (!pending.reuse && record.live)
        {
            ReleaseSampler(*impl_, record);
        }
        record.live = true;
        record.key = pending.key;
        record.deviceGeneration = impl_->deviceGeneration;
        record.native = pending.native;
        record.lastFence = fenceToken;
    }
    for (std::size_t index = 0; index < reservation.pipelineCount; ++index)
    {
        auto &pending = reservation.pipelines[index];
        auto &record = impl_->pipelines[pending.slot];
        if (!pending.reuse && record.live)
        {
            ReleasePipeline(*impl_, record);
        }
        record.live = true;
        record.key = pending.key;
        record.policy = pending.policy;
        record.native = pending.native;
        record.lastFence = fenceToken;
    }
    for (std::size_t index = 0; index < reservation.geometryCount; ++index)
    {
        auto &pending = reservation.geometries[index];
        auto &record = impl_->geometries[pending.slot];
        if (pending.reuse)
        {
            if (pending.uploadLight)
            {
                if (!pending.reuseLight && !impl_->testMode)
                {
                    if (record.lightBuffer)
                        SDL_ReleaseGPUBuffer(impl_->device, record.lightBuffer);
                    if (record.lightTransfer)
                        SDL_ReleaseGPUTransferBuffer(impl_->device, record.lightTransfer);
                }
                record.lightBuffer = pending.lightBuffer;
                record.lightTransfer = pending.lightTransfer;
                record.lightRevision = pending.lease->terrainLight->revision;
                record.lightBytes = pending.lease->terrainLight->values.size() * sizeof(float);
            }
            record.lastFence = fenceToken;
            continue;
        }
        if (record.live)
        {
            impl_->geometrySlots.erase(record.asset);
            if (!pending.recycled)
            {
                ReleaseGeometry(*impl_, record);
            }
        }
        record.live = true;
        record.asset = pending.asset;
        record.vertices = pending.vertices;
        record.indices = pending.indices;
        record.terrainCells = pending.terrainCells;
        record.terrainInstances = pending.terrainInstances;
        if (pending.recycled && !impl_->testMode)
        {
            if (record.lightBuffer)
                SDL_ReleaseGPUBuffer(impl_->device, record.lightBuffer);
            if (record.lightTransfer)
                SDL_ReleaseGPUTransferBuffer(impl_->device, record.lightTransfer);
        }
        record.lightBuffer = pending.lightBuffer;
        record.lightTransfer = pending.lightTransfer;
        record.lightRevision =
            pending.lease->terrainLight ? pending.lease->terrainLight->revision : 0;
        record.lightBytes = pending.lease->terrainLight
                                ? pending.lease->terrainLight->values.size() * sizeof(float)
                                : 0;
        if (!pending.recycled)
        {
            record.vertexCapacity = pending.vertexCount;
            record.indexCapacity = pending.indexCount;
            record.terrainCellCapacity =
                pending.lease->terrainCells == nullptr ? 0 : pending.lease->terrainCells->size();
            record.terrainInstanceCapacity = pending.lease->terrainInstances == nullptr
                                                 ? 0
                                                 : pending.lease->terrainInstances->size();
        }
        record.lastFence = fenceToken;
    }
    impl_->pendingFrameSequence = frame.FrameSequence();
    impl_->lastFrameSequence = frame.FrameSequence();
}

namespace
{
bool ConfigureDeviceProperties(SDL_PropertiesID properties, std::wstring_view rendererBackend)
{
    const std::string driver(rendererBackend.begin(), rendererBackend.end());
    return SDL_SetStringProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,
                                 driver == "auto" ? nullptr : driver.c_str()) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN,
                                  false) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN,
                                  false) &&
           SDL_SetBooleanProperty(properties,
                                  SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN,
                                  false) &&
           SDL_SetBooleanProperty(properties, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN,
                                  false);
}
} // namespace

SdlGpuBackendInitializeResult SdlGpuRenderBackend::Initialize(AppWindow &window) noexcept
{
    if (!IsOwnerThreadForPeer())
    {
        return SdlGpuBackendInitializeResult::WrongOwnerThread;
    }
    if (impl_ == nullptr)
    {
        return SdlGpuBackendInitializeResult::DeviceCreationFailed;
    }
    if (impl_->ready)
    {
        return SdlGpuBackendInitializeResult::Success;
    }
    if (!window.IsReady())
    {
        return SdlGpuBackendInitializeResult::WindowUnavailable;
    }
    if (impl_->nativeTestConfig.enabled)
    {
        return InitializeInjected(window);
    }

    SDL_Window *const sdlWindow = static_cast<SDL_Window *>(window.Handle());
    if (sdlWindow == nullptr)
    {
        return SdlGpuBackendInitializeResult::WindowUnavailable;
    }
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(sdlWindow, &width, &height) || width <= 0 || height <= 0)
    {
        return SdlGpuBackendInitializeResult::WindowExtentUnsupported;
    }

    impl_->testMode = false;
    impl_->window = &window;
    impl_->sdlWindow = sdlWindow;
    SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0)
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::PropertiesCreationFailed;
    }
    const bool propertiesConfigured = ConfigureDeviceProperties(
        properties, window.applicationKeeper_.ApplicationConfig().rendererBackend);
    if (!propertiesConfigured)
    {
        SDL_DestroyProperties(properties);
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::PropertiesConfigurationFailed;
    }
    impl_->device = SDL_CreateGPUDeviceWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (impl_->device == nullptr)
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::DeviceCreationFailed;
    }
    if (!SDL_SetGPUAllowedFramesInFlight(impl_->device, 1))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::FramesInFlightConfigurationFailed;
    }
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, sdlWindow))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::WindowClaimFailed;
    }
    impl_->windowClaimed = true;

    const SDL_GPUTextureUsageFlags colorUsage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(impl_->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                      SDL_GPU_TEXTURETYPE_2D, colorUsage))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::UnsupportedColorFormat;
    }
    if (!SDL_GPUTextureSupportsSampleCount(impl_->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                           SDL_GPU_SAMPLECOUNT_1))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::UnsupportedSampleCount;
    }

    const SDL_GPUTextureFormat depthCandidates[] = {
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
    };
    for (const SDL_GPUTextureFormat candidate : depthCandidates)
    {
        if (SDL_GPUTextureSupportsFormat(impl_->device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) &&
            SDL_GPUTextureSupportsSampleCount(impl_->device, candidate, SDL_GPU_SAMPLECOUNT_1))
        {
            impl_->depthFormat = candidate;
            break;
        }
    }
    if (impl_->depthFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::UnsupportedDepthFormat;
    }

    const char *const driverName = SDL_GetGPUDeviceDriver(impl_->device);
    const SDL_GPUShaderFormat shaderFormats = SDL_GetGPUShaderFormats(impl_->device);
    if (!IsDriverShaderPair(driverName, shaderFormats))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::UnsupportedDriver;
    }
    if (std::string_view(driverName) == "direct3d12")
    {
        impl_->shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
    }
    else if (std::string_view(driverName) == "vulkan")
    {
        impl_->shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
    }
    else
    {
        impl_->shaderFormat = SDL_GPU_SHADERFORMAT_MSL;
    }
    impl_->swapchainFormat = SDL_GetGPUSwapchainTextureFormat(impl_->device, sdlWindow);
    if (!IsSdrSwapchainFormat(impl_->swapchainFormat))
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::UnsupportedSwapchainFormat;
    }
    const std::optional<AllocationLedger> ledger =
        CalculateLedger(DepthFormatBytes(impl_->depthFormat));
    if (!ledger || ledger->downloadTransferBytes > (std::numeric_limits<Uint32>::max)())
    {
        ClearNativeState(*impl_);
        return SdlGpuBackendInitializeResult::ArenaCapacityOverflow;
    }
    impl_->ledger = *ledger;
    impl_->deviceGeneration = impl_->nextDeviceGeneration++;
    impl_->ready = true;
    impl_->vsyncAvailable = true;
    impl_->vsyncEnabled = true;
    return SdlGpuBackendInitializeResult::Success;
}

SdlGpuBackendInitializeResult SdlGpuRenderBackend::InitializeInjected(AppWindow &window) noexcept
{
    const NativeTestConfig config = impl_->nativeTestConfig;
    if (config.windowWidth == 0 || config.windowHeight == 0)
    {
        return SdlGpuBackendInitializeResult::WindowExtentUnsupported;
    }
    if (!config.propertiesCreate)
    {
        return SdlGpuBackendInitializeResult::PropertiesCreationFailed;
    }
    if (!config.propertiesConfigure)
    {
        return SdlGpuBackendInitializeResult::PropertiesConfigurationFailed;
    }
    if (!config.deviceCreate)
    {
        return SdlGpuBackendInitializeResult::DeviceCreationFailed;
    }
    if (!config.framesInFlight)
    {
        return SdlGpuBackendInitializeResult::FramesInFlightConfigurationFailed;
    }
    if (!config.windowClaim)
    {
        return SdlGpuBackendInitializeResult::WindowClaimFailed;
    }
    if (!config.colorFormat)
    {
        return SdlGpuBackendInitializeResult::UnsupportedColorFormat;
    }
    if (!config.colorSampleCount)
    {
        return SdlGpuBackendInitializeResult::UnsupportedSampleCount;
    }
    SDL_GPUTextureFormat depthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (config.depth24)
    {
        depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    else if (config.depth32)
    {
        depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    else
    {
        return SdlGpuBackendInitializeResult::UnsupportedDepthFormat;
    }
    const char *driverName = nullptr;
    switch (config.driver)
    {
    case TestDriver::Direct3D12:
        driverName = "direct3d12";
        break;
    case TestDriver::Vulkan:
        driverName = "vulkan";
        break;
    case TestDriver::Metal:
        driverName = "metal";
        break;
    case TestDriver::Unknown:
        driverName = "unknown";
        break;
    }
    if (!IsDriverShaderPair(driverName, static_cast<SDL_GPUShaderFormat>(config.shaderFormats)))
    {
        return SdlGpuBackendInitializeResult::UnsupportedDriver;
    }
    const SDL_GPUTextureFormat swapchainFormat =
        config.swapchainFormat == 0 ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
                                    : static_cast<SDL_GPUTextureFormat>(config.swapchainFormat);
    if (!IsSdrSwapchainFormat(swapchainFormat))
    {
        return SdlGpuBackendInitializeResult::UnsupportedSwapchainFormat;
    }
    const std::optional<AllocationLedger> ledger = CalculateLedger(DepthFormatBytes(depthFormat));
    if (!ledger)
    {
        return SdlGpuBackendInitializeResult::ArenaCapacityOverflow;
    }
    if (!config.uploadArena || !config.downloadArena)
    {
        return SdlGpuBackendInitializeResult::ArenaAllocationFailed;
    }

    impl_->testMode = true;
    impl_->window = &window;
    impl_->sdlWindow = static_cast<SDL_Window *>(TestHandle(1));
    impl_->device = static_cast<SDL_GPUDevice *>(TestHandle(2));
    impl_->windowClaimed = true;
    impl_->depthFormat = depthFormat;
    impl_->swapchainFormat = swapchainFormat;
    impl_->shaderFormat = static_cast<SDL_GPUShaderFormat>(config.shaderFormats);
    impl_->ledger = *ledger;
    impl_->uploadArena = nullptr;
    impl_->uploadArenaBytes = 0;
    impl_->downloadArena = static_cast<SDL_GPUTransferBuffer *>(TestHandle(4));
    impl_->deviceGeneration = impl_->nextDeviceGeneration++;
    impl_->ready = true;
    impl_->vsyncAvailable = true;
    impl_->vsyncEnabled = true;
    return SdlGpuBackendInitializeResult::Success;
}

void SdlGpuRenderBackend::RetireStaleSessionTargets(const ApplicationRenderFrame &frame) noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    for (auto &record : impl_->sessionTargets)
    {
        if (!record.live || !IsFenceFree(*impl_, record.lastFence))
        {
            continue;
        }
        bool admitted = false;
        for (std::size_t index = 0; index < frame.CompositionCount(); ++index)
        {
            const ApplicationRenderFrame::CompositionRecord *const session =
                frame.Composition(index);
            if (session == nullptr)
            {
                continue;
            }
            if (record.key.id == session->id.RawValue() &&
                record.key.generation == session->generation.RawValue() &&
                record.key.surfaceGeneration == session->surfaceGeneration &&
                record.key.width == session->destination.width &&
                record.key.height == session->destination.height &&
                record.key.deviceGeneration == impl_->deviceGeneration)
            {
                admitted = true;
                break;
            }
        }
        if (!admitted)
        {
            ReleaseSessionTarget(*impl_, record);
        }
    }
    for (auto &record : impl_->geometries)
    {
        if (!record.live || !IsFenceFree(*impl_, record.lastFence))
        {
            continue;
        }
        bool admitted = false;
        for (std::size_t index = 0; index < frame.CompositionCount(); ++index)
        {
            const ApplicationRenderFrame::CompositionRecord *const session =
                frame.Composition(index);
            if (session != nullptr && record.asset.sessionId == session->id.RawValue() &&
                record.asset.generation == session->generation.RawValue())
            {
                admitted = true;
                break;
            }
        }
        if (!admitted)
        {
            impl_->geometrySlots.erase(record.asset);
            ReleaseGeometry(*impl_, record);
        }
    }
}

CompositorFrameResult SdlGpuRenderBackend::ReplayAndPresent(
    const ApplicationRenderFrame &frame) noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr || !impl_->ready ||
        impl_->pendingFence != nullptr || frame.FrameSequence() == 0 ||
        (impl_->lastFrameSequence != 0 && frame.FrameSequence() <= impl_->lastFrameSequence))
    {
        return CompositorFrameResult::Rejected;
    }

    impl_->lastReplayPhaseTimings = {};
    struct TotalTiming final
    {
        SdlGpuReplayPhaseTimings &timings;
        std::chrono::steady_clock::time_point started;
        ~TotalTiming()
        {
            timings.total = std::chrono::steady_clock::now() - started;
        }
    } totalTiming{impl_->lastReplayPhaseTimings, std::chrono::steady_clock::now()};
    const auto planningStarted = std::chrono::steady_clock::now();

    // Read-only format re-query for the frame transition; the cache mutation
    // is deferred until whole-frame validation succeeds.
    if (!QuerySwapchainFormatTransition())
    {
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }

    std::vector<CandidatePlan> &plans = impl_->framePlans;
    std::size_t planCount = 0;
    std::size_t completionBytes = 0;
    if (!PreflightFrame(frame, plans, planCount, completionBytes) ||
        !EnsureDownloadArenaCapacity(completionBytes))
    {
        impl_->swapchainRefreshPending = false;
        return CompositorFrameResult::Rejected;
    }
    std::size_t ownerUploadBytes = 0;
    for (std::size_t index = 0; index < planCount; ++index)
    {
        const auto total = CheckedAdd(ownerUploadBytes, plans[index].uploadBytes);
        if (!total)
        {
            impl_->swapchainRefreshPending = false;
            return CompositorFrameResult::Rejected;
        }
        ownerUploadBytes = *total;
    }
    std::size_t geometryUploadBytes = 0;
    std::size_t clearDrawBytes = 0;
    for (const CandidatePlan &plan : plans)
    {
        const auto *session = frame.Session(plan.sessionIndex);
        if (session == nullptr)
        {
            return CompositorFrameResult::Rejected;
        }
        const auto vertexBytes =
            CheckedMultiply(session->tape.Vertices().size(), sizeof(RenderTapeVertex));
        const auto indexBytes =
            CheckedMultiply(session->tape.Indices().size(), sizeof(std::uint32_t));
        const auto sessionBytes =
            vertexBytes && indexBytes ? CheckedAdd(*vertexBytes, *indexBytes) : std::nullopt;
        if (!sessionBytes)
        {
            return CompositorFrameResult::Rejected;
        }
        const auto geometryTotal = CheckedAdd(geometryUploadBytes, *sessionBytes);
        if (!geometryTotal)
        {
            return CompositorFrameResult::Rejected;
        }
        geometryUploadBytes = *geometryTotal;

        const auto persistentTotal =
            CheckedAdd(geometryUploadBytes, plan.persistentGeometryUploadBytes);
        if (!persistentTotal)
        {
            return CompositorFrameResult::Rejected;
        }
        geometryUploadBytes = *persistentTotal;

        bool firstClear = true;
        for (const RenderTapeEntry &entry : session->tape.Entries())
        {
            if (entry.kind != RenderTapeEntryKind::Clear)
            {
                continue;
            }
            const RenderTapeClear &clear = session->tape.Clears()[entry.index];
            const std::uint8_t encoding =
                ClearEncodingAtPosition(clear, {0, 0, plan.width, plan.height}, firstClear);
            firstClear = false;
            if ((encoding & 0xF0u) == 0)
            {
                continue;
            }
            const auto bytes = CheckedAdd(QuadVertexCount * sizeof(RenderTapeVertex),
                                          QuadIndexCount * sizeof(std::uint32_t));
            const auto clearTotal = bytes ? CheckedAdd(clearDrawBytes, *bytes) : std::nullopt;
            if (!clearTotal)
            {
                return CompositorFrameResult::Rejected;
            }
            clearDrawBytes = *clearTotal;
        }
    }
    const auto geometryTotal = CheckedAdd(geometryUploadBytes, clearDrawBytes);
    if (!geometryTotal)
    {
        return CompositorFrameResult::Rejected;
    }
    geometryUploadBytes = *geometryTotal;

    std::size_t skinningUploadBytes = 0;
    std::size_t drawInstanceUploadBytes = 0;
    for (const CandidatePlan &plan : plans)
    {
        const auto *session = frame.Session(plan.sessionIndex);
        const auto boneBytes = session == nullptr
                                   ? std::nullopt
                                   : CheckedMultiply(session->tape.BoneMatrices().size(),
                                                     sizeof(RenderTapeBoneMatrix));
        const auto boneTotal =
            boneBytes ? CheckedAdd(skinningUploadBytes, *boneBytes) : std::nullopt;
        const auto instanceCount = session == nullptr
                                       ? std::nullopt
                                       : CheckedAdd(session->tape.QuadInstances().size(),
                                                    session->tape.RigidInstances().size());
        const auto sixRowBytes =
            instanceCount ? CheckedMultiply(*instanceCount, sizeof(RenderTapeQuadInstance))
                          : std::nullopt;
        const auto particleBytes = session == nullptr
                                       ? std::nullopt
                                       : CheckedMultiply(session->tape.ParticleInstances().size(),
                                                         sizeof(RenderTapeParticleInstance));
        const auto trailRows = session == nullptr
                                   ? std::nullopt
                                   : CheckedAdd(session->tape.TrailSamples().size(),
                                                session->tape.TrailInstances().size());
        const auto trailBytes =
            trailRows ? CheckedMultiply(*trailRows, sizeof(RenderTapeTrailSample)) : std::nullopt;
        const auto spriteBytes =
            sixRowBytes && particleBytes ? CheckedAdd(*sixRowBytes, *particleBytes) : std::nullopt;
        const auto instanceBytes =
            spriteBytes && trailBytes ? CheckedAdd(*spriteBytes, *trailBytes) : std::nullopt;
        const auto instanceTotal =
            instanceBytes ? CheckedAdd(drawInstanceUploadBytes, *instanceBytes) : std::nullopt;
        if (!boneTotal || !instanceTotal)
        {
            return CompositorFrameResult::Rejected;
        }
        skinningUploadBytes = *boneTotal;
        drawInstanceUploadBytes = *instanceTotal;
    }

    std::size_t compositionQuads = frame.CompositionCount() + frame.OverlayRects().size();
    for (const WorkspaceOverlayLabel &label : frame.OverlayLabels())
    {
        const auto total = CheckedAdd(compositionQuads, label.text.size());
        if (!total)
        {
            return CompositorFrameResult::Rejected;
        }
        compositionQuads = *total;
    }
    const auto quadBytes = CheckedAdd(QuadVertexCount * sizeof(RenderTapeVertex),
                                      QuadIndexCount * sizeof(std::uint32_t));
    const auto overlayGeometryBytes =
        quadBytes ? CheckedMultiply(compositionQuads, *quadBytes) : std::nullopt;
    const bool needsOverlayAtlas =
        frame.CompositionCount() != 0 || !frame.OverlayRects().empty() ||
        std::any_of(
            frame.OverlayLabels().begin(), frame.OverlayLabels().end(),
            [](const WorkspaceOverlayLabel &label) noexcept { return !label.text.empty(); });
    const std::size_t atlasUploadBytes =
        needsOverlayAtlas && impl_->overlayAtlas == nullptr ? std::size_t{50 * 35 * 4} : 0;
    const auto overlayDrawBytes =
        overlayGeometryBytes ? CheckedAdd(*overlayGeometryBytes, atlasUploadBytes) : std::nullopt;
    if (!overlayDrawBytes || !EnsureBonePaletteBufferCapacity(skinningUploadBytes) ||
        !EnsureDrawInstanceBufferCapacity(drawInstanceUploadBytes) ||
        !EnsureUploadArenaCapacity(geometryUploadBytes, skinningUploadBytes,
                                   drawInstanceUploadBytes, ownerUploadBytes, *overlayDrawBytes))
    {
        impl_->swapchainRefreshPending = false;
        return CompositorFrameResult::Rejected;
    }
    // Frame transition: retire stale targets and apply a format-only change.
    // Only private swapchain-target pipelines change; device/session
    // generations and session-target pipelines are untouched.
    RetireStaleSessionTargets(frame);
    CommitSwapchainFormatTransition();
    impl_->lastReplayPhaseTimings.preflightAndPlanning =
        std::chrono::steady_clock::now() - planningStarted;

    FrameReservation *const reservation = impl_->reservation.get();
    if (reservation == nullptr)
    {
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    ResetReservationMetadata(*reservation);
    reservation->completionCheckpoint = impl_->completions.SaveCheckpoint();
    if (!impl_->completions.Prepare(completionBytes))
    {
        ResetReservationMetadata(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    reservation->completionPrepared = true;

    if (impl_->testMode)
    {
        ++impl_->replayAcquireCount;
        if (!impl_->nativeTestConfig.commandBufferAcquire)
        {
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        reservation->commandBuffer = static_cast<SDL_GPUCommandBuffer *>(TestHandle(0x8400));
    }
    else
    {
        reservation->commandBuffer = SDL_AcquireGPUCommandBuffer(impl_->device);
        ++impl_->replayAcquireCount;
        if (reservation->commandBuffer == nullptr)
        {
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
    }

    impl_->ledger.clearDrawBytes = clearDrawBytes;
    const auto encodeStarted = std::chrono::steady_clock::now();
    const bool encoded = EncodeFrame(frame, plans, planCount, *reservation);
    impl_->lastReplayPhaseTimings.encode = std::chrono::steady_clock::now() - encodeStarted;
    if (!encoded)
    {
        CancelReservation(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }

    struct SubmitTiming final
    {
        std::chrono::nanoseconds &output;
        std::chrono::steady_clock::time_point started;
        ~SubmitTiming()
        {
            output = std::chrono::steady_clock::now() - started;
        }
    } submitTiming{impl_->lastReplayPhaseTimings.presentAndSubmit,
                   std::chrono::steady_clock::now()};

    if (impl_->testMode)
    {
        if (!impl_->nativeTestConfig.swapchainWait)
        {
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        if (!impl_->nativeTestConfig.swapchainTexture)
        {
            CancelReservation(*reservation);
            if (!reservation->cancelSucceeded)
            {
                ClearNativeState(*impl_);
                return CompositorFrameResult::DeviceLost;
            }
            impl_->lastFrameSequence = frame.FrameSequence();
            return CompositorFrameResult::DroppedNoDrawable;
        }
        reservation->swapchainAcquired = true;
        if (!impl_->nativeTestConfig.submitFence)
        {
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        ++impl_->replaySubmitCount;
        const std::uint64_t fenceToken = frame.FrameSequence();
        reservation->fence = static_cast<SDL_GPUFence *>(TestHandle(fenceToken));
        impl_->pendingFence = reservation->fence;
        PublishReservation(frame, plans, planCount, *reservation, fenceToken);
        return CompositorFrameResult::Submitted;
    }

    SDL_GPUTexture *swapchainTexture = nullptr;
    Uint32 swapchainWidth = 0;
    Uint32 swapchainHeight = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(reservation->commandBuffer, impl_->sdlWindow,
                                               &swapchainTexture, &swapchainWidth,
                                               &swapchainHeight))
    {
        CancelReservation(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    if (swapchainTexture == nullptr)
    {
        CancelReservation(*reservation);
        if (!reservation->cancelSucceeded)
        {
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        impl_->lastFrameSequence = frame.FrameSequence();
        return CompositorFrameResult::DroppedNoDrawable;
    }
    reservation->swapchainAcquired = true;
    const SDL_GPUColorTargetInfo compositionTarget{swapchainTexture,
                                                   0,
                                                   0,
                                                   SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F},
                                                   SDL_GPU_LOADOP_CLEAR,
                                                   SDL_GPU_STOREOP_STORE,
                                                   nullptr,
                                                   0,
                                                   0,
                                                   false,
                                                   false,
                                                   0,
                                                   0};
    SDL_GPURenderPass *const compositionPass =
        SDL_BeginGPURenderPass(reservation->commandBuffer, &compositionTarget, 1, nullptr);
    if (compositionPass == nullptr)
    {
        CancelReservation(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    SDL_GPUBuffer *const storageBuffers[]{impl_->bonePaletteBuffer, impl_->drawInstanceBuffer,
                                          impl_->bonePaletteBuffer, impl_->bonePaletteBuffer};
    SDL_BindGPUVertexStorageBuffers(compositionPass, 0, storageBuffers,
                                    static_cast<Uint32>(std::size(storageBuffers)));
    RenderTapeUniformData uniform{
        {RenderTapeIdentityMatrix4x4, RenderTapeIdentityMatrix4x4, RenderTapeIdentityMatrix4x4},
        {{}, {}, {}, {}}};
    const SDL_GPUViewport compositionViewport{
        0.0F, 0.0F, static_cast<float>(swapchainWidth), static_cast<float>(swapchainHeight),
        0.0F, 1.0F};
    SDL_SetGPUViewport(compositionPass, &compositionViewport);
    const auto findPipeline = [&](const PipelineKey &key) noexcept -> SDL_GPUGraphicsPipeline * {
        for (std::size_t index = 0; index < reservation->pipelineCount; ++index)
        {
            if (reservation->pipelines[index].key == key)
            {
                return static_cast<SDL_GPUGraphicsPipeline *>(reservation->pipelines[index].native);
            }
        }
        return nullptr;
    };
    const auto findSampler = [&](RenderSamplerIntent intent) noexcept -> SDL_GPUSampler * {
        for (std::size_t index = 0; index < reservation->samplerCount; ++index)
        {
            if (reservation->samplers[index].key == intent)
            {
                return static_cast<SDL_GPUSampler *>(reservation->samplers[index].native);
            }
        }
        return nullptr;
    };
    SDL_GPUSampler *const compositionSampler =
        findSampler({LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge});
    const auto bindGeometry = [&]() noexcept {
        const SDL_GPUBufferBinding vertexBinding{reservation->vertexBuffer, 0};
        const SDL_GPUBufferBinding indexBinding{reservation->indexBuffer, 0};
        SDL_BindGPUVertexBuffers(compositionPass, 0, &vertexBinding, 1);
        SDL_BindGPUIndexBuffer(compositionPass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    };
    const auto drawQuad = [&](std::size_t quad, PipelineKey key, SDL_GPUTexture *texture,
                              SDL_GPUSampler *sampler, std::array<float, 4> mode) noexcept {
        SDL_GPUGraphicsPipeline *const pipeline = findPipeline(key);
        if (pipeline == nullptr || texture == nullptr || sampler == nullptr)
        {
            return false;
        }
        uniform.fragment.mode = mode;
        SDL_BindGPUGraphicsPipeline(compositionPass, pipeline);
        bindGeometry();
        SDL_PushGPUVertexUniformData(reservation->commandBuffer, 0, &uniform.vertex,
                                     sizeof(uniform.vertex));
        SDL_PushGPUFragmentUniformData(reservation->commandBuffer, 0, &uniform.fragment,
                                       sizeof(uniform.fragment));
        const SDL_GPUTextureSamplerBinding binding{texture, sampler};
        SDL_BindGPUFragmentSamplers(compositionPass, 0, &binding, 1);
        SDL_DrawGPUIndexedPrimitives(
            compositionPass, QuadIndexCount, 1,
            static_cast<Uint32>(reservation->compositionIndexOffset + quad * QuadIndexCount), 0, 0);
        return true;
    };
    std::size_t compositionQuad = 0;
    const PipelineKey compositionKey = CompositionPipelineKeyForPeer();
    for (std::size_t compositionIndex = 0; compositionIndex < frame.CompositionCount();
         ++compositionIndex)
    {
        const ApplicationRenderFrame::CompositionRecord *const composition =
            frame.Composition(compositionIndex);
        if (composition == nullptr)
        {
            SDL_EndGPURenderPass(compositionPass);
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        void *color = nullptr;
        for (std::size_t index = 0; index < reservation->targetCount; ++index)
        {
            const FrameReservation::Target &target = reservation->targets[index];
            if (target.sessionId == composition->id.RawValue() &&
                target.sessionGeneration == composition->generation.RawValue() &&
                target.surfaceGeneration == composition->surfaceGeneration &&
                target.width == composition->destination.width &&
                target.height == composition->destination.height)
            {
                color = target.color;
                break;
            }
        }
        for (const auto &target : impl_->sessionTargets)
        {
            if (color == nullptr && target.live && target.key.id == composition->id.RawValue() &&
                target.key.generation == composition->generation.RawValue() &&
                target.key.surfaceGeneration == composition->surfaceGeneration &&
                target.key.width == composition->destination.width &&
                target.key.height == composition->destination.height &&
                target.key.deviceGeneration == impl_->deviceGeneration)
            {
                color = target.color;
                break;
            }
        }
        if (color != nullptr &&
            !drawQuad(compositionQuad, compositionKey, static_cast<SDL_GPUTexture *>(color),
                      compositionSampler, {1.0F, 0.0F, -1.0F, 0.0F}))
        {
            SDL_EndGPURenderPass(compositionPass);
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
        ++compositionQuad;
    }
    PipelineKey overlayKey = OverlayPipelineKeyForPeer();
    PipelineKey labelKey = overlayKey;
    labelKey.shaderFamily = ShaderFamily::Label;
    if (reservation->compositionQuadCount > frame.CompositionCount() &&
        (impl_->overlayAtlas == nullptr || impl_->overlaySampler == nullptr))
    {
        SDL_EndGPURenderPass(compositionPass);
        CancelReservation(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    for (const WorkspaceOverlayRect &overlay : frame.OverlayRects())
    {
        if (!drawQuad(
                compositionQuad++, overlayKey, static_cast<SDL_GPUTexture *>(impl_->overlayAtlas),
                static_cast<SDL_GPUSampler *>(impl_->overlaySampler), {0.0F, 0.0F, -1.0F, 0.0F}))
        {
            SDL_EndGPURenderPass(compositionPass);
            CancelReservation(*reservation);
            ClearNativeState(*impl_);
            return CompositorFrameResult::DeviceLost;
        }
    }
    for (const WorkspaceOverlayLabel &label : frame.OverlayLabels())
    {
        for (std::size_t character = 0; character < label.text.size(); ++character)
        {
            if (!drawQuad(compositionQuad++, labelKey,
                          static_cast<SDL_GPUTexture *>(impl_->overlayAtlas),
                          static_cast<SDL_GPUSampler *>(impl_->overlaySampler),
                          {1.0F, 0.0F, -1.0F, 0.0F}))
            {
                SDL_EndGPURenderPass(compositionPass);
                CancelReservation(*reservation);
                ClearNativeState(*impl_);
                return CompositorFrameResult::DeviceLost;
            }
        }
    }
    SDL_EndGPURenderPass(compositionPass);
    ++impl_->replaySubmitCount;
    reservation->fence = SDL_SubmitGPUCommandBufferAndAcquireFence(reservation->commandBuffer);
    if (reservation->fence == nullptr)
    {
        CancelReservation(*reservation);
        ClearNativeState(*impl_);
        return CompositorFrameResult::DeviceLost;
    }
    impl_->pendingFence = reservation->fence;
    const std::uint64_t fenceToken =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(reservation->fence));
    PublishReservation(frame, plans, planCount, *reservation, fenceToken);
    return CompositorFrameResult::Submitted;
}

namespace
{
void RetireSubmittedFence(SdlGpuRenderBackend::Impl &impl) noexcept
{
    const auto token = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(impl.pendingFence));
    if (!impl.testMode)
        SDL_ReleaseGPUFence(impl.device, impl.pendingFence);
    impl.pendingFence = nullptr;
    impl.pendingFrameSequence = 0;
    impl.retiredFence = (std::max)(impl.retiredFence, token);
}
} // namespace

CompositorRetireResult SdlGpuRenderBackend::RetireCompletedFrame() noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr)
    {
        return CompositorRetireResult::DeviceLost;
    }
    if (impl_->pendingFence == nullptr)
    {
        return impl_->ready ? CompositorRetireResult::Ready : CompositorRetireResult::DeviceLost;
    }
    bool complete = false;
    if (impl_->testMode)
    {
        complete = impl_->nativeTestConfig.fenceComplete;
    }
    else
    {
        complete = SDL_QueryGPUFence(impl_->device, impl_->pendingFence);
    }
    if (!complete)
    {
        return CompositorRetireResult::Pending;
    }
    FrameReservation *const reservation = impl_->reservation.get();
    if (reservation == nullptr)
    {
        return CompositorRetireResult::DeviceLost;
    }
    const auto liveTarget = [&](SessionId id, SessionGeneration generation,
                                std::uint64_t surfaceGeneration, std::uint32_t width,
                                std::uint32_t height) noexcept {
        for (const auto &target : impl_->sessionTargets)
        {
            if (target.live && target.key.id == id.RawValue() &&
                target.key.generation == generation.RawValue() &&
                target.key.surfaceGeneration == surfaceGeneration && target.key.width == width &&
                target.key.height == height &&
                target.key.deviceGeneration == impl_->deviceGeneration)
            {
                return true;
            }
        }
        return false;
    };
    const auto liveTexture = [&](LogicalRenderAssetRef asset) noexcept {
        for (const auto &texture : impl_->textures)
        {
            if (texture.live && texture.key.asset == asset &&
                texture.key.deviceGeneration == impl_->deviceGeneration)
            {
                return true;
            }
        }
        return false;
    };
    std::byte *mappedDownload = nullptr;
    if (impl_->testMode && reservation->downloadBytes != 0 && !impl_->nativeTestConfig.downloadMap)
    {
        ClearNativeState(*impl_);
        return CompositorRetireResult::DeviceLost;
    }
    if (!impl_->testMode && reservation->downloadBytes != 0)
    {
        mappedDownload = static_cast<std::byte *>(
            SDL_MapGPUTransferBuffer(impl_->device, impl_->downloadArena, false));
        if (mappedDownload == nullptr)
        {
            ClearNativeState(*impl_);
            return CompositorRetireResult::DeviceLost;
        }
    }
    bool materialized = true;
    for (std::size_t index = 0; index < reservation->sessionCompletionCount; ++index)
    {
        const auto &value = reservation->sessionCompletions[index].value;
        if (!value.has_value() ||
            reservation->sessionCompletions[index].deviceGeneration != impl_->deviceGeneration ||
            value->frameSequence != impl_->pendingFrameSequence ||
            value->sessionId.RawValue() == 0 || value->generation.RawValue() == 0 ||
            value->surfaceGeneration == 0 ||
            !liveTarget(value->sessionId, value->generation, value->surfaceGeneration,
                        reservation->sessionCompletions[index].targetWidth,
                        reservation->sessionCompletions[index].targetHeight) ||
            !impl_->completions.TryAppendSession(*value))
        {
            materialized = false;
            break;
        }
    }
    for (std::size_t index = 0; materialized && index < reservation->requestCompletionCount;
         ++index)
    {
        auto &pending = reservation->requestCompletions[index];
        if (!pending.value.has_value())
        {
            materialized = false;
            break;
        }
        if (pending.deviceGeneration != impl_->deviceGeneration ||
            pending.value->frameSequence != impl_->pendingFrameSequence ||
            pending.value->sessionId.RawValue() == 0 || pending.value->generation.RawValue() == 0 ||
            pending.value->surfaceGeneration == 0 || pending.value->requestId == 0 ||
            !liveTarget(pending.value->sessionId, pending.value->generation,
                        pending.value->surfaceGeneration, pending.targetWidth,
                        pending.targetHeight) ||
            (pending.value->kind == RenderOwnerRequestKind::CopyTargetToLogicalTexture &&
             !liveTexture(pending.value->destination)))
        {
            materialized = false;
            break;
        }
        if (!pending.download)
        {
            materialized = impl_->completions.TryAppendRequest(*pending.value, {});
            continue;
        }
        const std::size_t bytes = pending.downloadBytes;
        if (bytes > impl_->completions.PayloadCapacityBytes() - impl_->completions.PayloadBytes() ||
            pending.width == 0 || pending.height == 0 ||
            bytes != static_cast<std::size_t>(pending.width) * pending.height * Rgba8BytesPerPixel)
        {
            materialized = false;
            break;
        }
        std::byte *const destination =
            impl_->completions.payload_.data() + impl_->completions.payloadBytesUsed_;
        const std::size_t sourceRowBytes =
            static_cast<std::size_t>(pending.width) * Rgba8BytesPerPixel;
        const bool flipRows = pending.verticallyFlipped;
        for (std::size_t row = 0; row < pending.height; ++row)
        {
            const std::size_t sourceRow = flipRows ? pending.height - 1 - row : row;
            if (mappedDownload == nullptr && reservation->testReadbackBytes == 0)
            {
                materialized = false;
                break;
            }
            const std::size_t destinationOffset = row * sourceRowBytes;
            if (mappedDownload == nullptr)
            {
                for (std::size_t byte = 0; byte < sourceRowBytes; ++byte)
                {
                    destination[destinationOffset + byte] =
                        reservation->testReadback[(sourceRow * sourceRowBytes + byte) %
                                                  reservation->testReadbackBytes];
                }
            }
            else
            {
                const std::byte *const source =
                    mappedDownload + pending.downloadOffset + sourceRow * sourceRowBytes;
                std::copy(source, source + sourceRowBytes, destination + destinationOffset);
            }
        }
        if (materialized)
        {
            RenderOwnerRequestCompletion completion = *pending.value;
            materialized = impl_->completions.TryAppendRequest(completion, {destination, bytes});
        }
    }
    if (mappedDownload != nullptr)
    {
        SDL_UnmapGPUTransferBuffer(impl_->device, impl_->downloadArena);
    }
    if (!materialized)
    {
        impl_->completions.Clear();
        ClearNativeState(*impl_);
        return CompositorRetireResult::DeviceLost;
    }
    RetireSubmittedFence(*impl_);
    ResetReservationMetadata(*reservation);
    if (impl_->pendingVSync.has_value())
    {
        (void)ApplyVSyncParameters(*impl_->pendingVSync);
    }
    return CompositorRetireResult::Ready;
}

const ApplicationRenderCompletionBatch &SdlGpuRenderBackend::RenderCompletions() const noexcept
{
    static const ApplicationRenderCompletionBatch empty;
    return impl_ == nullptr ? empty : impl_->completions;
}

void SdlGpuRenderBackend::ClearRenderCompletions() noexcept
{
    if (IsOwnerThreadForPeer() && impl_ != nullptr)
    {
        impl_->completions.Clear();
    }
}

bool SdlGpuRenderBackend::Recover(AppWindow &window) noexcept
{
    if (!IsOwnerThreadForPeer())
    {
        return false;
    }
    // Frame-sequence monotonicity survives the device generation change so a
    // retained pre-loss frame can never repair the new generation.
    const std::uint64_t lastSequence = impl_ == nullptr ? 0 : impl_->lastFrameSequence;
    const bool vsync = impl_ == nullptr ? true : impl_->pendingVSync.value_or(impl_->vsyncEnabled);
    Shutdown();
    const SdlGpuBackendInitializeResult result = Initialize(window);
    if (result == SdlGpuBackendInitializeResult::Success && impl_ != nullptr && lastSequence != 0)
    {
        impl_->lastFrameSequence = lastSequence;
    }
    if (result == SdlGpuBackendInitializeResult::Success && !vsync)
    {
        DisableVSync();
    }
    return result == SdlGpuBackendInitializeResult::Success;
}

void SdlGpuRenderBackend::Shutdown() noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr)
    {
        return;
    }
    if (impl_->reservation != nullptr)
    {
        ResetReservationMetadata(*impl_->reservation);
    }
    if (impl_->pendingFence != nullptr)
    {
        // Retire healthy submitted work before teardown; an incomplete fence
        // belongs to a lost generation and is discarded with it.
        (void)RetireCompletedFrame();
    }
    ClearNativeState(*impl_);
    impl_->lastFrameSequence = 0;
}

bool SdlGpuRenderBackend::IsReady() const noexcept
{
    return impl_ != nullptr && impl_->ready;
}

SdlGpuMemoryStats SdlGpuRenderBackend::MemoryUsage() const noexcept
{
    SdlGpuMemoryStats stats;
    if (impl_ == nullptr)
    {
        return stats;
    }
    stats.uploadArenaBytes = impl_->uploadArenaBytes;
    stats.downloadArenaBytes = impl_->ledger.downloadTransferBytes;
    stats.completionPayloadBytes = impl_->completions.PayloadCapacityBytes();
    stats.residentTextureBytes = impl_->residentTextureBytes;
    stats.geometryBufferBytes = impl_->geometryVertexBufferBytes + impl_->geometryIndexBufferBytes +
                                impl_->bonePaletteBufferBytes + impl_->drawInstanceBufferBytes;
    for (const auto &geometry : impl_->geometries)
    {
        stats.geometryBufferBytes += geometry.lightBytes;
        stats.uploadArenaBytes += geometry.lightBytes;
    }
    for (const Impl::SessionTargetRecord &target : impl_->sessionTargets)
    {
        if (target.live)
        {
            stats.sessionTargetBytes += static_cast<std::size_t>(target.key.width) *
                                        target.key.height *
                                        (Rgba8BytesPerPixel + DepthFormatBytes(impl_->depthFormat));
        }
    }
    return stats;
}

const SdlGpuReplayPhaseTimings &SdlGpuRenderBackend::LastReplayPhaseTimings() const noexcept
{
    static const SdlGpuReplayPhaseTimings empty;
    return impl_ == nullptr ? empty : impl_->lastReplayPhaseTimings;
}

std::uint64_t SdlGpuRenderBackend::DeviceGeneration() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->deviceGeneration;
}

const wchar_t *SdlGpuRenderBackend::DriverName() const noexcept
{
    if (impl_ == nullptr)
    {
        return L"unknown";
    }
    switch (impl_->shaderFormat)
    {
    case SDL_GPU_SHADERFORMAT_DXIL:
        return L"direct3d12";
    case SDL_GPU_SHADERFORMAT_SPIRV:
        return L"vulkan";
    case SDL_GPU_SHADERFORMAT_MSL:
        return L"metal";
    default:
        return L"unknown";
    }
}

const wchar_t *SdlGpuRenderBackend::ShaderArtifactName() const noexcept
{
    if (impl_ == nullptr)
    {
        return L"unknown";
    }
    switch (impl_->shaderFormat)
    {
    case SDL_GPU_SHADERFORMAT_DXIL:
        return L"DXIL";
    case SDL_GPU_SHADERFORMAT_SPIRV:
        return L"SPIR-V";
    case SDL_GPU_SHADERFORMAT_MSL:
        return L"MSL";
    default:
        return L"unknown";
    }
}

const wchar_t *SdlGpuRenderBackend::DepthStencilFormatName() const noexcept
{
    if (impl_ == nullptr)
    {
        return L"unknown";
    }
    switch (impl_->depthFormat)
    {
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
        return L"D24_UNORM_S8_UINT";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
        return L"D32_FLOAT_S8_UINT";
    default:
        return L"unknown";
    }
}

const wchar_t *SdlGpuRenderBackend::SwapchainFormatName() const noexcept
{
    if (impl_ == nullptr)
    {
        return L"unknown";
    }
    switch (impl_->swapchainFormat)
    {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
        return L"R8G8B8A8_UNORM";
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
        return L"B8G8R8A8_UNORM";
    default:
        return L"unknown";
    }
}

void SdlGpuRenderBackend::InitVSync() noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr)
    {
        return;
    }
    if (!impl_->ready)
    {
        impl_->vsyncAvailable = false;
        return;
    }
    impl_->vsyncAvailable = ApplyVSyncParameters(true);
}

bool SdlGpuRenderBackend::IsVSyncAvailable() const noexcept
{
    return impl_ != nullptr && impl_->vsyncAvailable;
}

bool SdlGpuRenderBackend::IsVSyncEnabled() const noexcept
{
    return impl_ != nullptr && impl_->vsyncEnabled;
}

void SdlGpuRenderBackend::EnableVSync() noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr || !impl_->ready)
    {
        return;
    }
    (void)ApplyVSyncParameters(true);
}

void SdlGpuRenderBackend::DisableVSync() noexcept
{
    if (!IsOwnerThreadForPeer() || impl_ == nullptr || !impl_->ready)
    {
        return;
    }
    if (impl_->testMode && !impl_->nativeTestConfig.immediatePresentMode)
    {
        return;
    }
    if (!impl_->testMode && !SDL_WindowSupportsGPUPresentMode(impl_->device, impl_->sdlWindow,
                                                              SDL_GPU_PRESENTMODE_IMMEDIATE))
    {
        return;
    }
    (void)ApplyVSyncParameters(false);
}

// Read-only swapchain-format re-query for the frame transition between
// AppWindow/Workspace layout changes and replay. The queried value only
// affects pipeline keys; the cache mutation is deferred to the commit step so
// a whole-frame rejection leaves every store untouched.
bool SdlGpuRenderBackend::QuerySwapchainFormatTransition() noexcept
{
    if (impl_ == nullptr || !impl_->ready)
    {
        return false;
    }
    impl_->swapchainRefreshPending = false;
    SDL_GPUTextureFormat nextFormat;
    if (impl_->testMode)
    {
        nextFormat =
            impl_->nativeTestConfig.requerySwapchainFormat == 0
                ? impl_->swapchainFormat
                : static_cast<SDL_GPUTextureFormat>(impl_->nativeTestConfig.requerySwapchainFormat);
    }
    else
    {
        if (impl_->device == nullptr || impl_->sdlWindow == nullptr)
        {
            return false;
        }
        nextFormat = SDL_GetGPUSwapchainTextureFormat(impl_->device, impl_->sdlWindow);
    }
    if (!IsSdrSwapchainFormat(nextFormat))
    {
        return false;
    }
    if (nextFormat == impl_->swapchainFormat)
    {
        return true;
    }
    impl_->queriedSwapchainFormat = nextFormat;
    impl_->swapchainRefreshPending = true;
    return true;
}

void SdlGpuRenderBackend::CommitSwapchainFormatTransition() noexcept
{
    if (impl_ == nullptr || !impl_->swapchainRefreshPending)
    {
        return;
    }
    impl_->swapchainRefreshPending = false;
    impl_->swapchainFormat = impl_->queriedSwapchainFormat;
    InvalidateSwapchainPipelines();
}

bool SdlGpuRenderBackend::ApplyVSyncParameters(bool enable) noexcept
{
    if (impl_ == nullptr || !impl_->ready)
    {
        return false;
    }
    if (impl_->pendingFence != nullptr)
    {
        impl_->pendingVSync = enable;
        return true;
    }
    impl_->pendingVSync.reset();
    const SDL_GPUPresentMode mode =
        enable ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
    if (impl_->testMode)
    {
        if (!impl_->nativeTestConfig.swapchainParameters)
        {
            return false;
        }
    }
    else if (!SDL_SetGPUSwapchainParameters(impl_->device, impl_->sdlWindow,
                                          SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot change VSync: %s", SDL_GetError());
        return false;
    }
    if (!QuerySwapchainFormatTransition())
    {
        return false;
    }
    CommitSwapchainFormatTransition();
    impl_->vsyncAvailable = true;
    impl_->vsyncEnabled = enable;
    return true;
}

void SdlGpuRenderBackend::InvalidateSwapchainPipelines() noexcept
{
    if (impl_ == nullptr)
    {
        return;
    }
    for (auto &record : impl_->pipelines)
    {
        if (record.live &&
            (record.key.shaderFamily == ShaderFamily::Composition ||
             record.key.shaderFamily == ShaderFamily::OverlayRect ||
             record.key.shaderFamily == ShaderFamily::Label) &&
            IsFenceFree(*impl_, record.lastFence))
        {
            ReleasePipeline(*impl_, record);
        }
    }
}

SdlGpuRenderBackend::NativeTestConfig &SdlGpuRenderBackend::NativeConfigForPeer() noexcept
{
    return impl_->nativeTestConfig;
}

const SdlGpuRenderBackend::AllocationLedger *SdlGpuRenderBackend::LedgerForPeer() const noexcept
{
    return impl_ == nullptr ? nullptr : &impl_->ledger;
}

std::size_t SdlGpuRenderBackend::PendingGeometryUploadBytesForPeer() const noexcept
{
    if (impl_ == nullptr || impl_->reservation == nullptr)
    {
        return 0;
    }
    std::size_t bytes = 0;
    const FrameReservation &reservation = *impl_->reservation;
    for (std::size_t index = 0; index < reservation.geometryCount; ++index)
    {
        const auto &pending = reservation.geometries[index];
        if (!pending.reuse)
        {
            bytes += pending.vertexCount * sizeof(RenderTapeVertex) +
                     pending.indexCount * sizeof(std::uint32_t) +
                     (pending.lease->terrainCells == nullptr
                          ? 0
                          : pending.lease->terrainCells->size() * sizeof(RenderTapeTerrainCell)) +
                     (pending.lease->terrainInstances == nullptr
                          ? 0
                          : pending.lease->terrainInstances->size() *
                                sizeof(RenderTapeTerrainInstance));
        }
    }
    return bytes;
}

std::optional<SdlGpuRenderBackend::SamplerPolicy> SdlGpuRenderBackend::SamplerPolicyForPeer(
    RenderSamplerIntent intent) const noexcept
{
    return MakeSamplerPolicy(intent);
}

std::optional<SdlGpuRenderBackend::PipelinePolicy> SdlGpuRenderBackend::PipelinePolicyForPeer(
    const PipelineKey &key) const noexcept
{
    return MakePipelinePolicy(key);
}

std::optional<std::uint32_t> SdlGpuRenderBackend::ColorWriteMaskForPeer(
    const PipelineKey &key) const noexcept
{
    if (!MakePipelinePolicy(key))
    {
        return std::nullopt;
    }
    return ToGpuColorWriteMask(key.colorWriteMask);
}

SdlGpuRenderBackend::PipelineKey SdlGpuRenderBackend::DefaultPipelineKeyForPeer() const noexcept
{
    PipelineKey key;
    if (impl_ != nullptr)
    {
        key.colorFormat = static_cast<std::uint32_t>(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        key.depthFormat = static_cast<std::uint32_t>(impl_->depthFormat);
        key.deviceGeneration = impl_->deviceGeneration;
    }
    key.vertexLayout = CanonicalVertexLayout;
    return key;
}

SdlGpuRenderBackend::PipelineKey SdlGpuRenderBackend::CompositionPipelineKeyForPeer() const noexcept
{
    PipelineKey key = DefaultPipelineKeyForPeer();
    key.shaderFamily = ShaderFamily::Composition;
    key.hasDepthStencilTarget = false;
    key.depthFormat = 0;
    key.depthTestEnable = false;
    key.depthWriteEnable = false;
    key.stencilEnable = false;
    key.cullEnable = false;
    if (impl_ != nullptr)
    {
        key.colorFormat = static_cast<std::uint32_t>(impl_->swapchainRefreshPending
                                                         ? impl_->queriedSwapchainFormat
                                                         : impl_->swapchainFormat);
    }
    return key;
}

SdlGpuRenderBackend::PipelineKey SdlGpuRenderBackend::OverlayPipelineKeyForPeer() const noexcept
{
    PipelineKey key = CompositionPipelineKeyForPeer();
    key.shaderFamily = ShaderFamily::OverlayRect;
    key.blendEnable = true;
    key.blendSource = RenderBlendFactor::SrcAlpha;
    key.blendDestination = RenderBlendFactor::OneMinusSrcAlpha;
    return key;
}

SdlGpuRenderBackend::PipelineKey SdlGpuRenderBackend::PipelineVariantForPeer(
    std::size_t index) const noexcept
{
    PipelineKey key = DefaultPipelineKeyForPeer();
    const auto take = [&index](std::size_t radix) noexcept {
        const std::size_t value = index % radix;
        index /= radix;
        return value;
    };
    key.shaderFamily = static_cast<ShaderFamily>(take(4));
    key.topology = static_cast<RenderIndexTopology>(take(2) + 1);
    key.samplerUse = take(2) != 0;
    key.blendEnable = take(2) != 0;
    key.blendSource = static_cast<RenderBlendFactor>(take(6));
    key.blendDestination = static_cast<RenderBlendFactor>(take(6));
    key.depthTestEnable = take(2) != 0;
    key.depthWriteEnable = take(2) != 0;
    key.depthCompare = static_cast<RenderCompareFunction>(take(4));
    key.stencilEnable = take(2) != 0;
    key.stencilCompare = static_cast<RenderCompareFunction>(take(4));
    key.stencilFail = static_cast<RenderStencilOperation>(take(3));
    key.stencilDepthFail = static_cast<RenderStencilOperation>(take(3));
    key.stencilPass = static_cast<RenderStencilOperation>(take(3));
    key.cullEnable = take(2) != 0;
    key.cullFace = static_cast<RenderCullFace>(take(2));
    key.frontFace = static_cast<RenderFrontFace>(take(2));
    if (key.shaderFamily == ShaderFamily::Composition && impl_ != nullptr)
    {
        key.colorFormat = static_cast<std::uint32_t>(impl_->swapchainFormat);
        key.hasDepthStencilTarget = false;
        key.depthFormat = 0;
    }
    return key;
}

bool SdlGpuRenderBackend::PrepareCompletionForPeer(std::size_t bytes) noexcept
{
    return impl_ != nullptr && impl_->completions.Prepare(bytes);
}

std::size_t SdlGpuRenderBackend::CompletionCapacityForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->completions.PayloadCapacityBytes();
}

bool SdlGpuRenderBackend::ReserveSamplerForPeer(RenderSamplerIntent intent,
                                                std::size_t &slot) noexcept
{
    slot = 0;
    if (impl_ == nullptr || !impl_->ready || !IsValid(intent) ||
        !MakeSamplerPolicy(intent).has_value())
    {
        return false;
    }
    for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
    {
        auto &record = impl_->samplers[index];
        if (record.live && record.key == intent &&
            record.deviceGeneration == impl_->deviceGeneration)
        {
            slot = index;
            return true;
        }
    }
    std::size_t selected = impl_->samplers.size();
    for (std::size_t index = 0; index < impl_->samplers.size(); ++index)
    {
        if (!impl_->samplers[index].live)
        {
            selected = index;
            break;
        }
    }
    if (selected == impl_->samplers.size())
    {
        try
        {
            impl_->samplers.emplace_back();
        }
        catch (...)
        {
            return false;
        }
    }
    {
        auto &record = impl_->samplers[selected];
        if (record.live)
        {
            ReleaseSampler(*impl_, record);
        }
        record.live = true;
        record.key = intent;
        record.deviceGeneration = impl_->deviceGeneration;
        if (!impl_->testMode)
        {
            const SamplerPolicy policy = *MakeSamplerPolicy(intent);
            SDL_GPUSamplerCreateInfo info{};
            info.min_filter = static_cast<SDL_GPUFilter>(policy.minFilter);
            info.mag_filter = static_cast<SDL_GPUFilter>(policy.magFilter);
            info.mipmap_mode = static_cast<SDL_GPUSamplerMipmapMode>(policy.mipmapMode);
            info.address_mode_u = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeU);
            info.address_mode_v = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeV);
            info.address_mode_w = static_cast<SDL_GPUSamplerAddressMode>(policy.addressModeW);
            info.mip_lod_bias = policy.mipLodBias;
            info.max_anisotropy = policy.maxAnisotropy;
            info.compare_op = static_cast<SDL_GPUCompareOp>(policy.compareOp);
            info.min_lod = policy.minLod;
            info.max_lod = policy.maxLod;
            info.enable_anisotropy = policy.enableAnisotropy;
            info.enable_compare = policy.enableCompare;
            info.props = static_cast<SDL_PropertiesID>(policy.props);
            record.native = SDL_CreateGPUSampler(impl_->device, &info);
            if (record.native == nullptr)
            {
                record = {};
                return false;
            }
        }
        else
        {
            record.native = TestHandle(0x10 + selected);
        }
        slot = selected;
        return true;
    }
}

bool SdlGpuRenderBackend::ReserveTextureForPeer(LogicalRenderAssetRef asset, std::size_t width,
                                                std::size_t height, std::size_t byteCount,
                                                std::size_t &slot) noexcept
{
    slot = 0;
    if (impl_ == nullptr || !impl_->ready || !IsValid(asset) || width == 0 || height == 0 ||
        width > (std::numeric_limits<std::uint32_t>::max)() ||
        height > (std::numeric_limits<std::uint32_t>::max)() ||
        !IsExactRgba8ByteCount(static_cast<std::uint32_t>(width),
                               static_cast<std::uint32_t>(height), byteCount))
    {
        return false;
    }
    const Impl::TextureKey key{asset, impl_->deviceGeneration};
    for (std::size_t index = 0; index < impl_->textures.size(); ++index)
    {
        auto &record = impl_->textures[index];
        if (record.live && record.key == key)
        {
            if (record.width != width || record.height != height ||
                record.residentBytes != byteCount)
            {
                return false;
            }
            slot = index;
            return true;
        }
    }
    std::size_t emptySlot = impl_->textures.size();
    for (std::size_t index = 0; index < impl_->textures.size(); ++index)
    {
        const auto &candidate = impl_->textures[index];
        if (!candidate.live)
        {
            if (emptySlot == impl_->textures.size())
            {
                emptySlot = index;
            }
            continue;
        }
    }
    std::size_t selected = emptySlot;
    if (selected == impl_->textures.size())
    {
        try
        {
            impl_->textures.emplace_back();
        }
        catch (...)
        {
            return false;
        }
    }

    void *incomingNative = nullptr;
    if (!impl_->testMode)
    {
        const SDL_GPUTextureCreateInfo info{SDL_GPU_TEXTURETYPE_2D,
                                            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                            SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                            static_cast<std::uint32_t>(width),
                                            static_cast<std::uint32_t>(height),
                                            1,
                                            1,
                                            SDL_GPU_SAMPLECOUNT_1,
                                            0};
        incomingNative = SDL_CreateGPUTexture(impl_->device, &info);
        if (incomingNative == nullptr)
        {
            return false;
        }
    }
    else
    {
        if (!impl_->nativeTestConfig.textureCreate)
        {
            return false;
        }
        incomingNative = TestHandle(0x100 + selected);
    }

    auto &record = impl_->textures[selected];
    if (record.live)
    {
        impl_->residentTextureBytes -= record.residentBytes;
        if (record.key.asset.id.value < impl_->textureSlotsByAssetId.size())
        {
            impl_->textureSlotsByAssetId[record.key.asset.id.value] =
                (std::numeric_limits<std::size_t>::max)();
        }
        ReleaseTexture(*impl_, record);
    }
    const auto residentBytes = CheckedAdd(impl_->residentTextureBytes, byteCount);
    if (!residentBytes)
    {
        if (!impl_->testMode)
        {
            SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(incomingNative));
        }
        return false;
    }

    record.live = true;
    record.key = key;
    record.width = static_cast<std::uint32_t>(width);
    record.height = static_cast<std::uint32_t>(height);
    record.residentBytes = byteCount;
    record.native = incomingNative;
    impl_->residentTextureBytes = *residentBytes;
    try
    {
        if (asset.id.value >= impl_->textureSlotsByAssetId.size())
        {
            impl_->textureSlotsByAssetId.resize(static_cast<std::size_t>(asset.id.value + 1),
                                                (std::numeric_limits<std::size_t>::max)());
        }
    }
    catch (...)
    {
        if (!impl_->testMode)
        {
            SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(incomingNative));
        }
        record = {};
        impl_->residentTextureBytes -= byteCount;
        return false;
    }
    impl_->textureSlotsByAssetId[asset.id.value] = selected;
    slot = selected;
    return true;
}

bool SdlGpuRenderBackend::ReserveSessionTargetForPeer(SessionId id, SessionGeneration generation,
                                                      std::uint64_t surfaceGeneration,
                                                      std::size_t width, std::size_t height,
                                                      std::size_t &slot) noexcept
{
    slot = 0;
    if (impl_ == nullptr || !impl_->ready || id.RawValue() == 0 || generation.RawValue() == 0 ||
        surfaceGeneration == 0 || width == 0 || height == 0 ||
        width > (std::numeric_limits<std::uint32_t>::max)() ||
        height > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    const Impl::SessionTargetKey key{id.RawValue(),
                                     generation.RawValue(),
                                     surfaceGeneration,
                                     static_cast<std::uint32_t>(width),
                                     static_cast<std::uint32_t>(height),
                                     impl_->deviceGeneration};
    for (std::size_t index = 0; index < impl_->sessionTargets.size(); ++index)
    {
        if (impl_->sessionTargets[index].live && impl_->sessionTargets[index].key == key)
        {
            slot = index;
            return true;
        }
    }
    std::size_t selected = impl_->sessionTargets.size();
    for (std::size_t index = 0; index < impl_->sessionTargets.size(); ++index)
    {
        if (!impl_->sessionTargets[index].live)
        {
            selected = index;
            break;
        }
    }
    if (selected == impl_->sessionTargets.size())
    {
        try
        {
            impl_->sessionTargets.emplace_back();
        }
        catch (...)
        {
            return false;
        }
    }
    {
        auto &record = impl_->sessionTargets[selected];
        if (record.live)
        {
            ReleaseSessionTarget(*impl_, record);
        }
        record.live = true;
        record.key = key;
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.targetColorCreate ||
                !impl_->nativeTestConfig.targetDepthCreate)
            {
                record = {};
                return false;
            }
            record.color = TestHandle(0x200 + selected * 2);
            record.depth = TestHandle(0x201 + selected * 2);
        }
        else
        {
            const SDL_GPUTextureCreateInfo colorInfo{SDL_GPU_TEXTURETYPE_2D,
                                                     SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                                     SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                                         SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                                     record.key.width,
                                                     record.key.height,
                                                     1,
                                                     1,
                                                     SDL_GPU_SAMPLECOUNT_1,
                                                     0};
            const SDL_GPUTextureCreateInfo depthInfo{SDL_GPU_TEXTURETYPE_2D,
                                                     impl_->depthFormat,
                                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                                     record.key.width,
                                                     record.key.height,
                                                     1,
                                                     1,
                                                     SDL_GPU_SAMPLECOUNT_1,
                                                     0};
            record.color = SDL_CreateGPUTexture(impl_->device, &colorInfo);
            if (record.color == nullptr)
            {
                record = {};
                return false;
            }
            record.depth = SDL_CreateGPUTexture(impl_->device, &depthInfo);
            if (record.depth == nullptr)
            {
                SDL_ReleaseGPUTexture(impl_->device, static_cast<SDL_GPUTexture *>(record.color));
                record = {};
                return false;
            }
        }
        slot = selected;
        return true;
    }
}

bool SdlGpuRenderBackend::ReservePipelineForPeer(const PipelineKey &key, std::size_t &slot) noexcept
{
    slot = 0;
    const std::optional<PipelinePolicy> policy = MakePipelinePolicy(key);
    if (!policy || impl_ == nullptr)
    {
        return false;
    }
    for (std::size_t index = 0; index < impl_->pipelines.size(); ++index)
    {
        auto &record = impl_->pipelines[index];
        if (record.live && record.key == key)
        {
            slot = index;
            return true;
        }
    }
    std::size_t selected = impl_->pipelines.size();
    for (std::size_t index = 0; index < impl_->pipelines.size(); ++index)
    {
        if (!impl_->pipelines[index].live)
        {
            selected = index;
            break;
        }
    }
    if (selected == impl_->pipelines.size())
    {
        try
        {
            impl_->pipelines.emplace_back();
        }
        catch (...)
        {
            return false;
        }
    }
    {
        auto &record = impl_->pipelines[selected];
        if (record.live)
        {
            ReleasePipeline(*impl_, record);
        }
        record.live = true;
        record.key = key;
        record.policy = *policy;
        if (impl_->testMode)
        {
            if (!impl_->nativeTestConfig.pipelineCreate)
            {
                record = {};
                return false;
            }
            record.native = TestHandle(0x300 + selected);
        }
        else if (!CreatePipelineNative(key, *policy, record.native))
        {
            record = {};
            return false;
        }
        slot = selected;
        return true;
    }
}

void SdlGpuRenderBackend::MarkFenceForPeer(std::uint64_t fence) noexcept
{
    if (impl_ == nullptr || fence == 0)
    {
        return;
    }
    for (auto &record : impl_->sessionTargets)
    {
        if (record.live)
        {
            record.lastFence = fence;
        }
    }
    for (auto &record : impl_->textures)
    {
        if (record.live)
        {
            record.lastFence = fence;
        }
    }
    for (auto &record : impl_->samplers)
    {
        if (record.live)
        {
            record.lastFence = fence;
        }
    }
    for (auto &record : impl_->pipelines)
    {
        if (record.live)
        {
            record.lastFence = fence;
        }
    }
}

void SdlGpuRenderBackend::RetireFenceForPeer(std::uint64_t fence) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->retiredFence = (std::max)(impl_->retiredFence, fence);
    }
}

std::size_t SdlGpuRenderBackend::LiveSamplerCountForPeer() const noexcept
{
    if (impl_ == nullptr)
    {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count_if(impl_->samplers.begin(), impl_->samplers.end(),
                      [](const auto &record) noexcept { return record.live; }));
}

std::size_t SdlGpuRenderBackend::LiveTextureCountForPeer() const noexcept
{
    if (impl_ == nullptr)
    {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count_if(impl_->textures.begin(), impl_->textures.end(),
                      [](const auto &record) noexcept { return record.live; }));
}

std::size_t SdlGpuRenderBackend::LiveSessionTargetCountForPeer() const noexcept
{
    if (impl_ == nullptr)
    {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count_if(impl_->sessionTargets.begin(), impl_->sessionTargets.end(),
                      [](const auto &record) noexcept { return record.live; }));
}

std::size_t SdlGpuRenderBackend::LivePipelineCountForPeer() const noexcept
{
    if (impl_ == nullptr)
    {
        return 0;
    }
    return static_cast<std::size_t>(
        std::count_if(impl_->pipelines.begin(), impl_->pipelines.end(),
                      [](const auto &record) noexcept { return record.live; }));
}

std::size_t SdlGpuRenderBackend::ResidentTextureBytesForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->residentTextureBytes;
}

bool SdlGpuRenderBackend::IsOwnerThreadForPeer() const noexcept
{
    return std::this_thread::get_id() == ownerThread_;
}

std::optional<SdlGpuRenderBackend::GpuRect> SdlGpuRenderBackend::ViewportForPeer(
    RenderTapeRect rect, std::uint32_t surfaceHeight, float minDepth, float maxDepth) const noexcept
{
    return MapGpuRect(rect, surfaceHeight, minDepth, maxDepth);
}

std::optional<SdlGpuRenderBackend::GpuRect> SdlGpuRenderBackend::ScissorForPeer(
    RenderTapeRect rect, std::uint32_t surfaceHeight) const noexcept
{
    return MapGpuRect(rect, surfaceHeight, 0.0F, 1.0F);
}

std::optional<SdlGpuRenderBackend::GpuRect> SdlGpuRenderBackend::TransferRectForPeer(
    RenderTapeRect rect, std::uint32_t surfaceHeight) const noexcept
{
    return MapGpuRect(rect, surfaceHeight, 0.0F, 1.0F);
}

std::optional<float> SdlGpuRenderBackend::ClipDepthForPeer(float clipZ, float clipW) const noexcept
{
    return ConvertClipDepth(clipZ, clipW);
}

std::uint8_t SdlGpuRenderBackend::ClearEncodingForPeer(const RenderTapeClear &clear,
                                                       RenderTapeRect target) noexcept
{
    return ClearEncoding(clear, target);
}

std::uint8_t SdlGpuRenderBackend::AlphaCompareCodeForPeer(RenderCompareFunction compare) noexcept
{
    return AlphaCompareCode(compare);
}

std::array<float, 4> SdlGpuRenderBackend::ApplyTextureEnvironmentForPeer(
    RenderTextureEnvironment environment, std::array<float, 4> color,
    std::array<float, 4> sampled) noexcept
{
    return ApplyTextureEnvironment(environment, color, sampled);
}

std::array<float, 4> SdlGpuRenderBackend::LightingColorForPeer(bool enabled) noexcept
{
    return LightingColor(enabled);
}

std::array<float, 4> SdlGpuRenderBackend::SessionCompositionUvForPeer() noexcept
{
    return SessionCompositionUv();
}

std::array<float, 4> SdlGpuRenderBackend::GlyphUvForPeer(wchar_t character) noexcept
{
    return GlyphUv(character);
}

std::size_t SdlGpuRenderBackend::ReplayAcquireCountForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->replayAcquireCount;
}

std::size_t SdlGpuRenderBackend::ReplaySubmitCountForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->replaySubmitCount;
}

std::size_t SdlGpuRenderBackend::ReplayCancelCountForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->replayCancelCount;
}

bool SdlGpuRenderBackend::EnsureGeometryBufferCapacityForPeer(std::size_t vertexBytes,
                                                              std::size_t indexBytes) noexcept
{
    return EnsureGeometryBufferCapacity(vertexBytes, indexBytes);
}

std::size_t SdlGpuRenderBackend::DrawInstanceBufferAllocationCountForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->drawInstanceBufferAllocationCount;
}

std::size_t SdlGpuRenderBackend::GeometryBufferAllocationCountForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->geometryBufferAllocationCount;
}

std::size_t SdlGpuRenderBackend::GeometryVertexBufferCapacityForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->geometryVertexBufferBytes;
}

std::size_t SdlGpuRenderBackend::GeometryIndexBufferCapacityForPeer() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->geometryIndexBufferBytes;
}

std::size_t SdlGpuRenderBackend::OperationCountForPeer() const noexcept
{
    return impl_ == nullptr || impl_->reservation == nullptr ? 0
                                                             : impl_->reservation->operationCount;
}

std::uint8_t SdlGpuRenderBackend::OperationKindForPeer(std::size_t index) const noexcept
{
    if (impl_ == nullptr || impl_->reservation == nullptr ||
        index >= impl_->reservation->operationCount)
    {
        return 0;
    }
    return impl_->reservation->operationKinds[index];
}

std::size_t SdlGpuRenderBackend::ClearDrawCountForPeer() const noexcept
{
    return impl_ == nullptr || impl_->reservation == nullptr ? 0
                                                             : impl_->reservation->clearDrawCount;
}

std::optional<SdlGpuRenderBackend::ClearDrawDescriptor> SdlGpuRenderBackend::ClearDrawForPeer(
    std::size_t index) const noexcept
{
    if (impl_ == nullptr || impl_->reservation == nullptr ||
        index >= impl_->reservation->clearDrawCount)
    {
        return std::nullopt;
    }
    return impl_->reservation->clearDraws[index];
}

Compositor::Compositor(SdlGpuRenderBackend &gpuBackend) noexcept : gpuBackend_(gpuBackend)
{
}

Compositor::Compositor(ApplicationKeeper &keeper, SdlGpuRenderBackend &gpuBackend) noexcept
    : Compositor(gpuBackend)
{
    (void)keeper.RegisterCompositor(*this);
}

CompositorRetireResult Compositor::RetireCompletedFrame() noexcept
{
    return gpuBackend_.RetireCompletedFrame();
}

const ApplicationRenderCompletionBatch &Compositor::RenderCompletions() const noexcept
{
    return gpuBackend_.RenderCompletions();
}

void Compositor::ClearRenderCompletions() noexcept
{
    gpuBackend_.ClearRenderCompletions();
}

CompositorFrameResult Compositor::ReplayAndPresent(const ApplicationRenderFrame &frame) noexcept
{
    return gpuBackend_.ReplayAndPresent(frame);
}

bool Compositor::Recover(AppWindow &window) noexcept
{
    return gpuBackend_.Recover(window);
}

void Compositor::InitVSync() noexcept
{
    gpuBackend_.InitVSync();
}

bool Compositor::IsVSyncAvailable() const noexcept
{
    return gpuBackend_.IsVSyncAvailable();
}

bool Compositor::IsVSyncEnabled() const noexcept
{
    return gpuBackend_.IsVSyncEnabled();
}

const wchar_t *Compositor::DriverName() const noexcept
{
    return gpuBackend_.DriverName();
}

void Compositor::EnableVSync() noexcept
{
    gpuBackend_.EnableVSync();
}

void Compositor::DisableVSync() noexcept
{
    gpuBackend_.DisableVSync();
}

SdlGpuMemoryStats Compositor::MemoryUsage() const noexcept
{
    return gpuBackend_.MemoryUsage();
}

const SdlGpuReplayPhaseTimings &Compositor::LastReplayPhaseTimings() const noexcept
{
    return gpuBackend_.LastReplayPhaseTimings();
}

void ApplicationLegacyCalls::InitVSync()
{
    if (Compositor *compositor = applicationKeeper_.CompositorUnit())
    {
        compositor->InitVSync();
    }
}

bool ApplicationLegacyCalls::IsVSyncAvailable()
{
    Compositor *compositor = applicationKeeper_.CompositorUnit();
    return compositor != nullptr && compositor->IsVSyncAvailable();
}

bool ApplicationLegacyCalls::IsVSyncEnabled()
{
    Compositor *compositor = applicationKeeper_.CompositorUnit();
    return compositor != nullptr && compositor->IsVSyncEnabled();
}

const wchar_t *ApplicationLegacyCalls::SdlGpuDriverName() const noexcept
{
    const Compositor *const compositor = applicationKeeper_.CompositorUnit();
    return compositor == nullptr ? L"unknown" : compositor->DriverName();
}

void ApplicationLegacyCalls::EnableVSync()
{
    if (Compositor *compositor = applicationKeeper_.CompositorUnit())
    {
        compositor->EnableVSync();
    }
}

void ApplicationLegacyCalls::DisableVSync()
{
    if (Compositor *compositor = applicationKeeper_.CompositorUnit())
    {
        compositor->DisableVSync();
    }
}

void SdlGpuRenderBackendDeleter::operator()(SdlGpuRenderBackend *backend) const noexcept
{
    delete backend;
}
