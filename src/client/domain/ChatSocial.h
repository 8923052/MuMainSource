#pragma once
#include "support/CoreMath.h"
#include "session/SessionRuntime.h"

#define MAX_COMMAND 13
#define MAX_DISTANCE_TILE 2

class SessionKeeper;

class CHARACTER;
class OBJECT;

// Chat balloons: speech bubbles drawn above characters and the message list
// that feeds them.
namespace UI::Chat
{
struct CHAT
{
    wchar_t ID[32];
    wchar_t Union[30];
    wchar_t Guild[30];
    wchar_t szShopTitle[16];
    char Color;
    char GuildColor;
    float IDLifeTime;
    wchar_t Text[2][256];
    float LifeTime[2];
    CHARACTER *Owner;
    int x;
    int y;
    int Width;
    int Height;
    float Position[3];
};

inline constexpr int MAX_CHAT = 120;
inline constexpr int WHISPER_ID_SLOTS = 10;
inline constexpr int WHISPER_ID_LENGTH = 11;

struct Storage
{
    CHAT Chat[MAX_CHAT]{};
    int WhisperID_Num = 0;
    wchar_t WhisperRegistID[WHISPER_ID_SLOTS][WHISPER_ID_LENGTH]{};
};
} // namespace UI::Chat

namespace SEASON3B
{
class CPartyManager : protected SessionLegacyCalls
{
  public:
    explicit CPartyManager(SessionKeeper &keeper) noexcept;
    virtual ~CPartyManager();

  public:
    bool Create();
    void Release();
    bool Update();
    bool Render();

  public:
    void SearchPartyMember();
    bool IsPartyActive();
    bool IsPartyMember(int index);
    bool IsPartyMemberChar(CHARACTER *c);
    CHARACTER *GetPartyMemberChar(PARTY_t *pMember);
};
} // namespace SEASON3B

enum COMMAND_TYPE
{
    COMMAND_NONE = -1,
    COMMAND_TRADE = 0,
    COMMAND_PURCHASE,
    COMMAND_PARTY,
    COMMAND_WHISPER,
    COMMAND_GUILD,
    COMMAND_GUILDUNION,
    COMMAND_RIVAL,
    COMMAND_RIVALOFF,
    COMMAND_ADD_FRIEND,
    COMMAND_FOLLOW,
    COMMAND_BATTLE,
    COMMAND_END
};

typedef struct GUILD_LIST_t
{
    wchar_t Name[MAX_USERNAME_SIZE + 1];
    BYTE Number;
    BYTE Server;
    BYTE GuildStatus;
} GUILD_LIST_t;

typedef struct PARTY_t
{
    wchar_t Name[MAX_USERNAME_SIZE + 1];
    BYTE Number;
    BYTE Map;
    BYTE x;
    BYTE y;
    int currHP;
    int maxHP;
    BYTE stepHP;
    int index;
} PARTY_t;

typedef struct
{
    wchar_t ID[MAX_USERNAME_SIZE + 1];
    wchar_t Text[256];
    int Type;
    int LifeTime;
    int Width;
} WHISPER;

namespace ChatTextDetail
{
using UI::Chat::CHAT;
using UI::Chat::MAX_CHAT;
inline const int ciSystemColor = 240;
} // namespace ChatTextDetail
