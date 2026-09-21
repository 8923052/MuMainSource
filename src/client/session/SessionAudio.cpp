#include "session/SessionAudio.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationLoopFrame.h"
#include "data/GameData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

SessionAudioBus::SessionAudioBus(SessionId id) noexcept : id_(id), view_(*this)
{
}

SessionId SessionAudioBus::Id() const noexcept
{
    return id_;
}

SessionAudioBusView &SessionAudioBus::View() noexcept
{
    return view_;
}

const SessionAudioBusView &SessionAudioBus::View() const noexcept
{
    return view_;
}

bool SessionAudioBus::IsMuted() const noexcept
{
    return muted_;
}

void SessionAudioBus::SetMuted(bool muted) noexcept
{
    muted_ = muted;
}

void SessionAudioBus::AdvanceLogicalFrame() noexcept
{
    ++logicalFrame_;
}

std::uint64_t SessionAudioBus::LogicalFrame() const noexcept
{
    return logicalFrame_;
}

bool SessionAudioBus::PlayLogicalTrack(std::string_view track)
{
    if (track.empty())
    {
        return false;
    }
    logicalTrack_.assign(track);
    logicalTrackPosition_ = 0;
    logicalTrackPlaying_ = true;
    return true;
}

void SessionAudioBus::StopLogicalTrack() noexcept
{
    logicalTrackPlaying_ = false;
}

bool SessionAudioBus::IsLogicalTrackPlaying() const noexcept
{
    return logicalTrackPlaying_;
}

std::string_view SessionAudioBus::LogicalTrack() const noexcept
{
    return logicalTrack_;
}

void SessionAudioBus::SetLogicalTrackPosition(std::uint64_t position) noexcept
{
    logicalTrackPosition_ = position;
}

std::uint64_t SessionAudioBus::LogicalTrackPosition() const noexcept
{
    return logicalTrackPosition_;
}

bool SessionAudioBus::AttachLogicalEmitter(std::size_t channel,
                                           SessionAudioEmitterId emitter) noexcept
{
    if (channel >= logicalEmitters_.size())
    {
        return false;
    }
    logicalEmitters_[channel] = emitter;
    return true;
}

bool SessionAudioBus::DetachLogicalEmitter(std::size_t channel) noexcept
{
    if (channel >= logicalEmitters_.size())
    {
        return false;
    }
    logicalEmitters_[channel].reset();
    return true;
}

std::optional<SessionAudioEmitterId> SessionAudioBus::LogicalEmitter(
    std::size_t channel) const noexcept
{
    return channel < logicalEmitters_.size() ? logicalEmitters_[channel] : std::nullopt;
}

std::size_t SessionAudioBus::PlayLogicalEffect(int buffer, SessionAudioEmitterId emitter) noexcept
{
    LogicalEffectState &effect = logicalEffects_[buffer];
    const std::size_t channel = effect.activeChannel;
    effect.emitters[channel] = emitter;
    effect.activeChannel = (channel + 1) % effect.emitters.size();
    return channel;
}

void SessionAudioBus::StopLogicalEffect(int buffer) noexcept
{
    const auto effect = logicalEffects_.find(buffer);
    if (effect != logicalEffects_.end())
    {
        effect->second.emitters.fill(std::nullopt);
    }
}

void SessionAudioBus::StopAllLogicalEffects() noexcept
{
    logicalEffects_.clear();
}

std::optional<SessionAudioEmitterId> SessionAudioBus::LogicalEffectEmitter(
    int buffer, std::size_t channel) const noexcept
{
    const auto effect = logicalEffects_.find(buffer);
    return effect != logicalEffects_.end() && channel < effect->second.emitters.size()
               ? effect->second.emitters[channel]
               : std::nullopt;
}

SessionAudioBusCollection::SessionAudioBusCollection() noexcept : workspaceView_(*this)
{
}

bool SessionAudioBusCollection::AddSession(SessionId id)
{
    if (buses_.contains(id))
    {
        return false;
    }
    buses_.emplace(id, std::make_unique<SessionAudioBus>(id));
    ApplyGating();
    return true;
}

bool SessionAudioBusCollection::RemoveSession(SessionId id) noexcept
{
    if (buses_.erase(id) == 0)
    {
        return false;
    }
    if (focusedSession_ == id)
    {
        focusedSession_.reset();
    }
    ApplyGating();
    return true;
}

bool SessionAudioBusCollection::Focus(SessionId id) noexcept
{
    if (!buses_.contains(id))
    {
        return false;
    }
    focusedSession_ = id;
    ApplyGating();
    return true;
}

void SessionAudioBusCollection::ClearFocus() noexcept
{
    focusedSession_.reset();
    ApplyGating();
}

SessionAudioBusView *SessionAudioBusCollection::Audio(SessionId id) noexcept
{
    const auto bus = buses_.find(id);
    return bus == buses_.end() ? nullptr : &bus->second->View();
}

std::size_t SessionAudioBusCollection::SessionCount() const noexcept
{
    return buses_.size();
}

SessionAudioWorkspaceView &SessionAudioBusCollection::WorkspaceView() noexcept
{
    return workspaceView_;
}

void SessionAudioBusCollection::ApplyGating() noexcept
{
    for (const auto &[id, bus] : buses_)
    {
        bus->SetMuted(!focusedSession_.has_value() || id != *focusedSession_);
    }
}

SessionAudioBusView::SessionAudioBusView(SessionAudioBus &bus) noexcept : bus_(&bus)
{
}

SessionId SessionAudioBusView::Id() const noexcept
{
    return bus_->Id();
}

bool SessionAudioBusView::IsMuted() const noexcept
{
    return bus_->IsMuted();
}

void SessionAudioBusView::AdvanceLogicalFrame() noexcept
{
    bus_->AdvanceLogicalFrame();
}

std::uint64_t SessionAudioBusView::LogicalFrame() const noexcept
{
    return bus_->LogicalFrame();
}

bool SessionAudioBusView::PlayLogicalTrack(std::string_view track)
{
    return bus_->PlayLogicalTrack(track);
}

void SessionAudioBusView::StopLogicalTrack() noexcept
{
    bus_->StopLogicalTrack();
}

bool SessionAudioBusView::IsLogicalTrackPlaying() const noexcept
{
    return bus_->IsLogicalTrackPlaying();
}

std::string_view SessionAudioBusView::LogicalTrack() const noexcept
{
    return bus_->LogicalTrack();
}

void SessionAudioBusView::SetLogicalTrackPosition(std::uint64_t position) noexcept
{
    bus_->SetLogicalTrackPosition(position);
}

std::uint64_t SessionAudioBusView::LogicalTrackPosition() const noexcept
{
    return bus_->LogicalTrackPosition();
}

bool SessionAudioBusView::AttachLogicalEmitter(std::size_t channel,
                                               SessionAudioEmitterId emitter) noexcept
{
    return bus_->AttachLogicalEmitter(channel, emitter);
}

bool SessionAudioBusView::DetachLogicalEmitter(std::size_t channel) noexcept
{
    return bus_->DetachLogicalEmitter(channel);
}

std::optional<SessionAudioEmitterId> SessionAudioBusView::LogicalEmitter(
    std::size_t channel) const noexcept
{
    return bus_->LogicalEmitter(channel);
}

std::size_t SessionAudioBusView::PlayLogicalEffect(int buffer,
                                                   SessionAudioEmitterId emitter) noexcept
{
    return bus_->PlayLogicalEffect(buffer, emitter);
}

void SessionAudioBusView::StopLogicalEffect(int buffer) noexcept
{
    bus_->StopLogicalEffect(buffer);
}

void SessionAudioBusView::StopAllLogicalEffects() noexcept
{
    bus_->StopAllLogicalEffects();
}

std::optional<SessionAudioEmitterId> SessionAudioBusView::LogicalEffectEmitter(
    int buffer, std::size_t channel) const noexcept
{
    return bus_->LogicalEffectEmitter(buffer, channel);
}

SessionAudioLogicUnit::SessionAudioLogicUnit(SessionKeeper &keeper,
                                             SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), Destroy(keeper.PlatformDestroyRequested()),
      g_Camera(keeper.CameraStateObject()), applicationAudio_(keeper.ApplicationAudioObject()),
      audioBus_(keeper.AudioOutputForConstruction()), observer_(observer)
{
    (void)sessionKeeper_.RegisterAudioLogic(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::AudioLogicUnitConstructed);
    }
}

SessionAudioLogicUnit::~SessionAudioLogicUnit()
{
    applicationAudio_.ReleaseSessionAudio(sessionKeeper_.Id());
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::AudioLogicUnitDestroyed);
    }
}

bool SessionAudioLogicUnit::UpdateSpatialAudio() noexcept
{
    applicationAudio_.ApplySessionAudioGating(audioBus_);
    SessionAudioLogicView *audio = sessionKeeper_.AudioLogicView();
    return audio != nullptr &&
           (audio->UsesSessionOwnedLegacyBehavior() ? UpdateSceneSpatialAudio()
                                                    : audio->UpdateSpatialAudio());
}

bool SessionAudioLogicUnit::BuildSpatialAudioEffect(SessionOrderedEffectBatch &effects,
                                                    std::uint64_t stableSequence) noexcept
{
    SessionAudioLogicView *audio = sessionKeeper_.AudioLogicView();
    if (audio == nullptr)
    {
        return false;
    }
    if (!audio->UsesSessionOwnedLegacyBehavior())
    {
        return UpdateSpatialAudio();
    }

    SessionSpatialAudioRequest request{};
    if ((SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE || SceneFlag == MAIN_SCENE) &&
        Hero != nullptr)
    {
        request.updateListener = 1;
        request.cameraYaw = g_Camera.Angle[2];
        request.listenerPosition = {
            Hero->Object.Position[0],
            Hero->Object.Position[1],
            Hero->Object.Position[2],
        };
    }
    return effects.TryAppend(SessionOrderedEffectKind::HardwareAudio,
                             SessionSpatialAudioEffectChannel, stableSequence,
                             std::as_bytes(std::span(&request, 1)));
}

bool SessionAudioLogicUnit::UpdateSceneSpatialAudio()
{
    if (SceneFlag == LOG_IN_SCENE || SceneFlag == CHARACTER_SCENE || SceneFlag == MAIN_SCENE)
    {
        Set3DSoundPosition();
    }
    return true;
}

void SessionAudioLogicUnit::StopMusic()
{
    audioBus_.StopLogicalTrack();
    if (applicationAudio_.MasterMusicVolume() <= 0)
    {
        return;
    }
    applicationAudio_.StopMusic(audioBus_);
}

void SessionAudioLogicUnit::StopMp3(const char *name, BOOL enforce)
{
    if (applicationAudio_.MasterMusicVolume() <= 0 && !enforce)
    {
        return;
    }
    applicationAudio_.StopMp3(audioBus_, name, enforce);
    if (name != nullptr && audioBus_.LogicalTrack() == name)
    {
        audioBus_.StopLogicalTrack();
    }
}

void SessionAudioLogicUnit::PlayMp3(const char *name, BOOL enforce)
{
    if (Destroy || name == nullptr || (applicationAudio_.MasterMusicVolume() <= 0 && !enforce))
    {
        return;
    }
    if (audioBus_.LogicalTrack() == name && audioBus_.IsLogicalTrackPlaying())
    {
        return;
    }
    if (audioBus_.PlayLogicalTrack(name))
    {
        applicationAudio_.PlayMp3(audioBus_, name, enforce);
    }
}

bool SessionAudioLogicUnit::IsEndMp3()
{
    const bool ended = applicationAudio_.IsEndMp3(audioBus_);
    if (ended)
    {
        audioBus_.StopLogicalTrack();
    }
    return ended;
}

int SessionAudioLogicUnit::GetMp3PlayPosition()
{
    const int position = applicationAudio_.GetMp3PlayPosition(audioBus_);
    audioBus_.SetLogicalTrackPosition(static_cast<std::uint64_t>(position));
    return position;
}

HRESULT SessionAudioLogicUnit::PlayBuffer(ESound buffer, OBJECT *object, BOOL looped)
{
    std::uint64_t emitter = 0;
    if (object != nullptr)
    {
        const auto [entry, inserted] = emitterIds_.try_emplace(object, nextEmitterId_);
        if (inserted)
        {
            ++nextEmitterId_;
        }
        emitter = entry->second;
    }
    (void)audioBus_.PlayLogicalEffect(static_cast<int>(buffer), emitter);
    return applicationAudio_.PlayBuffer(audioBus_, buffer, emitter, looped);
}

void SessionAudioLogicUnit::StopBuffer(ESound buffer, BOOL resetPosition)
{
    audioBus_.StopLogicalEffect(static_cast<int>(buffer));
    applicationAudio_.StopBuffer(audioBus_, buffer, resetPosition);
}

void SessionAudioLogicUnit::AllStopSound()
{
    audioBus_.StopAllLogicalEffects();
    applicationAudio_.AllStopSound(audioBus_);
}

void SessionAudioLogicUnit::Set3DSoundPosition()
{
    if (Hero == nullptr)
        return;
    applicationAudio_.UpdateSessionSpatialAudio(audioBus_, g_Camera.Angle[2],
                                                Hero->Object.Position);
}

void SessionLegacyCalls::Set3DSoundPosition()
{
    sessionKeeper_.AudioLogic()->Set3DSoundPosition();
}
void SessionLegacyCalls::StopMusic()
{
    sessionKeeper_.AudioLogic()->StopMusic();
}
void SessionLegacyCalls::StopMp3(const char *name, BOOL enforce)
{
    sessionKeeper_.AudioLogic()->StopMp3(name, enforce);
}
void SessionLegacyCalls::PlayMp3(const char *name, BOOL enforce)
{
    sessionKeeper_.AudioLogic()->PlayMp3(name, enforce);
}
bool SessionLegacyCalls::IsEndMp3()
{
    return sessionKeeper_.AudioLogic()->IsEndMp3();
}
int SessionLegacyCalls::GetMp3PlayPosition()
{
    return sessionKeeper_.AudioLogic()->GetMp3PlayPosition();
}
HRESULT SessionLegacyCalls::PlayBuffer(ESound buffer, OBJECT *object, BOOL looped) const
{
    return sessionKeeper_.AudioLogic()->PlayBuffer(buffer, object, looped);
}
void SessionLegacyCalls::StopBuffer(ESound buffer, BOOL resetPosition)
{
    sessionKeeper_.AudioLogic()->StopBuffer(buffer, resetPosition);
}
void SessionLegacyCalls::AllStopSound()
{
    sessionKeeper_.AudioLogic()->AllStopSound();
}

SessionAudioWorkspaceView::SessionAudioWorkspaceView(SessionAudioBusCollection &buses) noexcept
    : buses_(&buses)
{
}

bool SessionAudioWorkspaceView::AddSession(SessionId id)
{
    return buses_->AddSession(id);
}

bool SessionAudioWorkspaceView::RemoveSession(SessionId id) noexcept
{
    return buses_->RemoveSession(id);
}

bool SessionAudioWorkspaceView::Focus(SessionId id) noexcept
{
    return buses_->Focus(id);
}

void SessionAudioWorkspaceView::ClearFocus() noexcept
{
    buses_->ClearFocus();
}

void SessionAudioWorkspaceView::ApplyGating() noexcept
{
    buses_->ApplyGating();
}

SessionAudioBusView *SessionAudioWorkspaceView::Audio(SessionId id) noexcept
{
    return buses_->Audio(id);
}

std::size_t SessionAudioWorkspaceView::SessionCount() const noexcept
{
    return buses_->SessionCount();
}

bool LegacySessionAudioLogicView::UpdateSpatialAudio() noexcept
{
    return true;
}

void MapProcess::PrepareAudio(BaseMap *map, int rawMap)
{
    static constexpr ESound Loops[] = {
        SOUND_WIND01,
        SOUND_RAIN01,
        SOUND_DUNGEON01,
        SOUND_FOREST01,
        SOUND_TOWER01,
        SOUND_WATER01,
        SOUND_DESERT01,
        SOUND_HEAVEN01,
        SOUND_ELBELAND_VILLAGEPROTECTION01,
        SOUND_ELBELAND_WATERFALLSMALL01,
        SOUND_ELBELAND_WATERWAY01,
        SOUND_ELBELAND_ENTERDEVIAS01,
        SOUND_ELBELAND_WATERSMALL01,
        SOUND_ELBELAND_RAVINE01,
        SOUND_ELBELAND_ENTERATLANCE01,
#ifdef ASG_ADD_MAP_KARUTAN
        SOUND_KARUTAN_DESERT_ENV,
        SOUND_KARUTAN_INSECT_ENV,
        SOUND_KARUTAN_KARDAMAHAL_ENV,
#endif
    };
    static constexpr const char *Music[] = {
        MUSIC_PUB,
        MUSIC_MAIN_THEME,
        MUSIC_CHURCH,
        MUSIC_DEVIAS,
        MUSIC_NORIA,
        MUSIC_DUNGEON,
        MUSIC_ATLANS,
        MUSIC_ICARUS,
        MUSIC_TARKAN,
        MUSIC_LOSTTOWER_A,
        MUSIC_KALIMA,
        MUSIC_BC_HUNTINGGROUND,
        MUSIC_KANTURU_1ST,
        MUSIC_SWAMP_OF_QUIET,
        MUSIC_BC_ADIA,
        MUSIC_ELBELAND,
        MUSIC_BC_CRYWOLF_1ST,
        MUSIC_KANTURU_TOWER,
        MUSIC_KANTURU_MAYA_BATTLE,
        MUSIC_KANTURU_NIGHTMARE_BATTLE,
        MUSIC_KANTURU_2ND,
        MUSIC_RAKLION,
        MUSIC_RAKLION_BOSS,
        MUSIC_SANTA_TOWN,
        MUSIC_PKFIELD,
        MUSIC_EMPIREGUARDIAN1,
        MUSIC_EMPIREGUARDIAN2,
        MUSIC_EMPIREGUARDIAN3,
        MUSIC_EMPIREGUARDIAN4,
#ifdef ASG_ADD_MAP_KARUTAN
        MUSIC_KARUTAN1,
        MUSIC_KARUTAN2,
#endif
        MUSIC_BALGAS_BARRACK,
        MUSIC_BALGAS_REFUGE,
    };
    inactiveAmbient_.clear();
    inactiveMusic_.clear();
    for (const auto sound : Loops)
        if (!map || !map->AllowsAmbientSound(sound))
            inactiveAmbient_.push_back(sound);
    for (const auto *track : Music)
        if (!map || !map->AllowsMusic(track))
            inactiveMusic_.push_back(track);
    audioMap_ = map;
    audioMapId_ = rawMap;
}

void MapProcess::UpdateWorldAudio()
{
    BaseMap *const map = ContextBehavior();
    const int rawMap = gMapManager.ContextMap();
    if (audioMap_ != map || audioMapId_ != rawMap)
        PrepareAudio(map, rawMap);
    if (map)
        map->PlayAmbientSounds();
    for (const auto sound : inactiveAmbient_)
        StopBuffer(sound, true);
    for (const auto *track : inactiveMusic_)
        StopMp3(track);
    if (map)
        map->UpdateMusic();
}

void SessionLegacyCalls::PlayBGM()
{
    sessionKeeper_.Gameplay()->TheMapProcess().Kanturu2nd().PlayBGM();
}
