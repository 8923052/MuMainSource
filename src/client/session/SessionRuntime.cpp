#include "session/SessionRuntime.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/GameData.h"
#include "session/GameSession.h"
#include "session/SessionAudio.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/CoreMath.h"
#include "ui/runtime/UiRuntime.h"

namespace
{
void DiscardUnpublishedSession(std::optional<SessionManager::PreparedSession> &prepared,
                               SessionWorkspace &workspace, SessionId id)
{
    prepared.reset(); // World leave still borrows this workspace's audio/display views.
    (void)workspace.RemoveSession(id);
}
} // namespace

ApplicationSessionRuntime::ApplicationSessionRuntime(
    ApplicationKeeper &keeper, SessionWorkspace &workspace, SessionManager &sessions,
    SessionFrameView &frame, SessionUiView &ui, SessionVisualView &visual,
    SessionAudioLogicView &audioLogic, SessionIdGenerator &ids, SessionSlotId slotId,
    SessionConfigValues initialConfig)
    : ApplicationLegacyCalls(keeper), workspace_(workspace), sessions_(sessions), frame_(frame),
      ui_(ui), visual_(visual), audioLogic_(audioLogic), ids_(ids), slotId_(slotId),
      initialConfig_(std::move(initialConfig))
{
    (void)applicationKeeper_.RegisterApplicationSessionRuntime(*this);
}

ApplicationSessionRuntime::~ApplicationSessionRuntime()
{
    Shutdown();
}

bool ApplicationSessionRuntime::CanCreateSession() const noexcept
{
    const GameData *gameData = applicationKeeper_.GameDataUnit();
    const ApplicationAudio *audio = applicationKeeper_.ApplicationAudioUnit();
    return applicationKeeper_.IsReady() && gameData != nullptr && gameData->IsLoaded() &&
           audio != nullptr && audio->IsInitialized();
}

bool ApplicationSessionRuntime::Start(SessionDisplayRect contentRect)
{
    if (primarySessionId_.has_value() || !ownedSessionIds_.empty() || !CanCreateSession())
    {
        return false;
    }

    SessionConfigValues config = initialConfig_;
    if (SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit())
    {
        config = store->TakeStartupProfile(slotId_);
    }
    const std::optional<SessionId> id = OpenSession(slotId_, config, false);
    if (!id.has_value() || !workspace_.AssignFullContent(*id, contentRect) ||
        !workspace_.Focus(*id))
    {
        if (id.has_value())
        {
            StopSession(*id);
        }
        return false;
    }
    primarySessionId_ = id;
    RefreshCanAddSession();
    return true;
}

std::size_t ApplicationSessionRuntime::StartConfiguredSessions(SessionDisplayRect windowRect)
{
    if (!ownedSessionIds_.empty() || !pendingStartupSessions_.empty() || !CanCreateSession() ||
        !workspace_.ConfigureWindow(windowRect))
    {
        return 0;
    }

    SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit();
    const std::vector<SessionSlotId> slots =
        store != nullptr ? store->ValidSlotsOrFallback() : std::vector<SessionSlotId>{slotId_};
    startupInitDelay_ = store != nullptr ? store->InitDelay() : std::chrono::seconds{};
    for (std::size_t index = 0; index < slots.size(); ++index)
    {
        const SessionSlotId slot = slots[index];
        SessionConfigValues config =
            store != nullptr ? store->TakeStartupProfile(slot) : initialConfig_;
        if (index > 0 && startupInitDelay_ > std::chrono::steady_clock::duration::zero())
        {
            pendingStartupSessions_.emplace_back(slot, std::move(config));
            continue;
        }
        const std::optional<SessionId> id = OpenSession(slot, config, false);
        if (id.has_value() && !primarySessionId_.has_value())
        {
            primarySessionId_ = id;
        }
    }
    if (!pendingStartupSessions_.empty())
    {
        nextStartupSessionTime_ = std::chrono::steady_clock::now() + startupInitDelay_;
    }
    if (primarySessionId_.has_value())
    {
        (void)workspace_.Focus(*primarySessionId_);
    }
    RefreshCanAddSession();
    return ownedSessionIds_.size();
}

void ApplicationSessionRuntime::SetSessionInitializer(SessionInitializer initializer)
{
    initializer_ = std::move(initializer);
}

void ApplicationSessionRuntime::SetSessionFinalizer(SessionFinalizer finalizer)
{
    finalizer_ = std::move(finalizer);
}

std::optional<SessionId> ApplicationSessionRuntime::OpenSession(SessionSlotId slotId,
                                                                const SessionConfigValues &config,
                                                                bool focus)
{
    if (!CanCreateSession() || sessions_.FindBySlot(slotId) != nullptr ||
        workspace_.SessionForSlot(slotId).has_value())
    {
        return std::nullopt;
    }

    try
    {
        ownedSessionIds_.reserve(ownedSessionIds_.size() + 1);
    }
    catch (...)
    {
        return std::nullopt;
    }
    const SessionId id = ids_.Next();
    if (!workspace_.AddSession(id, slotId))
    {
        return std::nullopt;
    }

    std::unique_ptr<GameSession> session =
        GameSession::Create(applicationKeeper_, id, slotId, config);
    bool initialized = session != nullptr;
    if (initialized && initializer_)
    {
        try
        {
            initialized = initializer_(*session);
        }
        catch (...)
        {
            initialized = false;
        }
    }
    std::optional<SessionManager::PreparedSession> prepared =
        initialized ? sessions_.Prepare(std::move(session)) : std::nullopt;
    if (!prepared.has_value())
    {
        session.reset();
        (void)workspace_.RemoveSession(id);
        return std::nullopt;
    }

    if (!sessions_.Publish(std::move(*prepared)))
    {
        DiscardUnpublishedSession(prepared, workspace_, id);
        return std::nullopt;
    }
    ownedSessionIds_.push_back(id);
    if (focus && !workspace_.Focus(id))
    {
        StopSession(id);
        return std::nullopt;
    }
    return id;
}

bool ApplicationSessionRuntime::AddDefaultSlot()
{
    SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit();
    if (store == nullptr || transactionActive_)
    {
        return false;
    }
    transactionActive_ = true;
    workspace_.SetTransactionActive(true);
    const auto finish = [this]() noexcept {
        transactionActive_ = false;
        workspace_.SetTransactionActive(false);
        RefreshCanAddSession();
    };

    const std::optional<SessionSlotId> slot = store->NextAppendSlot(sessions_.GreatestLiveSlot());
    if (!slot.has_value())
    {
        finish();
        return false;
    }
    try
    {
        ownedSessionIds_.reserve(ownedSessionIds_.size() + 1);
    }
    catch (...)
    {
        finish();
        return false;
    }
    const SessionId id = ids_.Next();
    if (!workspace_.AddSession(id, *slot))
    {
        finish();
        return false;
    }

    std::unique_ptr<GameSession> session =
        GameSession::Create(applicationKeeper_, id, *slot, SessionConfigValues{});
    bool initialized = session != nullptr;
    if (initialized && initializer_)
    {
        try
        {
            initialized = initializer_(*session);
        }
        catch (...)
        {
            initialized = false;
        }
    }
    std::optional<SessionManager::PreparedSession> prepared =
        initialized ? sessions_.Prepare(std::move(session)) : std::nullopt;
    if (!prepared.has_value())
    {
        session.reset();
        (void)workspace_.RemoveSession(id);
        finish();
        return false;
    }
    if (!sessions_.Publish(std::move(*prepared)))
    {
        DiscardUnpublishedSession(prepared, workspace_, id);
        finish();
        return false;
    }
    ownedSessionIds_.push_back(id);
    bool persisted = false;
    try
    {
        persisted = store->AppendDefaultSlot(*slot);
    }
    catch (...)
    {
    }
    if (!persisted)
    {
        StopSession(id);
        finish();
        return false;
    }
    (void)workspace_.Focus(id);
    if (!primarySessionId_.has_value())
    {
        primarySessionId_ = id;
    }
    finish();
    return true;
}

bool ApplicationSessionRuntime::MoveSessionUp(SessionId id)
{
    SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit();
    GameSession *selected = sessions_.Find(id);
    if (store == nullptr || selected == nullptr || !Owns(id) || transactionActive_ ||
        selected->SlotId().RawValue() <= 1)
    {
        return false;
    }
    const SessionSlotId source = selected->SlotId();
    const std::optional<SessionSlotId> destination =
        SessionSlotId::TryCreate(source.RawValue() - 1);
    if (!destination.has_value())
    {
        return false;
    }

    transactionActive_ = true;
    workspace_.SetTransactionActive(true);
    GameSession *displaced = sessions_.FindBySlot(*destination);
    if (workspace_.SlotId(id) != source ||
        (displaced != nullptr &&
         (!Owns(displaced->Id()) || workspace_.SlotId(displaced->Id()) != *destination)) ||
        (displaced == nullptr && workspace_.SessionForSlot(*destination).has_value()))
    {
        transactionActive_ = false;
        workspace_.SetTransactionActive(false);
        return false;
    }
    bool liveUpdated = false;
    bool committed = false;
    try
    {
        committed = store->MoveOrSwapSlot(
            source, *destination, [this, id, displaced, destination, &liveUpdated]() {
                if (displaced != nullptr)
                {
                    liveUpdated = sessions_.SwapSlots(id, displaced->Id()) &&
                                  workspace_.SwapSlotIds(id, displaced->Id());
                }
                else
                {
                    liveUpdated = sessions_.RekeySlot(id, *destination) &&
                                  workspace_.UpdateSlotId(id, *destination);
                }
            });
    }
    catch (...)
    {
    }
    if (!committed)
    {
        transactionActive_ = false;
        workspace_.SetTransactionActive(false);
        return false;
    }
    transactionActive_ = false;
    workspace_.SetTransactionActive(false);
    RefreshCanAddSession();
    return liveUpdated && workspace_.Focus(id);
}

bool ApplicationSessionRuntime::DiscardSession(SessionId id)
{
    SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit();
    GameSession *session = sessions_.Find(id);
    if (store == nullptr || session == nullptr || !Owns(id) || transactionActive_)
    {
        return false;
    }

    transactionActive_ = true;
    workspace_.SetTransactionActive(true);
    bool committed = false;
    try
    {
        committed = store->DeleteSlot(session->SlotId());
    }
    catch (...)
    {
    }
    if (!committed)
    {
        transactionActive_ = false;
        workspace_.SetTransactionActive(false);
        return false;
    }
    StopSession(id);
    transactionActive_ = false;
    workspace_.SetTransactionActive(false);
    RefreshCanAddSession();
    return true;
}

void ApplicationSessionRuntime::QueueDiscard(SessionId id)
{
    if (Owns(id) &&
        std::find(pendingDiscards_.begin(), pendingDiscards_.end(), id) == pendingDiscards_.end())
    {
        pendingDiscards_.push_back(id);
    }
}

void ApplicationSessionRuntime::ProcessPendingCommands()
{
    ProcessPendingStartupSessions(std::chrono::steady_clock::now());

    if (const std::optional<WorkspaceCommand> command = workspace_.TakeCommand())
    {
        switch (command->kind)
        {
        case WorkspaceCommandKind::AddDefaultSlot:
            (void)AddDefaultSlot();
            break;
        case WorkspaceCommandKind::MoveFocusedSlotUp:
            if (command->sessionId.has_value())
            {
                (void)MoveSessionUp(*command->sessionId);
            }
            break;
        case WorkspaceCommandKind::DiscardSession:
            if (command->sessionId.has_value())
            {
                (void)DiscardSession(*command->sessionId);
            }
            break;
        }
    }

    std::vector<SessionId> requests;
    requests.swap(pendingDiscards_);
    for (SessionId id : requests)
    {
        (void)DiscardSession(id);
    }
    if (workspace_.TakeLayoutChanged())
    {
        UpdateResolutionDependentSystems();
    }
}

void ApplicationSessionRuntime::ProcessPendingStartupSessions(
    std::chrono::steady_clock::time_point now)
{
    if (pendingStartupSessions_.empty() || now < nextStartupSessionTime_)
    {
        return;
    }

    auto [slot, config] = std::move(pendingStartupSessions_.front());
    pendingStartupSessions_.pop_front();
    const std::optional<SessionId> id = OpenSession(slot, config, false);
    if (id.has_value() && !primarySessionId_.has_value())
    {
        primarySessionId_ = id;
        (void)workspace_.Focus(*id);
    }
    if (!pendingStartupSessions_.empty())
    {
        nextStartupSessionTime_ = now + startupInitDelay_;
    }
    RefreshCanAddSession();
}

SessionConfigValues *ApplicationSessionRuntime::CurrentConfig() noexcept
{
    GameSession *session = PublishedSession();
    return session != nullptr ? &session->Config() : nullptr;
}

GameSession *ApplicationSessionRuntime::PublishedSession() noexcept
{
    if (const std::optional<SessionId> focused = workspace_.FocusedSession())
    {
        if (Owns(*focused))
        {
            return sessions_.Find(*focused);
        }
    }
    return primarySessionId_.has_value() ? sessions_.Find(*primarySessionId_) : nullptr;
}

bool ApplicationSessionRuntime::ApplyGameplayExternalEvent(
    const GameplayExternalEvent &event) noexcept
{
    GameSession *session = PublishedSession();
    return session != nullptr && session->ApplyGameplayExternalEvent(event);
}

bool ApplicationSessionRuntime::Owns(SessionId id) const noexcept
{
    return std::find(ownedSessionIds_.begin(), ownedSessionIds_.end(), id) !=
           ownedSessionIds_.end();
}

void ApplicationSessionRuntime::RefreshCanAddSession() noexcept
{
    SessionConfigStore *store = applicationKeeper_.SessionConfigStoreUnit();
    workspace_.SetCanAddSession(pendingStartupSessions_.empty() && store != nullptr &&
                                store->NextAppendSlot(sessions_.GreatestLiveSlot()).has_value());
}

void ApplicationSessionRuntime::StopSession(SessionId id) noexcept
{
    if (!Owns(id))
    {
        return;
    }
    if (ApplicationNetwork *network = applicationKeeper_.ApplicationNetworkUnit())
    {
        (void)network->RemoveSession(id);
    }
    if (GameSession *session = sessions_.Find(id); session != nullptr && finalizer_)
    {
        try
        {
            finalizer_(*session);
        }
        catch (...)
        {
        }
    }
    sessions_.Remove(id).reset();
    (void)workspace_.RemoveSession(id);
    ownedSessionIds_.erase(std::remove(ownedSessionIds_.begin(), ownedSessionIds_.end(), id),
                           ownedSessionIds_.end());
    pendingDiscards_.erase(std::remove(pendingDiscards_.begin(), pendingDiscards_.end(), id),
                           pendingDiscards_.end());
    if (primarySessionId_ == id)
    {
        primarySessionId_ = ownedSessionIds_.empty()
                                ? std::nullopt
                                : std::optional<SessionId>(ownedSessionIds_.front());
    }
}

void ApplicationSessionRuntime::Shutdown() noexcept
{
    const std::vector<SessionId> ids = ownedSessionIds_;
    for (SessionId id : ids)
    {
        StopSession(id);
    }
    primarySessionId_.reset();
    pendingDiscards_.clear();
    pendingStartupSessions_.clear();
    startupInitDelay_ = {};
    transactionActive_ = false;
    workspace_.SetTransactionActive(false);
}

void ApplicationSessionRuntime::StopSession() noexcept
{
    if (primarySessionId_.has_value())
    {
        StopSession(*primarySessionId_);
    }
}

void ApplicationSessionRuntime::UpdateResolutionDependentSystems()
{
    sessions_.UpdateResolutionDependentSystems();
}

SessionFrameView *ApplicationSessionRuntime::Frame() noexcept
{
    return &frame_;
}

SessionUiView *ApplicationSessionRuntime::Ui() noexcept
{
    return &ui_;
}

SessionVisualView *ApplicationSessionRuntime::Visual() noexcept
{
    return &visual_;
}

SessionAudioLogicView *ApplicationSessionRuntime::AudioLogic() noexcept
{
    return &audioLogic_;
}

bool CommitSessionOrderedEffects(std::span<SessionAdvanceResult> results,
                                 SessionOrderedEffectSink *sink) noexcept
{
    bool allSucceeded = true;
    for (SessionAdvanceResult &result : results)
    {
        if (!result.Succeeded())
        {
            continue;
        }
        if (result.orderedCommitAttempted_)
        {
            result.status_ = SessionAdvanceStatus::Failed;
            result.renderTape_.reset();
            allSucceeded = false;
            continue;
        }
        result.orderedCommitAttempted_ = true;
        const auto started = std::chrono::steady_clock::now();
        const bool committed =
            result.orderedEffects_.Records().empty() ||
            (sink != nullptr && sink->CommitBatch(result.Id(), result.Generation(),
                                                  result.FrameSequence(), result.orderedEffects_));
        result.orderedCommitWallTime_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        result.orderedCommitSucceeded_ = committed;
        if (!committed)
        {
            result.status_ = SessionAdvanceStatus::Failed;
            result.renderTape_.reset();
            allSucceeded = false;
        }
    }
    return allSucceeded;
}

SessionIdGenerator::SessionIdGenerator() noexcept : nextValue_(1)
{
}

SessionId SessionIdGenerator::Next() noexcept
{
    const auto id = SessionId::TryCreate(nextValue_);
    if (nextValue_ == std::numeric_limits<SessionId::ValueType>::max())
    {
        nextValue_ = 1;
    }
    else
    {
        ++nextValue_;
    }
    return *id;
}

#ifdef _DEBUG
extern "C"
{
    __declspec(dllexport) const SessionHotspotRenderJobSample *MuHotspotRenderJobSamples = nullptr;
    __declspec(dllexport) const int *MuHotspotRenderJobIndex = nullptr;
    extern __declspec(dllexport) const std::uint32_t MuHotspotRenderJobCapacity =
        static_cast<std::uint32_t>(SessionHotspotFrameHistorySize);
    __declspec(dllexport) const SessionHotspotFrameSample *MuHotspotSessionFrameSamples = nullptr;
    __declspec(dllexport) const int *MuHotspotSessionFrameIndex = nullptr;
    extern __declspec(dllexport) const std::uint32_t MuHotspotSessionFrameCapacity =
        static_cast<std::uint32_t>(SessionHotspotFrameHistorySize);
}
#endif

SessionManager::SessionManager(ApplicationSessionScheduler &scheduler) noexcept
    : scheduler_(scheduler)
{
#ifdef _DEBUG
    BindHotspotProbes();
#endif
}

SessionManager::SessionManager(ApplicationKeeper &keeper,
                               ApplicationSessionScheduler &scheduler) noexcept
    : scheduler_(scheduler), applicationKeeper_(&keeper),
      applicationAudio_(&keeper.ApplicationAudioObject())
{
#ifdef _DEBUG
    BindHotspotProbes();
#endif
    (void)keeper.RegisterSessionManager(*this);
}

SessionManager::~SessionManager()
{
#ifdef _DEBUG
    if (MuHotspotSessionFrameSamples == hotspotFrameSamples_.data())
    {
        MuHotspotSessionFrameSamples = nullptr;
        MuHotspotSessionFrameIndex = nullptr;
        MuHotspotRenderJobSamples = nullptr;
        MuHotspotRenderJobIndex = nullptr;
    }
#endif
    StopScheduling();
}

SessionManager::PreparedSession::PreparedSession(SessionStorage::node_type node) noexcept
    : node_(std::move(node))
{
}

SessionManager::PreparedSession::~PreparedSession() = default;

bool SessionManager::Add(std::unique_ptr<GameSession> session)
{
    if (session == nullptr || !session->IsUsable() ||
        sessions_.size() >= ApplicationFramePlan::MaximumSessions)
    {
        return false;
    }

    for (const auto &[existingId, existingSession] : sessions_)
    {
        (void)existingId;
        if (existingSession->SlotId() == session->SlotId())
        {
            return false;
        }
    }

    const SessionId id = session->Id();
    try
    {
        const bool inserted = sessions_.emplace(id, std::move(session)).second;
        if (!inserted || RegisterLane(id))
        {
            return inserted;
        }
        sessions_.erase(id);
        return false;
    }
    catch (...)
    {
        return false;
    }
}

std::optional<SessionManager::PreparedSession> SessionManager::Prepare(
    std::unique_ptr<GameSession> session)
{
    if (session == nullptr || !session->IsUsable() || Find(session->Id()) != nullptr ||
        FindBySlot(session->SlotId()) != nullptr)
    {
        return std::nullopt;
    }
    try
    {
        SessionStorage staging;
        const SessionId id = session->Id();
        staging.emplace(id, std::move(session));
        return PreparedSession(staging.extract(id));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool SessionManager::Publish(PreparedSession &&prepared) noexcept
{
    if (prepared.node_.empty())
    {
        return false;
    }
    if (sessions_.size() >= ApplicationFramePlan::MaximumSessions)
    {
        return false;
    }
    const GameSession &candidate = *prepared.node_.mapped();
    if (Find(candidate.Id()) != nullptr || FindBySlot(candidate.SlotId()) != nullptr)
    {
        return false;
    }
    const SessionId id = candidate.Id();
    const auto inserted = sessions_.insert(std::move(prepared.node_));
    if (!inserted.inserted || RegisterLane(id))
    {
        return inserted.inserted;
    }
    prepared.node_ = sessions_.extract(id);
    return false;
}

std::unique_ptr<GameSession> SessionManager::Remove(SessionId id) noexcept
{
    const auto found = sessions_.find(id);
    if (found == sessions_.end())
    {
        return nullptr;
    }

    (void)QuiesceLane(id);
    std::unique_ptr<GameSession> session = std::move(found->second);
    sessions_.erase(found);
    return session;
}

GameSession *SessionManager::Find(SessionId id) noexcept
{
    const auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second.get();
}

const GameSession *SessionManager::Find(SessionId id) const noexcept
{
    const auto found = sessions_.find(id);
    return found == sessions_.end() ? nullptr : found->second.get();
}

GameSession *SessionManager::FindBySlot(SessionSlotId slotId) noexcept
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        if (session->SlotId() == slotId)
        {
            return session.get();
        }
    }
    return nullptr;
}

const GameSession *SessionManager::FindBySlot(SessionSlotId slotId) const noexcept
{
    for (const auto &[id, session] : sessions_)
    {
        (void)id;
        if (session->SlotId() == slotId)
        {
            return session.get();
        }
    }
    return nullptr;
}

std::vector<SessionId> SessionManager::SessionIds() const
{
    std::vector<SessionId> ids;
    ids.reserve(sessions_.size());
    for (const auto &[id, session] : sessions_)
    {
        (void)session;
        ids.push_back(id);
    }
    return ids;
}

std::optional<SessionSlotId> SessionManager::GreatestLiveSlot() const noexcept
{
    std::optional<SessionSlotId> greatest;
    for (const auto &[id, session] : sessions_)
    {
        (void)id;
        if (!greatest.has_value() || *greatest < session->SlotId())
        {
            greatest = session->SlotId();
        }
    }
    return greatest;
}

bool SessionManager::RekeySlot(SessionId id, SessionSlotId slotId) noexcept
{
    GameSession *session = Find(id);
    GameSession *existing = FindBySlot(slotId);
    if (session == nullptr || (existing != nullptr && existing != session))
    {
        return false;
    }
    session->RekeySlot(slotId);
    return true;
}

bool SessionManager::SwapSlots(SessionId first, SessionId second) noexcept
{
    GameSession *firstSession = Find(first);
    GameSession *secondSession = Find(second);
    if (firstSession == nullptr || secondSession == nullptr)
    {
        return false;
    }
    const SessionSlotId firstSlot = firstSession->SlotId();
    firstSession->RekeySlot(secondSession->SlotId());
    secondSession->RekeySlot(firstSlot);
    return true;
}

std::size_t SessionManager::SessionCount() const noexcept
{
    return sessions_.size();
}

void SessionManager::PrepareMainSceneTickJobs(const ApplicationFramePlan &plan, bool observers)
{
    frameJobs_.clear();
    frameTickResults_.clear();
    for (std::size_t index = 0; index < plan.SessionCount(); ++index)
    {
        const auto &input = plan.Session(index);
        auto &context = frameWorkerContexts_[index];
        context.observerPhase = observers;
        frameJobs_.push_back(
            {input.sessionId, input.generation, frameSequence_, input.renderRequired,
             input.surfaceGeneration, &context, [](void *raw) noexcept {
                 auto &worker = *static_cast<SessionWorkerContext *>(raw);
                 if (!worker.advanceResult->Succeeded())
                     return true;
                 if (worker.observerPhase)
                     return worker.session->AdvanceMapObserverTickWorkerSafe(
                         worker.advanceResult->orderedEffects_, worker.nextStableSequence);
                 if (!worker.mainSceneUpdateRequired)
                     return true;
                 return worker.session->AdvanceMainSceneTickWorkerSafe(
                     worker.advanceResult->orderedEffects_, worker.nextStableSequence);
             }});
        frameTickResults_.emplace_back(input.sessionId, input.generation, frameSequence_);
    }
}

void SessionManager::CompleteMainSceneTickPhase() noexcept
{
    for (std::size_t index = 0; index < frameResults_.size(); ++index)
    {
        auto &result = frameResults_[index];
        const auto &tick = frameTickResults_[index];
        result.AccumulateWorkerStage(tick);
        if (!tick.Succeeded())
        {
            result.CompleteOwnerStage(false, {}, {}, std::nullopt);
            frameWorkerContexts_[index].mainSceneUpdateRequired = false;
        }
    }
}

bool SessionManager::ExecuteMainSceneTicks(const ApplicationFramePlan &plan) noexcept
{
    const bool updateSources = std::any_of(
        frameWorkerContexts_.begin(), frameWorkerContexts_.end(), [](const auto &context) {
            return context.advanceResult->Succeeded() && context.mainSceneUpdateRequired;
        });
    // All sources finish this frame before observers consume their poses.
    // Observers also consume lifecycle changes when a world is not ready to advance.
    for (const bool observers : {false, true})
    {
        if (!observers && !updateSources)
            continue;
        PrepareMainSceneTickJobs(plan, observers);
        if (!scheduler_.ExecuteFrame(plan, frameJobs_, frameTickResults_))
            return false;
        CompleteMainSceneTickPhase();
    }
    return true;
}

void SessionManager::BeginOwnerCompletionPrefixes() noexcept
{
    for (std::size_t index = 0; index < frameResults_.size(); ++index)
    {
        SessionAdvanceResult &result = frameResults_[index];
        if (!result.Succeeded())
        {
            continue;
        }
        const auto generation = generations_.find(result.Id());
        GameSession *session = Find(result.Id());
        if (session == nullptr || generation == generations_.end() ||
            generation->second != result.Generation())
        {
            result.CompleteOwnerStage(false, {}, {}, std::nullopt);
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        SessionWorkerContext &context = frameWorkerContexts_[index];
        context.ownerPrefixSucceeded = frameInputs_[index].ownerInputReady &&
                                       session->BeginOwnerCompletionBeforeSystems(
                                           frameInputs_[index].interaction, result.orderedEffects_,
                                           context.visualResult, context.nextStableSequence);
        context.ownerWallTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        if (!context.ownerPrefixSucceeded)
        {
            result.CompleteOwnerStage(false, context.ownerWallTime, {}, std::nullopt);
        }
    }
}

void SessionManager::PrepareSystemsWorkerJobs(const ApplicationFramePlan &plan)
{
    frameJobs_.clear();
    for (std::size_t index = 0; index < plan.SessionCount(); ++index)
    {
        const SessionFrameInput &input = plan.Session(index);
        SessionWorkerContext &context = frameWorkerContexts_[index];
        frameJobs_.push_back({
            input.sessionId,
            input.generation,
            frameSequence_,
            input.renderRequired,
            input.surfaceGeneration,
            &context,
            [](void *rawContext) noexcept {
                auto *worker = static_cast<SessionWorkerContext *>(rawContext);
                return worker->ownerPrefixSucceeded && worker->session != nullptr &&
                       worker->input != nullptr &&
                       worker->session->UpdateSystemsWorkerSafe(worker->input->physics);
            },
        });
        framePhysicsResults_.emplace_back(input.sessionId, input.generation, frameSequence_);
    }
}

bool SessionManager::ExecuteSystemsWorkerPhase(const ApplicationFramePlan &plan) noexcept
{
    PrepareSystemsWorkerJobs(plan);
    if (scheduler_.ExecuteFrame(plan, frameJobs_, framePhysicsResults_))
    {
        return true;
    }
    for (std::size_t index = 0; index < frameResults_.size(); ++index)
    {
        SessionAdvanceResult &result = frameResults_[index];
        if (result.Succeeded())
        {
            result.CompleteOwnerStage(false, frameWorkerContexts_[index].ownerWallTime, {},
                                      std::nullopt);
        }
    }
    return false;
}

void SessionManager::EndOwnerCompletionSuffixes() noexcept
{
    for (std::size_t index = 0; index < frameResults_.size(); ++index)
    {
        SessionAdvanceResult &result = frameResults_[index];
        SessionAdvanceResult &physicsResult = framePhysicsResults_[index];
        result.AccumulateWorkerStage(physicsResult);
        if (!result.Succeeded())
        {
            continue;
        }
        SessionWorkerContext &context = frameWorkerContexts_[index];
        const auto generation = generations_.find(result.Id());
        GameSession *session = Find(result.Id());
        if (!physicsResult.Succeeded() || session == nullptr || generation == generations_.end() ||
            generation->second != result.Generation())
        {
            result.CompleteOwnerStage(false, context.ownerWallTime, {}, std::nullopt);
            continue;
        }
        const auto started = std::chrono::steady_clock::now();
        const bool succeeded = session->EndOwnerCompletionAfterSystems(result.orderedEffects_,
                                                                       context.nextStableSequence);
        context.ownerWallTime += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
        result.CompleteOwnerStage(succeeded, context.ownerWallTime, {}, std::nullopt);
    }
}

bool SessionManager::ExecuteRenderWorkerPhase(const ApplicationFramePlan &plan,
                                              ApplicationOwnerWork ownerWork) noexcept
{
    frameRenderInputs_.clear();
    frameRenderWorkerContexts_.clear();
    frameRenderJobs_.clear();
    frameRenderResults_.clear();
    try
    {
        frameRenderInputs_.reserve(plan.SessionCount());
        frameRenderWorkerContexts_.reserve(plan.SessionCount());
        frameRenderJobs_.reserve(plan.SessionCount());
        frameRenderResults_.reserve(plan.SessionCount());
    }
    catch (...)
    {
        return false;
    }

    UI::Modern::RmlUiRuntime *const runtime =
        applicationKeeper_ == nullptr ? nullptr : applicationKeeper_->ModernUiRuntime();
    for (std::size_t index = 0; index < frameResults_.size(); ++index)
    {
        SessionAdvanceResult &result = frameResults_[index];
        const SessionFrameInput &input = frameInputs_[index];
        if (!result.Succeeded() || !input.renderRequired)
        {
            continue;
        }
        const auto generation = generations_.find(result.Id());
        GameSession *const session = Find(result.Id());
        std::optional<SessionRenderTapeRecording> recording =
            applicationKeeper_ == nullptr
                ? std::nullopt
                : applicationKeeper_->AcquireRenderTapeRecording(
                      result.Id(), result.Generation(), input.surfaceGeneration, input.targetWidth,
                      input.targetHeight);
        if (session == nullptr || generation == generations_.end() ||
            generation->second != result.Generation() || !recording.has_value() ||
            !session->PrepareRenderTapeOnOwner(result.Generation(), result.FrameSequence(),
                                               input.surfaceGeneration, input.targetWidth,
                                               input.targetHeight, std::move(*recording)))
        {
            result.CompleteRenderTapeStage(false, {}, std::nullopt);
            continue;
        }
        if (runtime == nullptr || !session->SubmitModernUiPreparation(*runtime))
        {
            (void)session->CompletePreparedRenderTapeOnOwner(false);
            result.CompleteRenderTapeStage(false, {}, std::nullopt);
            continue;
        }

        try
        {
            frameRenderInputs_.push_back(input);
            frameRenderWorkerContexts_.push_back({session, index});
            SessionRenderWorkerContext &context = frameRenderWorkerContexts_.back();
            frameRenderJobs_.push_back({
                input.sessionId,
                input.generation,
                frameSequence_,
                true,
                input.surfaceGeneration,
                &context,
                [](void *rawContext) noexcept {
                    auto *worker = static_cast<SessionRenderWorkerContext *>(rawContext);
                    return worker->session != nullptr &&
                           worker->session->RecordPreparedRenderTapeWorkerSafe();
                },
            });
            frameRenderResults_.emplace_back(input.sessionId, input.generation, frameSequence_);
        }
        catch (...)
        {
            (void)session->CompletePreparedRenderTapeOnOwner(false);
            result.CompleteRenderTapeStage(false, {}, std::nullopt);
            return false;
        }
    }

    if (frameRenderInputs_.empty())
    {
        ownerWork.Run();
        return true;
    }
    const auto renderPlan = ApplicationFramePlan::TryCreate(
        frameSequence_, plan.FrameDeltaMilliseconds(), frameRenderInputs_);
    if (!renderPlan.has_value())
    {
        return false;
    }
    const bool executed =
        scheduler_.ExecuteFrame(*renderPlan, frameRenderJobs_, frameRenderResults_, ownerWork);
    const bool modernUiCompleted = runtime != nullptr && runtime->WaitUntilIdle();
    return executed && modernUiCompleted;
}

void SessionManager::FinalizeRenderWorkerPhase() noexcept
{
    for (std::size_t index = 0; index < frameRenderResults_.size(); ++index)
    {
        SessionAdvanceResult &renderResult = frameRenderResults_[index];
#ifdef _DEBUG
        RecordHotspotRenderJob(renderResult);
#endif
        SessionRenderWorkerContext &context = frameRenderWorkerContexts_[index];
        SessionAdvanceResult &result = frameResults_[context.frameResultIndex];
        result.AccumulateWorkerStage(renderResult);
    }

    // Phase-C recording is downstream of the ordered gameplay/network
    // product. A missing transition-scene tape must not suppress effects
    // which are required to make that scene renderable on a later frame.
    (void)CommitSessionOrderedEffects(frameResults_, this);

    for (std::size_t index = 0; index < frameRenderResults_.size(); ++index)
    {
        const SessionAdvanceResult &renderResult = frameRenderResults_[index];
        SessionRenderWorkerContext &context = frameRenderWorkerContexts_[index];
        SessionAdvanceResult &result = frameResults_[context.frameResultIndex];
        if (!renderResult.Succeeded())
        {
            (void)context.session->CompletePreparedRenderTapeOnOwner(false);
            result.CompleteRenderTapeStage(false, renderResult.WorkerWallTime(), std::nullopt);
            continue;
        }
        const auto generation = generations_.find(result.Id());
        GameSession *const session = Find(result.Id());
        const bool accept = result.Succeeded() && session == context.session &&
                            generation != generations_.end() &&
                            generation->second == result.Generation();
        std::optional<SessionRenderTape> tape =
            context.session->CompletePreparedRenderTapeOnOwner(accept);
        result.CompleteRenderTapeStage(accept && tape.has_value(), renderResult.WorkerWallTime(),
                                       std::move(tape));
    }
}

#ifdef _DEBUG
void SessionManager::BindHotspotProbes() noexcept
{
    MuHotspotSessionFrameSamples = hotspotFrameSamples_.data();
    MuHotspotSessionFrameIndex = &hotspotFrameIndex_;
    const char *enabled = std::getenv("MU_PROFILE_RENDER_JOBS");
    hotspotRenderJobEnabled_ = enabled != nullptr && std::strcmp(enabled, "1") == 0;
    MuHotspotRenderJobSamples = hotspotRenderJobEnabled_ ? hotspotRenderJobs_.data() : nullptr;
    MuHotspotRenderJobIndex = hotspotRenderJobEnabled_ ? &hotspotRenderJobIndex_ : nullptr;
}

void SessionManager::RecordHotspotRenderJob(const SessionAdvanceResult &result) noexcept
{
    lastAdvancePhaseTimings_.totalRenderJobCpu += result.CpuTime();
    lastAdvancePhaseTimings_.maximumRenderJobCpu =
        (std::max)(lastAdvancePhaseTimings_.maximumRenderJobCpu, result.CpuTime());
    lastAdvancePhaseTimings_.maximumRenderJobWall =
        (std::max)(lastAdvancePhaseTimings_.maximumRenderJobWall, result.WorkerWallTime());
    ++lastAdvancePhaseTimings_.renderJobCount;
    if (!hotspotRenderJobEnabled_)
        return;
    using Milliseconds = std::chrono::duration<float, std::milli>;
    hotspotRenderJobs_[hotspotRenderJobIndex_] = {
        result.FrameSequence(), result.Id().RawValue(), result.Generation().RawValue(),
        Milliseconds(result.CpuTime()).count(), Milliseconds(result.WorkerWallTime()).count()};
    hotspotRenderJobIndex_ =
        (hotspotRenderJobIndex_ + 1) % static_cast<int>(SessionHotspotFrameHistorySize);
}

void SessionManager::RecordHotspotFrameSample() noexcept
{
    const auto milliseconds = [](std::chrono::nanoseconds value) noexcept {
        return std::chrono::duration<float, std::milli>(value).count();
    };
    hotspotFrameSamples_[hotspotFrameIndex_] = {
        frameSequence_,
        milliseconds(lastAdvancePhaseTimings_.renderWorker),
        milliseconds(lastAdvancePhaseTimings_.totalRenderJobCpu),
        milliseconds(lastAdvancePhaseTimings_.maximumRenderJobCpu),
        milliseconds(lastAdvancePhaseTimings_.maximumRenderJobWall),
        lastAdvancePhaseTimings_.renderJobCount,
    };
    hotspotFrameIndex_ =
        (hotspotFrameIndex_ + 1) % static_cast<int>(SessionHotspotFrameHistorySize);
}
#endif

std::optional<std::size_t> SessionManager::CaptureAdvanceInputsOnOwner(
    std::span<GameSession *> admitted, float frameAnimationFactor) noexcept
{
    // Validate every fact that can reject the immutable plan before invoking
    // routed UI/picking capture. Owner input is observable legacy work and may
    // not run for a frame that can already be proven unpublishable.
    std::size_t count = 0;
    for (auto &[id, session] : sessions_)
    {
        if (session->IsWorldBlocked())
            continue;
        if (generations_.find(id) == generations_.end())
        {
            return std::nullopt;
        }
        if (session->HasVisibleRenderSurface() &&
            (SurfaceGeneration(*session) == 0 || session->RenderTargetWidth() == 0 ||
             session->RenderTargetHeight() == 0))
        {
            return std::nullopt;
        }
        session->SetFrameAnimationFactor(frameAnimationFactor);
        const auto visualInput = session->CaptureVisualAnimationInputOnOwner();
        const auto physicsInput = session->CapturePhysicsFrameInputOnOwner();
        SessionVisualAnimationResult validation;
        if (!visualInput.has_value() || !physicsInput.has_value() ||
            !SessionVisualUnit::TryBuildAnimationResult(*visualInput, validation))
        {
            return std::nullopt;
        }
        frameVisualInputs_.push_back(*visualInput);
        framePhysicsInputs_.push_back(*physicsInput);
        admitted[count++] = session.get();
    }

    return count;
}

std::size_t SessionManager::AdvanceSessions() noexcept
{
    const int referenceFps = applicationKeeper_ == nullptr
                                 ? Core::Time::DefaultReferenceFps
                                 : applicationKeeper_->ApplicationConfig().legacyReferenceFps;
    return AdvanceSessions(1000.0 / referenceFps, {});
}

std::size_t SessionManager::AdvanceSessions(double frameDeltaMilliseconds,
                                            ApplicationOwnerWork ownerWork) noexcept
{
    lastAdvancePhaseTimings_ = {};
    const auto totalStarted = std::chrono::steady_clock::now();
    const std::uint64_t barrierStarted = scheduler_.Metrics().frameBarrierWaitNanoseconds;
    struct FinishTimings final
    {
        SessionAdvancePhaseTimings &timings;
        ApplicationSessionScheduler &scheduler;
        std::chrono::steady_clock::time_point started;
        std::uint64_t barrierStarted;

        ~FinishTimings()
        {
            timings.total = std::chrono::steady_clock::now() - started;
            const std::uint64_t finished = scheduler.Metrics().frameBarrierWaitNanoseconds;
            timings.workerBarrierWait = std::chrono::nanoseconds(
                finished >= barrierStarted ? finished - barrierStarted : 0);
        }
    } finishTimings{lastAdvancePhaseTimings_, scheduler_, totalStarted, barrierStarted};

    if (!scheduling_ || !std::isfinite(frameDeltaMilliseconds) || frameDeltaMilliseconds < 0.0)
    {
        return 0;
    }

    if (sessions_.size() > ApplicationFramePlan::MaximumSessions)
    {
        return 0;
    }

    frameSequence_ =
        frameSequence_ == (std::numeric_limits<std::uint64_t>::max)() ? 1 : frameSequence_ + 1;
    frameVisualInputs_.clear();
    framePhysicsInputs_.clear();
    frameInputs_.clear();
    frameWorkerContexts_.clear();
    frameJobs_.clear();
    frameResults_.clear();
    framePhysicsResults_.clear();
    frameTickResults_.clear();
    frameRenderInputs_.clear();
    frameRenderWorkerContexts_.clear();
    frameRenderJobs_.clear();
    frameRenderResults_.clear();
    try
    {
        frameVisualInputs_.reserve(sessions_.size());
        framePhysicsInputs_.reserve(sessions_.size());
        frameInputs_.reserve(sessions_.size());
        frameWorkerContexts_.reserve(sessions_.size());
        frameJobs_.reserve(sessions_.size());
        frameResults_.reserve(sessions_.size());
        framePhysicsResults_.reserve(sessions_.size());
        frameTickResults_.reserve(sessions_.size());
        frameRenderInputs_.reserve(sessions_.size());
        frameRenderWorkerContexts_.reserve(sessions_.size());
        frameRenderJobs_.reserve(sessions_.size());
        frameRenderResults_.reserve(sessions_.size());
    }
    catch (...)
    {
        return 0;
    }

    const float frameAnimationFactor = ApplicationFrameUnit::CalculateAnimationFactor(
        frameDeltaMilliseconds, applicationKeeper_ == nullptr
                                    ? Core::Time::DefaultReferenceFps
                                    : applicationKeeper_->ApplicationConfig().legacyReferenceFps);
    std::array<GameSession *, ApplicationFramePlan::MaximumSessions> admitted{};
    const auto admittedCount = CaptureAdvanceInputsOnOwner(admitted, frameAnimationFactor);
    if (!admittedCount)
        return 0;

    std::size_t inputIndex = 0;
    for (GameSession *session : std::span(admitted).first(*admittedCount))
    {
        const auto id = session->Id();
        const auto generation = generations_.find(id);
        if (generation == generations_.end())
        {
            frameResults_.clear();
            return 0;
        }
        const std::uint64_t surfaceGeneration = SurfaceGeneration(*session);
        const std::uint32_t targetWidth = session->RenderTargetWidth();
        const std::uint32_t targetHeight = session->RenderTargetHeight();
        const auto interaction =
            session->CaptureAdvanceFrameInputOnOwner(session->HasVisibleRenderSurface());
        const bool renderRequired = RequiresRenderTape(*session);
        const SessionVisualAnimationInput &visualInput = frameVisualInputs_[inputIndex++];
        const SessionPhysicsFrameInput &physicsInput = framePhysicsInputs_[inputIndex - 1];
        // Local scene preparation can fail during the owner capture itself.
        if (!interaction && session->IsWorldBlocked())
            continue;
        frameInputs_.push_back({id, generation->second, renderRequired, surfaceGeneration,
                                targetWidth, targetHeight, interaction.has_value(),
                                interaction.value_or(GameplayInteractionFact{}), visualInput,
                                physicsInput});
    }

    const auto plan =
        ApplicationFramePlan::TryCreate(frameSequence_, frameDeltaMilliseconds, frameInputs_);
    if (!plan.has_value())
    {
        return 0;
    }
    if (applicationKeeper_ != nullptr)
    {
        if (!applicationKeeper_->PrepareRenderTapeFrame(*plan))
        {
            return 0;
        }
    }
    else if (std::any_of(frameInputs_.begin(), frameInputs_.end(),
                         [](const SessionFrameInput &input) { return input.renderRequired; }))
    {
        return 0;
    }
    PrepareFrameJobs(*plan);
    lastAdvancePhaseTimings_.ownerPreparation = std::chrono::steady_clock::now() - totalStarted;
    // The clock, lifecycle gate, and logical-audio counter mutate only the
    // originating session and therefore execute on bounded scheduler lanes.
    // Production water animation is calculated from captured values there as
    // well, then applied at its original owner-stage position. The unresolved
    // legacy graph remains ordered until its external effects have immutable
    // request products.
    const auto entityWorkerStarted = std::chrono::steady_clock::now();
    const bool entityWorkersExecuted = scheduler_.ExecuteFrame(*plan, frameJobs_, frameResults_);
    lastAdvancePhaseTimings_.entityWorker = std::chrono::steady_clock::now() - entityWorkerStarted;
    if (!entityWorkersExecuted)
    {
        frameResults_.clear();
        return 0;
    }

    const auto tickStarted = std::chrono::steady_clock::now();
    if (!ExecuteMainSceneTicks(*plan))
        return 0;
    lastAdvancePhaseTimings_.entityWorker += std::chrono::steady_clock::now() - tickStarted;

    const auto ownerPrefixStarted = std::chrono::steady_clock::now();
    BeginOwnerCompletionPrefixes();
    lastAdvancePhaseTimings_.ownerCompletion =
        std::chrono::steady_clock::now() - ownerPrefixStarted;
    const auto systemsWorkerStarted = std::chrono::steady_clock::now();
    const bool systemsExecuted = ExecuteSystemsWorkerPhase(*plan);
    lastAdvancePhaseTimings_.systemsWorker =
        std::chrono::steady_clock::now() - systemsWorkerStarted;
    if (!systemsExecuted)
    {
        return 0;
    }
    const auto ownerSuffixStarted = std::chrono::steady_clock::now();
    EndOwnerCompletionSuffixes();
    lastAdvancePhaseTimings_.ownerCompletion +=
        std::chrono::steady_clock::now() - ownerSuffixStarted;

    const auto renderWorkerStarted = std::chrono::steady_clock::now();
    const bool renderWorkersExecuted = ExecuteRenderWorkerPhase(*plan, ownerWork);
    lastAdvancePhaseTimings_.renderWorker = std::chrono::steady_clock::now() - renderWorkerStarted;
    if (!renderWorkersExecuted)
    {
        for (SessionRenderWorkerContext &context : frameRenderWorkerContexts_)
        {
            if (context.session != nullptr)
            {
                (void)context.session->CompletePreparedRenderTapeOnOwner(false);
            }
        }
        return 0;
    }
    const auto renderFinalizeStarted = std::chrono::steady_clock::now();
    FinalizeRenderWorkerPhase();
    lastAdvancePhaseTimings_.ownerCompletion +=
        std::chrono::steady_clock::now() - renderFinalizeStarted;

    static bool failedResultReported = false;
    const auto failedResult =
        std::find_if(frameResults_.begin(), frameResults_.end(),
                     [](const SessionAdvanceResult &result) { return !result.Succeeded(); });
    if (failedResult != frameResults_.end() && !failedResultReported)
    {
        const std::size_t failedIndex =
            static_cast<std::size_t>(failedResult - frameResults_.begin());
        std::fprintf(
            stderr,
            "[SessionFrame] failed session=%llu frame=%llu status=%u effects=%zu rejected=%llu committed=%u owner-ready=%u prefix=%u physics=%u visual=%u destroy=%u tick=%.3f water=%.3f texture=%d\n",
            static_cast<unsigned long long>(failedResult->Id().RawValue()),
            static_cast<unsigned long long>(failedResult->FrameSequence()),
            static_cast<unsigned int>(failedResult->Status()),
            failedResult->OrderedEffects().Records().size(),
            static_cast<unsigned long long>(failedResult->OrderedEffects().RejectedCount()),
            failedResult->OrderedCommitCompleted() ? 1U : 0U,
            frameInputs_[failedIndex].ownerInputReady ? 1U : 0U,
            frameWorkerContexts_[failedIndex].ownerPrefixSucceeded ? 1U : 0U,
            static_cast<unsigned int>(framePhysicsResults_[failedIndex].Status()),
            frameWorkerContexts_[failedIndex].visualResult.succeeded ? 1U : 0U,
            frameVisualInputs_[failedIndex].destroyRequested ? 1U : 0U,
            frameVisualInputs_[failedIndex].currentTickCount,
            frameVisualInputs_[failedIndex].previousWaterChange,
            frameVisualInputs_[failedIndex].previousWaterTexture);
        failedResultReported = true;
    }
    else if (failedResult == frameResults_.end())
    {
        failedResultReported = false;
    }

    frameDeltaMilliseconds_ = frameDeltaMilliseconds;
#ifdef _DEBUG
    RecordHotspotFrameSample();
#endif
    return static_cast<std::size_t>(
        std::count_if(frameResults_.begin(), frameResults_.end(),
                      [](const SessionAdvanceResult &result) { return result.Succeeded(); }));
}

double SessionManager::SessionCpuPercent(SessionId id) const noexcept
{
    if (frameDeltaMilliseconds_ <= 0.0)
    {
        return 0.0;
    }
    const auto result =
        std::find_if(frameResults_.begin(), frameResults_.end(),
                     [id](const SessionAdvanceResult &value) { return value.Id() == id; });
    if (result == frameResults_.end())
    {
        return 0.0;
    }
    const unsigned int processorCount = (std::max)(1U, std::thread::hardware_concurrency());
    const double cpuMilliseconds =
        std::chrono::duration<double, std::milli>(result->WorkerWallTime()).count();
    return std::clamp(100.0 * cpuMilliseconds / (frameDeltaMilliseconds_ * processorCount), 0.0,
                      100.0);
}

std::size_t SessionManager::SessionOwnedStorageBytes(SessionId id) const noexcept
{
    const GameSession *const session = Find(id);
    return session == nullptr ? 0 : sizeof(*session);
}

SessionMemoryStats SessionManager::MemoryStats() noexcept
{
    SessionMemoryStats stats;
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        stats.fixedBytes += sizeof(*session);
        SessionModelPool &models = session->keeper_.ModelPoolObject();
        stats.modelPoolBytes += models.StorageBytes();
    }
    return stats;
}

bool SessionManager::CommitBatch(SessionId sessionId, SessionGeneration generation,
                                 std::uint64_t frameSequence,
                                 const SessionOrderedEffectBatch &batch) noexcept
{
    const auto liveGeneration = generations_.find(sessionId);
    GameSession *session = Find(sessionId);
    SessionAudioBusView *bus = applicationAudio_ == nullptr
                                   ? nullptr
                                   : applicationAudio_->WorkspaceView().Audio(sessionId);
    if (frameSequence == 0 || frameSequence != frameSequence_ ||
        liveGeneration == generations_.end() || liveGeneration->second != generation ||
        session == nullptr || batch.RejectedCount() != 0)
    {
        return false;
    }
    for (const SessionOrderedEffectRecord &record : batch.Records())
    {
        const std::span<const std::byte> payload = batch.Payload(record);
        switch (record.kind)
        {
        case SessionOrderedEffectKind::ManagedConnectionSend:
            if (payload.empty() ||
                !session->Network().CanCommitCapturedSend(sessionId, record.channel))
            {
                return false;
            }
            break;
        case SessionOrderedEffectKind::HardwareAudio:
            if (applicationAudio_ == nullptr || bus == nullptr || bus->Id() != sessionId ||
                record.channel != SessionSpatialAudioEffectChannel ||
                payload.size() != sizeof(SessionSpatialAudioRequest))
            {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    for (const SessionOrderedEffectRecord &record : batch.Records())
    {
        const std::span<const std::byte> payload = batch.Payload(record);
        if (record.kind == SessionOrderedEffectKind::ManagedConnectionSend)
        {
            session->Network().CommitCapturedSend(payload);
            continue;
        }

        SessionSpatialAudioRequest request;
        std::memcpy(&request, payload.data(), sizeof(request));
        applicationAudio_->ApplySessionAudioGating(*bus);
        if (request.updateListener)
        {
            applicationAudio_->UpdateSessionSpatialAudio(*bus, request.cameraYaw,
                                                         request.listenerPosition.data());
        }
    }
    return true;
}

void SessionManager::AdvanceSkillDelays(int elapsedMs)
{
    if (!scheduling_)
    {
        return;
    }

    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->AdvanceSkillDelay(elapsedMs);
    }
}

void SessionManager::CompleteRenderCompletions(const ApplicationRenderCompletionBatch &completions,
                                               CGlobalBitmap &assets) noexcept
{
    (void)assets;
    for (std::size_t index = 0; index < completions.SessionCompletionCount(); ++index)
    {
        const SessionReplayCompletion *const completion = completions.SessionCompletionAt(index);
        if (completion == nullptr || !completion->succeeded)
        {
            continue;
        }
        const auto liveGeneration = generations_.find(completion->sessionId);
        GameSession *const session = Find(completion->sessionId);
        if (session == nullptr || liveGeneration == generations_.end() ||
            liveGeneration->second != completion->generation)
        {
            continue;
        }
        (void)session->CompleteRender(*completion);
    }

    for (std::size_t index = 0; index < completions.RequestCompletionCount(); ++index)
    {
        const RenderOwnerRequestCompletion *const completion =
            completions.RequestCompletionAt(index);
        if (completion == nullptr || !completion->succeeded)
        {
            continue;
        }
        const auto liveGeneration = generations_.find(completion->sessionId);
        GameSession *const session = Find(completion->sessionId);
        if (session == nullptr || liveGeneration == generations_.end() ||
            liveGeneration->second != completion->generation)
        {
            continue;
        }
        const std::span<const std::byte> payload = completions.Payload(*completion);
        if (payload.size() != completion->payloadByteCount)
        {
            continue;
        }
        (void)session->CompleteRenderRequest(*completion, payload);
    }
}

void SessionManager::UpdateResolutionDependentSystems()
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->UpdateResolutionDependentSystems();
    }
}

bool SessionManager::ToggleCameraZoomLock(SessionId id)
{
    GameSession *session = Find(id);
    if (session == nullptr)
    {
        return false;
    }
    session->ToggleCameraZoomLock();
    return true;
}

CUITextInputBox *SessionManager::FocusedTextInputBox(SessionId id) noexcept
{
    if (GameSession *session = Find(id))
    {
        return session->FocusedTextInputBox();
    }
    return nullptr;
}

bool SessionManager::ProcessModernUiInput(SessionId id, const SessionInputEvent &event) noexcept
{
    GameSession *const session = Find(id);
    UI::Modern::RmlUiRuntime *const runtime =
        applicationKeeper_ == nullptr ? nullptr : applicationKeeper_->ModernUiRuntime();
    if (session != nullptr && session->HasVisibleRenderSurface() && runtime != nullptr)
    {
        bool handled = false;
        const bool executed =
            runtime->Execute([&]() noexcept { handled = session->ProcessModernUiInput(event); });
        return executed && handled;
    }
    return false;
}

std::optional<SessionDisplayRect> SessionManager::ModernTextInputArea(SessionId id) const noexcept
{
    const GameSession *const session = Find(id);
    UI::Modern::RmlUiRuntime *const runtime =
        applicationKeeper_ == nullptr ? nullptr : applicationKeeper_->ModernUiRuntime();
    if (session == nullptr || !session->HasVisibleRenderSurface() || runtime == nullptr)
    {
        return std::nullopt;
    }
    std::optional<UI::Modern::RmlTextInputArea> area;
    if (!runtime->Execute([&]() noexcept { area = session->ModernTextInputArea(); }))
    {
        return std::nullopt;
    }
    if (!area.has_value())
    {
        return std::nullopt;
    }
    return SessionDisplayRect{area->x, area->y,
                              static_cast<std::uint32_t>((std::max)(area->width, 0)),
                              static_cast<std::uint32_t>((std::max)(area->height, 0))};
}

void SessionManager::SetMouseWheel(SessionId id, int wheel) noexcept
{
    if (GameSession *session = Find(id))
    {
        session->SetMouseWheel(wheel);
    }
}

void SessionManager::ApplyPointerMove(SessionId id, std::int32_t x, std::int32_t y) noexcept
{
    if (GameSession *session = Find(id))
    {
        session->ApplyPointerMove(x, y);
    }
}

void SessionManager::ApplyPointerButton(SessionId id, std::uint32_t button, bool pressed,
                                        std::uint8_t clicks) noexcept
{
    if (GameSession *session = Find(id))
    {
        session->ApplyPointerButton(button, pressed, clicks);
    }
}

void SessionManager::RouteKeyboardState(std::optional<SessionId> focused,
                                        const BYTE *states) noexcept
{
    for (auto &[id, session] : sessions_)
    {
        session->ApplyInputKeySnapshot(states, focused.has_value() && *focused == id);
    }
}

void SessionManager::ResetTransientMouseStates() noexcept
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->ResetTransientMouseState();
    }
}

void SessionManager::ResetMouseButtons() noexcept
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->ResetMouseButtons();
    }
}

void SessionManager::CycleCameraMode(SessionId id)
{
    if (GameSession *session = Find(id))
    {
        session->CycleCameraMode();
    }
}

void SessionManager::ResetCameraView(SessionId id)
{
    if (GameSession *session = Find(id))
    {
        session->ResetCameraView();
    }
}

void SessionManager::TickMuHelpers()
{
    if (!scheduling_)
    {
        return;
    }
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->TickMuHelper();
    }
}

void SessionManager::BeginReconnects()
{
    if (!scheduling_)
    {
        return;
    }
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->BeginReconnect();
    }
}

void SessionManager::UpdateReconnects()
{
    if (!scheduling_)
    {
        return;
    }
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->UpdateReconnect();
    }
}

void SessionManager::DeleteSockets()
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->DeleteSocket();
    }
}

void SessionManager::SendNetworkPings(DWORD tickCount)
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->SendNetworkPing(tickCount);
    }
}

void SessionManager::SendCheatDetectionLogouts()
{
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        session->SendCheatDetectionLogout();
    }
}

bool SessionManager::UpdateSceneCompatibility()
{
    if (!scheduling_)
    {
        return false;
    }

    bool ready = !sessions_.empty();
    for (auto &[id, session] : sessions_)
    {
        (void)id;
        ready = session->UpdateSceneCompatibility() && ready;
    }
    return ready;
}

void SessionManager::StopScheduling() noexcept
{
    if (!scheduling_)
    {
        return;
    }
    scheduling_ = false;
    for (const auto &[id, session] : sessions_)
    {
        (void)session;
        (void)QuiesceLane(id);
    }
    scheduler_.Stop();
}

bool SessionManager::IsScheduling() const noexcept
{
    return scheduling_;
}

std::span<const SessionAdvanceResult> SessionManager::LastAdvanceResults() const noexcept
{
    return frameResults_;
}

std::span<SessionAdvanceResult> SessionManager::LastAdvanceResults() noexcept
{
    return frameResults_;
}

ApplicationSessionSchedulerMetrics SessionManager::SchedulerMetrics() const noexcept
{
    return scheduler_.Metrics();
}

const SessionAdvancePhaseTimings &SessionManager::LastAdvancePhaseTimings() const noexcept
{
    return lastAdvancePhaseTimings_;
}

std::uint64_t SessionManager::LastFrameSequence() const noexcept
{
    return frameSequence_;
}

bool SessionManager::RegisterLane(SessionId id) noexcept
{
    const auto generation = scheduler_.RegisterLane(id);
    if (!generation.has_value())
    {
        return false;
    }
    try
    {
        generations_.emplace(id, *generation);
    }
    catch (...)
    {
        (void)scheduler_.QuiesceAndRemoveLane(id);
        return false;
    }
    return true;
}

bool SessionManager::QuiesceLane(SessionId id) noexcept
{
    const bool removed = scheduler_.QuiesceAndRemoveLane(id);
    generations_.erase(id);
    return removed;
}

bool SessionManager::RequiresRenderTape(GameSession &session) const noexcept
{
    return applicationKeeper_ != nullptr && session.RequiresRenderTape();
}

std::uint64_t SessionManager::SurfaceGeneration(GameSession &session) const noexcept
{
    return session.RenderSurfaceGeneration();
}

void SessionManager::PrepareFrameJobs(const ApplicationFramePlan &plan)
{
    for (std::size_t index = 0; index < plan.SessionCount(); ++index)
    {
        const SessionFrameInput &input = plan.Session(index);
        GameSession *session = Find(input.sessionId);
        frameWorkerContexts_.push_back({session, &input, {}});
        SessionWorkerContext &context = frameWorkerContexts_.back();
        context.frameDeltaMilliseconds = plan.FrameDeltaMilliseconds();
        frameJobs_.push_back({
            input.sessionId,
            input.generation,
            frameSequence_,
            input.renderRequired,
            input.surfaceGeneration,
            &context,
            [](void *rawContext) noexcept {
                auto *worker = static_cast<SessionWorkerContext *>(rawContext);
                return worker->session != nullptr && worker->input != nullptr &&
                       worker->session->BeginAdvanceFrameWorkerSafe(
                           worker->frameDeltaMilliseconds, worker->input->renderRequired,
                           worker->input->visualAnimation, worker->visualResult);
            },
            [](void *rawContext, SessionOrderedEffectBatch &effects) noexcept {
                auto *worker = static_cast<SessionWorkerContext *>(rawContext);
                if (worker->session == nullptr)
                    return false;
                worker->mainSceneUpdateRequired =
                    worker->session->PrepareMainSceneUpdate(worker->frameDeltaMilliseconds);
                return true;
            },
        });
        frameResults_.emplace_back(input.sessionId, input.generation, frameSequence_);
        context.advanceResult = &frameResults_.back();
    }
}
