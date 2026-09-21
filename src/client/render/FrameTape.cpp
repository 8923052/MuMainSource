#include "render/FrameTape.h"
#include "app/ApplicationKeeper.h"
#include "data/Localization.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ItemsSkills.h"
#include "domain/WorldSimulation.h"
#include "render/Map.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionKeeper.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/session/UiSessionLogic.h"

#ifdef _DEBUG
extern "C"
{
    __declspec(dllexport) std::uint64_t MuRenderFrameRejectionCount = 0;
    __declspec(dllexport) std::uint32_t MuRenderFrameRejectionReason = 0;
    __declspec(dllexport) std::uint64_t MuRenderFrameRejectedSession = 0;
    __declspec(dllexport) std::uint64_t MuRenderFrameRejectedFrame = 0;
    __declspec(dllexport) std::uint64_t MuRenderFrameRejectedAssetId = 0;
    __declspec(dllexport) std::uint64_t MuRenderFrameRejectedAssetRevision = 0;
}
#endif

namespace
{
#ifdef _DEBUG
enum class RenderFrameRejection : std::uint32_t
{
    InvalidTapeIdentity = 1,
    MissingLogicalAsset = 2,
    InvalidTapeContents = 3,
    AssetTableMismatch = 4,
};

void ReportRenderFrameRejection(RenderFrameRejection reason, SessionId session, std::uint64_t frame,
                                LogicalRenderAssetRef asset = {}) noexcept
{
    ++MuRenderFrameRejectionCount;
    MuRenderFrameRejectionReason = static_cast<std::uint32_t>(reason);
    MuRenderFrameRejectedSession = session.RawValue();
    MuRenderFrameRejectedFrame = frame;
    MuRenderFrameRejectedAssetId = asset.id.value;
    MuRenderFrameRejectedAssetRevision = asset.revision;

    struct Signature final
    {
        std::uint64_t session = 0;
        RenderFrameRejection reason{};
        LogicalRenderAssetRef asset;
    };
    static std::array<Signature, ApplicationFramePlan::MaximumSessions> reported{};
    Signature *signature = nullptr;
    for (Signature &candidate : reported)
    {
        if (candidate.session == session.RawValue() || candidate.session == 0)
        {
            signature = &candidate;
            break;
        }
    }
    if (signature == nullptr)
    {
        signature = &reported[session.RawValue() % reported.size()];
    }
    if (signature->session == session.RawValue() && signature->reason == reason &&
        signature->asset == asset)
    {
        return;
    }
    *signature = {session.RawValue(), reason, asset};

    const char *const reasonName = [&]() noexcept {
        switch (reason)
        {
        case RenderFrameRejection::InvalidTapeIdentity:
            return "invalid-tape-identity";
        case RenderFrameRejection::MissingLogicalAsset:
            return "missing-logical-asset";
        case RenderFrameRejection::InvalidTapeContents:
            return "invalid-tape-contents";
        case RenderFrameRejection::AssetTableMismatch:
            return "asset-table-mismatch";
        }
        return "unknown";
    }();
    constexpr const char *Format =
        "[RenderFrame] rejected reason=%s(%u) session=%llu frame=%llu asset=%llu:%llu\n";
    std::fprintf(stderr, Format, reasonName, static_cast<unsigned int>(reason),
                 static_cast<unsigned long long>(session.RawValue()),
                 static_cast<unsigned long long>(frame),
                 static_cast<unsigned long long>(asset.id.value),
                 static_cast<unsigned long long>(asset.revision));
    FILE *log = nullptr;
    if (fopen_s(&log, "render-frame-failures.log", "a") == 0)
    {
        std::fprintf(log, Format, reasonName, static_cast<unsigned int>(reason),
                     static_cast<unsigned long long>(session.RawValue()),
                     static_cast<unsigned long long>(frame),
                     static_cast<unsigned long long>(asset.id.value),
                     static_cast<unsigned long long>(asset.revision));
        std::fclose(log);
    }
}
#else
enum class RenderFrameRejection : std::uint32_t
{
    InvalidTapeIdentity,
    MissingLogicalAsset,
    InvalidTapeContents,
    AssetTableMismatch,
};

void ReportRenderFrameRejection(RenderFrameRejection, SessionId, std::uint64_t,
                                LogicalRenderAssetRef = {}) noexcept
{
}
#endif

bool Contains(SessionDisplayRect outer, SessionDisplayRect inner) noexcept
{
    const std::int64_t outerRight = static_cast<std::int64_t>(outer.x) + outer.width;
    const std::int64_t outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    const std::int64_t innerRight = static_cast<std::int64_t>(inner.x) + inner.width;
    const std::int64_t innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    return inner.x >= outer.x && inner.y >= outer.y && innerRight <= outerRight &&
           innerBottom <= outerBottom;
}

bool Contains(SessionDisplayRect outer, RenderTapeRect inner) noexcept
{
    const std::int64_t outerRight = static_cast<std::int64_t>(outer.x) + outer.width;
    const std::int64_t outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    const std::int64_t innerRight = static_cast<std::int64_t>(inner.x) + inner.width;
    const std::int64_t innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    return inner.x >= outer.x && inner.y >= outer.y && innerRight <= outerRight &&
           innerBottom <= outerBottom;
}

bool Contains(RenderTapeRect outer, RenderTapeRect inner) noexcept
{
    const std::int64_t outerRight = static_cast<std::int64_t>(outer.x) + outer.width;
    const std::int64_t outerBottom = static_cast<std::int64_t>(outer.y) + outer.height;
    const std::int64_t innerRight = static_cast<std::int64_t>(inner.x) + inner.width;
    const std::int64_t innerBottom = static_cast<std::int64_t>(inner.y) + inner.height;
    return inner.width != 0 && inner.height != 0 && inner.x >= outer.x && inner.y >= outer.y &&
           innerRight <= outerRight && innerBottom <= outerBottom;
}

bool SameSession(SessionId left, SessionId right) noexcept
{
    return left == right;
}

bool SameAsset(const LogicalRenderAssetLease &lease, LogicalRenderAssetRef asset) noexcept
{
    return lease.asset == asset;
}

bool SameLease(const LogicalRenderAssetLease &left, const LogicalRenderAssetLease &right) noexcept
{
    return left.asset == right.asset && left.width == right.width && left.height == right.height &&
           left.sampler == right.sampler && left.bytes != nullptr && right.bytes != nullptr &&
           *left.bytes == *right.bytes;
}
} // namespace

const ApplicationRenderFrame::SessionRecord *ApplicationRenderFrame::Session(
    std::size_t index) const noexcept
{
    if (index >= sessions_.size())
    {
        return nullptr;
    }
    return &sessions_[index];
}

const ApplicationRenderFrame::CompositionRecord *ApplicationRenderFrame::Composition(
    std::size_t index) const noexcept
{
    if (index >= compositions_.size())
    {
        return nullptr;
    }
    return &compositions_[index];
}

std::unique_ptr<ApplicationRenderFrame> ApplicationRenderFrame::TryCreate(
    std::uint64_t frameSequence, std::span<SessionAdvanceResult> results,
    const SessionWorkspace &workspace, const CGlobalBitmap &assets) noexcept
{
    try
    {
        auto frame = std::make_unique<ApplicationRenderFrame>();
        frame->frameSequence_ = frameSequence;
        frame->workspaceRevision_ = workspace.PresentationRevision();
        frame->windowRect_ = workspace.WindowRect();
        if (frameSequence == 0 || frame->windowRect_.width == 0 || frame->windowRect_.height == 0 ||
            results.size() > ApplicationFramePlan::MaximumSessions)
        {
            return nullptr;
        }

        const std::vector<SessionId> ordered = workspace.OrderedSessions();
        if (ordered.size() > ApplicationFramePlan::MaximumSessions)
        {
            return nullptr;
        }
        for (std::size_t left = 0; left < ordered.size(); ++left)
        {
            if (ordered[left].RawValue() == 0)
            {
                return nullptr;
            }
            for (std::size_t right = left + 1; right < ordered.size(); ++right)
            {
                if (SameSession(ordered[left], ordered[right]))
                {
                    return nullptr;
                }
            }
        }

        for (std::size_t left = 0; left < results.size(); ++left)
        {
            if (results[left].Id().RawValue() == 0 || results[left].Generation().RawValue() == 0 ||
                results[left].FrameSequence() != frameSequence)
            {
                return nullptr;
            }
            for (std::size_t right = left + 1; right < results.size(); ++right)
            {
                if (SameSession(results[left].Id(), results[right].Id()))
                {
                    return nullptr;
                }
            }
        }

        for (const WorkspaceOverlayRect &overlay : workspace.OverlayRects())
        {
            if (overlay.rect.width == 0 || overlay.rect.height == 0 ||
                !Contains(frame->windowRect_, overlay.rect))
            {
                return nullptr;
            }
            frame->overlayRects_.push_back(overlay);
        }
        for (const WorkspaceOverlayLabel &overlay : workspace.OverlayLabels())
        {
            if (overlay.text.size() > 256 || overlay.x < frame->windowRect_.x ||
                overlay.y < frame->windowRect_.y || !std::isfinite(overlay.scale) ||
                overlay.scale <= 0.0F)
            {
                return nullptr;
            }
            frame->overlayLabels_.push_back(overlay);
        }

        std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> frameAssetRefs;

        for (const SessionId id : ordered)
        {
            if (!workspace.IsVisible(id))
            {
                continue;
            }
            const SessionDisplayView *const display = workspace.Display(id);
            const std::optional<SessionDisplayRect> destination = workspace.ContentRect(id);
            if (display == nullptr || destination == std::nullopt || display->Id() != id ||
                !display->IsVisible() || destination->width == 0 || destination->height == 0 ||
                !Contains(frame->windowRect_, *destination))
            {
                return nullptr;
            }

            SessionAdvanceResult *candidate = nullptr;
            for (SessionAdvanceResult &result : results)
            {
                if (result.Id() != id)
                {
                    continue;
                }
                frame->compositions_.push_back(CompositionRecord{
                    id, result.Generation(), *destination, display->SurfaceGeneration()});
                if (result.Succeeded() && result.RenderTape().has_value())
                {
                    candidate = &result;
                }
                break;
            }
            if (candidate == nullptr)
            {
                continue;
            }
            const SessionRenderTape &tape = *candidate->RenderTape();
            const bool validatedByRecording = tape.WasValidatedByRecording();
            if (!tape.HasValidStorageCounts() || tape.Id().RawValue() == 0 ||
                tape.Generation().RawValue() == 0 || tape.SurfaceGeneration() == 0 ||
                tape.Id() != id || tape.Generation() != candidate->Generation() ||
                tape.FrameSequence() != frameSequence ||
                tape.SurfaceGeneration() != display->SurfaceGeneration() ||
                tape.ViewportWidth() != destination->width ||
                tape.ViewportHeight() != destination->height)
            {
                ReportRenderFrameRejection(RenderFrameRejection::InvalidTapeIdentity, id,
                                           frameSequence);
                continue;
            }

            if (validatedByRecording)
            {
                std::unordered_map<LogicalRenderAssetRef, const UploadLogicalAssetRgba8Request *,
                                   LogicalRenderAssetRefHash>
                    frameUploads;
                std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash>
                    producedTargets;
                for (const RenderOwnerRequest &request : tape.OwnerRequests())
                {
                    if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
                    {
                        frameUploads.emplace(upload->destination, upload);
                    }
                    else if (const auto *copy =
                                 std::get_if<CopyTargetToLogicalTextureRequest>(&request))
                    {
                        producedTargets.insert(copy->destination);
                    }
                }
                bool resourcesReady = true;
                LogicalRenderAssetRef missingAsset;
                for (const LogicalRenderAssetRef asset : tape.LogicalAssets())
                {
                    std::optional<LogicalRenderAssetLease> lease = assets.TryLease(asset);
                    if (!lease.has_value())
                    {
                        const auto upload = frameUploads.find(asset);
                        if (upload == frameUploads.end())
                        {
                            if (producedTargets.contains(asset))
                            {
                                continue;
                            }
                            resourcesReady = false;
                            missingAsset = asset;
                            break;
                        }
                        const std::span<const std::byte> payload = tape.PayloadBytes().subspan(
                            upload->second->payloadOffset, upload->second->payloadByteCount);
                        lease =
                            LogicalRenderAssetLease{asset,
                                                    upload->second->width,
                                                    upload->second->height,
                                                    RenderAssetFormat::Rgba8,
                                                    upload->second->sampler,
                                                    std::make_shared<const std::vector<std::byte>>(
                                                        payload.begin(), payload.end())};
                    }
                    if (frameAssetRefs.insert(asset).second)
                    {
                        frame->assets_.push_back(std::move(*lease));
                    }
                }
                if (!resourcesReady)
                {
                    ReportRenderFrameRejection(RenderFrameRejection::MissingLogicalAsset, id,
                                               frameSequence, missingAsset);
                    continue;
                }
                frame->sessions_.push_back(SessionRecord{id, candidate->Generation(), *destination,
                                                         std::move(*candidate->renderTape_)});
                candidate->renderTape_.reset();
                continue;
            }

            std::vector<LogicalRenderAssetLease> candidateAssets;
            candidateAssets.reserve(tape.LogicalAssets().size());
            std::unordered_set<LogicalRenderAssetRef, LogicalRenderAssetRefHash> candidateRefs;
            candidateRefs.reserve(tape.LogicalAssets().size());
            const auto hasCandidateRef = [&](LogicalRenderAssetRef asset) {
                return candidateRefs.contains(asset);
            };
            const auto addCandidateRef = [&](LogicalRenderAssetRef asset) {
                if (hasCandidateRef(asset))
                {
                    return true;
                }
                return candidateRefs.insert(asset).second;
            };
            std::uint64_t lastStableOrder = 0;
            bool valid = true;
            LogicalRenderAssetRef rejectedAsset;
            for (std::size_t left = 0;
                 !validatedByRecording && valid && left < tape.GeometryAssets().size(); ++left)
            {
                const LogicalGeometryAssetLease &lease = tape.GeometryAssets()[left];
                valid = IsValid(lease) && lease.asset.sessionId == tape.Id().RawValue() &&
                        lease.asset.generation == tape.Generation().RawValue();
                for (std::size_t right = left + 1; valid && right < tape.GeometryAssets().size();
                     ++right)
                {
                    valid = lease.asset != tape.GeometryAssets()[right].asset;
                }
            }
            for (std::size_t left = 0;
                 !validatedByRecording && valid && left < tape.LogicalAssets().size(); ++left)
            {
                if (!IsValid(tape.LogicalAssets()[left]))
                {
                    valid = false;
                    break;
                }
                for (std::size_t right = left + 1; right < tape.LogicalAssets().size(); ++right)
                {
                    if (tape.LogicalAssets()[left] == tape.LogicalAssets()[right])
                    {
                        valid = false;
                        break;
                    }
                }
            }
            if (!valid)
            {
                ReportRenderFrameRejection(RenderFrameRejection::InvalidTapeContents, id,
                                           frameSequence);
                continue;
            }
            for (const RenderTapeEntry &entry : tape.Entries())
            {
                if (!validatedByRecording && !IsValid(entry.kind))
                {
                    valid = false;
                    break;
                }
                if (!validatedByRecording && !IsValid(entry.pass))
                {
                    valid = false;
                    break;
                }
                if (entry.stableOrder == 0)
                {
                    valid = false;
                    break;
                }
                if (entry.stableOrder <= lastStableOrder)
                {
                    valid = false;
                    break;
                }
                lastStableOrder = entry.stableOrder;
                if (entry.kind == RenderTapeEntryKind::Clear)
                {
                    if (entry.index >= tape.Clears().size() ||
                        !IsValid(tape.Clears()[entry.index]) ||
                        (tape.Clears()[entry.index].scissorEnable &&
                         !Contains(
                             RenderTapeRect{0, 0, tape.ViewportWidth(), tape.ViewportHeight()},
                             tape.Clears()[entry.index].scissor)))
                    {
                        valid = false;
                        break;
                    }
                    continue;
                }
                if (entry.kind == RenderTapeEntryKind::Draw)
                {
                    if (entry.index >= tape.Draws().size())
                    {
                        valid = false;
                        break;
                    }
                    const RenderTapeDraw &draw = tape.Draws()[entry.index];
                    const bool hasGeometry = IsValid(draw.geometry);
                    const LogicalGeometryAssetLease *const geometry =
                        hasGeometry ? tape.FindGeometryAsset(draw.geometry) : nullptr;
                    const std::size_t vertexSize = hasGeometry && geometry
                                                       ? geometry->vertices->size()
                                                       : tape.Vertices().size();
                    const std::size_t indexSize =
                        hasGeometry && geometry ? geometry->indices->size() : tape.Indices().size();
                    if (draw.constantsIndex >= tape.Constants().size() ||
                        (!validatedByRecording &&
                         (!IsValid(draw.topology) || !IsValid(draw.pipeline.positionSpace) ||
                          !IsValid(draw.pipeline.geometryMode) ||
                          !IsValid(tape.Constants()[draw.constantsIndex]))) ||
                        (hasGeometry && geometry == nullptr) ||
                        (!hasGeometry &&
                         (draw.geometry.sessionId != 0 || draw.geometry.generation != 0 ||
                          draw.geometry.revision != 0)) ||
                        draw.vertexOffset > vertexSize ||
                        draw.vertexCount > vertexSize - draw.vertexOffset ||
                        draw.indexOffset > indexSize ||
                        draw.indexCount > indexSize - draw.indexOffset)
                    {
                        valid = false;
                        break;
                    }
                    if ((draw.topology == RenderIndexTopology::Lines && draw.indexCount % 2 != 0) ||
                        (draw.topology == RenderIndexTopology::Triangles &&
                         draw.indexCount % 3 != 0))
                    {
                        valid = false;
                        break;
                    }
                    for (std::size_t index = 0; !validatedByRecording && index < draw.vertexCount;
                         ++index)
                    {
                        const RenderTapeVertex &vertex =
                            hasGeometry ? (*geometry->vertices)[draw.vertexOffset + index]
                                        : tape.Vertices()[draw.vertexOffset + index];
                        if (!IsValid(vertex))
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid)
                    {
                        break;
                    }
                    const RenderTapeConstants &constants = tape.Constants()[draw.constantsIndex];
                    if (!validatedByRecording)
                    {
                        const auto &bmd = constants.bmd;
                        const bool rigid =
                            draw.pipeline.geometryMode == RenderGeometryMode::RigidInstances;
                        if (rigid)
                        {
                            valid = valid && hasGeometry && bmd.enabled && bmd.rigid &&
                                    !bmd.shadow && bmd.rigidInstanceCount != 0 &&
                                    bmd.rigidInstanceOffset <= tape.RigidInstances().size() &&
                                    bmd.rigidInstanceCount <=
                                        tape.RigidInstances().size() - bmd.rigidInstanceOffset;
                            for (std::size_t index = 0; valid && index < bmd.rigidInstanceCount;
                                 ++index)
                                valid =
                                    IsValid(tape.RigidInstances()[bmd.rigidInstanceOffset + index]);
                        }
                        else if (bmd.rigidInstanceOffset != 0 || bmd.rigidInstanceCount != 0)
                            valid = false;
                        if (!valid)
                            break;
                    }
                    if (!validatedByRecording)
                    {
                        const bool particles =
                            draw.pipeline.geometryMode == RenderGeometryMode::ParticleInstances;
                        const bool terrainInstances =
                            draw.pipeline.geometryMode == RenderGeometryMode::Terrain ||
                            draw.pipeline.geometryMode == RenderGeometryMode::Grass;
                        const bool usesInstances =
                            particles || UsesQuadInstanceStorage(draw.pipeline.geometryMode);
                        const auto available = particles ? tape.ParticleInstances().size()
                                                         : tape.QuadInstances().size();
                        const auto &run = constants.quad;
                        if (UsesTrailInstanceStorage(draw.pipeline.geometryMode))
                            valid = hasGeometry && tape.HasValidExternalTrailDraw(
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
                                              tape.ParticleInstances()[run.instanceOffset + index])
                                        : IsValid(tape.QuadInstances()[run.instanceOffset + index]);
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
                    }
                    if (!valid)
                    {
                        break;
                    }
                    const RenderTapeRect viewport{0, 0, tape.ViewportWidth(),
                                                  tape.ViewportHeight()};
                    if (!Contains(viewport, constants.viewport) ||
                        (constants.scissorEnable &&
                         !Contains(constants.viewport, constants.scissor)))
                    {
                        valid = false;
                        break;
                    }
                    for (std::size_t index = 0; !validatedByRecording && index < draw.indexCount;
                         ++index)
                    {
                        const std::uint32_t vertexIndex =
                            hasGeometry ? (*geometry->indices)[draw.indexOffset + index]
                                        : tape.Indices()[draw.indexOffset + index];
                        if (vertexIndex >= draw.vertexCount)
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid)
                    {
                        break;
                    }
                    if (draw.asset.id.value != 0 || draw.asset.revision != 0)
                    {
                        if (hasCandidateRef(draw.asset))
                        {
                            continue;
                        }
                        std::optional<LogicalRenderAssetLease> lease = assets.TryLease(draw.asset);
                        if (!lease.has_value())
                        {
                            valid = false;
                            rejectedAsset = draw.asset;
                            break;
                        }
                        if (!addCandidateRef(draw.asset))
                        {
                            valid = false;
                            break;
                        }
                        bool alreadyAdded = false;
                        for (const LogicalRenderAssetLease &candidateAsset : candidateAssets)
                        {
                            if (SameAsset(candidateAsset, lease->asset))
                            {
                                alreadyAdded = true;
                                break;
                            }
                        }
                        if (!alreadyAdded)
                        {
                            candidateAssets.push_back(*lease);
                        }
                    }
                    continue;
                }

                if (entry.index >= tape.OwnerRequests().size())
                {
                    valid = false;
                    break;
                }
                const RenderOwnerRequest &request = tape.OwnerRequests()[entry.index];
                if (const auto *upload = std::get_if<UploadLogicalAssetRgba8Request>(&request))
                {
                    const std::span<const std::byte> payload = tape.PayloadBytes();
                    if (!IsValid(upload->destination) || upload->width == 0 ||
                        upload->height == 0 || !IsValid(upload->retention) ||
                        !IsValid(upload->sampler) || upload->payloadOffset > payload.size() ||
                        upload->payloadByteCount > payload.size() - upload->payloadOffset ||
                        !IsExactRgba8ByteCount(upload->width, upload->height,
                                               upload->payloadByteCount))
                    {
                        valid = false;
                        break;
                    }
                    std::shared_ptr<const std::vector<std::byte>> bytes;
                    try
                    {
                        bytes = std::make_shared<const std::vector<std::byte>>(
                            payload.begin() + upload->payloadOffset,
                            payload.begin() + upload->payloadOffset + upload->payloadByteCount);
                    }
                    catch (...)
                    {
                        valid = false;
                        break;
                    }
                    const LogicalRenderAssetLease lease{
                        upload->destination,      upload->width,   upload->height,
                        RenderAssetFormat::Rgba8, upload->sampler, std::move(bytes)};
                    if (!addCandidateRef(upload->destination))
                    {
                        valid = false;
                        break;
                    }
                    for (const LogicalRenderAssetLease &candidateAsset : candidateAssets)
                    {
                        if (SameAsset(candidateAsset, lease.asset) &&
                            !SameLease(candidateAsset, lease))
                        {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid)
                    {
                        break;
                    }
                    bool alreadyAdded = false;
                    for (const LogicalRenderAssetLease &candidateAsset : candidateAssets)
                    {
                        if (SameAsset(candidateAsset, lease.asset))
                        {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded)
                    {
                        candidateAssets.push_back(lease);
                    }
                    continue;
                }

                const auto validateSource = [&](SessionId sourceSession,
                                                SessionGeneration sourceGeneration,
                                                std::uint64_t sourceSurface) noexcept {
                    return sourceSession == tape.Id() && sourceGeneration == tape.Generation() &&
                           sourceSurface == tape.SurfaceGeneration();
                };
                if (const auto *copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request))
                {
                    valid =
                        IsValid(copy->destination) && IsValid(copy->sampler) &&
                        copy->sourceRect.width != 0 && copy->sourceRect.height != 0 &&
                        Contains(RenderTapeRect{0, 0, tape.ViewportWidth(), tape.ViewportHeight()},
                                 copy->sourceRect) &&
                        validateSource(copy->sourceSession, copy->sourceGeneration,
                                       copy->sourceSurfaceGeneration);
                    if (valid && !addCandidateRef(copy->destination))
                    {
                        valid = false;
                    }
                }
                else if (const auto *download = std::get_if<DownloadTargetRgba8Request>(&request))
                {
                    valid =
                        download->requestId != 0 && download->sourceFrameSequence != 0 &&
                        download->sourceFrameSequence < frameSequence &&
                        download->rect.width != 0 && download->rect.height != 0 &&
                        Contains(RenderTapeRect{0, 0, tape.ViewportWidth(), tape.ViewportHeight()},
                                 download->rect) &&
                        validateSource(download->sourceSession, download->sourceGeneration,
                                       download->sourceSurfaceGeneration);
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
            if (!valid)
            {
                ReportRenderFrameRejection(IsValid(rejectedAsset)
                                               ? RenderFrameRejection::MissingLogicalAsset
                                               : RenderFrameRejection::InvalidTapeContents,
                                           id, frameSequence, rejectedAsset);
                continue;
            }

            for (const LogicalRenderAssetRef asset : tape.LogicalAssets())
            {
                if (!hasCandidateRef(asset))
                {
                    valid = false;
                    break;
                }
            }
            valid = valid && candidateRefs.size() == tape.LogicalAssets().size();
            if (!valid)
            {
                ReportRenderFrameRejection(RenderFrameRejection::AssetTableMismatch, id,
                                           frameSequence);
                continue;
            }

            for (const LogicalRenderAssetLease &candidateAsset : candidateAssets)
            {
                bool alreadyAdded = false;
                for (const LogicalRenderAssetLease &existing : frame->assets_)
                {
                    if (SameAsset(existing, candidateAsset.asset))
                    {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (!alreadyAdded)
                {
                    frame->assets_.push_back(candidateAsset);
                }
            }
            frame->sessions_.push_back(SessionRecord{id, candidate->Generation(), *destination,
                                                     std::move(*candidate->renderTape_)});
            candidate->renderTape_.reset();
        }
        frame->validatedByApplication_ = true;
        return frame;
    }
    catch (...)
    {
        return nullptr;
    }
}

std::optional<ApplicationRenderTapeStorage::FrameLease> ApplicationRenderTapeStorage::PrepareFrame(
    const ApplicationFramePlan &plan) noexcept
{
    std::vector<PreparedSessionIdentity> preparedIdentities;

    if (!PrepareIdentities(plan, preparedIdentities))
    {
        return std::nullopt;
    }

    const std::uint64_t frameSequence = plan.FrameSequence();
    if (frameSequence == 0 ||
        (lastPreparedFrameSequence_.has_value() && frameSequence <= *lastPreparedFrameSequence_))
    {
        return std::nullopt;
    }

    std::size_t freeCount = 0;
    for (const std::shared_ptr<RenderTapeBlock> &block : blocks_)
    {
        if (block.use_count() == 1)
        {
            ++freeCount;
        }
    }

    // Retain every block at its high watermark. Grow only when current work
    // needs more free blocks than prior frames left available.
    while (freeCount < preparedIdentities.size())
    {
        try
        {
            blocks_.push_back(std::make_shared<RenderTapeBlock>());
        }
        catch (...)
        {
            return std::nullopt;
        }
        ++freeCount;
    }

    // Collect exactly renderRequiredCount free blocks for this frame.
    std::vector<std::shared_ptr<RenderTapeBlock>> reserved;
    try
    {
        reserved.reserve(preparedIdentities.size());
        for (const std::shared_ptr<RenderTapeBlock> &block : blocks_)
        {
            if (reserved.size() >= preparedIdentities.size())
            {
                break;
            }
            if (block.use_count() == 1)
            {
                reserved.push_back(block);
            }
        }
    }
    catch (...)
    {
        return std::nullopt;
    }
    if (reserved.size() != preparedIdentities.size())
    {
        return std::nullopt;
    }

    lastPreparedFrameSequence_ = frameSequence;
    return FrameLease(std::move(reserved), std::move(preparedIdentities), frameSequence);
}

bool ApplicationRenderTapeStorage::PrepareIdentities(
    const ApplicationFramePlan &plan, std::vector<PreparedSessionIdentity> &identities) noexcept
{
    try
    {
        identities.reserve(plan.SessionCount());
        for (std::size_t index = 0; index < plan.SessionCount(); ++index)
        {
            const SessionFrameInput &input = plan.Session(index);
            if (!input.renderRequired)
            {
                continue;
            }
            if (input.sessionId.RawValue() == 0 || input.generation.RawValue() == 0 ||
                input.surfaceGeneration == 0 || input.targetWidth == 0 || input.targetHeight == 0)
            {
                return false;
            }
            identities.push_back(PreparedSessionIdentity{input.sessionId, input.generation,
                                                         input.surfaceGeneration, input.targetWidth,
                                                         input.targetHeight});
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::optional<SessionRenderTapeRecording> ApplicationRenderTapeStorage::FrameLease::
    AcquireChildBlock(SessionId id, SessionGeneration generation, std::uint64_t surfaceGeneration,
                      std::uint32_t viewportWidth, std::uint32_t viewportHeight) noexcept
{
    if (nextIndex_ >= reserved_.size() || viewportWidth == 0 || viewportHeight == 0)
    {
        return std::nullopt;
    }

    const PreparedSessionIdentity &prepared = preparedIdentities_[nextIndex_];
    if (prepared.id != id || prepared.generation != generation ||
        prepared.surfaceGeneration != surfaceGeneration ||
        prepared.viewportWidth != viewportWidth || prepared.viewportHeight != viewportHeight)
    {
        return std::nullopt;
    }

    std::shared_ptr<RenderTapeBlock> block = std::move(reserved_[nextIndex_++]);
    block->PrepareForLease();
    return SessionRenderTapeRecording(std::move(block), id, generation, frameSequence_,
                                      surfaceGeneration, viewportWidth, viewportHeight);
}

namespace
{
template <std::size_t N> bool AllFinite(const std::array<float, N> &values) noexcept
{
    for (float value : values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

bool IsValidComponentCount(RenderClientArraySemantic semantic, std::uint32_t count) noexcept
{
    switch (semantic)
    {
    case RenderClientArraySemantic::Position:
        return count == 2 || count == 3 || count == 4;
    case RenderClientArraySemantic::Color:
        return count == 3 || count == 4;
    case RenderClientArraySemantic::TextureCoordinate:
        return count >= 1 && count <= 4;
    case RenderClientArraySemantic::Normal:
        return count == 3;
    }
    return false;
}

// ---- CPU matrix math (section 5.4): standard OpenGL fixed-function
// semantics, column-major 16-float storage (element = column*4 + row),
// matching glLoadMatrixf/glMultMatrixf so a later oracle can pass these
// arrays straight through. Every Multiply4x4(current, X) call below is
// exactly the post-multiply glMultMatrixf(X) performs on `current`.

std::array<float, 16> Multiply4x4(const std::array<float, 16> &a,
                                  const std::array<float, 16> &b) noexcept
{
    std::array<float, 16> result{};
    for (std::size_t col = 0; col < 4; ++col)
    {
        for (std::size_t row = 0; row < 4; ++row)
        {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k)
            {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            result[col * 4 + row] = sum;
        }
    }
    return result;
}

std::array<float, 4> TransformPoint4(const std::array<float, 16> &m,
                                     const std::array<float, 4> &p) noexcept
{
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
    {
        result[row] = m[0 * 4 + row] * p[0] + m[1 * 4 + row] * p[1] + m[2 * 4 + row] * p[2] +
                      m[3 * 4 + row] * p[3];
    }
    return result;
}

std::array<float, 16> TranslationMatrix(float x, float y, float z) noexcept
{
    std::array<float, 16> m = RenderTapeIdentityMatrix4x4;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

std::array<float, 16> ScaleMatrix(float x, float y, float z) noexcept
{
    std::array<float, 16> m = RenderTapeIdentityMatrix4x4;
    m[0] = x;
    m[5] = y;
    m[10] = z;
    return m;
}

// glRotatef's standard axis-angle matrix (OpenGL 1.1 spec). Undefined for
// a zero-length axis in real GL; this fail-closed tape rejects that case
// outright instead of guessing a result.
std::optional<std::array<float, 16>> RotationMatrix(float degrees, float x, float y,
                                                    float z) noexcept
{
    const float lengthSq = x * x + y * y + z * z;
    if (!std::isfinite(degrees) || !std::isfinite(lengthSq) || lengthSq < 1e-12F)
    {
        return std::nullopt;
    }
    const float invLength = 1.0F / std::sqrt(lengthSq);
    const float nx = x * invLength;
    const float ny = y * invLength;
    const float nz = z * invLength;
    const float radians = degrees * (std::numbers::pi_v<float> / 180.0F);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float oneMinusC = 1.0F - c;

    std::array<float, 16> m = RenderTapeIdentityMatrix4x4;
    m[0] = nx * nx * oneMinusC + c;
    m[1] = nx * ny * oneMinusC + nz * s;
    m[2] = nx * nz * oneMinusC - ny * s;
    m[4] = nx * ny * oneMinusC - nz * s;
    m[5] = ny * ny * oneMinusC + c;
    m[6] = ny * nz * oneMinusC + nx * s;
    m[8] = nx * nz * oneMinusC + ny * s;
    m[9] = ny * nz * oneMinusC - nx * s;
    m[10] = nz * nz * oneMinusC + c;
    return m;
}

std::array<float, 16> OrthoMatrix(float left, float right, float bottom, float top, float zNear,
                                  float zFar) noexcept
{
    std::array<float, 16> m{};
    m[0] = 2.0F / (right - left);
    m[5] = 2.0F / (top - bottom);
    m[10] = -2.0F / (zFar - zNear);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(zFar + zNear) / (zFar - zNear);
    m[15] = 1.0F;
    return m;
}

// gluPerspective's standard derivation (field of view -> glFrustum).
std::array<float, 16> PerspectiveMatrix(float fovYDegrees, float aspect, float zNear,
                                        float zFar) noexcept
{
    std::array<float, 16> m{};
    const float f = 1.0F / std::tan(fovYDegrees * (std::numbers::pi_v<float> / 180.0F) * 0.5F);
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.0F;
    m[14] = (2.0F * zFar * zNear) / (zNear - zFar);
    return m;
}

// ---- Canonical primitive expansion (section 5.3) -----------------

constexpr std::size_t MaxSize = (std::numeric_limits<std::size_t>::max)();

void CopyVerticesIfNeeded(std::span<const RenderTapeVertex> source,
                          std::span<RenderTapeVertex> destination) noexcept
{
    if (source.data() != destination.data())
    {
        std::copy(source.begin(), source.end(), destination.begin());
    }
}

void CopyPackedBmdVertices(const float *positions, const float *textureCoordinates,
                           const float *colors, std::size_t count,
                           const std::array<float, 4> &latchedColor,
                           const std::array<float, 3> &latchedNormal,
                           RenderTapeVertex *output) noexcept
{
    if (colors != nullptr)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            output->position = {positions[0], positions[1], positions[2], 1.0F};
            output->textureCoordinate = {textureCoordinates[0], textureCoordinates[1]};
            output->color = {colors[0], colors[1], colors[2], colors[3]};
            output->normal = latchedNormal;
            positions += 3;
            textureCoordinates += 2;
            colors += 4;
            ++output;
        }
        return;
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        output->position = {positions[0], positions[1], positions[2], 1.0F};
        output->textureCoordinate = {textureCoordinates[0], textureCoordinates[1]};
        output->color = latchedColor;
        output->normal = latchedNormal;
        positions += 3;
        textureCoordinates += 2;
        ++output;
    }
}

// These helpers only calculate and write into caller-provided tape
// ranges. They deliberately do not build a second vector-sized cache.
std::optional<std::size_t> TriangleIndexCount(LegacyPrimitive primitive,
                                              std::size_t vertexCount) noexcept
{
    switch (primitive)
    {
    case LegacyPrimitive::Triangles:
        return vertexCount % 3 == 0 ? std::optional<std::size_t>(vertexCount) : std::nullopt;
    case LegacyPrimitive::TriangleStrip:
    case LegacyPrimitive::TriangleFan:
    case LegacyPrimitive::Polygon:
        if (vertexCount < 3)
        {
            return std::size_t{0};
        }
        if (vertexCount - 2 > MaxSize / 3)
        {
            return std::nullopt;
        }
        return (vertexCount - 2) * 3;
    case LegacyPrimitive::Quads:
        if (vertexCount % 4 != 0 || vertexCount / 4 > MaxSize / 6)
        {
            return std::nullopt;
        }
        return (vertexCount / 4) * 6;
    default:
        return std::nullopt;
    }
}

bool WriteTriangleIndices(LegacyPrimitive primitive, std::size_t vertexCount,
                          std::span<std::uint32_t> output) noexcept
{
    const std::optional<std::size_t> indexCount = TriangleIndexCount(primitive, vertexCount);
    if (!indexCount.has_value() || output.size() < *indexCount ||
        vertexCount > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    std::uint32_t *cursor = output.data();
    switch (primitive)
    {
    case LegacyPrimitive::Triangles:
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            *cursor++ = static_cast<std::uint32_t>(i);
        }
        break;
    case LegacyPrimitive::TriangleStrip:
        for (std::size_t i = 0; i + 2 < vertexCount; ++i)
        {
            const std::uint32_t a = static_cast<std::uint32_t>(i);
            const std::uint32_t b = static_cast<std::uint32_t>(i + 1);
            const std::uint32_t c = static_cast<std::uint32_t>(i + 2);
            if (i % 2 == 0)
            {
                *cursor++ = a;
                *cursor++ = b;
                *cursor++ = c;
            }
            else
            {
                *cursor++ = b;
                *cursor++ = a;
                *cursor++ = c;
            }
        }
        break;
    case LegacyPrimitive::TriangleFan:
    case LegacyPrimitive::Polygon:
        for (std::size_t i = 1; i + 1 < vertexCount; ++i)
        {
            *cursor++ = 0;
            *cursor++ = static_cast<std::uint32_t>(i);
            *cursor++ = static_cast<std::uint32_t>(i + 1);
        }
        break;
    case LegacyPrimitive::Quads:
        for (std::size_t base = 0; base < vertexCount; base += 4)
        {
            const std::uint32_t b0 = static_cast<std::uint32_t>(base);
            const std::uint32_t b1 = static_cast<std::uint32_t>(base + 1);
            const std::uint32_t b2 = static_cast<std::uint32_t>(base + 2);
            const std::uint32_t b3 = static_cast<std::uint32_t>(base + 3);
            *cursor++ = b0;
            *cursor++ = b1;
            *cursor++ = b2;
            *cursor++ = b0;
            *cursor++ = b2;
            *cursor++ = b3;
        }
        break;
    default:
        return false;
    }
    return cursor == output.data() + *indexCount;
}

std::optional<std::size_t> LineIndexCount(LegacyPrimitive primitive,
                                          std::size_t vertexCount) noexcept
{
    switch (primitive)
    {
    case LegacyPrimitive::Lines:
        return vertexCount % 2 == 0 ? std::optional<std::size_t>(vertexCount) : std::nullopt;
    case LegacyPrimitive::LineStrip:
        if (vertexCount < 2)
        {
            return std::size_t{0};
        }
        if (vertexCount - 1 > MaxSize / 2)
        {
            return std::nullopt;
        }
        return (vertexCount - 1) * 2;
    case LegacyPrimitive::LineLoop:
        if (vertexCount < 2)
        {
            return std::size_t{0};
        }
        if (vertexCount > MaxSize / 2)
        {
            return std::nullopt;
        }
        return vertexCount * 2;
    default:
        return std::nullopt;
    }
}

bool WriteLineIndices(LegacyPrimitive primitive, std::size_t vertexCount,
                      std::span<std::uint32_t> output) noexcept
{
    const std::optional<std::size_t> indexCount = LineIndexCount(primitive, vertexCount);
    if (!indexCount.has_value() || output.size() < *indexCount ||
        vertexCount > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    std::size_t cursor = 0;
    if (primitive == LegacyPrimitive::Lines)
    {
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            output[cursor++] = static_cast<std::uint32_t>(i);
        }
    }
    else
    {
        for (std::size_t i = 0; i + 1 < vertexCount; ++i)
        {
            output[cursor++] = static_cast<std::uint32_t>(i);
            output[cursor++] = static_cast<std::uint32_t>(i + 1);
        }
        if (primitive == LegacyPrimitive::LineLoop && vertexCount >= 2)
        {
            output[cursor++] = static_cast<std::uint32_t>(vertexCount - 1);
            output[cursor++] = 0;
        }
    }
    return cursor == *indexCount;
}

bool TriangleIndicesAt(LegacyPrimitive primitive, std::size_t triangle, std::size_t vertexCount,
                       std::array<std::uint32_t, 3> &output) noexcept
{
    if (!TriangleIndexCount(primitive, vertexCount).has_value() ||
        vertexCount > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }
    switch (primitive)
    {
    case LegacyPrimitive::Triangles: {
        const std::size_t base = triangle * 3;
        output = {static_cast<std::uint32_t>(base), static_cast<std::uint32_t>(base + 1),
                  static_cast<std::uint32_t>(base + 2)};
        return base + 2 < vertexCount;
    }
    case LegacyPrimitive::TriangleStrip: {
        const std::size_t base = triangle;
        const std::uint32_t a = static_cast<std::uint32_t>(base);
        const std::uint32_t b = static_cast<std::uint32_t>(base + 1);
        const std::uint32_t c = static_cast<std::uint32_t>(base + 2);
        output = base % 2 == 0 ? std::array{a, b, c} : std::array{b, a, c};
        return base + 2 < vertexCount;
    }
    case LegacyPrimitive::TriangleFan:
    case LegacyPrimitive::Polygon:
        output = {0, static_cast<std::uint32_t>(triangle + 1),
                  static_cast<std::uint32_t>(triangle + 2)};
        return triangle + 2 < vertexCount;
    case LegacyPrimitive::Quads: {
        const std::size_t base = (triangle / 2) * 4;
        const std::uint32_t b0 = static_cast<std::uint32_t>(base);
        const std::uint32_t b1 = static_cast<std::uint32_t>(base + 1);
        const std::uint32_t b2 = static_cast<std::uint32_t>(base + 2);
        const std::uint32_t b3 = static_cast<std::uint32_t>(base + 3);
        output = triangle % 2 == 0 ? std::array{b0, b1, b2} : std::array{b0, b2, b3};
        return base + 3 < vertexCount;
    }
    default:
        return false;
    }
}

bool ClassifyProjectedTriangle(std::span<const RenderTapeVertex> vertices,
                               const std::array<std::uint32_t, 3> &indices,
                               RenderFrontFace frontFace,
                               const std::array<float, 16> &modelViewProjection,
                               bool &outFrontFacing, RenderTapeFailure &outFailure) noexcept
{
    if (indices[0] >= vertices.size() || indices[1] >= vertices.size() ||
        indices[2] >= vertices.size())
    {
        outFailure = RenderTapeFailure::InvalidArgument;
        return false;
    }
    const std::array<float, 4> clipA =
        TransformPoint4(modelViewProjection, vertices[indices[0]].position);
    const std::array<float, 4> clipB =
        TransformPoint4(modelViewProjection, vertices[indices[1]].position);
    const std::array<float, 4> clipC =
        TransformPoint4(modelViewProjection, vertices[indices[2]].position);
    if (!AllFinite(clipA) || !AllFinite(clipB) || !AllFinite(clipC))
    {
        outFailure = RenderTapeFailure::NonFiniteValue;
        return false;
    }
    if (clipA[3] == 0.0F || clipB[3] == 0.0F || clipC[3] == 0.0F)
    {
        outFailure = RenderTapeFailure::InvalidArgument;
        return false;
    }
    const float ax = clipA[0] / clipA[3];
    const float ay = clipA[1] / clipA[3];
    const float bx = clipB[0] / clipB[3];
    const float by = clipB[1] / clipB[3];
    const float cx = clipC[0] / clipC[3];
    const float cy = clipC[1] / clipC[3];
    const float signedArea = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    outFrontFacing =
        frontFace == RenderFrontFace::CounterClockwise ? signedArea > 0.0F : signedArea < 0.0F;
    return true;
}

struct TriangleClassificationCounts final
{
    std::size_t fillIndexCount = 0;
    std::size_t lineIndexCount = 0;
};

std::optional<TriangleClassificationCounts> CountClassifiedTriangles(
    LegacyPrimitive primitive, std::span<const RenderTapeVertex> vertices,
    RenderFrontFace frontFace, const std::array<float, 16> &modelView,
    const std::array<float, 16> &projection, RenderTapeFailure &outFailure) noexcept
{
    const std::optional<std::size_t> triangleIndexCount =
        TriangleIndexCount(primitive, vertices.size());
    if (!triangleIndexCount.has_value())
    {
        outFailure = RenderTapeFailure::UnbalancedPrimitive;
        return std::nullopt;
    }
    const std::array<float, 16> modelViewProjection = Multiply4x4(projection, modelView);
    TriangleClassificationCounts counts;
    const std::size_t triangleCount = *triangleIndexCount / 3;
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        std::array<std::uint32_t, 3> indices{};
        if (!TriangleIndicesAt(primitive, triangle, vertices.size(), indices))
        {
            outFailure = RenderTapeFailure::InvalidArgument;
            return std::nullopt;
        }
        bool frontFacing = false;
        if (!ClassifyProjectedTriangle(vertices, indices, frontFace, modelViewProjection,
                                       frontFacing, outFailure))
        {
            return std::nullopt;
        }
        const std::size_t add = frontFacing ? 6U : 3U;
        std::size_t &target = frontFacing ? counts.lineIndexCount : counts.fillIndexCount;
        if (target > MaxSize - add)
        {
            outFailure = RenderTapeFailure::IndexCapacity;
            return std::nullopt;
        }
        target += add;
    }
    return counts;
}

bool WriteClassifiedTriangles(LegacyPrimitive primitive, std::span<const RenderTapeVertex> vertices,
                              RenderFrontFace frontFace, const std::array<float, 16> &modelView,
                              const std::array<float, 16> &projection,
                              std::span<std::uint32_t> fillOutput,
                              std::span<std::uint32_t> lineOutput,
                              RenderTapeFailure &outFailure) noexcept
{
    const std::optional<std::size_t> triangleIndexCount =
        TriangleIndexCount(primitive, vertices.size());
    if (!triangleIndexCount.has_value())
    {
        outFailure = RenderTapeFailure::InvalidArgument;
        return false;
    }
    const std::array<float, 16> modelViewProjection = Multiply4x4(projection, modelView);
    std::size_t fillCursor = 0;
    std::size_t lineCursor = 0;
    for (std::size_t triangle = 0; triangle < *triangleIndexCount / 3; ++triangle)
    {
        std::array<std::uint32_t, 3> indices{};
        bool frontFacing = false;
        if (!TriangleIndicesAt(primitive, triangle, vertices.size(), indices) ||
            !ClassifyProjectedTriangle(vertices, indices, frontFace, modelViewProjection,
                                       frontFacing, outFailure))
        {
            return false;
        }
        if (frontFacing)
        {
            if (lineCursor + 6 > lineOutput.size())
            {
                outFailure = RenderTapeFailure::IndexCapacity;
                return false;
            }
            lineOutput[lineCursor++] = indices[0];
            lineOutput[lineCursor++] = indices[1];
            lineOutput[lineCursor++] = indices[1];
            lineOutput[lineCursor++] = indices[2];
            lineOutput[lineCursor++] = indices[2];
            lineOutput[lineCursor++] = indices[0];
        }
        else
        {
            if (fillCursor + 3 > fillOutput.size())
            {
                outFailure = RenderTapeFailure::IndexCapacity;
                return false;
            }
            fillOutput[fillCursor++] = indices[0];
            fillOutput[fillCursor++] = indices[1];
            fillOutput[fillCursor++] = indices[2];
        }
    }
    return fillCursor == fillOutput.size() && lineCursor == lineOutput.size();
}

// Each point becomes a one-pixel clip-space quad. The output spans point
// into the tape's uncommitted reservation; processing backwards keeps the
// source span safe when DrawArrays supplied that same reservation as input.
bool ExpandPointsToClipQuads(std::span<const RenderTapeVertex> points,
                             std::span<RenderTapeVertex> outputVertices,
                             std::span<std::uint32_t> outputIndices,
                             const std::array<float, 16> &modelView,
                             const std::array<float, 16> &projection, std::uint32_t viewportWidth,
                             std::uint32_t viewportHeight, RenderTapeFailure &outFailure) noexcept
{
    if (points.size() > MaxSize / 4 || points.size() > MaxSize / 6 ||
        outputVertices.size() < points.size() * 4 || outputIndices.size() < points.size() * 6)
    {
        outFailure = RenderTapeFailure::VertexCapacity;
        return false;
    }
    const std::array<float, 16> modelViewProjection = Multiply4x4(projection, modelView);
    for (std::size_t pointIndex = points.size(); pointIndex-- > 0;)
    {
        const RenderTapeVertex point = points[pointIndex];
        const std::array<float, 4> clip = TransformPoint4(modelViewProjection, point.position);
        if (!AllFinite(clip))
        {
            outFailure = RenderTapeFailure::NonFiniteValue;
            return false;
        }
        if (clip[3] == 0.0F || viewportWidth == 0 || viewportHeight == 0)
        {
            outFailure = RenderTapeFailure::InvalidArgument;
            return false;
        }
        const float halfX = clip[3] / static_cast<float>(viewportWidth);
        const float halfY = clip[3] / static_cast<float>(viewportHeight);
        const std::size_t vertexBase = pointIndex * 4;
        const std::array<std::array<float, 2>, 4> offsets{
            {{-halfX, -halfY}, {halfX, -halfY}, {halfX, halfY}, {-halfX, halfY}}};
        for (std::size_t corner = 0; corner < offsets.size(); ++corner)
        {
            RenderTapeVertex expanded = point;
            expanded.position = {clip[0] + offsets[corner][0], clip[1] + offsets[corner][1],
                                 clip[2], clip[3]};
            outputVertices[vertexBase + corner] = expanded;
        }
        const std::size_t indexBase = pointIndex * 6;
        outputIndices[indexBase + 0] = static_cast<std::uint32_t>(vertexBase);
        outputIndices[indexBase + 1] = static_cast<std::uint32_t>(vertexBase + 1);
        outputIndices[indexBase + 2] = static_cast<std::uint32_t>(vertexBase + 2);
        outputIndices[indexBase + 3] = static_cast<std::uint32_t>(vertexBase);
        outputIndices[indexBase + 4] = static_cast<std::uint32_t>(vertexBase + 2);
        outputIndices[indexBase + 5] = static_cast<std::uint32_t>(vertexBase + 3);
    }
    return true;
}

bool SphereSizes(std::uint32_t slices, std::uint32_t stacks, std::size_t &vertexCount,
                 std::size_t &indexCount) noexcept
{
    if (slices < 1 || stacks < 1)
    {
        return false;
    }

    constexpr std::size_t indicesPerCell = 6;
    const std::uint64_t rows = static_cast<std::uint64_t>(stacks) + 1U;
    const std::uint64_t columns = static_cast<std::uint64_t>(slices) + 1U;
    const std::uint64_t cells =
        static_cast<std::uint64_t>(stacks) * static_cast<std::uint64_t>(slices);
    if (rows > (std::numeric_limits<std::size_t>::max)() / columns ||
        cells > (std::numeric_limits<std::size_t>::max)() / indicesPerCell)
    {
        return false;
    }
    vertexCount = static_cast<std::size_t>(rows * columns);
    indexCount = static_cast<std::size_t>(cells * indicesPerCell);
    return true;
}

// Deterministic replacement for gluNewQuadric()/gluSphere() (section
// 5.7). Its caller preflights both spans in the current tape block.
void GenerateDeterministicSphere(float radius, std::uint32_t slices, std::uint32_t stacks,
                                 std::span<RenderTapeVertex> outVertices,
                                 std::span<std::uint32_t> outIndices) noexcept
{
    const float pi = std::numbers::pi_v<float>;
    const auto rowStride = slices + 1;
    for (std::uint32_t i = 0; i <= stacks; ++i)
    {
        const float phi = pi * static_cast<float>(i) / static_cast<float>(stacks);
        const float y = std::cos(phi);
        const float ringRadius = std::sin(phi);
        for (std::uint32_t j = 0; j <= slices; ++j)
        {
            const float theta = 2.0F * pi * static_cast<float>(j) / static_cast<float>(slices);
            const float x = ringRadius * std::cos(theta);
            const float z = ringRadius * std::sin(theta);
            RenderTapeVertex vertex;
            vertex.position = {radius * x, radius * y, radius * z, 1.0F};
            vertex.normal = {x, y, z};
            vertex.textureCoordinate = {static_cast<float>(j) / static_cast<float>(slices),
                                        static_cast<float>(i) / static_cast<float>(stacks)};
            vertex.color = {1.0F, 1.0F, 1.0F, 1.0F};
            outVertices[static_cast<std::size_t>(i) * rowStride + j] = vertex;
        }
    }

    constexpr std::size_t indicesPerCell = 6;
    for (std::uint32_t i = 0; i < stacks; ++i)
    {
        for (std::uint32_t j = 0; j < slices; ++j)
        {
            const std::uint32_t v00 = i * rowStride + j;
            const std::uint32_t v01 = i * rowStride + j + 1;
            const std::uint32_t v10 = (i + 1) * rowStride + j;
            const std::uint32_t v11 = (i + 1) * rowStride + j + 1;
            const std::size_t indexBase =
                (static_cast<std::size_t>(i) * slices + j) * indicesPerCell;
            outIndices[indexBase + 0] = v00;
            outIndices[indexBase + 1] = v11;
            outIndices[indexBase + 2] = v10;
            outIndices[indexBase + 3] = v00;
            outIndices[indexBase + 4] = v01;
            outIndices[indexBase + 5] = v11;
        }
    }
}
} // namespace

// ==== Frame / pass lifecycle ==========================================

bool LegacyRenderFacade::BeginFrame(SessionId id, SessionGeneration generation,
                                    std::uint64_t frameSequence, std::uint64_t surfaceGeneration,
                                    std::uint32_t viewportWidth, std::uint32_t viewportHeight,
                                    SessionRenderTapeRecording recording) noexcept
{
    if (recording_.has_value() || recording.Spent() || recording.Failed() || ownerId_ != id ||
        recording.Id() != id || recording.Generation() != generation ||
        recording.FrameSequence() != frameSequence ||
        recording.SurfaceGeneration() != surfaceGeneration ||
        recording.ViewportWidth() != viewportWidth || recording.ViewportHeight() != viewportHeight)
    {
        return false;
    }
    recording_ = std::move(recording);
    generation_ = generation;
    frameSequence_ = frameSequence;
    surfaceGeneration_ = surfaceGeneration;
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    ResetFrameState();
    return true;
}

void LegacyRenderFacade::ResetFrameState() noexcept
{
    lastPalette_ = {};
    paletteAppends_ = paletteReuses_ = 0;
    passOpen_ = false;
    anyPassCompleted_ = false;
    primitiveOpen_ = false;
    currentPass_ = RenderTapePass::Terrain;
    immediateVertices_.clear();
    currentTexCoord_ = {0.0F, 0.0F};
    nextQuadInstanceRunId_ = 1;
    activeQuadInstanceRunId_ = 0;
    quadInstanceRunConstants_ = {};
    quadInstanceRunTexture_ = {};

    matrixMode_ = LegacyMatrixMode::ModelView;
    modelView_.fill(RenderTapeIdentityMatrix4x4);
    projection_.fill(RenderTapeIdentityMatrix4x4);
    texture_.fill(RenderTapeIdentityMatrix4x4);
    modelViewDepth_ = 1;
    projectionDepth_ = 1;
    textureDepth_ = 1;

    attribStack_.fill(RenderTapeConstants{});
    attribDepth_ = 0;
    clientArrays_ = {};
    clientArrayEnabled_.fill(false);
    clientAttribStack_.fill(ClientAttribSnapshot{});
    clientAttribDepth_ = 0;

    pendingConstants_ = RenderTapeConstants{};
    const RenderTapeRect fullSurface{0, 0, viewportWidth_, viewportHeight_};
    pendingConstants_.viewport = fullSurface;
    pendingConstants_.scissor = fullSurface;
    drawStateGeneration_ = 1;
    quadInstanceRunStateGeneration_ = 0;
    pendingClearColor_ = {0.0F, 0.0F, 0.0F, 0.0F};
    pendingClearDepth_ = 1.0F;
    pendingClearStencil_ = 0;
    currentTexture_ = {};
}

void LegacyRenderFacade::ResetPassState(const SessionFogPassConstants &fog) noexcept
{
    matrixMode_ = LegacyMatrixMode::ModelView;
    modelView_.fill(RenderTapeIdentityMatrix4x4);
    projection_.fill(RenderTapeIdentityMatrix4x4);
    texture_.fill(RenderTapeIdentityMatrix4x4);
    modelViewDepth_ = 1;
    projectionDepth_ = 1;
    textureDepth_ = 1;
    attribStack_.fill(RenderTapeConstants{});
    attribDepth_ = 0;
    clientArrays_ = {};
    clientArrayEnabled_.fill(false);
    clientAttribStack_.fill(ClientAttribSnapshot{});
    clientAttribDepth_ = 0;

    pendingConstants_ = RenderTapeConstants{};
    const RenderTapeRect fullSurface{0, 0, viewportWidth_, viewportHeight_};
    pendingConstants_.viewport = fullSurface;
    pendingConstants_.scissor = fullSurface;
    pendingConstants_.fogEnable = fog.Enabled();
    pendingConstants_.fogColor = fog.Color();
    pendingConstants_.fogStart = fog.Start();
    pendingConstants_.fogEnd = fog.End();
    drawStateGeneration_ = 1;
    quadInstanceRunStateGeneration_ = 0;

    currentTexCoord_ = {0.0F, 0.0F};
    primitiveOpen_ = false;
    immediateVertices_.clear();
    pendingClearColor_ = {0.0F, 0.0F, 0.0F, 0.0F};
    pendingClearDepth_ = 1.0F;
    pendingClearStencil_ = 0;
    currentTexture_ = {};
}

bool LegacyRenderFacade::BeginPass(RenderTapePass pass, const SessionFogPassConstants &fog) noexcept
{
    if (!recording_.has_value() || recording_->Failed() || passOpen_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(pass))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    ResetPassState(fog);
    currentPass_ = pass;
    passOpen_ = true;
    return true;
}

bool LegacyRenderFacade::EndPass() noexcept
{
    if (!recording_.has_value() || recording_->Failed() || !passOpen_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (primitiveOpen_)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedPrimitive);
        return false;
    }
    if (modelViewDepth_ != 1)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedMatrixStack);
        return false;
    }
    if (projectionDepth_ != 1)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedMatrixStack);
        return false;
    }
    if (textureDepth_ != 1)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedMatrixStack);
        return false;
    }
    if (attribDepth_ != 0 || clientAttribDepth_ != 0)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedAttributeStack);
        return false;
    }
    passOpen_ = false;
    anyPassCompleted_ = true;
    return true;
}

std::optional<SessionRenderTape> LegacyRenderFacade::Finalize() noexcept
{
    if (!recording_.has_value())
    {
        return std::nullopt;
    }
    if (passOpen_ || !anyPassCompleted_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
    }
    std::optional<SessionRenderTape> tape = recording_->Finalize();
    recording_.reset();
    ResetFrameState();
    return tape;
}

void LegacyRenderFacade::Abort() noexcept
{
    if (recording_.has_value())
    {
        recording_->Abort();
        recording_.reset();
    }
    ResetFrameState();
}

void LegacyRenderFacade::LatchFacadeFailure(RenderTapeFailure failure,
                                            std::source_location location) noexcept
{
    if (recording_.has_value())
    {
        recording_->LatchFailure(failure, SourceRow(location), currentPass_);
    }
}

bool LegacyRenderFacade::CanRecordInPass() const noexcept
{
    return recording_.has_value() && !recording_->Failed() && passOpen_;
}

bool LegacyRenderFacade::CanMutateState() const noexcept
{
    return CanRecordInPass() && !primitiveOpen_;
}

// ==== Immediate mode ===================================================

bool LegacyRenderFacade::Begin(LegacyPrimitive primitive) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(primitive))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    primitiveOpen_ = true;
    currentPrimitive_ = primitive;
    immediateVertices_.clear();
    return true;
}

bool LegacyRenderFacade::End() noexcept
{
    if (!CanRecordInPass() || !primitiveOpen_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    primitiveOpen_ = false;
    const bool ok = EmitPrimitiveDraw(currentPrimitive_, immediateVertices_);
    immediateVertices_.clear();
    return ok;
}

RenderTapeVertex LegacyRenderFacade::LatchVertex(
    const std::array<float, 4> &position) const noexcept
{
    RenderTapeVertex vertex;
    vertex.position = position;
    vertex.textureCoordinate = currentTexCoord_;
    vertex.color = pendingConstants_.color;
    vertex.normal = pendingConstants_.normal;
    return vertex;
}

bool LegacyRenderFacade::Vertex3(float x, float y, float z) noexcept
{
    if (!CanRecordInPass() || !primitiveOpen_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    try
    {
        immediateVertices_.push_back(LatchVertex({x, y, z, 1.0F}));
    }
    catch (...)
    {
        LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
        return false;
    }
    return true;
}

bool LegacyRenderFacade::Vertex2(float x, float y) noexcept
{
    return Vertex3(x, y, 0.0F);
}

bool LegacyRenderFacade::Color4(float r, float g, float b, float a) noexcept
{
    if (!CanRecordInPass())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    pendingConstants_.color = {r, g, b, a};
    return true;
}

bool LegacyRenderFacade::Color3(float r, float g, float b) noexcept
{
    return Color4(r, g, b, 1.0F);
}

bool LegacyRenderFacade::Color4Ub(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                  std::uint8_t a) noexcept
{
    constexpr float Scale = 1.0F / 255.0F;
    return Color4(static_cast<float>(r) * Scale, static_cast<float>(g) * Scale,
                  static_cast<float>(b) * Scale, static_cast<float>(a) * Scale);
}

bool LegacyRenderFacade::Color3Ub(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
{
    return Color4Ub(r, g, b, 255);
}

bool LegacyRenderFacade::Normal3(float x, float y, float z) noexcept
{
    if (!CanRecordInPass())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    pendingConstants_.normal = {x, y, z};
    return true;
}

bool LegacyRenderFacade::TexCoord2(float u, float v) noexcept
{
    if (!CanRecordInPass())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    currentTexCoord_ = {u, v};
    return true;
}

// ==== Primitive emission (shared by End() and DrawArrays()) ===========

bool LegacyRenderFacade::EmitExpandedTriangles(LegacyPrimitive primitive,
                                               std::span<const RenderTapeVertex> vertices) noexcept
{
    const std::optional<std::size_t> indexCount = TriangleIndexCount(primitive, vertices.size());
    if (!indexCount.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedPrimitive);
        return false;
    }
    if (*indexCount == 0)
    {
        return true;
    }

    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    RenderPipelineKey pipeline;
    pipeline.positionSpace = RenderPositionSpace::Object;

    if (pendingConstants_.polygonFrontMode != RenderPolygonMode::Line)
    {
        const std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests{
            SessionRenderTapeRecording::ReservedDrawRequest{
                *indexCount, RenderIndexTopology::Triangles, pipeline, constants, currentTexture_,
                drawStateGeneration_}};
        const std::optional<SessionRenderTapeRecording::DrawReservation> reservation =
            recording_->ReserveDraws(currentPass_, vertices.size(), requests,
                                     SourceRow(std::source_location::current()));
        if (!reservation.has_value())
        {
            return false;
        }
        CopyVerticesIfNeeded(vertices, reservation->vertices);
        if (!WriteTriangleIndices(primitive, vertices.size(), reservation->indices))
        {
            LatchFacadeFailure(RenderTapeFailure::IndexCapacity);
            return false;
        }
        return recording_->CommitDraws(*reservation, requests);
    }

    RenderTapeFailure failure = RenderTapeFailure::None;
    const std::optional<TriangleClassificationCounts> classified = CountClassifiedTriangles(
        primitive, vertices, pendingConstants_.frontFace, modelView_[modelViewDepth_ - 1],
        projection_[projectionDepth_ - 1], failure);
    if (!classified.has_value())
    {
        LatchFacadeFailure(failure);
        return false;
    }
    if (classified->fillIndexCount == 0 && classified->lineIndexCount == 0)
    {
        return true;
    }
    std::array<SessionRenderTapeRecording::ReservedDrawRequest, 2> requests{};
    std::size_t requestCount = 0;
    if (classified->fillIndexCount != 0)
    {
        requests[requestCount++] = {classified->fillIndexCount,
                                    RenderIndexTopology::Triangles,
                                    pipeline,
                                    constants,
                                    currentTexture_,
                                    drawStateGeneration_};
    }
    if (classified->lineIndexCount != 0)
    {
        requests[requestCount++] = {classified->lineIndexCount,
                                    RenderIndexTopology::Lines,
                                    pipeline,
                                    constants,
                                    currentTexture_,
                                    drawStateGeneration_};
    }
    const std::span<const SessionRenderTapeRecording::ReservedDrawRequest> requestSpan(
        requests.data(), requestCount);
    const std::optional<SessionRenderTapeRecording::DrawReservation> reservation =
        recording_->ReserveDraws(currentPass_, vertices.size(), requestSpan,
                                 SourceRow(std::source_location::current()));
    if (!reservation.has_value())
    {
        return false;
    }
    CopyVerticesIfNeeded(vertices, reservation->vertices);
    const std::span<std::uint32_t> fillOutput =
        reservation->indices.first(classified->fillIndexCount);
    const std::span<std::uint32_t> lineOutput =
        reservation->indices.subspan(classified->fillIndexCount, classified->lineIndexCount);
    if (!WriteClassifiedTriangles(
            primitive, vertices, pendingConstants_.frontFace, modelView_[modelViewDepth_ - 1],
            projection_[projectionDepth_ - 1], fillOutput, lineOutput, failure))
    {
        LatchFacadeFailure(failure);
        return false;
    }
    return recording_->CommitDraws(*reservation, requestSpan);
}

bool LegacyRenderFacade::EmitPrimitiveDraw(LegacyPrimitive primitive,
                                           std::span<const RenderTapeVertex> vertices) noexcept
{
    if (primitive == LegacyPrimitive::Points)
    {
        if (vertices.size() > MaxSize / 4 || vertices.size() > MaxSize / 6)
        {
            LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
            return false;
        }
        const std::size_t expandedVertexCount = vertices.size() * 4;
        const std::size_t expandedIndexCount = vertices.size() * 6;
        RenderTapeConstants constants = pendingConstants_;
        constants.modelView = modelView_[modelViewDepth_ - 1];
        constants.projection = projection_[projectionDepth_ - 1];
        constants.textureMatrix = texture_[textureDepth_ - 1];
        RenderPipelineKey pipeline;
        pipeline.positionSpace = RenderPositionSpace::Clip;
        const std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests{
            SessionRenderTapeRecording::ReservedDrawRequest{
                expandedIndexCount, RenderIndexTopology::Triangles, pipeline, constants,
                currentTexture_, drawStateGeneration_}};
        const std::optional<SessionRenderTapeRecording::DrawReservation> reservation =
            recording_->ReserveDraws(currentPass_, expandedVertexCount, requests,
                                     SourceRow(std::source_location::current()));
        if (!reservation.has_value())
        {
            return false;
        }
        RenderTapeFailure failure = RenderTapeFailure::None;
        if (!ExpandPointsToClipQuads(vertices, reservation->vertices, reservation->indices,
                                     modelView_[modelViewDepth_ - 1],
                                     projection_[projectionDepth_ - 1], viewportWidth_,
                                     viewportHeight_, failure))
        {
            LatchFacadeFailure(failure);
            return false;
        }
        return recording_->CommitDraws(*reservation, requests);
    }

    const bool isLineFamily = primitive == LegacyPrimitive::Lines ||
                              primitive == LegacyPrimitive::LineStrip ||
                              primitive == LegacyPrimitive::LineLoop;
    if (isLineFamily)
    {
        const std::optional<std::size_t> indexCount = LineIndexCount(primitive, vertices.size());
        if (!indexCount.has_value())
        {
            LatchFacadeFailure(RenderTapeFailure::UnbalancedPrimitive);
            return false;
        }
        if (*indexCount == 0)
        {
            return true;
        }
        RenderTapeConstants constants = pendingConstants_;
        constants.modelView = modelView_[modelViewDepth_ - 1];
        constants.projection = projection_[projectionDepth_ - 1];
        constants.textureMatrix = texture_[textureDepth_ - 1];
        RenderPipelineKey pipeline;
        pipeline.positionSpace = RenderPositionSpace::Object;
        const std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests{
            SessionRenderTapeRecording::ReservedDrawRequest{*indexCount, RenderIndexTopology::Lines,
                                                            pipeline, constants, currentTexture_,
                                                            drawStateGeneration_}};
        const std::optional<SessionRenderTapeRecording::DrawReservation> reservation =
            recording_->ReserveDraws(currentPass_, vertices.size(), requests,
                                     SourceRow(std::source_location::current()));
        if (!reservation.has_value())
        {
            return false;
        }
        CopyVerticesIfNeeded(vertices, reservation->vertices);
        if (!WriteLineIndices(primitive, vertices.size(), reservation->indices))
        {
            LatchFacadeFailure(RenderTapeFailure::IndexCapacity);
            return false;
        }
        return recording_->CommitDraws(*reservation, requests);
    }

    return EmitExpandedTriangles(primitive, vertices);
}

std::optional<LegacyRenderFacade::IndexedTriangleReservation> LegacyRenderFacade::
    ReserveIndexedTriangles(std::size_t vertexCount, std::size_t indexCount) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return std::nullopt;
    }
    if (vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 ||
        vertexCount > (std::numeric_limits<std::uint32_t>::max)())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return std::nullopt;
    }
    if (UsesPolygonLineMode())
    {
        LatchFacadeFailure(RenderTapeFailure::UnsupportedSemantic);
        return std::nullopt;
    }

    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    RenderPipelineKey pipeline;
    pipeline.positionSpace = RenderPositionSpace::Object;
    std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests{
        SessionRenderTapeRecording::ReservedDrawRequest{indexCount, RenderIndexTopology::Triangles,
                                                        pipeline, constants, currentTexture_,
                                                        drawStateGeneration_}};
    std::optional<SessionRenderTapeRecording::DrawReservation> draw = recording_->ReserveSingleDraw(
        currentPass_, vertexCount, requests[0], SourceRow(std::source_location::current()));
    if (!draw.has_value())
    {
        return std::nullopt;
    }
    return IndexedTriangleReservation{*draw, requests};
}

bool LegacyRenderFacade::CommitIndexedTriangles(
    const IndexedTriangleReservation &reservation) noexcept
{
    return recording_->CommitSingleDraw(reservation.draw, reservation.requests[0]);
}

// ==== Typed client arrays ===============================================

bool LegacyRenderFacade::SetClientArray(RenderClientArraySemantic semantic,
                                        std::span<const std::byte> storage,
                                        std::uint32_t componentCount,
                                        RenderClientArrayScalarType scalarType, std::int32_t stride,
                                        bool normalized) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(semantic) || !IsValid(scalarType) || stride < 0 ||
        !IsValidComponentCount(semantic, componentCount))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    clientArrays_[static_cast<std::size_t>(semantic)] =
        ClientArrayBinding{storage, componentCount, scalarType, stride, normalized};
    return true;
}

void LegacyRenderFacade::EnableClientArray(RenderClientArraySemantic semantic) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return;
    }
    if (IsValid(semantic))
    {
        clientArrayEnabled_[static_cast<std::size_t>(semantic)] = true;
    }
    else
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
    }
}

void LegacyRenderFacade::DisableClientArray(RenderClientArraySemantic semantic) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return;
    }
    if (IsValid(semantic))
    {
        clientArrayEnabled_[static_cast<std::size_t>(semantic)] = false;
    }
    else
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
    }
}

bool LegacyRenderFacade::ReadArrayVertices(std::int32_t first, std::int32_t count,
                                           std::span<RenderTapeVertex> outVertices) noexcept
{
    constexpr auto PositionIndex = static_cast<std::size_t>(RenderClientArraySemantic::Position);
    if (!clientArrayEnabled_[PositionIndex])
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (outVertices.size() < static_cast<std::size_t>(count))
    {
        LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
        return false;
    }
    const std::size_t firstIndex = static_cast<std::size_t>(first);
    const std::size_t vertexCount = static_cast<std::size_t>(count);
    const std::size_t lastIndex = firstIndex + vertexCount - 1U;
    std::array<std::size_t, 4> strides{};
    for (std::size_t semanticIndex = 0; semanticIndex < clientArrays_.size(); ++semanticIndex)
    {
        if (!clientArrayEnabled_[semanticIndex])
        {
            continue;
        }

        const ClientArrayBinding &binding = clientArrays_[semanticIndex];
        const std::size_t elementBytes =
            static_cast<std::size_t>(binding.componentCount) * sizeof(float);
        const std::size_t stride =
            binding.stride == 0 ? elementBytes : static_cast<std::size_t>(binding.stride);
        if (elementBytes > binding.storage.size() ||
            lastIndex > (binding.storage.size() - elementBytes) / stride)
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
            return false;
        }
        strides[semanticIndex] = stride;
    }

    constexpr auto ColorIndex = static_cast<std::size_t>(RenderClientArraySemantic::Color);
    constexpr auto TexCoordIndex =
        static_cast<std::size_t>(RenderClientArraySemantic::TextureCoordinate);
    constexpr auto NormalIndex = static_cast<std::size_t>(RenderClientArraySemantic::Normal);
    const ClientArrayBinding &positions = clientArrays_[PositionIndex];
    const ClientArrayBinding &colors = clientArrays_[ColorIndex];
    const ClientArrayBinding &texCoords = clientArrays_[TexCoordIndex];
    const ClientArrayBinding &normals = clientArrays_[NormalIndex];
    const bool colorEnabled = clientArrayEnabled_[ColorIndex];
    const bool texCoordEnabled = clientArrayEnabled_[TexCoordIndex];
    const bool normalEnabled = clientArrayEnabled_[NormalIndex];
    const std::size_t positionStride = strides[PositionIndex];
    const std::size_t colorStride = strides[ColorIndex];
    const std::size_t texCoordStride = strides[TexCoordIndex];
    const std::size_t normalStride = strides[NormalIndex];
    const std::byte *positionSource = positions.storage.data() + firstIndex * positionStride;
    const std::byte *colorSource =
        colorEnabled ? colors.storage.data() + firstIndex * colorStride : nullptr;
    const std::byte *texCoordSource =
        texCoordEnabled ? texCoords.storage.data() + firstIndex * texCoordStride : nullptr;
    const std::byte *normalSource =
        normalEnabled ? normals.storage.data() + firstIndex * normalStride : nullptr;

    const bool packedBmdArrays =
        positions.componentCount == 3 && positionStride == 3U * sizeof(float) && texCoordEnabled &&
        texCoords.componentCount == 2 && texCoordStride == 2U * sizeof(float) && !normalEnabled &&
        (!colorEnabled || (colors.componentCount == 4 && colorStride == 4U * sizeof(float))) &&
        reinterpret_cast<std::uintptr_t>(positionSource) % alignof(float) == 0 &&
        reinterpret_cast<std::uintptr_t>(texCoordSource) % alignof(float) == 0 &&
        (!colorEnabled || reinterpret_cast<std::uintptr_t>(colorSource) % alignof(float) == 0);
    if (packedBmdArrays)
    {
        const float *packedPositions = reinterpret_cast<const float *>(positionSource);
        const float *packedTexCoords = reinterpret_cast<const float *>(texCoordSource);
        const float *packedColors =
            colorEnabled ? reinterpret_cast<const float *>(colorSource) : nullptr;
        CopyPackedBmdVertices(packedPositions, packedTexCoords, packedColors, vertexCount,
                              pendingConstants_.color, pendingConstants_.normal,
                              outVertices.data());
        return true;
    }

    const std::size_t positionBytes =
        static_cast<std::size_t>(positions.componentCount) * sizeof(float);
    const std::size_t colorBytes = static_cast<std::size_t>(colors.componentCount) * sizeof(float);
    const std::size_t texCoordBytes =
        (std::min)(static_cast<std::size_t>(texCoords.componentCount), std::size_t{2}) *
        sizeof(float);
    for (std::size_t i = 0; i < vertexCount; ++i)
    {
        RenderTapeVertex &vertex = outVertices[i];

        vertex.position = {0.0F, 0.0F, 0.0F, 1.0F};
        std::memcpy(vertex.position.data(), positionSource, positionBytes);
        positionSource += positionStride;

        vertex.color = pendingConstants_.color;
        if (colorEnabled)
        {
            vertex.color = {0.0F, 0.0F, 0.0F, 1.0F};
            std::memcpy(vertex.color.data(), colorSource, colorBytes);
            colorSource += colorStride;
        }

        vertex.textureCoordinate = currentTexCoord_;
        if (texCoordEnabled)
        {
            vertex.textureCoordinate = {0.0F, 0.0F};
            std::memcpy(vertex.textureCoordinate.data(), texCoordSource, texCoordBytes);
            texCoordSource += texCoordStride;
        }

        vertex.normal = pendingConstants_.normal;
        if (normalEnabled)
        {
            std::memcpy(vertex.normal.data(), normalSource, 3U * sizeof(float));
            normalSource += normalStride;
        }
    }
    return true;
}

bool LegacyRenderFacade::CountClassifiedArrayTriangles(LegacyPrimitive primitive,
                                                       std::int32_t first, std::int32_t count,
                                                       std::size_t &fillIndexCount,
                                                       std::size_t &lineIndexCount) noexcept
{
    fillIndexCount = 0;
    lineIndexCount = 0;
    const std::size_t vertexCount = static_cast<std::size_t>(count);
    const std::optional<std::size_t> indexCount = TriangleIndexCount(primitive, vertexCount);
    if (!indexCount.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedPrimitive);
        return false;
    }
    const std::array<float, 16> modelViewProjection =
        Multiply4x4(projection_[projectionDepth_ - 1], modelView_[modelViewDepth_ - 1]);
    for (std::size_t triangle = 0; triangle < *indexCount / 3; ++triangle)
    {
        std::array<std::uint32_t, 3> sourceIndices{};
        if (!TriangleIndicesAt(primitive, triangle, vertexCount, sourceIndices))
        {
            LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
            return false;
        }
        std::array<RenderTapeVertex, 3> vertices{};
        for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
        {
            const std::uint64_t sourceIndex =
                static_cast<std::uint64_t>(first) + sourceIndices[vertex];
            if (sourceIndex >
                    static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()) ||
                !ReadArrayVertices(static_cast<std::int32_t>(sourceIndex), 1,
                                   std::span<RenderTapeVertex>(&vertices[vertex], 1)))
            {
                if (!recording_->Failed())
                {
                    LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
                }
                return false;
            }
        }
        bool frontFacing = false;
        RenderTapeFailure failure = RenderTapeFailure::None;
        if (!ClassifyProjectedTriangle(vertices, std::array<std::uint32_t, 3>{0, 1, 2},
                                       pendingConstants_.frontFace, modelViewProjection,
                                       frontFacing, failure))
        {
            LatchFacadeFailure(failure);
            return false;
        }
        std::size_t &target = frontFacing ? lineIndexCount : fillIndexCount;
        const std::size_t add = frontFacing ? 6U : 3U;
        if (target > MaxSize - add)
        {
            LatchFacadeFailure(RenderTapeFailure::IndexCapacity);
            return false;
        }
        target += add;
    }
    return true;
}

bool LegacyRenderFacade::DrawArrays(LegacyPrimitive primitive, std::int32_t first,
                                    std::int32_t count) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(primitive) || first < 0 || count < 0)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    if (count == 0)
    {
        return true;
    }
    if (!recording_->HasEntryCapacity())
    {
        LatchFacadeFailure(RenderTapeFailure::EntryCapacity);
        return false;
    }
    const std::span<RenderTapeVertex> scratch =
        recording_->ReserveVertices(static_cast<std::size_t>(count));
    if (scratch.size() != static_cast<std::size_t>(count))
    {
        LatchFacadeFailure(RenderTapeFailure::VertexCapacity);
        return false;
    }
    const std::span<RenderTapeVertex> vertices = scratch.first(static_cast<std::size_t>(count));
    if (!ReadArrayVertices(first, count, vertices))
    {
        return false;
    }
    return EmitPrimitiveDraw(primitive, vertices);
}

bool LegacyRenderFacade::DrawGeometry(const LogicalGeometryAssetLease &geometry,
                                      std::uint32_t indexOffset, std::uint32_t indexCount) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    return recording_->AppendGeometryDraw(currentPass_, geometry, indexOffset, indexCount,
                                          RenderIndexTopology::Triangles, RenderPipelineKey{},
                                          constants, currentTexture_, 0, drawStateGeneration_);
}

bool LegacyRenderFacade::BuildTrustedGeometryDraw(const LogicalGeometryAssetLease &geometry,
                                                  std::uint32_t indexOffset,
                                                  std::uint32_t indexCount,
                                                  TrustedGeometryDraw &draw) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    draw = TrustedGeometryDraw{
        &geometry,           0,          static_cast<std::uint32_t>(geometry.vertices->size()),
        indexOffset,         indexCount, RenderIndexTopology::Triangles,
        RenderPipelineKey{}, constants,  currentTexture_,
        drawStateGeneration_};
    return true;
}

bool LegacyRenderFacade::AppendTrustedGeometryDrawBatch(
    std::span<const TrustedGeometryDraw> draws) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    return recording_->AppendTrustedGeometryDrawBatch(currentPass_, draws);
}

std::uint64_t LegacyRenderFacade::BeginGrassGeometryRun(
    const RenderTapeGrassConstants &grass) noexcept
{
    const std::uint64_t runId = BeginQuadInstanceRun();
    if (runId != 0)
    {
        quadInstanceRunConstants_.grass = grass;
        quadInstanceRunConstants_.grass.runId = runId;
    }
    return runId;
}

bool LegacyRenderFacade::DrawTerrainInstances(const LogicalGeometryAssetLease &geometry,
                                              std::uint32_t instanceOffset,
                                              std::uint32_t instanceCount,
                                              const RenderTapeTerrainConstants &terrain) noexcept
{
    if (!CanMutateState() || !recording_.has_value() || instanceCount == 0)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::Terrain;
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    constants.quad = {0, instanceOffset, instanceCount};
    constants.terrain = terrain;
    return recording_->AppendGeometryDraw(currentPass_, geometry, 0, 4, 0, 6,
                                           RenderIndexTopology::Triangles, pipeline, constants,
                                           currentTexture_);
}

bool LegacyRenderFacade::DrawGrassGeometry(const LogicalGeometryAssetLease &geometry,
                                           std::uint32_t indexOffset, std::uint32_t indexCount,
                                           std::uint64_t runId) noexcept
{
    if (!CanMutateState() || !recording_.has_value() || runId == 0 ||
        runId != activeQuadInstanceRunId_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::Grass;
    RenderTapeConstants constants = quadInstanceRunConstants_;
    constants.quad = {runId, indexOffset, indexCount};
    return recording_->AppendGeometryDraw(currentPass_, geometry, 0, 4, 0, 6,
                                           RenderIndexTopology::Triangles, pipeline, constants,
                                           quadInstanceRunTexture_);
}

std::uint64_t LegacyRenderFacade::BeginQuadInstanceRun() noexcept
{
    if (!CanMutateState() || nextQuadInstanceRunId_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return 0;
    }
    activeQuadInstanceRunId_ = nextQuadInstanceRunId_++;
    quadInstanceRunConstants_ = pendingConstants_;
    quadInstanceRunConstants_.modelView = modelView_[modelViewDepth_ - 1];
    quadInstanceRunConstants_.projection = projection_[projectionDepth_ - 1];
    quadInstanceRunConstants_.textureMatrix = texture_[textureDepth_ - 1];
    quadInstanceRunTexture_ = currentTexture_;
    quadInstanceRunStateGeneration_ = drawStateGeneration_;
    return activeQuadInstanceRunId_;
}

bool LegacyRenderFacade::CanContinueQuadInstanceRun(std::uint64_t runId) const noexcept
{
    return runId != 0 && runId == activeQuadInstanceRunId_ &&
           quadInstanceRunStateGeneration_ == drawStateGeneration_ &&
           quadInstanceRunTexture_ == currentTexture_;
}

std::optional<SessionRenderTapeRecording::TrailSampleReservation> LegacyRenderFacade::
    ReserveTrailSamples(std::size_t count) noexcept
{
    if (!CanMutateState() || !recording_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return std::nullopt;
    }
    return recording_->ReserveTrailSamples(count);
}

std::uint64_t LegacyRenderFacade::BeginTrailInstanceRun(
    const RenderTapeTrailConstants &trail) noexcept
{
    const auto runId = BeginQuadInstanceRun();
    if (runId != 0)
    {
        quadInstanceRunConstants_.trail = trail;
        quadInstanceRunConstants_.bmd.enabled = false;
        quadInstanceRunConstants_.bmd.rigidInstanceOffset =
            quadInstanceRunConstants_.bmd.rigidInstanceCount = 0;
    }
    return runId;
}

bool LegacyRenderFacade::DrawTrailInstance(const LogicalGeometryAssetLease &geometry,
                                           const RenderTapeTrailInstance &instance,
                                           std::uint64_t runId, bool blur) noexcept
{
    if (!CanMutateState() || !recording_ || runId == 0 || runId != activeQuadInstanceRunId_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode =
        blur ? RenderGeometryMode::BlurInstances : RenderGeometryMode::TrailInstances;
    return recording_->AppendTrailInstance(currentPass_, geometry, instance, runId, pipeline,
                                           quadInstanceRunConstants_, quadInstanceRunTexture_,
                                           SourceRow(std::source_location::current()));
}

std::uint64_t LegacyRenderFacade::BeginParticleInstanceRun(const float camera[3][4]) noexcept
{
    const auto runId = BeginQuadInstanceRun();
    if (runId == 0)
        return 0;
    // Particle mode borrows the existing three-row instance-transform uniform
    // layout. The normal model-view matrix still applies after billboard expansion.
    auto &bmd = quadInstanceRunConstants_.bmd;
    bmd.enabled = false;
    bmd.rigidInstanceOffset = bmd.rigidInstanceCount = 0;
    bmd.rigidTransform = {{camera[0][0], camera[0][1], camera[0][2], camera[0][3]},
                          {camera[1][0], camera[1][1], camera[1][2], camera[1][3]},
                          {camera[2][0], camera[2][1], camera[2][2], camera[2][3]}};
    return runId;
}

bool LegacyRenderFacade::DrawParticleInstance(const LogicalGeometryAssetLease &geometry,
                                              const RenderTapeParticleInstance &instance,
                                              std::uint64_t runId) noexcept
{
    if (!CanMutateState() || !recording_.has_value() || runId == 0 ||
        runId != activeQuadInstanceRunId_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::ParticleInstances;
    return recording_->AppendParticleInstance(currentPass_, geometry, instance, runId, pipeline,
                                              quadInstanceRunConstants_, quadInstanceRunTexture_,
                                              SourceRow(std::source_location::current()));
}

std::uint64_t LegacyRenderFacade::BeginTextGlyphRun(LogicalRenderAssetRef asset) noexcept
{
    const std::uint64_t runId = BeginQuadInstanceRun();
    if (runId == 0)
    {
        return 0;
    }
    quadInstanceRunTexture_ = asset;
    quadInstanceRunConstants_.depthTestEnable = false;
    quadInstanceRunConstants_.depthWriteEnable = false;
    quadInstanceRunConstants_.cullEnable = false;
    quadInstanceRunConstants_.blendEnable = true;
    quadInstanceRunConstants_.blendSrc = RenderBlendFactor::SrcAlpha;
    quadInstanceRunConstants_.blendDst = RenderBlendFactor::OneMinusSrcAlpha;
    quadInstanceRunConstants_.alphaTestEnable = false;
    quadInstanceRunConstants_.fogEnable = false;
    quadInstanceRunConstants_.lightingEnable = false;
    quadInstanceRunConstants_.textureEnable = true;
    quadInstanceRunConstants_.textureEnvironment = RenderTextureEnvironment::Modulate;
    quadInstanceRunConstants_.textureMatrix = RenderTapeIdentityMatrix4x4;
    quadInstanceRunConstants_.stencilEnable = false;
    quadInstanceRunConstants_.colorWriteMask = {true, true, true, true};
    quadInstanceRunConstants_.polygonFrontMode = RenderPolygonMode::Fill;
    return runId;
}

bool LegacyRenderFacade::DrawQuadInstance(const LogicalGeometryAssetLease &geometry,
                                          const RenderTapeQuadInstance &instance,
                                          std::uint64_t runId) noexcept
{
    if (!CanMutateState() || !recording_.has_value() || runId == 0 ||
        runId != activeQuadInstanceRunId_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::QuadInstances;
    return recording_->AppendQuadInstance(currentPass_, geometry, instance, runId, pipeline,
                                          quadInstanceRunConstants_, quadInstanceRunTexture_,
                                          SourceRow(std::source_location::current()));
}

bool LegacyRenderFacade::DrawSpriteInstance(const LogicalGeometryAssetLease &geometry,
                                            const RenderTapeQuadInstance &instance,
                                            std::uint64_t runId) noexcept
{
    if (!CanMutateState() || !recording_.has_value() || runId == 0 ||
        runId != activeQuadInstanceRunId_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::SpriteInstances;
    return recording_->AppendQuadInstance(currentPass_, geometry, instance, runId, pipeline,
                                          quadInstanceRunConstants_, quadInstanceRunTexture_,
                                          SourceRow(std::source_location::current()));
}

std::optional<std::uint32_t> LegacyRenderFacade::AppendBoneMatrices(
    std::span<const RenderTapeBoneMatrix> matrices, bool stablePose) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return std::nullopt;
    }
    if (stablePose && !lastPalette_.empty() && matrices.data() == lastPalette_.data() &&
        matrices.size() == lastPalette_.size())
    {
        ++paletteReuses_;
        return lastPaletteOffset_;
    }
    lastPalette_ = {};
    const auto offset = recording_->AppendBoneMatrices(currentPass_, matrices);
    if (offset)
    {
        ++paletteAppends_;
        if (stablePose)
        {
            lastPalette_ = matrices;
            lastPaletteOffset_ = *offset;
        }
    }
    return offset;
}

bool LegacyRenderFacade::DrawBmdGeometry(const LogicalGeometryAssetLease &geometry,
                                         std::uint32_t vertexOffset, std::uint32_t vertexCount,
                                         std::uint32_t indexOffset, std::uint32_t indexCount,
                                         const RenderTapeBmdConstants &bmd) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    constants.bmd = bmd;
    return recording_->AppendGeometryDraw(currentPass_, geometry, vertexOffset, vertexCount,
                                          indexOffset, indexCount, RenderIndexTopology::Triangles,
                                          RenderPipelineKey{}, constants, currentTexture_);
}

bool LegacyRenderFacade::DrawRigidInstances(const LogicalGeometryAssetLease &geometry,
                                            std::uint32_t vertexOffset, std::uint32_t vertexCount,
                                            std::uint32_t indexOffset, std::uint32_t indexCount,
                                            std::span<const RenderTapeRigidInstance> instances,
                                            const RenderTapeBmdConstants &bmd) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    constants.bmd = bmd;
    return recording_->AppendRigidInstances(currentPass_, geometry, vertexOffset, vertexCount,
                                            indexOffset, indexCount, instances, constants,
                                            currentTexture_);
}

bool LegacyRenderFacade::DrawBmdShadowGeometry(const LogicalGeometryAssetLease &geometry,
                                               const LogicalGeometryAssetLease &terrain,
                                               std::uint32_t vertexOffset,
                                               std::uint32_t vertexCount, std::uint32_t indexOffset,
                                               std::uint32_t indexCount,
                                               const RenderTapeBmdConstants &bmd) noexcept
{
    if (!CanMutateState() || !recording_.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    constants.bmd = bmd;
    return recording_->AppendGeometryDraw(currentPass_, geometry, vertexOffset, vertexCount,
                                          indexOffset, indexCount, RenderIndexTopology::Triangles,
                                          RenderPipelineKey{}, constants, currentTexture_, 0,
                                          &terrain);
}

bool LegacyRenderFacade::Sphere(float radius, std::uint32_t slices, std::uint32_t stacks) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(radius) || radius <= 0.0F)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }

    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    if (!SphereSizes(slices, stacks, vertexCount, indexCount))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }

    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    RenderPipelineKey pipeline;
    pipeline.positionSpace = RenderPositionSpace::Object;
    const std::array<SessionRenderTapeRecording::ReservedDrawRequest, 1> requests{
        SessionRenderTapeRecording::ReservedDrawRequest{indexCount, RenderIndexTopology::Triangles,
                                                        pipeline, constants, currentTexture_,
                                                        drawStateGeneration_}};
    const std::optional<SessionRenderTapeRecording::DrawReservation> reservation =
        recording_->ReserveDraws(currentPass_, vertexCount, requests,
                                 SourceRow(std::source_location::current()));
    if (!reservation.has_value())
    {
        return false;
    }

    GenerateDeterministicSphere(radius, slices, stacks, reservation->vertices,
                                reservation->indices);
    return recording_->CommitDraws(*reservation, requests);
}

// ==== CPU matrix stack ==================================================

std::span<std::array<float, 16>> LegacyRenderFacade::CurrentMatrixStack() noexcept
{
    switch (matrixMode_)
    {
    case LegacyMatrixMode::Projection:
        return {projection_.data(), projection_.size()};
    case LegacyMatrixMode::Texture:
        return {texture_.data(), texture_.size()};
    case LegacyMatrixMode::ModelView:
        break;
    }
    return {modelView_.data(), modelView_.size()};
}

std::span<const std::array<float, 16>> LegacyRenderFacade::CurrentMatrixStack() const noexcept
{
    switch (matrixMode_)
    {
    case LegacyMatrixMode::Projection:
        return {projection_.data(), projection_.size()};
    case LegacyMatrixMode::Texture:
        return {texture_.data(), texture_.size()};
    case LegacyMatrixMode::ModelView:
        break;
    }
    return {modelView_.data(), modelView_.size()};
}

std::size_t &LegacyRenderFacade::CurrentMatrixDepth() noexcept
{
    switch (matrixMode_)
    {
    case LegacyMatrixMode::Projection:
        return projectionDepth_;
    case LegacyMatrixMode::Texture:
        return textureDepth_;
    case LegacyMatrixMode::ModelView:
        break;
    }
    return modelViewDepth_;
}

std::size_t LegacyRenderFacade::CurrentMatrixDepth() const noexcept
{
    switch (matrixMode_)
    {
    case LegacyMatrixMode::Projection:
        return projectionDepth_;
    case LegacyMatrixMode::Texture:
        return textureDepth_;
    case LegacyMatrixMode::ModelView:
        break;
    }
    return modelViewDepth_;
}

std::array<float, 16> &LegacyRenderFacade::ActiveMatrix() noexcept
{
    return CurrentMatrixStack()[CurrentMatrixDepth() - 1];
}

bool LegacyRenderFacade::ApplyMatrix(const std::array<float, 16> &next) noexcept
{
    if (!AllFinite(next))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    SetDrawStateValue(ActiveMatrix(), next);
    return true;
}

void LegacyRenderFacade::AdvanceDrawStateGeneration() noexcept
{
    ++drawStateGeneration_;
    if (drawStateGeneration_ == 0)
        drawStateGeneration_ = 1;
    quadInstanceRunStateGeneration_ = 0;
}

bool LegacyRenderFacade::MatrixMode(LegacyMatrixMode mode) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(mode))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    matrixMode_ = mode;
    return true;
}

bool LegacyRenderFacade::LoadIdentity() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    return ApplyMatrix(RenderTapeIdentityMatrix4x4);
}

bool LegacyRenderFacade::LoadMatrix(const std::array<float, 16> &matrix) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    return ApplyMatrix(matrix);
}

bool LegacyRenderFacade::MultMatrix(const std::array<float, 16> &matrix) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!AllFinite(matrix))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    return ApplyMatrix(Multiply4x4(ActiveMatrix(), matrix));
}

bool LegacyRenderFacade::Translate(float x, float y, float z) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    return ApplyMatrix(Multiply4x4(ActiveMatrix(), TranslationMatrix(x, y, z)));
}

bool LegacyRenderFacade::Scale(float x, float y, float z) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    return ApplyMatrix(Multiply4x4(ActiveMatrix(), ScaleMatrix(x, y, z)));
}

bool LegacyRenderFacade::Rotate(float degrees, float x, float y, float z) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    std::optional<std::array<float, 16>> rotation = RotationMatrix(degrees, x, y, z);
    if (!rotation.has_value())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    return ApplyMatrix(Multiply4x4(ActiveMatrix(), *rotation));
}

bool LegacyRenderFacade::Ortho(float left, float right, float bottom, float top, float zNear,
                               float zFar) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) ||
        !std::isfinite(top) || !std::isfinite(zNear) || !std::isfinite(zFar) || left == right ||
        bottom == top || zNear == zFar)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    return ApplyMatrix(
        Multiply4x4(ActiveMatrix(), OrthoMatrix(left, right, bottom, top, zNear, zFar)));
}

bool LegacyRenderFacade::Perspective(float fovYDegrees, float aspect, float zNear,
                                     float zFar) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(fovYDegrees) || fovYDegrees <= 0.0F || fovYDegrees >= 180.0F ||
        !std::isfinite(aspect) || aspect <= 0.0F || !std::isfinite(zNear) || zNear <= 0.0F ||
        !std::isfinite(zFar) || zFar <= zNear)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    return ApplyMatrix(
        Multiply4x4(ActiveMatrix(), PerspectiveMatrix(fovYDegrees, aspect, zNear, zFar)));
}

bool LegacyRenderFacade::PushMatrix() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    std::span<std::array<float, 16>> stack = CurrentMatrixStack();
    std::size_t &depth = CurrentMatrixDepth();
    if (depth >= stack.size())
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedMatrixStack);
        return false;
    }
    stack[depth] = stack[depth - 1];
    ++depth;
    return true;
}

bool LegacyRenderFacade::PopMatrix() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    std::size_t &depth = CurrentMatrixDepth();
    if (depth <= 1)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedMatrixStack);
        return false;
    }
    --depth;
    AdvanceDrawStateGeneration();
    return true;
}

const std::array<float, 16> &LegacyRenderFacade::CurrentMatrix(LegacyMatrixMode mode) const noexcept
{
    switch (mode)
    {
    case LegacyMatrixMode::Projection:
        return projection_[projectionDepth_ - 1];
    case LegacyMatrixMode::Texture:
        return texture_[textureDepth_ - 1];
    case LegacyMatrixMode::ModelView:
        break;
    }
    return modelView_[modelViewDepth_ - 1];
}

// ==== Server/client attribute stack =====================================

bool LegacyRenderFacade::PushAttrib() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (attribDepth_ >= attribStack_.size())
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedAttributeStack);
        return false;
    }
    attribStack_[attribDepth_++] = pendingConstants_;
    return true;
}

bool LegacyRenderFacade::PopAttrib() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (attribDepth_ == 0)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedAttributeStack);
        return false;
    }
    pendingConstants_ = attribStack_[--attribDepth_];
    AdvanceDrawStateGeneration();
    return true;
}

bool LegacyRenderFacade::PushClientAttrib() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (clientAttribDepth_ >= clientAttribStack_.size())
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedAttributeStack);
        return false;
    }
    ClientAttribSnapshot snapshot;
    snapshot.arrays = clientArrays_;
    snapshot.enabled = clientArrayEnabled_;
    clientAttribStack_[clientAttribDepth_++] = snapshot;
    return true;
}

bool LegacyRenderFacade::PopClientAttrib() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (clientAttribDepth_ == 0)
    {
        LatchFacadeFailure(RenderTapeFailure::UnbalancedAttributeStack);
        return false;
    }
    clientArrays_ = clientAttribStack_[--clientAttribDepth_].arrays;
    clientArrayEnabled_ = clientAttribStack_[clientAttribDepth_].enabled;
    return true;
}

// ==== Individual finite state setters ===================================

bool LegacyRenderFacade::SetOpaqueState() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    auto &state = pendingConstants_;
    if (state.blendEnable || !state.cullEnable || !state.depthWriteEnable ||
        state.alphaTestEnable || !state.textureEnable || !state.fogEnable)
    {
        state.blendEnable = state.alphaTestEnable = false;
        state.cullEnable = state.depthWriteEnable = state.textureEnable = state.fogEnable = true;
        AdvanceDrawStateGeneration();
    }
    return true;
}

bool LegacyRenderFacade::SetAlphaTestState(bool depthWrite) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    auto &state = pendingConstants_;
    if (!state.blendEnable || state.blendSrc != RenderBlendFactor::SrcAlpha ||
        state.blendDst != RenderBlendFactor::OneMinusSrcAlpha || state.cullEnable ||
        state.depthWriteEnable != depthWrite || !state.alphaTestEnable || !state.textureEnable ||
        !state.fogEnable)
    {
        state.blendEnable = state.alphaTestEnable = state.textureEnable = state.fogEnable = true;
        state.blendSrc = RenderBlendFactor::SrcAlpha;
        state.blendDst = RenderBlendFactor::OneMinusSrcAlpha;
        state.cullEnable = false;
        state.depthWriteEnable = depthWrite;
        AdvanceDrawStateGeneration();
    }
    return true;
}

bool LegacyRenderFacade::SetAdditiveState() noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    auto &state = pendingConstants_;
    if (!state.blendEnable || state.blendSrc != RenderBlendFactor::One ||
        state.blendDst != RenderBlendFactor::One || state.cullEnable || state.depthWriteEnable ||
        state.alphaTestEnable || !state.textureEnable || state.fogEnable)
    {
        state.blendEnable = state.textureEnable = true;
        state.blendSrc = state.blendDst = RenderBlendFactor::One;
        state.cullEnable = state.depthWriteEnable = state.alphaTestEnable = state.fogEnable = false;
        AdvanceDrawStateGeneration();
    }
    return true;
}

bool LegacyRenderFacade::SetTextureEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.textureEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetDepthTestEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.depthTestEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetDepthWriteEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.depthWriteEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetDepthFunc(RenderCompareFunction compare) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(compare))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.depthCompare, compare);
    return true;
}

bool LegacyRenderFacade::SetCullEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.cullEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetCullFace(RenderCullFace face) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(face))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.cullFace, face);
    return true;
}

bool LegacyRenderFacade::SetFrontFace(RenderFrontFace face) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(face))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.frontFace, face);
    return true;
}

bool LegacyRenderFacade::SetBlendEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.blendEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetBlendFunc(RenderBlendFactor source,
                                      RenderBlendFactor destination) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(source) || !IsValid(destination))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.blendSrc, source);
    SetDrawStateValue(pendingConstants_.blendDst, destination);
    return true;
}

bool LegacyRenderFacade::SetAlphaTestEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.alphaTestEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetAlphaFunc(RenderCompareFunction compare, float reference) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(compare) || !std::isfinite(reference))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.alphaCompare, compare);
    SetDrawStateValue(pendingConstants_.alphaReference, reference);
    return true;
}

bool LegacyRenderFacade::SetFogEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.fogEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetFogMode(RenderFogMode mode) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(mode))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.fogMode, mode);
    return true;
}

bool LegacyRenderFacade::SetFogColor(const std::array<float, 4> &color) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!AllFinite(color))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    SetDrawStateValue(pendingConstants_.fogColor, color);
    return true;
}

bool LegacyRenderFacade::SetFogRange(float start, float end) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(start) || !std::isfinite(end))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    SetDrawStateValue(pendingConstants_.fogStart, start);
    SetDrawStateValue(pendingConstants_.fogEnd, end);
    return true;
}

bool LegacyRenderFacade::SetFogDensity(float density) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(density))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    SetDrawStateValue(pendingConstants_.fogDensity, density);
    return true;
}

bool LegacyRenderFacade::SetLightingEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.lightingEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetColorMask(bool r, bool g, bool b, bool a) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.colorWriteMask, std::array<bool, 4>{r, g, b, a});
    return true;
}

bool LegacyRenderFacade::SetStencilEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.stencilEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetStencilFunc(RenderCompareFunction compare, std::uint32_t reference,
                                        std::uint32_t readMask) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(compare))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.stencilCompare, compare);
    SetDrawStateValue(pendingConstants_.stencilReference, reference);
    SetDrawStateValue(pendingConstants_.stencilReadMask, readMask);
    return true;
}

bool LegacyRenderFacade::SetStencilOp(RenderStencilOperation onFail,
                                      RenderStencilOperation onDepthFail,
                                      RenderStencilOperation onPass) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(onFail) || !IsValid(onDepthFail) || !IsValid(onPass))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.stencilFailOp, onFail);
    SetDrawStateValue(pendingConstants_.stencilDepthFailOp, onDepthFail);
    SetDrawStateValue(pendingConstants_.stencilPassOp, onPass);
    return true;
}

bool LegacyRenderFacade::SetTextureEnvironment(RenderTextureEnvironment environment) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(environment))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.textureEnvironment, environment);
    return true;
}

bool LegacyRenderFacade::SetShadeMode(RenderShadeMode mode) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(mode))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.shadeMode, mode);
    return true;
}

bool LegacyRenderFacade::SetLineWidth(float width) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (width != 1.0F)
    {
        LatchFacadeFailure(RenderTapeFailure::UnsupportedSemantic);
        return false;
    }
    SetDrawStateValue(pendingConstants_.lineWidth, width);
    return true;
}

bool LegacyRenderFacade::SetPolygonMode(RenderCullFace face, RenderPolygonMode mode) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (face != RenderCullFace::Front || !IsValid(mode))
    {
        LatchFacadeFailure(RenderTapeFailure::UnsupportedSemantic);
        return false;
    }
    SetDrawStateValue(pendingConstants_.polygonFrontMode, mode);
    return true;
}

// ==== Viewport, scissor, clear ==========================================

bool LegacyRenderFacade::SetViewport(RenderTapeRect rect) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (rect.width == 0 || rect.height == 0)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }
    SetDrawStateValue(pendingConstants_.viewport, rect);
    return true;
}

bool LegacyRenderFacade::SetScissorEnable(bool enable) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.scissorEnable, enable);
    return true;
}

bool LegacyRenderFacade::SetScissor(RenderTapeRect rect) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    SetDrawStateValue(pendingConstants_.scissor, rect);
    return true;
}

bool LegacyRenderFacade::SetClearColor(const std::array<float, 4> &color) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!AllFinite(color))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    pendingClearColor_ = color;
    return true;
}

bool LegacyRenderFacade::SetClearDepth(float depth) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!std::isfinite(depth))
    {
        LatchFacadeFailure(RenderTapeFailure::NonFiniteValue);
        return false;
    }
    pendingClearDepth_ = depth;
    return true;
}

bool LegacyRenderFacade::SetClearStencilValue(std::uint32_t stencil) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    pendingClearStencil_ = stencil;
    return true;
}

bool LegacyRenderFacade::Clear(bool color, bool depth, bool stencil) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!color && !depth && !stencil)
    {
        return true;
    }
    RenderTapeClear clear;
    clear.clearColor = color;
    clear.clearDepth = depth;
    clear.clearStencil = stencil;
    clear.color = pendingClearColor_;
    clear.depth = pendingClearDepth_;
    clear.stencil = pendingClearStencil_;
    clear.scissorEnable = pendingConstants_.scissorEnable;
    clear.scissor = pendingConstants_.scissor;
    clear.colorWriteMask = pendingConstants_.colorWriteMask;
    clear.depthWriteEnable = pendingConstants_.depthWriteEnable;
    return recording_->AppendClear(currentPass_, clear, SourceRow(std::source_location::current()));
}

bool LegacyRenderFacade::ClearStencil(std::uint32_t value) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    RenderTapeClear clear;
    clear.clearStencil = true;
    clear.stencil = value;
    return recording_->AppendClear(currentPass_, clear, SourceRow(std::source_location::current()));
}

// ==== Logical textures and owner requests ===============================

bool LegacyRenderFacade::DefineTexture2D(LogicalRenderAssetRef destination, std::uint32_t width,
                                         std::uint32_t height, std::span<const std::byte> pixels,
                                         LegacyPixelFormat format, RenderAssetRetention retention,
                                         RenderSamplerIntent sampler) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(destination) || !IsValid(format) || !IsValid(retention) || !IsValid(sampler))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }

    if (format == LegacyPixelFormat::Rgb)
    {
        return recording_->AppendUploadLogicalAssetRgb8(currentPass_, destination, width, height,
                                                        pixels, retention, sampler,
                                                        SourceRow(std::source_location::current()));
    }
    if (!IsExactRgba8ByteCount(width, height, pixels.size()))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }

    return recording_->AppendUploadLogicalAssetRgba8(currentPass_, destination, width, height,
                                                     pixels, retention, sampler,
                                                     SourceRow(std::source_location::current()));
}

bool LegacyRenderFacade::AppendFrameOnlyTextureQuad(
    LogicalRenderAssetRef destination, std::uint32_t width, std::uint32_t height,
    std::span<const std::byte> rgba8, RenderSamplerIntent sampler,
    const std::array<std::array<float, 2>, 4> &vertices) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (!IsValid(destination) || !IsValid(sampler) || width == 0 || height == 0 ||
        !IsExactRgba8ByteCount(width, height, rgba8.size()))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidArgument);
        return false;
    }

    const std::array<std::array<float, 2>, 4> texcoords{
        std::array<float, 2>{0.0F, 1.0F}, std::array<float, 2>{0.0F, 0.0F},
        std::array<float, 2>{1.0F, 0.0F}, std::array<float, 2>{1.0F, 1.0F}};
    std::array<RenderTapeVertex, 4> latchedVertices{};
    for (std::size_t index = 0; index < latchedVertices.size(); ++index)
    {
        latchedVertices[index].position = {vertices[index][0], vertices[index][1], 0.0F, 1.0F};
        latchedVertices[index].color = {1.0F, 1.0F, 1.0F, 1.0F};
        latchedVertices[index].textureCoordinate = {texcoords[index][0], texcoords[index][1]};
        latchedVertices[index].normal = pendingConstants_.normal;
    }
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    RenderTapeConstants constants = pendingConstants_;
    constants.modelView = modelView_[modelViewDepth_ - 1];
    constants.projection = projection_[projectionDepth_ - 1];
    constants.textureMatrix = texture_[textureDepth_ - 1];
    constants.depthTestEnable = false;
    constants.depthWriteEnable = false;
    constants.cullEnable = false;
    constants.blendEnable = true;
    constants.blendSrc = RenderBlendFactor::SrcAlpha;
    constants.blendDst = RenderBlendFactor::OneMinusSrcAlpha;
    constants.alphaTestEnable = false;
    constants.fogEnable = false;
    constants.lightingEnable = false;
    constants.textureEnable = true;
    constants.textureEnvironment = RenderTextureEnvironment::Modulate;
    constants.textureMatrix = RenderTapeIdentityMatrix4x4;
    constants.stencilEnable = false;
    constants.colorWriteMask = {true, true, true, true};
    constants.polygonFrontMode = RenderPolygonMode::Fill;
    RenderPipelineKey pipeline;
    pipeline.positionSpace = RenderPositionSpace::Object;
    const std::uint32_t sourceRow = SourceRow(std::source_location::current());
    const bool appended = recording_->AppendUploadAndDraw(
        currentPass_, destination, width, height, rgba8, RenderAssetRetention::FrameOnly, sampler,
        latchedVertices, indices, RenderIndexTopology::Triangles, pipeline, constants, destination,
        sourceRow);
    if (appended)
    {
        currentTexture_ = destination;
    }
    return appended;
}

void LegacyRenderFacade::BindTexture(LogicalRenderAssetRef asset) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return;
    }
    if (!asset.id.value && asset.revision != 0)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidIdentity);
        return;
    }
    if (asset.id.value != 0 && !IsValid(asset))
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidIdentity);
        return;
    }
    currentTexture_ = asset;
}

bool LegacyRenderFacade::CopyTargetToLogicalTexture(SessionId sourceSession,
                                                    SessionGeneration sourceGeneration,
                                                    std::uint64_t sourceSurfaceGeneration,
                                                    RenderTapeRect sourceRect,
                                                    LogicalRenderAssetRef destination) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (sourceSession != ownerId_ || sourceGeneration != generation_ ||
        sourceSurfaceGeneration != surfaceGeneration_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidIdentity);
        return false;
    }
    const CopyTargetToLogicalTextureRequest request{
        sourceSession,
        sourceGeneration,
        sourceSurfaceGeneration,
        sourceRect,
        destination,
        RenderSamplerIntent{LegacyTextureFilter::Linear, LegacyTextureWrap::ClampToEdge}};
    return recording_->AppendOwnerRequest(currentPass_, RenderOwnerRequest{request},
                                          SourceRow(std::source_location::current()));
}

bool LegacyRenderFacade::DownloadTargetRgba8(SessionId sourceSession,
                                             SessionGeneration sourceGeneration,
                                             std::uint64_t sourceSurfaceGeneration,
                                             std::uint64_t sourceFrameSequence, RenderTapeRect rect,
                                             bool verticallyFlipped,
                                             std::uint64_t requestId) noexcept
{
    if (!CanMutateState())
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidOperation);
        return false;
    }
    if (sourceSession != ownerId_ || sourceGeneration != generation_ ||
        sourceSurfaceGeneration == 0 || sourceFrameSequence == 0 ||
        sourceFrameSequence >= frameSequence_)
    {
        LatchFacadeFailure(RenderTapeFailure::InvalidIdentity);
        return false;
    }
    const DownloadTargetRgba8Request request{
        sourceSession,     sourceGeneration, sourceSurfaceGeneration, sourceFrameSequence, rect,
        verticallyFlipped, requestId};
    return recording_->AppendOwnerRequest(currentPass_, RenderOwnerRequest{request},
                                          SourceRow(std::source_location::current()));
}

namespace
{
constexpr std::size_t MaximumTapeElementCount = (std::numeric_limits<std::uint32_t>::max)();

bool CanGrowBy(std::size_t current, std::size_t added) noexcept
{
    return added <= MaximumTapeElementCount && current <= MaximumTapeElementCount - added;
}

template <typename T> bool EnsureSize(std::vector<T> &storage, std::size_t required) noexcept
{
    if (required <= storage.size())
    {
        return true;
    }
    std::size_t grown = storage.empty() ? 64 : storage.size();
    while (grown < required)
    {
        const std::size_t remaining = MaximumTapeElementCount - grown;
        if (grown > remaining)
        {
            grown = MaximumTapeElementCount;
            break;
        }
        grown *= 2;
    }
    if (grown < required)
    {
        return false;
    }
    try
    {
        storage.resize(grown);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool DrawConstantsMatch(const RenderTapeConstants &left, const RenderTapeConstants &right) noexcept
{
    constexpr std::size_t BeforeColor = offsetof(RenderTapeConstants, color);
    constexpr std::size_t AfterNormal =
        offsetof(RenderTapeConstants, normal) + sizeof(RenderTapeConstants::normal);
    const auto *const leftBytes = reinterpret_cast<const unsigned char *>(&left);
    const auto *const rightBytes = reinterpret_cast<const unsigned char *>(&right);
    return std::memcmp(leftBytes, rightBytes, BeforeColor) == 0 &&
           std::memcmp(leftBytes + AfterNormal, rightBytes + AfterNormal,
                       sizeof(RenderTapeConstants) - AfterNormal) == 0;
}

bool TryMergeGeometryDraw(RenderTapeBlock &block, RenderTapePass pass,
                          LogicalGeometryAssetRef geometry, std::uint32_t vertexOffset,
                          std::uint32_t vertexCount, std::uint32_t indexOffset,
                          std::uint32_t indexCount, RenderIndexTopology topology,
                          RenderPipelineKey pipeline, const RenderTapeConstants &constants,
                          LogicalRenderAssetRef asset, std::uint64_t mergeKey) noexcept
{
    if (pipeline.geometryMode == RenderGeometryMode::Grass && block.drawCount != 0 &&
        block.entryCount != 0 && block.constantsCount != 0)
    {
        const RenderTapeEntry &entry = block.entries[block.entryCount - 1];
        RenderTapeDraw &previous = block.draws[block.drawCount - 1];
        const RenderTapeGrassConstants &previousGrass =
            block.constants[previous.constantsIndex].grass;
        if (entry.kind == RenderTapeEntryKind::Draw && entry.pass == pass &&
            entry.index == block.drawCount - 1 && previous.geometry == geometry &&
            previous.vertexOffset == vertexOffset && previous.vertexCount == vertexCount &&
            previous.indexOffset + previous.indexCount == indexOffset &&
            previous.topology == topology && previous.pipeline == pipeline &&
            previous.asset == asset && constants.grass.runId != 0 &&
            previousGrass.runId == constants.grass.runId)
        {
            previous.indexCount += indexCount;
            return true;
        }
    }
    if (!constants.bmd.enabled && pipeline.geometryMode != RenderGeometryMode::Grass &&
        block.drawCount != 0 && block.entryCount != 0 && block.constantsCount != 0 &&
        block.drawCount <= block.drawMergeKeys.size())
    {
        const RenderTapeEntry &entry = block.entries[block.entryCount - 1];
        RenderTapeDraw &previous = block.draws[block.drawCount - 1];
        if (entry.kind == RenderTapeEntryKind::Draw && entry.pass == pass &&
            entry.index == block.drawCount - 1 && previous.geometry == geometry &&
            previous.vertexOffset == vertexOffset && previous.vertexCount == vertexCount &&
            previous.indexOffset + previous.indexCount == indexOffset &&
            previous.topology == topology && previous.pipeline == pipeline &&
            previous.asset == asset &&
            (mergeKey != 0
                 ? block.drawMergeKeys[block.drawCount - 1] == mergeKey
                 : DrawConstantsMatch(block.constants[previous.constantsIndex], constants)))
        {
            previous.indexCount += indexCount;
            return true;
        }
    }
    return false;
}

std::size_t GeometryAssetBytes(const LogicalGeometryAssetLease &geometry) noexcept
{
    return geometry.vertices->size() * sizeof(RenderTapeVertex) +
           geometry.indices->size() * sizeof(std::uint32_t) +
           (geometry.terrainCells == nullptr
                ? 0
                : geometry.terrainCells->size() * sizeof(RenderTapeTerrainCell)) +
           (geometry.terrainInstances == nullptr
                ? 0
                : geometry.terrainInstances->size() * sizeof(RenderTapeTerrainInstance));
}

bool CanMergeLastDraw(const RenderTapeBlock &block, RenderTapePass pass,
                      const SessionRenderTapeRecording::ReservedDrawRequest &request) noexcept
{
    if (block.entryCount == 0 || block.drawCount == 0 || block.constantsCount == 0 ||
        block.entryCount > block.entries.size() || block.drawCount > block.draws.size() ||
        block.drawCount > block.drawMergeKeys.size() ||
        block.constantsCount > block.constants.size())
    {
        return false;
    }
    const RenderTapeEntry &entry = block.entries[block.entryCount - 1];
    if (entry.kind != RenderTapeEntryKind::Draw || entry.pass != pass ||
        entry.index != block.drawCount - 1)
    {
        return false;
    }
    const RenderTapeDraw &draw = block.draws[block.drawCount - 1];
    return draw.vertexOffset + draw.vertexCount == block.vertexCount &&
           draw.indexOffset + draw.indexCount == block.indexCount &&
           draw.topology == request.topology && draw.pipeline == request.pipeline &&
           draw.asset == request.asset &&
           (request.mergeKey != 0
                ? block.drawMergeKeys[block.drawCount - 1] == request.mergeKey
                : DrawConstantsMatch(block.constants[draw.constantsIndex], request.constants));
}

bool CanMergeLastDraw(
    const RenderTapeBlock &block, RenderTapePass pass,
    std::span<const SessionRenderTapeRecording::ReservedDrawRequest> requests) noexcept
{
    return requests.size() == 1 && CanMergeLastDraw(block, pass, requests[0]);
}

std::optional<std::size_t> Rgba8ByteCountForRgb8(std::uint32_t width, std::uint32_t height,
                                                 std::size_t sourceByteCount) noexcept
{
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixelCount > (std::numeric_limits<std::uint64_t>::max)() / 3U)
    {
        return std::nullopt;
    }
    const std::uint64_t expectedSourceBytes = pixelCount * 3U;
    if (expectedSourceBytes != sourceByteCount ||
        pixelCount > (std::numeric_limits<std::uint64_t>::max)() / 4U ||
        pixelCount * 4U > (std::numeric_limits<std::size_t>::max)())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(pixelCount * 4U);
}
} // namespace

SessionRenderTapeRecording::SessionRenderTapeRecording(std::shared_ptr<RenderTapeBlock> block,
                                                       SessionId id, SessionGeneration generation,
                                                       std::uint64_t frameSequence,
                                                       std::uint64_t surfaceGeneration,
                                                       std::uint32_t viewportWidth,
                                                       std::uint32_t viewportHeight) noexcept
    : blockOwner_(std::move(block)), block_(blockOwner_.get()), id_(id), generation_(generation),
      frameSequence_(frameSequence), surfaceGeneration_(surfaceGeneration),
      viewportWidth_(viewportWidth), viewportHeight_(viewportHeight)
{
}

std::optional<std::uint64_t> SessionRenderTapeRecording::NextStableOrder() noexcept
{
    if (nextStableOrder_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        return std::nullopt;
    }
    return nextStableOrder_++;
}

void SessionRenderTapeRecording::LatchFailure(RenderTapeFailure failure,
                                              std::uint32_t sourceInventoryRowId,
                                              RenderTapePass pass) noexcept
{
    if (Spent() || Failed())
    {
        return;
    }
    diagnostics_ =
        RenderTapeFailureDiagnostics{failure, sourceInventoryRowId, pass, nextStableOrder_};
}

bool SessionRenderTapeRecording::AppendClear(RenderTapePass pass, const RenderTapeClear &clear,
                                             std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (!IsValid(pass) || !IsValid(clear))
    {
        LatchFailure(RenderTapeFailure::NonFiniteValue, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->entryCount, 1) || !CanGrowBy(block_->clearCount, 1) ||
        !EnsureSize(block_->entries, block_->entryCount + 1) ||
        !EnsureSize(block_->clears, block_->clearCount + 1))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return false;
    }
    const std::optional<std::uint64_t> stableOrder = NextStableOrder();
    if (!stableOrder.has_value())
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }

    const std::uint32_t clearIndex = static_cast<std::uint32_t>(block_->clearCount);
    block_->clears[block_->clearCount] = clear;
    block_->entries[block_->entryCount] =
        RenderTapeEntry{*stableOrder, RenderTapeEntryKind::Clear, pass, clearIndex};
    ++block_->clearCount;
    ++block_->entryCount;
    return true;
}

bool SessionRenderTapeRecording::AppendDraw(
    RenderTapePass pass, std::span<const RenderTapeVertex> vertices,
    std::span<const std::uint32_t> indices, RenderIndexTopology topology,
    RenderPipelineKey pipeline, const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    const bool hasAsset = asset.id.value != 0 || asset.revision != 0;
    if (!IsValid(pass) || !IsValid(topology) || !IsValid(pipeline.positionSpace) ||
        !IsValid(pipeline.geometryMode) || (hasAsset && !IsValid(asset)))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (!IsValid(constants))
    {
        LatchFailure(RenderTapeFailure::NonFiniteValue, sourceInventoryRowId, pass);
        return false;
    }
    for (const RenderTapeVertex &vertex : vertices)
    {
        if (!IsValid(vertex))
        {
            LatchFailure(RenderTapeFailure::NonFiniteValue, sourceInventoryRowId, pass);
            return false;
        }
    }
    for (const std::uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }
    }
    const std::array<ReservedDrawRequest, 1> requests{
        ReservedDrawRequest{indices.size(), topology, pipeline, constants, asset}};
    const std::optional<DrawReservation> reservation =
        ReserveDraws(pass, vertices.size(), requests, sourceInventoryRowId);
    if (!reservation.has_value())
    {
        return false;
    }
    std::copy(vertices.begin(), vertices.end(), reservation->vertices.begin());
    std::copy(indices.begin(), indices.end(), reservation->indices.begin());
    return CommitDraws(*reservation, requests);
}

bool SessionRenderTapeRecording::AppendRigidInstances(
    RenderTapePass pass, const LogicalGeometryAssetLease &geometry, std::uint32_t vertexOffset,
    std::uint32_t vertexCount, std::uint32_t indexOffset, std::uint32_t indexCount,
    std::span<const RenderTapeRigidInstance> instances, const RenderTapeConstants &constants,
    LogicalRenderAssetRef asset) noexcept
{
    if (!CanRecord())
        return false;
    if (instances.empty())
        return true;
    if (!CanGrowBy(block_->rigidInstanceCount, instances.size()) ||
        !EnsureSize(block_->rigidInstances, block_->rigidInstanceCount + instances.size()))
    {
        LatchFailure(RenderTapeFailure::RigidInstanceCapacity, 0, pass);
        return false;
    }
    RenderTapeConstants drawConstants = constants;
    drawConstants.bmd.enabled = drawConstants.bmd.rigid = true;
    drawConstants.bmd.rigidInstanceOffset = static_cast<std::uint32_t>(block_->rigidInstanceCount);
    drawConstants.bmd.rigidInstanceCount = static_cast<std::uint32_t>(instances.size());
    RenderPipelineKey pipeline;
    pipeline.geometryMode = RenderGeometryMode::RigidInstances;
    if (!AppendGeometryDraw(pass, geometry, vertexOffset, vertexCount, indexOffset, indexCount,
                            RenderIndexTopology::Triangles, pipeline, drawConstants, asset))
        return false;
    std::copy(instances.begin(), instances.end(),
              block_->rigidInstances.begin() + block_->rigidInstanceCount);
    block_->rigidInstanceCount += instances.size();
    return true;
}

bool SessionRenderTapeRecording::AppendGeometryDraw(
    RenderTapePass pass, const LogicalGeometryAssetLease &geometry, std::uint32_t vertexOffset,
    std::uint32_t vertexCount, std::uint32_t indexOffset, std::uint32_t indexCount,
    RenderIndexTopology topology, RenderPipelineKey pipeline, const RenderTapeConstants &constants,
    LogicalRenderAssetRef asset, std::uint32_t sourceInventoryRowId,
    const LogicalGeometryAssetLease *auxiliaryGeometry, std::uint64_t mergeKey) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    const bool hasTexture = asset.id.value != 0 || asset.revision != 0;
    if (TryMergeGeometryDraw(*block_, pass, geometry.asset, vertexOffset, vertexCount, indexOffset,
                             indexCount, topology, pipeline, constants, asset, mergeKey))
    {
        return true;
    }

    const bool addsGeometry = !block_->ContainsGeometryAsset(geometry.asset);
    const bool addsAuxiliary = auxiliaryGeometry != nullptr &&
                               auxiliaryGeometry->asset != geometry.asset &&
                               !block_->ContainsGeometryAsset(auxiliaryGeometry->asset);
    const std::size_t addedGeometryCount =
        static_cast<std::size_t>(addsGeometry) + static_cast<std::size_t>(addsAuxiliary);
    const bool addsTexture = hasTexture && !block_->ContainsLogicalAsset(asset);
    if (!EnsureSize(block_->entries, block_->entryCount + 1) ||
        !EnsureSize(block_->draws, block_->drawCount + 1) ||
        !EnsureSize(block_->drawMergeKeys, block_->drawCount + 1) ||
        !EnsureSize(block_->constants, block_->constantsCount + 1) ||
        (addedGeometryCount != 0 &&
         !EnsureSize(block_->geometryAssets, block_->geometryAssetCount + addedGeometryCount)) ||
        (addsTexture && !EnsureSize(block_->logicalAssets, block_->logicalAssetCount + 1)))
    {
        LatchFailure(addedGeometryCount != 0 ? RenderTapeFailure::LogicalGeometryCapacity
                                             : RenderTapeFailure::EntryCapacity,
                     sourceInventoryRowId, pass);
        return false;
    }
    if (nextStableOrder_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }

    const bool insertedGeometry =
        addsGeometry && block_->InsertGeometryAsset(
                            geometry.asset, static_cast<std::uint32_t>(block_->geometryAssetCount));
    const bool insertedAuxiliary =
        addsAuxiliary &&
        block_->InsertGeometryAsset(
            auxiliaryGeometry->asset,
            static_cast<std::uint32_t>(block_->geometryAssetCount +
                                       static_cast<std::size_t>(insertedGeometry)));
    const bool insertedTexture = addsTexture && block_->InsertLogicalAsset(asset);
    if ((addsGeometry && !insertedGeometry) || (addsAuxiliary && !insertedAuxiliary) ||
        (addsTexture && !insertedTexture))
    {
        if (insertedGeometry)
        {
            block_->EraseGeometryAsset(geometry.asset);
        }
        if (insertedAuxiliary)
        {
            block_->EraseGeometryAsset(auxiliaryGeometry->asset);
        }
        if (insertedTexture)
        {
            block_->EraseLogicalAsset(asset);
        }
        LatchFailure(RenderTapeFailure::LogicalGeometryCapacity, sourceInventoryRowId, pass);
        return false;
    }

    if (insertedGeometry)
    {
        block_->geometryAssets[block_->geometryAssetCount++] = geometry;
        block_->geometryAssetBytes += GeometryAssetBytes(geometry);
    }
    if (insertedAuxiliary)
    {
        block_->geometryAssets[block_->geometryAssetCount++] = *auxiliaryGeometry;
        block_->geometryAssetBytes += GeometryAssetBytes(*auxiliaryGeometry);
    }
    if (insertedTexture)
    {
        block_->logicalAssets[block_->logicalAssetCount++] = asset;
    }
    const std::size_t drawIndex = block_->drawCount;
    const std::size_t constantsIndex = block_->constantsCount;
    block_->constants[constantsIndex] = constants;
    RenderTapeDraw &recordedDraw = block_->draws[drawIndex];
    recordedDraw.vertexOffset = vertexOffset;
    recordedDraw.vertexCount = vertexCount;
    recordedDraw.indexOffset = indexOffset;
    recordedDraw.indexCount = indexCount;
    recordedDraw.topology = topology;
    recordedDraw.pipeline = pipeline;
    recordedDraw.constantsIndex = static_cast<std::uint32_t>(constantsIndex);
    recordedDraw.asset = asset;
    recordedDraw.geometry = geometry.asset;
    block_->drawMergeKeys[drawIndex] = mergeKey;
    block_->entries[block_->entryCount] = RenderTapeEntry{
        nextStableOrder_, RenderTapeEntryKind::Draw, pass, static_cast<std::uint32_t>(drawIndex)};
    ++block_->drawCount;
    ++block_->constantsCount;
    ++block_->entryCount;
    ++nextStableOrder_;
    return true;
}

bool SessionRenderTapeRecording::AppendTrustedGeometryDrawBatch(
    RenderTapePass pass, std::span<const TrustedGeometryDraw> draws) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (draws.empty())
    {
        return true;
    }
    const std::size_t count = draws.size();
    if (!CanGrowBy(block_->entryCount, count) || !CanGrowBy(block_->drawCount, count) ||
        !CanGrowBy(block_->constantsCount, count) ||
        !CanGrowBy(block_->geometryAssetCount, count) ||
        !CanGrowBy(block_->logicalAssetCount, count) ||
        count > (std::numeric_limits<std::uint64_t>::max)() - nextStableOrder_ ||
        !EnsureSize(block_->entries, block_->entryCount + count) ||
        !EnsureSize(block_->draws, block_->drawCount + count) ||
        !EnsureSize(block_->drawMergeKeys, block_->drawCount + count) ||
        !EnsureSize(block_->constants, block_->constantsCount + count) ||
        !EnsureSize(block_->geometryAssets, block_->geometryAssetCount + count) ||
        !EnsureSize(block_->logicalAssets, block_->logicalAssetCount + count))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, 0, pass);
        return false;
    }

    const TrustedGeometryDraw *request = draws.data();
    const TrustedGeometryDraw *const end = request + draws.size();
    for (; request != end; ++request)
    {
        const LogicalGeometryAssetLease &geometry = *request->geometry;
        if (TryMergeGeometryDraw(*block_, pass, geometry.asset, request->vertexOffset,
                                 request->vertexCount, request->indexOffset, request->indexCount,
                                 request->topology, request->pipeline, request->constants,
                                 request->asset, request->mergeKey))
        {
            continue;
        }

        const bool hasTexture = request->asset.id.value != 0 || request->asset.revision != 0;
        const bool addsGeometry = !block_->ContainsGeometryAsset(geometry.asset);
        const bool addsTexture = hasTexture && !block_->ContainsLogicalAsset(request->asset);
        const bool insertedGeometry =
            addsGeometry &&
            block_->InsertGeometryAsset(geometry.asset,
                                        static_cast<std::uint32_t>(block_->geometryAssetCount));
        const bool insertedTexture = addsTexture && block_->InsertLogicalAsset(request->asset);
        if ((addsGeometry && !insertedGeometry) || (addsTexture && !insertedTexture))
        {
            if (insertedGeometry)
            {
                block_->EraseGeometryAsset(geometry.asset);
            }
            if (insertedTexture)
            {
                block_->EraseLogicalAsset(request->asset);
            }
            LatchFailure(RenderTapeFailure::LogicalGeometryCapacity, 0, pass);
            return false;
        }

        if (insertedGeometry)
        {
            block_->geometryAssets[block_->geometryAssetCount++] = geometry;
            block_->geometryAssetBytes += GeometryAssetBytes(geometry);
        }
        if (insertedTexture)
        {
            block_->logicalAssets[block_->logicalAssetCount++] = request->asset;
        }
        const std::size_t drawIndex = block_->drawCount;
        const std::size_t constantsIndex = block_->constantsCount;
        block_->constants[constantsIndex] = request->constants;
        RenderTapeDraw &recordedDraw = block_->draws[drawIndex];
        recordedDraw.vertexOffset = request->vertexOffset;
        recordedDraw.vertexCount = request->vertexCount;
        recordedDraw.indexOffset = request->indexOffset;
        recordedDraw.indexCount = request->indexCount;
        recordedDraw.topology = request->topology;
        recordedDraw.pipeline = request->pipeline;
        recordedDraw.constantsIndex = static_cast<std::uint32_t>(constantsIndex);
        recordedDraw.asset = request->asset;
        recordedDraw.geometry = geometry.asset;
        block_->drawMergeKeys[drawIndex] = request->mergeKey;
        block_->entries[block_->entryCount] =
            RenderTapeEntry{nextStableOrder_, RenderTapeEntryKind::Draw, pass,
                            static_cast<std::uint32_t>(drawIndex)};
        ++block_->drawCount;
        ++block_->constantsCount;
        ++block_->entryCount;
        ++nextStableOrder_;
    }
    return true;
}

std::optional<std::uint32_t> SessionRenderTapeRecording::AppendBoneMatrices(
    RenderTapePass pass, std::span<const RenderTapeBoneMatrix> matrices,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord() || matrices.empty())
    {
        return std::nullopt;
    }
    if (!CanGrowBy(block_->boneMatrixCount, matrices.size()) ||
        !EnsureSize(block_->boneMatrices, block_->boneMatrixCount + matrices.size()))
    {
        LatchFailure(RenderTapeFailure::BonePaletteCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(block_->boneMatrixCount);
    std::memcpy(block_->boneMatrices.data() + block_->boneMatrixCount, matrices.data(),
                matrices.size_bytes());
    block_->boneMatrixCount += matrices.size();
    return offset;
}

template <class Instance>
bool SessionRenderTapeRecording::AppendUnitQuadInstance(
    RenderTapePass pass, const LogicalGeometryAssetLease &geometry, const Instance &instance,
    std::uint64_t runId, RenderPipelineKey pipeline, const RenderTapeConstants &constants,
    LogicalRenderAssetRef asset, std::uint32_t sourceInventoryRowId,
    std::vector<Instance> &instances, std::size_t &instanceCount,
    RenderTapeFailure allocationFailure) noexcept
{
    constexpr std::uint32_t UnitQuadVertexCount = 4;
    constexpr std::uint32_t UnitQuadIndexCount = 6;
    if (block_->entryCount != 0 && block_->drawCount != 0 && block_->constantsCount != 0)
    {
        const RenderTapeEntry &entry = block_->entries[block_->entryCount - 1];
        RenderTapeDraw &draw = block_->draws[block_->drawCount - 1];
        RenderTapeConstants &drawConstants = block_->constants[draw.constantsIndex];
        const RenderTapeQuadConstants &quad = drawConstants.quad;
        if (entry.kind == RenderTapeEntryKind::Draw && entry.pass == pass &&
            entry.index == block_->drawCount - 1 && draw.geometry == geometry.asset &&
            draw.vertexOffset == 0 && draw.vertexCount == UnitQuadVertexCount &&
            draw.indexOffset == 0 && draw.indexCount == UnitQuadIndexCount &&
            draw.topology == RenderIndexTopology::Triangles && draw.pipeline == pipeline &&
            draw.asset == asset && quad.runId == runId && quad.instanceCount != 0 &&
            static_cast<std::size_t>(quad.instanceOffset) + quad.instanceCount == instanceCount)
        {
            if (!CanGrowBy(instanceCount, 1) || !EnsureSize(instances, instanceCount + 1))
            {
                LatchFailure(allocationFailure, sourceInventoryRowId, pass);
                return false;
            }
            instances[instanceCount++] = instance;
            ++drawConstants.quad.instanceCount;
            if constexpr (std::is_same_v<Instance, RenderTapeTrailInstance>)
                if (pipeline.geometryMode == RenderGeometryMode::BlurInstances)
                    drawConstants.trail.sampleCount =
                        static_cast<std::uint32_t>(block_->trailSampleCount) -
                        drawConstants.trail.sampleOffset;
            return true;
        }
    }

    if (!CanGrowBy(instanceCount, 1) || !EnsureSize(instances, instanceCount + 1))
    {
        LatchFailure(allocationFailure, sourceInventoryRowId, pass);
        return false;
    }
    RenderTapeConstants drawConstants = constants;
    if constexpr (std::is_same_v<Instance, RenderTapeTrailInstance>)
        if (pipeline.geometryMode == RenderGeometryMode::BlurInstances)
            drawConstants.trail.sampleCount = static_cast<std::uint32_t>(block_->trailSampleCount) -
                                              drawConstants.trail.sampleOffset;
    drawConstants.quad = {runId, static_cast<std::uint32_t>(instanceCount), 1};
    if (!AppendGeometryDraw(pass, geometry, 0, UnitQuadVertexCount, 0, UnitQuadIndexCount,
                            RenderIndexTopology::Triangles, pipeline, drawConstants, asset,
                            sourceInventoryRowId))
    {
        return false;
    }
    instances[instanceCount++] = instance;
    return true;
}

bool SessionRenderTapeRecording::AppendQuadInstance(RenderTapePass pass,
                                                    const LogicalGeometryAssetLease &geometry,
                                                    const RenderTapeQuadInstance &instance,
                                                    std::uint64_t runId, RenderPipelineKey pipeline,
                                                    const RenderTapeConstants &constants,
                                                    LogicalRenderAssetRef asset,
                                                    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
        return false;
    return AppendUnitQuadInstance(
        pass, geometry, instance, runId, pipeline, constants, asset, sourceInventoryRowId,
        block_->quadInstances, block_->quadInstanceCount, RenderTapeFailure::QuadInstanceCapacity);
}

bool SessionRenderTapeRecording::AppendParticleInstance(
    RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
    const RenderTapeParticleInstance &instance, std::uint64_t runId, RenderPipelineKey pipeline,
    const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
        return false;
    return AppendUnitQuadInstance(pass, geometry, instance, runId, pipeline, constants, asset,
                                  sourceInventoryRowId, block_->particleInstances,
                                  block_->particleInstanceCount,
                                  RenderTapeFailure::ParticleInstanceCapacity);
}

std::optional<SessionRenderTapeRecording::TrailSampleReservation> SessionRenderTapeRecording::
    ReserveTrailSamples(std::size_t count) noexcept
{
    if (!CanRecord())
        return std::nullopt;
    if (!CanGrowBy(block_->trailSampleCount, count) ||
        !EnsureSize(block_->trailSamples, block_->trailSampleCount + count))
    {
        LatchFailure(RenderTapeFailure::TrailCapacity, 0, RenderTapePass::Effects);
        return std::nullopt;
    }
    const auto offset = static_cast<std::uint32_t>(block_->trailSampleCount);
    block_->trailSampleCount += count;
    if (count == 0)
        return TrailSampleReservation{offset, {}};
    return TrailSampleReservation{offset, {block_->trailSamples.data() + offset, count}};
}

bool SessionRenderTapeRecording::AppendTrailInstance(
    RenderTapePass pass, const LogicalGeometryAssetLease &geometry,
    const RenderTapeTrailInstance &instance, std::uint64_t runId, RenderPipelineKey pipeline,
    const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
        return false;
    return AppendUnitQuadInstance(pass, geometry, instance, runId, pipeline, constants, asset,
                                  sourceInventoryRowId, block_->trailInstances,
                                  block_->trailInstanceCount, RenderTapeFailure::TrailCapacity);
}

bool SessionRenderTape::HasValidExternalTrailDraw(
    RenderGeometryMode mode, const RenderTapeConstants &constants) const noexcept
{
    const auto &run = constants.quad;
    const auto &trail = constants.trail;
    const auto instances = TrailInstances();
    const auto samples = TrailSamples();
    if (run.runId == 0 || run.instanceCount == 0 || run.instanceOffset > instances.size() ||
        run.instanceCount > instances.size() - run.instanceOffset || trail.faceMask < 1 ||
        trail.faceMask > 3 || (mode == RenderGeometryMode::BlurInstances && trail.faceMask != 1) ||
        !std::isfinite(trail.secondFaceUOffset) || trail.sampleOffset > samples.size() ||
        trail.sampleCount > samples.size() - trail.sampleOffset)
        return false;
    const std::uint32_t stride = trail.faceMask == 3 ? 2 : 1;
    if (trail.sampleCount < stride * 2 || trail.sampleCount % stride != 0)
        return false;
    for (const auto &sample : samples.subspan(trail.sampleOffset, trail.sampleCount))
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(sample.edge0[axis]) || !std::isfinite(sample.edge1[axis]))
                return false;
    for (const auto &instance : instances.subspan(run.instanceOffset, run.instanceCount))
    {
        if (instance.sampleIndex >= trail.sampleCount / stride - 1)
            return false;
        if (mode == RenderGeometryMode::TrailInstances && instance.parameters[2] != 0.f &&
            instance.parameters[2] != 1.f)
            return false;
        for (const auto value : instance.parameters)
            if (!std::isfinite(value))
                return false;
        for (const auto value : instance.color)
            if (!std::isfinite(value))
                return false;
        if (mode == RenderGeometryMode::BlurInstances && instance.parameters[0] <= 0.f)
            return false;
    }
    return true;
}

bool SessionRenderTapeRecording::AppendOwnerRequest(RenderTapePass pass,
                                                    const RenderOwnerRequest &request,
                                                    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (!IsValid(pass))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (std::holds_alternative<UploadLogicalAssetRgba8Request>(request))
    {
        // Uploads must use the single atomic operation below. Keeping a
        // generic request path here would re-enable payload/request splits.
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }
    if (const auto *copy = std::get_if<CopyTargetToLogicalTextureRequest>(&request))
    {
        if (copy->sourceSession != id_ || copy->sourceGeneration != generation_ ||
            copy->sourceSurfaceGeneration != surfaceGeneration_)
        {
            LatchFailure(RenderTapeFailure::InvalidIdentity, sourceInventoryRowId, pass);
            return false;
        }
        if (!IsValid(copy->destination) || !IsValid(copy->sampler) || copy->sourceRect.width == 0 ||
            copy->sourceRect.height == 0)
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }
    }
    else if (const auto *download = std::get_if<DownloadTargetRgba8Request>(&request))
    {
        if (download->sourceSession != id_ || download->sourceGeneration != generation_ ||
            download->sourceSurfaceGeneration == 0 || download->sourceFrameSequence == 0 ||
            download->sourceFrameSequence >= frameSequence_)
        {
            LatchFailure(RenderTapeFailure::InvalidIdentity, sourceInventoryRowId, pass);
            return false;
        }
        if (download->requestId == 0 || download->rect.width == 0 || download->rect.height == 0)
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }
    }
    if (!CanGrowBy(block_->entryCount, 1) || !EnsureSize(block_->entries, block_->entryCount + 1))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->ownerRequestCount, 1) ||
        !EnsureSize(block_->ownerRequests, block_->ownerRequestCount + 1))
    {
        LatchFailure(RenderTapeFailure::OwnerRequestCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (nextStableOrder_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }
    const std::uint64_t stableOrder = nextStableOrder_;

    const std::uint32_t requestIndex = static_cast<std::uint32_t>(block_->ownerRequestCount);
    block_->ownerRequests[block_->ownerRequestCount] = request;
    block_->entries[block_->entryCount] =
        RenderTapeEntry{stableOrder, RenderTapeEntryKind::OwnerRequest, pass, requestIndex};
    ++block_->ownerRequestCount;
    ++block_->entryCount;
    ++nextStableOrder_;
    return true;
}

bool SessionRenderTapeRecording::AppendUploadLogicalAssetRgba8(
    RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t width,
    std::uint32_t height, std::span<const std::byte> rgba8, RenderAssetRetention retention,
    RenderSamplerIntent sampler, std::uint32_t sourceInventoryRowId) noexcept
{
    return AppendUpload(pass, destination, width, height, rgba8, retention, sampler, false,
                        sourceInventoryRowId);
}

bool SessionRenderTapeRecording::PatchUploadedLogicalAssetRgba8(
    RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height, std::span<const std::byte> rgba8,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (!IsValid(pass) || !IsValid(destination) ||
        !IsExactRgba8ByteCount(width, height, rgba8.size()))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }

    for (std::size_t index = 0; index < block_->ownerRequestCount; ++index)
    {
        auto *const upload =
            std::get_if<UploadLogicalAssetRgba8Request>(&block_->ownerRequests[index]);
        if (upload == nullptr || upload->destination != destination)
        {
            continue;
        }
        if (upload->retention != RenderAssetRetention::FrameOnly || x > upload->width ||
            width > upload->width - x || y > upload->height || height > upload->height - y ||
            upload->payloadOffset > block_->payloadBytesUsed ||
            upload->payloadByteCount > block_->payloadBytesUsed - upload->payloadOffset)
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }

        const std::size_t sourceStride = static_cast<std::size_t>(width) * 4U;
        const std::size_t destinationStride = static_cast<std::size_t>(upload->width) * 4U;
        std::byte *const pixels = block_->payload.data() + upload->payloadOffset;
        for (std::uint32_t row = 0; row < height; ++row)
        {
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y + row) * destinationStride) +
                static_cast<std::size_t>(x) * 4U;
            std::copy_n(rgba8.data() + static_cast<std::size_t>(row) * sourceStride, sourceStride,
                        pixels + destinationOffset);
        }
        return true;
    }

    LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
    return false;
}

bool SessionRenderTapeRecording::AppendUploadAndDraw(
    RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t width,
    std::uint32_t height, std::span<const std::byte> rgba8, RenderAssetRetention retention,
    RenderSamplerIntent sampler, std::span<const RenderTapeVertex> vertices,
    std::span<const std::uint32_t> indices, RenderIndexTopology topology,
    RenderPipelineKey pipeline, const RenderTapeConstants &constants, LogicalRenderAssetRef asset,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (!IsValid(pass) || !IsValid(destination) || !IsValid(retention) || !IsValid(sampler) ||
        !IsExactRgba8ByteCount(width, height, rgba8.size()))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (block_->ContainsUploadDestination(destination))
    {
        LatchFailure(RenderTapeFailure::InvalidIdentity, sourceInventoryRowId, pass);
        return false;
    }
    if (!IsValid(topology) || !IsValid(pipeline.positionSpace) || !IsValid(pipeline.geometryMode) ||
        !IsValid(constants))
    {
        LatchFailure(RenderTapeFailure::NonFiniteValue, sourceInventoryRowId, pass);
        return false;
    }
    for (const RenderTapeVertex &vertex : vertices)
    {
        if (!IsValid(vertex))
        {
            LatchFailure(RenderTapeFailure::NonFiniteValue, sourceInventoryRowId, pass);
            return false;
        }
    }
    for (const std::uint32_t index : indices)
    {
        if (index >= vertices.size())
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }
    }
    const bool hasAsset = asset.id.value != 0 || asset.revision != 0;
    if (hasAsset && !IsValid(asset))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->entryCount, 2) || !EnsureSize(block_->entries, block_->entryCount + 2))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->ownerRequestCount, 1) ||
        !EnsureSize(block_->ownerRequests, block_->ownerRequestCount + 1))
    {
        LatchFailure(RenderTapeFailure::OwnerRequestCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->vertexCount, vertices.size()) ||
        !EnsureSize(block_->vertices, block_->vertexCount + vertices.size()))
    {
        LatchFailure(RenderTapeFailure::VertexCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->indexCount, indices.size()) ||
        !EnsureSize(block_->indices, block_->indexCount + indices.size()))
    {
        LatchFailure(RenderTapeFailure::IndexCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->constantsCount, 1) ||
        !EnsureSize(block_->constants, block_->constantsCount + 1))
    {
        LatchFailure(RenderTapeFailure::ConstantsCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->drawCount, 1) || !EnsureSize(block_->draws, block_->drawCount + 1) ||
        !EnsureSize(block_->drawMergeKeys, block_->drawCount + 1))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->payloadBytesUsed, rgba8.size()) ||
        !EnsureSize(block_->payload, block_->payloadBytesUsed + rgba8.size()))
    {
        LatchFailure(RenderTapeFailure::PayloadCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (rgba8.size() > (std::numeric_limits<std::uint32_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (nextStableOrder_ > (std::numeric_limits<std::uint64_t>::max)() - 2)
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }

    std::size_t newAssetCount = 0;
    if (hasAsset)
    {
        if (!block_->ContainsLogicalAsset(asset))
        {
            newAssetCount = 1;
        }
    }
    if (!CanGrowBy(block_->logicalAssetCount, newAssetCount) ||
        !EnsureSize(block_->logicalAssets, block_->logicalAssetCount + newAssetCount))
    {
        LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
        return false;
    }

    const std::size_t payloadOffset = block_->payloadBytesUsed;
    const std::uint32_t requestIndex = static_cast<std::uint32_t>(block_->ownerRequestCount);
    const std::uint32_t drawIndex = static_cast<std::uint32_t>(block_->drawCount);
    const std::uint32_t constantsIndex = static_cast<std::uint32_t>(block_->constantsCount);
    const std::uint64_t uploadOrder = nextStableOrder_;
    const std::uint64_t drawOrder = nextStableOrder_ + 1;
    const UploadLogicalAssetRgba8Request upload{destination,
                                                width,
                                                height,
                                                static_cast<std::uint32_t>(payloadOffset),
                                                static_cast<std::uint32_t>(rgba8.size()),
                                                retention,
                                                sampler};
    if (!block_->InsertUploadDestination(destination))
    {
        LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (newAssetCount != 0)
    {
        if (!block_->InsertLogicalAsset(asset))
        {
            block_->EraseUploadDestination(destination);
            LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
            return false;
        }
    }
    std::copy(rgba8.begin(), rgba8.end(), block_->payload.data() + payloadOffset);
    block_->ownerRequests[requestIndex] = upload;
    block_->entries[block_->entryCount] =
        RenderTapeEntry{uploadOrder, RenderTapeEntryKind::OwnerRequest, pass, requestIndex};

    std::copy(vertices.begin(), vertices.end(), block_->vertices.data() + block_->vertexCount);
    std::copy(indices.begin(), indices.end(), block_->indices.data() + block_->indexCount);
    block_->constants[constantsIndex] = constants;
    block_->draws[drawIndex] = RenderTapeDraw{static_cast<std::uint32_t>(block_->vertexCount),
                                              static_cast<std::uint32_t>(vertices.size()),
                                              static_cast<std::uint32_t>(block_->indexCount),
                                              static_cast<std::uint32_t>(indices.size()),
                                              topology,
                                              pipeline,
                                              constantsIndex,
                                              asset};
    block_->drawMergeKeys[drawIndex] = 0;
    block_->entries[block_->entryCount + 1] =
        RenderTapeEntry{drawOrder, RenderTapeEntryKind::Draw, pass, drawIndex};
    if (newAssetCount != 0)
    {
        block_->logicalAssets[block_->logicalAssetCount] = asset;
    }
    block_->payloadBytesUsed += rgba8.size();
    ++block_->ownerRequestCount;
    block_->vertexCount += vertices.size();
    block_->indexCount += indices.size();
    ++block_->constantsCount;
    ++block_->drawCount;
    block_->logicalAssetCount += newAssetCount;
    block_->entryCount += 2;
    nextStableOrder_ += 2;
    return true;
}

bool SessionRenderTapeRecording::AppendUploadLogicalAssetRgb8(
    RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t width,
    std::uint32_t height, std::span<const std::byte> rgb8, RenderAssetRetention retention,
    RenderSamplerIntent sampler, std::uint32_t sourceInventoryRowId) noexcept
{
    return AppendUpload(pass, destination, width, height, rgb8, retention, sampler, true,
                        sourceInventoryRowId);
}

bool SessionRenderTapeRecording::AppendUpload(
    RenderTapePass pass, LogicalRenderAssetRef destination, std::uint32_t width,
    std::uint32_t height, std::span<const std::byte> source, RenderAssetRetention retention,
    RenderSamplerIntent sampler, bool sourceIsRgb, std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    std::size_t outputByteCount = source.size();
    if (sourceIsRgb)
    {
        const std::optional<std::size_t> convertedByteCount =
            Rgba8ByteCountForRgb8(width, height, source.size());
        if (!convertedByteCount.has_value())
        {
            LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
            return false;
        }
        outputByteCount = *convertedByteCount;
    }
    else if (!IsExactRgba8ByteCount(width, height, source.size()))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (!IsValid(pass) || !IsValid(destination) || !IsValid(retention) || !IsValid(sampler))
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (block_->ContainsUploadDestination(destination))
    {
        LatchFailure(RenderTapeFailure::InvalidIdentity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->entryCount, 1) || !EnsureSize(block_->entries, block_->entryCount + 1))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->ownerRequestCount, 1) ||
        !EnsureSize(block_->ownerRequests, block_->ownerRequestCount + 1))
    {
        LatchFailure(RenderTapeFailure::OwnerRequestCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (!CanGrowBy(block_->payloadBytesUsed, outputByteCount) ||
        !EnsureSize(block_->payload, block_->payloadBytesUsed + outputByteCount))
    {
        LatchFailure(RenderTapeFailure::PayloadCapacity, sourceInventoryRowId, pass);
        return false;
    }
    if (outputByteCount > (std::numeric_limits<std::uint32_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return false;
    }
    if (nextStableOrder_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return false;
    }

    const std::uint64_t stableOrder = nextStableOrder_;
    const std::uint32_t requestIndex = static_cast<std::uint32_t>(block_->ownerRequestCount);
    const std::uint32_t payloadOffset = static_cast<std::uint32_t>(block_->payloadBytesUsed);
    const UploadLogicalAssetRgba8Request request{
        destination, width,  height, payloadOffset, static_cast<std::uint32_t>(outputByteCount),
        retention,   sampler};
    if (!block_->InsertUploadDestination(destination))
    {
        LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
        return false;
    }
    std::byte *const destinationBytes = block_->payload.data() + block_->payloadBytesUsed;
    if (!sourceIsRgb)
    {
        std::copy(source.begin(), source.end(), destinationBytes);
    }
    else
    {
        const std::size_t pixelCount = outputByteCount / 4U;
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::size_t sourceOffset = pixel * 3U;
            const std::size_t destinationOffset = pixel * 4U;
            destinationBytes[destinationOffset] = source[sourceOffset];
            destinationBytes[destinationOffset + 1U] = source[sourceOffset + 1U];
            destinationBytes[destinationOffset + 2U] = source[sourceOffset + 2U];
            destinationBytes[destinationOffset + 3U] = std::byte{0xFF};
        }
    }
    block_->ownerRequests[block_->ownerRequestCount] = request;
    block_->entries[block_->entryCount] =
        RenderTapeEntry{stableOrder, RenderTapeEntryKind::OwnerRequest, pass, requestIndex};
    block_->payloadBytesUsed += outputByteCount;
    ++block_->ownerRequestCount;
    ++block_->entryCount;
    ++nextStableOrder_;
    return true;
}

std::span<RenderTapeVertex> SessionRenderTapeRecording::ReserveVertices(
    std::size_t vertexCount) noexcept
{
    if (!CanRecord() || !CanGrowBy(block_->vertexCount, vertexCount) ||
        !EnsureSize(block_->vertices, block_->vertexCount + vertexCount))
    {
        return {};
    }
    return {block_->vertices.data() + block_->vertexCount, vertexCount};
}

std::optional<SessionRenderTapeRecording::DrawReservation> SessionRenderTapeRecording::ReserveDraws(
    RenderTapePass pass, std::size_t vertexCount, std::span<const ReservedDrawRequest> requests,
    std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return std::nullopt;
    }
    if (requests.empty())
    {
        LatchFailure(RenderTapeFailure::InvalidArgument, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    const bool mergesLastDraw = CanMergeLastDraw(*block_, pass, requests);
    const std::size_t addedDraws = mergesLastDraw ? 0 : requests.size();
    if (!CanGrowBy(block_->entryCount, addedDraws) || !CanGrowBy(block_->drawCount, addedDraws) ||
        !EnsureSize(block_->entries, block_->entryCount + addedDraws) ||
        !EnsureSize(block_->draws, block_->drawCount + addedDraws) ||
        !EnsureSize(block_->drawMergeKeys, block_->drawCount + addedDraws))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (!CanGrowBy(block_->vertexCount, vertexCount) ||
        !EnsureSize(block_->vertices, block_->vertexCount + vertexCount))
    {
        LatchFailure(RenderTapeFailure::VertexCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }

    std::size_t indexCount = 0;
    for (const ReservedDrawRequest &request : requests)
    {
        if (request.indexCount > (std::numeric_limits<std::size_t>::max)() - indexCount)
        {
            LatchFailure(RenderTapeFailure::IndexCapacity, sourceInventoryRowId, pass);
            return std::nullopt;
        }
        indexCount += request.indexCount;
    }
    if (!CanGrowBy(block_->indexCount, indexCount) ||
        !EnsureSize(block_->indices, block_->indexCount + indexCount))
    {
        LatchFailure(RenderTapeFailure::IndexCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (!CanGrowBy(block_->constantsCount, addedDraws) ||
        !EnsureSize(block_->constants, block_->constantsCount + addedDraws))
    {
        LatchFailure(RenderTapeFailure::ConstantsCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }

    std::size_t newAssetCount = 0;
    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
    {
        const LogicalRenderAssetRef asset = requests[requestIndex].asset;
        if (asset.id.value == 0 && asset.revision == 0)
        {
            continue;
        }
        bool alreadyTracked = false;
        alreadyTracked = block_->ContainsLogicalAsset(asset);
        for (std::size_t prior = 0; !alreadyTracked && prior < requestIndex; ++prior)
        {
            if (requests[prior].asset == asset)
            {
                alreadyTracked = true;
            }
        }
        if (!alreadyTracked)
        {
            ++newAssetCount;
        }
    }
    if (!CanGrowBy(block_->logicalAssetCount, newAssetCount) ||
        !EnsureSize(block_->logicalAssets, block_->logicalAssetCount + newAssetCount))
    {
        LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (addedDraws > (std::numeric_limits<std::uint64_t>::max)() - nextStableOrder_)
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return std::nullopt;
    }

    return DrawReservation{ReserveVertices(vertexCount),
                           {block_->indices.data() + block_->indexCount, indexCount},
                           static_cast<std::uint32_t>(block_->vertexCount),
                           static_cast<std::uint32_t>(block_->indexCount),
                           vertexCount,
                           indexCount,
                           requests.size(),
                           pass,
                           sourceInventoryRowId,
                           mergesLastDraw};
}

std::optional<SessionRenderTapeRecording::DrawReservation> SessionRenderTapeRecording::
    ReserveSingleDraw(RenderTapePass pass, std::size_t vertexCount,
                      const ReservedDrawRequest &request,
                      std::uint32_t sourceInventoryRowId) noexcept
{
    if (!CanRecord())
    {
        return std::nullopt;
    }
    const bool mergesLastDraw = CanMergeLastDraw(*block_, pass, request);
    if (!mergesLastDraw && (block_->entryCount == MaximumTapeElementCount ||
                            block_->drawCount == MaximumTapeElementCount ||
                            (block_->entryCount >= block_->entries.size() &&
                             !EnsureSize(block_->entries, block_->entryCount + 1)) ||
                            (block_->drawCount >= block_->draws.size() &&
                             !EnsureSize(block_->draws, block_->drawCount + 1)) ||
                            (block_->drawCount >= block_->drawMergeKeys.size() &&
                             !EnsureSize(block_->drawMergeKeys, block_->drawCount + 1))))
    {
        LatchFailure(RenderTapeFailure::EntryCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (vertexCount > MaximumTapeElementCount - block_->vertexCount ||
        (block_->vertexCount + vertexCount > block_->vertices.size() &&
         !EnsureSize(block_->vertices, block_->vertexCount + vertexCount)))
    {
        LatchFailure(RenderTapeFailure::VertexCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (request.indexCount > MaximumTapeElementCount - block_->indexCount ||
        (block_->indexCount + request.indexCount > block_->indices.size() &&
         !EnsureSize(block_->indices, block_->indexCount + request.indexCount)))
    {
        LatchFailure(RenderTapeFailure::IndexCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (!mergesLastDraw && (block_->constantsCount == MaximumTapeElementCount ||
                            (block_->constantsCount >= block_->constants.size() &&
                             !EnsureSize(block_->constants, block_->constantsCount + 1))))
    {
        LatchFailure(RenderTapeFailure::ConstantsCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }

    const bool hasAsset = request.asset.id.value != 0 || request.asset.revision != 0;
    const bool addsLogicalAsset = hasAsset && !block_->ContainsLogicalAsset(request.asset);
    if (addsLogicalAsset && (block_->logicalAssetCount == MaximumTapeElementCount ||
                             (block_->logicalAssetCount >= block_->logicalAssets.size() &&
                              !EnsureSize(block_->logicalAssets, block_->logicalAssetCount + 1))))
    {
        LatchFailure(RenderTapeFailure::LogicalAssetCapacity, sourceInventoryRowId, pass);
        return std::nullopt;
    }
    if (!mergesLastDraw && nextStableOrder_ == (std::numeric_limits<std::uint64_t>::max)())
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, sourceInventoryRowId, pass);
        return std::nullopt;
    }

    return DrawReservation{{block_->vertices.data() + block_->vertexCount, vertexCount},
                           {block_->indices.data() + block_->indexCount, request.indexCount},
                           static_cast<std::uint32_t>(block_->vertexCount),
                           static_cast<std::uint32_t>(block_->indexCount),
                           vertexCount,
                           request.indexCount,
                           1,
                           pass,
                           sourceInventoryRowId,
                           mergesLastDraw,
                           addsLogicalAsset};
}

bool SessionRenderTapeRecording::CommitSingleDraw(const DrawReservation &reservation,
                                                  const ReservedDrawRequest &request) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (reservation.mergesLastDraw)
    {
        RenderTapeDraw &draw = block_->draws[block_->drawCount - 1];
        std::uint32_t *index = reservation.indices.data();
        const std::uint32_t *const indexEnd = index + reservation.indices.size();
        for (; index != indexEnd; ++index)
        {
            *index += draw.vertexCount;
        }
        draw.vertexCount += static_cast<std::uint32_t>(reservation.vertexCount);
        draw.indexCount += static_cast<std::uint32_t>(reservation.indexCount);
        block_->vertexCount += reservation.vertexCount;
        block_->indexCount += reservation.indexCount;
        return true;
    }

    if (reservation.addsLogicalAsset)
    {
        if (!block_->InsertLogicalAsset(request.asset))
        {
            LatchFailure(RenderTapeFailure::LogicalAssetCapacity, reservation.sourceInventoryRowId,
                         reservation.pass);
            return false;
        }
        block_->logicalAssets[block_->logicalAssetCount] = request.asset;
        ++block_->logicalAssetCount;
    }

    const std::size_t drawIndex = block_->drawCount;
    const std::size_t constantsIndex = block_->constantsCount;
    block_->constants[constantsIndex] = request.constants;
    block_->draws[drawIndex] = RenderTapeDraw{reservation.vertexOffset,
                                              static_cast<std::uint32_t>(reservation.vertexCount),
                                              reservation.indexOffset,
                                              static_cast<std::uint32_t>(reservation.indexCount),
                                              request.topology,
                                              request.pipeline,
                                              static_cast<std::uint32_t>(constantsIndex),
                                              request.asset};
    block_->drawMergeKeys[drawIndex] = request.mergeKey;
    block_->entries[block_->entryCount] =
        RenderTapeEntry{nextStableOrder_, RenderTapeEntryKind::Draw, reservation.pass,
                        static_cast<std::uint32_t>(drawIndex)};
    block_->vertexCount += reservation.vertexCount;
    block_->indexCount += reservation.indexCount;
    ++block_->constantsCount;
    ++block_->drawCount;
    ++block_->entryCount;
    ++nextStableOrder_;
    return true;
}

bool SessionRenderTapeRecording::CommitDraws(const DrawReservation &reservation,
                                             std::span<const ReservedDrawRequest> requests) noexcept
{
    if (!CanRecord())
    {
        return false;
    }
    if (requests.size() != reservation.requestCount ||
        reservation.vertexOffset != block_->vertexCount ||
        reservation.indexOffset != block_->indexCount ||
        block_->vertexCount > block_->vertices.size() ||
        block_->indexCount > block_->indices.size() ||
        reservation.vertexCount > block_->vertices.size() - block_->vertexCount ||
        reservation.indexCount > block_->indices.size() - block_->indexCount)
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, reservation.sourceInventoryRowId,
                     reservation.pass);
        return false;
    }

    std::size_t indexCursor = 0;
    for (const ReservedDrawRequest &request : requests)
    {
        if (request.indexCount > reservation.indexCount - indexCursor)
        {
            LatchFailure(RenderTapeFailure::InvalidOperation, reservation.sourceInventoryRowId,
                         reservation.pass);
            return false;
        }
        indexCursor += request.indexCount;
    }
    if (indexCursor != reservation.indexCount)
    {
        LatchFailure(RenderTapeFailure::InvalidOperation, reservation.sourceInventoryRowId,
                     reservation.pass);
        return false;
    }

    if (reservation.mergesLastDraw)
    {
        RenderTapeDraw &draw = block_->draws[block_->drawCount - 1];
        for (std::uint32_t &index : reservation.indices)
        {
            index += draw.vertexCount;
        }
        draw.vertexCount += static_cast<std::uint32_t>(reservation.vertexCount);
        draw.indexCount += static_cast<std::uint32_t>(reservation.indexCount);
        block_->vertexCount += reservation.vertexCount;
        block_->indexCount += reservation.indexCount;
        return true;
    }

    std::size_t newAssetCount = 0;
    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
    {
        const LogicalRenderAssetRef asset = requests[requestIndex].asset;
        if (asset.id.value == 0 && asset.revision == 0)
        {
            continue;
        }
        bool alreadyTracked = false;
        alreadyTracked = block_->ContainsLogicalAsset(asset);
        for (std::size_t prior = 0; !alreadyTracked && prior < requestIndex; ++prior)
        {
            if (requests[prior].asset == asset)
            {
                alreadyTracked = true;
            }
        }
        if (!alreadyTracked)
        {
            if (!block_->InsertLogicalAsset(asset))
            {
                LatchFailure(RenderTapeFailure::LogicalAssetCapacity,
                             reservation.sourceInventoryRowId, reservation.pass);
                return false;
            }
            block_->logicalAssets[block_->logicalAssetCount + newAssetCount] = asset;
            ++newAssetCount;
        }
    }

    std::size_t committedIndexCount = 0;
    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
    {
        const ReservedDrawRequest &request = requests[requestIndex];
        const std::size_t drawIndex = block_->drawCount + requestIndex;
        const std::size_t constantsIndex = block_->constantsCount + requestIndex;
        block_->constants[constantsIndex] = request.constants;
        block_->draws[drawIndex] = RenderTapeDraw{
            reservation.vertexOffset,
            static_cast<std::uint32_t>(reservation.vertexCount),
            reservation.indexOffset + static_cast<std::uint32_t>(committedIndexCount),
            static_cast<std::uint32_t>(request.indexCount),
            request.topology,
            request.pipeline,
            static_cast<std::uint32_t>(constantsIndex),
            request.asset};
        block_->drawMergeKeys[drawIndex] = request.mergeKey;
        block_->entries[block_->entryCount + requestIndex] =
            RenderTapeEntry{nextStableOrder_ + requestIndex, RenderTapeEntryKind::Draw,
                            reservation.pass, static_cast<std::uint32_t>(drawIndex)};
        committedIndexCount += request.indexCount;
    }
    block_->logicalAssetCount += newAssetCount;
    block_->vertexCount += reservation.vertexCount;
    block_->indexCount += reservation.indexCount;
    block_->constantsCount += requests.size();
    block_->drawCount += requests.size();
    block_->entryCount += requests.size();
    nextStableOrder_ += requests.size();
    return true;
}

std::optional<SessionRenderTape> SessionRenderTapeRecording::Finalize() noexcept
{
    if (Spent())
    {
        return std::nullopt;
    }
    if (Failed())
    {
        blockOwner_.reset();
        block_ = nullptr;
        return std::nullopt;
    }
    std::shared_ptr<const RenderTapeBlock> sealed = blockOwner_;
    blockOwner_.reset();
    block_ = nullptr;
    return SessionRenderTape(std::move(sealed), id_, generation_, frameSequence_,
                             surfaceGeneration_, viewportWidth_, viewportHeight_,
                             SessionRenderTape::RecordingValidatedTag{});
}

void SessionRenderTapeRecording::Abort() noexcept
{
    blockOwner_.reset();
    block_ = nullptr;
}

//bool    showShoppingMall = false;

// opengl render util

void SessionRenderUnit::glViewport2(int x, int y, int Width, int Height)
{
    OpenglWindowX = x;
    OpenglWindowY = y;
    // NOTE: Do NOT update OpenglWindowWidth/Height here!
    // These represent the FULL window dimensions for UI rendering,
    // while Width/Height here are the game viewport (which may be smaller due to UI bars)
    // OpenglWindowWidth/Height are set once at startup and updated only on window resize
    cameraProjection_.SetViewport(x, y, Width, Height);
}

// Saved camera state for save/restore around item rendering blocks.
// Item rendering calls gluPerspective2 (corrupts PerspectiveX/Y/ScreenCenter)
// and GetOpenGLMatrix(g_Camera.Matrix) (corrupts the camera matrix). Both must
// be restored so ScreenToWorldRay reads correct values for click detection.
void SessionRenderUnit::SaveCameraPerspective()
{
    savedCameraPerspectiveState_.perspectiveX = g_Camera.PerspectiveX;
    savedCameraPerspectiveState_.perspectiveY = g_Camera.PerspectiveY;
    savedCameraPerspectiveState_.screenCenterX = g_Camera.ScreenCenterX;
    savedCameraPerspectiveState_.screenCenterY = g_Camera.ScreenCenterY;
    savedCameraPerspectiveState_.screenCenterYFlip = g_Camera.ScreenCenterYFlip;
    memcpy(savedCameraPerspectiveState_.matrix, g_Camera.Matrix, sizeof(g_Camera.Matrix));
}

void SessionRenderUnit::RestoreCameraPerspective()
{
    g_Camera.PerspectiveX = savedCameraPerspectiveState_.perspectiveX;
    g_Camera.PerspectiveY = savedCameraPerspectiveState_.perspectiveY;
    g_Camera.ScreenCenterX = savedCameraPerspectiveState_.screenCenterX;
    g_Camera.ScreenCenterY = savedCameraPerspectiveState_.screenCenterY;
    g_Camera.ScreenCenterYFlip = savedCameraPerspectiveState_.screenCenterYFlip;
    memcpy(g_Camera.Matrix, savedCameraPerspectiveState_.matrix, sizeof(g_Camera.Matrix));
}

// Perspective setup for item/3D-UI rendering. Sets GL perspective AND updates
// g_Camera perspective cache so item rendering can compute screen positions.
// Callers should wrap the entire item-rendering block in SaveCameraPerspective /
// RestoreCameraPerspective to avoid leaking FOV=1 values to ScreenToWorldRay.
void SessionRenderUnit::gluPerspective2(float Fov, float Aspect, float ZNear, float ZFar)
{
    (void)facade_.Perspective(Fov, Aspect, ZNear, ZFar);

    g_Camera.ScreenCenterX = OpenglWindowX + OpenglWindowWidth / 2;
    g_Camera.ScreenCenterY = OpenglWindowY + OpenglWindowHeight / 2;
    g_Camera.ScreenCenterYFlip = WindowHeight - g_Camera.ScreenCenterY;
    const float fovRad = Fov * 0.5F * Q_PI / 180.0F;
    g_Camera.PerspectiveX = tanf(fovRad) / static_cast<float>(OpenglWindowWidth / 2) * Aspect;
    g_Camera.PerspectiveY = tanf(fovRad) / static_cast<float>(OpenglWindowHeight / 2);
}

void SessionRenderUnit::BeginOpengl(int x, int y, int Width, int Height)
{
    const auto scaleEdge = [](int value, int extent, int reference) noexcept {
        return static_cast<int>(static_cast<std::int64_t>(value) * extent / reference);
    };
    const int right = scaleEdge(x + Width, WindowWidth, REFERENCE_WIDTH);
    const int top = scaleEdge(y + Height, WindowHeight, REFERENCE_HEIGHT);
    x = scaleEdge(x, WindowWidth, REFERENCE_WIDTH);
    y = scaleEdge(y, WindowHeight, REFERENCE_HEIGHT);
    Width = right - x;
    Height = top - y;

    (void)facade_.MatrixMode(LegacyMatrixMode::Projection);
    (void)facade_.PushMatrix();
    (void)facade_.LoadIdentity();
    cameraProjection_.SetViewport(x, y, Width, Height);
    const float aspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    cameraProjection_.SetupPerspective(g_Camera, g_Camera.FOV, aspectRatio, g_Camera.ViewNear,
                                       g_Camera.ViewFar * RENDER_DISTANCE_MULTIPLIER);
    (void)facade_.MatrixMode(LegacyMatrixMode::ModelView);
    (void)facade_.PushMatrix();
    (void)facade_.LoadIdentity();
    (void)facade_.Rotate(g_Camera.Angle[1], 0.0F, 1.0F, 0.0F);
    (void)facade_.Rotate(g_Camera.Angle[0], 1.0F, 0.0F, 0.0F);
    (void)facade_.Rotate(g_Camera.Angle[2], 0.0F, 0.0F, 1.0F);
    (void)facade_.Translate(-g_Camera.Position[0], -g_Camera.Position[1], -g_Camera.Position[2]);
    (void)facade_.SetAlphaTestEnable(false);
    (void)facade_.SetTextureEnable(true);
    (void)facade_.SetDepthTestEnable(true);
    (void)facade_.SetCullEnable(true);
    (void)facade_.SetDepthWriteEnable(true);
    (void)facade_.SetDepthFunc(RenderCompareFunction::LessOrEqual);
    (void)facade_.SetAlphaFunc(RenderCompareFunction::Greater, 0.25F);
    (void)facade_.SetFogEnable(FogEnable);
    (void)facade_.SetFogMode(RenderFogMode::Linear);
    (void)facade_.SetFogColor({FogColor[0], FogColor[1], FogColor[2], FogColor[3]});
    (void)facade_.SetFogRange(g_Camera.ViewFar, g_Camera.ViewFar * 1.25F);
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
}

void SessionRenderUnit::EndOpengl()
{
    (void)facade_.MatrixMode(LegacyMatrixMode::ModelView);
    (void)facade_.PopMatrix();
    (void)facade_.MatrixMode(LegacyMatrixMode::Projection);
    (void)facade_.PopMatrix();
}

bool SessionRenderUnit::SwitchWorldRenderTapePass(RenderTapePass pass, int x, int y, int width,
                                                  int height) noexcept
{
    EndOpengl();
    if (!BeginRenderTapePass(pass))
    {
        return false;
    }
    BeginOpengl(x, y, width, height);
    return true;
}

void SessionRenderUnit::UpdateMousePositionn()
{
    vec3_t vPos;

    (void)facade_.MatrixMode(LegacyMatrixMode::ModelView);
    (void)facade_.LoadIdentity();
    (void)facade_.Translate(-g_Camera.Position[0], -g_Camera.Position[1], -g_Camera.Position[2]);
    cameraProjection_.GetModelViewMatrix(g_Camera.Matrix);
    Vector(-g_Camera.Matrix[0][3], -g_Camera.Matrix[1][3], -g_Camera.Matrix[2][3], vPos);
    VectorIRotate(vPos, g_Camera.Matrix, MousePosition);
}

// render util

void SessionRenderUnit::RenderBox(float Matrix[3][4])
{
    vec3_t BoundingBoxMin;
    vec3_t BoundingBoxMax;
    Vector(-10.f, -30.f, -10.f, BoundingBoxMin);
    Vector(10.f, 0.f, 10.f, BoundingBoxMax);

    vec3_t BoundingVertices[8];
    Vector(BoundingBoxMax[0], BoundingBoxMax[1], BoundingBoxMax[2], BoundingVertices[0]);
    Vector(BoundingBoxMax[0], BoundingBoxMax[1], BoundingBoxMin[2], BoundingVertices[1]);
    Vector(BoundingBoxMax[0], BoundingBoxMin[1], BoundingBoxMax[2], BoundingVertices[2]);
    Vector(BoundingBoxMax[0], BoundingBoxMin[1], BoundingBoxMin[2], BoundingVertices[3]);
    Vector(BoundingBoxMin[0], BoundingBoxMax[1], BoundingBoxMax[2], BoundingVertices[4]);
    Vector(BoundingBoxMin[0], BoundingBoxMax[1], BoundingBoxMin[2], BoundingVertices[5]);
    Vector(BoundingBoxMin[0], BoundingBoxMin[1], BoundingBoxMax[2], BoundingVertices[6]);
    Vector(BoundingBoxMin[0], BoundingBoxMin[1], BoundingBoxMin[2], BoundingVertices[7]);

    vec3_t TransformVertices[8];
    for (int j = 0; j < 8; j++)
    {
        VectorTransform(BoundingVertices[j], Matrix, TransformVertices[j]);
    }

    glBegin(GL_QUADS);
    //glBegin(GL_LINES);
    glColor3f(0.2f, 0.2f, 0.2f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[6]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[4]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[5]);

    glColor3f(0.2f, 0.2f, 0.2f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[2]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[3]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[1]);

    glColor3f(0.6f, 0.6f, 0.6f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[3]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[2]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[6]);

    glColor3f(0.6f, 0.6f, 0.6f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[1]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[5]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[4]);

    glColor3f(0.4f, 0.4f, 0.4f);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[7]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[5]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[1]);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[3]);

    glColor3f(0.4f, 0.4f, 0.4f);
    glTexCoord2f(0.0F, 1.0F);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.0F, 1.0F);
    glVertex3fv(TransformVertices[4]);
    glTexCoord2f(1.0F, 0.0F);
    glVertex3fv(TransformVertices[6]);
    glTexCoord2f(0.0F, 0.0F);
    glVertex3fv(TransformVertices[2]);
    glEnd();
}

void SessionRenderUnit::RenderPlane3D(float Width, float Height, float Matrix[3][4])
{
    vec3_t BoundingVertices[4];
    Vector(-Width, -Width, Height, BoundingVertices[3]);
    Vector(Width, Width, Height, BoundingVertices[2]);
    Vector(Width, Width, -Height, BoundingVertices[1]);
    Vector(-Width, -Width, -Height, BoundingVertices[0]);

    vec3_t TransformVertices[4];
    for (int j = 0; j < 4; j++)
    {
        VectorTransform(BoundingVertices[j], Matrix, TransformVertices[j]);
    }

    glBegin(GL_QUADS);
    glTexCoord2f(0.f, 1.f);
    glVertex3fv(TransformVertices[0]);
    glTexCoord2f(1.f, 1.f);
    glVertex3fv(TransformVertices[1]);
    glTexCoord2f(1.f, 0.f);
    glVertex3fv(TransformVertices[2]);
    glTexCoord2f(0.f, 0.f);
    glVertex3fv(TransformVertices[3]);
    glEnd();
}

void SessionRenderUnit::BeginSprite()
{
    glPushMatrix();
    glLoadIdentity();
}

void SessionRenderUnit::EndSprite()
{
    glPopMatrix();
}

void SessionRenderUnit::RenderSprite(int Texture, const vec3_t Position, float Width, float Height,
                                     const vec3_t Light, float Rotation, float u, float v,
                                     float uWidth, float vHeight)
{
    const auto texture = Bitmaps.GetTextureProperties(Texture);
    (void)RenderSpriteInstance(Texture, Position, Width, Height, Light, Rotation, u, v, uWidth,
                               vHeight, texture.has_value() ? texture->components : 0, 0);
}

std::uint64_t SessionRenderUnit::RenderSpriteInstance(int Texture, const vec3_t Position,
                                                      float Width, float Height, const vec3_t Light,
                                                      float Rotation, float u, float v,
                                                      float uWidth, float vHeight,
                                                      char textureComponents, std::uint64_t runId)
{
    if (runId == 0)
    {
        BindTexture(Texture);
    }

    vec3_t center;
    VectorTransform(Position, g_Camera.Matrix, center);

    if (textureComponents == 3)
        glColor3fv(Light);
    else
    {
        if (Texture == BITMAP_BLOOD + 1 || Texture == BITMAP_FONT_HIT)
            glColor4f(Light[0], Light[1], Light[2], 1.f);
        else
            glColor4f(Light[0], Light[1], Light[2], Light[0]);
    }

    float sine = 0.0F;
    float cosine = 1.0F;
    if (Rotation != 0.0F)
    {
        constexpr float DegreesToRadians = Q_PI / 180.0F;
        const float radians = Rotation * DegreesToRadians;
        sine = sinf(radians);
        cosine = cosf(radians);
    }

    RenderTapeQuadInstance instance{};
    instance.corners[0] = {center[0], center[1], center[2], Width * 0.5F};
    instance.corners[1] = {Height * 0.5F, sine, cosine, 0.0F};
    instance.corners[2] = {u, v, uWidth, vHeight};
    instance.color = LegacyRender().CurrentColor();
    if (runId == 0)
    {
        runId = LegacyRender().BeginQuadInstanceRun();
    }
    const bool recorded = DrawSpriteInstance(instance, runId);
    glTexCoord2f(u, v);
    return recorded ? runId : 0;
}

void SessionRenderUnit::RenderSpriteUV(int Texture, vec3_t Position, float Width, float Height,
                                       float (*UV)[2], vec3_t Light[4], float Alpha)
{
    BindTexture(Texture);

    vec3_t p2;
    VectorTransform(Position, g_Camera.Matrix, p2);
    float x = p2[0];
    float y = p2[1];
    float z = p2[2];

    Width *= 0.5f;
    Height *= 0.5f;
    vec3_t p[4];
    Vector(x - Width, y - Height, z, p[0]);
    Vector(x + Width, y - Height, z, p[1]);
    Vector(x + Width, y + Height, z, p[2]);
    Vector(x - Width, y + Height, z, p[3]);

    const std::array<float, 3> &normal = LegacyRender().CurrentNormal();
    (void)LegacyRender().WriteTriangleFan(4, [&](std::span<RenderTapeVertex> vertices) noexcept {
        RenderTapeVertex *target = vertices.data();
        for (int index = 0; index < 4; ++index)
        {
            *target++ = {{p[index][0], p[index][1], p[index][2], 1.0F},
                         {UV[index][0], UV[index][1]},
                         {Light[index][0], Light[index][1], Light[index][2], Alpha},
                         normal};
        }
    });
    glColor4f(Light[3][0], Light[3][1], Light[3][2], Alpha);
    glTexCoord2f(UV[3][0], UV[3][1]);
}

void SessionRenderUnit::RenderNumber(const vec3_t Position, int Num, const vec3_t Color,
                                     float Alpha, float Scale)
{
    vec3_t p;
    VectorCopy(Position, p);
    vec3_t Light[4];
    VectorCopy(Color, Light[0]);
    VectorCopy(Color, Light[1]);
    VectorCopy(Color, Light[2]);
    VectorCopy(Color, Light[3]);
    if (Num == -1)
    {
        float UV[4][2];
        TEXCOORD(UV[0], 0.f, 32.f / 32.f);
        TEXCOORD(UV[1], 32.f / 256.f, 32.f / 32.f);
        TEXCOORD(UV[2], 32.f / 256.f, 17.f / 32.f);
        TEXCOORD(UV[3], 0.f, 17.f / 32.f);
        RenderSpriteUV(BITMAP_FONT + 1, p, 45, 20, UV, Light, Alpha);
    }
    else if (Num == -2)
    {
        RenderSprite(BITMAP_FONT_HIT, p, 32 * Scale, 20 * Scale, Light[0], 0.f, 0.f, 0.f,
                     27.f / 32.f, 15.f / 16.f);
    }
    else
    {
        wchar_t Text[32];
        _itow(Num, Text, 10);
        p[0] -= wcslen(Text) * 5.f;
        unsigned int Length = wcslen(Text);
        p[0] -= Length * Scale * 0.125f;
        p[1] -= Length * Scale * 0.125f;
        for (unsigned int i = 0; i < Length; i++)
        {
            float UV[4][2];
            float u = (float)(Text[i] - 48) * 16.f / 256.f;
            TEXCOORD(UV[0], u, 16.f / 32.f);
            TEXCOORD(UV[1], u + 16.f / 256.f, 16.f / 32.f);
            TEXCOORD(UV[2], u + 16.f / 256.f, 0.f);
            TEXCOORD(UV[3], u, 0.f);
            RenderSpriteUV(BITMAP_FONT + 1, p, Scale, Scale, UV, Light, Alpha);
            p[0] += Scale * 0.5f;
            p[1] += Scale * 0.5f;
        }
    }
}

float SessionRenderUnit::RenderNumber2D(float x, float y, int Num, float Width, float Height)
{
    wchar_t Text[32];
    _itow(Num, Text, 10);
    int Length = (int)wcslen(Text);
    x -= Width * Length / 2;
    for (int i = 0; i < Length; i++)
    {
        float u = (float)(Text[i] - 48) * 16.f / 256.f;
        //glColor3fv(Color);
        RenderBitmap(BITMAP_FONT + 1, x, y, Width, Height, u, 0.f, 16.f / 256.f, 16.f / 32.f);
        x += Width * 0.7f;
    }
    return x;
}

void SessionRenderUnit::BeginBitmap()
{
    (void)facade_.MatrixMode(LegacyMatrixMode::Projection);
    (void)facade_.PushMatrix();
    (void)facade_.LoadIdentity();
    (void)facade_.SetViewport({0, 0, WindowWidth, WindowHeight});
    (void)facade_.Ortho(0.0F, static_cast<float>(WindowWidth), 0.0F,
                        static_cast<float>(WindowHeight), -1.0F, 1.0F);
    (void)facade_.MatrixMode(LegacyMatrixMode::ModelView);
    (void)facade_.PushMatrix();
    (void)facade_.LoadIdentity();
    DisableDepthTest();
}

void SessionRenderUnit::EndBitmap()
{
    (void)facade_.MatrixMode(LegacyMatrixMode::ModelView);
    (void)facade_.PopMatrix();
    (void)facade_.MatrixMode(LegacyMatrixMode::Projection);
    (void)facade_.PopMatrix();
}

void SessionRenderUnit::RenderPointRotate(int Texture, float ix, float iy, float iWidth,
                                          float iHeight, float x, float y, float Width,
                                          float Height, float Rotate, float Rotate_Loc,
                                          float uWidth, float vHeight, int Num)
{
    const SessionDisplayView *display = sessionKeeper_.Display();
    if (display == nullptr)
    {
        LegacyRender().RejectUnsupported();
        return;
    }
    const float activeWidth = static_cast<float>(display->LocalRect().width);
    const float activeHeight = static_cast<float>(display->LocalRect().height);
    float outputX = 0.0f;
    float outputY = 0.0f;
    vec3_t point, corners[4], rotated, output[4], angle;
    float matrix[3][4];
    ix = ConvertX(ix);
    iy = ConvertY(iy);
    x = ConvertX(x);
    y = ConvertY(y);
    Width = ConvertX(Width);
    Height = ConvertY(Height);
    BindTexture(Texture);
    y = Height - y;
    iy = Height - iy;
    Vector((ix - Width * 0.5F) + (Width * 0.5F - (Width - x)),
           (iy - Height * 0.5F) + (Height * 0.5F - (Height - y)), 0.0F, point);
    Vector(0.0F, 0.0F, Rotate, angle);
    AngleMatrix(angle, matrix);
    VectorRotate(point, matrix, rotated);
    Vector(-iWidth * 0.5F, iHeight * 0.5F, 0.0F, corners[0]);
    Vector(-iWidth * 0.5F, -iHeight * 0.5F, 0.0F, corners[1]);
    Vector(iWidth * 0.5F, -iHeight * 0.5F, 0.0F, corners[2]);
    Vector(iWidth * 0.5F, iHeight * 0.5F, 0.0F, corners[3]);
    Vector(0.0F, 0.0F, Rotate_Loc, angle);
    AngleMatrix(angle, matrix);
    const float textureCoordinates[4][2]{
        {0.0F, 0.0F}, {0.0F, vHeight}, {uWidth, vHeight}, {uWidth, 0.0F}};
    glBegin(GL_TRIANGLE_FAN);
    for (int index = 0; index < 4; ++index)
    {
        glTexCoord2f(textureCoordinates[index][0], textureCoordinates[index][1]);
        matrix[0][3] = rotated[0] + 25.0F;
        matrix[1][3] = rotated[1];
        VectorTransform(corners[index], matrix, output[index]);
        glVertex2f(output[index][0] + activeWidth * 0.5F, output[index][1] + activeHeight * 0.5F);
    }
    glEnd();
    outputX =
        (output[0][0] + activeWidth * 0.5F) * static_cast<float>(REFERENCE_WIDTH) / activeWidth;
    outputY =
        (output[0][1] + activeHeight * 0.5F) * static_cast<float>(REFERENCE_HEIGHT) / activeHeight;

    if (Num < 0)
    {
        return;
    }
    if (Num >= 100)
    {
        g_pNewUIMiniMap->SetBtnPos(Num - 100, outputX - (iWidth / 2),
                                   (REFERENCE_HEIGHT - outputY) - (iHeight / 2), iWidth, iHeight);
    }
    else
    {
        g_pNewUIMiniMap->SetBtnPos(Num, outputX, REFERENCE_HEIGHT - outputY, iWidth / 2,
                                   iHeight / 2);
    }
}

// collision detect util

void SessionRenderUnit::InitCollisionDetectLineToFace()
{
    Distance = 9999999.f;
}

bool SessionRenderUnit::CollisionDetectLineToFace(vec3_t Position, vec3_t Target, int Polygon,
                                                  float *v1, float *v2, float *v3, float *v4,
                                                  vec3_t Normal, bool Collision)
{
    Core::Math::LineToFaceCollision closest;
    closest.distance = Distance;
    VectorCopy(CollisionPosition, closest.position);
    const bool hit = Core::Math::DetectLineToFace(Position, Target, Polygon, v1, v2, v3, v4, Normal,
                                                  closest, Collision);
    if (hit && Collision)
    {
        Distance = closest.distance;
        VectorCopy(closest.position, CollisionPosition);
    }
    return hit;
}
