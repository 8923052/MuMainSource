#pragma once

#include "render/Assets.h"
#include "render/FrameTape.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

class AppWindow;
class ApplicationRenderFrame;
class SdlGpuRenderBackendTestPeer;

enum class SdlGpuBackendInitializeResult : std::uint8_t
{
    Success,
    WrongOwnerThread,
    WindowUnavailable,
    WindowExtentUnsupported,
    PropertiesCreationFailed,
    PropertiesConfigurationFailed,
    DeviceCreationFailed,
    FramesInFlightConfigurationFailed,
    WindowClaimFailed,
    UnsupportedColorFormat,
    UnsupportedSampleCount,
    UnsupportedDepthFormat,
    UnsupportedDriver,
    UnsupportedSwapchainFormat,
    ArenaCapacityOverflow,
    ArenaAllocationFailed,
};

struct SdlGpuMemoryStats final
{
    std::size_t uploadArenaBytes = 0;
    std::size_t downloadArenaBytes = 0;
    std::size_t completionPayloadBytes = 0;
    std::size_t residentTextureBytes = 0;
    std::size_t geometryBufferBytes = 0;
    std::size_t sessionTargetBytes = 0;
};

struct SdlGpuReplayPhaseTimings final
{
    std::chrono::nanoseconds preflightAndPlanning{};
    std::chrono::nanoseconds encode{};
    std::chrono::nanoseconds presentAndSubmit{};
    std::chrono::nanoseconds total{};
};

class SdlGpuRenderBackend final
{
  public:
    SdlGpuRenderBackend() noexcept;
    ~SdlGpuRenderBackend();

    SdlGpuRenderBackend(const SdlGpuRenderBackend &) = delete;
    SdlGpuRenderBackend &operator=(const SdlGpuRenderBackend &) = delete;

    SdlGpuBackendInitializeResult Initialize(AppWindow &window) noexcept;
    bool Recover(AppWindow &window) noexcept;
    void Shutdown() noexcept;

    bool IsReady() const noexcept;
    std::uint64_t DeviceGeneration() const noexcept;
    const wchar_t *DriverName() const noexcept;
    const wchar_t *ShaderArtifactName() const noexcept;
    const wchar_t *DepthStencilFormatName() const noexcept;
    const wchar_t *SwapchainFormatName() const noexcept;
    SdlGpuMemoryStats MemoryUsage() const noexcept;
    const SdlGpuReplayPhaseTimings &LastReplayPhaseTimings() const noexcept;

    void InitVSync() noexcept;
    bool IsVSyncAvailable() const noexcept;
    bool IsVSyncEnabled() const noexcept;
    void EnableVSync() noexcept;
    void DisableVSync() noexcept;

    CompositorFrameResult ReplayAndPresent(const ApplicationRenderFrame &frame) noexcept;
    CompositorRetireResult RetireCompletedFrame() noexcept;
    const ApplicationRenderCompletionBatch &RenderCompletions() const noexcept;
    void ClearRenderCompletions() noexcept;

    struct GpuRect final
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float minDepth = 0.0F;
        float maxDepth = 1.0F;
    };

    struct Impl;

  private:
    friend class SdlGpuRenderBackendTestPeer;

    struct CandidatePlan;
    struct FrameReservation;

    enum class TestDriver : std::uint8_t
    {
        Direct3D12,
        Vulkan,
        Metal,
        Unknown,
    };

    struct NativeTestConfig final
    {
        bool enabled = false;
        bool propertiesCreate = true;
        bool propertiesConfigure = true;
        bool deviceCreate = true;
        bool framesInFlight = true;
        bool windowClaim = true;
        bool colorFormat = true;
        bool colorSampleCount = true;
        bool depth24 = true;
        bool depth32 = true;
        bool uploadArena = true;
        bool downloadArena = true;
        bool targetColorCreate = true;
        bool targetDepthCreate = true;
        bool textureCreate = true;
        bool pipelineCreate = true;
        bool swapchainParameters = true;
        bool immediatePresentMode = true;
        bool commandBufferAcquire = true;
        bool encode = true;
        bool swapchainWait = true;
        bool swapchainTexture = true;
        bool submitFence = true;
        bool cancelCommandBuffer = true;
        bool fenceComplete = true;
        bool downloadMap = true;
        TestDriver driver = TestDriver::Direct3D12;
        std::uint32_t shaderFormats = 1u << 3;
        std::uint32_t swapchainFormat = 0;
        std::uint32_t requerySwapchainFormat = 0;
        std::uint32_t windowWidth = 800;
        std::uint32_t windowHeight = 600;
    };

    struct AllocationLedger final
    {
        std::size_t geometryUploadBytes = 0;
        std::size_t skinningUploadBytes = 0;
        std::size_t terrainLightUploadBytes = 0;
        std::size_t drawInstanceUploadBytes = 0;
        std::size_t ownerUploadBytes = 0;
        std::size_t downloadTransferBytes = 0;
        std::size_t overlayDrawBytes = 0;
        std::size_t clearDrawBytes = 0;
    };

    struct SamplerPolicy final
    {
        std::uint8_t minFilter = 0;
        std::uint8_t magFilter = 0;
        std::uint8_t mipmapMode = 0;
        std::uint8_t addressModeU = 0;
        std::uint8_t addressModeV = 0;
        std::uint8_t addressModeW = 0;
        float mipLodBias = 0.0F;
        float maxAnisotropy = 1.0F;
        std::uint8_t compareOp = 7;
        float minLod = 0.0F;
        float maxLod = 0.0F;
        bool enableAnisotropy = false;
        bool enableCompare = false;
        std::uint64_t props = 0;
    };

    enum class ShaderFamily : std::uint8_t
    {
        RenderTape,
        Composition,
        OverlayRect,
        Label,
    };

    struct PipelineKey final
    {
        ShaderFamily shaderFamily = ShaderFamily::RenderTape;
        RenderIndexTopology topology = RenderIndexTopology::Triangles;
        std::uint8_t vertexLayout = 0;
        bool samplerUse = false;
        bool blendEnable = false;
        RenderBlendFactor blendSource = RenderBlendFactor::One;
        RenderBlendFactor blendDestination = RenderBlendFactor::Zero;
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        RenderCompareFunction depthCompare = RenderCompareFunction::LessOrEqual;
        bool stencilEnable = false;
        RenderCompareFunction stencilCompare = RenderCompareFunction::Always;
        RenderStencilOperation stencilFail = RenderStencilOperation::Keep;
        RenderStencilOperation stencilDepthFail = RenderStencilOperation::Keep;
        RenderStencilOperation stencilPass = RenderStencilOperation::Keep;
        bool stencilReplace = false;
        std::uint8_t stencilReadMask = 0xFF;
        std::uint8_t stencilWriteMask = 0xFF;
        bool cullEnable = true;
        RenderCullFace cullFace = RenderCullFace::Back;
        RenderFrontFace frontFace = RenderFrontFace::CounterClockwise;
        std::uint8_t colorWriteMask = 0x0F;
        bool hasDepthStencilTarget = true;
        std::uint32_t colorFormat = 0;
        std::uint32_t depthFormat = 0;
        std::uint64_t deviceGeneration = 0;

        bool operator==(const PipelineKey &) const noexcept = default;
    };

    struct ClearDrawDescriptor final
    {
        PipelineKey pipeline;
        GpuRect scissor;
        std::uint32_t targetWidth = 0;
        std::uint32_t targetHeight = 0;
        std::array<float, 4> color{0.0F, 0.0F, 0.0F, 0.0F};
        std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
        float depth = 1.0F;
        std::uint32_t stencil = 0;
        std::uint8_t attachmentMask = 0;
        std::size_t vertexOffset = 0;
        std::size_t indexOffset = 0;
        bool pipelineBound = false;
        bool vertexBound = false;
        bool indexBound = false;
        bool uniformPushed = false;
        bool stencilReferenceSet = false;
        bool drawIssued = false;
    };

    struct PipelinePolicy final
    {
        bool depthClip = true;
        bool depthBiasEnabled = false;
        float depthBiasConstant = 0.0F;
        float depthBiasClamp = 0.0F;
        float depthBiasSlope = 0.0F;
        bool fillModeFill = true;
        bool clipDistance = false;
        bool indirectDraw = false;
    };

    struct CandidatePlan final
    {
        bool valid = false;
        std::size_t sessionIndex = 0;
        std::uint64_t sessionId = 0;
        std::uint64_t sessionGeneration = 0;
        std::uint64_t surfaceGeneration = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<PipelineKey> pipelines;
        std::size_t pipelineCount = 0;
        std::size_t completionBytes = 0;
        std::size_t uploadBytes = 0;
        std::size_t persistentGeometryUploadBytes = 0;
    };

    static std::optional<AllocationLedger> CalculateLedger(std::size_t depthBytes) noexcept;
    static std::optional<SamplerPolicy> MakeSamplerPolicy(RenderSamplerIntent intent) noexcept;
    std::optional<PipelinePolicy> MakePipelinePolicy(const PipelineKey &key) const noexcept;
    bool CreatePipelineNative(const PipelineKey &key, const PipelinePolicy &policy,
                              void *&native) noexcept;
    bool ApplyVSyncParameters(bool enable) noexcept;
    bool QuerySwapchainFormatTransition() noexcept;
    void CommitSwapchainFormatTransition() noexcept;
    void InvalidateSwapchainPipelines() noexcept;
    bool EnsureUploadArenaCapacity(std::size_t geometryUploadBytes, std::size_t skinningUploadBytes,
                                   std::size_t drawInstanceUploadBytes,
                                   std::size_t ownerUploadBytes,
                                   std::size_t overlayDrawBytes) noexcept;
    bool EnsureDownloadArenaCapacity(std::size_t bytes) noexcept;
    bool EnsureGeometryBufferCapacity(std::size_t vertexBytes, std::size_t indexBytes) noexcept;
    bool EnsureBonePaletteBufferCapacity(std::size_t bytes) noexcept;
    bool EnsureDrawInstanceBufferCapacity(std::size_t bytes) noexcept;
    bool PreflightFrame(const ApplicationRenderFrame &frame, std::vector<CandidatePlan> &plans,
                        std::size_t &planCount, std::size_t &completionBytes) const noexcept;
    bool CreatePersistentGeometryBuffers(const LogicalGeometryAssetLease &lease, std::size_t slot,
                                         void *&vertices, void *&indices,
                                         void *&terrainCells, void *&terrainInstances) noexcept;
    bool StageTerrainLights(FrameReservation &reservation) noexcept;
    bool UploadTerrainLights(FrameReservation &reservation) noexcept;
    bool StagePersistentGeometry(const ApplicationRenderFrame &frame,
                                 const std::vector<CandidatePlan> &plans, std::size_t planCount,
                                 FrameReservation &reservation) noexcept;
    bool WritePoseAndInstanceUploads(const ApplicationRenderFrame &frame,
                                     std::span<const CandidatePlan> plans,
                                     std::byte *mapped) noexcept;
    bool EncodeFrame(const ApplicationRenderFrame &frame, const std::vector<CandidatePlan> &plans,
                     std::size_t planCount, FrameReservation &reservation) noexcept;
    void PublishReservation(const ApplicationRenderFrame &frame,
                            const std::vector<CandidatePlan> &plans, std::size_t planCount,
                            FrameReservation &reservation, std::uint64_t fenceToken) noexcept;
    void RetireStaleSessionTargets(const ApplicationRenderFrame &frame) noexcept;
    void CancelReservation(FrameReservation &reservation) noexcept;
    static void ResetReservationMetadata(FrameReservation &reservation) noexcept;

    NativeTestConfig &NativeConfigForPeer() noexcept;
    const AllocationLedger *LedgerForPeer() const noexcept;
    std::size_t PendingGeometryUploadBytesForPeer() const noexcept;
    std::optional<SamplerPolicy> SamplerPolicyForPeer(RenderSamplerIntent intent) const noexcept;
    std::optional<PipelinePolicy> PipelinePolicyForPeer(const PipelineKey &key) const noexcept;
    std::optional<std::uint32_t> ColorWriteMaskForPeer(const PipelineKey &key) const noexcept;
    SdlGpuBackendInitializeResult InitializeInjected(AppWindow &window) noexcept;
    PipelineKey DefaultPipelineKeyForPeer() const noexcept;
    PipelineKey CompositionPipelineKeyForPeer() const noexcept;
    PipelineKey OverlayPipelineKeyForPeer() const noexcept;
    PipelineKey ClearPipelineKeyForPeer(const RenderTapeClear &clear) const noexcept;
    std::optional<ClearDrawDescriptor> BuildClearDrawDescriptorForPeer(
        const RenderTapeClear &clear, std::uint8_t encoding, std::uint32_t width,
        std::uint32_t height, std::size_t vertexOffset, std::size_t indexOffset) const noexcept;
    PipelineKey PipelineVariantForPeer(std::size_t index) const noexcept;
    bool PrepareCompletionForPeer(std::size_t bytes) noexcept;
    std::size_t CompletionCapacityForPeer() const noexcept;
    bool ReserveSamplerForPeer(RenderSamplerIntent intent, std::size_t &slot) noexcept;
    bool ReserveTextureForPeer(LogicalRenderAssetRef asset, std::size_t width, std::size_t height,
                               std::size_t byteCount, std::size_t &slot) noexcept;
    bool ReserveSessionTargetForPeer(SessionId id, SessionGeneration generation,
                                     std::uint64_t surfaceGeneration, std::size_t width,
                                     std::size_t height, std::size_t &slot) noexcept;
    bool ReservePipelineForPeer(const PipelineKey &key, std::size_t &slot) noexcept;
    void MarkFenceForPeer(std::uint64_t fence) noexcept;
    void RetireFenceForPeer(std::uint64_t fence) noexcept;
    std::size_t LiveSamplerCountForPeer() const noexcept;
    std::size_t LiveTextureCountForPeer() const noexcept;
    std::size_t LiveSessionTargetCountForPeer() const noexcept;
    std::size_t LivePipelineCountForPeer() const noexcept;
    std::size_t ResidentTextureBytesForPeer() const noexcept;
    bool IsOwnerThreadForPeer() const noexcept;
    std::size_t ReplayAcquireCountForPeer() const noexcept;
    std::size_t ReplaySubmitCountForPeer() const noexcept;
    std::size_t ReplayCancelCountForPeer() const noexcept;
    bool EnsureGeometryBufferCapacityForPeer(std::size_t vertexBytes,
                                             std::size_t indexBytes) noexcept;
    std::size_t GeometryBufferAllocationCountForPeer() const noexcept;
    std::size_t DrawInstanceBufferAllocationCountForPeer() const noexcept;
    std::size_t GeometryVertexBufferCapacityForPeer() const noexcept;
    std::size_t GeometryIndexBufferCapacityForPeer() const noexcept;
    std::size_t OperationCountForPeer() const noexcept;
    std::uint8_t OperationKindForPeer(std::size_t index) const noexcept;
    std::size_t ClearDrawCountForPeer() const noexcept;
    std::optional<ClearDrawDescriptor> ClearDrawForPeer(std::size_t index) const noexcept;
    std::optional<GpuRect> ViewportForPeer(RenderTapeRect rect, std::uint32_t surfaceHeight,
                                           float minDepth, float maxDepth) const noexcept;
    std::optional<GpuRect> ScissorForPeer(RenderTapeRect rect,
                                          std::uint32_t surfaceHeight) const noexcept;
    std::optional<GpuRect> TransferRectForPeer(RenderTapeRect rect,
                                               std::uint32_t surfaceHeight) const noexcept;
    std::optional<float> ClipDepthForPeer(float clipZ, float clipW) const noexcept;
    static std::uint8_t ClearEncodingForPeer(const RenderTapeClear &clear,
                                             RenderTapeRect target) noexcept;
    static std::uint8_t AlphaCompareCodeForPeer(RenderCompareFunction compare) noexcept;
    static std::array<float, 4> ApplyTextureEnvironmentForPeer(
        RenderTextureEnvironment environment, std::array<float, 4> color,
        std::array<float, 4> sampled) noexcept;
    static std::array<float, 4> LightingColorForPeer(bool enabled) noexcept;
    static std::array<float, 4> SessionCompositionUvForPeer() noexcept;
    static std::array<float, 4> GlyphUvForPeer(wchar_t character) noexcept;

    std::thread::id ownerThread_;
    std::unique_ptr<Impl> impl_;
};

class AppWindow;
class SdlGpuRenderBackend;
struct SdlGpuMemoryStats;
struct SdlGpuReplayPhaseTimings;
class ApplicationKeeper;

class Compositor final
{
  public:
    explicit Compositor(SdlGpuRenderBackend &gpuBackend) noexcept;
    Compositor(ApplicationKeeper &keeper, SdlGpuRenderBackend &gpuBackend) noexcept;
    CompositorRetireResult RetireCompletedFrame() noexcept;
    const ApplicationRenderCompletionBatch &RenderCompletions() const noexcept;
    void ClearRenderCompletions() noexcept;
    CompositorFrameResult ReplayAndPresent(const ApplicationRenderFrame &frame) noexcept;
    bool Recover(AppWindow &window) noexcept;
    void InitVSync() noexcept;
    bool IsVSyncAvailable() const noexcept;
    bool IsVSyncEnabled() const noexcept;
    const wchar_t *DriverName() const noexcept;
    void EnableVSync() noexcept;
    void DisableVSync() noexcept;
    SdlGpuMemoryStats MemoryUsage() const noexcept;
    const SdlGpuReplayPhaseTimings &LastReplayPhaseTimings() const noexcept;

  private:
    SdlGpuRenderBackend &gpuBackend_;
};

// Backend pointer ownership, shared by application composition.
struct SdlGpuRenderBackendDeleter final
{
    void operator()(SdlGpuRenderBackend *backend) const noexcept;
};

using SdlGpuRenderBackendPtr = std::unique_ptr<SdlGpuRenderBackend, SdlGpuRenderBackendDeleter>;
