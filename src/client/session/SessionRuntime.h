#pragma once
#include "support/CoreMath.h"

#include "app/Application.h"
#include "data/ItemData.h"
#include "data/WorldData.h"
#include "network/generated/PacketFunctions_ClientToServer_Enums.h"
#include "render/Assets.h"
#include "support/Scenes.h"

#include <array>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PkFieldDetail
{
struct MonsterDefinition;
}

namespace ItemRulesDetail
{
struct GroundItemLabelDescriptor;
}

namespace SplashSceneDetail
{
enum class BackgroundTheme;
}

enum eCursorType
{
    CURSOR_NORMAL = 0,
    CURSOR_PUSH,
    CURSOR_ATTACK,
    CURSOR_GET,
    CURSOR_TALK,
    CURSOR_REPAIR,
    CURSOR_LEANAGAINST,
    CURSOR_SITDOWN,
    CURSOR_DONTMOVE,
    CURSOR_IDSELECT,
};

struct SessionDisplayRect;
struct GameplayExternalEvent;
struct SessionInputEvent;
struct SessionPhysicsFrameInput;
struct SessionFrameInput;
struct SessionAdvanceJob;
struct ApplicationRenderCompletionBatch;
struct ApplicationSessionSchedulerMetrics;
class ApplicationSessionScheduler;
class ApplicationFramePlan;
class SessionAdvanceResult;

class ApplicationLegacyCalls;
class BMD;
class BuffScriptLoader;
class BuffStateSystem;
class BuffStateValueControl;
class BuffTimeControl;
class CDuelMgr;
class CGuildCache;
class CHARACTER;
class CHARACTER_MACHINE;
class CPhysicsCloth;
class CSPetSystem;
class CSenatusInfo;
class CServerListManager;
class CShadowVolume;
class CTimeCheck;
class CUIGateKeeper;
class CUIGuardsMan;
class CUIManager;
class CUIMng;
class CUIPopup;
class CameraManager;
class Connection;
class GambleSystem;
class ItemAddOptioninfo;
class JewelHarmonyInfo;
class LegacyRenderFacade;
class MapProcess;
class OBJECT;
class OrbitalCamera;
class PATH;
class SessionBitmapView;
class SessionGameDataUnit;
class SessionItemStore;
class SessionKeeper;
class SessionModelLoader;
class SessionModelPool;
class SessionRandom;
class SessionRenderUnit;
class WorldFileData;
class WorldImageData;
enum class CastleLevel : std::uint8_t;
enum eCurrentMode : int;
enum eCurrentStep : int;
namespace SEASON3B
{
class CMoveCommandData;
}
namespace SEASON3B
{
class CNewUIInventoryCtrl;
}
namespace SEASON3B
{
class CNewUISystem;
}
namespace SEASON3B
{
class CPartyManager;
}
namespace UI::Chat
{
struct CHAT;
}
struct AnimationPoseSample;
struct BMD;
class CHARACTER;
struct CHARACTER_ENABLE;
struct CharacterDrawInput;
struct GUILD_LIST_t;
struct GroundItemLabelCacheEntry;
struct ITEM_ATTRIBUTE;
struct ItemCreationParams;
struct MARK_t;
struct MovementSkill;
class OBJECT;
struct ObjectDrawInput;
struct PARTY_t;
struct PMSG_MASTER_SKILL_LIST_SEND;
struct SessionCharacterPopulationStorage;
struct WorldCharacterVisualState;
struct _CROWN_SWITCH_INFO;
struct _MASTER_LEVEL_VALUE;
struct tagITEM_t;
typedef _MASTER_LEVEL_VALUE MASTER_LEVEL_VALUE;
#define MAX_GUILDS 80

#define MAX_PARTYS 5

#define MAX_WHISPER_ID 5

constexpr auto NUM_BUTTON_CMB = (2);

constexpr auto NUM_PAR_BUTTON_CMB = (5);

namespace SessionQuestDialogDimensions
{
inline constexpr std::size_t MessageLineCount = 7;
inline constexpr std::size_t MessageLength = 38;
inline constexpr std::size_t AnswerCount = 10;
inline constexpr std::size_t AnswerLineCount = 1;
} // namespace SessionQuestDialogDimensions
struct SessionItemHelpStorage final
{
    static constexpr int LevelCount = 16;
    static constexpr int ColumnCount = 17;

    int g_iCurrentItem = -1;
    int g_iItemInfo[LevelCount][ColumnCount]{};
};
struct AnimationPoseSample;

struct WorldCharacterVisualState;

class WorldFileData;
class WorldImageData;

typedef float vec_t;
typedef float vec3_t[3];

class SessionKeeper;
class SessionRandom;
class SessionRenderUnit;
class LegacyRenderFacade;
class SessionGameDataUnit;
class Connection;
class BMD;
class SessionModelLoader;
class SessionItemStore;
class CPhysicsCloth;
class PATH;
class CTimeCheck;
class CHARACTER_MACHINE;
class BuffStateSystem;
class BuffScriptLoader;
class BuffTimeControl;
class BuffStateValueControl;
class CServerListManager;
class CSPetSystem;
class CSenatusInfo;
class CShadowVolume;
class CDuelMgr;
class CUIGuardsMan;
class CGuildCache;
class GambleSystem;
class CUIMng;
class CUIGateKeeper;
class CUIPopup;
class JewelHarmonyInfo;
class ItemAddOptioninfo;
class CameraManager;
class OrbitalCamera;
class MapProcess;
struct PMSG_MASTER_SKILL_LIST_SEND;
struct PRECEIVE_MATCH_GAME_STATE;
using LPPRECEIVE_MATCH_GAME_STATE = PRECEIVE_MATCH_GAME_STATE *;
struct tagPRECEIVE_CROWN_SWITCH_INFO;
typedef struct tagPRECEIVE_CROWN_SWITCH_INFO PRECEIVE_CROWN_SWITCH_INFO;
struct MatchResult;
struct GroundItemLabelCacheEntry;
struct tagOBB_t;
typedef struct tagOBB_t OBB_t;
struct tagITEM_t;
typedef struct tagITEM_t ITEM_t;
struct tagPET_INFO;
typedef struct tagPET_INFO PET_INFO;
struct tagCHARACTER_ATTRIBUTE;
typedef struct tagCHARACTER_ATTRIBUTE CHARACTER_ATTRIBUTE;
struct ItemCreationParams;
struct JOINT;
struct PARTICLE;
struct _PART_t;
typedef struct _PART_t PART_t;
struct _PATH_t;
typedef struct _PATH_t PATH_t;
typedef int POPUP_RESULT;

enum ActionSkillType : int;
enum CLASS_TYPE : BYTE;
enum EMonsterModelType : int;
enum eBuffState : int;
enum eTypeSkill : int;
enum struct STORAGE_TYPE;
namespace UI::Chat
{
struct CHAT;
}
namespace SEASON3B
{
class CNewUIInventoryCtrl;
}
namespace SEASON3B
{
class CNewUISystem;
}
namespace SEASON3B
{
class CPartyManager;
}
namespace SEASON3B
{
class CMoveCommandData;
}

class SessionLegacyCalls : protected ApplicationLegacyCalls
{
  protected:
    // The only render recorder entry point available to session-owned legacy
    // calls. The keeper owns the exact SessionRenderUnit; no global or
    // focus-based lookup is allowed on the recording path.
    LegacyRenderFacade &LegacyRender() const noexcept;

    // Routed R4 input reads intentionally hide the application-wide hardware
    // snapshot for session behavior. They are compatibility helpers, not OMF
    // forwarding entries.
    bool IsKeyDown(int virtualKey) const;
    bool IsNone(int virtualKey) const;
    bool IsRelease(int virtualKey) const;
    bool IsPress(int virtualKey) const;
    bool IsRepeat(int virtualKey) const;
    void SetKeyState(int virtualKey, int state);
    bool LoadBitmapW(const wchar_t *fileName, std::uint32_t textureIndex,
                     LegacyTextureFilter filter = LegacyTextureFilter::Nearest,
                     LegacyTextureWrap wrapMode = LegacyTextureWrap::ClampToEdge, bool check = true,
                     bool fullPath = false) const;
    void DeleteBitmap(std::uint32_t textureIndex, bool force = false);
    void BindTexture(int tex) const;
    void BindTextureStream(int tex) const;
    void EndTextureStream() const;
    void EnableDepthTest() const;
    void DisableDepthTest() const;
    void EnableDepthMask() const;
    void DisableDepthMask() const;
    void EnableCullFace() const;
    void DisableCullFace() const;
    void DisableTexture(bool alphaTest = false) const;
    void DisableAlphaBlend() const;
    void EnableAlphaTest(bool depthMask = true) const;
    void EnableAlphaBlend() const;
    void EnableAlphaBlendMinus() const;
    void EnableAlphaBlend2() const;
    void EnableAlphaBlend3() const;
    void EnableAlphaBlend4() const;
    void EnableLightMap() const;
    float ConvertX(float x) const;
    float ConvertY(float y) const;
    void DrawSolidRect(int x, int y, int width, int height, float brightness) const;
    void RenderColor(float x, float y, float width, float height, float alpha = 0.f,
                     int flag = 0) const;
    void EndRenderColor() const;
    void RenderColorBitmap(int texture, float x, float y, float width, float height, float u = 0.f,
                           float v = 0.f, float uWidth = 1.f, float vHeight = 1.f,
                           unsigned int color = 0xffffffff) const;
    void RenderBitmap(int texture, float x, float y, float width, float height, float u = 0.f,
                      float v = 0.f, float uWidth = 1.f, float vHeight = 1.f, bool scale = true,
                      bool startScale = true, float alpha = 0.f) const;
    void RenderBitmapRotate(int texture, float x, float y, float width, float height, float rotate,
                            float u = 0.f, float v = 0.f, float uWidth = 1.f,
                            float vHeight = 1.f) const;
    void RenderBitRotate(int texture, float x, float y, float width, float height,
                         float rotate) const;
    void RenderBitmapLocalRotate(int texture, float x, float y, float width, float height,
                                 float rotate, float u = 0.f, float v = 0.f, float uWidth = 1.f,
                                 float vHeight = 1.f) const;
    void RenderBitmapAlpha(int texture, float sx, float sy, float width, float height) const;
    void RenderBitmapUV(int texture, float x, float y, float width, float height, float u, float v,
                        float uWidth, float vHeight) const;

    // The legacy scene sources still spell the fixed-function operations as
    // OpenGL calls. These compatibility members keep those calls on the exact
    // session facade while recording; the direct route calls the platform
    // entry points unchanged. They are intentionally a finite surface, not a
    // second renderer or a recorder lookup.
    void glBegin(unsigned int mode) const;
    void glEnd() const;
    void glVertex2f(float x, float y) const;
    void glVertex3f(float x, float y, float z) const;
    void glVertex3fv(const float *value) const;
    void glColor3f(float red, float green, float blue) const;
    void glColor3fv(const float *value) const;
    void glColor3ub(unsigned char red, unsigned char green, unsigned char blue) const;
    void glColor4f(float red, float green, float blue, float alpha) const;
    void glColor4fv(const float *value) const;
    void glColor4ub(unsigned char red, unsigned char green, unsigned char blue,
                    unsigned char alpha) const;
    void glNormal3f(float x, float y, float z) const;
    void glNormal3fv(const float *value) const;
    void glTexCoord2f(float u, float v) const;
    void glTexCoord2fv(const float *value) const;
    void glEnable(unsigned int capability) const;
    void glDisable(unsigned int capability) const;
    void glDepthMask(unsigned char enabled) const;
    void glDepthFunc(unsigned int function) const;
    void glBlendFunc(unsigned int source, unsigned int destination) const;
    void glAlphaFunc(unsigned int function, float reference) const;
    void glCullFace(unsigned int face) const;
    void glFrontFace(unsigned int face) const;
    void glPolygonMode(unsigned int face, unsigned int mode) const;
    void glLineWidth(float width) const;
    void glMatrixMode(unsigned int mode) const;
    void glLoadIdentity() const;
    void glLoadMatrixf(const float *matrix) const;
    void glMultMatrixf(const float *matrix) const;
    void glTranslatef(float x, float y, float z) const;
    void glRotatef(float angle, float x, float y, float z) const;
    void glScalef(float x, float y, float z) const;
    void glPushMatrix() const;
    void glPopMatrix() const;
    void glPushAttrib(unsigned int mask) const;
    void glPopAttrib() const;
    void glPushClientAttrib(unsigned int mask) const;
    void glPopClientAttrib() const;
    void glViewport(int x, int y, int width, int height) const;
    void glScissor(int x, int y, int width, int height) const;
    void glClearColor(float red, float green, float blue, float alpha) const;
    void glClear(unsigned int mask) const;
    void glClearDepth(double depth) const;
    void glClearStencil(int stencil) const;
    void glColorMask(unsigned char red, unsigned char green, unsigned char blue,
                     unsigned char alpha) const;
    void glStencilFunc(unsigned int function, int reference, unsigned int mask) const;
    void glStencilOp(unsigned int onFail, unsigned int onDepthFail, unsigned int onPass) const;
    void glTexEnvi(unsigned int target, unsigned int parameter, int value) const;
    void glTexEnvf(unsigned int target, unsigned int parameter, float value) const;
    void glFogf(unsigned int parameter, float value) const;
    void glFogfv(unsigned int parameter, const float *value) const;
    void glFogi(unsigned int parameter, int value) const;
    void glShadeModel(unsigned int mode) const;
    void glEnableClientState(unsigned int array) const;
    void glDisableClientState(unsigned int array) const;
    void glDrawArrays(unsigned int mode, int first, int count) const;
    void glVertexPointer(int size, unsigned int type, int stride, const void *pointer) const;
    void glColorPointer(int size, unsigned int type, int stride, const void *pointer) const;
    void glTexCoordPointer(int size, unsigned int type, int stride, const void *pointer) const;
    int DenyCrownRegistPopupClose(POPUP_RESULT result);                               // OMF-01143
    void ReceiveMapInfoResult(const BYTE *receiveBuffer);                             // OMF-01161
    void RecevieRaklionBattleResult(const BYTE *receiveBuffer);                       // OMF-01195
    void RecevieRaklionWideAreaAttack(const BYTE *receiveBuffer);                     // OMF-01196
    void RecevieRaklionUserMonsterCount(const BYTE *receiveBuffer);                   // OMF-01197
    bool ReceiveRequestMoveMap(const BYTE *receiveBuffer);                            // OMF-01210
    bool ReceiveIGS_SendCashGift(const BYTE *receiveBuffer);                          // OMF-01221
    bool ReceiveIGS_PossibleBuy(const BYTE *receiveBuffer);                           // OMF-01222
    bool ReceiveIGS_LeftCountItem(const BYTE *receiveBuffer);                         // OMF-01223
    bool ReceivePeriodItemListCount(const BYTE *receiveBuffer);                       // OMF-01229
    void SetCharacterScale(CHARACTER *character);                                     // OMF-00371
    bool IsBackItem(const CHARACTER *character, int type);                            // OMF-00389
    bool IsCanUseItem();                                                              // OMF-00501
    bool IsCanTrade();                                                                // OMF-00502
    int IsPurchaseShop();                                                             // OMF-01969
    bool CheckMouseIn(int x, int y, int width, int height) const;                     // OMF-01970
    BOOL CheckMouseIn(int x, int y, int width, int height, int coordinateType) const; // OMF-01867
    int GetScreenWidth();                                                             // OMF-00547
    void ConvertTaxGold(DWORD gold, wchar_t *text);                                   // OMF-00511
    int64_t ConvertRepairGold(int64_t gold, int durability, int maxDurability, short type,
                              wchar_t *text); // OMF-00515
    SessionItemStore *ItemStore() const;
    const ITEM *FindInventoryItemBySlot(const int slot) const; // OMF-00776
    void SetItemColor(int index, ITEM *inventory, int color);  // OMF-00549
    bool IsRepairBan(ITEM *item) const;                        // OMF-00562
    std::wstring GetItemDisplayName(ITEM *item);               // OMF-00568
    void InitPartyList();                                      // OMF-00576
    bool CreateGuildMark(int markIndex, bool blend = true);    // OMF-00588
    void CreateCastleMark(int type, BYTE *buffer = nullptr,
                          bool blend = true);                          // OMF-00589
    void HideKeyPad();                                                 // OMF-00578
    int CheckMouseOnKeyPad();                                          // OMF-00579
    int FindHotKey(int skill);                                         // OMF-00472
    bool CheckTile(CHARACTER *character, OBJECT *object, float range); // OMF-00444
    void CloseNPCGMWindow();                                           // OMF-00460
    bool SkillKeyPush(int skill);                                      // OMF-00467
    bool IsIllegalMovementByUsingMsg(const wchar_t *text);             // OMF-00493
    void ComputeItemInfo(int helpItem);                                // OMF-00507
    bool ReceivePeriodItemList(const BYTE *receiveBuffer);             // OMF-01230
    void ReceiveBCReg(const BYTE *receiveBuffer);                      // OMF-01124
    void ReceiveBCRegInfo(const BYTE *receiveBuffer);                  // OMF-01126
    void ReceiveBCRegMark(const BYTE *receiveBuffer);                  // OMF-01127
    bool HasAccountBlockedCharacter();                                 // OMF-00109
    bool HasEmptyCharacterSlot();                                      // OMF-00110
    bool HasLiveCharacter();                                           // OMF-00111
    CHARACTER *GetSelectedCharacter();                                 // OMF-00112
    void RenderAccountBlockMessage();                                  // OMF-00113
    void SetupItemUseStateMessage(int num, int index, int message);    // OMF-01803
    void SetupPersonalShopWarningMessage(int num, int index);          // OMF-01804
    void SetupChaosCastleCheckMessage(int num, int index);             // OMF-01805
    void SetupGemIntegrationMessage();                                 // OMF-01806
    void SetupCancelSkillMessage(int index);                           // OMF-01807
    void SetupGenericMessage(int num, int index);                      // OMF-01808
    void ConfigureMessageBoxButtons(int message);                      // OMF-01809
    BOOL ShowCheckBox(int num, int index, int message);                // OMF-01810
    int64_t ItemValue(ITEM *item, int goldType = 1) const;             // OMF-00422
    void PrintItem(wchar_t *fileName) const;                           // OMF-00405
    BOOL IsCorrectSkillType(INT skillSequence, eTypeSkill skillType);  // OMF-00400
    BOOL IsCorrectSkillType_FrendlySkill(INT skillSequence);           // OMF-00401
    BOOL IsCorrectSkillType_Buff(INT skillSequence);                   // OMF-00402
    BOOL IsCorrectSkillType_DeBuff(INT skillSequence);                 // OMF-00403
    BOOL IsCorrectSkillType_CommonAttack(INT skillSequence);           // OMF-00404
    bool IsHighValueItem(ITEM *item) const;                            // OMF-00552
    int GetExcellentAddValue(ITEM *item) const;                        // OMF-00411
    void CalcDamageMin(ITEM *item, ITEM_ATTRIBUTE *attribute,
                       int excellentAddValue) const; // OMF-00412
    void CalcDamageMax(ITEM *item, ITEM_ATTRIBUTE *attribute,
                       int excellentAddValue) const; // OMF-00413
    void CalcMagicPower(ITEM *item, ITEM_ATTRIBUTE *attribute,
                        int excellentAddValue) const;                         // OMF-00414
    void CalcSuccessfulBlocking(ITEM *item, ITEM_ATTRIBUTE *attribute) const; // OMF-00415
    void CalcDefense(ITEM *item, ITEM_ATTRIBUTE *attribute) const;            // OMF-00416
    void CalcRequirements(ITEM *item, ITEM_ATTRIBUTE *attribute) const;       // OMF-00417
    void CalcWingOptions(ITEM *item) const;                                   // OMF-00418
    void CalcExcellentOptions(ITEM *item) const;                              // OMF-00419
    void CalcPartType(ITEM *item) const;                                      // OMF-00420
    void SetItemAttributes(ITEM *item) const;                                 // OMF-00421
    bool IsRequireEquipItem(ITEM *item);                                      // OMF-00423
    void PlusSpecial(WORD *value, int special, ITEM *item);                   // OMF-00424
    void PlusSpecialPercent(WORD *value, int special, ITEM *item,
                            WORD percent);                          // OMF-00425
    void PlusSpecialPercent2(WORD *value, int special, ITEM *item); // OMF-00426
    WORD ItemDefense(ITEM *item);                                   // OMF-00427
    WORD ItemMagicDefense(ITEM *item);                              // OMF-00428
    WORD ItemWalkSpeed(ITEM *item);                                 // OMF-00429
    void RequireClass(ITEM_ATTRIBUTE *item);                        // OMF-00504
    bool IsTradeBan(ITEM *item);                                    // OMF-00554
    BYTE CaculateFreeTicketLevel(int type) const;                   // OMF-00593
    bool CheckUseMasterSkill(CHARACTER *character, int index);      // OMF-00863
    void UseBattleMasterSkill();                                    // OMF-00864
    bool CheckCharacterRange(OBJECT *source, float range, short pkKey,
                             BYTE kind = 0);                            // OMF-01465
    int SearchArrow() const;                                            // OMF-00442
    int SearchArrowCount() const;                                       // OMF-00443
    bool GetAttackDamage(int *minimumDamage, int *maximumDamage) const; // OMF-00522
    void RepairAllGold();                                               // OMF-00516
    void StartMatchCountDown(int type);                                 // OMF-00727
    void ClearMatchInfo();                                              // OMF-00728
    void SetMatchGameCommand(const LPPRECEIVE_MATCH_GAME_STATE data);   // OMF-00729
    void RenderTime();                                                  // OMF-00730
    void UpdateMatchState();                                            // OMF-00731
    void SetMatchResult(int resultCount, int myResult, MatchResult *results,
                        int success = 0); // OMF-00732
    void RenderResult();                  // OMF-00733
    void SetPosition(int x, int y);       // OMF-00734
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
    explicit SessionLegacyCalls(SessionKeeper &keeper) noexcept;

    BOOL CreateSocket(const wchar_t *ipAddress, unsigned short port);
    void DeleteSocket();
    void ReceiveServerConnectBusy(const BYTE *receiveBuffer);
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
    void StopMusic();
    void StopMp3(const char *name, BOOL enforce = false);
    void PlayMp3(const char *name, BOOL enforce = false);
    bool IsEndMp3();
    int GetMp3PlayPosition();
    HRESULT PlayBuffer(ESound buffer, OBJECT *object = nullptr, BOOL looped = false) const;
    void StopBuffer(ESound buffer, BOOL resetPosition);
    void AllStopSound();
    void Set3DSoundPosition();
    void CreateEventMatch(int world);
    bool CreateOkMessageBox(const std::wstring &message, DWORD color = 0xffffffff,
                            float priority = 3.0f) const;
    void DeleteEventMatch();
    void PlayBGM();
    bool CheckAbuseFilter(wchar_t *text, bool checkSlash = true);
    bool CheckAbuseNameFilter(wchar_t *text);
    bool CheckName();
    BOOL CheckOptionMouseClick(int optionPositionY, BOOL playClickSound = TRUE);
    void MovePersonalShop();
    void MoveServerDivisionInventory();
    void ClearInventory();
    void OpenPersonalShopMsgWnd(int messageType);
    bool IsCorrectShopTitle(const wchar_t *shopTitle);
    void EditObjects();
    void MoveObject(OBJECT *object);
    void MoveObjects();
    void MoveParticles();
    void MoveJoint(JOINT *joint, int index);
    void MoveJoints();
    void CreatePoint(vec3_t position, int value, vec3_t color, float scale = 15.f, bool move = true,
                     bool repeatedly = false); // OMF-01550
    void MovePoints();
    void CreatePointer(int type, vec3_t position, float angle, vec3_t light,
                       float scale = 1.f); // OMF-01554
    void MovePointers();
    void CheckSkull(OBJECT *object);

    void MoveEtcLeaf(PARTICLE *particle);
    void MoveAirLeaf(PARTICLE *particle, bool rotate, bool discreteNoise);
    bool MoveLeaves();
    bool MoveMainCamera();
    CameraManager &CameraManager_Instance();
    OrbitalCamera *GetOrbitalCameraInstance();
    void GetOrbitalCameraAngles(float *outYaw, float *outPitch);
    void GetActiveCameraConfig(float *outFOV, float *outNearPlane, float *outFarPlane,
                               float *outTerrainCullRange);
    void GenerateScreenshotFilename(wchar_t *outFileName, wchar_t *outMessage);
    void CaptureScreenshot();
    void HandleScreenshotCapture();
    void UpdateWaterAnimation();
    void RenderSprite(const OBJECT *object, const OBJECT *owner);
    void BeginOpengl(int x = 0, int y = 0, int width = 640, int height = 480);
    void EndOpengl(); // OMF-01719
    void glViewport2(int x, int y, int width, int height);
    void gluPerspective2(float fov, float aspect, float zNear, float zFar);
    void SaveCameraPerspective();
    void RestoreCameraPerspective();
    void UpdateMousePositionn();
    void CreateFrustrum2D(vec_t *position);
    void CreateFrustrum(float xAspect, float yAspect, vec_t *position);
    void CacheActiveFrustum();
    void RenderSprite(int texture, const vec3_t position, float width, float height,
                      const vec3_t light, float rotation = 0.f, float u = 0.f, float v = 0.f,
                      float uWidth = 1.f, float vHeight = 1.f);
    void RenderSpriteUV(int texture, vec3_t position, float width, float height, float (*uv)[2],
                        vec3_t light[4], float alpha = 1.f);
    void RenderNumber(const vec3_t position, int number, const vec3_t color, float alpha = 1.f,
                      float scale = 15.f);
    void RenderNumberPoints(const vec3_t position, int number, const vec3_t color, float alpha,
                            float scale);
    float RenderNumber2D(float x, float y, int number, float width, float height);
    void BeginBitmap();
    void EndBitmap();                                                  // OMF-01735
    void BeginSprite();                                                // OMF-01728
    void EndSprite();                                                  // OMF-01729
    void RenderBox(float matrix[3][4]);                                // OMF-01726
    void RenderPlane3D(float width, float height, float matrix[3][4]); // OMF-01727
    void RenderPointRotate(int texture, float ix, float iy, float iWidth, float iHeight, float x,
                           float y, float width, float height, float rotate, float rotateLocation,
                           float uWidth, float vHeight,
                           int number); // OMF-01742
    void RenderDebugSphere(const vec_t *center, float radius, float red, float green,
                           float blue); // OMF-01669
    void RenderDebugBox(const vec_t *origin, float sizeX, float sizeY, float sizeZ, float red,
                        float green, float blue); // OMF-01670
    void SetBooleanPosition(UI::Chat::CHAT *chat);
    void SetPlayerColor(BYTE playerKillLevel);
    void RenderBoolean(int x, int y, UI::Chat::CHAT *chat);
    void AddChat(UI::Chat::CHAT *chat, const wchar_t *chatText, int flag);
    void AddGuildName(UI::Chat::CHAT *chat, CHARACTER *owner);
    void CreateChat(wchar_t *characterName, const wchar_t *chatText, CHARACTER *owner, int flag = 0,
                    int setColor = -1);
    int CreateChat(wchar_t *characterName, const wchar_t *chatText, OBJECT *owner, int flag = 0,
                   int setColor = -1);
    void AssignChat(wchar_t *characterName, const wchar_t *chatText, int flag = 0);
    void MoveChat();
    void RenderBooleans();
    bool CheckLevel(int requiredLevel, wchar_t *targetId);
    void Register(int requiredLevel, wchar_t *targetId);
    void Clear();
    void RenderList();
    float RenderNumber(float x, float y, int number, float scale = 1.0f);
    void SendMove(CHARACTER *character, OBJECT *object);
    void SendCharacterMove(unsigned short key, float angle, unsigned char pathNum,
                           unsigned char *pathX, unsigned char *pathY, unsigned char targetX,
                           unsigned char targetY);
    void LetHeroStop(CHARACTER *character = nullptr, BOOL setMovementFalse = FALSE);
    void SetCharacterPos(CHARACTER *character, BYTE positionX, BYTE positionY, vec3_t position);
    void SendRequestAction(OBJECT &object, BYTE action);
    void SendRequestMagic(int type, int key);
    void SendRequestMagicContinue(int type, int x, int y, int angle, BYTE destination,
                                  BYTE targetPosition, WORD targetKey, BYTE *skillSerial);
    bool CheckCommand(wchar_t *text, bool macroText = false);
    void SendMacroChat(wchar_t *text);
    void MoveInterface();
    bool CheckAttack_Fenrir(CHARACTER *character);
    bool CheckWall(int sx1, int sy1, int sx2, int sy2); // OMF-00445
    bool IsGMCharacter();                               // OMF-00491
    bool IsNonAttackGM();                               // OMF-00492
    bool IsHeroSwingInProgress() const;                 // OMF-00873
    bool CheckAttack();
    bool CheckMonsterSkill(CHARACTER *character, OBJECT *object);
    bool SendPetCommand(CHARACTER *character, int index);
    void ClearRightMouseInputState();                    // OMF-00820
    CSPetSystem *ResolvePetSystem(CHARACTER *character); // OMF-00819
    void InitPetManager();                               // OMF-00821
    bool IsVirtualKeyPressed(int virtualKey);            // OMF-00818
    void MovePet(CHARACTER *character);                  // OMF-00824
    void RenderPet(CHARACTER *character);                // OMF-00825
    bool SelectPetCommand();                             // OMF-00826
    void SetPetCommand(CHARACTER *character, int key,
                       std::uint8_t command);                      // OMF-00829
    void SetAttack(CHARACTER *character, int key, int attackType); // OMF-00830
    void InitItemBackup();                                         // OMF-00833
    void SetPetInfo(std::uint8_t inventoryType, std::uint8_t inventoryPosition,
                    PET_INFO *petInfo);                          // OMF-00835
    PET_INFO *GetPetInfo(ITEM *item) const;                      // OMF-00836
    void CalcPetInfo(PET_INFO *petInfo);                         // OMF-00837
    void SetPetItemConvert(ITEM *item, PET_INFO *petInfo) const; // OMF-00838
    std::uint32_t GetPetItemValue(PET_INFO *petInfo) const;      // OMF-00839
    void DeletePet(CHARACTER *character);                        // OMF-00832
    void CreatePetDarkSpirit(CHARACTER *character);
    void CreatePetDarkSpirit_Now(CHARACTER *character);
    bool RenderPetCmdInfo(int sx, int sy, int type);
    bool RenderPetItemInfo(int sx, int sy, ITEM *item, int inventoryType);
    int getTargetCharacterKey(CHARACTER *character, int selected);
    bool IsCanBCSkill(int type);
    bool CheckSkillUseCondition(OBJECT *object, int type);
    void ReloadArrow();
    bool CheckArrow();
    void SendRequestUse(int index, int target, bool addPoints = true) const;
    bool SendRequestEquipmentItem(STORAGE_TYPE sourceType, int sourceIndex, ITEM *item,
                                  STORAGE_TYPE destinationType, int destinationIndex) const;
    bool PathFinding2(int sx, int sy, int tx, int ty, PATH_t *pathState, float distance = 0.0f,
                      int defaultWall = 0x0002);
    void AttackKnight(CHARACTER *character, ActionSkillType skill, float distance);
    void MoveMonsterClient(CHARACTER *character, OBJECT *object);
    void MoveCharacterClient(CHARACTER *character);
    void MoveCharactersClient();

    void DeleteBoids(); // OMF-00256
    void MoveBoids();
    bool CreateMountSub(int type, vec3_t position, OBJECT *owner, OBJECT *object, int subType = 0,
                        int linkBone = 0);
    void DeleteMount(OBJECT *owner); // OMF-00246
    void CreateMount(int type, vec3_t position, OBJECT *owner, int subType = 0, int linkBone = 0);
    bool MoveMount(OBJECT *object, bool forceRender = false, CHARACTER *owner = nullptr,
                   AnimationPoseSample *pose = nullptr);
    void MoveMounts();

    void MoveHeavenBug(OBJECT *object, int index);
    void MoveBoidGroup(OBJECT *object, int index);
    void MoveFishs();
    void RenderBoids(bool afterCharacter = false);
    void RenderFishs();
    bool RenderMount(const ObjectDrawInput &object, bool forceRender = false);
    void RenderCharacterAttachments();
    void MoveCharacter(CHARACTER *character, OBJECT *object);
    void AdvanceCharacterEnvironmentState(CHARACTER &character);
    void SetCharacterTarget(CHARACTER &character, int index);
    int FindCharacterIndex(int key);                // OMF-00364
    int FindCharacterIndexByMonsterIndex(int type); // OMF-00365
    int HangerBloodCastleQuestItem(int key);        // OMF-00366
    CHARACTER *FindCharacterByID(wchar_t *name);    // OMF-00376
    CHARACTER *FindCharacterByKey(int key);         // OMF-00377

    void ChangeCharacterExt(int key, BYTE *equipment, CHARACTER *character = nullptr,
                            OBJECT *helper = nullptr);
    void ReadEquipmentExtended(int key, BYTE flags, BYTE *equipment, CHARACTER *character = nullptr,
                               OBJECT *helper = nullptr);
    void AttackEffect(CHARACTER *character);
    void FallingCharacter(CHARACTER *character, OBJECT *object);
    void PushingCharacter(CHARACTER *character, OBJECT *object);
    void DeadCharacter(CHARACTER *character, OBJECT *object, BMD *model);
    void HeroAttributeCalc(CHARACTER *character);
    void AnimationCharacter(CHARACTER *character, OBJECT *object, BMD *model);
    void CreateWeaponBlur(CHARACTER *character, OBJECT *object, BMD *model);
    void SetPlayerAttack(CHARACTER *character);
    void SetAction(OBJECT *object, int action, bool blending = true);
    void SetActionClass(CHARACTER *character, OBJECT *object, int action, int actionType);
    void PushObject(vec3_t pushPosition, vec3_t position, float power, vec3_t angle); // OMF-00285
    void SetAction_Fenrir_Skill(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Damage(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Run(CHARACTER *character, OBJECT *object);
    void SetAction_Fenrir_Walk(CHARACTER *character, OBJECT *object);
    void SetAttackSpeed();
    void SetPlayerHighBowAttack(CHARACTER *character);
    void SetPlayerBow(CHARACTER *character);
    void SetPlayerHighBow(CHARACTER *character);
    void SetPlayerMagic(CHARACTER *character);
    void SetPlayerTeleport(CHARACTER *character);
    void SetAllAction(int action);
    void RegisterBone(CHARACTER *character, const std::wstring &name, int bone);
    void UnregisterBone(CHARACTER *character);
    void UnregisterAll();
    CHARACTER *GetOwnCharacter(OBJECT *object, const std::wstring &name);
    int GetBoneNumber(OBJECT *object, const std::wstring &name);
    CharacterDrawInput CharacterPresentationInput(const CHARACTER &character) const;
    bool GetBonePosition(const ObjectDrawInput &draw, int bone, vec3_t position) const;
    bool GetBonePosition(const ObjectDrawInput &draw, int bone, const vec3_t relative,
                         vec3_t position) const;
    bool GetBonePosition(const OBJECT *object, int bone, vec3_t position) const;
    bool GetBonePosition(const OBJECT *object, int bone, const vec3_t relative,
                         vec3_t position) const;
    bool GetBonePosition(OBJECT *pObject, const std::wstring &name, OUT vec3_t Position);
    bool GetBonePosition(OBJECT *pObject, const std::wstring &name, IN vec3_t Relative,
                         OUT vec3_t Position);
    void UpdateCharactersAnimationParallel(std::span<CHARACTER *const> characters);
    void SetPlayerStop(CHARACTER *character);
    bool AttackStage(CHARACTER *character, OBJECT *object);
    void OnlyNpcChatProcess(CHARACTER *character, OBJECT *object);
    void PlayerNpcStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void PlayerStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void PlayWalkSound();
    bool CheckFullSet(CHARACTER *character);
    float CharacterMoveSpeed(CHARACTER *character);
    void MoveCharacterPosition(CHARACTER *character);
    void ChangeChaosCastleUnit(CHARACTER *character);
    void AdvanceMapEffectVisual(OBJECT &object);
    void AdvanceBoidVisual(OBJECT &object);
    void PrepareWorldObjectPose(OBJECT &object, float fraction = 1.f);
    void CreateCharacterPointer(CHARACTER *character, int type, unsigned char positionX,
                                unsigned char positionY, float rotation = 0.0f);
    void SetCharacterClass(CHARACTER *character);
    CHARACTER *CreateCharacter(int key, int type, unsigned char positionX, unsigned char positionY,
                               float rotation = 0.0f);
    void SetChangeClass(CHARACTER *character);
    CHARACTER *CreateHero(int key, CLASS_TYPE characterClass, int skin = 0, float x = 0.0f,
                          float y = 0.0f, float rotation = 0.0f);
    const vec34_t *RenderLinkObject(float x, float y, float z, const CharacterDrawInput &character,
                                    const PART_t *part, int type, int level, int option1, bool link,
                                    bool translate, int renderType = 0, bool rightHandItem = true,
                                    int slot = -1);
    void RenderBrightEffect(BMD *model, int bitmap, int link, float scale, vec_t *light,
                            OBJECT *object); // OMF-00354
    bool RenderCharacterBackItem(const CharacterDrawInput &character, bool translate);
    void ActionObject(OBJECT *object);
    void BodyLight(const ObjectDrawInput &object, BMD *model);
    vec34_t *AllocateDrawPose(int boneCount);
    bool Calc_RenderObject(ObjectDrawInput &object, bool translate, int select, int extraMonster);
    bool Calc_ObjectAnimation(ObjectDrawInput &object, bool translate, int select);
    void Draw_RenderObject(const ObjectDrawInput &object, bool translate, int select,
                           int extraMonster);
    const vec34_t *RenderObject(const ObjectDrawInput &object, bool translate = false,
                                int select = 0, int extraMonster = 0) const;
    void CreatePartsFactory(CHARACTER *character);
    void RenderParts(const CHARACTER *character);
    void DeleteParts(CHARACTER *character);
    void ClearCharacters(int key = -1);
    void DeleteCharacter(int key);
    void DeleteCharacter(CHARACTER *character, OBJECT *object);
    void ReleaseCharacters();
    const vec34_t *RenderObject_AfterImage(const ObjectDrawInput &object, bool translate = false,
                                           int select = 0, int extraMonster = 0);
    void RenderCharacter_AfterImage(const CharacterDrawInput &character, const PART_t *part,
                                    bool translate = false, int select = 0,
                                    float animationInterval1 = 1.4f,
                                    float animationInterval2 = 0.7f);
    void RenderObject_AfterCharacter(const ObjectDrawInput &object, bool translate = false,
                                     int select = 0, int extraMonster = 0);
    void Draw_RenderObject_AfterCharacter(const ObjectDrawInput &object, bool translate = false,
                                          int select = 0, int extraMonster = 0);
    void RenderObjects_AfterCharacter();

    const PkFieldDetail::MonsterDefinition *FindMonsterDefinition(int monsterType);
    bool IsIceCity();
    bool IsSantaTown();
    bool IsDuelArena();
    bool IsDoppelGanger1();
    bool IsDoppelGanger2();
    bool IsDoppelGanger3();
    bool IsDoppelGanger4();
    bool IsUnitedMarketPlace() const;
    bool IsKarutanMap();

    void ItemObjectAttribute(OBJECT *object);
    void HandleItemFalling(OBJECT *object);
    void MoveItems();
    void CreateOperate(OBJECT *owner);                                          // OMF-00615
    void ClearItems();                                                          // OMF-00625
    BYTE MakeSkillSerialNumber(BYTE *serialNumber);                             // OMF-00453
    void SortInBlockByType();                                                   // OMF-00618
    void DeleteObjectTile(int x, int y);                                        // OMF-00619
    bool SaveObjects(wchar_t *fileName, int mapNumber);                         // OMF-00622
    void CreateShadowAngle();                                                   // OMF-00653
    void ClearActionObject();                                                   // OMF-00594
    void SetActionObject(int world, int type, int lifeTime, int velocity = -1); // OMF-00595
    void SaveMacro(const wchar_t *fileName);                                    // OMF-00675
    void OpenMacro(const wchar_t *fileName);                                    // OMF-00676
    void CreateBlur(CHARACTER *owner, vec3_t first, vec3_t second, vec3_t light, int type,
                    bool shortBlur = false, int subType = 0); // OMF-01498
    void MoveBlurs();                                         // OMF-01499
    void ClearAllObjectBlurs();                               // OMF-01501
    void CreateObjectBlur(OBJECT *owner, vec3_t first, vec3_t second, vec3_t light, int type,
                          bool shortBlur = false, int subType = 0,
                          int limitLifeTime = -1);          // OMF-01503
    void MoveObjectBlurs();                                 // OMF-01504
    void RemoveObjectBlurs(OBJECT *owner, int subType = 0); // OMF-01506
    int CreateSprite(int type, const vec_t *position, float scale, const vec_t *light,
                     const OBJECT *owner, float rotation = 0.f, int subType = 0) const; // OMF-01559
    void InsertShadowVolume(CShadowVolume *shadowVolume);                               // OMF-01571
    void InitCollisionDetectLineToFace();                                               // OMF-01750
    bool CollisionDetectLineToFace(vec_t *position, vec_t *target, int polygon, float *vertex1,
                                   float *vertex2, float *vertex3, float *vertex4, vec_t *normal,
                                   bool collision = true); // OMF-01751
    OBJECT *CreateObject(int type, vec3_t position, vec3_t angle, float scale = 1.0f);
    void SaveTrapObjects(wchar_t *fileName);
    void RenderCloudLowLevel(int index, int type);
    void RenderItems();
    void NextGradeObjectRender(const CharacterDrawInput &character);
    void RenderBoundingBox(const OBJECT *object);
    void RenderPartObjectEffect(const ObjectDrawInput &object, int type, const vec3_t light,
                                float alpha = 0.0f, int level = 0, int excellentFlags = 0,
                                int ancientDiscriminator = 0, int select = 0,
                                int renderType = 0x00000002);
    void RenderPartObjectBody(BMD *model, const ObjectDrawInput &object, int type, float alpha,
                              int renderType);
    void RenderPartObjectBodyColor(BMD *model, const ObjectDrawInput &object, int type, float alpha,
                                   int renderType, float bright, int texture = -1,
                                   int monsterIndex = -1);
    void RenderPartObjectBodyColor2(BMD *model, const ObjectDrawInput &object, int type,
                                    float alpha, int renderType, float bright, int texture = -1);
    void GetSpecialOptionText(int type, wchar_t *text, WORD option, BYTE value, int mana);
    void PartObjectColor(int type, float alpha, float bright, vec3_t light,
                         bool extraMonster = false);
    void PartObjectColor2(int type, float alpha, float bright, vec3_t light,
                          bool extraMonster = false);
    void RenderPartObjectEdgeLight(BMD *model, const ObjectDrawInput &object, int flags,
                                   bool translate, float scale);
    void RenderPartObjectEdge(BMD *model, const ObjectDrawInput &object, int flags, bool translate,
                              float scale); // OMF-00646
    void RenderPartObjectEdge2(BMD *model, const ObjectDrawInput &object, int flags, bool translate,
                               float scale, OBB_t *obb); // OMF-00647
    void RenderPartObject(const ObjectDrawInput &object, int type, const PART_t *data,
                          const vec3_t light, float alpha = 0.0f, int level = 0,
                          int excellentFlags = 0, int ancientDiscriminator = 0,
                          bool globalTransform = false, bool hideSkin = false,
                          bool translate = false, int select = 0, int renderType = 0x00000002);
    bool CharacterAnimation(CHARACTER *character, OBJECT *object);
    void EtcStopAnimationSetting(CHARACTER *character, OBJECT *object);
    void SetPlayerWalk(CHARACTER *character);
    BOOL PlayMonsterSound(OBJECT *object);
    void SetPlayerShock(CHARACTER *character, int hit);
    void SetPlayerDie(CHARACTER *character);
    void AttackWizard(CHARACTER *character, int skill, float distance);
    void AttackRagefighter(CHARACTER *character, int skill, float distance);
    bool CheckTarget(CHARACTER *character);
    bool CanExecuteSkill(CHARACTER *character, ActionSkillType skill, float distance);
    void CheckChatText(wchar_t *text);
    void AttackCommon(CHARACTER *character, int skill, float distance);
    void UseSkillWizard(CHARACTER *character, OBJECT *object);
    void UseSkillSummon(CHARACTER *character, OBJECT *object);
    void UseSkillRagefighter(CHARACTER *character, OBJECT *object);
    bool UseSkillRagePosition(CHARACTER *character);
    bool CheckMana(CHARACTER *character, int skill);
    void Setting_Monster(CHARACTER *character, EMonsterType type, int positionX, int positionY);
    CHARACTER *CreateMonster(EMonsterType type, int positionX, int positionY, int key = 0);
    bool IsInAida();
    bool IsInAidaSection2(const vec3_t position);
    bool IsInHuntingGround();
    bool IsInHuntingGroundSection2(const vec3_t position);
    bool IsGmArea();
    bool IsKanturu1st();
    bool IsInKanturu3rd();
    void Kanturu3rdInit();
    bool IsSuccessBattle();
    void CheckSuccessBattle(BYTE state, BYTE detailState);
    void MayaSceneMayaAction(BYTE skill);
    void Kanturu3rdState(BYTE state, BYTE detailState);
    void Kanturu3rdResult(BYTE result);
    void Kanturu3rdUserandMonsterCount(int monsterCount, int userCount);
    CHARACTER *CreateHellGate(char *id, int key, EMonsterType index, int x, int y, int createFlag);
    const wchar_t *getMonsterName(int type) const;
    void MonsterConvert(MONSTER *monster, int level);
    void GetItemName(int type, int level, wchar_t *text) const;
    void RenderDebugWindow();
    int RenderDebugText(int y);
    void RenderItemInfo(int x, int y, ITEM *item, bool sell, int inventoryType = 0,
                        bool itemTextListBoxUse = false) const;
    void RenderTipTextList(const int x, const int y, int textCount, int tab, int sort = 3,
                           int renderPoint = 0, BOOL useBackground = TRUE);
    void RenderHelpLine(int columnType, const wchar_t *printStyle, int &tabSpace,
                        const wchar_t *gapText = nullptr, int positionY = 0, int type = 0);
    void RenderRepairInfo(int x, int y, ITEM *item, bool sell) const;
    void BuildGroundItemLabelDescriptor(OBJECT *object, ITEM *item,
                                        ItemRulesDetail::GroundItemLabelDescriptor &descriptor);
    void RenderGroundItemLabelTexture(OBJECT *object, const GroundItemLabelCacheEntry &cacheEntry);
    bool RenderGroundItemLabelCached(OBJECT *object, ITEM *item);
    void RenderObjectScreen(int type, int itemLevel, int excellentFlags, int ancientDiscriminator,
                            vec3_t target, int select, bool pickUp);
    void RenderItem3D(float x, float y, float width, float height, int type, int level,
                      int excellentFlags, int ancientDiscriminator, bool pickUp = false,
                      float presentationScale = 1.0f, bool useSlotTrs = false);
    void RenderEqiupmentBox();
    void RenderGuildList(int startX, int startY);
    void RenderServerDivision();
    void RenderInventoryInterface(int startX, int startY, int flag);
    void RenderGuildColor(float x, float y, int sizeX, int sizeY, int index);
    int SelectItem();
    int SelectCharacter(BYTE kind);
    int SelectOperate();
    void EmitMeshEffects(BMD &model, int mesh, int type, int subtype = 0, vec3_t angle = nullptr,
                         void *owner = nullptr);
    void SelectObjects();
    bool CharacterVisibleToObserver(const CHARACTER &character);
    bool IsCursorOnLegacyUi();
    void RenderItemName(int index, OBJECT *object, ITEM *item, bool sort);
    void RenderFrameGraph(float graphX, float graphY, float graphW, float graphH);
    void RenderDebugInfo();
    void RenderFpsCounter();
    void RenderCharacter(const CHARACTER *character, const OBJECT *object, int select = 0);
    void BuildCharacterScenePickOBB(const OBJECT *object, OBB_t &outObb);
    void RenderGuild(const ObjectDrawInput &object, int type = -1, vec3_t position = nullptr);
    void RenderCharactersClient();
    void RenderLight(const ObjectDrawInput &object, int texture, float scale, int bone, float x,
                     float y, float z);
    void RenderEye(const ObjectDrawInput &object, int left, int right, float size = 1.0f);
    bool CreateWaterTerrain(int mapIndex);
    bool IsWaterTerrain();
    void AddWaterWave(int x, int y, int range, int height);
    void MoveWaterTerrain();
    bool RenderWaterTerrain();
    void DeleteWaterTerrain();
    float GetWaterTerrain(float x, float y);
    void RenderWaterTerrain(int texture, float x, float y, float sizeX, float sizeY,
                            const vec3_t light, float rotation = 0.0f, float alpha = 1.0f,
                            float height = 0.0f);
    void SettingHellasColor();
    BYTE GetHellasLevel(CLASS_TYPE characterClass, int level);
    bool EnableKalima(CLASS_TYPE characterClass, int level, int itemLevel);
    bool GetUseLostMap(bool drawAlert = false);
    int RenderHellasItemInfo(ITEM *item, int textNumber);
    void RenderJoints(BYTE bRenderOneMore = 0);
    void RenderParticles(BYTE byRenderOneMore = 0);
    void CreateEffectFpsChecked(int type, vec3_t position, vec3_t angle, vec3_t light,
                                int subType = 0, OBJECT *owner = nullptr, short pkKey = -1,
                                WORD skillIndex = 0, WORD skill = 0, WORD skillSerialNumber = 0,
                                float scale = 0.0f, int targetIndex = -1);
    void CreateEffect(int type, vec3_t position, vec3_t angle, vec3_t light, int subType = 0,
                      OBJECT *owner = nullptr, short pkKey = -1, WORD skillIndex = 0,
                      WORD skill = 0, WORD skillSerialNumber = 0, float scale = 0.0f,
                      int targetIndex = -1);
    void CreateJointFpsChecked(int type, vec3_t position, vec3_t targetPosition, vec3_t angle,
                               int subType = 0, OBJECT *target = nullptr, float scale = 10.0f,
                               short pkKey = -1, WORD skillIndex = 0, WORD skillSerialNumber = 0,
                               int characterIndex = -1, const float *priorColor = nullptr,
                               short targetIndex = -1);
    void CreateJoint(int type, vec3_t position, vec3_t targetPosition, vec3_t angle,
                     int subType = 0, OBJECT *target = nullptr, float scale = 10.0f,
                     short pkKey = -1, WORD skillIndex = 0, WORD skillSerialNumber = 0,
                     int characterIndex = -1, const float *priorColor = nullptr,
                     short targetIndex = -1);
    void DeleteJoint(int type, OBJECT *target, int subType = -1); // OMF-01530
    bool SearchJoint(int type, OBJECT *target, int subType = -1); // OMF-01531
    int CreateParticleFpsChecked(int type, vec3_t position, vec3_t angle, vec3_t light,
                                 int subType = 0, float scale = 1.0f, OBJECT *owner = nullptr);
    int CreateParticle(int type, vec3_t position, vec3_t angle, vec3_t light, int subType = 0,
                       float scale = 1.0f, OBJECT *owner = nullptr);
    void HandPosition(PARTICLE *particle);
    OBJECT *CollisionDetectObjects(OBJECT *pickObject);
    void GetMagicScrew(int parameter, vec3_t result, float speedRate = 1.0f);
    void MoveParticle(OBJECT *object, int turn);
    void MoveParticle(OBJECT *object, vec3_t angle);
    bool MoveJump(OBJECT *object);
    void MoveEffect(OBJECT *object, int index);
    void MoveEffects();
    void CheckClientArrow(OBJECT *object);
    void RenderWheelWeapon(OBJECT *object);
    void RenderFuryStrike(OBJECT *object);
    void RenderSkillSpear(OBJECT *object);
    void RenderSprites(BYTE byRenderOneMore = 0);
    void RenderShadowVolumesAsFrame();
    void ShadeWithShadowVolumes();
    void RenderShadowToScreen();
    void RenderBlurs();
    void RenderObjectBlurs();
    void RenderEffects(bool bRenderBlendMesh = false);
    void RenderAfterEffects(bool renderBlendMesh = false);
    void RenderEffectShadows();
    void RenderCircle(int type, const vec3_t objectPosition, float scaleBottom, float scaleTop,
                      float height, float rotation = 0.f, float lightTop = 1.f,
                      float textureV = 0.f);
    void RenderLeaves();
    void RenderPoints(BYTE byRenderOneMore = 0);
    void CreateMyGensInfluenceGroundEffect();
    void RenderPointers();
    void RenderTerrain(bool editFlag);
    void RenderTerrainBitmapTile(float x, float y, float lodFactor, int lod, vec3_t coordinates[4],
                                 bool lightEnable, float alpha, float height = 0.f);
    void RenderTerrainBitmap(int texture, int mapX, int mapY, float rotation);
    void RenderTerrainAlphaBitmap(int texture, float x, float y, float sizeX, float sizeY,
                                  const vec3_t light, float rotation = 0.f, float alpha = 1.f,
                                  float height = 5.f);
    void SetupCharacterSceneViewport(int &outWidth, int &outHeight);
    void SetupMainSceneViewport(int &outWidth, int &outHeight, BYTE &outByWaterMap,
                                vec_t *cameraPos);
    void ApplySelectedCharacterLighting();
    void RenderSelectedCharacterEffects();
    void RenderInfomation3D();
    void RenderInfomation();
    void RenderInputText(int x, int y, int index, int gold = 0);
    void RenderBar(float x, float y, float width, float height, float bar, bool disabled = false,
                   bool clipping = true);
    void RenderSwichState();
    void RenderTournamentInterface();
    void RenderPartyHP();
    void BackSelectModel();
    void ForwardSelectModel();
    void RenderInterface(bool render);
    void RenderOutSides();
    void RenderTimes();
    void RenderCursor();
    void RenderTipText(int x, int y, const wchar_t *text);
    DWORD CreateUIID(); // OMF-01866
    int CutStr(const wchar_t *source, wchar_t *output, const int targetPixelWidth,
               const int maxOutputLines, const int outputLength, const int firstLineTab = 0);
    int CutText3(const wchar_t *text, wchar_t *output, const int targetWidth,
                 const int maxOutputLines, const int outputLength, const int firstLineTab = 0,
                 const BOOL reverseWrite = FALSE);
    int DivideStringByPixel(wchar_t *output, int outputRows, int outputColumns,
                            const wchar_t *source, int pixelsPerLine, bool insertSpace = true,
                            const wchar_t newlineCharacter = L';');
    void ResetWantedList(); // OMF-00739
    void RenderGoldRect(float x, float y, float width, float height, int fillType = 0);
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height) const;
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV) const;
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV, DWORD color) const;
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV, float uWidth, float vHeight,
                     DWORD color = 0xffffffff) const;
    void RenderImageStretch(std::uint32_t imageType, float x, float y, float width, float height,
                            float sourceX, float sourceY, float sourceWidth, float sourceHeight,
                            DWORD color = 0xffffffff) const;
    void SetLineColor(int type, float alphaRate = 1.0f);
    void RenderWindowVLine(float x, float y, float height);
    void RenderWindowHLine(float x, float y, float width);
    void RenderTabLine(int x, int y, int tabWidth, int tabHeight, int tabCount, int selectedTab);
    void RenderCheckBox(int x, int y, BOOL selected);
    void RenderFace(int texture, int mapX, int mapY);
    void RenderFace_After(int texture, int mapX, int mapY);
    void RenderFaceAlpha(int texture, int mapX, int mapY);
    void RenderFaceBlend(int texture, int mapX, int mapY);
    void RenderTerrainFace(float x, float y, int mapX, int mapY, float lodFactor);
    void RenderTerrainFace_After(float x, float y, int mapX, int mapY, float lodFactor);
    bool RenderTerrainTile(float x, float y, int mapX, int mapY, float lodFactor, int lod,
                           bool flag);
    void RenderTerrainTile_After(float x, float y, int mapX, int mapY, float lodFactor, int lod,
                                 bool flag);
    void RenderTerrainBlock(float x, float y, int mapX, int mapY, bool editFlag);
    void RenderTerrainFrustrum(bool editFlag);
    void RenderTerrainBlock_After(float x, float y, int mapX, int mapY, bool editFlag);
    void RenderTerrainFrustrum_After(bool editFlag);
    void RenderTerrain_After(bool editFlag);
    void RenderSun();
    void RenderSky();
    void SetClearAndFogColor(float red, float green, float blue);
    void SetWorldClearColor();
    void MainScene();
    bool OpenFont();
    void InitPath(); // OMF-00298
    void SaveOptions();
    void OpenBasicData();
    SplashSceneDetail::BackgroundTheme SelectBackgroundTheme();
    void LoadBackgroundTheme(SplashSceneDetail::BackgroundTheme theme);
    void LoadCommonTitleBitmaps();
    void UnloadTitleBitmaps();
    void WebzenScene();
    void OpenModel(int type, wchar_t *directory, wchar_t *modelFileName, ...);
    void OpenModels(int model, wchar_t *fileName, int index);
    void OpenPlayers();
    void OpenPlayerTextures();
    void OpenItems();
    void OpenItemTextures();
    void OpenNpc(int type);
    void DeleteNpcs();
    void SetMonsterSound(int type, int sound1, int sound2, int sound3, int sound4, int sound5,
                         int sound6 = -1, int sound7 = -1, int sound8 = -1, int sound9 = -1,
                         int sound10 = -1);
    void DeleteMonsters();
    bool OpenMonsterModel(EMonsterModelType type);
    void OpenSkills();
    void OpenSounds();
    void OpenImages();
    void ConfigureCharacterSceneModels();
    void ReleaseCharacterSceneData();
    void MoveCharacterCamera(vec_t *origin, vec_t *position, vec_t *angle);
    int GetLoginCameraCount();        // OMF-01772
    int GetLoginCameraWalkCut();      // OMF-01773
    void InitializeLoginCamera();     // OMF-01776
    void CalculateWalkDelta();        // OMF-01777
    void SelectNextWaypoint();        // OMF-01778
    void UpdateCameraWaypoint();      // OMF-01779
    void InterpolateCameraMovement(); // OMF-01780
    void MoveCamera();

    bool SaveTerrainAttribute(wchar_t *fileName, int mapNumber);
    void CreateTerrain(const WorldImageData &data, bool extended);
    void InstallTerrainMapping(const WorldFileData &data);
    void InstallTerrainAttributes(const WorldFileData &data);
    void InstallTerrainLight(const WorldImageData &data);
    void InstallWorldPlacements(const WorldFileData &data);
    void SetTerrainWaterState(std::list<int> &terrainIndex, int state);
    void CreateTerrainLight();
    void SaveTerrainHeight(wchar_t *fileName);
    void FaceTexture(int texture, float x, float y, bool water, bool scale);
    void InitTerrainLight();
    void AddTerrainLight(float x, float y, vec3_t light, int range, vec3_t *buffer);
    bool Set_CurrentAction_Kanturu2nd_Monster(CHARACTER *character, OBJECT *object);
    void Sound_Kanturu2nd_Object(OBJECT *object);
    bool Is_Kanturu2nd();
    bool Is_Kanturu2nd_3rd();
    MapProcess &TheMapProcess() const;
    void AdvanceDarkHorseSkill(OBJECT *object, BMD *model, bool emit = true);
    void AdvanceSkillEarthQuake(CHARACTER *character, OBJECT *object, BMD *model,
                                WorldCharacterVisualState &visual, int maxSkill);
    void MoveBat(OBJECT *object);
    void MoveButterFly(OBJECT *object);
    void MoveBird(OBJECT *object);
    void MoveEagle(OBJECT *object);
    void MoveTornado(OBJECT *object);
    float MoveHumming(vec3_t position, vec3_t angle, vec3_t targetPosition, float turn);
    void MovePosition(vec3_t position, vec3_t angle, vec3_t speed);
    void MoveBoid(OBJECT *object, int index, OBJECT *boids, int maximum);
    bool rand_fps_check(int referenceFrames);
    int WorldRandom();
    void GetNearRandomPos(vec3_t position, int range, vec3_t result);
    double WorldSimulationTime() const noexcept;
    void MayaAction(OBJECT *object, BMD *model);
    void Kanturu3rdSuccess();
    void Kanturu3rdFailed();
    bool CollisionEffectToObject(OBJECT *effect, float range, float rangeZ, bool collisionGround,
                                 bool realCollision = false);
    void RenderAurora(int type, int renderType, float x, float y, float sizeX, float sizeY,
                      const vec3_t light);
    bool IsBattleCastleStart();
    bool InBattleCastle2(vec3_t position);
    bool InBattleCastle3(vec3_t position);
    void SetBattleCastleStart(bool result);
    bool InArea(float x, float y, vec3_t position, float range);
    void CollisionHeroCharacter(vec3_t position, float range, int animationType);
    void CollisionTempCharacter(vec3_t position, float range, int animationType);
    bool CalcDistanceChrToChr(OBJECT *object, BYTE type, float range);
    void SetCastleGate_Attribute(int x, int y, BYTE operation, bool allClear = false);
    void SetBuildTimeLocation(OBJECT *object);
    bool SettingBattleFormation(CHARACTER *character, eBuffState state);
    bool GetGuildMaster(CHARACTER *character);
    void SettingBattleKing(CHARACTER *character);
    void DeleteBattleFormation(CHARACTER *character, eBuffState state);
    void ChangeBattleFormation(wchar_t *guildName, bool effect = false);
    void DeleteTmpCharacter();
    void StartFog(vec3_t color);
    void EndFog();
    bool CreateFireSnuff(PARTICLE *particle);
    void SetAttackDefenseObjectType(OBJECT *object);
    void MoveFlyBigStone(OBJECT *object);
    bool SettingBattleCastleMonsterLinkBone(CHARACTER *character, int type);
    bool StopBattleCastleMonster(CHARACTER *character, OBJECT *object);
    void InitGateAttribute();
    void CreateGuardStoneHealingVisual(CHARACTER *character, float range);
    void EmitMonsterHitEffect(OBJECT *object);
    bool IsCyringWolf2nd();
    void CryWolfMVPInit();
    bool IsCyrWolf1st();
    bool Get_State_Only_Elf() const;
    void Set_Message_Box(int stringId, int number, int key, int objectNumber = -1);
    void Set_Val_Hp(int state);
    void RenderObjectDescription();
    void AddObjectDescription(wchar_t *text, vec3_t position);
    void CheckGrass(OBJECT *object);
    int CreateBigMon(OBJECT *object);
    void MoveBigMon(OBJECT *object);
    void CreateMonsterSkill_ReduceDef(OBJECT *object, int attackTime, BYTE time, float height);
    void CreateMonsterSkill_Poison(OBJECT *object, int attackTime, BYTE time);
    void CreateMonsterSkill_Summon(OBJECT *object, int attackTime, BYTE time);
    void SetActionDestroy_Def(OBJECT *object);

    bool SettingHellasMonsterLinkBone(CHARACTER *character, int type);
    void MonsterMoveWaterSmoke(OBJECT *object);
    void MonsterDieWaterSmoke(OBJECT *object);
    template <typename T> T &TheWorld(int type);
    void ParseNodes();                                      // OMF-01575
    void ParseSkeleton();                                   // OMF-01576
    void ParseTriangles(bool flip);                         // OMF-01577
    void FixupSMD();                                        // OMF-01581
    void InitTerrainMappingLayer();                         // OMF-01593
    void AddTerrainAttribute(int x, int y, BYTE attribute); // OMF-01597
    void SubTerrainAttribute(int x, int y, BYTE attribute); // OMF-01598
    void AddTerrainAttributeRange(int x, int y, int width, int height, BYTE attribute,
                                  BYTE add = 0);                // OMF-01599
    bool SaveTerrainMapping(wchar_t *fileName, int mapNumber);  // OMF-01602
    void CreateTerrainNormal();                                 // OMF-01603
    void CreateTerrainNormal_Part(int x, int y);                // OMF-01604
    void CreateTerrainLight_Part(int x, int y);                 // OMF-01606
    void SaveTerrainLight(wchar_t *fileName);                   // OMF-01608
    float RequestTerrainHeight(float x, float y);               // OMF-01614
    void RequestTerrainNormal(float x, float y, vec3_t normal); // OMF-01615
    void RequestTerrainLight(float x, float y, vec3_t light);   // OMF-01620
    void CalcShadowPosition(vec3_t *position, const vec3_t origin, const float scaleX,
                            const float scaleY) const; // OMF-01586
    void GetClothShadowPosition(vec3_t *target, const CPhysicsCloth *cloth, const int index,
                                const vec3_t origin, const float scaleX,
                                const float scaleY) const; // OMF-01587
    void Vertex0();                                        // OMF-01628
    void Vertex1();                                        // OMF-01629
    void Vertex2();                                        // OMF-01630
    void Vertex3();                                        // OMF-01631
    void Vertex01();                                       // OMF-01632
    void Vertex12();                                       // OMF-01633
    void Vertex23();                                       // OMF-01634
    void Vertex30();                                       // OMF-01635
    void Vertex02();                                       // OMF-01636
    void VertexAlpha0();                                   // OMF-01637
    void VertexAlpha1();                                   // OMF-01638
    void VertexAlpha2();                                   // OMF-01639
    void VertexAlpha3();                                   // OMF-01640
    void VertexAlpha01();                                  // OMF-01641
    void VertexAlpha12();                                  // OMF-01642
    void VertexAlpha23();                                  // OMF-01643
    void VertexAlpha30();                                  // OMF-01644
    void VertexAlpha02();                                  // OMF-01645
    void VertexBlend0();                                   // OMF-01646
    void VertexBlend1();                                   // OMF-01647
    void VertexBlend2();                                   // OMF-01648
    void VertexBlend3();                                   // OMF-01649
    void ComputeIterationBoundsFromHull();                 // OMF-01662
    void BuildHull2DAndBounds(const float *pointsX, const float *pointsY,
                              int pointCount);             // OMF-01663
    void ExpandHullOutward(float offset);                  // OMF-01664
    void ResetFrustrumBoundsFullTerrain();                 // OMF-01668
    bool TestFrustrum2D(float x, float y, float range);    // OMF-01672
    bool TestFrustrum(const vec3_t position, float range); // OMF-01673
    void CreateSun();                                      // OMF-01684
    BYTE TERRAIN_ATTRIBUTE(float x, float y);              // OMF-01689
    bool OpenSMDFile(wchar_t *fileName, int type, bool flip);
    bool OpenSMDModel(int id, wchar_t *fileName, int actions = 1, bool flip = false);
    bool OpenSMDAnimation(int id, wchar_t *fileName, bool lockPosition = false);
    void SMD2BMDModel(int id, int actions);
    void SMD2BMDAnimation(int id, bool lockPosition);
    bool AddShopTitle(int key, CHARACTER *player, const std::wstring &title);
    void RemoveShopTitle(CHARACTER *player);
    void RemoveAllShopTitle();
    void ClosePersonalShop();
    void ClearPersonalShop();
    void RemoveAllShopTitleExceptHero();
    CHARACTER *FindCharacterTagShopTitle(int key);
    void ShowShopTitles();
    void HideShopTitles();
    void EnableShopTitleDraw(CHARACTER *player);
    void DisableShopTitleDraw(CHARACTER *player);
    bool IsShopTitleVisible(CHARACTER *player);
    bool IsShopInViewport(CHARACTER *player);
    void GetShopTitle(CHARACTER *player, std::wstring &title);
    void GetShopTitleSummary(CHARACTER *player, std::wstring &summary);
    void UpdatePersonalShopTitleImp();
    void DrawPersonalShopTitleImp();
    bool CreateCharacterScene();
    void NewMoveCharacterScene();
    void StartGame();
    void ManageMainSceneAudio();
    void ReceiveCreateTransformViewport(std::span<const BYTE> receiveBuffer);
    void ReceiveCreateSummonViewport(const BYTE *receiveBuffer);
    void ReceiveDarkside(const BYTE *receiveBuffer);
    void ReceiveRaklionStateInfo(const BYTE *receiveBuffer);
    void ReceiveRaklionCurrentState(const BYTE *receiveBuffer);
    void RecevieRaklionStateChange(const BYTE *receiveBuffer);
    bool ReceiveEnterEmpireGuardianEvent(const BYTE *receiveBuffer);
    void ReceiveGetItem(std::span<const BYTE> receiveBuffer);
    void ReceivePartyGetItem(const BYTE *receiveBuffer);
    void ReceiveNPCDlgUIStart(const BYTE *receiveBuffer);
    void ReceivePreviewPort(std::span<const BYTE> receiveBuffer);
    bool ReceiveIGS_UpdateScript(const BYTE *receiveBuffer);
    void ReceiveServerList(const BYTE *receiveBuffer);
    void ReceiveCharacterListExtended(const BYTE *receiveBuffer);
    void ReceiveCharacterCard_New(const BYTE *receiveBuffer);
    void ReceiveCreateCharacter(const BYTE *receiveBuffer);
    BOOL ReceiveLogOut(const BYTE *receiveBuffer, BOOL encrypted);
    void ReceiveRevival(const BYTE *receiveBuffer);
    void ReceiveMagicList(const BYTE *receiveBuffer);
    void Receive_Master_SetSkillList(PMSG_MASTER_SKILL_LIST_SEND *message);
    void ReceiveMuHelperConfigurationData(std::span<const BYTE> receiveBuffer);
    void ResetClientToLoginScene();
    void ReceiveConfirmPassword(const BYTE *receiveBuffer);
    void ReceiveConfirmPassword2(const std::span<const BYTE> receiveBuffer);
    void ReceiveChangePassword(const BYTE *receiveBuffer);
    void ReceiveDeleteInventory(const BYTE *receiveBuffer);
    int CalcItemLength(std::span<const BYTE> receiveBuffer);
    BOOL ReceiveInventoryExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveTradeInventoryExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveEquipment(std::span<const BYTE> receiveBuffer);
    void ReceiveChat(const BYTE *receiveBuffer);
    void ReceiveChatKey(const BYTE *receiveBuffer);
    void InitGame();
    void ReceiveChatWhisper(const BYTE *receiveBuffer);
    void ReceiveMovePosition(const BYTE *receiveBuffer);
    void FallingStartCharacter(CHARACTER *character, OBJECT *object);
    void ReceiveParty(const BYTE *receiveBuffer);
    void ReceiveGuild(const BYTE *receiveBuffer);
    void ReceiveGuildEndWar(const BYTE *receiveBuffer);
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
    void ReceiveGensJoining(const BYTE *receiveBuffer);         // OMF-01108
    void ReceiveGensSecession(const BYTE *receiveBuffer);       // OMF-01109
    void ReceivePlayerGensInfluence(const BYTE *receiveBuffer); // OMF-01110
    void ReceiveReward(const BYTE *receiveBuffer);              // OMF-01113
    void ReceiveOtherPlayerGensInfluenceViewport(const BYTE *receiveBuffer);
    void ReceivePetCommand(const BYTE *receiveBuffer);      // OMF-01115
    void ReceivePetAttack(const BYTE *receiveBuffer);       // OMF-01116
    void ReceivePetInfo(const BYTE *receiveBuffer);         // OMF-01117
    void ReceiveKillCount(const BYTE *receiveBuffer);       // OMF-01152
    void ReceiveBuildTime(const BYTE *receiveBuffer);       // OMF-01153
    void ReceiveCastleGuildMark(const BYTE *receiveBuffer); // OMF-01154
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
    void ReceiveCastleHuntZoneInfo(const BYTE *receiveBuffer);
    void ReceiveCastleHuntZoneResult(const BYTE *receiveBuffer);
    void ReceiveCatapultState(const BYTE *receiveBuffer);    // OMF-01157
    void ReceiveCatapultFire(const BYTE *receiveBuffer);     // OMF-01158
    void ReceiveCatapultFireToMe(const BYTE *receiveBuffer); // OMF-01159
    void ReceiveCrywolfAltarContract(const BYTE *receiveBuffer);
    void ReceiveCrywolfInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolStateAltarfInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolfLifeTime(const BYTE *receiveBuffer);
    void ReceiveCrywolfTankerHit(const BYTE *receiveBuffer);
    void ReceiveCrywolfBenefitPlusChaosRate(const BYTE *receiveBuffer); // OMF-01171
    void ReceiveCrywolfBossMonsterInfo(const BYTE *receiveBuffer);
    void ReceiveCrywolfPersonalRank(const BYTE *receiveBuffer);
    void ReceiveCrywolfHeroList(const BYTE *receiveBuffer);
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
    bool ReceiveIGS_CashPoint(const BYTE *receiveBuffer);
    bool ReceiveIGS_ShopOpenResult(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageItemListCount(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageItemList(const BYTE *receiveBuffer);
    bool ReceiveIGS_StorageGiftItemList(const BYTE *receiveBuffer);
    bool ReceiveIGS_EventItemlistCnt(const BYTE *receiveBuffer);
    bool ReceiveIGS_EventItemlist(const BYTE *receiveBuffer);
    bool ReceiveIGS_UpdateBanner(const BYTE *receiveBuffer);
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
    void ReceiveChatRoomConnectResult(DWORD windowUiId, const BYTE *receiveBuffer);   // OMF-01886
    void ReceiveChatRoomUserStateChange(DWORD windowUiId, const BYTE *receiveBuffer); // OMF-01887
    void ReceiveChatRoomUserList(DWORD windowUiId, const BYTE *receiveBuffer);        // OMF-01888
    void ReceiveChatRoomChatText(DWORD windowUiId, const BYTE *receiveBuffer);        // OMF-01889
    void ReceiveChatRoomNoticeText(DWORD windowUiId, const BYTE *receiveBuffer);      // OMF-01890
    void TranslateChattingProtocol(DWORD windowUiId, const BYTE *receiveBuffer,
                                   int size); // OMF-01891
    void ReceiveChangePlayer(std::span<const BYTE> receiveBuffer);
    void ReceiveCreatePlayerViewportExtended(std::span<const BYTE> receiveBuffer);
    void ReceiveDeleteCharacterViewport(const BYTE *receiveBuffer);
    void ReceiveDeleteCharacter(const BYTE *receiveBuffer);
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
    void RegisterBuff(eBuffState buff, OBJECT *object, const int buffTime = 0);
    void UnRegisterBuff(eBuffState buff, OBJECT *object);
    void AppearMonster(CHARACTER *character);
    void ReceiveSetPointsExtended(const BYTE *receiveBuffer);
    void ReceiveWTTimeLeft(const BYTE *receiveBuffer);           // OMF-01118
    void ReceiveWTMatchResult(const BYTE *receiveBuffer);        // OMF-01119
    void ReceiveTaxInfo(const BYTE *receiveBuffer);              // OMF-01134
    void ReceiveWTBattleSoccerGoalIn(const BYTE *receiveBuffer); // OMF-01120
    BOOL ReceiveStraightAttack(const BYTE *receiveBuffer, int size, BOOL encrypted);
    void InsertBuffPhysicalEffect(eBuffState buff, OBJECT *object);
    void ClearBuffPhysicalEffect(eBuffState buff, OBJECT *object);
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
    void ReceiveGuildWarScore(const BYTE *receiveBuffer);
    void ReceiveGuildInfo(const BYTE *receiveBuffer);
    void ReceiveGuildAssign(const BYTE *receiveBuffer);
    void ReceiveGuildRelationShip(const BYTE *receiveBuffer);
    void ReceiveGuildRelationShipResult(const BYTE *receiveBuffer);
    void ReceiveUnionViewportNotify(const BYTE *receiveBuffer);
    void ReceiveUnionList(const BYTE *receiveBuffer);
    void ReceiveSoccerTime(const BYTE *receiveBuffer);
    void ReceiveSoccerGoal(const BYTE *receiveBuffer);
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

    void Damage(vec_t *sourcePosition, CHARACTER *target, float attackRange, int attackPoint,
                bool hit);
    void MonsterMoveSandSmoke(OBJECT *object);
    BOOL PlayMonsterSoundGlobal(OBJECT *object);
    void MoveTournamentInterface();
    void MoveBattleSoccerEffect(CHARACTER *character);
    void ClearInput(BOOL clearWhisperTarget = TRUE);
    void ReceiveOption(const BYTE *receiveBuffer);
    void ReceiveEventChipInfomation(const BYTE *receiveBuffer);
    void ReceiveEventChip(const BYTE *receiveBuffer);
    void ReceiveMutoNumber(const BYTE *receiveBuffer);
    void ReceiveEventCount(const BYTE *receiveBuffer);
    void ReceiveQuestHistory(const BYTE *receiveBuffer);
    void ReceiveQuestState(const BYTE *receiveBuffer);
    void ReceiveQuestResult(const BYTE *receiveBuffer);                // OMF-01096
    void ReceiveQuestMonKillInfo(const BYTE *receiveBuffer);           // OMF-01098
    void ReceiveQuestByEtcEPList(const BYTE *receiveBuffer);           // OMF-01099
    void ReceiveQuestByNPCEPList(const BYTE *receiveBuffer);           // OMF-01100
    void ReceiveQuestCompleteResult(const BYTE *receiveBuffer);        // OMF-01103
    void ReceiveQuestGiveUp(const BYTE *receiveBuffer);                // OMF-01104
    void ReceiveProgressQuestList(const BYTE *receiveBuffer);          // OMF-01105
    void ReceiveProgressQuestRequestReward(const BYTE *receiveBuffer); // OMF-01106
    void DeleteCharacter();
    void RenderHelpCategory(int columnType, int positionX, int positionY);
    void RenderEqiupmentPart3D(int index, float x, float y, float width, float height);
    void RenderEqiupment3D();

    void CreateItemDrop(ITEM_t *item, ItemCreationParams params, vec_t *position, bool isFreshDrop);
    void CreateMoneyDrop(ITEM_t *item, int amount, vec_t *position, bool isFreshDrop);
    void CreateShiny(OBJECT *object);
    void RenderZen(int itemIndex, ITEM_t *item, vec_t *light);
    void CreateSnowBursts(OBJECT &object, vec_t *light);
    void ReceiveChainMagic(const BYTE *receiveBuffer);
    void ReceiveSoccerScore(const BYTE *receiveBuffer);
    void CreateHealing(OBJECT *object);
    void CreateForce(OBJECT *object, vec_t *position);
    void RetireEffect(OBJECT *object);
    void EffectDestructor(OBJECT *object);                     // OMF-01469
    void TerminateOwnerEffectObject(int ownerObjectType = -1); // OMF-01470
    bool DeleteParticle(int type);                             // OMF-01473
    bool DeleteEffect(int type, OBJECT *owner, int subType = -1);
    void DeleteEffect(int effectType);
    bool SearchEffect(int type, OBJECT *owner, int subType = -1);
    BOOL FindSameEffectOfSameOwner(int type, OBJECT *owner);
    void CheckTargetRange(OBJECT *object);
    void CreateBomb(vec_t *position, bool explode, int subType = 0);
    void CreateBomb2(vec_t *position, bool explode, int subType = 0, float scale = 0.0f);
    void CreateBomb3(vec_t *position, int subType, float scale = 1.0f);
    void CreateInferno(vec_t *position, int subType = 0);
    void CreateSpark(int type, CHARACTER *character, vec_t *position, vec_t *angle);
    void CreateBlood(OBJECT *object);
    void CreateBonfire(vec_t *position, vec_t *angle);
    void CreateFire(int type, OBJECT *object, float x, float y, float z);
    void RenderCircle2D(int type, vec_t *screenPosition, float scaleBottom, float scaleTop,
                        float height, float rotation, float textureV, float textureVScale);
    void CreateMagicShiny(CHARACTER *character, int hand = 0);
    void CreateTeleportBegin(OBJECT *object);
    void CreateTeleportEnd(OBJECT *object);
    void CreateArrow(CHARACTER *character, OBJECT *object, OBJECT *target, WORD skillIndex,
                     WORD skill, WORD skillKey);
    void CreateArrows(CHARACTER *character, OBJECT *object, OBJECT *target, WORD skillIndex = 0,
                      WORD skill = 1, WORD skillKey = 0);
    bool CreateCursedTempleSkillEffect(CHARACTER *character, int skillIndex, int subType);
    void LogSafeCastSizeMismatch(const char *packetType, std::size_t received,
                                 std::size_t expected);
    template <typename T>
    T *safe_cast(const std::span<const BYTE> receiveBuffer, const char *packetType = nullptr);

    wchar_t (&m_Username)[MAX_USERNAME_SIZE + 1];
    wchar_t (&m_Password)[MAX_PASSWORD_SIZE + 1];
    int &m_RememberMe;
    float &g_fScreenRate_x;
    float &g_fScreenRate_y;
    SessionBitmapView Bitmaps;
    std::span<MARK_t> GuildMark;
    int &SelectMarkColor;
    bool &SelectFlag;
    int &g_iKeyPadEnable;
    CGuildCache &g_GuildCache;
    GambleSystem &g_GambleSystem;
    SEASON3B::CMoveCommandData &g_MoveCommandData;
    CTimeCheck &g_Time;
    CSenatusInfo &g_SenatusInfo;
    PATH &path;
    MovementSkill &g_MovementSkill;
    DWORD &g_dwLatestMagicTick;
    int &ItemHelp;
    float &MouseUpdateTime;
    int &MouseUpdateTimeMax;
    bool &s_bIgnoreHeldClickAfterNpcTalk;
    bool &WhisperEnable;
    bool &ChatWindowEnable;
    int &InputFrame;
    int &EditFlag;
    wchar_t (&ColorTable)[8][10];
    int &SelectMonster;
    int &SelectModel;
    int &SelectMapping;
    int &SelectColor;
    int &SelectWall;
    float &SelectMappingAngle;
    bool &DebugEnable;
    int &SelectedItem;
    int &SelectedNpc;
    int &SelectedCharacter;
    int &SelectedOperate;
    int &Attacking;
    int &g_iFollowCharacter;
    bool &g_bAutoGetItem;
    bool &g_bRenderGameCursor;
    float &LButtonPopTime;
    float &LButtonPressTime;
    float &RButtonPopTime;
    float &RButtonPressTime;
    int &BrushSize;
    int &HeroTile;
    int &TargetNpc;
    int &TargetType;
    int &TargetX;
    int &TargetY;
    float &TargetAngle;
    OBJECT *&TradeNpc;
    bool &DontMove;
    bool &ServerHide;
    bool &SkillEnable;
    bool &MouseOnWindow;
    int (&TerrainWallType)[TERRAIN_SIZE * TERRAIN_SIZE];
    float (&TerrainWallAngle)[TERRAIN_SIZE * TERRAIN_SIZE];
    OBJECT *&PickObject;
    vec3_t &PickObjectAngle;
    float &PickObjectHeight;
    bool &PickObjectLockHeight;
    bool &EnableRandomObject;
    float &WallAngle;
    bool &LockInputStatus;
    bool &GuildInputEnable;
    bool &TabInputEnable;
    bool &GoldInputEnable;
    bool &InputEnable;
    bool &g_bScratchTicket;
    int &InputGold;
    int &InputNumber;
    int &InputTextWidth;
    int &InputIndex;
    int &InputResidentNumber;
    int (&InputTextMax)[12];
    wchar_t (&InputText)[12][256];
    wchar_t (&InputTextIME)[12][4];
    char (&InputTextHide)[12];
    int (&InputLength)[12];
    std::uint64_t &LastMacroTime;
    int &WhisperIDCurrent;
    char (&WhisperID)[MAX_WHISPER_ID][256];
    DWORD &g_dwOneToOneTick;
    bool &g_bGMObservation;
    BYTE (&DebugText)[10][256];
    int (&DebugTextLength)[10];
    char &DebugTextCount;
    int &ItemKey;
    int &ActionTarget;
    float &StandTime;
    int &HeroAngle;
    bool &EnableFastInput;
    BOOL &g_bWhileMovingZone;
    DWORD &g_dwLatestZoneMoving;
    int &TotalPacketSize;
    int &OldTime;
    int &g_iWidthEx;
    BOOL &g_bUseChatListBox;
    DWORD &g_dwActiveUIID;
    DWORD &g_dwMouseUseUIID;
    DWORD &g_dwTopWindow;
    DWORD &g_dwCurrentPressedButtonID;
    DWORD &g_dwLastLetterID;
    int &g_iNoticeInverse;
    CUIManager *&g_pUIManager;
    SEASON3B::CNewUISystem *&g_pNewUISystem;
    float &Time_Effect;
    bool &ashies;
    int &weather;
    vec3_t &MousePosition;
    vec3_t &MouseTarget;
    int &MouseX;
    int &MouseY;
    int &g_iMousePopPosition_x;
    int &g_iMousePopPosition_y;
    bool &MouseLButton;
    bool &MouseLButtonPop;
    bool &MouseLButtonPush;
    bool &MouseRButton;
    bool &MouseRButtonPop;
    bool &MouseRButtonPush;
    bool &MouseLButtonDBClick;
    bool &MouseMButton;
    bool &MouseMButtonPop;
    bool &MouseMButtonPush;
    DWORD &MouseRButtonPress;
    bool &GrabEnable;
    wchar_t (&GrabFileName)[MAX_PATH];
    int &GrabScreen;
    int &radioButtonIterIndex;
    CUIGateKeeper *&g_pUIGateKeeper;
    JewelHarmonyInfo *&g_pUIJewelHarmonyinfo;
    ItemAddOptioninfo *&g_pItemAddOptioninfo;
    CUIPopup *&g_pUIPopup;
    bool &HeroInventoryEnable;
    bool &StorageInventoryEnable;
    bool &g_bPersonalShopWnd;
    bool &g_bServerDivisionEnable;
    const wchar_t *(&optionFontLabels)[3];
    int &g_iLetterReadNextPos_x;
    int &g_iLetterReadNextPos_y;
    int &SelectedHero;
    bool &InitLogIn;
    bool &InitLoading;
    bool &InitCharacterScene;
    bool &InitMainScene;
    bool &MainSceneReady;
    float &g_fMULogoAlpha;
    short &g_shCameraLevel;
    const wchar_t *&szServerIpAddress;
    WORD &g_ServerPort;
    EGameScene &SceneFlag;
    int (&g_iCustomMessageBoxButton)[NUM_BUTTON_CMB][NUM_PAR_BUTTON_CMB];
    int (&g_iCustomMessageBoxButton_Cancel)[NUM_PAR_BUTTON_CMB];
    int &g_iCancelSkillTarget;
    int &DeleteGuildIndex;
    int &ErrorMessage;
    float &g_Luminosity;
    int &EnableEvent;
    CastleLevel &g_currentCastleLevel;
    bool &g_actionMatch;
    int &DeleteIndex;
    int &AppointStatus;
    wchar_t (&DeleteID)[100];
    wchar_t (&s_szTargetID)[MAX_USERNAME_SIZE + 1];
    int &s_nTargetFireMemberIndex;
    char &AppointType;
    int &TerrainFlag;
    bool &ActiveTerrain;
    bool &TerrainGrassEnable;
    bool &DetailLowEnable;
    vec3_t (&TerrainNormal)[TERRAIN_SIZE * TERRAIN_SIZE];
    vec3_t (&PrimaryTerrainLight)[TERRAIN_SIZE * TERRAIN_SIZE];
    vec3_t (&BackTerrainLight)[TERRAIN_SIZE * TERRAIN_SIZE];
    vec3_t (&TerrainLight)[TERRAIN_SIZE * TERRAIN_SIZE];
    float (&BackTerrainHeight)[TERRAIN_SIZE * TERRAIN_SIZE];
    const unsigned char *&TerrainMappingLayer1;
    const unsigned char *&TerrainMappingLayer2;
    const float *&TerrainMappingAlpha;
    float (&TerrainGrassTexture)[TERRAIN_SIZE];
    float (&TerrainGrassWind)[TERRAIN_SIZE * TERRAIN_SIZE];
    float (&g_fTerrainGrassWind1)[TERRAIN_SIZE * TERRAIN_SIZE];
    WORD (&TerrainWall)[TERRAIN_SIZE * TERRAIN_SIZE];
    float &WaterMove;
    int &CurrentLayer;
    float &g_fSpecialHeight;
    float &g_fFrustumRange;
    unsigned char (&BMPHeader)[1080];
    int &TerrainIndex1;
    int &TerrainIndex2;
    int &TerrainIndex3;
    int &TerrainIndex4;
    int &Index0;
    int &Index1;
    int &Index2;
    int &Index3;
    int &Index01;
    int &Index12;
    int &Index23;
    int &Index30;
    int &Index02;
    vec3_t (&TerrainVertex)[4];
    vec3_t &TerrainVertex01;
    vec3_t &TerrainVertex12;
    vec3_t &TerrainVertex23;
    vec3_t &TerrainVertex30;
    vec3_t &TerrainVertex02;
    float (&TerrainTextureCoord)[4][2];
    float (&TerrainTextureCoord01)[2];
    float (&TerrainTextureCoord12)[2];
    float (&TerrainTextureCoord23)[2];
    float (&TerrainTextureCoord30)[2];
    float (&TerrainTextureCoord02)[2];
    float &TerrainMappingAlpha01;
    float &TerrainMappingAlpha12;
    float &TerrainMappingAlpha23;
    float &TerrainMappingAlpha30;
    float &TerrainMappingAlpha02;
    int &WaterTextureNumber;
    int &FrustrumBoundMinX;
    int &FrustrumBoundMinY;
    int &FrustrumBoundMaxX;
    int &FrustrumBoundMaxY;
    float (&FrustrumX)[16];
    float (&FrustrumY)[16];
    int &FrustrumCount;
    vec3_t (&FrustrumVertex)[5];
    vec3_t (&FrustrumFaceNormal)[5];
    float (&FrustrumFaceD)[5];
    OBJECT &Sun;
    short (&BoundingVertices)[MAX_BONES];
    vec3_t (&BoundingMin)[MAX_BONES];
    vec3_t (&BoundingMax)[MAX_BONES];
    float (&BoneTransform)[MAX_BONES][3][4];
    vec3_t (&VertexTransform)[MAX_MESH][MAX_VERTICES];
    vec3_t (&NormalTransform)[MAX_MESH][MAX_VERTICES];
    float (&IntensityTransform)[MAX_MESH][MAX_VERTICES];
    vec3_t (&LightTransform)[MAX_MESH][MAX_VERTICES];
    vec3_t (&RenderArrayVertices)[MAX_VERTICES * 3];
    vec4_t (&RenderArrayColors)[MAX_VERTICES * 3];
    vec2_t (&RenderArrayTexCoords)[MAX_VERTICES * 3];
    float (&ParentMatrix)[3][4];
    vec3_t &LightVector;
    vec3_t &LightVector2;
    bool &HighLight;
    float &BoneScale;
    vec3_t &g_vright;
    int &g_smodels_total;
    float (&g_chrome)[MAX_VERTICES][2];
    int (&g_chromeage)[MAX_BONES];
    vec3_t (&g_chromeup)[MAX_BONES];
    vec3_t (&g_chromeright)[MAX_BONES];
    float &SubPixel;
    MASTER_LEVEL_VALUE &Master_Level_Data;
    _CROWN_SWITCH_INFO *&Switch_Info;
    int &HeroKey;
    int &CurrentProtocolState;
    wchar_t (&Password)[MAX_USERNAME_SIZE + 1];
    int &HeroIndex;
    wchar_t (&Question)[31];
    int &AttackPlayer;
    int &LogIn;
    wchar_t (&LogInID)[MAX_USERNAME_SIZE + 1];
    bool &LogOut;
    wchar_t (&ChatWhisperID)[MAX_USERNAME_SIZE + 1];
    int &CurrentSkill;
    int &BuyCost;
    int &EnableUse;
    int &SendGetItem;
    int &SendDropItem;
    BOOL &g_bPacketAfter_EquipmentItem;
    BYTE (&g_byPacketAfter_EquipmentItem)[256];
    bool &EnableGuildWar;
    int &GuildWarIndex;
    wchar_t (&GuildWarName)[9];
    int (&GuildWarScore)[2];
    bool &EnableSoccer;
    BYTE &HeroSoccerTeam;
    int &SoccerTime;
    wchar_t (&SoccerTeamName)[2][9];
    bool &SoccerObserver;
    CHARACTER_ENABLE &g_CharCardEnable;
    int &g_iMaxLetterCount;
    int &SummonLife;
    wchar_t (&g_GuildNotice)[3][128];
    GUILD_LIST_t (&GuildList)[MAX_GUILDS];
    int &g_nGuildMemberCount;
    int &GuildTotalScore;
    int &GuildPlayerKey;
    PARTY_t (&Party)[MAX_PARTYS];
    int &PartyNumber;
    int &PartyKey;
    ITEM &PickItem;
    ITEM &TargetItem;
    ITEM (&Inventory)[MAX_INVENTORY];
    ITEM (&InventoryExt)[MAX_INVENTORY_EXT];
    ITEM (&ShopInventory)[MAX_SHOP_INVENTORY];
    ITEM (&g_PersonalShopInven)[MAX_PERSONALSHOP_INVEN];
    ITEM (&g_PersonalShopBackup)[MAX_PERSONALSHOP_INVEN];
    bool &g_bEnablePersonalShop;
    int &g_iPShopWndType;
    POINT &g_ptPersonalShop;
    int &g_iPersonalShopMsgType;
    wchar_t (&g_szPersonalShopTitle)[MAX_SHOPTITLE + 1];
    CHARACTER &g_PersonalShopSeller;
    bool &g_bIsTooltipOn;
    int &CheckSkill;
    ITEM *&CheckInventory;
    bool &EquipmentSuccess;
    bool &CheckShop;
    int &CheckX;
    int &CheckY;
    ITEM *&SrcInventory;
    int &SrcInventoryIndex;
    int &DstInventoryIndex;
    int &AllRepairGold;
    int &StorageGoldFlag;
    int &ListCount;
    int &GuildListPage;
    int &g_bEventChipDialogEnable;
    int &g_shEventChipCount;
    short (&g_shMutoNumber)[3];
    bool &g_bServerDivisionAccept;
    wchar_t (&g_strGiftName)[64];
    bool &RepairShop;
    int &RepairEnable;
    int &AskYesOrNo;
    char &OkYesOrNo;
    WORD &g_wStoragePassword;
    short (&g_nKeyPadMapping)[10];
    wchar_t (&g_lpszKeyPadInput)[2][MAX_KEYPADINPUT + 1];
    BYTE (&BuyItem)[4];
    wchar_t (&TextList)[50][100];
    int (&TextListColor)[50];
    int (&TextBold)[50];
    SIZE (&Size)[50];
    int &TextNum;
    int &SkipNum;
    int &InventoryStartX;
    int &InventoryStartY;
    int &ShopInventoryStartX;
    int &ShopInventoryStartY;
    int &TradeInventoryStartX;
    int &TradeInventoryStartY;
    int &CharacterInfoStartX;
    int &CharacterInfoStartY;
    int &GuildStartX;
    int &GuildStartY;
    int &GuildListStartX;
    int &GuildListStartY;
    bool &EquipmentItem;
    OBJECT &ObjectSelect;
    unsigned int (&MarkColor)[16];
    SessionModelPool &Models;
    SessionModelLoader &gLoadData;
    Connection *&SocketClient;
    BOOL &g_bGameServerConnected;
    CServerListManager &g_ServerListManager;
    bool &bCheckNPC;
    int &g_iNumLineMessageBoxCustom;
    wchar_t (&g_lpszMessageBoxCustom)[SessionQuestDialogDimensions::MessageLineCount]
                                     [SessionQuestDialogDimensions::MessageLength];
    int &g_iCurrentDialogScript;
    int &g_iNumAnswer;
    wchar_t (&g_lpszDialogAnswer)[SessionQuestDialogDimensions::AnswerCount]
                                 [SessionQuestDialogDimensions::AnswerLineCount]
                                 [SessionQuestDialogDimensions::MessageLength];
    SEASON3B::CPartyManager *const g_pPartyManager;
    std::uint8_t &g_tabBar;
    std::uint32_t &g_renderItemIndexBackup;
    ITEM &g_renderItemInfoBackup;
    PET_INFO &gs_PetInfo;
    CDuelMgr &g_DuelMgr;
    CUIGuardsMan &g_GuardsMan;
    SessionCharacterPopulationStorage &CharactersClient;
    CHARACTER &CharacterView;
    CHARACTER *&Hero;
    CHARACTER_MACHINE *&CharacterMachine;
    CHARACTER_ATTRIBUTE *&CharacterAttribute;
    float (&g_fBoneSave)[10][3][4];
    int &EquipmentLevelSet;
    bool &g_bAddDefense;
    int &g_iLimitAttackTime;
    int &g_iOldPositionX;
    int &g_iOldPositionY;
    float &g_fStopTime;
    int &playerNpcDeviasTextIndex;
    int &playerNpcLorenciaTextIndex;
    std::vector<CHARACTER *> &activeChars;
    std::unique_ptr<OBJECT> (&g_ItemObject)[ITEM_ETC + MAX_ITEM_INDEX];
    float &swordDancerPosition;
    int &swordDancerRandom;
    float &EarthQuake;
    int &visibleObject;
    OBJECT &g_CloudsLow;
    float &objectTextureAnimation;
    float &objectMeshLight;
    float &objectMeshLightDelta;
    int &objectPriorAnimationFrame;
    float &RainTarget;
    float &RainCurrent;
    int &RainSpeed;
    int &RainAngle;
    float &RainPosition;
    int &MonsterKey;
    eCurrentMode &m_nCurrMode;
    eCurrentStep &m_eCurrStep;
    GuildRelationshipType &m_byRelationShipType;
    GuildRequestType &m_byRelationShipRequestType;
    BYTE &m_byTargetUserIndexH;
    BYTE &m_byTargetUserIndexL;
    int &g_iCurrentItem;
    int (&g_iItemInfo)[SessionItemHelpStorage::LevelCount][SessionItemHelpStorage::ColumnCount];
    float &s_fWind;
    vec3_t &s_vWind;
    vec3_t &g_vParticleWind;
    vec3_t &g_vParticleWindVelo;
    void ApplyLoginSceneOffset(float &x, float &y, float &z); // OMF-00087
    BuffStateSystem &TheBuffStateSystem();                    // OMF-00187
    BuffScriptLoader &TheBuffInfo();                          // OMF-00188
    BuffTimeControl &TheBuffTimeControl();                    // OMF-00189
    BuffStateValueControl &TheBuffStateValueControl();        // OMF-00190
    void Action(CHARACTER *c, OBJECT *o, bool Now);           // OMF-00459
    void Attack(CHARACTER *c);                                // OMF-00468
    void CheckGate();                                         // OMF-00469
    bool HandleHeroPositionSlide(CHARACTER *c);               // OMF-00470
    void MoveHero();                                          // OMF-00471
    void ApplyGroundItemLabelDescriptor(
        const ItemRulesDetail::GroundItemLabelDescriptor &descriptor); // OMF-00541
    bool CreateGroundItemLabelTexture(const ItemRulesDetail::GroundItemLabelDescriptor &descriptor,
                                      GroundItemLabelCacheEntry &cacheEntry);          // OMF-00542
    void PruneGroundItemLabelCache(DWORD currentTick);                                 // OMF-00539
    void SetGroundItemLabelBuildBudget(int buildBudget);                               // OMF-00545
    void ConvertChaosTaxGold(DWORD gold, wchar_t *text);                               // OMF-00512
    bool CreatePersonalItemTable();                                                    // OMF-00799
    void ReleasePersonalItemTable();                                                   // OMF-00800
    void AddPersonalItemPrice(int index, int price, int type);                         // OMF-00801
    void RemovePersonalItemPrice(int index, int type);                                 // OMF-00802
    void RemoveAllPerosnalItemPrice(int type);                                         // OMF-00803
    bool GetPersonalItemPrice(int index, int &price, int type);                        // OMF-00804
    bool IsExistUndecidedPrice();                                                      // OMF-00583
    bool IsStrifeMap(int nMapIndex);                                                   // OMF-00587
    void AdvanceObjectVisual(OBJECT *o);                                               // OMF-00605
    void RenderObjects();                                                              // OMF-00606
    void HandleItemOnGround(OBJECT *o);                                                // OMF-00633
    void AttackElf(CHARACTER *c, int Skill, float Distance);                           // OMF-00687
    bool CastWarriorSkill(CHARACTER *c, OBJECT *o, ITEM *p, ActionSkillType iSkill);   // OMF-00692
    bool SkillWarrior(CHARACTER *c, ITEM *p);                                          // OMF-00693
    void UseSkillWarrior(CHARACTER *c, OBJECT *o);                                     // OMF-00694
    void UseSkillElf(CHARACTER *c, OBJECT *o);                                         // OMF-00696
    bool SkillElf(CHARACTER *c, ITEM *p);                                              // OMF-00701
    int ExecuteSkill(CHARACTER *c, ActionSkillType Skill, float Distance);             // OMF-00704
    CHARACTER *FindLiveCharacterByKey(int key);                                        // OMF-00707
    SEASON3B::CNewUIInventoryCtrl *GetInventoryCtrl() const;                           // OMF-00735
    void SendReqUnMix();                                                               // OMF-00736
    void SendReqMix();                                                                 // OMF-00737
    void ProcessCSAction();                                                            // OMF-00738
    bool FindWantedList();                                                             // OMF-00740
    void SelectFromList(int iIndex, int iLevel);                                       // OMF-00741
    int GetUnMixGemLevel() const;                                                      // OMF-00742
    void MoveUnMixList();                                                              // OMF-00743
    void RenderUnMixList();                                                            // OMF-00744
    bool CheckInv();                                                                   // OMF-00746
    bool CheckMyInvValid();                                                            // OMF-00747
    char CheckOneItem(const ITEM *item) const;                                         // OMF-00745
    void CalcGen();                                                                    // OMF-00748
    char CalcCompiledCount(const ITEM *item) const;                                    // OMF-00749
    int CalcItemValue(const ITEM *item) const;                                         // OMF-00750
    int CalcEmptyInv() const;                                                          // OMF-00751
    void Init();                                                                       // OMF-00752
    void GetBack();                                                                    // OMF-00753
    void Exit();                                                                       // OMF-00754
    int GetJewelRequireCount(int index) const;                                         // OMF-00755
    int Check_Jewel(int jewel, int type = 0, bool model = false) const;                // OMF-00756
    int GetJewelIndex(int jewel, int type) const;                                      // OMF-00757
    void SetMode(BOOL mode);                                                           // OMF-00758
    void SetGem(char gem);                                                             // OMF-00759
    void SetComType(char type);                                                        // OMF-00760
    void SetState(char state);                                                         // OMF-00761
    void SetError(char error);                                                         // OMF-00762
    char GetError() const;                                                             // OMF-00763
    bool isComMode() const;                                                            // OMF-00764
    int Check_Jewel_Unit(int jewel, bool model = false) const;                         // OMF-00765
    int Check_Jewel_Com(int jewel, bool model = false) const;                          // OMF-00766
    bool isCompiledGem(const ITEM *item) const;                                        // OMF-00767
    bool isAble() const;                                                               // OMF-00768
    int CalcRecoveryZen(BYTE type, wchar_t *Text);                                     // OMF-00806
    void RecoverPet(BYTE type);                                                        // OMF-00807
    void MovePetCommand(CHARACTER *c);                                                 // OMF-00827
    bool RequestPetInfo(int sx, int sy, ITEM *pItem);                                  // OMF-00834
    void ReceiveServerConnect(const BYTE *ReceiveBuffer);                              // OMF-00909
    void ReceiveMuHelperStatusUpdate(std::span<const BYTE> ReceiveBuffer);             // OMF-00928
    void ReceiveMoveCharacter(std::span<const BYTE> ReceiveBuffer);                    // OMF-00938
    BOOL ReceiveTeleport(const BYTE *ReceiveBuffer, BOOL bEncrypted);                  // OMF-00940
    void ReceiveCreateMonsterViewport(const BYTE *ReceiveBuffer);                      // OMF-00946
    void ReceiveAttackDamageExtended(const BYTE *ReceiveBuffer);                       // OMF-00952
    void ReceiveAction(const BYTE *ReceiveBuffer, int Size);                           // OMF-00953
    BOOL ReceiveMonsterSkill(const BYTE *ReceiveBuffer, int Size, BOOL bEncrypted);    // OMF-00958
    BOOL ReceiveMagic(const BYTE *ReceiveBuffer, int Size, BOOL bEncrypted);           // OMF-00959
    BOOL ReceiveMagicContinue(const BYTE *ReceiveBuffer, int Size, BOOL bEncrypted);   // OMF-00960
    void ReceiveDie(const BYTE *ReceiveBuffer, int Size);                              // OMF-00967
    void ReceiveCreateMoney(std::span<const BYTE> ReceiveBuffer);                      // OMF-00968
    void ReceiveCreateItemViewportExtended(std::span<const BYTE> ReceiveBuffer);       // OMF-00969
    void ReceiveDeleteItemViewport(const BYTE *ReceiveBuffer);                         // OMF-00970
    void RequestInventorySync();                                                       // OMF-00971
    bool CheckExceptionBuff(eBuffState buff, OBJECT *object, bool isErase);            // OMF-01236
    void InsertBuffLogicalEffect(eBuffState buff, OBJECT *object, const int buffTime); // OMF-01237
    void ClearBuffLogicalEffect(eBuffState buff, OBJECT *object);                      // OMF-01238
    void RenderCharacterScene3D();                                                     // OMF-01767
    void RenderCharacterSceneUI();                                                     // OMF-01769
    bool NewRenderCharacterScene();                                                    // OMF-01770
    void LoadingScene();                                                               // OMF-01771
    bool CreateLogInScene();                                                           // OMF-01782
    void NewMoveLogInScene();                                                          // OMF-01783
    bool NewRenderLogInScene();                                                        // OMF-01784
    bool RequireLeavesEffect();                                                        // OMF-01785
    bool ShouldRenderLeaves();                                                         // OMF-01786
    void InitializeMainScene();                                                        // OMF-01787
    void InitializeSceneFrame();                                                       // OMF-01788
    void UpdateUIAndInput();                                                           // OMF-01789
    void UpdateGameEntities();                                                         // OMF-01790
    void RenderGameWorld(BYTE &byWaterMap, int width, int height);                     // OMF-01793
    void RenderMainSceneUI();                                                          // OMF-01794
    bool RenderMainScene();                                                            // OMF-01795
    bool GetTimeCheck(int DelayTime);                                                  // OMF-01811
    void UpdateLoginAndCharacterScenes();                                              // OMF-01827
    bool RenderCurrentScene();                                                         // OMF-01832
    void CheckServerConnection();                                                      // OMF-01836

    SessionKeeper &sessionKeeper_;
};

struct SessionDisplayRect final
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    constexpr auto operator<=>(const SessionDisplayRect &) const noexcept = default;
};

struct WorkspaceOverlayRect final
{
    SessionDisplayRect rect;
    std::uint32_t color = 0;
};

struct WorkspaceOverlayLabel final
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::wstring text;
    std::uint32_t color = 0;
    float scale = 1.0F;
};

class CErrorReport;
class CInGameShopSystem;
class CMapManager;
class CPortalMgr;
class CmuConsoleDebug;
class CUIFriendMenu;
class SessionRenderText;
class CSBaseMatch;
class CSItemOption;
class CSQuest;
class CQuestMng;
class CUITextInputBox;
class CUIMercenaryInputBox;
class CameraProjection;
class CameraState;
class ApplicationAudio;
class ApplicationConfigUnit;
class AppWindow;
class MapProcess;
class SessionGameplayUnit;
class SessionGameDataUnit;
class SessionKeeper;
class SessionRenderUnit;
class SessionNetworkUnit;
class SessionUiUnit;
namespace SEASON3B
{
class CNewUIMessageBoxMng;
}
namespace SEASON3A
{
class CMixRecipeMgr;
}
namespace SEASON4A
{
class CSocketItemMgr;
}
namespace MUHelper
{
class SessionMuHelperUnit;
}

class SessionUiLegacyBindings : protected SessionLegacyCalls
{
  protected:
    explicit SessionUiLegacyBindings(SessionKeeper &keeper) noexcept;
    SessionKeeper &SessionOrigin() const noexcept;
    MapProcess &MapProcessForConstruction() const noexcept;
    SessionGameplayUnit &GameplayForConstruction() const noexcept;
    SessionRenderUnit &RendererForConstruction() const noexcept;
    SessionNetworkUnit &NetworkForConstruction() const noexcept;
    SessionUiUnit &UiForConstruction() const noexcept;
    SessionGameDataUnit &GameDataForConstruction() const noexcept;
    MUHelper::SessionMuHelperUnit &MuHelperForConstruction() const noexcept;
    ApplicationConfigUnit &ApplicationConfigForConstruction() const noexcept;
    ApplicationAudio &ApplicationAudioForConstruction() const noexcept;
    AppWindow &AppWindowForConstruction() const noexcept;
    unsigned int ModernUiViewportWidth() const noexcept;
    unsigned int ModernUiViewportHeight() const noexcept;
    float ModernUiScale() const noexcept;
    float ModernUiScreenRateX() const noexcept;
    float ModernUiScreenRateY() const noexcept;

    std::wstring &g_strSelectedML;
    SessionRenderText &g_RenderText;
    HWND &g_hWnd;
    bool &g_bWndActive;
    unsigned int &WindowWidth;
    unsigned int &WindowHeight;
    CameraState &g_Camera;
    bool TextureEnable = true;
    bool AlphaTestEnable = false;
    float &FPS_ANIMATION_FACTOR;
    double &WorldTime;
    CErrorReport &g_ErrorReport;
    CmuConsoleDebug &g_ConsoleDebug;
    CMapManager &gMapManager;
    CPortalMgr &g_PortalMgr;
    int &UserCount;
    int &MonsterCount;
    SEASON3B::CNewUIMessageBoxMng &g_MessageBox;
    CUITextInputBox *&g_pSingleTextInputBox;
    CUITextInputBox *&g_pSinglePasswdInputBox;
    CUIMercenaryInputBox *&g_pMercenaryInputBox;
    CUITextInputBox *&focusedTextInputBox_;
    DWORD &g_dwKeyFocusUIID;
    int &MouseWheel;
    CSBaseMatch *&g_csMatchInfo;
    SEASON3A::CMixRecipeMgr &g_MixRecipeMgr;
    SEASON4A::CSocketItemMgr &g_SocketItemMgr;
    CSItemOption &g_csItemOption;
    CSQuest &g_csQuest;
    CQuestMng &g_QuestMng;
    int &iUnMixIndex;
    int &iUnMixLevel;
    BOOL &m_bType;
    char &m_cGemType;
    char &m_cComType;
    BYTE &m_cCount;
    int &m_iValue;
    BYTE &m_cPercent;
    char &m_cState;
    char &m_cErr;
    void ReleaseTextInputFocus() noexcept;
};

template <typename T, std::size_t N> class SessionBoundArray
{
  public:
    explicit SessionBoundArray(SessionKeeper &keeper)
        : SessionBoundArray(keeper, std::make_index_sequence<N>{})
    {
    }

    T &operator[](std::size_t index) noexcept
    {
        return values_[index];
    }
    const T &operator[](std::size_t index) const noexcept
    {
        return values_[index];
    }
    T *data() noexcept
    {
        return values_.data();
    }
    const T *data() const noexcept
    {
        return values_.data();
    }
    T *begin() noexcept
    {
        return values_.data();
    }
    T *end() noexcept
    {
        return values_.data() + N;
    }
    const T *begin() const noexcept
    {
        return values_.data();
    }
    const T *end() const noexcept
    {
        return values_.data() + N;
    }

  private:
    template <std::size_t... Index>
    SessionBoundArray(SessionKeeper &keeper, std::index_sequence<Index...>)
        : values_{T((static_cast<void>(Index), keeper))...}
    {
    }

    std::array<T, N> values_;
};

struct SessionConfigValues final
{
    std::wstring displayName;
    bool rememberMe = false;
    bool savePassword = false;
    unsigned short autoLoginPort = 0;
    std::wstring autoSelectCharacter;
    std::wstring username;
    std::wstring encryptedPassword;

    friend bool operator==(const SessionConfigValues &, const SessionConfigValues &) = default;
};

struct SessionVisualAnimationInput final
{
    bool workerSafeLegacyPath = false;
    bool activeScene = false;
    bool destroyRequested = false;
    double currentTickCount = 0.0;
    double previousWaterChange = 0.0;
    int previousWaterTexture = 0;
    int referenceFps = Core::Time::DefaultReferenceFps;
};

struct SessionVisualAnimationResult final
{
    bool workerSafeLegacyPath = false;
    bool succeeded = false;
    double previousWaterChange = 0.0;
    double nextWaterChange = 0.0;
    int previousWaterTexture = 0;
    int nextWaterTexture = 0;
    bool capturedActiveScene = false;
    bool capturedDestroyRequested = false;
    double capturedCurrentTickCount = 0.0;
};

struct LogicPickResult final
{
    bool hit = false;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    float tileX = 0.0f;
    float tileY = 0.0f;

    constexpr auto operator<=>(const LogicPickResult &) const noexcept = default;
};

enum class GameplayInteractionKind
{
    None,
    Character,
    Operate,
    Npc,
    Item,
    GroundMovement,
    Blocked,
};

struct GameplayInteractionFact final
{
    GameplayInteractionKind kind = GameplayInteractionKind::None;
    std::optional<CharacterId> character;
    std::optional<EntityId> entity;
    std::optional<ItemId> item;
    LogicPickResult terrainPick;
    int selectedCharacter = -1;
    int selectedNpc = -1;
    int selectedItem = -1;
    int selectedOperate = -1;

    constexpr auto operator<=>(const GameplayInteractionFact &) const noexcept = default;
};

struct SessionPhysicsFrameInput final
{
    static std::optional<SessionPhysicsFrameInput> TryCreate(float animationFactor,
                                                             double worldTime) noexcept
    {
        if (!std::isfinite(animationFactor) || animationFactor < 0.0f || !std::isfinite(worldTime))
        {
            return std::nullopt;
        }
        return SessionPhysicsFrameInput{animationFactor, worldTime};
    }

    float animationFactor = 0.0f;
    double worldTime = 0.0;
};

struct SessionFrameInput final
{
    SessionId sessionId;
    SessionGeneration generation;
    bool renderRequired = false;
    std::uint64_t surfaceGeneration = 0;
    std::uint32_t targetWidth = 0;
    std::uint32_t targetHeight = 0;
    bool ownerInputReady = true;
    GameplayInteractionFact interaction;
    SessionVisualAnimationInput visualAnimation;
    SessionPhysicsFrameInput physics;
};

struct SessionHotspotRenderJobSample final
{
    std::uint64_t frameSequence = 0;
    std::uint64_t sessionId = 0;
    std::uint64_t sessionGeneration = 0;
    float cpuMs = 0.0F;
    float wallMs = 0.0F;
};

static_assert(sizeof(SessionHotspotRenderJobSample) == 32);

struct ApplicationOwnerWork final
{
    using ExecuteFunction = void (*)(void *) noexcept;

    // Called by the submitting owner after jobs are admitted and before its
    // barrier wait. It is never dispatched to a scheduler worker.
    void Run() const noexcept
    {
        if (execute != nullptr)
        {
            execute(context);
        }
    }

    void *context = nullptr;
    ExecuteFunction execute = nullptr;
};

class ApplicationKeeperTestPeer;
class ApplicationSessionRuntimeTestPeer;
class GameSession;
class SessionAudioLogicView;
class SessionFrameView;
class SessionIdGenerator;
class SessionManager;
class SessionUiView;
class SessionVisualView;
class SessionWorkspace;

class ApplicationSessionRuntime final : protected ApplicationLegacyCalls
{
  public:
    using SessionInitializer = std::function<bool(GameSession &)>;
    using SessionFinalizer = std::function<void(GameSession &)>;

    ApplicationSessionRuntime(ApplicationKeeper &keeper, SessionWorkspace &workspace,
                              SessionManager &sessions, SessionFrameView &frame, SessionUiView &ui,
                              SessionVisualView &visual, SessionAudioLogicView &audioLogic,
                              SessionIdGenerator &ids, SessionSlotId slotId,
                              SessionConfigValues initialConfig);
    ~ApplicationSessionRuntime();

    ApplicationSessionRuntime(const ApplicationSessionRuntime &) = delete;
    ApplicationSessionRuntime &operator=(const ApplicationSessionRuntime &) = delete;
    ApplicationSessionRuntime(ApplicationSessionRuntime &&) = delete;
    ApplicationSessionRuntime &operator=(ApplicationSessionRuntime &&) = delete;

    bool Start(SessionDisplayRect contentRect);
    std::size_t StartConfiguredSessions(SessionDisplayRect windowRect);
    void SetSessionInitializer(SessionInitializer initializer);
    void SetSessionFinalizer(SessionFinalizer finalizer);
    bool AddDefaultSlot();
    bool MoveSessionUp(SessionId id);
    bool DiscardSession(SessionId id);
    void QueueDiscard(SessionId id);
    void ProcessPendingCommands();

    SessionConfigValues *CurrentConfig() noexcept;
    GameSession *PublishedSession() noexcept;
    bool ApplyGameplayExternalEvent(const GameplayExternalEvent &event) noexcept;
    void StopSession() noexcept;
    void Shutdown() noexcept;
    void UpdateResolutionDependentSystems();

    SessionFrameView *Frame() noexcept;
    SessionUiView *Ui() noexcept;
    SessionVisualView *Visual() noexcept;
    SessionAudioLogicView *AudioLogic() noexcept;

  private:
    friend class ApplicationKeeperTestPeer;
    friend class ApplicationSessionRuntimeTestPeer;

    bool CanCreateSession() const noexcept;
    void ProcessPendingStartupSessions(std::chrono::steady_clock::time_point now);
    std::optional<SessionId> OpenSession(SessionSlotId slotId, const SessionConfigValues &config,
                                         bool focus);
    void StopSession(SessionId id) noexcept;
    bool Owns(SessionId id) const noexcept;
    void RefreshCanAddSession() noexcept;

    SessionWorkspace &workspace_;
    SessionManager &sessions_;
    SessionFrameView &frame_;
    SessionUiView &ui_;
    SessionVisualView &visual_;
    SessionAudioLogicView &audioLogic_;
    SessionIdGenerator &ids_;
    SessionSlotId slotId_;
    SessionConfigValues initialConfig_;
    std::optional<SessionId> primarySessionId_;
    std::vector<SessionId> ownedSessionIds_;
    std::vector<SessionId> pendingDiscards_;
    std::deque<std::pair<SessionSlotId, SessionConfigValues>> pendingStartupSessions_;
    std::chrono::steady_clock::time_point nextStartupSessionTime_{};
    std::chrono::steady_clock::duration startupInitDelay_{};
    SessionInitializer initializer_;
    SessionFinalizer finalizer_;
    bool transactionActive_ = false;
};

enum class SessionOrderedEffectKind : std::uint8_t
{
    ManagedConnectionSend,
    HardwareAudio,
    PlatformRequest,
    DiagnosticRecord,
};

struct SessionOrderedEffectRecord final
{
    SessionOrderedEffectKind kind = SessionOrderedEffectKind::DiagnosticRecord;
    std::uint32_t channel = 0;
    std::uint64_t stableSequence = 0;
    std::uint32_t payloadOffset = 0;
    std::uint32_t payloadSize = 0;
};

class SessionOrderedEffectBatch final
{
  public:
    static constexpr std::size_t MaximumRecords = 64;
    static constexpr std::size_t MaximumPayloadBytes = 4096;

    bool TryAppend(SessionOrderedEffectKind kind, std::uint32_t channel,
                   std::uint64_t stableSequence, std::span<const std::byte> payload) noexcept
    {
        if (stableSequence == 0 || recordCount_ == records_.size() ||
            payload.size() > payload_.size() - payloadSize_ ||
            (recordCount_ != 0 && stableSequence <= records_[recordCount_ - 1].stableSequence))
        {
            ++rejectedCount_;
            return false;
        }
        const std::size_t offset = payloadSize_;
        for (const std::byte value : payload)
        {
            payload_[payloadSize_++] = value;
        }
        records_[recordCount_++] = {
            kind,
            channel,
            stableSequence,
            static_cast<std::uint32_t>(offset),
            static_cast<std::uint32_t>(payload.size()),
        };
        return true;
    }

    std::span<const SessionOrderedEffectRecord> Records() const noexcept
    {
        return {records_.data(), recordCount_};
    }

    std::span<const std::byte> Payload(const SessionOrderedEffectRecord &record) const noexcept
    {
        if (record.payloadOffset > payloadSize_ ||
            record.payloadSize > payloadSize_ - record.payloadOffset)
        {
            return {};
        }
        return {
            payload_.data() + record.payloadOffset,
            record.payloadSize,
        };
    }

    std::size_t PayloadBytes() const noexcept
    {
        return payloadSize_;
    }
    std::uint64_t RejectedCount() const noexcept
    {
        return rejectedCount_;
    }

    void Clear() noexcept
    {
        recordCount_ = 0;
        payloadSize_ = 0;
    }

  private:
    std::array<SessionOrderedEffectRecord, MaximumRecords> records_{};
    std::array<std::byte, MaximumPayloadBytes> payload_{};
    std::size_t recordCount_ = 0;
    std::size_t payloadSize_ = 0;
    std::uint64_t rejectedCount_ = 0;
};

class SessionOrderedEffectSink
{
  public:
    virtual ~SessionOrderedEffectSink() = default;

    // The sink owns one all-or-nothing transaction for the batch. It must
    // preflight capacity/targets before making any external effect visible.
    virtual bool CommitBatch(SessionId sessionId, SessionGeneration generation,
                             std::uint64_t frameSequence,
                             const SessionOrderedEffectBatch &batch) noexcept = 0;
};

enum class SessionAdvanceStatus
{
    Pending,
    Succeeded,
    Failed,
    Cancelled,
    StaleGeneration,
    ConcurrentAdvanceRejected,
};

// Commits successful lane products in the exact result order supplied by the
// frame plan. A missing sink rejects non-empty work instead of dropping it.
bool CommitSessionOrderedEffects(std::span<SessionAdvanceResult> results,
                                 SessionOrderedEffectSink *sink) noexcept;

class SessionIdGenerator final
{
  public:
    SessionIdGenerator() noexcept;
    SessionId Next() noexcept;

  private:
    SessionId::ValueType nextValue_;
};

class GameSession;
class CUITextInputBox;
class ApplicationKeeper;
class ApplicationAudio;
class CGlobalBitmap;
struct SessionInputEvent;

struct SessionMemoryStats final
{
    std::size_t fixedBytes = 0;
    std::size_t modelPoolBytes = 0;
};

struct SessionAdvancePhaseTimings final
{
    std::chrono::nanoseconds ownerPreparation{};
    std::chrono::nanoseconds entityWorker{};
    std::chrono::nanoseconds ownerCompletion{};
    std::chrono::nanoseconds systemsWorker{};
    std::chrono::nanoseconds renderWorker{};
#ifdef _DEBUG
    std::chrono::nanoseconds totalRenderJobCpu{};
    std::chrono::nanoseconds maximumRenderJobCpu{};
    std::chrono::nanoseconds maximumRenderJobWall{};
#endif
    std::chrono::nanoseconds workerBarrierWait{};
    std::chrono::nanoseconds total{};
#ifdef _DEBUG
    std::uint32_t renderJobCount = 0;
#endif
};

#ifdef _DEBUG
struct SessionHotspotFrameSample final
{
    std::uint64_t frameSequence = 0;
    float renderPhaseWallMs = 0.0F;
    float totalRenderJobCpuMs = 0.0F;
    float maximumRenderJobCpuMs = 0.0F;
    float maximumRenderJobWallMs = 0.0F;
    std::uint32_t renderJobCount = 0;
};
static_assert(sizeof(SessionHotspotFrameSample) == 32);
inline constexpr std::size_t SessionHotspotFrameHistorySize = 300;
#endif

class SessionManager final : private SessionOrderedEffectSink
{
    using SessionStorage = std::map<SessionId, std::unique_ptr<GameSession>>;

  public:
    class PreparedSession final
    {
      public:
        PreparedSession(PreparedSession &&) noexcept = default;
        PreparedSession &operator=(PreparedSession &&) noexcept = default;
        ~PreparedSession();

        PreparedSession(const PreparedSession &) = delete;
        PreparedSession &operator=(const PreparedSession &) = delete;

      private:
        friend class SessionManager;
        explicit PreparedSession(SessionStorage::node_type node) noexcept;
        SessionStorage::node_type node_;
    };

    explicit SessionManager(ApplicationSessionScheduler &scheduler) noexcept;
    SessionManager(ApplicationKeeper &keeper, ApplicationSessionScheduler &scheduler) noexcept;
    ~SessionManager();
    bool Add(std::unique_ptr<GameSession> session);
    std::optional<PreparedSession> Prepare(std::unique_ptr<GameSession> session);
    bool Publish(PreparedSession &&prepared) noexcept;
    std::unique_ptr<GameSession> Remove(SessionId id) noexcept;
    GameSession *Find(SessionId id) noexcept;
    const GameSession *Find(SessionId id) const noexcept;
    GameSession *FindBySlot(SessionSlotId slotId) noexcept;
    const GameSession *FindBySlot(SessionSlotId slotId) const noexcept;
    std::vector<SessionId> SessionIds() const;
    std::optional<SessionSlotId> GreatestLiveSlot() const noexcept;
    bool RekeySlot(SessionId id, SessionSlotId slotId) noexcept;
    bool SwapSlots(SessionId first, SessionId second) noexcept;
    std::size_t SessionCount() const noexcept;

    std::size_t AdvanceSessions() noexcept;
    std::size_t AdvanceSessions(double frameDeltaMilliseconds,
                                ApplicationOwnerWork ownerWork = {}) noexcept;
    void AdvanceSkillDelays(int elapsedMs);
    void UpdateResolutionDependentSystems();
    CUITextInputBox *FocusedTextInputBox(SessionId id) noexcept;
    bool ProcessModernUiInput(SessionId id, const SessionInputEvent &event) noexcept;
    std::optional<SessionDisplayRect> ModernTextInputArea(SessionId id) const noexcept;
    bool ToggleCameraZoomLock(SessionId id);
    void SetMouseWheel(SessionId id, int wheel) noexcept;
    void ApplyPointerMove(SessionId id, std::int32_t x, std::int32_t y) noexcept;
    void ApplyPointerButton(SessionId id, std::uint32_t button, bool pressed,
                            std::uint8_t clicks) noexcept;
    void RouteKeyboardState(std::optional<SessionId> focused, const BYTE *states) noexcept;
    void ResetTransientMouseStates() noexcept;
    void ResetMouseButtons() noexcept;
    void CycleCameraMode(SessionId id);
    void ResetCameraView(SessionId id);
    void TickMuHelpers();
    void BeginReconnects();
    void UpdateReconnects();
    void DeleteSockets();
    void SendNetworkPings(DWORD tickCount);
    void SendCheatDetectionLogouts();
    bool UpdateSceneCompatibility();
    void StopScheduling() noexcept;
    bool IsScheduling() const noexcept;
    std::span<const SessionAdvanceResult> LastAdvanceResults() const noexcept;
    std::span<SessionAdvanceResult> LastAdvanceResults() noexcept;
    void CompleteRenderCompletions(const ApplicationRenderCompletionBatch &completions,
                                   CGlobalBitmap &assets) noexcept;
    ApplicationSessionSchedulerMetrics SchedulerMetrics() const noexcept;
    const SessionAdvancePhaseTimings &LastAdvancePhaseTimings() const noexcept;
    std::uint64_t LastFrameSequence() const noexcept;
    double SessionCpuPercent(SessionId id) const noexcept;
    std::size_t SessionOwnedStorageBytes(SessionId id) const noexcept;
    SessionMemoryStats MemoryStats() noexcept;

  private:
    std::optional<std::size_t> CaptureAdvanceInputsOnOwner(std::span<GameSession *> admitted,
                                                           float frameAnimationFactor) noexcept;
    struct SessionWorkerContext final
    {
        GameSession *session = nullptr;
        const SessionFrameInput *input = nullptr;
        SessionVisualAnimationResult visualResult;
        std::uint64_t nextStableSequence = 1;
        double frameDeltaMilliseconds = 0.0;
        std::chrono::nanoseconds ownerWallTime{};
        bool ownerPrefixSucceeded = false;
        bool mainSceneUpdateRequired = false;
        bool observerPhase = false;
        SessionAdvanceResult *advanceResult = nullptr;
    };

    struct SessionRenderWorkerContext final
    {
        GameSession *session = nullptr;
        std::size_t frameResultIndex = 0;
    };

    bool RegisterLane(SessionId id) noexcept;
    bool QuiesceLane(SessionId id) noexcept;
    bool RequiresRenderTape(GameSession &session) const noexcept;
    std::uint64_t SurfaceGeneration(GameSession &session) const noexcept;
    void BeginOwnerCompletionPrefixes() noexcept;
    void PrepareFrameJobs(const ApplicationFramePlan &plan);
    void PrepareMainSceneTickJobs(const ApplicationFramePlan &plan, bool observers);
    bool ExecuteMainSceneTicks(const ApplicationFramePlan &plan) noexcept;
    void CompleteMainSceneTickPhase() noexcept;
    void PrepareSystemsWorkerJobs(const ApplicationFramePlan &plan);
    bool ExecuteSystemsWorkerPhase(const ApplicationFramePlan &plan) noexcept;
    void EndOwnerCompletionSuffixes() noexcept;
    bool ExecuteRenderWorkerPhase(const ApplicationFramePlan &plan,
                                  ApplicationOwnerWork ownerWork) noexcept;
    void FinalizeRenderWorkerPhase() noexcept;
#ifdef _DEBUG
    void BindHotspotProbes() noexcept;
    void RecordHotspotFrameSample() noexcept;
    void RecordHotspotRenderJob(const SessionAdvanceResult &result) noexcept;
#endif
    bool CommitBatch(SessionId sessionId, SessionGeneration generation, std::uint64_t frameSequence,
                     const SessionOrderedEffectBatch &batch) noexcept override;

    ApplicationSessionScheduler &scheduler_;
    ApplicationKeeper *applicationKeeper_ = nullptr;
    ApplicationAudio *applicationAudio_ = nullptr;
    SessionStorage sessions_;
    std::map<SessionId, SessionGeneration> generations_;
    std::vector<SessionVisualAnimationInput> frameVisualInputs_;
    std::vector<SessionPhysicsFrameInput> framePhysicsInputs_;
    std::vector<SessionFrameInput> frameInputs_;
    std::vector<SessionWorkerContext> frameWorkerContexts_;
    std::vector<SessionAdvanceJob> frameJobs_;
    std::vector<SessionAdvanceResult> frameResults_;
    std::vector<SessionAdvanceResult> framePhysicsResults_;
    std::vector<SessionAdvanceResult> frameTickResults_;
    std::vector<SessionFrameInput> frameRenderInputs_;
    std::vector<SessionRenderWorkerContext> frameRenderWorkerContexts_;
    std::vector<SessionAdvanceJob> frameRenderJobs_;
    std::vector<SessionAdvanceResult> frameRenderResults_;
    std::uint64_t frameSequence_ = 0;
    double frameDeltaMilliseconds_ = 0.0;
    SessionAdvancePhaseTimings lastAdvancePhaseTimings_;
#ifdef _DEBUG
    std::array<SessionHotspotFrameSample, SessionHotspotFrameHistorySize> hotspotFrameSamples_{};
    int hotspotFrameIndex_ = 0;
    std::array<SessionHotspotRenderJobSample, SessionHotspotFrameHistorySize> hotspotRenderJobs_{};
    int hotspotRenderJobIndex_ = 0;
    bool hotspotRenderJobEnabled_ = false;
#endif
    bool scheduling_ = true;
};
