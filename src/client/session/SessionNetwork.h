#pragma once
#include "support/CoreMath.h"
#include "app/ApplicationNetwork.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/WorldData.h"
#include "session/SessionRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <typeinfo>
#include <vector>

class GameplayExternalEventBatch;

#pragma pack(push)
#pragma pack()
typedef struct
{
    BYTE Code;
    BYTE Size;
    BYTE HeadCode;
} PBMSG_HEADER, *LPPBMSG_HEADER;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_subCode;
    BYTE m_Type;
    WORD m_Time;
} PMSG_MATCH_TIMEVIEW, *LPPMSG_MATCH_TIMEVIEW;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
typedef struct _PMSG_MATCH_RESULT
{
    void Clear()
    {
        std::memset(&m_MatchTeamName1, 0, sizeof m_MatchTeamName1);
        std::memset(&m_MatchTeamName2, 0, sizeof m_MatchTeamName2);
        m_Score1 = 0;
        m_Score2 = 0;
        m_Type = 0;
    }

    PBMSG_HEADER m_Header;
    BYTE m_subCode;
    BYTE m_Type;
    char m_MatchTeamName1[MAX_USERNAME_SIZE];
    WORD m_Score1;
    char m_MatchTeamName2[MAX_USERNAME_SIZE];
    WORD m_Score2;
} PMSG_MATCH_RESULT, *LPPMSG_MATCH_RESULT;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
typedef struct _MATCH_RESULT
{
    void Clear()
    {
        std::memset(&m_MatchTeamName1, 0, sizeof m_MatchTeamName1);
        std::memset(&m_MatchTeamName2, 0, sizeof m_MatchTeamName2);
        m_Score1 = 0;
        m_Score2 = 0;
        m_Type = 0;
    }

    PBMSG_HEADER m_Header;
    BYTE m_subCode;
    BYTE m_Type;
    wchar_t m_MatchTeamName1[MAX_USERNAME_SIZE];
    WORD m_Score1;
    wchar_t m_MatchTeamName2[MAX_USERNAME_SIZE];
    WORD m_Score2;
} MATCH_RESULT, *LPP_MATCH_RESULT;
#pragma pack(pop)

class SessionNetworkUnit;
class CUIMng;
namespace MUHelper
{
class SessionMuHelperUnit;
}

// Drives automatic reconnection after an in-game disconnect (issue #338).
// When the connection to the game server drops while playing, the client caches
// the data needed to resume the session (game-server address, account
// credentials, selected character) and replays the normal
// connect -> login -> character-list -> select-character -> join sequence,
// retrying the probe every few seconds until the server is back. Because the
// server persists character state, re-entering the game restores the player
// exactly where they left off.
// The manager only injects the two steps the scene loops cannot do on their own
// (sending the login and picking the character). Everything else flows through
// the existing per-frame scene state machine (NewMoveLogInScene /
// NewMoveCharacterScene / StartGame), which advances on CurrentProtocolState.
class ReconnectManager : protected SessionLegacyCalls
{
  public:
    enum class Phase
    {
        Idle,          // not reconnecting
        Probing,       // in-game, world still rendered; probing the server for life
        Connecting,    // socket opened, waiting for the server hello
        LoggingIn,     // login sent, login/character scene loops are driving
        SelectingChar, // character list arrived, selecting the cached character
        Joining,       // loading into the world
        Retrying,      // post-teardown: re-connect + re-login attempt failed, retry
        Done,          // back in game; dialog dismissed next frame
    };

    ReconnectManager(SessionKeeper &keeper) noexcept;
    ~ReconnectManager();

    // Session cache, populated during normal play.
    void CacheServer(const wchar_t *ip, unsigned short port);
    void CacheCredentials(const wchar_t *username, const wchar_t *password);
    void CacheCharacter(const wchar_t *characterName);
    void ClearSession();
    bool HasSession() const
    {
        return m_hasSession;
    }

    // Lifecycle.
    void RequestBegin();  // detector asks to start reconnect (begins probing)
    bool Begin();         // true when the between-frame teardown reset gameplay
    void Update();        // per-frame driver (runs in the scene-update phase)
    void RequestCancel(); // dialog's Cancel button; processed by Update()

    // Read by ReconnectDialog.
    bool IsActive() const
    {
        return m_active || m_beginPending;
    }
    Phase GetPhase() const
    {
        return m_phase;
    }
    int GetStepIndex() const;        // 1-based progress step for the bar
    static int GetStepCount();       // total steps the bar represents
    int GetCountdownSeconds() const; // seconds until the next probe (Waiting only)

  private:
    friend class GameSessionTestPeer;
    CUIMng &LegacyUiManager() noexcept
    {
        return legacyUiManager_;
    }
    void EnterPhase(Phase phase);
    void Abort();             // give up -> back to the login screen
    double ElapsedMs() const; // time spent in the current phase
    bool IsSocketAlive() const;
    bool TrySelectCachedCharacter();

    CUIMng &legacyUiManager_;

    void UpdateProbing();
    void UpdateConnecting();
    void UpdateLoggingIn();
    void UpdateSelectingChar();
    void UpdateJoining();
    void UpdateRetrying();

    // Background TCP reachability probe (keeps the game rendered while waiting).
    void StartProbe();
    void PollProbe();
    void CloseProbe();

    bool m_active = false;
    bool m_beginPending = false;       // RequestBegin posted, Begin not yet run
    bool m_cancelRequested = false;    // Cancel clicked, Abort not yet run
    bool m_abortAfterTeardown = false; // cancel during Probing: tear down, then abort
    bool m_hasSession = false;
    bool m_muHelperWasActive = false; // MU Helper state to restore after reconnect
    MUHelper::SessionMuHelperUnit &muHelper_;
    double &WorldTime;
    Phase m_phase = Phase::Idle;
    double m_phaseStartTime = 0.0;

    // Background probe socket (uintptr_t holds a winsock SOCKET; ~0 = none).
    uintptr_t m_probeSocket = static_cast<uintptr_t>(~0ull);
    double m_probeStartTime = 0.0;
    bool m_wsaInitialised = false;

    wchar_t m_serverIp[256] = {}; // IPv4 literal or hostname
    unsigned short m_serverPort = 0;
    wchar_t m_username[MAX_USERNAME_SIZE + 1] = {};
    wchar_t m_password[MAX_PASSWORD_SIZE + 1] = {};
    wchar_t m_characterName[MAX_USERNAME_SIZE + 1] = {};
};

struct MapServerInfo
{
    std::array<char, 16> m_szMapSvrIpAddress{};
    std::uint16_t m_wMapSvrPort{0};
    std::uint16_t m_wMapSvrCode{0};
    std::int32_t m_iJoinAuthCode1{0};
    std::int32_t m_iJoinAuthCode2{0};
    std::int32_t m_iJoinAuthCode3{0};
    std::int32_t m_iJoinAuthCode4{0};
};
class SessionNetworkUnit;

using MServerInfo = MapServerInfo;

class CSMServer : protected SessionLegacyCalls
{
  public:
    explicit CSMServer(SessionKeeper &keeper);
    ~CSMServer() = default;

    void Init(void);

    void SetHeroID(wchar_t *ID);

    void SetServerInfo(MServerInfo sInfo);
    void GetServerInfo(MServerInfo &sInfo);

    void GetServerAddress(wchar_t *szAddress);
    std::uint16_t GetServerPort() const
    {
        return (m_hasServerInfo ? m_serverInfo.m_wMapSvrPort : 0);
    }
    std::uint16_t GetServerCode() const
    {
        return (m_hasServerInfo ? m_serverInfo.m_wMapSvrCode : 0);
    }

    void ConnectChangeMapServer(MServerInfo sInfo);
    bool MatchesServer(const wchar_t *address, std::uint16_t port) const noexcept;
    bool SendChangeMapServer();

  private:
    std::wstring m_heroId;
    MapServerInfo m_serverInfo{};
    bool m_hasServerInfo{false};
};

struct CHARACTER_ENABLE
{
    bool bCharacterEnable[3]{};
};

using LPCHARACTER_ENABLE = CHARACTER_ENABLE *;
#ifndef __SOCKETCLIENT_H__
#define __SOCKETCLIENT_H__

#define WM_ASYNCSELECTMSG (WM_USER + 0)

#define MAX_CHAT_SIZE 90
#define SIZE_PROTOCOLVERSION (5)
#define SIZE_PROTOCOLSERIAL (16)
#define MAX_GUILDNAME 8

inline constexpr BYTE Version[SIZE_PROTOCOLVERSION] = {'2', '0', '4', '0', '4'};
inline constexpr BYTE Serial[SIZE_PROTOCOLSERIAL + 1] = {
    'k', '1', 'P', 'k', '2', 'j', 'c', 'E', 'T', '4', '8', 'm', 'x', 'L', '3', 'b', 0};
inline constexpr int DirTable[16] = {-1, -1, 0, -1, 1, -1, 1, 0, 1, 1, 0, 1, -1, 1, -1, 0};

#define REQUEST_JOIN_SERVER 0
#define RECEIVE_JOIN_SERVER_WAITING 1
#define RECEIVE_JOIN_SERVER_SUCCESS 2
#define RECEIVE_JOIN_SERVER_FAIL_VERSION 3
#define REQUEST_CREATE_ACCOUNT 10
#define RECEIVE_CREATE_ACCOUNT_SUCCESS 11
#define RECEIVE_CREATE_ACCOUNT_FAIL_ID 12
#define RECEIVE_CREATE_ACCOUNT_FAIL_RESIDENT 13

#define REQUEST_LOG_IN 19
#define RECEIVE_LOG_IN_SUCCESS 20
#define RECEIVE_LOG_IN_FAIL_PASSWORD 21
#define RECEIVE_LOG_IN_FAIL_ID 22
#define RECEIVE_LOG_IN_FAIL_ID_CONNECTED 23
#define RECEIVE_LOG_IN_FAIL_SERVER_BUSY 24
#define RECEIVE_LOG_IN_FAIL_ID_BLOCK 25
#define RECEIVE_LOG_IN_FAIL_VERSION 26
#define RECEIVE_LOG_IN_FAIL_CONNECT 27
#define RECEIVE_LOG_IN_FAIL_ERROR 28
#define RECEIVE_LOG_IN_FAIL_USER_TIME1 29
#define RECEIVE_LOG_IN_FAIL_USER_TIME2 30
#define RECEIVE_LOG_IN_FAIL_PC_TIME1 31
#define RECEIVE_LOG_IN_FAIL_PC_TIME2 32
#define RECEIVE_LOG_IN_FAIL_DATE 33
#define RECEIVE_LOG_IN_FAIL_POINT_DATE 34
#define RECEIVE_LOG_IN_FAIL_POINT_HOUR 35
#define RECEIVE_LOG_IN_FAIL_INVALID_IP 36
#define RECEIVE_LOG_IN_FAIL_NO_PAYMENT_INFO 37
#define RECEIVE_LOG_IN_FAIL_ONLY_OVER_15 38
#define RECEIVE_LOG_IN_FAIL_CHARGED_CHANNEL 39

#define REQUEST_CHARACTERS_LIST 50
#define RECEIVE_CHARACTERS_LIST 51
#define REQUEST_CREATE_CHARACTER 52
#define RECEIVE_CREATE_CHARACTER_SUCCESS 53
#define RECEIVE_CREATE_CHARACTER_FAIL 54
#define RECEIVE_CREATE_CHARACTER_FAIL2 55
#define REQUEST_DELETE_CHARACTER 56
#define RECEIVE_DELETE_CHARACTER_SUCCESS 57
#define REQUEST_JOIN_MAP_SERVER 60
#define RECEIVE_JOIN_MAP_SERVER 61
#define RECEIVE_CONFIRM_PASSWORD_SUCCESS 62
#define RECEIVE_CONFIRM_PASSWORD_FAIL_ID 63
#define RECEIVE_CONFIRM_PASSWORD2_SUCCESS 64
#define RECEIVE_CONFIRM_PASSWORD2_FAIL_ID 65
#define RECEIVE_CONFIRM_PASSWORD2_FAIL_ANSWER 66
#define RECEIVE_CONFIRM_PASSWORD2_FAIL_RESIDENT 67
#define RECEIVE_CHANGE_PASSWORD_SUCCESS 68
#define RECEIVE_CHANGE_PASSWORD_FAIL_ID 69
#define RECEIVE_CHANGE_PASSWORD_FAIL_RESIDENT 70
#define RECEIVE_CHANGE_PASSWORD_FAIL_PASSWORD 71

#define PACKET_ITEM_LENGTH_EXTENDED_MIN 5
#define PACKET_ITEM_LENGTH_EXTENDED_MAX 15

#define EQUIPMENT_LENGTH_EXTENDED 25
#define MAX_SPE_BUFFERSIZE_ (2048)

// English Protocol:
#define PACKET_MOVE 0xD4
#define PACKET_POSITION 0x15
#define PACKET_MAGIC_ATTACK 0xDB
#define PACKET_ATTACK 0x11

inline uint64_t ntoh64(uint64_t value)
{
    return ((value & 0x00000000000000FFULL) << 56) | ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x0000000000FF0000ULL) << 24) | ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x000000FF00000000ULL) >> 8) | ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x00FF000000000000ULL) >> 40) | ((value & 0xFF00000000000000ULL) >> 56);
}

typedef struct
{
    BYTE Code;
    BYTE SizeH;
    BYTE SizeL;
    BYTE HeadCode;
} PWMSG_HEADER, *LPPWMSG_HEADER;

typedef struct
{
    BYTE Code;
    BYTE Size;
    BYTE byBuffer[255];
} PBMSG_ENCRYPTED, *LPPBMSG_ENCRYPTED;

typedef struct
{
    BYTE Code;
    BYTE SizeH;
    BYTE SizeL;
    BYTE byBuffer[MAX_SPE_BUFFERSIZE_];
} PWMSG_ENCRYPTED, *LPWBMSG_ENCRYPTED;

//request default SubCode
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
} PREQUEST_DEFAULT_SUBCODE, *LPPREQUEST_DEFAULT_SUBCODE;

//receive default
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Value;
} PHEADER_DEFAULT, *LPPHEADER_DEFAULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE result;
    WORD btStatValue;
    BYTE btFruitType;
} PMSG_USE_STAT_FRUIT, *LPPMSG_USE_STAT_FRUIT;

//receive default subcode
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Value;
} PHEADER_DEFAULT_SUBCODE, *LPPHEADER_DEFAULT_SUBCODE;

//receive Character List
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE MaxClass;
    BYTE MoveCount;
    BYTE CharacterCount;
    BYTE IsVaultExtended;
} PHEADER_DEFAULT_CHARACTER_LIST, *LPPHEADER_DEFAULT_CHARACTER_LIST;

#define CLASS_SUMMONER_CARD 0x01
#define CLASS_DARK_LORD_CARD 0x02
#define CLASS_DARK_CARD 0x04
#define CLASS_CHARACTERCARD_TOTALCNT 3

typedef struct
{
    PBMSG_HEADER header;
    BYTE Flag;
    BYTE CharacterCard;
} PHEADER_CHARACTERCARD, *LPPHEADER_CHARACTERCARD;

//receive default key
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
} PHEADER_DEFAULT_KEY, *LPPHEADER_DEFAULT_KEY;

//receive default key
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Value;
    BYTE KeyH;
    BYTE KeyL;
} PHEADER_DEFAULT_VALUE_KEY, *LPPHEADER_DEFAULT_VALUE_KEY;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Value;
    BYTE KeyH;
    BYTE KeyM;
    BYTE KeyL;
} PHEADER_MATCH_OPEN_VALUE, *LPPHEADER_MATCH_OPEN_VALUE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE State;
    BYTE KeyH;
    BYTE KeyL;
    BYTE BuffIndex;
} PMSG_VIEWSKILLSTATE, *LPPMSG_VIEWSKILLSTATE;

//receive default(word)
typedef struct
{
    PWMSG_HEADER Header;
    BYTE Value;
} PWHEADER_DEFAULT_WORD, *LPPWHEADER_DEFAULT_WORD;

//receive default(word)
typedef struct
{
    PWMSG_HEADER Header;
    INT Value;
} PWHEADER_DEFAULT_WORD2, *LPPWHEADER_DEFAULT_WORD2;

//receive default subcode(word)
typedef struct
{
    PWMSG_HEADER Header;
    BYTE SubCode;
    BYTE Value;
} PHEADER_DEFAULT_SUBCODE_WORD, *LPPHEADER_DEFAULT_SUBCODE_WORD;

typedef struct
{
    PBMSG_HEADER Header;
    WORD Value;
} PHEADER_DEFAULT_WORD, *LPPHEADER_DEFAULT_WORD;

typedef struct
{
    PBMSG_HEADER Header;
    DWORD Value;
} PHEADER_DEFAULT_DWORD, *LPPHEADER_DEFAULT_DWORD;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE byBuffer[1024];
} PHEADER_DEFAULT_CUSTOM, *LPPHEADER_DEFAULT_CUSTOM;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
} PHEADER_DEFAULT_ITEM_EXTENDED_HEAD, *LPPHEADER_DEFAULT_ITEM_EXTENDED_HEAD;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PHEADER_DEFAULT_ITEM_EXTENDED, *LPPHEADER_DEFAULT_ITEM_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Index;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PHEADER_DEFAULT_SUBCODE_ITEM_EXTENDED, *LPPHEADER_DEFAULT_SUBCODE_ITEM_EXTENDED;

// log in

typedef struct
{
    WORD Index;
    BYTE Percent;
} PRECEIVE_SERVER_LIST, *LPPRECEIVE_SERVER_LIST;

//receive join server
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Result;
    BYTE NumberH;
    BYTE NumberL;
    BYTE Version[SIZE_PROTOCOLVERSION];
} PRECEIVE_JOIN_SERVER, *LPPRECEIVE_JOIN_SERVER;

//receive confirm password
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Result;
    char Question[30];
} PRECEIVE_CONFIRM_PASSWORD, *LPPRECEIVE_CONFIRM_PASSWORD;

//receive confirm password
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Result;
    char Password[MAX_USERNAME_SIZE];
} PRECEIVE_CONFIRM_PASSWORD2, *LPPRECEIVE_CONFIRM_PASSWORD2;

typedef struct
{
    BYTE Index;
    char ID[MAX_USERNAME_SIZE];
    WORD Level;
    BYTE CtlCode;
    SERVER_CLASS_TYPE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE byGuildStatus;
} PRECEIVE_CHARACTER_LIST_EXTENDED, *LPPRECEIVE_CHARACTER_LIST_EXTENDED;

//receive create character
#pragma pack(push, 1)
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Result;
    char ID[MAX_USERNAME_SIZE];
    BYTE Index;
    WORD Level;
    BYTE Class; // SERVER_CLASS_TYPE, shifted by 3 bits
    //BYTE         Equipment[24];
} PRECEIVE_CREATE_CHARACTER, *LPPRECEIVE_CREATE_CHARACTER;
#pragma pack(pop)

//receive join map server
#pragma pack(push, 1)
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE PositionX;
    BYTE PositionY;
    BYTE Map;
    BYTE Angle;
    uint64_t CurrentExperience;
    uint64_t ExperienceForNextLevel;
    WORD LevelUpPoint;
    WORD Strength;
    WORD Dexterity;
    WORD Vitality;
    WORD Energy;
    WORD Charisma;
    DWORD Life;
    DWORD LifeMax;
    DWORD Mana;
    DWORD ManaMax;
    DWORD Shield;
    DWORD ShieldMax;
    DWORD SkillMana;
    DWORD SkillManaMax;
    DWORD Gold;
    BYTE PK;
    BYTE CtlCode;
    short AddPoint;
    short MaxAddPoint;
    WORD wMinusPoint;
    WORD wMaxMinusPoint;
    WORD AttackSpeed;
    WORD MagicSpeed;
    WORD MaxAttackSpeed;
    BYTE InventoryExtensions;
    BYTE Spare;
    WORD Resets;
} PRECEIVE_JOIN_MAP_SERVER_EXTENDED, *LPPRECEIVE_JOIN_MAP_SERVER_EXTENDED;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE PositionX;
    BYTE PositionY;
    BYTE Map;
    BYTE Angle;
    DWORD Life;
    DWORD Mana;
    DWORD Shield;
    DWORD SkillMana;
    uint64_t CurrentExperience;

    DWORD Gold;
} PRECEIVE_REVIVAL_EXTENDED, *LPPRECEIVE_REVIVAL_EXTENDED;
#pragma pack(pop)

//inventory
typedef struct
{
    BYTE Index;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PRECEIVE_INVENTORY_EXTENDED, *LPPRECEIVE_INVENTORY_EXTENDED;

/// <summary>
/// Layout:
///   Group:  4 bit
///   Number: 12 bit
///   Level:  8 bit
///   Dura:   8 bit
///   OptFlags: 8 bit
///     HasOpt
///     HasLuck
///     HasSkill
///     HasExc
///     HasAnc
///     HasGuardian
///     HasHarmony
///     HasSockets
///   Optional, depending on Flags:
///     Opt_Lvl 4 bit
///     Opt_Typ 4 bit
///     Exc:    8 bit
///     Anc_Dis 4 bit
///     Anc_Bon 4 bit
///     Harmony 8 bit
///     Soc_Bon 4 bit
///     Soc_Cnt 4 bit
///     Sockets n * 8 bit
///  Total: 5 ~ 15 bytes.
/// </summary>
#pragma pack(push, 1)
typedef struct
{
    WORD GroupAndNumber;
    BYTE Level;
    BYTE Durability;
    ItemOptionFlags OptionFlags;
} PITEM_EXTENDED_BASE, *LPPITEM_EXTENDED_BASE;
#pragma pack(pop)

// trade
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char ID[MAX_USERNAME_SIZE];
    WORD Level;
    DWORD GuildKey;
} PTRADE, *LPPTRADE;

// game

//request chat
typedef struct
{
    PBMSG_HEADER Header;
    char ID[MAX_USERNAME_SIZE];
    char ChatText[MAX_CHAT_SIZE];
} PCHATING, *LPPCHATING;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    char ChatText[MAX_CHAT_SIZE];
} PCHATING_KEY, *LPPCHATING_KEY;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    BYTE Count;
    WORD Delay;
    DWORD Color;
    BYTE Speed;
    char Notice[256];
} PRECEIVE_NOTICE, *LPPRECEIVE_NOTICE;

//receive equipment
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE KeyH;
    BYTE KeyL;
    BYTE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
} PRECEIVE_EQUIPMENT_EXTENDED, *LPPRECEIVE_EQUIPMENT_EXTENDED;

//receive other map character
typedef struct
{
    PWMSG_HEADER Header;
    WORD Key;
    BYTE PositionX;
    BYTE PositionY;
    BYTE TargetX;
    BYTE TargetY;

    BYTE RotationAndHeroState;
    WORD AttackSpeed;
    WORD MagicSpeed;
    char ID[MAX_USERNAME_SIZE];

    SERVER_CLASS_TYPE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE s_BuffCount;
} PCREATE_CHARACTER_EXTENDED, *LPPCREATE_CHARACTER_EXTENDED;

//receive other map character
typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
    BYTE PositionX;
    BYTE PositionY;
    BYTE TypeH;
    BYTE TypeL;
    char ID[MAX_USERNAME_SIZE];
    BYTE TargetX;
    BYTE TargetY;
    BYTE Path;
    SERVER_CLASS_TYPE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE s_BuffCount;
    BYTE s_BuffEffectState[MAX_BUFF_SLOT_INDEX];
} PCREATE_TRANSFORM_EXTENDED, *LPPCREATE_TRANSFORM_EXTENDED;

//receive other map character
typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
    BYTE TypeH;
    BYTE TypeL;
    BYTE PositionX;
    BYTE PositionY;
    BYTE TargetX;
    BYTE TargetY;
    BYTE Path;
    char ID[MAX_USERNAME_SIZE];
    BYTE s_BuffCount;
    BYTE s_BuffEffectState[MAX_BUFF_SLOT_INDEX];
} PCREATE_SUMMON, *LPPCREATE_SUMMON;

//receive other map character
typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
    BYTE TypeH;
    BYTE TypeL;
    BYTE PositionX;
    BYTE PositionY;
    BYTE TargetX;
    BYTE TargetY;
    BYTE Path;
    BYTE s_BuffCount;
    BYTE s_BuffEffectState[MAX_BUFF_SLOT_INDEX];
} PCREATE_MONSTER, *LPPCREATE_MONSTER;

//receive move character
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE SourceX;
    BYTE SourceY;
    BYTE TargetX;
    BYTE TargetY;
    BYTE PathMetadata;
} PMOVE_CHARACTER, *LPPMOVE_CHARACTER;

//delete character and item
typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
} PDELETE_CHARACTER, *LPPDELETE_CHARACTER;

//create item
typedef struct
{
    BYTE IdL;
    BYTE IdH;
    BYTE PositionX;
    BYTE PositionY;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PCREATE_ITEM_EXTENDED, *LPPCREATE_ITEM_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE IsFreshDrop;
    WORD Id;
    BYTE PositionX;
    BYTE PositionY;
    DWORD Amount;
} PCREATE_MONEY, *LPPCREATE_MONEY;

//change character
typedef struct
{
    PBMSG_HEADER Header;
    WORD Key;
    BYTE ItemSlot;
    BYTE ItemGroup;
    WORD ItemNumber;
    BYTE ItemLevel;
    BYTE ExcellentFlags;
    BYTE AncientDiscriminator;
    BYTE IsAncientSetComplete;
} PCHANGE_CHARACTER_EXTENDED, *LPPCHANGE_CHARACTER_EXTENDED;

//receive get item
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PRECEIVE_GET_ITEM_EXTENDED, *LPPRECEIVE_GET_ITEM_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    BYTE Money[4];
} PRECEIVE_INVENTORY_MONEY, *LPPRECEIVE_INVENTORY_MONEY;

//receive attack
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE DamageH;
    BYTE DamageL;
    BYTE DamageType;
    BYTE ShieldDamageH;
    BYTE ShieldDamageL;
} PRECEIVE_ATTACK, *LPPRECEIVE_ATTACK;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE DamageType;    // 3
    WORD TargetId;      // 4
    BYTE HealthStatus;  // 6 status of the remaining health in fractions of 1/250
    BYTE ShieldStatus;  // 7 status of the remaining shield in fractions of 1/250
    DWORD HealthDamage; // 8
    DWORD ShieldDamage; // 12
} PRECEIVE_ATTACK_EXTENDED, *LPPRECEIVE_ATTACK_EXTENDED;

//receive die
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE ExpH;
    BYTE ExpL;
    BYTE DamageH;
    BYTE DamageL;
} PRECEIVE_DIE, *LPPRECEIVE_DIE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE ExperienceType;   // 3
    DWORD AddedExperience; // 4
    DWORD DamageOfLastHit; // 8
    WORD KilledObjectId;   // 12
    WORD KillerObjectId;   // 14
} PRECEIVE_EXP_EXTENDED, *LPPRECEIVE_EXP_EXTENDED;

//receive default key
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE MagicH;
    BYTE MagicL;
    BYTE TKeyH;
    BYTE TKeyL;
} PHEADER_DEFAULT_DIE, *LPPHEADER_DEFAULT_DIE;

//receive action
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE Angle;
    BYTE Action;
    BYTE TargetKeyH;
    BYTE TargetKeyL;
} PRECEIVE_ACTION, *LPPRECEIVE_ACTION;

//receive magic continue
typedef struct
{
    PBMSG_HEADER Header;
    BYTE MagicH;
    BYTE MagicL;
    BYTE KeyH;
    BYTE KeyL;
    BYTE PositionX;
    BYTE PositionY;
    BYTE Angle;
} PRECEIVE_MAGIC_CONTINUE, *LPPRECEIVE_MAGIC_CONTINUE;

//receive magic
typedef struct
{
    PBMSG_HEADER Header;
    BYTE MagicH;
    BYTE MagicL;
    BYTE SourceKeyH;
    BYTE SourceKeyL;
    BYTE TargetKeyH;
    BYTE TargetKeyL;
} PRECEIVE_MAGIC, *LPPRECEIVE_MAGIC;

//receive MonsterSkill
typedef struct
{
    PBMSG_HEADER Header;
    BYTE MagicH;
    BYTE MagicL;
    WORD SourceKey;
    WORD TargetKey;
} PRECEIVE_MONSTERSKILL, *LPPRECEIVE_MONSTERSKILL;

//receive magic target
typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
} PRECEIVE_MAGIC_POSITION, *LPPRECEIVE_MAGIC_POSITION;

//receive magic target
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;

    BYTE MagicH;
    BYTE MagicL;

    BYTE PositionX;
    BYTE PositionY;
    BYTE Count;
} PRECEIVE_MAGIC_POSITIONS, *LPPRECEIVE_MAGIC_POSITIONS;

//receive magic list count
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Value;
    BYTE ListType;
} PHEADER_MAGIC_LIST_COUNT, *LPPHEADER_MAGIC_LIST_COUNT;

#pragma pack(push, 1)
//receive magic target
typedef struct
{
    BYTE Index;

    WORD Type;
    BYTE Level;
} PRECEIVE_MAGIC_LIST, *LPPRECEIVE_MAGIC_LIST;
#pragma pack(pop)

//receive skill count.
typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE m_byType;
    BYTE m_byCount;
} PRECEIVE_EX_SKILL_COUNT, *LPPRECEIVE_EX_SKILL_COUNT;

struct PMSG_MASTER_SKILL_LIST_SEND
{
    PWMSG_HEADER header; // C2:F3:E2
    BYTE subcode;
    DWORD count;
};

struct PMSG_MASTER_SKILL_LIST
{
    BYTE SkillIndex; // Index in skill tree
    BYTE SkillLevel;
    float MainValue;
    float NextValue;
};

//receive gold
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Flag;
    DWORD Gold;
} PRECEIVE_GOLD, *LPPRECEIVE_GOLD;

//receive repair gold
typedef struct
{
    PBMSG_HEADER Header;
    DWORD Gold;
} PRECEIVE_REPAIR_GOLD, *LPPRECEIVE_REPAIR_GOLD;

//receive level up
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    WORD Level;
    WORD LevelUpPoint;
    WORD MaxLife;
    WORD MaxMana;
    WORD MaxShield;
    WORD SkillManaMax;
    short AddPoint;
    short MaxAddPoint;
    WORD wMinusPoint;
    WORD wMaxMinusPoint;
} PRECEIVE_LEVEL_UP, *LPPRECEIVE_LEVEL_UP;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    WORD Level;
    WORD LevelUpPoint;
    DWORD MaxLife;
    DWORD MaxMana;
    DWORD MaxShield;
    DWORD SkillManaMax;
    short AddPoint;
    short MaxAddPoint;
    WORD wMinusPoint;
    WORD wMaxMinusPoint;
} PRECEIVE_LEVEL_UP_EXTENDED, *LPPRECEIVE_LEVEL_UP_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    BYTE Life[5];
} PRECEIVE_LIFE, *LPPRECEIVE_LIFE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    DWORD Life;
    DWORD Shield;
    DWORD Mana;
    DWORD BP;
    WORD AttackSpeed;
    WORD MagicSpeed;
} PRECEIVE_STATS_EXTENDED, *LPPRECEIVE_STATS_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    DWORD Life;
    DWORD Shield;
    DWORD Mana;
    DWORD BP;
} PRECEIVE_MAX_STATS_EXTENDED, *LPPRECEIVE_MAX_STATS_EXTENDED;

//receive add point
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Result;
    WORD Max;
    WORD ShieldMax;
    WORD SkillManaMax;
} PRECEIVE_ADD_POINT, *LPPRECEIVE_ADD_POINT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;       // 3
    BYTE StatType;      // 4
    WORD AddedAmount;   // 6
    DWORD MaxHealth;    // 8
    DWORD MaxMana;      // 12
    DWORD ShieldMax;    // 16
    DWORD SkillManaMax; // 20
} PRECEIVE_ADD_POINT_EXTENDED, *LPPRECEIVE_ADD_POINT_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;    // 3
    DWORD Strength;  // 4
    DWORD Dexterity; // 8
    DWORD Vitality;  // 12
    DWORD Energy;    // 16
    DWORD Charisma;  // 20
} PRECEIVE_SET_POINTS_EXTENDED, *LPPRECEIVE_SET_POINTS_EXTENDED;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    BYTE PositionX;
    BYTE PositionY;
} PRECEIVE_MOVE_POSITION, *LPPRECEIVE_MOVE_POSITION;

typedef struct
{
    PBMSG_HEADER Header;
    WORD Flag;
    BYTE Map;
    BYTE PositionX;
    BYTE PositionY;
    BYTE Angle;
} PRECEIVE_TELEPORT_POSITION, *LPPRECEIVE_TELEPORT_POSITION;

//receive damage
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE DamageH;
    BYTE DamageL;
    BYTE ShieldDamageH;
    BYTE ShieldDamageL;
} PRECEIVE_DAMAGE, *LPPRECEIVE_DAMAGE;

//receive party info
typedef struct
{
    BYTE value;
} PRECEIVE_PARTY_INFO, *LPPRECEIVE_PARTY_INFO;

//receive party infos
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Count;
} PRECEIVE_PARTY_INFOS, *LPPRECEIVE_PARTY_INFOS;

//receive party list
typedef struct
{
    char ID[MAX_USERNAME_SIZE];
    BYTE Number;
    BYTE Map;
    BYTE x;
    BYTE y;
    int currHP;
    int maxHP;
} PRECEIVE_PARTY_LIST, *LPPRECEIVE_PARTY_LIST;

//receive party list
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    BYTE Count;
} PRECEIVE_PARTY_LISTS, *LPPRECEIVE_PARTY_LISTS;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE KeyH;
    BYTE KeyL;
    int ItemInfo;
    BYTE ItemLevel;
} PRECEIVE_GETITEMINFO_FOR_PARTY, *LPPRECEIVE_GETITEMINFO_FOR_PARTY;

//receive pk
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE KeyH;
    BYTE KeyL;
    BYTE PK;
} PRECEIVE_PK, *LPPRECEIVE_PK;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    WORD Time;
} PRECEIVE_HELPER_ITEM, *LPPRECEIVE_HELPER_ITEM;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char IP[15];
    WORD Port;
} PRECEIVE_SERVER_ADDRESS, *LPPRECEIVE_SERVER_ADDRESS;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Server;
} PRECEIVE_SERVER_BUSY, *LPPRECEIVE_SERVER_BUSY;

//typedef struct {
//    BYTE         KeyH;
//    BYTE         KeyL;
//    char         Name[8];
//    BYTE         Mark[32];
//} PRECEIVE_GUILD, * LPPRECEIVE_GUILD;
//typedef struct {
//    PBMSG_HEADER Header;
//    BYTE         KeyH;
//    BYTE         KeyL;
//    char         Name[8];
//    BYTE         Mark[32];
//} PRECEIVE_GUILD_MARK, * LPPRECEIVE_GUILD_MARK;

typedef struct
{
    BYTE KeyH;
    BYTE KeyL;
    BYTE GuildKeyH;
    BYTE GuildKeyL;
} PRECEIVE_GUILD_PLAYER, *LPPRECEIVE_GUILD_PLAYER;

// Guild member list
typedef struct
{
    char ID[MAX_USERNAME_SIZE];
    BYTE Number;
    BYTE CurrentServer;
    BYTE GuildStatus;
} PRECEIVE_GUILD_LIST, *LPPRECEIVE_GUILD_LIST;

// Guild member list packet
typedef struct
{
    PWMSG_HEADER Header;
    BYTE Result;
    BYTE Count;
    DWORD TotalScore;
    BYTE Score;
    char szRivalGuildName[MAX_GUILDNAME];
} PRECEIVE_GUILD_LISTS, *LPPRECEIVE_GUILD_LISTS;

//receive guild war
typedef struct
{
    PBMSG_HEADER Header;
    char Name[8];
    BYTE Type;
    BYTE Team;
} PRECEIVE_WAR, *LPPRECEIVE_WAR;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Score1;
    BYTE Score2;
    BYTE Type;
} PRECEIVE_WAR_SCORE, *LPPRECEIVE_WAR_SCORE;

typedef struct
{
    int GuildKey;
    BYTE GuildStatus;
    BYTE GuildType;
    BYTE GuildRelationShip;
    BYTE KeyH;
    BYTE KeyL;
} PRECEIVE_GUILD_ID, *LPPRECEIVE_GUILD_ID;

typedef struct
{
    PBMSG_HEADER Header;
    int GuildKey;
    BYTE GuildType;
    char UnionName[MAX_GUILDNAME];
    char GuildName[MAX_GUILDNAME];
    BYTE Mark[32];
} PPRECEIVE_GUILDINFO, *LPPPRECEIVE_GUILDINFO;

enum GUILD_REQ_COMMON_RESULT
{
    GUILD_ANS_NOTEXIST_GUILD = 0x10,
    GUILD_ANS_UNIONFAIL_BY_CASTLE = 0x10,
    GUILD_ANS_NOTEXIST_PERMISSION = 0x11,
    GUILD_ANS_NOTEXIST_EXTRA_STATUS = 0x12,
    GUILD_ANS_NOTEXIST_EXTRA_TYPE = 0x13,
    GUILD_ANS_EXIST_RELATIONSHIP_UNION = 0x15,
    GUILD_ANS_EXIST_RELATIONSHIP_RIVAL = 0x16,
    GUILD_ANS_EXIST_UNION = 0x17,
    GUILD_ANS_EXIST_RIVAL = 0x18,
    GUILD_ANS_NOTEXIST_UNION = 0x19,
    GUILD_ANS_NOTEXIST_RIVAL = 0x1A,
    GUILD_ANS_NOT_UNION_MASTER = 0x1B,
    GUILD_ANS_NOT_GUILD_RIVAL = 0x1C,
    GUILD_ANS_CANNOT_BE_UNION_MASTER_GUILD = 0x1D,
    GUILD_ANS_EXCEED_MAX_UNION_MEMBER = 0x1E,
    GUILD_ANS_CANCEL_REQUEST = 0x20,
    GUILD_ANS_UNION_MASTER_NOT_GENS = 0xA1,
    GUILD_ANS_GUILD_MASTER_NOT_GENS = 0xA2,
    GUILD_ANS_UNION_MASTER_DISAGREE_GENS = 0xA3,
};

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Value;
    BYTE GuildType;
} PMSG_GUILD_CREATE_RESULT, *LPPMSG_GUILD_CREATE_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE byGuildType;
    BYTE byResult;
} PMSG_GUILD_ASSIGN_TYPE_RESULT, *LPPMSG_GUILD_ASSIGN_TYPE_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE byType;
    BYTE byResult;
    char szTargetName[MAX_USERNAME_SIZE];
} PRECEIVE_GUILD_ASSIGN, *LPPRECEIVE_GUILD_ASSIGN;

typedef struct
{
    PWMSG_HEADER Header;
    BYTE byCount;
    BYTE byResult;
    BYTE byRivalCount;
    BYTE byUnionCount;
} PMSG_UNIONLIST_COUNT, *LPPMSG_UNIONLIST_COUNT;

typedef struct
{
    BYTE byMemberCount;
    BYTE GuildMark[32];
    char szGuildName[MAX_GUILDNAME];
} PMSG_UNIONLIST, *LPPMSG_UNIONLIST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE byRelationShipType;
    BYTE byRequestType;
    BYTE byTargetUserIndexH;
    BYTE byTargetUserIndexL;
} PMSG_GUILD_RELATIONSHIP, *LPPMSG_GUILD_RELATIONSHIP;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE byRelationShipType;
    BYTE byRequestType;
    BYTE byResult;
    BYTE byTargetUserIndexH;
    BYTE byTargetUserIndexL;
} PMSG_GUILD_RELATIONSHIP_RESULT, *LPPMSG_GUILD_RELATIONSHIP_RESULT;

typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE byResult;
    BYTE byRequestType;
    BYTE byRelationShipType;
} PMSG_BAN_UNIONGUILD, *LPPMSG_BAN_UNIONGUILD;

typedef struct
{
    PWMSG_HEADER Header;
    BYTE byCount;
} PMSG_UNION_VIEWPORT_NOTIFY_COUNT, *LPPMSG_UNION_VIEWPORT_NOTIFY_COUNT;
typedef struct
{
    BYTE byKeyH;
    BYTE byKeyL;
    int nGuildKey;
    BYTE byGuildRelationShip;
    char szUnionName[MAX_GUILDNAME];
} PMSG_UNION_VIEWPORT_NOTIFY, *LPPMSG_UNION_VIEWPORT_NOTIFY;

//receive gold
typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    DWORD StorageGold;
    DWORD Gold;
} PRECEIVE_STORAGE_GOLD, *LPPRECEIVE_STORAGE_GOLD;

//receive soccer time
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    WORD Time;
} PRECEIVE_SOCCER_TIME, *LPPRECEIVE_SOCCER_TIME;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char Name1[8];
    BYTE Score1;
    char Name2[8];
    BYTE Score2;
} PRECEIVE_SOCCER_SCORE, *LPPRECEIVE_SOCCER_SCORE;

#pragma pack(push, 1)
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE HotKey[20];
    BYTE GameOption;
    BYTE KeyQWE[3];
    BYTE ChatLogBox;
    BYTE KeyR;
    int QWERLevel;
} PRECEIVE_OPTION, *LPPRECEIVE_OPTION;
#pragma pack(pop)

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE Cmd1;
    BYTE Cmd2;
    BYTE Cmd3;
} PRECEIVE_SERVER_COMMAND, *LPPRECEIVE_SERVER_COMMAND;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_MyRank;
    BYTE m_Count;
    BYTE m_byRank;
} PDEVILRANK, *LPPDEVILRANK;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byType;
    int m_nChipCount;
    short m_shMutoNum[3];
} PRECEIVE_EVENT_CHIP_INFO, *LPPRECEIVE_EVENT_CHIP_INFO;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byType;
    unsigned int m_unChipCount;
} PRECEIVE_ENVET_CHIP, *LPPRECEIVE_EVENT_CHIP;

typedef struct
{
    PBMSG_HEADER Header;
    short m_shMutoNum[3];
} PRECEIVE_MUTONUMBER, *LPPRECEIVE_MUTONUMBER;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byIsRegistered;
    char m_strGiftName[64];
} PRECEIVE_SCRATCH_TICKET_EVENT, *LPPRECEIVE_SCRATCH_TICKET_EVENT;

typedef struct
{
    PBMSG_HEADER Header;
    WORD wEffectNum;
} PRECEIVE_PLAY_SOUND_EFFECT, *LPPRECEIVE_PLAY_SOUND_EFFECT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byValue;
    BYTE m_byNumber;
} PHEADER_EVENT, *LPPHEADER_EVENT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_wEventType;
    BYTE m_wLeftEnterCount;
} PRECEIVE_EVENT_COUNT, *LPPRECEIVE_EVENT_COUNT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byCount;
    BYTE m_byQuest[50];
} PRECEIVE_QUEST_HISTORY, *LPPRECEIVE_QUEST_HISTORY;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byQuestIndex;
    BYTE m_byState;
} PRECEIVE_QUEST_STATE, *LPPRECEIVE_QUEST_STATE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byQuestIndex;
    BYTE m_byResult;
    BYTE m_byState;
} PRECEIVE_QUEST_RESULT, *LPPRECEIVE_QUEST_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
    BYTE m_byReparation;
    BYTE m_byNumber; // SERVER_CLASS_TYPE, shifted by 3
} PRECEIVE_QUEST_REPARATION, *LPPRECEIVE_QUEST_REPARATION;

// GC[0xF6][0x0A]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    WORD m_wNPCIndex;
    WORD m_wQuestCount;
} PMSG_NPCTALK_QUESTLIST, *LPPMSG_NPCTALK_QUESTLIST;

typedef struct
{
    PWMSG_HEADER Header;
    BYTE SubCode;
    BYTE m_byRequestCount;
    BYTE m_byRewardCount;
    BYTE m_byRandRewardCount;
    DWORD m_dwQuestIndex;
} PMSG_NPC_QUESTEXP_INFO, *LPPMSG_NPC_QUESTEXP_INFO;

// GC[0xF6][0x0B] QuestStepInfo. C1 packet, 11 bytes. The server sends it
// when the player selects a quest in the quest list (carrying StartingNumber),
// when a quest has been started (carrying Number), or after the player refused
// to start (carrying RefuseNumber). The client uses the (Group, StepNumber)
// pair to look up the local quest progress entry.
#pragma pack(push, 1)
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    WORD m_wQuestStepNumber;
    WORD m_wQuestGroup;
} PMSG_QUEST_STEP_INFO, *LPPMSG_QUEST_STEP_INFO;
#pragma pack(pop)

enum QUEST_REQUEST_TYPE : BYTE
{
    QUEST_REQUEST_NONE = 0,
    QUEST_REQUEST_MONSTER = 1,
    QUEST_REQUEST_SKILL = 2,
    QUEST_REQUEST_ITEM = 3,
    QUEST_REQUEST_LEVEL = 4,
    QUEST_REQUEST_TUTORIAL = 5,
    QUEST_REQUEST_BUFF = 6,
    QUEST_REQUEST_EVENT_MAP_USER_KILL = 7,
    QUEST_REQUEST_EVENT_MAP_MON_KILL = 8,
    QUEST_REQUEST_EVENT_MAP_BLOOD_GATE = 9,
    QUEST_REQUEST_EVENT_MAP_CLEAR_BLOOD = 10,
    QUEST_REQUEST_EVENT_MAP_CLEAR_CHAOS = 11,
    QUEST_REQUEST_EVENT_MAP_CLEAR_DEVIL = 12,
    QUEST_REQUEST_EVENT_MAP_CLEAR_ILLUSION = 13,
    QUEST_REQUEST_EVENT_MAP_DEVIL_POINT = 14,
    QUEST_REQUEST_ZEN = 15,
    QUEST_REQUEST_PVP_POINT = 16,
    QUEST_REQUEST_NPC_TALK = 17,
};

typedef struct
{
    QUEST_REQUEST_TYPE m_dwType;
    WORD m_wIndex;
    DWORD m_dwValue;
    DWORD m_wCurValue;
    BYTE m_byItemInfo[PACKET_ITEM_LENGTH_EXTENDED_MAX];
} NPC_QUESTEXP_REQUEST_INFO, *LPNPC_QUESTEXP_REQUEST_INFO;

enum QUEST_REWARD_TYPE : BYTE
{
    QUEST_REWARD_NONE = 0x0000,
    QUEST_REWARD_EXP = 0x0001,
    QUEST_REWARD_ZEN = 0x0002,
    QUEST_REWARD_ITEM = 0x0004,
    QUEST_REWARD_BUFF = 0x0008,
    QUEST_REWARD_CONTRIBUTE = 0x0010,
    QUEST_REWARD_RANDOM = 0x0020,
};

typedef struct
{
    QUEST_REWARD_TYPE m_dwType;
    WORD m_wIndex;
    DWORD m_dwValue;
    BYTE m_byItemInfo[PACKET_ITEM_LENGTH_EXTENDED_MAX];
} NPC_QUESTEXP_REWARD_INFO, *LPNPC_QUESTEXP_REWARD_INFO;

typedef struct
{
    NPC_QUESTEXP_REQUEST_INFO NpcQuestRequestInfo[5];
    NPC_QUESTEXP_REWARD_INFO NpcQuestRewardInfo[5];
} NPC_QUESTEXP_INFO, *LPNPC_QUESTEXP_INFO;

// GC[0xF6][0x0D]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    DWORD m_dwQuestIndex;
    BYTE m_byResult;
} PMSG_ANS_QUESTEXP_COMPLETE, *LPPMSG_ANS_QUESTEXP_COMPLETE;

// GC[0xF6][0x0F]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    DWORD m_dwQuestGiveUpIndex;
} PMSG_ANS_QUESTEXP_GIVEUP, *LPPMSG_ANS_QUESTEXP_GIVEUP;

// GC[0xF6][0x1A]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    BYTE m_byQuestCount;
} PMSG_ANS_QUESTEXP_PROGRESS_LIST, *LPPMSG_ANS_QUESTEXP_PROGRESS_LIST;

// GC[0xF8][0x02]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    BYTE m_byResult;
    BYTE m_byInfluence;
} PMSG_ANS_REG_GENS_MEMBER, *LPPMSG_ANS_REG_GENS_MEMBER;

// GC[0xF8][0x04]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    BYTE m_byResult;
} PMSG_ANS_SECEDE_GENS_MEMBER, *LPPMSG_ANS_SECEDE_GENS_MEMBER;

// GC[0xF8][0x07]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    BYTE m_byInfluence;
    int m_nRanking;
    int m_nGensClass;
    int m_nContributionPoint;
    int m_nNextContributionPoint;
} PMSG_MSG_SEND_GENS_INFO, *LPPMSG_MSG_SEND_GENS_INFO;

// GC[0xF8][0x05]
typedef struct
{
    PWMSG_HEADER Header;
    BYTE SubCode;

    BYTE m_byCount;
} PMSG_SEND_GENS_MEMBER_VIEWPORT, *LPPMSG_SEND_GENS_MEMBER_VIEWPORT;

typedef struct
{
    BYTE m_byInfluence;
    BYTE m_byNumberH;
    BYTE m_byNumberL;
    int m_nRanking;
    int m_nGensClass;
    int m_nContributionPoint;
} PMSG_GENS_MEMBER_VIEWPORT_INFO, *LPPMSG_GENS_MEMBER_VIEWPORT_INFO;
#endif // ASG_ADD_GENS_SYSTEM

// GC[0xF8][0x08]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE m_byRewardResult;
} PMSG_GENS_REWARD_CODE, *LPPMSG_GENS_REWARD_CODE;

// GC[0xF9][0x01]
typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;

    WORD m_wNPCIndex;
    DWORD m_dwContributePoint;
} PMSG_ANS_NPC_CLICK, *LPPMSG_ANS_NPC_CLICK;

typedef struct
{
    BYTE m_byX;
    BYTE m_byY;
} PRECEIVE_MAP_ATTRIBUTE, *LPPRECEIVE_MAP_ATTRIBUTE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE m_byType;
    BYTE m_byMapAttr;
    BYTE m_byMapSetType;
    BYTE m_byCount;

    PRECEIVE_MAP_ATTRIBUTE m_vAttribute[128 * 128];
} PRECEIVE_SET_MAPATTRIBUTE, *LPPRECEIVE_SET_MAPATTRIBUTE;

typedef struct PRECEIVE_MATCH_GAME_STATE
{
    PBMSG_HEADER Header;
    BYTE m_byPlayState;
    WORD m_wRemainSec;
    WORD m_wMaxKillMonster;
    WORD m_wCurKillMonster;
    WORD m_wIndex;
    BYTE m_byItemType;
} PRECEIVE_MATCH_GAME_STATE, *LPPRECEIVE_MATCH_GAME_STATE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nResult;
    BYTE bIndexH;
    BYTE bIndexL;
    char szID[MAX_USERNAME_SIZE];
} PMSG_ANS_DUEL_INVITE, *LPPMSG_ANS_DUEL_INVITE;

typedef struct _tagPMSG_REQ_DUEL_ANSWER // SC2
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE bIndexH;
    BYTE bIndexL;
    char szID[MAX_USERNAME_SIZE];
} PMSG_REQ_DUEL_ANSWER, *LPPMSG_REQ_DUEL_ANSWER;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nResult;
    BYTE bIndexH;
    BYTE bIndexL;
    char szID[MAX_USERNAME_SIZE];
} PMSG_ANS_DUEL_EXIT, *LPPMSG_ANS_DUEL_EXIT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE bIndexH1;
    BYTE bIndexL1;
    BYTE bIndexH2;
    BYTE bIndexL2;
    BYTE btDuelScore1;
    BYTE btDuelScore2;
} PMSG_DUEL_SCORE_BROADCAST, *LPPMSG_DUEL_SCORE_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE bIndexH1;
    BYTE bIndexL1;
    BYTE bIndexH2;
    BYTE bIndexL2;
    BYTE btHP1;
    BYTE btHP2;
    BYTE btShield1;
    BYTE btShield2;
} PMSG_DUEL_HP_BROADCAST, *LPPMSG_DUEL_HP_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    struct
    {
        char szID1[MAX_USERNAME_SIZE];
        char szID2[MAX_USERNAME_SIZE];
        BYTE bStart;
        BYTE bWatch;
    } channel[4];
} PMSG_ANS_DUEL_CHANNELLIST, *LPPMSG_ANS_DUEL_CHANNELLIST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nResult;
    BYTE nChannelId;
    char szID1[MAX_USERNAME_SIZE];
    char szID2[MAX_USERNAME_SIZE];
    BYTE bIndexH1;
    BYTE bIndexL1;
    BYTE bIndexH2;
    BYTE bIndexL2;
} PMSG_ANS_DUEL_JOINCNANNEL, *LPPMSG_ANS_DUEL_JOINCNANNEL;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char szID[MAX_USERNAME_SIZE];
} PMSG_DUEL_JOINCNANNEL_BROADCAST, *LPPMSG_DUEL_JOINCNANNEL_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nResult;
} PMSG_ANS_DUEL_LEAVECNANNEL, *LPPMSG_ANS_DUEL_LEAVECNANNEL;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char szID[MAX_USERNAME_SIZE];
} PMSG_DUEL_LEAVECNANNEL_BROADCAST, *LPPMSG_DUEL_LEAVECNANNEL_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nCount;
    struct
    {
        char szID[MAX_USERNAME_SIZE];
    } user[10];
} PMSG_DUEL_OBSERVERLIST_BROADCAST, *LPPMSG_DUEL_OBSERVERLIST_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    char szWinner[MAX_USERNAME_SIZE];
    char szLoser[MAX_USERNAME_SIZE];
} PMSG_DUEL_RESULT_BROADCAST, *LPPMSG_DUEL_RESULT_BROADCAST;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE SubCode;
    BYTE nFlag;
} PMSG_DUEL_ROUNDSTART_BROADCAST, *LPPMSG_DUEL_ROUNDSTART_BROADCAST;

typedef struct tagPSHOPTITLE_HEADER
{
    PWMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byCount;
} PSHOPTITLE_HEADERINFO, *LPPSHOPTITLE_HEADERINFO;
typedef struct tagPSHOPTITLE_DATA
{
    BYTE byIndexH;
    BYTE byIndexL;
    char szTitle[MAX_SHOPTITLE]; //. MAX_SHOPTITLE
} PSHOPTITLE_DATAINFO, *LPPSHOPTITLE_DATAINFO;
typedef struct tagPSHOPTITLE_CHANGE
{
    PBMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byIndexH;
    BYTE byIndexL;
    char szTitle[MAX_SHOPTITLE]; //. MAX_SHOPTITLE
    char szId[MAX_USERNAME_SIZE];
} PSHOPTITLE_CHANGEINFO, *LPPSHOPTITLE_CHANGEINFO;

typedef struct tagPSHOPSETPRICE_RESULT
{
    PBMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byResult;
    BYTE byItemPos;
} PSHOPSETPRICE_RESULTINFO, *LPPSHOPSETPRICE_RESULTINFO;

typedef struct tagCREATEPSHOP_RESULT
{
    PBMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byResult;
} CREATEPSHOP_RESULTINFO, *LPCREATEPSHOP_RESULSTINFO;
typedef struct tagDESTROYPSHOP_RESULT
{
    PBMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byResult;
    BYTE byIndexH;
    BYTE byIndexL;
} DESTROYPSHOP_RESULTINFO, *LPDESTROYPSHOP_RESULTINFO;

enum ShopItemListResult : BYTE
{
    Success = 0x01,

    Fail1 = 0x03,
    Fail2 = 0x04,
};

typedef struct tagGETPSHOPITEMLIST_HEADER
{
    PWMSG_HEADER Header;
    BYTE bySubcode;
    ShopItemListResult byResult;
    BYTE byIndexH;
    BYTE byIndexL;
    char szId[MAX_USERNAME_SIZE];
    char szShopTitle[MAX_SHOPTITLE]; //. MAX_SHOPTITLE
    BYTE ItemCount;
} GETPSHOPITEMLIST_HEADERINFO, *LPGETPSHOPITEMLIST_HEADERINFO;

#pragma pack(push, 1) // just to get the actual length by sizeof...
typedef struct tagGETPSHOPITEM_DATA
{
    INT MoneyPrice;
    WORD PriceItemType;      // not yet used
    WORD RequiredItemAmount; // not yet used
    BYTE ItemSlot;
    BYTE Item[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} GETPSHOPITEM_DATAINFO, *LPGETPSHOPITEM_DATAINFO;
#pragma pack(pop)

typedef struct tagCLOSEPSHOP_RESULT
{
    PWMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byIndexH;
    BYTE byIndexL;
} CLOSEPSHOP_RESULTINFO, *LPCLOSEPSHOP_RESULTINFO;

typedef struct tagPURCHASEITEM_RESULT
{
    enum ItemBuyResult : BYTE
    {
        Undefined = 0,
        BoughtSuccessfully = 1,
        NotAvailable = 2,
        ShopNotOpened = 3,
        InTransaction = 4,
        InvalidShopSlot = 5,
        NameMismatchOrPriceMissing = 6,
        LackOfMoney = 7,
        MoneyOverflowOrNotEnoughSpace = 8,
        ItemBlock = 9,
    };

    PBMSG_HEADER Header;
    BYTE bySubcode;
    WORD SellerId;
    ItemBuyResult Result;
    BYTE ItemSlot;
    //BYTE			ItemData[PACKET_ITEM_LENGTH_EXTENDED_MIN];
} PURCHASEITEM_RESULTINFO, *LPPURCHASEITEM_RESULTINFO;
typedef struct tagSOLDITEM_RESULT
{
    PBMSG_HEADER Header;
    BYTE bySubcode;
    BYTE byPos;
    char szId[MAX_USERNAME_SIZE];
} SOLDITEM_RESULTINFO, *LPSOLDITEM_RESULTINFO;

typedef struct tagDISPLAYEFFECT_NOTIFY
{
    PBMSG_HEADER Header;
    BYTE byIndexH;
    BYTE byIndexL;
    BYTE byType;
} DISPLAYEREFFECT_NOTIFYINFO, *LPDISPLAYEREFFECT_NOTIFYINFO;

typedef struct
{
    PWMSG_HEADER Header;
    BYTE MemoCount;
    BYTE MaxMemo;
    BYTE Count;
} FS_FRIEND_LIST_HEADER, *LPFS_FRIEND_LIST_HEADER;

typedef struct
{
    char Name[MAX_USERNAME_SIZE];
    BYTE Server;
} FS_FRIEND_LIST_DATA, *LPFS_FRIEND_LIST_DATA;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    char Name[MAX_USERNAME_SIZE];
    BYTE Server;
} FS_FRIEND_RESULT, *LPFS_FRIEND_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    char Name[MAX_USERNAME_SIZE];
} FS_ACCEPT_ADD_FRIEND_RESULT, *LPFS_ACCEPT_ADD_FRIEND_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    char Name[MAX_USERNAME_SIZE];
    BYTE Server;
} FS_FRIEND_STATE_CHANGE, *LPFS_FRIEND_STATE_CHANGE;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    DWORD WindowGuid;
} FS_SEND_LETTER_RESULT, *LPFS_SEND_LETTER_RESULT;

typedef struct tagFS_LETTER_ALERT
{
    PBMSG_HEADER Header;
    WORD Index;
    char Name[MAX_USERNAME_SIZE];
    char Date[MAX_LETTER_DATE_LENGTH];
    char Separator;
    char Time[MAX_LETTER_TIME_LENGTH];
    char Reserved[30 - MAX_LETTER_DATE_LENGTH - MAX_LETTER_TIME_LENGTH - 1];
    // AddLetter wire field; the editable UI title capacity is a different limit.
    static constexpr std::size_t SubjectLength = 32;
    char Subject[SubjectLength];
    BYTE Read;
} FS_LETTER_ALERT, *LPFS_LETTER_ALERT;

typedef struct
{
    PWMSG_HEADER Header;
    WORD Index;
    SERVER_CLASS_TYPE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE Reserved[40 - EQUIPMENT_LENGTH_EXTENDED];
    BYTE PhotoDir;
    BYTE PhotoAction;
} FS_LETTER_TEXT_HEADER, *LPFS_LETTER_TEXT_HEADER;

typedef struct
{
    PWMSG_HEADER Header;
    WORD Index;
    SERVER_CLASS_TYPE Class;
    BYTE Flags;
    BYTE Equipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE Reserved[40 - EQUIPMENT_LENGTH_EXTENDED];
    BYTE PhotoDir;
    BYTE PhotoAction;
    char Memo[MAX_LETTERTEXT_LENGTH];
} FS_LETTER_TEXT, *LPFS_LETTER_TEXT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    WORD Index;
} FS_LETTER_RESULT, *LPFS_LETTER_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    char IP[15];
    WORD RoomNumber;
    DWORD Ticket;
    BYTE Type;
    char ID[10];
    BYTE Result;
} FS_CHAT_CREATE_RESULT, *LPFS_CHAT_CREATE_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
} FS_CHAT_JOIN_RESULT, *LPFS_CHAT_JOIN_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Type;
    BYTE Index;
    char Name[MAX_USERNAME_SIZE];
} FS_CHAT_CHANGE_STATE, *LPFS_CHAT_CHANGE_STATE;

typedef struct
{
    PWMSG_HEADER Header;
    WORD RoomNumber;
    BYTE Count;
} FS_CHAT_USERLIST_HEADER, *LPFS_CHAT_USERLIST_HEADER;

typedef struct
{
    BYTE Index;
    char Name[MAX_USERNAME_SIZE];
} FS_CHAT_USERLIST_DATA, *LPFS_CHAT_USERLIST_DATA;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Result;
    DWORD WindowGuid;
} FS_CHAT_INVITE_RESULT, *LPFS_CHAT_INVITE_RESULT;

typedef struct
{
    PBMSG_HEADER Header;
    BYTE Index;
    BYTE MsgSize;
    char Msg[100];
} FS_CHAT_TEXT, *LPFS_CHAT_TEXT;

// GC [0x2D]
typedef struct
{
    PBMSG_HEADER h;

    WORD wOptionType;
    WORD wEffectType;
    BYTE byEffectOption;
    int wEffectTime;
    BYTE byBuffType;
} PMSG_ITEMEFFECTCANCEL, *LPPMSG_ITEMEFFECTCANCEL;

typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_byPetType;
    BYTE m_byCommand;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
} PRECEIVE_PET_COMMAND, *LPPRECEIVE_PET_COMMAND;

typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_byPetType;
    BYTE m_bySkillType;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
    BYTE m_byTKeyH;
    BYTE m_byTKeyL;
} PRECEIVE_PET_ATTACK, *LPPRECEIVE_PET_ATTACK;

typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_byPetType;
    BYTE m_byInvType;
    BYTE m_byPos;
    BYTE m_byLevel;
    int m_iExp;
    BYTE m_byLife;
} PRECEIVE_PET_INFO, *LPPRECEIVE_PET_INFO;

typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_subCode;
    BYTE m_x;
    BYTE m_y;
} PMSG_SOCCER_GOALIN, *LPPMSG_SOCCER_GOALIN;

typedef struct
{
    PBMSG_HEADER m_Header;
    MServerInfo m_vSvrInfo;
} PHEADER_MAP_CHANGESERVER_INFO, *LPPHEADER_MAP_CHANGESERVER_INFO;

enum CASTLESIEGE_STATE
{
    CASTLESIEGE_STATE_NONE = -1,
    CASTLESIEGE_STATE_IDLE_1 = 0,
    CASTLESIEGE_STATE_REGSIEGE = 1,
    CASTLESIEGE_STATE_IDLE_2 = 2,
    CASTLESIEGE_STATE_REGMARK = 3,
    CASTLESIEGE_STATE_IDLE_3 = 4,
    CASTLESIEGE_STATE_NOTIFY = 5,
    CASTLESIEGE_STATE_READYSIEGE = 6,
    CASTLESIEGE_STATE_STARTSIEGE = 7,
    CASTLESIEGE_STATE_ENDSIEGE = 8,
    CASTLESIEGE_STATE_ENDCYCLE = 9,
};

// GC [0xB2][0x00]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    CHAR cCastleSiegeState;
    BYTE btStartYearH;
    BYTE btStartYearL;
    BYTE btStartMonth;
    BYTE btStartDay;
    BYTE btStartHour;
    BYTE btStartMinute;
    BYTE btEndYearH;
    BYTE btEndYearL;
    BYTE btEndMonth;
    BYTE btEndDay;
    BYTE btEndHour;
    BYTE btEndMinute;
    BYTE btSiegeStartYearH;
    BYTE btSiegeStartYearL;
    BYTE btSiegeStartMonth;
    BYTE btSiegeStartDay;
    BYTE btSiegeStartHour;
    BYTE btSiegeStartMinute;
    char cOwnerGuild[MAX_GUILDNAME];
    char cOwnerGuildMaster[MAX_USERNAME_SIZE];

    CHAR btStateLeftSec1;
    CHAR btStateLeftSec2;
    CHAR btStateLeftSec3;
    CHAR btStateLeftSec4;
} PMSG_ANS_CASTLESIEGESTATE, *LPPMSG_ANS_CASTLESIEGESTATE;

// GC [0xB2][0x01]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    char szGuildName[MAX_GUILDNAME];
} PMSG_ANS_REGCASTLESIEGE, *LPPMSG_ANS_REGCASTLESIEGE;

// GC [0xB2][0x02]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    BYTE btIsGiveUp;
    char szGuildName[MAX_GUILDNAME];
} PMSG_ANS_GIVEUPCASTLESIEGE, *LPPMSG_ANS_GIVEUPCASTLESIEGE;

// GC [0xB2][0x03]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    char szGuildName[MAX_GUILDNAME];
    BYTE btGuildMark1;
    BYTE btGuildMark2;
    BYTE btGuildMark3;
    BYTE btGuildMark4;
    BYTE btIsGiveUp;
    BYTE btRegRank;
} PMSG_ANS_GUILDREGINFO, *LPPMSG_ANS_GUILDREGINFO;

// GC [0xB2][0x04]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    char szGuildName[MAX_GUILDNAME];
    BYTE btGuildMark1;
    BYTE btGuildMark2;
    BYTE btGuildMark3;
    BYTE btGuildMark4;
} PMSG_ANS_REGGUILDMARK, *LPPMSG_ANS_REGGUILDMARK;

// GC [0xB2][0x05]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    INT iNpcNumber;
    INT iNpcIndex;
} PMSG_ANS_NPCBUY, *LPPMSG_ANS_NPCBUY;

// GC [0xB2][0x06]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    INT iNpcNumber;
    INT iNpcIndex;
    INT iNpcHP;
    INT iNpcMaxHP;
} PMSG_ANS_NPCREPAIR, *LPPMSG_ANS_NPCREPAIR;

// GC [0xB2][0x07]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    INT iNpcNumber;
    INT iNpcIndex;
    INT iNpcUpType;
    INT iNpcUpValue;
} PMSG_ANS_NPCUPGRADE, *LPPMSG_ANS_NPCUPGRADE;

// CG [0xB2][0x08]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    BYTE btTaxRateChaos;
    BYTE btTaxRateStore;
    BYTE btMoney1;
    BYTE btMoney2;
    BYTE btMoney3;
    BYTE btMoney4;
    BYTE btMoney5;
    BYTE btMoney6;
    BYTE btMoney7;
    BYTE btMoney8;
} PMSG_ANS_TAXMONEYINFO, *LPPMSG_ANS_TAXMONEYINFO;

// CG [0xB2][0x09]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    BYTE btTaxType;
    BYTE btTaxRate1;
    BYTE btTaxRate2;
    BYTE btTaxRate3;
    BYTE btTaxRate4;
} PMSG_ANS_TAXRATECHANGE, *LPPMSG_ANS_TAXRATECHANGE;

// CG [0xB2][0x10]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btResult;
    BYTE btMoney1;
    BYTE btMoney2;
    BYTE btMoney3;
    BYTE btMoney4;
    BYTE btMoney5;
    BYTE btMoney6;
    BYTE btMoney7;
    BYTE btMoney8;
} PMSG_ANS_MONEYDRAWOUT, *LPPMSG_ANS_MONEYDRAWOUT;

// CG [0xB2][0x1A]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE Header;
    BYTE btTaxType;
    BYTE btTaxRate;
} PMSG_ANS_MAPSVRTAXINFO, *LPPMSG_ANS_MAPSVRTAXINFO;

// GC [0xB3]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE m_Header;
    BYTE btResult;
    INT iCount;
} PMSG_ANS_NPCDBLIST, *LPPMSG_ANS_NPCDBLIST;
typedef struct
{
    INT iNpcNumber;
    INT iNpcIndex;
    INT iNpcDfLevel;
    INT iNpcRgLevel;
    INT iNpcMaxHp;
    INT iNpcHp;
    BYTE btNpcX;
    BYTE btNpcY;
    BYTE btNpcLive;
} PMSG_NPCDBLIST, *LPPMSG_NPCDBLIST;

// GC [0xB4]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE h;
    BYTE btResult;
    INT iCount;
} PMSG_ANS_CSREGGUILDLIST, *LPPMSG_ANS_CSREGGUILDLIST;
typedef struct
{
    char szGuildName[MAX_GUILDNAME];
    BYTE btRegMarks1;
    BYTE btRegMarks2;
    BYTE btRegMarks3;
    BYTE btRegMarks4;
    BYTE btIsGiveUp;
    BYTE btSeqNum;
} PMSG_CSREGGUILDLIST, *LPPMSG_CSREGGUILDLIST;

// GC [0xB5]
typedef struct
{
    PREQUEST_DEFAULT_SUBCODE h;
    BYTE btResult;
    INT iCount;
} PMSG_ANS_CSATTKGUILDLIST, *LPPMSG_ANS_CSATTKGUILDLIST;
typedef struct
{
    BYTE btCsJoinSide;
    BYTE btGuildInvolved;
    char szGuildName[8];
    INT iGuildScore;
} PMSG_CSATTKGUILDLIST, *LPPMSG_CSATTKGUILDLIST;

typedef struct
{
    PBMSG_HEADER m_Header;
    BYTE m_bySubCode;
} PBMSG_HEADER2;

// GC [0xA4][0x00]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE byResult;
    BYTE byQuestIndex;
    int anKillCountInfo[10];
} PMSG_ANS_QUEST_MONKILL_INFO, *LPPMSG_ANS_QUEST_MONKILL_INFO;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
    BYTE m_byHuntZoneEnter;
} PMSG_CSHUNTZONEENTER, *LPPMSG_CSHUNTZONEENTER;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
    BYTE m_byEnable;
    INT m_iCurrPrice;
    INT m_iMaxPrice;
    INT m_iUnitPrice;
} PRECEIVE_CASTLE_HUNTZONE_INFO, *LPPRECEIVE_CASTLE_HUNTZONE_INFO;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
} PRECEVIE_CASTLE_HUNTZONE_RESULT, *LPPRECEVIE_CASTLE_HUNTZONE_RESULT;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byGuildMark[32];
} PRECEIVE_CASTLE_FLAG, *LPPRECEIVE_CASTLE_FLAG;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
} PRECEIVE_GATE_STATE, *LPPRECEIVE_GATE_STATE;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
    BYTE m_byOperator;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
} PRECEIVE_GATE_OPERATOR, *LPPRECEIVE_GATE_OPERATOR;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byOperator;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
} PRECEIVE_GATE_CURRENT_STATE, *LPPRECEIVE_GATE_CURRENT_STATE;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byIndexH;
    BYTE m_byIndexL;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
    BYTE m_byState;
} PRECEIVE_SWITCH_PROC, *LPPRECEIVE_SWITCH_PROC;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byCrownState;

    DWORD m_dwCrownAccessTime;
} PRECEIVE_CROWN_STATE, *LPPRECEIVE_CROWN_STATE;

typedef struct tagPRECEIVE_CROWN_SWITCH_INFO
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byIndex1;
    BYTE m_byIndex2;
    BYTE m_bySwitchState;
    BYTE m_JoinSide;

    char m_szGuildName[8];
    char m_szUserName[MAX_USERNAME_SIZE + 1];
} PRECEIVE_CROWN_SWITCH_INFO, *LPRECEIVE_CROWN_SWITCH_INFO;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byBasttleCastleState;
    char m_szGuildName[8];
} PRECEIVE_BC_PROCESS, *LPPRECEIVE_BC_PROCESS;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byKeyH;
    BYTE m_byKeyL;

    BYTE m_byBuildTime;
} PRECEIVE_MONSTER_BUILD_TIME, *LPPRECEIVE_MONSTER_BUILD_TIME;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;

    BYTE m_byWeaponType;

    BYTE m_byKeyH;
    BYTE m_byKeyL;
} PRECEIVE_CATAPULT_STATE, *LPPRECEIVE_CATAPULT_STATE;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
    BYTE m_byKeyH;
    BYTE m_byKeyL;

    BYTE m_byWeaponType;

    BYTE m_byTargetX;
    BYTE m_byTargetY;
} PRECEIVE_WEAPON_FIRE, *LPPRECEIVE_WEAPON_FIRE;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byWeaponType;

    BYTE m_byTargetX;
    BYTE m_byTargetY;
} PRECEIVE_BOMBING_ALERT, *LPPRECEIVE_BOMBING_ALERT;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byKeyH;
    BYTE m_byKeyL;

    BYTE m_byWeaponType;
} PRECEIVE_BOMBING_TARGET, *LPPRECEIVE_BOMBING_TARGET;

typedef struct
{
    BYTE m_byObjType;
    BYTE m_byTypeH;
    BYTE m_byTypeL;
    BYTE m_byKeyH;
    BYTE m_byKeyL;
    BYTE m_byPosX;
    BYTE m_byPosY;
    BYTE Class;
    BYTE Flags;
    BYTE m_byEquipment[EQUIPMENT_LENGTH_EXTENDED];
    BYTE s_BuffCount;
    BYTE s_BuffEffectState[MAX_BUFF_SLOT_INDEX];
} PRECEIVE_PREVIEW_PORT_EXTENDED, *LPPRECEIVE_PREVIEW_PORT_EXTENDED;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byTeam;
    BYTE m_byX;
    BYTE m_byY;
    BYTE m_byCmd;
} PRECEIVE_GUILD_COMMAND, *LPPRECEIVE_GUILD_COMMAND;

typedef struct
{
    BYTE m_byX;
    BYTE m_byY;
} PRECEIVE_MEMBER_LOCATION, *LPPRECEIVE_MEMBER_LOCATION;

typedef struct
{
    BYTE m_byType;
    BYTE m_byX;
    BYTE m_byY;
} PRECEIVE_NPC_LOCATION, *LPPRECEIVE_NPC_LOCATION;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byResult;
} PRECEIVE_MAP_INFO_RESULT, *LPPRECEIVE_MAP_INFO_RESULT;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_byHour;
    BYTE m_byMinute;
} PRECEIVE_MATCH_TIMER, *LPPRECEIVE_MATCH_TIMER;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_iJewelType;
    BYTE m_iJewelMix;
} PMSG_REQ_JEWEL_MIX, *LPPMSG_REQ_JEWEL_MIX;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_iResult;
} PMSG_ANS_JEWEL_MIX, *LPPMSG_ANS_JEWEL_MIX;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_iJewelType;
    BYTE m_iJewelLevel;
    BYTE m_iJewelPos;
} PMSG_REQ_JEWEL_UNMIX, *LPPMSG_REQ_JEWEL_UNMIX;

typedef struct
{
    PBMSG_HEADER2 m_Header;

    int iMasterLevelSkill;
} PMSG_REQ_MASTERLEVEL_SKILL, *LPPMSG_REQ_MASTERLEVEL_SKILL;

typedef struct
{
    PBMSG_HEADER2 m_Header;
    BYTE m_iResult;
} PMSG_ANS_JEWEL_UNMIX, *LPPMSG_ANS_JEWEL_UNMIX;

// GC [0xBD][0x00]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btOccupationState;
    BYTE btCrywolfState;
} PMSG_ANS_CRYWOLF_INFO, *LPPMSG_ANS_CRYWOLF_INFO;

// GC [0xBD][0x02] containing Crywolf shield and altar status data
typedef struct
{
    PBMSG_HEADER2 h;
    INT iCrywolfStatueHP;
    BYTE btAltarState1;
    BYTE btAltarState2;
    BYTE btAltarState3;
    BYTE btAltarState4;
    BYTE btAltarState5;
} PMSG_ANS_CRYWOLF_STATE_ALTAR_INFO, *LPPMSG_ANS_CRYWOLF_STATE_ALTAR_INFO;

// GC [0xBD][0x03]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE bResult;
    BYTE btAltarState;
    BYTE btObjIndexH;
    BYTE btObjIndexL;
} PMSG_ANS_CRYWOLF_ALTAR_CONTRACT, *LPPPMSG_ANS_CRYWOLF_ALTAR_CONTRACT;

// GC [0xBD][0x04]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btHour;
    BYTE btMinute;
} PMSG_ANS_CRYWOLF_LEFTTIME, *LPPPMSG_ANS_CRYWOLF_LEFTTIME;

// GC [0xBD][0x0C]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btObjClassH;
    BYTE btObjClassL;

    BYTE btSourceX;
    BYTE btSourceY;

    BYTE btPointX;
    BYTE btPointY;
} PMSG_NOTIFY_REGION_MONSTER_ATTACK, *LPPMSG_NOTIFY_REGION_MONSTER_ATTACK;

typedef struct
{
    PBMSG_HEADER2 h;
    int btBossHP;
    BYTE btMonster2;
} PMSG_ANS_CRYWOLF_BOSSMONSTER_INFO, *LPPMSG_ANS_CRYWOLF_BOSSMONSTER_INFO;

typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btPlusChaosRate;
} PMSG_ANS_CRYWOLF_BENEFIT_PLUS_CHAOSRATE, *LPPMSG_ANS_CRYWOLF_BENEFIT_PLUS_CHAOSRATE;

typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btRank; //0 : D    1 : C   2 : B   3 : A   4 : S
    int iGettingExp;
} PMSG_ANS_CRYWOLF_PERSONAL_RANK, *LPPMSG_ANS_CRYWOLF_PERSONAL_RANK;

typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btCount;
} PMSG_ANS_CRYWOLF_HERO_LIST_INFO_COUNT, *LPPMSG_ANS_CRYWOLF_HERO_LIST_INFO_COUNT;

typedef struct
{
    BYTE iRank;
    char szHeroName[MAX_USERNAME_SIZE];
    int iHeroScore;
    SERVER_CLASS_TYPE btHeroClass;
} PMSG_ANS_CRYWOLF_HERO_LIST_INFO, *LPPMSG_ANS_CRYWOLF_HERO_LIST_INFO;

// CG [0xD1][0x00]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btState;
    BYTE btDetailState;
    BYTE btEnter;
    BYTE btUserCount;
    int iRemainTime;
} PMSG_ANS_KANTURU_STATE_INFO, *LPPMSG_ANS_KANTURU_STATE_INFO;

// GC [0xD1][0x01]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
} PMSG_ANS_ENTER_KANTURU_BOSS_MAP, *LPPMSG_ANS_ENTER_KANTURU_BOSS_MAP;

// GC [0xD1][0x02]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btCurrentState;
    BYTE btCurrentDetailState;
} PMSG_ANS_KANTURU_CURRENT_STATE, *LPPMSG_ANS_KANTURU_CURRENT_STATE;

// GC [0xD1][0x03]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btState;
    BYTE btDetailState;
} PMSG_ANS_KANTURU_STATE_CHANGE, *LPPMSG_ANS_KANTURU_STATE_CHANGE;

// GC [0xD1][0x04]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btResult;
} PMSG_ANS_KANTURU_BATTLE_RESULT, *LPPMSG_ANS_KANTURU_BATTLE_RESULT;

// GC [0xD1][0x05]
typedef struct
{
    PBMSG_HEADER2 h;

    int btTimeLimit;
} PMSG_ANS_KANTURU_BATTLE_SCENE_TIMELIMIT, *LPPMSG_ANS_KANTURU_BATTLE_SCENE_TIMELIMIT;

// GC [0xD1][0x06]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btObjClassH;
    BYTE btObjClassL;

    BYTE btType;
} PMSG_NOTIFY_KANTURU_WIDE_AREA_ATTACK, *LPPMSG_NOTIFY_KANTURU_WIDE_AREA_ATTACK;

// GC [0xD1][0x07]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE bMonsterCount;
    BYTE btUserCount;
} PMSG_NOTIFY_KANTURU_USER_MONSTER_COUNT, *LPPMSG_NOTIFY_KANTURU_USER_MONSTER_COUNT;

// CG [0xBF][0x00]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btCursedTempleIdx;
    BYTE iItemPos;
} PMSG_REQ_ENTER_CURSED_TEMPLE, *LPPMSG_REQ_ENTER_CURSED_TEMPLE;

// GC [0xBF][0x00]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE Result;
} PMSG_RESULT_ENTER_CURSED_TEMPLE, *LPPMSG_RESULT_ENTER_CURSED_TEMPLE;

// GC [0xBF][0x01]
typedef struct
{
    PBMSG_HEADER2 h;
    WORD wRemainSec;
    WORD btUserIndex;
    BYTE btX;
    BYTE btY;
    BYTE btAlliedPoint;
    BYTE btIllusionPoint;
    BYTE btMyTeam;

    BYTE btPartyCount;
} PMSG_CURSED_TAMPLE_STATE, *LPPMSG_CURSED_TAMPLE_STATE;

typedef struct
{
    WORD wPartyUserIndex;
    BYTE byMapNumber;
    BYTE btX;
    BYTE btY;
} PMSG_CURSED_TAMPLE_PARTY_POS, *LPPMSG_CURSED_TAMPLE_PARTY_POS;

// CG [0xBF][0x02]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE MagicH;
    BYTE MagicL;
    WORD wTargetObjIndex;
    BYTE Dis;
} PMSG_CURSED_TEMPLE_USE_MAGIC, *LPPMSG_CURSED_TEMPLE_USE_MAGIC;

// GC [0xBF][0x03]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btUserCount[6];
} PMSG_CURSED_TEMPLE_USER_COUNT, *LPPMSG_CURSED_TEMPLE_USER_COUNT;

// GC [0xBF][0x02]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE MagicResult;
    BYTE MagicH;
    BYTE MagicL;
    WORD wSourceObjIndex;
    WORD wTargetObjIndex;
} PMSG_CURSED_TEMPLE_USE_MAGIC_RESULT, *LPPMSG_CURSED_TEMPLE_USE_MAGIC_RESULT;

// GC [0xBF][0x04]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btAlliedPoint;
    BYTE btIllusionPoint;

    BYTE btUserCount;
} PMSG_CURSED_TEMPLE_RESULT, *LPPMSG_CURSED_TEMPLE_RESULT;

typedef struct
{
    char GameId[MAX_USERNAME_SIZE];
    BYTE byMapNumber;
    BYTE btTeam;
    SERVER_CLASS_TYPE btClass;
    int nAddExp;
} PMSG_CURSED_TEMPLE_USER_ADD_EXP, *LPPMSG_CURSED_TEMPLE_USER_ADD_EXP;

// GC [0xBF][0x06]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btSkillPoint;
} PMSG_CURSED_TEMPLE_SKILL_POINT, *LPPMSG_CURSED_TEMPLE_SKILL_POINT;

// GC [0xBF][0x07]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE MagicH;
    BYTE MagicL;

    WORD wObjIndex;
} PMSG_CURSED_TEMPLE_SKILL_END, *LPPMSG_CURSED_TEMPLE_SKILL_END;

// GC [0xBF][0x08]
typedef struct
{
    PBMSG_HEADER2 h;
    WORD wUserIndex;
    char Name[MAX_USERNAME_SIZE];
} PMSG_RELICS_GET_USER, *LPPMSG_RELICS_GET_USER;

// GC [0xBF][0x09]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btTempleNumber;
    BYTE btIllusionTempleState; // 0: wait, 1: wait->ready, 2: ready->play, 3: play->end,
} PMSG_ILLUSION_TEMPLE_STATE, *LPPMSG_ILLUSION_TEMPLE_STATE;

// GC [0xBF][0x0a]
typedef struct
{
    PBMSG_HEADER2 Header;

    BYTE MagicH;
    BYTE MagicL;

    WORD wUserIndex;
    BYTE byCount;
} PRECEIVE_CHAIN_MAGIC, *LPPRECEIVE_CHAIN_MAGIC;

typedef struct
{
    WORD wTargetIndex;
} PRECEIVE_CHAIN_MAGIC_OBJECT, *LPPRECEIVE_CHAIN_MAGIC_OBJECT;

typedef struct
{
    PBMSG_HEADER h;
    BYTE subcode;
    short nMLevel;
    BYTE btMExp1;
    BYTE btMExp2;
    BYTE btMExp3;
    BYTE btMExp4;
    BYTE btMExp5;
    BYTE btMExp6;
    BYTE btMExp7;
    BYTE btMExp8;

    BYTE btMNextExp1;
    BYTE btMNextExp2;
    BYTE btMNextExp3;
    BYTE btMNextExp4;
    BYTE btMNextExp5;
    BYTE btMNextExp6;
    BYTE btMNextExp7;
    BYTE btMNextExp8;
    short nMLPoint;
    WORD wMaxLife;
    WORD wMaxMana;
    WORD wMaxShield;
    WORD wMaxSkillMana;
} PMSG_MASTERLEVEL_INFO, *LPPMSG_MASTERLEVEL_INFO;

typedef struct
{
    PBMSG_HEADER h;
    BYTE subcode;
    short nMLevel;
    BYTE btMExp1;
    BYTE btMExp2;
    BYTE btMExp3;
    BYTE btMExp4;
    BYTE btMExp5;
    BYTE btMExp6;
    BYTE btMExp7;
    BYTE btMExp8;

    BYTE btMNextExp1;
    BYTE btMNextExp2;
    BYTE btMNextExp3;
    BYTE btMNextExp4;
    BYTE btMNextExp5;
    BYTE btMNextExp6;
    BYTE btMNextExp7;
    BYTE btMNextExp8;
    short nMLPoint;
    DWORD wMaxLife;
    DWORD wMaxMana;
    DWORD wMaxShield;
    DWORD wMaxSkillMana;
} PMSG_MASTERLEVEL_INFO_EXTENDED, *LPPMSG_MASTERLEVEL_INFO_EXTENDED;

typedef struct
{
    PBMSG_HEADER h;
    BYTE subcode;
    short nMLevel;
    short nAddMPoint;
    short nMLevelUpMPoint;
    short nMaxPoint;
    WORD wMaxLife;
    WORD wMaxMana;
    WORD wMaxShield;
    WORD wMaxBP;
} PMSG_MASTERLEVEL_UP, *LPPMSG_MASTERLEVEL_UP;

typedef struct
{
    PBMSG_HEADER h;
    BYTE subcode;
    short nMLevel;
    short nAddMPoint;
    short nMLevelUpMPoint;
    short nMaxPoint;
    DWORD wMaxLife;
    DWORD wMaxMana;
    DWORD wMaxShield;
    DWORD wMaxBP;
} PMSG_MASTERLEVEL_UP_EXTENDED, *LPPMSG_MASTERLEVEL_UP_EXTENDED;

typedef struct
{
    PBMSG_HEADER h;
    BYTE subcode;
    short Result;
    short MasterLevelUpPoints;
    int SkillIndex;
    int SkillNumber;
    int SkillLevel;
    float DisplayValue;
    float DisplayValueOfNextLevel;
} PMSG_ANS_MASTERLEVEL_SKILL, *LPPMSG_ANS_MASTERLEVEL_SKILL;

// GC [0xD1][0x10]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btState;
    BYTE btDetailState;

    BYTE btEnter;
    int iRemainTime;
} PMSG_ANS_RAKLION_STATE_INFO, *LPPMSG_ANS_RAKLION_STATE_INFO;

// GC [0xD1][0x11]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btCurrentState;
    BYTE btCurrentDetailState;
} PMSG_ANS_RAKLION_CURRENT_STATE, *LPPMSG_ANS_RAKLION_CURRENT_STATE;

// GC [0xD1][0x12]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btState;
    BYTE btDetailState;
} PMSG_ANS_RAKLION_STATE_CHANGE, *LPPMSG_ANS_RAKLION_STATE_CHANGE;

// GC [0xD1][0x13]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btResult;
} PMSG_ANS_RAKLION_BATTLE_RESULT, *LPPMSG_ANS_RAKLION_BATTLE_RESULT;

// GC [0xD1][0x14]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btObjClassH;
    BYTE btObjClassL;

    BYTE btType;
} PMSG_NOTIFY_RAKLION_WIDE_AREA_ATTACK, *LPPMSG_NOTIFY_RAKLION_WIDE_AREA_ATTACK;

// GC [0xD1][0x15]
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btMonsterCount;
    BYTE btUserCount;
} PMSG_NOTIFY_RAKLION_USER_MONSTER_COUNT, *LPPMSG_NOTIFY_RAKLION_USER_MONSTER_COUNT;

// CG[0xBF][0x0b]
typedef struct
{
    PBMSG_HEADER2 h;
    int nRegCoinCnt;
} PMSG_REQ_GET_COIN_COUNT, *LPPMSG_REQ_GET_COIN_COUNT;

// GC[0xBF][0x0b]
typedef struct
{
    PBMSG_HEADER2 h;
    int nCoinCnt;
} PMSG_ANS_GET_COIN_COUNT, *LPPMSG_ANS_GET_COIN_COUNT;

// CG[0xBF][0x0c]
typedef struct
{
    PBMSG_HEADER2 h;
} PMSG_REQ_REGEIST_COIN, *LPPMSG_REQ_REGEIST_COIN;

// GC0xBF][0x0c]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
    int nCurCoinCnt;
} PMSG_ANS_REGEIST_COIN, *LPPMSG_ANS_REGEIST_COIN;

// CG[0xBF][0x0d]
typedef struct
{
    PBMSG_HEADER2 h;
    int nCoinCnt;
} PMSG_REQ_TRADE_COIN, *LPPMSG_REG_TREADE_COIN;

// GC[0xBF][0x0d]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
} PMSG_ANS_TRADE_COIN, *LPPMSG_ANS_TREADE_COIN;

// GC [0xBF][0x0E]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
} PMSG_RESULT_ENTER_DOPPELGANGER, *LPPMSG_RESULT_ENTER_DOPPELGANGER;
// GC [0xBF][0x0F]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btPosIndex;
} PMSG_DOPPELGANGER_MONSTER_POSITION, *LPPMSG_DOPPELGANGER_MONSTER_POSITION;
// GC [0xBF][0x10]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btDoppelgangerState; // 0: wait, 1: wait->ready, 2: ready->play, 3: play->end,
} PMSG_DOPPELGANGER_STATE, *LPPMSG_DOPPELGANGER_STATE;
// GC [0xBF][0x11]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btIceworkerState;
    BYTE btPosIndex;
} PMSG_DOPPELGANGER_ICEWORKER_STATE, *LPPMSG_DOPPELGANGER_ICEWORKER_STATE;
// GC [0xBF][0x12]
typedef struct
{
    PBMSG_HEADER2 h;
    WORD wRemainSec;
    BYTE btUserCount;
    BYTE btDummy;
    BYTE UserPosData;
} PMSG_DOPPELGANGER_PLAY_INFO, *LPPMSG_DOPPELGANGER_PLAY_INFO;

typedef struct
{
    WORD wUserIndex;
    BYTE byMapNumber;
    BYTE btPosIndex;
} PMSG_DOPPELGANGER_USER_POS, *LPPMSG_DOPPELGANGER_USER_POS;
// GC [0xBF][0x13]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
    DWORD dwRewardExp;
} PMSG_DOPPELGANGER_RESULT, *LPPMSG_DOPPELGANGER_RESULT;
// GC [0xBF][0x14]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btMaxGoalCnt;
    BYTE btGoalCnt;
} PMSG_DOPPELGANGER_MONSTER_GOAL, *LPPMSG_DOPPELGANGER_MONSTER_GOAL;

#ifdef PBG_ADD_SECRETBUFF
// GC [0xBF][0x15]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btFatiguePercentage;
} PMSG_FATIGUEPERCENTAGE, *LPPMSG_FATIGUEPERCENTAGE;
#endif //PBG_ADD_SECRETBUFF

#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
// GC [0xBF][0x20]
typedef struct _tagPMSG_ANS_INVENTORY_EQUIPMENT_ITEM
{
    PBMSG_HEADER2 h;

    BYTE btItemPos;
    BYTE btResult;
} PMSG_ANS_INVENTORY_EQUIPMENT_ITEM, *LPPMSG_ANS_INVENTORY_EQUIPMENT_ITEM;
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY

// GC[0x8E][0x01]
typedef struct
{
    PBMSG_HEADER2 h;
    DWORD dwKeyValue;
} PMSG_MAPMOVE_CHECKSUM, *LPPMSG_MAPMOVE_CHECKSUM;

// GC[0x8E][0x03]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE btResult;
} PMSG_ANS_MAPMOVE, *LPPMSG_ANS_MAPMOVE;

// GC [0xF7][0x02]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE Result;
    BYTE Day;
    BYTE Zone;
    BYTE Wheather;
    DWORD RemainTick;
} PMSG_RESULT_ENTER_EMPIREGUARDIAN, *LPPMSG_RESULT_ENTER_EMPIREGUARDIAN;

// GC [0xF7][0x04]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE Type;
    DWORD RemainTick;
    BYTE MonsterCount;
} PMSG_REMAINTICK_EMPIREGUARDIAN, *LPPMSG_REMAINTICK_EMPIREGUARDIAN;

// GC [0xF7][0x06]
typedef struct
{
    PBMSG_HEADER2 h;
    BYTE Result;
    DWORD Exp;
} PMSG_CLEAR_RESULT_EMPIREGUARDIAN, *LPPMSG_CLEAR_RESULT_EMPIREGUARDIAN;

#ifdef KJH_ADD_INGAMESHOP_UI_SYSTEM

#pragma pack(push, 1)

// (0xD2)(0x01)
typedef struct
{
    PBMSG_HEADER2 h;
} PMSG_CASHSHOP_CASHPOINT_REQ, *LPPMSG_CASHSHOP_CASHPOINT_REQ;

// (0xD2)(0x01)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE btViewType;

    double dTotalCash;
    double dCashCredit;  // C (CreditCard)
    double dCashPrepaid; // P (PrepaidCard)
    double dTotalPoint;
    double dTotalMileage;
} PMSG_CASHSHOP_CASHPOINT_ANS, *LPPMSG_CASHSHOP_CASHPOINT_ANS;

// (0xD2)(0x02)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byShopOpenType;
} PMSG_CASHSHOP_SHOPOPEN_REQ, *LPPMSG_CASHSHOP_SHOPOPEN_REQ;

// (0xD2)(0x02)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byShopOpenResult;
} PMSG_CASHSHOP_SHOPOPEN_ANS, *LPPMSG_CASHSHOP_SHOPOPEN_ANS;

// (0xD2)(0x03)
typedef struct
{
    PBMSG_HEADER2 h;

    long lBuyItemPackageSeq;
    long lBuyItemDisplaySeq;
    long lBuyItemPriceSeq;
    WORD wItemCode;
} PMSG_CASHSHOP_BUYITEM_REQ, *LPPMSG_CASHSHOP_BUYITEM_REQ;

// Cash shop item purchase response (0xD2)(0x03)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byResultCode;
    long lItemLeftCount;
} PMSG_CASHSHOP_BUYITEM_ANS, *LPPMSG_CASHSHOP_BUYITEM_ANS;

// (0xD2)(0x04)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byResultCode;
    long lItemLeftCount;
    double dLimitedCash;
} PMSG_CASHSHOP_GIFTSEND_ANS, *LPPMSG_CASHSHOP_GIFTSEND_ANS;

// (0xD2)(0x05)
typedef struct
{
    PBMSG_HEADER2 h;
    int iPageIndex;
    char chStorageType;
} PMSG_CASHSHOP_STORAGELIST_REQ, *LPPMSG_CASHSHOP_STORAGELIST_REQ;

// (0xD2)(0x06)
typedef struct
{
    PBMSG_HEADER2 h;
    WORD wTotalItemCount;
    WORD wCurrentItemCount;
    WORD wPageIndex;
    WORD wTotalPage;
} PMSG_CASHSHOP_STORAGECOUNT, *LPPMSG_CASHSHOP_STORAGECOUNT;

// (0xD2)(0x0D)
typedef struct
{
    PBMSG_HEADER2 h;

    long lStorageIndex;
    long lItemSeq;
    long lStorageGroupCode;
    long lProductSeq;
    long lPriceSeq;
    double dCashPoint;
    char chItemType;
} PMSG_CASHSHOP_STORAGELIST, *LPPMSG_CASHSHOP_STORAGELIST;

// (0xD2)(0x0E)
typedef struct
{
    PBMSG_HEADER2 h;

    long lStorageIndex;
    long lItemSeq;
    long lStorageGroupCode;
    long lProductSeq;
    long lPriceSeq;
    double dCashPoint;
    char chItemType;

    char chSendUserName[MAX_USERNAME_SIZE + 1];
    char chMessage[MAX_GIFT_MESSAGE_SIZE];
} PMSG_CASHSHOP_GIFTSTORAGELIST, *LPPMSG_CASHSHOP_GIFTSTORAGELIST;

// (0xD2)(0x07)
typedef struct
{
    PBMSG_HEADER2 h;

    double dGiftCashLimit;
    BYTE byResultCode;
} PMSG_CASHSHOP_CASHSEND_ANS, *LPPMSG_CASHSHOP_CASHSEND_ANS;

// (0xD2)(0x08)
typedef struct
{
    PBMSG_HEADER2 h;
} PMSG_CASHSHOP_ITEMBUY_CONFIRM_REQ, *LPPMSG_CASHSHOP_ITEMBUY_CONFIRM_REQ;

// (0xD2)(0x08)
typedef struct
{
    PBMSG_HEADER2 h;

    double dPresentedCash;
    double dPresenteLimitCash;

    BYTE byResult;
    BYTE byItemBuyPossible;
    BYTE byPresendPossible;
} PMSG_CASHSHOP_ITEMBUY_CONFIRM_ANS, *LPPMSG_CASHSHOP_ITEMBUY_CONFIRM_ANS;

// (0xD2)(0x09)
typedef struct
{
    PBMSG_HEADER2 h;

    long lPackageSeq;
} PMSG_CASHSHOP_ITEMBUY_LEFT_COUNT_REQ, *LPPMSG_CASHSHOP_ITEMBUY_LEFT_COUNT_REQ;

// (0xD2)(0x09)
typedef struct
{
    PBMSG_HEADER2 h;

    long lPackageSeq;
    long lLeftCount;
} PMSG_CASHSHOP_ITEMBUY_LEFT_COUNT_ANS, *LPPMSG_CASHSHOP_ITEMBUY_LEFT_COUNT_ANS;

// (0xD2)(0x0A)
typedef struct
{
    PBMSG_HEADER2 h;

    long lStorageSeq;
    long lStorageItemSeq;
    char chStorageItemType;
} PMSG_CASHSHOP_STORAGE_ITEM_THROW_REQ, *LPPMSG_CASHSHOP_STORAGE_ITEM_THROW_REQ;

// (0xD2)(0x0A)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byResult;
} PMSG_CASHSHOP_STORAGE_ITEM_THROW_ANS, *LPPMSG_CASHSHOP_STORAGE_ITEM_THROW_ANS;

// (0xD2)(0x0B)
typedef struct
{
    PBMSG_HEADER2 h;

    long lStorageSeq;
    long lStorageItemSeq;

    WORD wItemCode;
    char chStorageItemType;
} PMSG_CASHSHOP_STORAGE_ITEM_USE_REQ, *LPPMSG_CASHSHOP_STORAGE_ITEM_USE_REQ;

// (0xD2)(0x0B)
typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byResult;
} PMSG_CASHSHOP_STORAGE_ITEM_USE_ANS, *LPPMSG_CASHSHOP_STORAGE_ITEM_USE_ANS;

// (0xD2)(0x0C)
typedef struct
{
    PBMSG_HEADER2 h;

    WORD wSaleZone;
    WORD wYear;
    WORD wYearIdentify;
} PMSG_CASHSHOP_VERSION_UPDATE, *LPPMSG_CASHSHOP_VERSION_UPDATE;

// (0xD2)(0x13)
typedef struct
{
    PBMSG_HEADER2 h;

    long lEventCategorySeq;
} PMSG_CASHSHOP_EVENTITEM_REQ, *LPPMSG_CASHSHOP_EVENTITEM_REQ;

// (0xD2)(0x13)
typedef struct
{
    PBMSG_HEADER2 h;

    WORD wEventItemListCount;
} PMSG_CASHSHOP_EVENTITEM_COUNT, *LPPMSG_CASHSHOP_EVENTITEM_COUNT;

typedef struct
{
    PBMSG_HEADER2 h;

    long lPackageSeq[INGAMESHOP_DISPLAY_ITEMLIST_SIZE];
} PMSG_CASHSHOP_EVENTITEM_LIST, *LPPMSG_CASHSHOP_EVENTITEM_LIST;

typedef struct
{
    PBMSG_HEADER2 h;

    WORD wBannerZone;
    WORD wYear;
    WORD wYearIdentify;
} PMSG_CASHSHOP_BANNER_UPDATE, *LPPMSG_CASHSHOP_BANNER_UPDATE;

#pragma pack(pop)

#endif // KJH_ADD_INGAMESHOP_UI_SYSTEM

#ifdef KJH_ADD_PERIOD_ITEM_SYSTEM

typedef struct
{
    PBMSG_HEADER2 h;

    BYTE byPeriodItemCount;
} PMSG_PERIODITEMEX_ITEMCOUNT, *LPPMSG_PERIODITEMEX_ITEMCOUNT;

typedef struct
{
    PBMSG_HEADER2 h;

    WORD wItemCode;
    WORD wItemSlotIndex;
    long lExpireDate;
} PMSG_PERIODITEMEX_ITEMLIST, *LPPMSG_PERIODITEMEX_ITEMLIST;

#endif // KJH_ADD_PERIOD_ITEM_SYSTEM

// 0x4A
typedef struct
{
    PBMSG_HEADER Header;
    BYTE AttackH;
    BYTE AttackL;
    BYTE SourceKeyH;
    BYTE SourceKeyL;
    BYTE TargetKeyH;
    BYTE TargetKeyL;
} PRECEIVE_STRAIGHTATTACK, *LPPRECEIVE_STRAIGHTATTACK;
// 0x4B
typedef struct
{
    PBMSG_HEADER Header;
    WORD MagicNumber;
    WORD TargerIndex[DARKSIDE_TARGET_MAX];
} PRECEIVE_DARKSIDE_INDEX, *LPPRECEIVE_DARKSIDE_INDEX;

// ?????????????????????????????????????
// Tears the live game session down to a clean login-scene state (matching the
// in-game logout path). Used by the auto-reconnect flow before it replays login.

void ReceiveMovePosition(const BYTE *ReceiveBuffer);

typedef struct _CROWN_SWITCH_INFO
{
    BYTE m_bySwitchState;
    BYTE m_JoinSide;
    wchar_t m_szGuildName[9];
    wchar_t m_szUserName[MAX_USERNAME_SIZE + 1];

    _CROWN_SWITCH_INFO()
    {
        Reset();
    }

    void Reset()
    {
        m_bySwitchState = 0;
        m_JoinSide = 0;
        ZeroMemory(m_szGuildName, sizeof(m_szGuildName));
        ZeroMemory(m_szUserName, sizeof(m_szUserName));
    }
} CROWN_SWITCH_INFO;
// MU Helper
#pragma pack(push, 1)
typedef struct
{
    BYTE DataStartMarker; // Index: 4
    BYTE : 3;             // Unused
    BYTE JewelOrGem : 1;  // Index: 5
    BYTE SetItem : 1;
    BYTE ExcellentItem : 1;
    BYTE Zen : 1;
    BYTE AddExtraItem : 1;
    BYTE HuntingRange : 4; // Index: 6
    BYTE ObtainRange : 4;
    WORD DistanceMin;            // Index: 7
    WORD BasicSkill1;            // Index: 9
    WORD ActivationSkill1;       // Index: 11
    WORD DelayMinSkill1;         // Index: 13
    WORD ActivationSkill2;       // Index: 15
    WORD DelayMinSkill2;         // Index: 17
    WORD CastingBuffMin;         // Index: 19
    WORD BuffSkill0NumberID;     // Index: 21
    WORD BuffSkill1NumberID;     // Index: 23
    WORD BuffSkill2NumberID;     // Index: 25
    BYTE HPStatusAutoPotion : 4; // Index: 27
    BYTE HPStatusAutoHeal : 4;
    BYTE HPStatusOfPartyMembers : 4; // Index: 28
    BYTE HPStatusDrainLife : 4;
    BYTE AutoPotion : 1; // Index: 29
    BYTE AutoHeal : 1;
    BYTE DrainLife : 1;
    BYTE LongDistanceAttack : 1;
    BYTE OriginalPosition : 1;
    BYTE Combo : 1;
    BYTE Party : 1;
    BYTE PreferenceOfPartyHeal : 1;
    BYTE BuffDurationforAllPartyMembers : 1; // Index: 30
    BYTE UseDarkSpirits : 1;
    BYTE BuffDuration : 1;
    BYTE Skill1Delay : 1;
    BYTE Skill1Con : 1;
    BYTE Skill1PreCon : 1;
    BYTE Skill1SubCon : 2;
    BYTE Skill2Delay : 1; // Index: 31
    BYTE Skill2Con : 1;
    BYTE Skill2PreCon : 1;
    BYTE Skill2SubCon : 2;
    BYTE RepairItem : 1;
    BYTE PickAllNearItems : 1;
    BYTE PickSelectedItems : 1;
    BYTE PetAttack; // Index: 32

    BYTE bUseSelfDefense : 1;       // Index: 33 (bit 0)
    BYTE bAutoAcceptFriend : 1;     // Index: 33 (bit 1)
    BYTE bAutoAcceptGuild : 1;      // Index: 33 (bit 2)
    BYTE bFallbackBasicAttack : 1;  // Index: 33 (bit 3)
    BYTE bConcentratedMonsters : 1; // Index: 33 (bit 4), client-local
    BYTE bUseSkillsClosely : 1;     // Index: 33 (bit 5), client-local
    BYTE : 2;                       // Unused bits of Index 33

    BYTE ManualControlYieldSecondsPlusOne; // Index: 34, client-local; zero uses legacy default
    BYTE _UnusedPadding[34];                // Index: 35 (34 bytes remaining)
    char ExtraItems[12][15]; // Index: 69
} PRECEIVE_MUHELPER_DATA, *LPRECEIVE_MUHELPER_DATA;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct
{
    DWORD ConsumeMoney;
    DWORD Money;
    DWORD Pause;
} PRECEIVE_MUHELPER_STATUS, *LPRECEIVE_MUHELPER_STATUS;
#pragma pack(pop)

struct _CROWN_SWITCH_INFO;

struct SessionNetworkStorage final
{
    MASTER_LEVEL_VALUE Master_Level_Data{};
    _CROWN_SWITCH_INFO *Switch_Info = nullptr;
    int HeroKey = 0;
    int CurrentProtocolState = 0;
    bool MainSceneReady = false;
    int PendingLoginMessageCode = 0;
    wchar_t Password[MAX_USERNAME_SIZE + 1]{};
    int HeroIndex = 0;
    wchar_t Question[31]{};
    int AttackPlayer = -1;
    int LogIn = 0;
    wchar_t LogInID[MAX_USERNAME_SIZE + 1]{};
    bool LogOut = false;
    wchar_t ChatWhisperID[MAX_USERNAME_SIZE + 1]{};
    int CurrentSkill = 0;
    int BuyCost = 0;
    int EnableUse = 0;
    int SendGetItem = -1;
    int SendDropItem = -1;
    BOOL g_bPacketAfter_EquipmentItem = FALSE;
    BYTE g_byPacketAfter_EquipmentItem[256]{};
    bool EnableGuildWar = false;
    int GuildWarIndex = -1;
    wchar_t GuildWarName[9]{};
    int GuildWarScore[2]{};
    bool EnableSoccer = false;
    BYTE HeroSoccerTeam = 0;
    int SoccerTime = 0;
    wchar_t SoccerTeamName[2][9]{};
    bool SoccerObserver = false;
    CHARACTER_ENABLE g_CharCardEnable{};
    int g_iMaxLetterCount = 0;
    int SummonLife = 0;
};

struct SessionWelfareTempleStorage final
{
    MATCH_RESULT g_wtMatchResult{};
    PMSG_MATCH_TIMEVIEW g_wtMatchTimeLeft{};
};

class SessionGameplayUnit;
class SessionUiUnit;
class SessionKeeper;
class GameSession;
class CErrorReport;
class CInGameShopSystem;
class CMapManager;
class CDirection;
class CSkillManager;
class CSQuest;
class CQuestMng;
class CSummonSystem;
class CMonkSystem;
class CPortalMgr;
class CmuConsoleDebug;
class CUIFriendMenu;
class CUIMapName;
class CUIMng;
class CSMServer;
class PetProcess;
class Connection;
class ManagedBindingUnit;
class ApplicationNetwork;
class SessionManager;
class SessionOrderedEffectBatch;
namespace SEASON3A
{
class CMixRecipeMgr;
}
struct PacketInfo;
struct PMSG_MASTER_SKILL_LIST_SEND;
namespace MUHelper
{
class SessionMuHelperUnit;
}
namespace SEASON3A
{
class CursedTemple;
}
namespace SEASON3B
{
class CNewUIMessageBoxMng;
}
namespace UI
{
class NoticeBoard;
}

class SessionNetworkUnit final : protected SessionLegacyCalls
{
  public:
    explicit SessionNetworkUnit(SessionKeeper &keeper) noexcept;
    ~SessionNetworkUnit();

    GameplayExternalEventBatch ProcessPacket(const PacketInfo &packet);
    BOOL CreateSocket(const wchar_t *address, unsigned short port);
    void DeleteSocket();
    std::unique_ptr<Connection> CreateConnection(const wchar_t *address, unsigned short port,
                                                 bool encrypted);
    bool PublishConnection(Connection &connection, ConnectionRole role);
    void SendPing(DWORD tickCount, int attackSpeed);
    void SendCheatDetectionLogout();
    bool BeginGameplaySendCapture(SessionOrderedEffectBatch &effects,
                                  std::uint64_t &nextStableSequence) noexcept;
    bool EndGameplaySendCapture() noexcept;
    bool CanCommitCapturedSend(SessionId sessionId, std::uint32_t channel) const noexcept;
    void CommitCapturedSend(std::span<const std::byte> payload) noexcept;
    ReconnectManager &Reconnect() noexcept
    {
        return *reconnect_;
    }
    CSMServer &MapServer() noexcept
    {
        return csMapServer_;
    }

  private:
    friend class GameSession;
    friend class GameSessionTestPeer;
    friend class SessionLegacyCalls;
    friend class SessionGameplayUnit;
    friend class CSMServer;
    bool MaterializePendingCharacterListOnOwner();
    void MaterializeCharacterList(const BYTE *receiveBuffer);
    bool InitializeConnectedChildren();
    CUIMng &LegacyUiManager() noexcept;
    int DenyCrownRegistPopupClose(POPUP_RESULT result);             // OMF-01143
    void ReceiveMapInfoResult(const BYTE *receiveBuffer);           // OMF-01161
    void RecevieRaklionBattleResult(const BYTE *receiveBuffer);     // OMF-01195
    void RecevieRaklionWideAreaAttack(const BYTE *receiveBuffer);   // OMF-01196
    void RecevieRaklionUserMonsterCount(const BYTE *receiveBuffer); // OMF-01197
    bool ReceiveRequestMoveMap(const BYTE *receiveBuffer);          // OMF-01210
    bool ReceiveIGS_SendCashGift(const BYTE *receiveBuffer);        // OMF-01221
    bool ReceiveIGS_PossibleBuy(const BYTE *receiveBuffer);         // OMF-01222
    bool ReceiveIGS_LeftCountItem(const BYTE *receiveBuffer);       // OMF-01223
    bool ReceivePeriodItemListCount(const BYTE *receiveBuffer);     // OMF-01229
    bool ReceivePeriodItemList(const BYTE *receiveBuffer);          // OMF-01230
    void ReceiveBCReg(const BYTE *receiveBuffer);                   // OMF-01124
    void ReceiveBCRegInfo(const BYTE *receiveBuffer);               // OMF-01126
    void ReceiveBCRegMark(const BYTE *receiveBuffer);               // OMF-01127
    void AddDebugText(const unsigned char *buffer, int size);
    int FindGuildName(wchar_t *name);
    void GuildTeam(CHARACTER *character);
    void InitGuildWar();
    void ReceiveDeclareWarResult(const BYTE *receiveBuffer);
    void ReceiveChatWhisperResult(const BYTE *receiveBuffer);
    void ReceiveDamage(const BYTE *receiveBuffer);
    void ReceiveSkillCount(const BYTE *receiveBuffer);
    void ReceiveDropItem(const BYTE *receiveBuffer);
    void ReceiveAddPoint(const BYTE *receiveBuffer);
    void ReceiveAddPointExtended(const BYTE *receiveBuffer);
    void ReceiveStatsExtended(const BYTE *receiveBuffer);
    BOOL ReceiveHelperItem(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveWeather(const BYTE *receiveBuffer);
    void ReceiveEvent(const BYTE *receiveBuffer);
    void ReceiveSummonLife(const BYTE *receiveBuffer);
    BOOL ReceiveTrade(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveTradeResult(const BYTE *receiveBuffer);
    void ReceiveTradeYourInventoryDelete(const BYTE *receiveBuffer);
    void ReceiveTradeMyGold(const BYTE *receiveBuffer);
    void ReceiveTradeYourGold(const BYTE *receiveBuffer);
    void ReceiveTradeYourResult(const BYTE *receiveBuffer);
    void ReceiveTradeExit(const BYTE *receiveBuffer);
    void InitGame();
    void ResetCombatTargetState();
    void ResetCharacterSessionState();
    void ReceiveServerConnectBusy(const BYTE *receiveBuffer);
    void RequestInventorySync();
    void ReceiveSell(const BYTE *receiveBuffer);
    void ReceivePing(const BYTE *receiveBuffer);
    void ReceiveGuildLeave(const BYTE *receiveBuffer);
    void ReceiveBanUnionGuildResult(const BYTE *receiveBuffer);
    void ReceivePlaySoundEffect(const BYTE *receiveBuffer);
    void ReceiveProgressQuestListReady(const BYTE *receiveBuffer);
    void ReceiveBCGiveUp(const BYTE *receiveBuffer);
    void ReceiveBCDeclareGuildList(const BYTE *receiveBuffer);          // OMF-01137
    void ReceiveBCGuildList(const BYTE *receiveBuffer);                 // OMF-01138
    void ReceiveGuildCommand(const BYTE *receiveBuffer);                // OMF-01162
    void ReceiveGuildMemberLocation(const BYTE *receiveBuffer);         // OMF-01163
    void ReceiveGuildNpcLocation(const BYTE *receiveBuffer);            // OMF-01164
    void ReceiveMatchTimer(const BYTE *receiveBuffer);                  // OMF-01165
    void ReceiveKanturu3rdTimer(const BYTE *receiveBuffer);             // OMF-01180
    void ReceiveCursedTempSkillPoint(const BYTE *receiveBuffer);        // OMF-01188
    void ReceiveCursedTempleHolyItemRelics(const BYTE *receiveBuffer);  // OMF-01189
    bool ReceiveRegistedLuckyCoin(const BYTE *receiveBuffer);           // OMF-01199
    bool ReceiveEnterDoppelGangerEvent(const BYTE *receiveBuffer);      // OMF-01202
    bool ReceiveDoppelGangerMonsterPosition(const BYTE *receiveBuffer); // OMF-01203
    bool ReceiveDoppelGangerIcewalkerState(const BYTE *receiveBuffer);  // OMF-01205
    bool ReceiveDoppelGangerTimePartyState(const BYTE *receiveBuffer);  // OMF-01206
    bool ReceiveDoppelGangerMonsterGoal(const BYTE *receiveBuffer);     // OMF-01208
    bool ReceiveMoveMapChecksum(const BYTE *receiveBuffer);             // OMF-01209
    void ReceiveCursedTempleGameResult(const BYTE *receiveBuffer);
    void ReceiveChatKey(const BYTE *receiveBuffer);
    void ReceiveMovePosition(const BYTE *receiveBuffer);
    void ReceiveDeleteCharacterViewport(const BYTE *receiveBuffer);
    void FallingStartCharacter(CHARACTER *character, OBJECT *object);
    void ReceiveParty(const BYTE *receiveBuffer);
    void ReceiveGuild(const BYTE *receiveBuffer);
    void ReceiveDeclareWar(const BYTE *receiveBuffer);
    void ReceiveGuildIDViewport(const BYTE *receiveBuffer);
    void ReceiveServerCommand(const BYTE *receiveBuffer);
    void ReceiveGemMixResult(const BYTE *receiveBuffer);
    void ReceiveGemUnMixResult(const BYTE *receiveBuffer);
    void ReceiveMoveToDevilSquareResult(const BYTE *receiveBuffer);
    void ReceiveDevilSquareOpenTime(const BYTE *receiveBuffer);
    void ReceiveDevilSquareCountDown(const BYTE *receiveBuffer);
    void ReceiveDevilSquareRank(const BYTE *receiveBuffer);
    void ReceiveMatchGameCommand(const BYTE *receiveBuffer);
    void ReceiveMoveToEventMatchResult(const BYTE *receiveBuffer);
    void ReceiveEventZoneOpenTime(const BYTE *receiveBuffer);
    void ReceiveMoveToEventMatchResult2(const BYTE *receiveBuffer);
    void ReceiveDuelResult(const BYTE *receiveBuffer);
    void ReceiveDisplayEffectViewport(const BYTE *receiveBuffer);
    void ReceiveServerImmigration(const BYTE *receiveBuffer);
    void ReceiveScratchResult(const BYTE *receiveBuffer);
    void ReceiveGensJoining(const BYTE *receiveBuffer);
    void ReceiveGensSecession(const BYTE *receiveBuffer);
    void ReceivePlayerGensInfluence(const BYTE *receiveBuffer);
    void ReceiveReward(const BYTE *receiveBuffer); // OMF-01113
    void ReceiveOtherPlayerGensInfluenceViewport(const BYTE *receiveBuffer);
    void ReceivePetCommand(const BYTE *receiveBuffer);
    void ReceivePetAttack(const BYTE *receiveBuffer);
    void ReceivePetInfo(const BYTE *receiveBuffer);
    void ReceiveKillCount(const BYTE *receiveBuffer);
    void ReceiveBuildTime(const BYTE *receiveBuffer);
    void ReceiveCastleGuildMark(const BYTE *receiveBuffer);
    void ReceiveUseStateItem(const BYTE *receiveBuffer);
    void ReceiveQuestPrize(const BYTE *receiveBuffer);
    void ReceiveKanturu3rdStateInfo(const BYTE *receiveBuffer);    // OMF-01175
    void ReceiveKanturu3rdEnterBossMap(const BYTE *receiveBuffer); // OMF-01176
    void ReceiveKanturu3rdCurrentState(const BYTE *receiveBuffer);
    void ReceiveKanturu3rdState(const BYTE *receiveBuffer);
    void ReceiveKanturu3rdResult(const BYTE *receiveBuffer);
    void RecevieKanturu3rdMayaSKill(const BYTE *receiveBuffer);
    void RecevieKanturu3rdLeftUserandMonsterCount(const BYTE *receiveBuffer);
    void ReceiveCrownSwitchState(const BYTE *receiveBuffer);
    void ReceiveCrownRegist(const BYTE *receiveBuffer);
    void ReceiveCrownState(const BYTE *receiveBuffer);
    void ReceiveBCStatus(const BYTE *receiveBuffer);              // OMF-01123
    void ReceiveBCNPCBuy(const BYTE *receiveBuffer);              // OMF-01128
    void ReceiveBCNPCRepair(const BYTE *receiveBuffer);           // OMF-01129
    void ReceiveBCNPCUpgrade(const BYTE *receiveBuffer);          // OMF-01130
    void ReceiveBCGetTaxInfo(const BYTE *receiveBuffer);          // OMF-01131
    void ReceiveBCChangeTaxRate(const BYTE *receiveBuffer);       // OMF-01132
    void ReceiveBCWithdraw(const BYTE *receiveBuffer);            // OMF-01133
    void ReceiveHuntZoneEnter(const BYTE *receiveBuffer);         // OMF-01135
    void ReceiveBCNPCList(const BYTE *receiveBuffer);             // OMF-01136
    void ReceiveBattleCasleSwitchInfo(const BYTE *receiveBuffer); // OMF-01147
    bool Check_Switch(PRECEIVE_CROWN_SWITCH_INFO *data);          // OMF-01148
    bool Delete_Switch();                                         // OMF-01149
    void ReceiveCastleHuntZoneInfo(const BYTE *receiveBuffer);
    void ReceiveCastleHuntZoneResult(const BYTE *receiveBuffer);
    void ReceiveCatapultState(const BYTE *receiveBuffer);    // OMF-01157
    void ReceiveCatapultFire(const BYTE *receiveBuffer);     // OMF-01158
    void ReceiveCatapultFireToMe(const BYTE *receiveBuffer); // OMF-01159
    void ReceiveCrywolfAltarContract(const BYTE *receiveBuffer);
    void ReceiveCursedTempleEnterInfo(const BYTE *receiveBuffer); // OMF-01183
    void ReceiveCursedTempleEnterResult(const BYTE *receiveBuffer);
    void ReceiveCursedTempleInfo(const BYTE *receiveBuffer);
    void ReceiveCursedTempMagicResult(const BYTE *receiveBuffer); // OMF-01186
    void ReceiveCursedTempSkillEnd(const BYTE *receiveBuffer);    // OMF-01187
    void ReceiveCursedTempleState(const BYTE *receiveBuffer);
    bool ReceiveRegistLuckyCoin(const BYTE *receiveBuffer);
    bool ReceiveRequestExChangeLuckyCoin(const BYTE *receiveBuffer);
    bool ReceiveDoppelGangerState(const BYTE *receiveBuffer);
    bool ReceiveDoppelGangerResult(const BYTE *receiveBuffer);
    bool ReceiveRemainTickEmpireGuardian(const BYTE *receiveBuffer); // OMF-01212
    bool ReceiveResultEmpireGuardian(const BYTE *receiveBuffer);
    bool ReceiveIGS_BuyItem(const BYTE *receiveBuffer);
    bool ReceiveIGS_SendItemGift(const BYTE *receiveBuffer);
    bool ReceiveIGS_UseStorageItem(const BYTE *receiveBuffer);
    GameplayExternalEventBatch ProcessPacket(const BYTE *receiveBuffer, std::int32_t size);
    void ReceiveAction(const BYTE *receiveBuffer, int size);
    BOOL ReceiveMonsterSkill(const BYTE *receiveBuffer, int size, BOOL encrypted);
    BOOL ReceiveMagic(const BYTE *receiveBuffer, int size, BOOL encrypted);
    BOOL ReceiveMagicContinue(const BYTE *receiveBuffer, int size, BOOL encrypted);
    void ReceiveMuHelperStatusUpdate(std::span<const BYTE> receiveBuffer);
    BOOL RejectWorldTransfer();
    void StopInactiveWorldEventAudio();
    void ResetClientToLoginScene();
    void ReceiveDarkside(const BYTE *receiveBuffer);
    void ReceiveOption(const BYTE *receiveBuffer);
    void ReceiveEventChipInfomation(const BYTE *receiveBuffer);
    void ReceiveEventChip(const BYTE *receiveBuffer);
    void ReceiveMutoNumber(const BYTE *receiveBuffer);
    void ReceiveEventCount(const BYTE *receiveBuffer);
    void ReceiveQuestHistory(const BYTE *receiveBuffer);
    void ReceiveQuestState(const BYTE *receiveBuffer);
    void ReceiveQuestResult(const BYTE *receiveBuffer);
    void ReceiveQuestMonKillInfo(const BYTE *receiveBuffer);
    void ReceiveQuestByEtcEPList(const BYTE *receiveBuffer);
    void ReceiveQuestByNPCEPList(const BYTE *receiveBuffer);
    void ReceiveQuestCompleteResult(const BYTE *receiveBuffer);
    void ReceiveQuestGiveUp(const BYTE *receiveBuffer);
    void ReceiveProgressQuestList(const BYTE *receiveBuffer);
    void ReceiveProgressQuestRequestReward(const BYTE *receiveBuffer);
    void ReceiveConfirmPassword(const BYTE *receiveBuffer);
    void ReceiveConfirmPassword2(const std::span<const BYTE> receiveBuffer);
    void ReceiveChangePassword(const BYTE *receiveBuffer);
    void ReceiveMoveCharacter(std::span<const BYTE> receiveBuffer);
    void ReceiveCreateTransformViewport(std::span<const BYTE> receiveBuffer);
    BOOL ReceiveTeleport(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveCreateMonsterViewport(const BYTE *receiveBuffer);
    void ReceiveCreateSummonViewport(const BYTE *receiveBuffer);
    void ReceiveAttackDamageExtended(const BYTE *receiveBuffer);
    void ReceiveDie(const BYTE *receiveBuffer, int size);
    void ReceiveCreateMoney(std::span<const BYTE> receiveBuffer);
    void ReceiveCreateItemViewportExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveDeleteItemViewport(const BYTE *receiveBuffer);
    void ReceiveGetItem(std::span<const BYTE> receiveBuffer);
    void ReceivePartyGetItem(const BYTE *receiveBuffer);
    void SetQuestNpc(int npcIndex);
    void ReceiveNPCDlgUIStart(const BYTE *receiveBuffer);
    void ReceivePreviewPort(std::span<const BYTE> receiveBuffer);
    void ReceiveServerConnect(const BYTE *receiveBuffer);
    bool ReceiveJoinServer(const BYTE *receiveBuffer);
    bool ReceiveChangeMapServerInfo(const BYTE *receiveBuffer);
    bool ReceiveIGS_UpdateScript(const BYTE *receiveBuffer);
    void ReceiveServerList(const BYTE *receiveBuffer);
    void ReceiveCharacterListExtended(const BYTE *receiveBuffer);
    void ReceiveCharacterCard_New(const BYTE *receiveBuffer);
    void ReceiveCreateCharacter(const BYTE *receiveBuffer);
    void ReceiveDeleteCharacter(const BYTE *receiveBuffer);
    BOOL ReceiveLogOut(const BYTE *receiveBuffer, BOOL encrypted);
    void ResetForServerSelection();
    void ReceiveRevival(const BYTE *receiveBuffer);
    void ReceiveMagicList(const BYTE *receiveBuffer);
    void Receive_Master_SetSkillList(PMSG_MASTER_SKILL_LIST_SEND *message);
    void ReceiveMuHelperConfigurationData(std::span<const BYTE> receiveBuffer);
    void ReceiveChainMagic(const BYTE *receiveBuffer);
    void ReceiveSoccerScore(const BYTE *receiveBuffer);
    void ReceiveDeleteInventory(const BYTE *receiveBuffer);
    int CalcItemLength(std::span<const BYTE> receiveBuffer);
    BOOL ReceiveInventoryExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveTradeInventoryExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveEquipment(std::span<const BYTE> receiveBuffer);
    void ReceiveChat(const BYTE *receiveBuffer);
    void ReceiveChatWhisper(const BYTE *receiveBuffer);
    void ReceiveNotice(const BYTE *receiveBuffer);
    void ReceiveFriendList(const BYTE *receiveBuffer);
    void ReceiveAddFriendResult(const BYTE *receiveBuffer);
    void ReceiveRequestAcceptAddFriend(const BYTE *receiveBuffer);
    void ReceiveDeleteFriendResult(const BYTE *receiveBuffer);
    void ReceiveFriendStateChange(const BYTE *receiveBuffer);
    void ReceiveLetterSendResult(const BYTE *receiveBuffer);
    void ReceiveLetter(const BYTE *receiveBuffer);
    void ReceiveLetterText(std::span<const BYTE> receiveBuffer, bool isCached);
    void ReceiveLetterDeleteResult(const BYTE *receiveBuffer);
    void ReceiveCreateChatRoomResult(const BYTE *receiveBuffer);
    void ReceiveChatRoomInviteResult(const BYTE *receiveBuffer);
    void ReceiveChangePlayer(std::span<const BYTE> receiveBuffer);
    void ReceiveCreatePlayerViewportExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveAttackDamage(CHARACTER *character, OBJECT *object, const bool success,
                             const int key, const int damage, const int shieldDamage,
                             const int damageType, const bool repeatedly, const bool endRepeatedly,
                             const bool doubleEnable, const bool comboEnable);
    void ReceiveAttackDamageCastle(CHARACTER *character, OBJECT *object, const bool success,
                                   const int key, const int damage, const int shieldDamage,
                                   const int damageType, const bool repeatedly,
                                   const bool endRepeatedly, const bool doubleEnable,
                                   const bool comboEnable);
    void ReceiveMagicPosition(const BYTE *receiveBuffer, int size);
    BOOL ReceiveDieExpLarge(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveSkillStatus(const BYTE *receiveBuffer);
    void ReceiveMagicFinish(const BYTE *receiveBuffer);
    void ReceiveBuffState(const BYTE *receiveBuffer);
    BOOL ReceiveDieExp(const BYTE *receiveBuffer, BOOL encrypted);
    BOOL ReceiveJoinMapServer(std::span<const BYTE> receiveBuffer);
    BOOL ReceiveEquipmentItemExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveModifyItemExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveBuyExtended(const std::span<const BYTE> receiveBuffer);
    void ReceiveTradeYourInventoryExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveMixExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveRepair(const BYTE *receiveBuffer);
    void ReceiveLevelUp(const BYTE *receiveBuffer, int size);
    void ReceiveDurability(const BYTE *receiveBuffer);
    void ReceiveStorageGold(const BYTE *receiveBuffer);
    void ReceiveStorageExit(const BYTE *receiveBuffer);
    void ReceiveStorageStatus(const BYTE *receiveBuffer);
    void ReceivePartyResult(const BYTE *receiveBuffer);
    void ReceivePartyList(const BYTE *receiveBuffer);
    void ReceivePartyInfo(const BYTE *receiveBuffer);
    void ReceivePartyLeave(const BYTE *receiveBuffer); // OMF-01008
    void ReceiveGuildResult(const BYTE *receiveBuffer);
    void ReceiveGuildList(const BYTE *receiveBuffer);
    void ReceiveCreateGuildInterface(const BYTE *receiveBuffer);
    void ReceiveCreateGuildMasterInterface(const BYTE *receiveBuffer);
    void ReceiveDeleteGuildViewport(const BYTE *receiveBuffer);
    void ReceiveCreateGuildResult(const BYTE *receiveBuffer); // OMF-01017
    void ReceivePK(const BYTE *receiveBuffer);
    void ReceiveGuildBeginWar(const BYTE *receiveBuffer);
    void ReceiveGuildEndWar(const BYTE *receiveBuffer);
    void ReceiveGuildWarScore(const BYTE *receiveBuffer);
    void ReceiveGuildInfo(const BYTE *receiveBuffer);
    void ReceiveGuildAssign(const BYTE *receiveBuffer);
    void ReceiveGuildRelationShip(const BYTE *receiveBuffer);
    void ReceiveGuildRelationShipResult(const BYTE *receiveBuffer);
    void ReceiveUnionViewportNotify(const BYTE *receiveBuffer);
    void ReceiveUnionList(const BYTE *receiveBuffer);
    void ReceiveSoccerTime(const BYTE *receiveBuffer);
    void ReceiveSoccerGoal(const BYTE *receiveBuffer);
    void ReceiveBattleCastleRegiment(const BYTE *receiveBuffer);
    void ReceiveBattleCastleStart(const BYTE *receiveBuffer);
    void ReceiveBattleCastleProcess(const BYTE *receiveBuffer);
    void GateOpen(CHARACTER *character, OBJECT *object);
    void GateClose(CHARACTER *character, OBJECT *object);
    void DoInterfaceOpen(int key);
    void ProcessState(int key, BYTE gateOnOff, BYTE state);
    void SendToggleGate();
    bool IsGateOpened();
    void ReceiveGateState(const BYTE *receiveBuffer);
    void ReceiveGateOperator(const BYTE *receiveBuffer);
    void ReceiveGateCurrentState(const BYTE *receiveBuffer);
    void Receive_Master_LevelUp(const BYTE *receiveBuffer, int size);
    void Receive_Master_Level_Exp(const BYTE *receiveBuffer, int size);
    void Receive_Master_LevelGetSkill(const BYTE *receiveBuffer);
    void ReceiveMixExit(const BYTE *receiveBuffer);
    BOOL ReceiveTalk(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveSetAttribute(const BYTE *receiveBuffer);
    void ReceiveSetPriceResult(const BYTE *receiveBuffer);
    void ReceiveCreatePersonalShop(const BYTE *receiveBuffer);
    void ReceiveDestroyPersonalShop(const BYTE *receiveBuffer);
    void ReceivePersonalShopItemList(std::span<const BYTE> receiveBuffer);
    void ReceiveRefreshItemList(std::span<const BYTE> receiveBuffer);
    void ReceivePurchaseItem(std::span<const BYTE> receiveBuffer);
    void NotifySoldItem(const BYTE *receiveBuffer);
    void NotifyClosePersonalShop(const BYTE *receiveBuffer);
    void ReceiveChangeMapServerResult(const BYTE *receiveBuffer);
    void ReceiveCheckSumRequest(const BYTE *receiveBuffer);
    void ReceiveQuestQSSelSentence(const BYTE *receiveBuffer);
    void ReceiveQuestQSRequestReward(const BYTE *receiveBuffer);
    void ReceiveDuelRequest(const BYTE *receiveBuffer);
    void ReceiveDuelStart(const BYTE *receiveBuffer);
    void ReceiveDuelEnd(const BYTE *receiveBuffer);
    void ReceiveDuelScore(const BYTE *receiveBuffer);
    void ReceiveDuelHP(const BYTE *receiveBuffer);
    void ReceiveDuelChannelList(const BYTE *receiveBuffer);
    void ReceiveDuelWatchRequestReply(const BYTE *receiveBuffer);
    void ReceiveDuelWatcherJoin(const BYTE *receiveBuffer);
    void ReceiveDuelWatchEnd(const BYTE *receiveBuffer);
    void ReceiveDuelWatcherQuit(const BYTE *receiveBuffer);
    void ReceiveDuelWatcherList(const BYTE *receiveBuffer);
    void ReceiveDuelRound(const BYTE *receiveBuffer);
    void ReceiveCreateShopTitleViewport(const BYTE *receiveBuffer);
    void ReceiveShopTitleChange(const BYTE *receiveBuffer);
    void ReceiveRaklionStateInfo(const BYTE *receiveBuffer);
    void ReceiveRaklionCurrentState(const BYTE *receiveBuffer);
    void RecevieRaklionStateChange(const BYTE *receiveBuffer);
    bool ReceiveEnterEmpireGuardianEvent(const BYTE *receiveBuffer);
    void ReceiveCrywolfInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolStateAltarfInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolfLifeTime(const BYTE *receiveBuffer);
    void ReceiveCrywolfTankerHit(const BYTE *receiveBuffer);
    void ReceiveCrywolfBenefitPlusChaosRate(const BYTE *receiveBuffer);
    void ReceiveCrywolfBossMonsterInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolfPersonalRank(const BYTE *receiveBuffer);
    void ReceiveCrywolfHeroList(const BYTE *receiveBuffer);
    bool ReceiveIGS_CashPoint(const BYTE *receiveBuffer);
    bool ReceiveIGS_ShopOpenResult(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageItemListCount(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageItemList(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageGiftItemList(const BYTE *receiveBuffer);
    bool ReceiveIGS_EventItemlistCnt(const BYTE *receiveBuffer);
    bool ReceiveIGS_EventItemlist(const BYTE *receiveBuffer);
    bool ReceiveIGS_UpdateBanner(const BYTE *receiveBuffer);
    void RegisterBuff(eBuffState buff, OBJECT *object, const int buffTime = 0);
    void UnRegisterBuff(eBuffState buff, OBJECT *object);
    bool CheckExceptionBuff(eBuffState buff, OBJECT *object, bool isErase);
    void InsertBuffLogicalEffect(eBuffState buff, OBJECT *object, const int buffTime);
    void ClearBuffLogicalEffect(eBuffState buff, OBJECT *object);
    void AppearMonster(CHARACTER *character);
    void ReceiveSetPointsExtended(const BYTE *receiveBuffer);
    void ReceiveWTTimeLeft(const BYTE *receiveBuffer);
    void ReceiveWTMatchResult(const BYTE *receiveBuffer);
    void ReceiveTaxInfo(const BYTE *receiveBuffer);
    void ReceiveWTBattleSoccerGoalIn(const BYTE *receiveBuffer);
    BOOL ReceiveStraightAttack(const BYTE *receiveBuffer, int size, BOOL encrypted);
    void InsertBuffPhysicalEffect(eBuffState buff, OBJECT *object);
    void ClearBuffPhysicalEffect(eBuffState buff, OBJECT *object);
    bool ReceiveEquippingInventoryItem(const BYTE *receiveBuffer);
    void LogSafeCastSizeMismatch(const char *packetType, std::size_t received,
                                 std::size_t expected);
    std::optional<std::uint32_t> CurrentMapRoute() noexcept;
    template <typename T>
    T *safe_cast(const std::span<const BYTE> receiveBuffer, const char *packetType = nullptr);
#ifdef PK_ATTACK_TESTSERVER_LOG
    void PrintPKLog(CHARACTER *character);
#endif

    SessionGameplayUnit &gameplay_;
    SessionUiUnit &ui_;
    SEASON3A::CursedTemple &cursedTemple_;
    MUHelper::SessionMuHelperUnit &muHelper_;
    ITEM_t (&Items)[MAX_ITEMS];
    MATCH_RESULT &g_wtMatchResult;
    PMSG_MATCH_TIMEVIEW &g_wtMatchTimeLeft;
    int &g_nTaxRate;
    int &g_nChaosTaxRate;
    int &g_iGoalEffect;
    CErrorReport &g_ErrorReport;
    CmuConsoleDebug &g_ConsoleDebug;
    HWND &g_hWnd;
    CMapManager &gMapManager;
    CDirection &g_Direction;
    CSkillManager &gSkillManager;
    CSummonSystem &g_SummonSystem;
    CMonkSystem &g_CMonkSystem;
    SEASON3B::CNewUIMessageBoxMng &g_MessageBox;
    CPortalMgr &g_PortalMgr;
    PetProcess &g_petProcess;
    BOOL &g_bUseWindowMode;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    CUIFriendMenu &g_pFriendMenu;
    CInGameShopSystem &g_InGameShopSystem;
    CUIMapName &g_pUIMapName;
    SEASON3A::CMixRecipeMgr &g_MixRecipeMgr;
    CSQuest &g_csQuest;
    CQuestMng &g_QuestMng;
    UI::NoticeBoard &notices_;
    ManagedBindingUnit &managedBindings_;
    ApplicationNetwork &applicationNetwork_;
    SessionManager &sessionManager_;
    bool isCurrentGateOpen_ = false;
    int npcCharacterKey_ = 0;
    bool gateAttackCleared_ = false;
    bool partyLeaveClearedFollow_ = false;
    bool guildCreateResetCadence_ = false;
    CSMServer csMapServer_;
    std::unique_ptr<ReconnectManager> reconnect_;
    Connection *capturedSendConnection_ = nullptr;
    bool gameplaySendCaptureActive_ = false;
    std::vector<BYTE> pendingCharacterList_;
};
