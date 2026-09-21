#pragma once

#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationKeeper.h"
#include "domain/Automation.h"
#include "domain/WorldSimulation.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"

#include <filesystem>
#include <memory>
#include <optional>

class GameSessionTestPeer;
class ApplicationAudio;
class ApplicationConfigUnit;
class SessionManager;
class AppWindow;
class CUITextInputBox;
class CUIMercenaryInputBox;
class CUIMng;

class GameSession final
{
  public:
    static std::unique_ptr<GameSession> Create(ApplicationKeeper &applicationKeeper, SessionId id,
                                               SessionSlotId slotId,
                                               const SessionConfigValues &initialConfig);

    ~GameSession();

    SessionId Id() const noexcept;
    SessionSlotId SlotId() const noexcept;
    SessionConfigValues &Config() noexcept;
    const SessionConfigValues &Config() const noexcept;
    bool IsUsable() const noexcept;
    bool IsWorldBlocked() const noexcept
    {
        return world_.BlocksFrameAdmission();
    }
    bool AdvanceFrame() noexcept;
    std::optional<GameplayInteractionFact> CaptureAdvanceFrameInputOnOwner(
        bool prepareVisibleScene) noexcept;
    std::optional<SessionVisualAnimationInput> CaptureVisualAnimationInputOnOwner() noexcept;
    std::optional<SessionPhysicsFrameInput> CapturePhysicsFrameInputOnOwner() noexcept;
    void SetFrameAnimationFactor(float factor) noexcept;
    bool BeginAdvanceFrameWorkerSafe() noexcept;
    bool BeginAdvanceFrameWorkerSafe(double frameDeltaMilliseconds, bool renderRequired,
                                     const SessionVisualAnimationInput &visualInput,
                                     SessionVisualAnimationResult &visualResult) noexcept;
    bool PrepareMainSceneUpdate(double frameDeltaMilliseconds) noexcept;
    bool AdvanceMainSceneTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                        std::uint64_t &nextStableSequence) noexcept;
    bool AdvanceMapObserverTickWorkerSafe(SessionOrderedEffectBatch &orderedEffects,
                                          std::uint64_t &nextStableSequence) noexcept;
    bool AdvanceMainSceneEntitiesWorkerSafe(double frameDeltaMilliseconds,
                                            SessionOrderedEffectBatch &orderedEffects,
                                            std::uint64_t &nextStableSequence) noexcept;
    bool CompleteAdvanceFrameOnOwner() noexcept;
    bool CompleteAdvanceFrameOnOwner(const GameplayInteractionFact &interaction) noexcept;
    bool CompleteAdvanceFrameOnOwner(const GameplayInteractionFact &interaction,
                                     SessionOrderedEffectBatch &orderedEffects) noexcept;
    bool CompleteAdvanceFrameOnOwner(const GameplayInteractionFact &interaction,
                                     SessionOrderedEffectBatch &orderedEffects,
                                     const SessionVisualAnimationResult &visualResult) noexcept;
    bool BeginOwnerCompletionBeforeSystems(const GameplayInteractionFact &interaction,
                                           SessionOrderedEffectBatch &orderedEffects,
                                           const SessionVisualAnimationResult &visualResult,
                                           std::uint64_t &nextStableSequence) noexcept;
    bool UpdateSystemsWorkerSafe(const SessionPhysicsFrameInput &input) noexcept;
    bool EndOwnerCompletionAfterSystems(SessionOrderedEffectBatch &orderedEffects,
                                        std::uint64_t &nextStableSequence) noexcept;
    void AdvanceSkillDelay(int elapsedMs);
    bool PrepareRenderTapeOnOwner(SessionGeneration generation, std::uint64_t frameSequence,
                                  std::uint64_t surfaceGeneration, std::uint32_t viewportWidth,
                                  std::uint32_t viewportHeight,
                                  SessionRenderTapeRecording recording) noexcept;
    bool SubmitModernUiPreparation(UI::Modern::RmlUiRuntime &runtime) noexcept;
    bool RecordPreparedRenderTapeWorkerSafe() noexcept;
    std::optional<SessionRenderTape> CompletePreparedRenderTapeOnOwner(bool accept) noexcept;
    std::optional<SessionRenderTape> RecordRenderTape(SessionGeneration generation,
                                                      std::uint64_t frameSequence,
                                                      std::uint64_t surfaceGeneration,
                                                      std::uint32_t viewportWidth,
                                                      std::uint32_t viewportHeight) noexcept;
    bool CompleteRender(const SessionReplayCompletion &completion) noexcept;
    bool CompleteRenderRequest(const RenderOwnerRequestCompletion &completion,
                               std::span<const std::byte> payload) noexcept;
    const std::optional<SessionReplayCompletion> &LastCompletedTarget() const noexcept;
    bool RequiresRenderTape() noexcept;
    bool HasVisibleRenderSurface() const noexcept;
#ifdef _DEBUG
    bool CaptureRenderingProfile(const std::filesystem::path &prefix) noexcept;
#endif
    std::uint64_t RenderSurfaceGeneration() noexcept;
    void UpdateResolutionDependentSystems();
    void ToggleCameraZoomLock();
    bool ApplyGameplayExternalEvent(const GameplayExternalEvent &event) noexcept;
    void TickMuHelper();
    void BeginReconnect();
    void UpdateReconnect();
    void DeleteSocket();
    void SendNetworkPing(DWORD tickCount);
    void SendCheatDetectionLogout();
    bool UpdateSceneCompatibility();
    SessionNetworkUnit &Network() noexcept
    {
        return network_;
    }
    CHARACTER_MACHINE &CharacterMachineForBootstrap() noexcept;
    bool InitializeCharacterPopulationForBootstrap() noexcept;
    bool InitializeLegacyUi();
    bool InitializeLegacyTextInputs();
    CUIMng &LegacyUiManagerForBootstrap() noexcept;
    CUITextInputBox *FocusedTextInputBox() noexcept;
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;
    void SetMouseWheel(int wheel) noexcept;
    void ApplyPointerMove(std::int32_t x, std::int32_t y) noexcept;
    void ApplyPointerButton(std::uint32_t button, bool pressed, std::uint8_t clicks) noexcept;
    void ApplyInputKeySnapshot(const BYTE *states, bool focused) noexcept;
    void ResetTransientMouseState() noexcept;
    void ResetMouseButtons() noexcept;
    void CycleCameraMode();
    void ResetCameraView();
    GameplayExternalEventBatch ProcessPacket(const PacketInfo &packet);

  private:
    friend class GameSessionTestPeer;
    friend class SessionManager;

    GameSession(ApplicationKeeper &applicationKeeper, SessionId id, SessionSlotId slotId,
                const SessionConfigValues &initialConfig, SessionLifecycleObserver *observer);
    static std::unique_ptr<GameSession> CreateObserved(ApplicationKeeper &applicationKeeper,
                                                       SessionId id, SessionSlotId slotId,
                                                       const SessionConfigValues &initialConfig,
                                                       SessionLifecycleObserver *observer);
    bool BeginShutdown() noexcept;
    void RekeySlot(SessionSlotId slotId) noexcept;
    void ShutdownLegacyTextInputs() noexcept;
    std::uint32_t RenderTargetWidth() noexcept;
    std::uint32_t RenderTargetHeight() noexcept;

    SessionKeeper keeper_;
    SessionLifecycleObserver *observer_;
    SessionClock clock_;
    SessionRandom random_;
    SessionLifecycleState lifecycle_;
    World world_;
    SessionGameplayUnit gameplay_;
    SessionUiUnit ui_;
    SessionInteractionUnit interaction_;
    SessionPresentationUnit presentation_;
    SessionGameDataUnit gameData_;
    MUHelper::SessionMuHelperUnit muHelper_;
    SessionNetworkUnit network_;
    SessionVisualUnit visual_;
    SessionAudioLogicUnit audioLogic_;
    SessionAdvanceUnit advanceUnit_;
    SessionRenderUnit renderUnit_;
    std::unique_ptr<CUIMercenaryInputBox> mercenaryInputBox_;
    std::unique_ptr<CUITextInputBox> singleTextInputBox_;
    std::unique_ptr<CUITextInputBox> singlePasswordInputBox_;
    bool usable_ = false;
};
