#include "session/GameSession.h"
#include "app/ApplicationAudio.h"
#include "render/ModelGeometry.h"
#include "session/SessionAudio.h"
#include "session/SessionKeeper.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/runtime/UiControls.h"

std::unique_ptr<GameSession> GameSession::Create(ApplicationKeeper &applicationKeeper, SessionId id,
                                                 SessionSlotId slotId,
                                                 const SessionConfigValues &initialConfig)
{
    return CreateObserved(applicationKeeper, id, slotId, initialConfig, nullptr);
}

std::unique_ptr<GameSession> GameSession::CreateObserved(ApplicationKeeper &applicationKeeper,
                                                         SessionId id, SessionSlotId slotId,
                                                         const SessionConfigValues &initialConfig,
                                                         SessionLifecycleObserver *observer)
{
    if (!applicationKeeper.IsReady())
    {
        return nullptr;
    }

    ApplicationAudio *audio = applicationKeeper.ApplicationAudioUnit();
    if (audio == nullptr || audio->WorkspaceView().Audio(id) == nullptr)
    {
        return nullptr;
    }

    try
    {
        std::unique_ptr<GameSession> session(
            new GameSession(applicationKeeper, id, slotId, initialConfig, observer));
        if (!session->ui_.InitializeConnectedChildren() ||
            !session->network_.InitializeConnectedChildren() || !session->keeper_.CompleteLinks() ||
            !session->keeper_.InitializeBuffStateSystem())
        {
            return nullptr;
        }

        session->usable_ = true;
        if (observer != nullptr)
        {
            observer->OnSessionLifecycleEvent(SessionLifecycleEvent::Published);
        }
        return session;
    }
    catch (...)
    {
        return nullptr;
    }
}

GameSession::GameSession(ApplicationKeeper &applicationKeeper, SessionId id, SessionSlotId slotId,
                         const SessionConfigValues &initialConfig,
                         SessionLifecycleObserver *observer)
    : keeper_(applicationKeeper, id, slotId, initialConfig, observer), observer_(observer),
      clock_(keeper_, observer), random_(keeper_, id.RawValue(), observer),
      lifecycle_(keeper_, observer), world_(keeper_, observer), gameplay_(keeper_, observer),
      ui_(keeper_, observer), interaction_(keeper_, observer), presentation_(keeper_, observer),
      gameData_(keeper_), muHelper_(keeper_), network_(keeper_), visual_(keeper_, observer),
      audioLogic_(keeper_, observer), advanceUnit_(keeper_, observer),
      renderUnit_(keeper_, observer)
{
}

GameSession::~GameSession()
{
    BeginShutdown();
}

bool GameSession::BeginShutdown() noexcept
{
    if (usable_)
    {
        world_.Leave(World::LeaveReason::Shutdown);
        gameplay_.ReleaseCharacters();
    }
    usable_ = false;
    ShutdownLegacyTextInputs();
    ui_.ShutdownLegacyUi();
    if (!keeper_.BeginShutdown())
    {
        return false;
    }
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::KeeperShutdown);
    }
    return true;
}

SessionId GameSession::Id() const noexcept
{
    return keeper_.Id();
}

SessionSlotId GameSession::SlotId() const noexcept
{
    return keeper_.SlotId();
}

void GameSession::RekeySlot(SessionSlotId slotId) noexcept
{
    keeper_.RekeySlot(slotId);
}

CHARACTER_MACHINE &GameSession::CharacterMachineForBootstrap() noexcept
{
    return keeper_.CharacterMachineObject();
}

bool GameSession::InitializeCharacterPopulationForBootstrap() noexcept
{
    return keeper_.InitializeCharacterPopulation();
}

SessionConfigValues &GameSession::Config() noexcept
{
    return keeper_.Config();
}

const SessionConfigValues &GameSession::Config() const noexcept
{
    return keeper_.Config();
}

bool GameSession::IsUsable() const noexcept
{
    return usable_;
}

bool GameSession::InitializeLegacyUi()
{
    return usable_ && ui_.InitializeLegacyUi();
}

CUIMng &GameSession::LegacyUiManagerForBootstrap() noexcept
{
    return ui_.LegacyUiManager();
}

bool GameSession::InitializeLegacyTextInputs()
{
    if (mercenaryInputBox_ || singleTextInputBox_ || singlePasswordInputBox_)
    {
        return false;
    }

    std::unique_ptr<CUIMercenaryInputBox> mercenary(CreateSessionMercenaryInputBox(keeper_));
    std::unique_ptr<CUITextInputBox> single(CreateSessionTextInputBox(keeper_));
    std::unique_ptr<CUITextInputBox> password(CreateSessionTextInputBox(keeper_));
    if (!mercenary || !single || !password)
    {
        return false;
    }

    mercenary->Init();
    single->Init(200, 20);
    password->Init(200, 20, 9, TRUE);
    single->SetState(UISTATE_HIDE);
    password->SetState(UISTATE_HIDE);
    mercenary->SetFont(LegacyFontRole::Normal);
    single->SetFont(LegacyFontRole::Normal);
    password->SetFont(LegacyFontRole::Normal);

    mercenaryInputBox_ = std::move(mercenary);
    singleTextInputBox_ = std::move(single);
    singlePasswordInputBox_ = std::move(password);
    keeper_.MercenaryInputBox() = mercenaryInputBox_.get();
    keeper_.SingleTextInputBox() = singleTextInputBox_.get();
    keeper_.SinglePasswordInputBox() = singlePasswordInputBox_.get();
    return true;
}

void GameSession::ShutdownLegacyTextInputs() noexcept
{
    keeper_.FocusedTextInputBox() = nullptr;
    keeper_.MercenaryInputBox() = nullptr;
    keeper_.SingleTextInputBox() = nullptr;
    keeper_.SinglePasswordInputBox() = nullptr;
    singlePasswordInputBox_.reset();
    singleTextInputBox_.reset();
    mercenaryInputBox_.reset();
}

CUITextInputBox *GameSession::FocusedTextInputBox() noexcept
{
    return keeper_.FocusedTextInputBox();
}

bool GameSession::ProcessModernUiInput(const SessionInputEvent &event)
{
    return ui_.ProcessModernUiInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> GameSession::ModernTextInputArea() const
{
    return ui_.ModernTextInputArea();
}

void GameSession::SetMouseWheel(int wheel) noexcept
{
    keeper_.MouseWheelState() = wheel;
}

void GameSession::ApplyPointerMove(std::int32_t x, std::int32_t y) noexcept
{
    auto &input = keeper_.InterfaceStorage();
    const SessionDisplayView *display = keeper_.Display();
    const SessionDisplayRect rect =
        display != nullptr ? display->LocalRect()
                           : SessionDisplayRect{0, 0, REFERENCE_WIDTH, REFERENCE_HEIGHT};
    const std::int64_t width = rect.width == 0 ? REFERENCE_WIDTH : rect.width;
    const std::int64_t height = rect.height == 0 ? REFERENCE_HEIGHT : rect.height;
    input.MouseX =
        static_cast<int>(std::clamp<std::int64_t>(x * REFERENCE_WIDTH / width, 0, REFERENCE_WIDTH));
    input.MouseY = static_cast<int>(
        std::clamp<std::int64_t>(y * REFERENCE_HEIGHT / height, 0, REFERENCE_HEIGHT));
}

void GameSession::ApplyPointerButton(std::uint32_t button, bool pressed,
                                     std::uint8_t clicks) noexcept
{
    auto &input = keeper_.InterfaceStorage();
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        if (pressed)
        {
            input.MouseLButtonPop = false;
            if (!input.MouseLButton)
                input.MouseLButtonPush = true;
            input.MouseLButton = true;
            if (clicks >= 2)
                input.MouseLButtonDBClick = true;
        }
        else
        {
            input.MouseLButtonPush = false;
            if (input.MouseLButton)
                input.MouseLButtonPop = true;
            input.MouseLButton = false;
            input.g_iMousePopPosition_x = input.MouseX;
            input.g_iMousePopPosition_y = input.MouseY;
        }
        break;
    case SDL_BUTTON_RIGHT:
        if (pressed)
        {
            input.MouseRButtonPop = false;
            if (!input.MouseRButton)
                input.MouseRButtonPush = true;
            input.MouseRButton = true;
        }
        else
        {
            input.MouseRButtonPush = false;
            if (input.MouseRButton)
                input.MouseRButtonPop = true;
            input.MouseRButton = false;
        }
        break;
    case SDL_BUTTON_MIDDLE:
        if (pressed)
        {
            input.MouseMButtonPop = false;
            if (!input.MouseMButton)
                input.MouseMButtonPush = true;
            input.MouseMButton = true;
        }
        else
        {
            input.MouseMButtonPush = false;
            if (input.MouseMButton)
                input.MouseMButtonPop = true;
            input.MouseMButton = false;
        }
        break;
    }
}

void GameSession::ApplyInputKeySnapshot(const BYTE *states, bool focused) noexcept
{
    keeper_.ApplyInputKeySnapshot(states, focused);
}

void GameSession::ResetTransientMouseState() noexcept
{
    auto &input = keeper_.InterfaceStorage();
    input.MouseLButtonDBClick = false;
    if (input.MouseLButtonPop && (input.g_iMousePopPosition_x != input.MouseX ||
                                  input.g_iMousePopPosition_y != input.MouseY))
    {
        input.MouseLButtonPop = false;
    }
}

void GameSession::ResetMouseButtons() noexcept
{
    auto &input = keeper_.InterfaceStorage();
    input.MouseLButton = false;
    input.MouseLButtonPop = false;
    input.MouseRButton = false;
    input.MouseRButtonPop = false;
    input.MouseRButtonPush = false;
    input.MouseLButtonDBClick = false;
    input.MouseMButton = false;
    input.MouseMButtonPop = false;
    input.MouseMButtonPush = false;
}

void GameSession::CycleCameraMode()
{
    gameplay_.CameraManagerObject().CycleToNextMode(keeper_.FrameAnimationFactor());
}

void GameSession::ResetCameraView()
{
    gameplay_.CameraManagerObject().ResetActiveView();
}

bool GameSession::AdvanceFrame() noexcept
{
    return usable_ && !IsWorldBlocked() && advanceUnit_.AdvanceFrame();
}

bool GameSession::BeginAdvanceFrameWorkerSafe() noexcept
{
    return usable_ && advanceUnit_.BeginFrameWorkerSafe();
}

bool GameSession::BeginAdvanceFrameWorkerSafe(double frameDeltaMilliseconds, bool renderRequired,
                                              const SessionVisualAnimationInput &visualInput,
                                              SessionVisualAnimationResult &visualResult) noexcept
{
    return usable_ && advanceUnit_.BeginFrameWorkerSafe(frameDeltaMilliseconds, renderRequired,
                                                        visualInput, visualResult);
}

bool GameSession::PrepareMainSceneUpdate(double frameDeltaMilliseconds) noexcept
{
    return usable_ && gameplay_.PrepareMainSceneUpdate(frameDeltaMilliseconds);
}

bool GameSession::AdvanceMainSceneTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                                 std::uint64_t &nextStableSequence) noexcept
{
    return usable_ &&
           advanceUnit_.AdvanceMainSceneTickWorkerSafe(orderedEffects, nextStableSequence);
}

bool GameSession::AdvanceMapObserverTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                                   std::uint64_t &nextStableSequence) noexcept
{
    return usable_ &&
           advanceUnit_.AdvanceMapObserverTickWorkerSafe(orderedEffects, nextStableSequence);
}

bool GameSession::AdvanceMainSceneEntitiesWorkerSafe(double frameDeltaMilliseconds,
                                                     SessionOrderedEffectBatch &orderedEffects,
                                                     std::uint64_t &nextStableSequence) noexcept
{
    return usable_ && advanceUnit_.AdvanceMainSceneEntitiesWorkerSafe(
                          frameDeltaMilliseconds, orderedEffects, nextStableSequence);
}

std::optional<GameplayInteractionFact> GameSession::CaptureAdvanceFrameInputOnOwner(
    bool prepareVisibleScene) noexcept
{
    if (!usable_)
    {
        return std::nullopt;
    }
    return advanceUnit_.CaptureFrameInputOnOwner(prepareVisibleScene);
}

std::optional<SessionVisualAnimationInput> GameSession::
    CaptureVisualAnimationInputOnOwner() noexcept
{
    return usable_ ? advanceUnit_.CaptureVisualAnimationInputOnOwner() : std::nullopt;
}

std::optional<SessionPhysicsFrameInput> GameSession::CapturePhysicsFrameInputOnOwner() noexcept
{
    return usable_ ? advanceUnit_.CapturePhysicsFrameInputOnOwner() : std::nullopt;
}

void GameSession::SetFrameAnimationFactor(float factor) noexcept
{
    keeper_.FrameAnimationFactor() = factor;
}

bool GameSession::CompleteAdvanceFrameOnOwner() noexcept
{
    return usable_ && advanceUnit_.CompleteFrameOnOwner();
}

bool GameSession::CompleteAdvanceFrameOnOwner(const GameplayInteractionFact &interaction) noexcept
{
    return usable_ && advanceUnit_.CompleteFrameOnOwner(interaction);
}

bool GameSession::BeginOwnerCompletionBeforeSystems(
    const GameplayInteractionFact &interaction, SessionOrderedEffectBatch &orderedEffects,
    const SessionVisualAnimationResult &visualResult, std::uint64_t &nextStableSequence) noexcept
{
    return usable_ && advanceUnit_.BeginOwnerCompletionBeforeSystems(
                          interaction, orderedEffects, visualResult, nextStableSequence);
}

bool GameSession::UpdateSystemsWorkerSafe(const SessionPhysicsFrameInput &input) noexcept
{
    return usable_ && advanceUnit_.UpdateSystemsWorkerSafe(input);
}

bool GameSession::EndOwnerCompletionAfterSystems(SessionOrderedEffectBatch &orderedEffects,
                                                 std::uint64_t &nextStableSequence) noexcept
{
    return usable_ &&
           advanceUnit_.EndOwnerCompletionAfterSystems(orderedEffects, nextStableSequence);
}

bool GameSession::CompleteAdvanceFrameOnOwner(const GameplayInteractionFact &interaction,
                                              SessionOrderedEffectBatch &orderedEffects) noexcept
{
    return usable_ && advanceUnit_.CompleteFrameOnOwner(interaction, orderedEffects);
}

bool GameSession::CompleteAdvanceFrameOnOwner(
    const GameplayInteractionFact &interaction, SessionOrderedEffectBatch &orderedEffects,
    const SessionVisualAnimationResult &visualResult) noexcept
{
    return usable_ && advanceUnit_.CompleteFrameOnOwner(interaction, orderedEffects, visualResult);
}

std::uint32_t GameSession::RenderTargetWidth() noexcept
{
    const SessionDisplayView *const display = keeper_.Display();
    return display != nullptr ? display->LocalRect().width : 0;
}

std::uint32_t GameSession::RenderTargetHeight() noexcept
{
    const SessionDisplayView *const display = keeper_.Display();
    return display != nullptr ? display->LocalRect().height : 0;
}

void GameSession::AdvanceSkillDelay(int elapsedMs)
{
    if (usable_ && keeper_.InterfaceStorage().SceneFlag == MAIN_SCENE)
    {
        keeper_.SkillManagerObject().CalcSkillDelay(elapsedMs);
    }
}

bool GameSession::PrepareRenderTapeOnOwner(SessionGeneration generation,
                                           std::uint64_t frameSequence,
                                           std::uint64_t surfaceGeneration,
                                           std::uint32_t viewportWidth,
                                           std::uint32_t viewportHeight,
                                           SessionRenderTapeRecording recording) noexcept
{
    return usable_ &&
           renderUnit_.PrepareFrameOnOwner(generation, frameSequence, surfaceGeneration,
                                           viewportWidth, viewportHeight, std::move(recording));
}

bool GameSession::RecordPreparedRenderTapeWorkerSafe() noexcept
{
    return usable_ && renderUnit_.RecordPreparedFrameWorkerSafe();
}

bool GameSession::SubmitModernUiPreparation(UI::Modern::RmlUiRuntime &runtime) noexcept
{
    return usable_ && renderUnit_.SubmitModernUiPreparation(runtime);
}

std::optional<SessionRenderTape> GameSession::CompletePreparedRenderTapeOnOwner(
    bool accept) noexcept
{
    return renderUnit_.CompletePreparedFrameOnOwner(usable_ && accept);
}

std::optional<SessionRenderTape> GameSession::RecordRenderTape(
    SessionGeneration generation, std::uint64_t frameSequence, std::uint64_t surfaceGeneration,
    std::uint32_t viewportWidth, std::uint32_t viewportHeight) noexcept
{
    std::optional<SessionRenderTapeRecording> recording =
        keeper_.ApplicationKeeperRef().AcquireRenderTapeRecording(
            Id(), generation, surfaceGeneration, viewportWidth, viewportHeight);
    if (!recording.has_value() ||
        !PrepareRenderTapeOnOwner(generation, frameSequence, surfaceGeneration, viewportWidth,
                                  viewportHeight, std::move(*recording)))
    {
        return std::nullopt;
    }
    return CompletePreparedRenderTapeOnOwner(RecordPreparedRenderTapeWorkerSafe());
}

bool GameSession::CompleteRender(const SessionReplayCompletion &completion) noexcept
{
    return usable_ && renderUnit_.CompleteRender(completion);
}

bool GameSession::CompleteRenderRequest(const RenderOwnerRequestCompletion &completion,
                                        std::span<const std::byte> payload) noexcept
{
    return usable_ && renderUnit_.CompleteRenderRequest(completion, payload);
}

const std::optional<SessionReplayCompletion> &GameSession::LastCompletedTarget() const noexcept
{
    return renderUnit_.LastCompletedTarget();
}

bool GameSession::RequiresRenderTape() noexcept
{
    const SessionInterfaceStorage &interfaceState = keeper_.InterfaceStorage();
    return HasVisibleRenderSurface() &&
           (interfaceState.SceneFlag != CHARACTER_SCENE || interfaceState.InitCharacterScene);
}

bool GameSession::HasVisibleRenderSurface() const noexcept
{
    const SessionDisplayView *display = keeper_.Display();
    return usable_ && display != nullptr && display->IsVisible() &&
           display->LocalRect().width != 0 && display->LocalRect().height != 0;
}

std::uint64_t GameSession::RenderSurfaceGeneration() noexcept
{
    const SessionDisplayView *display = keeper_.Display();
    return display != nullptr ? display->SurfaceGeneration() : 0;
}

#ifdef _DEBUG
namespace
{
void WriteProfileVector(std::ostream &out, const vec3_t value)
{
    out << '[' << value[0] << ',' << value[1] << ',' << value[2] << ']';
}

void WriteProfileModels(std::ostream &out, SessionKeeper &keeper)
{
    struct Counts
    {
        std::size_t visible = 0, rigid = 0, rigidBytes = 0;
    };
    std::map<int, Counts> counts;
    for (const auto &block : keeper.ObjectBlocks())
        if (block.Visible)
            for (const auto *object = block.Head; object; object = object->Next)
                if (object->Live && object->Visible)
                {
                    auto &count = counts[object->Type];
                    ++count.visible;
                    if (object->RigidPose && !object->RigidPoseDirty)
                    {
                        ++count.rigid;
                        count.rigidBytes +=
                            sizeof(RigidObjectPose) +
                            object->RigidPose->bones.capacity() * sizeof(RenderTapeBoneMatrix);
                    }
                }
    out << '[';
    bool first = true;
    for (const auto &[type, count] : counts)
    {
        auto &model = keeper.ModelPoolObject()[type];
        if (!first)
            out << ',';
        first = false;
        out << "{\"type\":" << type << ",\"visible_placement_flags\":" << count.visible
            << ",\"rigid_placement_flags\":" << count.rigid
            << ",\"visible_rigid_pose_bytes\":" << count.rigidBytes
            << ",\"meshes\":" << model.NumMeshs << ",\"bones\":" << model.NumBones << '}';
    }
    out << ']';
}
} // namespace

bool GameSession::CaptureRenderingProfile(const std::filesystem::path &prefix) noexcept
{
    const auto *hero = keeper_.HeroStorage();
    const auto *map = keeper_.WorldState().definition;
    if (!hero || !map || keeper_.InterfaceStorage().SceneFlag != MAIN_SCENE)
        return false;
    try
    {
        const auto stem = prefix.wstring() + L"-session-" + std::to_wstring(Id().RawValue());
        const auto imagePath = stem + L".jpg";
        auto &state = keeper_.InterfaceStorage();
        if (imagePath.size() >= std::size(state.GrabFileName))
            return false;
        std::ofstream out(std::filesystem::path(stem + L".json"));
        if (!out)
            return false;
        const auto &camera = keeper_.CameraStateObject();
        const auto &stats = renderUnit_.renderTapeDebugStats_;
        out << "{\"session\":" << Id().RawValue() << ",\"map\":" << map->id.RawValue()
            << ",\"visible\":" << HasVisibleRenderSurface() << ",\"frame\":" << stats.frameSequence
            << ",\"target\":[" << stats.viewportWidth << ',' << stats.viewportHeight << ']'
            << ",\"hero_tile\":[" << int(hero->PositionX) << ',' << int(hero->PositionY) << ']'
            << ",\"hero_position\":";
        WriteProfileVector(out, hero->Object.Position);
        out << ",\"camera_position\":";
        WriteProfileVector(out, camera.Position);
        out << ",\"camera_angle\":";
        WriteProfileVector(out, camera.Angle);
        out << ",\"camera_distance\":" << camera.Distance << ",\"fov\":" << camera.FOV
            << ",\"near\":" << camera.ViewNear << ",\"far\":" << camera.ViewFar
            << ",\"draws\":" << stats.draws << ",\"vertices\":" << stats.vertices
            << ",\"quad_instances\":" << stats.quadInstances
            << ",\"rigid_mesh_instances\":" << stats.rigidInstances << ",\"models\":";
        WriteProfileModels(out, keeper_);
        std::size_t groupBytes = 0, preparedGroups = 0;
        for (const auto &block : keeper_.ObjectBlocks())
        {
            groupBytes += block.DrawGroups.capacity() * sizeof(WorldObjectDrawGroup) +
                          block.DrawInstances.capacity() * sizeof(RenderTapeRigidInstance);
            for (const auto &group : block.DrawGroups)
                if (group.model)
                    ++preparedGroups;
        }
        out << ",\"world_draw_storage_bytes\":" << groupBytes
            << ",\"prepared_draw_groups\":" << preparedGroups;
        out << ",\"body_equipment\":[";
        for (std::size_t index = 0; index < std::size(hero->BodyPart); ++index)
        {
            if (index)
                out << ',';
            out << '[' << hero->BodyPart[index].Type << ',' << int(hero->BodyPart[index].Level)
                << ']';
        }
        out << "],\"weapon_models\":[" << hero->Weapon[0].Type << ',' << hero->Weapon[1].Type
            << "],\"wing_model\":" << hero->Wing.Type << '}';
        if (HasVisibleRenderSurface())
        {
            std::copy(imagePath.begin(), imagePath.end(), state.GrabFileName);
            state.GrabFileName[imagePath.size()] = 0;
            ui_.CaptureScreenshot();
        }
        return bool(out);
    }
    catch (...)
    {
        return false;
    }
}
#endif

void GameSession::UpdateResolutionDependentSystems()
{
    gameplay_.UpdateResolutionDependentSystems();
    ui_.UpdateResolutionDependentSystems();
}

void GameSession::ToggleCameraZoomLock()
{
    gameplay_.CameraManagerObject().ToggleZoomLock();
}

bool GameSession::ApplyGameplayExternalEvent(const GameplayExternalEvent &event) noexcept
{
    if (!usable_)
    {
        return false;
    }
    return gameplay_.Apply(event);
}

void GameSession::TickMuHelper()
{
    if (usable_)
    {
        muHelper_.Tick();
    }
}

void GameSession::BeginReconnect()
{
    if (!usable_ || !network_.Reconnect().Begin())
    {
        return;
    }

    (void)ApplyGameplayExternalEvent({GameplayExternalEventKind::ResetInteraction});
    (void)ApplyGameplayExternalEvent({GameplayExternalEventKind::ClearFollow});
}

void GameSession::UpdateReconnect()
{
    if (usable_)
    {
        network_.Reconnect().Update();
    }
}

void GameSession::DeleteSocket()
{
    network_.DeleteSocket();
}

void GameSession::SendNetworkPing(DWORD tickCount)
{
    CHARACTER_ATTRIBUTE *const character = keeper_.CharacterAttributeStorage();
    if (character == nullptr)
        return;

    int attackSpeed = character->AttackSpeed;
    if (character->Ability & ABILITY_FAST_ATTACK_SPEED ||
        character->Ability & ABILITY_FAST_ATTACK_SPEED2)
    {
        attackSpeed -= 20;
    }
    network_.SendPing(tickCount, attackSpeed);
}

void GameSession::SendCheatDetectionLogout()
{
    network_.SendCheatDetectionLogout();
}

bool GameSession::UpdateSceneCompatibility()
{
    return usable_ && gameplay_.UpdateApplicationSceneCompatibility();
}

GameplayExternalEventBatch GameSession::ProcessPacket(const PacketInfo &packet)
{
    if (!usable_)
        return {};
    if (observer_ != nullptr)
        observer_->OnSessionPacket(Id(), packet);
    return network_.ProcessPacket(packet);
}
