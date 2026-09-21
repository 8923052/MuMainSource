#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#define WHISPER_SOUND_ON 0x04
#define WHISPER_SOUND_OFF 0x08

using SessionAudioEmitterId = std::uint64_t;

class SessionAudioBus;

class SessionAudioBusView final
{
  public:
    SessionId Id() const noexcept;
    bool IsMuted() const noexcept;
    void AdvanceLogicalFrame() noexcept;
    std::uint64_t LogicalFrame() const noexcept;
    bool PlayLogicalTrack(std::string_view track);
    void StopLogicalTrack() noexcept;
    bool IsLogicalTrackPlaying() const noexcept;
    std::string_view LogicalTrack() const noexcept;
    void SetLogicalTrackPosition(std::uint64_t position) noexcept;
    std::uint64_t LogicalTrackPosition() const noexcept;
    bool AttachLogicalEmitter(std::size_t channel, SessionAudioEmitterId emitter) noexcept;
    bool DetachLogicalEmitter(std::size_t channel) noexcept;
    std::optional<SessionAudioEmitterId> LogicalEmitter(std::size_t channel) const noexcept;
    std::size_t PlayLogicalEffect(int buffer, SessionAudioEmitterId emitter) noexcept;
    void StopLogicalEffect(int buffer) noexcept;
    void StopAllLogicalEffects() noexcept;
    std::optional<SessionAudioEmitterId> LogicalEffectEmitter(int buffer,
                                                              std::size_t channel) const noexcept;

  private:
    friend class SessionAudioBus;

    explicit SessionAudioBusView(SessionAudioBus &bus) noexcept;

    SessionAudioBus *bus_;
};

class SessionWorkspace;
class SessionAudioBusCollection;

class SessionAudioBus final
{
  public:
    explicit SessionAudioBus(SessionId id) noexcept;

    SessionAudioBus(const SessionAudioBus &) = delete;
    SessionAudioBus &operator=(const SessionAudioBus &) = delete;
    SessionAudioBus(SessionAudioBus &&) = delete;
    SessionAudioBus &operator=(SessionAudioBus &&) = delete;

    SessionId Id() const noexcept;
    SessionAudioBusView &View() noexcept;
    const SessionAudioBusView &View() const noexcept;

  private:
    friend class SessionAudioBusView;
    friend class SessionAudioBusCollection;
    friend class SessionWorkspace;

    bool IsMuted() const noexcept;
    void SetMuted(bool muted) noexcept;
    void AdvanceLogicalFrame() noexcept;
    std::uint64_t LogicalFrame() const noexcept;
    bool PlayLogicalTrack(std::string_view track);
    void StopLogicalTrack() noexcept;
    bool IsLogicalTrackPlaying() const noexcept;
    std::string_view LogicalTrack() const noexcept;
    void SetLogicalTrackPosition(std::uint64_t position) noexcept;
    std::uint64_t LogicalTrackPosition() const noexcept;
    bool AttachLogicalEmitter(std::size_t channel, SessionAudioEmitterId emitter) noexcept;
    bool DetachLogicalEmitter(std::size_t channel) noexcept;
    std::optional<SessionAudioEmitterId> LogicalEmitter(std::size_t channel) const noexcept;
    std::size_t PlayLogicalEffect(int buffer, SessionAudioEmitterId emitter) noexcept;
    void StopLogicalEffect(int buffer) noexcept;
    void StopAllLogicalEffects() noexcept;
    std::optional<SessionAudioEmitterId> LogicalEffectEmitter(int buffer,
                                                              std::size_t channel) const noexcept;

    SessionId id_;
    bool muted_ = true;
    std::uint64_t logicalFrame_ = 0;
    std::string logicalTrack_;
    std::uint64_t logicalTrackPosition_ = 0;
    bool logicalTrackPlaying_ = false;
    static constexpr std::size_t LogicalChannelCount = 4;
    std::array<std::optional<SessionAudioEmitterId>, LogicalChannelCount> logicalEmitters_{};
    struct LogicalEffectState
    {
        std::array<std::optional<SessionAudioEmitterId>, LogicalChannelCount> emitters{};
        std::size_t activeChannel = 0;
    };
    std::map<int, LogicalEffectState> logicalEffects_;
    SessionAudioBusView view_;
};

class SessionAudioBusCollection;
class SessionAudioBusView;

class SessionAudioWorkspaceView final
{
  public:
    bool AddSession(SessionId id);
    bool RemoveSession(SessionId id) noexcept;
    bool Focus(SessionId id) noexcept;
    void ClearFocus() noexcept;
    void ApplyGating() noexcept;
    SessionAudioBusView *Audio(SessionId id) noexcept;
    std::size_t SessionCount() const noexcept;

  private:
    friend class SessionAudioBusCollection;

    explicit SessionAudioWorkspaceView(SessionAudioBusCollection &buses) noexcept;

    SessionAudioBusCollection *buses_;
};

class SessionAudioBusCollection final
{
  public:
    SessionAudioBusCollection() noexcept;

    bool AddSession(SessionId id);
    bool RemoveSession(SessionId id) noexcept;
    bool Focus(SessionId id) noexcept;
    void ClearFocus() noexcept;
    void ApplyGating() noexcept;
    SessionAudioBusView *Audio(SessionId id) noexcept;
    std::size_t SessionCount() const noexcept;
    SessionAudioWorkspaceView &WorkspaceView() noexcept;

  private:
    friend class SessionAudioWorkspaceView;

    std::map<SessionId, std::unique_ptr<SessionAudioBus>> buses_;
    std::optional<SessionId> focusedSession_;
    SessionAudioWorkspaceView workspaceView_;
};

class SessionKeeper;
class GameSessionTestPeer;
class SessionLifecycleObserver;
class ApplicationAudio;
class CameraState;
class SessionAudioBusView;
class SessionOrderedEffectBatch;

class SessionAudioLogicUnit final : protected SessionLegacyCalls
{
  public:
    SessionAudioLogicUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionAudioLogicUnit();

    bool UpdateSpatialAudio() noexcept;
    bool BuildSpatialAudioEffect(SessionOrderedEffectBatch &effects,
                                 std::uint64_t stableSequence) noexcept;

  private:
    friend class SessionLegacyCalls;
    friend class GameSessionTestPeer;

    void PlayMp3(const char *name, BOOL enforce = false);
    void StopMusic();
    void StopMp3(const char *name, BOOL enforce);
    bool IsEndMp3();
    int GetMp3PlayPosition();
    HRESULT PlayBuffer(ESound buffer, OBJECT *object = nullptr, BOOL looped = false);
    void StopBuffer(ESound buffer, BOOL resetPosition);
    void AllStopSound();
    bool UpdateSceneSpatialAudio();
    void Set3DSoundPosition();

    bool &Destroy;
    CameraState &g_Camera;
    ApplicationAudio &applicationAudio_;
    SessionAudioBusView &audioBus_;
    std::unordered_map<OBJECT *, std::uint64_t> emitterIds_;
    std::uint64_t nextEmitterId_ = 1;
    SessionLifecycleObserver *observer_;
};

class SessionAudioLogicView
{
  public:
    virtual ~SessionAudioLogicView() = default;

    virtual bool UsesSessionOwnedLegacyBehavior() const noexcept
    {
        return false;
    }
    virtual bool UpdateSpatialAudio() noexcept = 0;
};

inline constexpr std::uint32_t SessionSpatialAudioEffectChannel = 1;

struct SessionSpatialAudioRequest final
{
    std::uint32_t updateListener = 0;
    float cameraYaw = 0.0f;
    std::array<float, 3> listenerPosition{};
};

static_assert(std::is_trivially_copyable_v<SessionSpatialAudioRequest>);
static_assert(sizeof(SessionSpatialAudioRequest) == 20);

class LegacySessionAudioLogicView final : public SessionAudioLogicView
{
  public:
    bool UsesSessionOwnedLegacyBehavior() const noexcept override
    {
        return true;
    }
    bool UpdateSpatialAudio() noexcept override;
};

constexpr const char *MUSIC_PUB = "data\\music\\Pub.mp3";
constexpr const char *MUSIC_MUTHEME = "data\\music\\Mutheme.mp3";
constexpr const char *MUSIC_CHURCH = "data\\music\\Church.mp3";
constexpr const char *MUSIC_DEVIAS = "data\\music\\Devias.mp3";
constexpr const char *MUSIC_NORIA = "data\\music\\Noria.mp3";
constexpr const char *MUSIC_DUNGEON = "data\\music\\Dungeon.mp3";
constexpr const char *MUSIC_ATLANS = "data\\music\\atlans.mp3";
constexpr const char *MUSIC_ICARUS = "data\\music\\icarus.mp3";
constexpr const char *MUSIC_TARKAN = "data\\music\\tarkan.mp3";
constexpr const char *MUSIC_LOSTTOWER_A = "data\\music\\lost_tower_a.mp3";
constexpr const char *MUSIC_LOSTTOWER_B = "data\\music\\lost_tower_b.mp3";
constexpr const char *MUSIC_KALIMA = "data\\music\\kalima.mp3";
constexpr const char *MUSIC_CASTLE_PEACE = "data\\music\\castle.mp3";
constexpr const char *MUSIC_CASTLE_BATTLE_START = "data\\music\\charge.mp3";
constexpr const char *MUSIC_CASTLE_BATTLE_ING = "data\\music\\lastend.mp3";
constexpr const char *MUSIC_BC_HUNTINGGROUND = "data\\music\\huntingground.mp3";
constexpr const char *MUSIC_BC_ADIA = "data\\music\\Aida.mp3";
constexpr const char *MUSIC_BC_CRYWOLF_1ST = "data\\music\\crywolf1st.mp3";
constexpr const char *MUSIC_CRYWOLF_READY = "data\\music\\crywolf_ready-02.ogg";
constexpr const char *MUSIC_CRYWOLF_BEFORE = "data\\music\\crywolf_before-01.ogg";
constexpr const char *MUSIC_CRYWOLF_BACK = "data\\music\\crywolf_back-03.ogg";
constexpr const char *MUSIC_MAIN_THEME = "data\\music\\main_theme.mp3";
constexpr const char *MUSIC_KANTURU_1ST = "data\\music\\kanturu_1st.mp3";
constexpr const char *MUSIC_KANTURU_2ND = "data\\music\\kanturu_2nd.mp3";
constexpr const char *MUSIC_KANTURU_MAYA_BATTLE = "data\\music\\KanturuMayaBattle.mp3";
constexpr const char *MUSIC_KANTURU_NIGHTMARE_BATTLE = "data\\music\\KanturuNightmareBattle.mp3";
constexpr const char *MUSIC_KANTURU_TOWER = "data\\music\\KanturuTower.mp3";
constexpr const char *MUSIC_BALGAS_BARRACK = "data\\music\\BalgasBarrack.mp3";
constexpr const char *MUSIC_BALGAS_REFUGE = "data\\music\\BalgasRefuge.mp3";
constexpr const char *MUSIC_CURSEDTEMPLE_WAIT = "data\\music\\cursedtemplewait.mp3";
constexpr const char *MUSIC_CURSEDTEMPLE_GAME = "data\\music\\cursedtempleplay.mp3";
constexpr const char *MUSIC_ELBELAND = "data\\music\\elbeland.mp3";
constexpr const char *MUSIC_LOGIN_THEME = "data\\music\\login_theme.mp3";
constexpr const char *MUSIC_SWAMP_OF_QUIET = "data\\music\\SwampOfCalmness.mp3";
constexpr const char *MUSIC_RAKLION = "data\\music\\Raklion.mp3";
constexpr const char *MUSIC_RAKLION_BOSS = "data\\music\\Raklion_Hatchery.mp3";
constexpr const char *MUSIC_SANTA_TOWN = "data\\music\\Santa_Village.mp3";
constexpr const char *MUSIC_DUEL_ARENA = "data\\music\\DuelArena.mp3";
constexpr const char *MUSIC_PKFIELD = "data\\music\\PK_Field.mp3";
constexpr const char *MUSIC_EMPIREGUARDIAN1 = "data\\music\\ImperialGuardianFort.mp3";
constexpr const char *MUSIC_EMPIREGUARDIAN2 = "data\\music\\ImperialGuardianFort.mp3";
constexpr const char *MUSIC_EMPIREGUARDIAN3 = "data\\music\\ImperialGuardianFort.mp3";
constexpr const char *MUSIC_EMPIREGUARDIAN4 = "data\\music\\ImperialGuardianFort.mp3";
constexpr const char *MUSIC_DOPPELGANGER = "data\\music\\iDoppelganger.mp3";
constexpr const char *MUSIC_UNITEDMARKETPLACE = "data\\music\\iDoppelganger.mp3";
#ifdef ASG_ADD_MAP_KARUTAN
constexpr const char *MUSIC_KARUTAN1 = "data\\music\\Karutan_A.mp3";
constexpr const char *MUSIC_KARUTAN2 = "data\\music\\Karutan_B.mp3";
#endif
