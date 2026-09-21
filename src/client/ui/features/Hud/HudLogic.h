#pragma once
#include "domain/Automation.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "render/UiAdapter.h"
#include "session/SessionRuntime.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

class CSkillManager;
class CHARACTER_MACHINE;
class SessionGameplayUnit;
class SessionKeeper;
class CharacterRetirementTestPeer;

// Fixed-buffer model for skill hover tooltips. Built once per render call by
// `BuildModel`, then consumed by either the in-game renderer (which writes
// into the legacy TextList array) or the MuEditor renderer (which renders
// via ImGui).
// Centralized so the two renderers stay in sync on content order and
// special-case logic. Performance-conscious per CODING_RULES section 12:
// no heap allocations per render, fixed-size buffers throughout.
// See also:
//   - `SkillTooltip.{cpp,h}`              - in-game renderer
//   - `MuEditor/UI/SkillEditor/SkillTooltipEditor.cpp` - editor renderer

namespace UI::Skills::Tooltip
{

enum class LineColor : uint8_t
{
    White,
    Blue,
    Red,     // plain red text (unmet requirement lines, "(lacking N)" deficits)
    DarkRed, // white text on a red background (warnings, siege badge, brand info)
};

constexpr int MAX_TOOLTIP_LINE_TEXT = 128;
constexpr int MAX_TOOLTIP_LINES = 50;

struct Line
{
    wchar_t text[MAX_TOOLTIP_LINE_TEXT];
    LineColor color;
    bool isBold;
    bool isBlank; // emit a blank "\n" line for vertical spacing
};

// Fixed-capacity buffer. `count` lines are valid. `skipCount` mirrors the
// game tooltip's existing concept: certain lines (name, blanks) don't
// participate in positioning math, so the renderer counts them separately.
struct Model
{
    Line lines[MAX_TOOLTIP_LINES];
    int count;
    int skipCount;

    void Reset()
    {
        count = 0;
        skipCount = 0;
    }
};

// Options driving BuildModel. The hero-context fields are only read when
// `includeCharacterSpecific` is true (i.e. in-game). The editor passes
// `false` and the model omits all character-conditional content (class-
// specific damage calc, color-coded requirement comparisons, party teleport
// warnings, etc.).
struct BuildOptions
{
    int skillType;      // Resolved skill enum (e.g. AT_SKILL_FIRE_BALL)
    int skillSlotIndex; // Hero's action-bar slot, or -1 in editor
    bool includeCharacterSpecific;
};

struct DamageContext
{
    int heroClass;
    WORD strength;
    WORD dexterity;
    WORD vitality;
    WORD energy;
    WORD charisma;
    int magicMin;
    int magicMax;
    int skillMin;
    int skillMax;
    int skillAttackPowerRate;
};

class Builder;

class BuilderLegacyCalls : protected SessionLegacyCalls
{
  protected:
    BuilderLegacyCalls(SessionKeeper &keeper, Builder &owner) noexcept;

    void EmitBodyDamage(Model &m, int skillType);                                // OMF-01959
    void EmitRequirements(Model &m, const BuildOptions &options, int skillType); // OMF-01961
    DamageContext BuildDamageContext(int skillType);                             // OMF-01954
    void EmitBottomBanners(Model &model, const BuildOptions &options,
                           int skillType);      // OMF-01962
    void EmitBlueTags(Model &m, int skillType); // OMF-01963
    void EmitMagicalDamage(Model &model, int skillType,
                           const DamageContext &context); // OMF-01955

  private:
    Builder &owner_;
};

class Builder : protected BuilderLegacyCalls
{
    friend class BuilderLegacyCalls;

  public:
    explicit Builder(SessionKeeper &keeper) noexcept;
    void Build(const BuildOptions &options, Model &outModel);

  private:
    DamageContext BuildDamageContext(int skillType);
    void EmitBodyDamage(Model &model, int skillType);
    void EmitRequirements(Model &model, const BuildOptions &options, int skillType);
    void EmitBottomBanners(Model &model, const BuildOptions &options, int skillType);
    void EmitBlueTags(Model &model, int skillType);
    void EmitMagicalDamage(Model &model, int skillType,
                           const DamageContext &context); // OMF-01955

    CSkillManager &gSkillManager;
    SessionGameplayUnit &gameplay_;
    CHARACTER_MACHINE &characterMachine_;
};

} // namespace UI::Skills::Tooltip

namespace UI::Skills::Tooltip
{
class Renderer;

class SkillTooltipRendererLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    SkillTooltipRendererLegacyCalls(SessionKeeper &keeper, Renderer &owner) noexcept;

    void Render(int sx, int sy, int Type, int SkillNum, int iRenderPoint); // OMF-01942

  private:
    Renderer &owner_;
};

// Renders the hover tooltip for a skill in the action bar / skill list.
// Type is the slot index in CharacterAttribute->Skill[]. SkillNum is kept
// for signature compatibility with the original RenderSkillInfo (defaults
// to 0; the body never reads it). iRenderPoint anchors the tooltip
// relative to (sx, sy); STRP_NONE means "use legacy positioning".
class Renderer : protected SkillTooltipRendererLegacyCalls
{
    friend class SkillTooltipRendererLegacyCalls;

  public:
    explicit Renderer(SessionKeeper &keeper) noexcept;
    void Render(int sx, int sy, int Type, int SkillNum = 0, int iRenderPoint = STRP_NONE);

  private:
    Builder modelBuilder_;
};
} // namespace UI::Skills::Tooltip

class SessionGameplayUnit;
class SessionRenderUnit;
struct SessionInputEvent;

namespace SEASON3B
{
class CNewUICharacterInfoWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum
    {
        BTN_STAT_COUNT = 5,
        STAT_STRENGTH = 0,
        STAT_DEXTERITY,
        STAT_VITALITY,
        STAT_ENERGY,
        STAT_CHARISMA,
    };

    explicit CNewUICharacterInfoWindow(SessionKeeper &keeper);
    ~CNewUICharacterInfoWindow() override;

    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    float GetLayerDepth() override;
    void OpenningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    void ResetEquipmentLevel();

  private:
    float GetMasterSkillValue(ActionSkillType skill) const;
    float GetMasterSkillValue(ActionSkillType firstSkill, ActionSkillType secondSkill) const;
    int GetMasterSkillValueAsInt(ActionSkillType skill) const;
    int GetMasterSkillValueAsInt(ActionSkillType firstSkill, ActionSkillType secondSkill) const;
    bool BtnProcess();
    UI::Modern::PC::Character::RmlCharacterFrameContent BuildModernContent();
    void BuildModernProgress(UI::Modern::PC::Character::RmlCharacterFrameContent &content);
    std::wstring BuildPointAdjustmentText() const;
    void BuildModernAttributes(UI::Modern::PC::Character::RmlCharacterFrameContent &content);

    SessionGameplayUnit &gameplay_;
    SessionRenderUnit &renderer_;
    CNewUIManager *m_pNewUIMng = nullptr;
    UI::Modern::PC::Character::RmlCharacterFramePanel m_modernPanel;
    UI::Modern::PC::Character::RmlCharacterFrameContent modernContent_;
    bool modernVisible_ = false;
};
} // namespace SEASON3B

class SessionGameplayUnit;
class SessionRenderUnit;
struct SessionInputEvent;

namespace SEASON3B
{
class CNewUIManager;

class CNewUIPetInfoWindow final : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIPetInfoWindow(SessionKeeper &keeper);
    ~CNewUIPetInfoWindow() override;

    bool Create(CNewUIManager *manager, int x, int y);
    void Release();
    void SetPos(int x, int y);
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    bool Update() override;
    bool Render() override;
    float GetLayerDepth() override;
    void OpenningProcess();
    void ClosingProcess();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);

  private:
    enum PetTab
    {
        DarkHorseTab = 0,
        DarkSpiritTab,
    };

    bool BtnProcess();
    void CalcDamage(int tab);
    UI::Modern::PC::Character::RmlPetInfoContent BuildModernContent() const;

    SessionGameplayUnit &gameplay_;
    SessionRenderUnit &renderer_;
    CNewUIManager *manager_ = nullptr;
    UI::Modern::PC::Character::RmlPetInfoPanel modernPanel_;
    UI::Modern::PC::Character::RmlPetInfoContent modernContent_;
    bool modernVisible_ = false;
    int selectedTab_ = DarkHorseTab;
    int damage_[2]{};
};
} // namespace SEASON3B

namespace SEASON3B
{
class CNewUIBuffWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIBuffWindow(SessionKeeper &keeper);
    virtual ~CNewUIBuffWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);
    void SetPos(int iScreenWidth);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth(); //. 5.3f

    void OpenningProcess();
    void ClosingProcess();

  private:
    void ProcessBuffCancel();
    void BuffSort(std::list<eBuffState> &buffstate);
    void RenderBuffTooltip(eBuffClass &eBuffClassType, eBuffState &eBuffType, float x, float y);
    bool SetDisableRenderBuff(const eBuffState &_BuffState);

    SessionRenderUnit &renderer_;
    UI::Modern::RmlBuffLayer buffs_;
    std::vector<UI::Modern::RmlBuffLayer::Entry> entries_;
    bool visible_ = false;
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;
};
} // namespace SEASON3B

namespace SEASON3A
{
class CursedTemple;
}

namespace SEASON3B
{
class CNewUIManager;

class CNewUICommandWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    enum eIMAGE_LIST
    {
        IMAGE_COMMAND_SELECTID_BG = BITMAP_COMMAND_WINDOW_BEGIN,
    };

    explicit CNewUICommandWindow(SessionKeeper &keeper);
    ~CNewUICommandWindow() override;

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();
    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool BtnProcess();

    float GetLayerDepth();
    void OpenningProcess();
    void ClosingProcess();

    int GetCurCommandType();
    void BeginTargetCommand(int command);
    void SetMouseCursor(int iCursorType);
    int GetMouseCursor();

    bool CommandTrade(CHARACTER *pSelectedCha);
    bool CommandPurchase(CHARACTER *pSelectedCha);
    bool CommandParty(SHORT iChaKey);
    bool CommandWhisper(CHARACTER *pSelectedCha);
    bool CommandGuild(CHARACTER *pSelectedCha);
    bool CommandGuildUnion(CHARACTER *pSelectedCha);
    bool CommandGuildRival(CHARACTER *pSelectedCha);
    bool CommandCancelGuildRival(CHARACTER *pSelectedCha);
    bool CommandAddFriend(CHARACTER *pSelectedCha);
    bool CommandFollow(int iSelectedChaIndex);
    int CommandDual(CHARACTER *pSelectedCha);

  private:
    void LoadImages();
    void UnloadImages();
    void RenderSelectedCharacter();
    void RunCommand();
    void SelectCommand();
    void UpdateButtonAvailability();

    SEASON3A::CursedTemple &cursedTemple_;
    CNewUIManager *m_pNewUIMng = nullptr;
    UI::Modern::PC::Command::RmlCommandWindowPanel m_modernPanel;
    int m_iCurSelectCommand = COMMAND_NONE;
    int m_iCurMouseCursor = CURSOR_NORMAL;
    bool m_bSelectedChar = false;
    bool m_bCanCommand = false;
};
} // namespace SEASON3B

#ifdef PBG_ADD_GENSRANKING

#define MAX_TITLELENGTH 32

namespace SEASON3B
{
class CNewUIGensRanking : public CNewUIObj, protected SessionUiLegacyBindings
{
    enum IMAGE_LIST
    {
        IMAGE_BACK = CNewUIMessageBoxMng::IMAGE_MSGBOX_BACK,
        IMAGE_TOP = CNewUIMyInventory::IMAGE_INVENTORY_BACK_TOP2,
        IMAGE_LEFT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_LEFT,
        IMAGE_RIGHT = CNewUIMyInventory::IMAGE_INVENTORY_BACK_RIGHT,
        IMAGE_BOTTOM = CNewUIMyInventory::IMAGE_INVENTORY_BACK_BOTTOM,
        IMAGE_CLOSE_REGIST = CNewUIMessageBoxMng::IMAGE_MSGBOX_BTN_EMPTY_SMALL,
        IMAGE_GENS_EXIT_BTN = CNewUIMyInventory::IMAGE_INVENTORY_EXIT_BTN,
        IMAGE_NEWMARK_DUPRIAN = BITMAP_GENS_MARK_DUPRIAN,
        IMAGE_NEWMARK_BARNERT = BITMAP_GENS_MARK_BARNERT,

        IMAGE_GENSINFO_TOP_LEFT = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_LEFT,
        IMAGE_GENSINFO_TOP_RIGHT = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_RIGHT,
        IMAGE_GENSINFO_BOTTOM_LEFT = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_LEFT,
        IMAGE_GENSINFO_BOTTOM_RIGHT = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_RIGHT,
        IMAGE_GENSINFO_TOP_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_TOP_PIXEL,
        IMAGE_GENSINFO_BOTTOM_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_BOTTOM_PIXEL,
        IMAGE_GENSINFO_LEFT_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_LEFT_PIXEL,
        IMAGE_GENSINFO_RIGHT_PIXEL = CNewUIInventoryCtrl::IMAGE_ITEM_TABLE_RIGHT_PIXEL,

        IMAGE_RANKBACK = BITMAP_GENS_RANKBACK,
        IMAGE_RANKBACK_TEXTBOX = BITMAP_INTERFACE_NEW_CHAINFO_WINDOW_BEGIN,
    };

    static constexpr float GENSRANKING_WIDTH = 190.0f;
    static constexpr float GENSRANKING_HEIGHT = 429.0f;
    static constexpr int TEAMNAME_LENTH = 10;
    static constexpr float GENSMARK_WIDTH = 50.0f;
    static constexpr float GENSMARK_HEIGHT = 69.0f;
    static constexpr float GENSRANKBACK_WIDTH = 170.0f;
    static constexpr float GENSRANKBACK_HEIGHT = 88.0f;
    static constexpr float GENSRANKTEXTBACK_WIDTH = 170.0f;
    static constexpr float GENSRANKTEXTBACK_HEIGHT = 21.0f;

    enum IMAGE_INDEX
    {
        TITLENAME_NONE = 0,
        TITLENAME_START = 1,
        TITLENAME_END = 14,
    };

  public:
    enum IMAGE_AREA
    {
        MARK_UIINFO = 0,
        MARK_BOOLEAN,
        MARK_RANKINFOWIN,
    };

    enum GENS_TYPE
    {
        GENSTYPE_NONE = 0,
        GENSTYPE_DUPRIAN,
        GENSTYPE_BARNERT,
    };

  private:
    void Init();
    void Destroy();
    void StageContent();

    POINT m_Pos;

    FLOAT m_fBooleanSize;

    int m_nContribution;
    wchar_t m_szRanking[TEAMNAME_LENTH];
    wchar_t m_szGensTeam[TEAMNAME_LENTH];

    GENS_TYPE m_byGensInfluence;
    POINT m_ptRenderMarkPos;

    SessionRenderUnit &renderer_;
    UI::Modern::PC::Gens::RmlGensRankingPanel panel_;
    UI::Modern::PC::Gens::RmlGensRankingPanel::Content content_;
    std::string locale_;
    int stagedRank_ = -1;
    bool contentDirty_ = true;
    bool visible_ = false;

    wchar_t m_szTitleName[TITLENAME_END][MAX_TITLELENGTH];

    int m_nNextContribution;

  public:
    CNewUIManager *m_pNewUIMng = nullptr;
    explicit CNewUIGensRanking(SessionKeeper &keeper);
    virtual ~CNewUIGensRanking();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void SetPos(int x, int y);
    const POINT &GetPos()
    {
        return m_Pos;
    }

    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

    bool Update();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();

    void OpenningProcess();
    void ClosingProcess();

    float GetLayerDepth() override;

    void SetContribution(int _Contribution);
    int GetContribution();

    void SetNextContribution(int _NextContribution);
    int GetNextContribution();

    bool SetRanking(int _Ranking);
    wchar_t *GetRanking();

    bool SetGensInfo();
    bool SetGensTeamName(const wchar_t *_pTeamName);
    wchar_t *GetGensTeamName();

    void SetTitleName();
    wchar_t *GetTitleName(BYTE _index);

    void RanderMark(float _x, float _y, GENS_TYPE _GensInfluence, BYTE _GensRankInfo,
                    IMAGE_AREA _ImageArea = MARK_RANKINFOWIN, float _RenderY = 0);
    int GetImageIndex(BYTE _index);
};
} // namespace SEASON3B

#endif //PBG_ADD_GENSRANKING

namespace MUHelper
{
class SessionMuHelperUnit;
}
class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIHeroPositionInfo : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_CurHeroPosition;
    MUHelper::SessionMuHelperUnit &muHelper_;
    SessionRenderUnit &renderer_;
    CNewUIButton m_BtnConfig;
    CNewUIButton m_BtnStart;
    CNewUIButton m_BtnStop;

  public:
    explicit CNewUIHeroPositionInfo(SessionKeeper &keeper);
    virtual ~CNewUIHeroPositionInfo();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    float GetLayerDepth(); //. 4.3f

    void OpenningProcess();
    void ClosingProcess();

    void SetCurHeroPosition(int x, int y);

  private:
    void StageTopMenu();
    void UpdateButtonGeometry();
    static ButtonVisualState ToTopMenuButtonState(BUTTON_STATE state) noexcept;
};
} // namespace SEASON3B

class CmuConsoleDebug;
class CErrorReport;

class SessionGameplayUnit;
namespace MUHelper
{
class SessionMuHelperUnit;
}

namespace SEASON3B
{
class CNewUIManager;

class CNewUIHotKey : public CNewUIObj, protected SessionUiLegacyBindings
{
    SessionGameplayUnit &gameplay;
    MUHelper::SessionMuHelperUnit &muHelper;
    CInGameShopSystem &g_InGameShopSystem;
    ITEM_t (&Items)[MAX_ITEMS];
    CNewUIManager *m_pNewUIMng;
    bool m_bStateGameOver;

  public:
    explicit CNewUIHotKey(SessionKeeper &keeper);
    virtual ~CNewUIHotKey();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    float GetLayerDepth();    //. 1.0f
    float GetKeyEventOrder(); //. 1.0f

    bool CanUpdateKeyEvent();
    bool CanUpdateKeyEventRelatedMyInventory();

    void SetStateGameOver(bool bGameOver); // 게임오버중인 상태
    bool IsStateGameOver();

    bool AutoGetItem();

  private:
    bool OpenSystemMenuOnEscape();
    void ResetMouseRButton();
};
} // namespace SEASON3B

class SessionGameplayUnit;
class SessionRenderUnit;

class CmuConsoleDebug;
class SessionKeeper;
class CSkillManager;
class CMonkSystem;

namespace SEASON3B
{
enum
{
    HOTKEY_Q = 0,
    HOTKEY_W,
    HOTKEY_E,
    HOTKEY_R,
    HOTKEY_COUNT
};

enum
{
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    MAINFRAME_BTN_PARTCHARGE = 0,
#endif //defined PBG_ADD_INGAMESHOP_UI_MAINFRAME
    MAINFRAME_BTN_CHAINFO,
    MAINFRAME_BTN_MYINVEN,
    MAINFRAME_BTN_FRIEND,
    MAINFRAME_BTN_WINDOW,
};

enum KINDOFSKILL
{
    KOS_COMMAND = 1,
    KOS_SKILL1,
    KOS_SKILL2,
    KOS_SKILL3,
};

class CNewUIItemHotKey : protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIItemHotKey(SessionKeeper &keeper);
    virtual ~CNewUIItemHotKey();

    bool UpdateKeyEvent();
    void UpdateMouseEvent();

    void SetHotKey(int iHotKey, int iItemType, int iItemLevel);
    int GetHotKey(int iHotKey);
    int GetHotKeyLevel(int iHotKey);
    void UseItemRButton();
    void RenderItems();
    void RenderItemCount();
    void RenderItemHotKeyTooltip();

  private:
    int GetHotKeyItemIndex(int iType, bool bItemCount = false);
    bool GetHotKeyCommonItem(IN int iHotKey, OUT int &iStart, OUT int &iEnd);
    int GetHotKeyItemCount(int iType);

    int m_iHotKeyItemType[HOTKEY_COUNT];
    int m_iHotKeyItemLevel[HOTKEY_COUNT];
    int m_iTooltipHotKey;
};

class CNewUISkillList : public CNewUIObj, protected SessionUiLegacyBindings
{
    enum
    {
        SKILLHOTKEY_COUNT = 10
    };
    enum EVENT_STATE
    {
        EVENT_NONE = 0,

        // currentskill
        EVENT_BTN_HOVER_CURRENTSKILL,
        EVENT_BTN_DOWN_CURRENTSKILL,

        // skillhotkey
        EVENT_BTN_HOVER_SKILLHOTKEY,
        EVENT_BTN_DOWN_SKILLHOTKEY,

        // skill hot-key page
        EVENT_BTN_HOVER_SKILLPAGE,
        EVENT_BTN_DOWN_SKILLPAGE,

        // skilllist
        EVENT_BTN_HOVER_SKILLLIST,
        EVENT_BTN_DOWN_SKILLLIST,
    };

  public:
    enum IMAGE_LIST
    {
        IMAGE_SKILL1 = BITMAP_INTERFACE_NEW_SKILLICON_BEGIN,
        IMAGE_SKILL2,
        IMAGE_COMMAND,
        IMAGE_SKILL3,
        IMAGE_SKILLBOX,
        IMAGE_SKILLBOX_USE,
        IMAGE_NON_SKILL1,
        IMAGE_NON_SKILL2,
        IMAGE_NON_COMMAND,
        IMAGE_NON_SKILL3,
    };

    explicit CNewUISkillList(SessionKeeper &keeper);
    virtual ~CNewUISkillList();

    bool Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void RenderSkillInfo();
    float GetLayerDepth(); // 10.6f

    void Reset();

    void SetHotKey(int iHotKey, int iSkillType);
    int GetHotKey(int iHotKey);
    int GetSelectedMainFrameHotSlot() const;
    void FillMainFrameSkills(UI::Modern::RmlMainFrameRequest &request);
    bool PrepareModernUiOnWorker(int width, int height);

    bool IsSkillListUp();

    static void UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB);

  private:
    void LoadImages();
    void UnloadImages();
    bool IsArrayUp(BYTE bySkill);
    bool IsArrayIn(BYTE bySkill);
    void UseHotKey(int iHotKey);
    void ToggleSkillHotKeyPage();

    bool IsSkillIconDisabled(ActionSkillType type);
    float SkillCooldownRatio(int index);
    void FillMainFrameSkill(UI::Modern::RmlMainFrameRequest &request, int slot, int index);
    void RenderHotKeyNumber(int hotKey, float x, float y, float width, float height);
    void SetSkillTooltip(int type, float x, float y, float width);

    void ResetMouseLButton();

  private:
    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;
    UI::Modern::RmlSkillListLayer skillImages_;
    std::vector<UI::Modern::RmlSkillListLayer::Entry> skillEntries_;
    SessionRenderUnit &skillRenderer_;
    CSkillManager &gSkillManager;
    SessionGameplayUnit &gameplay_;
    CMonkSystem &g_CMonkSystem;
    UI::Skills::Tooltip::Renderer m_SkillTooltip;

    bool m_bHotKeySkillListUp;
    int m_iHotKeySkillType[SKILLHOTKEY_COUNT];

    bool m_bSkillList;

    bool m_bRenderSkillInfo;
    int m_iRenderSkillInfoType;
    int m_iRenderSkillInfoPosX;
    int m_iRenderSkillInfoPosY;

    EVENT_STATE m_EventState;
    int m_iLastPageSkill;
};

class CNewUIMainFrameWindow : public CNewUIObj,
                              public INewUI3DRenderObj,
                              protected SessionUiLegacyBindings
{
  public:
    enum IMAGE_LIST
    {
        IMAGE_MENU_1 = BITMAP_INTERFACE_NEW_MAINFRAME_BEGIN, // newui_menu01.jpg
        IMAGE_MENU_2,                                        // newui_menu02.jpg
        IMAGE_MENU_3,                                        // newui_menu03.jpg
        IMAGE_MENU_2_1,
        IMAGE_GAUGE_BLUE,       // newui_menu_blue.tga
        IMAGE_GAUGE_GREEN,      // newui_menu_green.tga
        IMAGE_GAUGE_RED,        // newui_menu_red.tga
        IMAGE_GAUGE_AG,         // newui_menu_AG.tga
        IMAGE_GAUGE_SD,         // newui_menu_SD.tga
        IMAGE_GAUGE_EXBAR,      // newui_Exbar.jpg
        IMAGE_MASTER_GAUGE_BAR, // Exbar_Master.jpg
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
        IMAGE_MENU_BTN_CSHOP,
#endif //defined PBG_ADD_INGAMESHOP_UI_MAINFRAME
        IMAGE_MENU_BTN_CHAINFO,
        IMAGE_MENU_BTN_MYINVEN,
        IMAGE_MENU_BTN_FRIEND,
        IMAGE_MENU_BTN_WINDOW,
    };

    explicit CNewUIMainFrameWindow(SessionKeeper &keeper);
    virtual ~CNewUIMainFrameWindow();

    bool Create(CNewUIManager *pNewUIMng, CNewUI3DRenderMng *pNewUI3DRenderMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    void Render3D();

    bool IsVisible() const;

    float GetLayerDepth();    // 10.2f
    float GetKeyEventOrder(); // 7.f

    void SetItemHotKey(int iHotKey, int iItemType, int iItemLevel);
    int GetItemHotKey(int iHotKey);
    int GetItemHotKeyLevel(int iHotKey);
    void UseHotKeyItemRButton();
    //void RenderHotKeyItems();
    void UpdateItemHotKey();

    void ResetSkillHotKey();
    void SetSkillHotKey(int iHotKey, int iSkillType);
    int GetSkillHotKey(int iHotKey);
    int GetSkillHotKeyIndex(int iSkillType);

    void SetPreExp_Wide(__int64 dwPreExp);
    void SetGetExp_Wide(__int64 dwGetExp);

    void SetPreExp(__int64 dwPreExp);
    void SetGetExp(__int64 dwGetExp);

    // buttons
    void SetBtnState(int iBtnType, bool bStateDown);

    static void UI2DEffectCallback(LPVOID pClass, DWORD dwParamA, DWORD dwParamB);

  private:
    void SetButtonInfo();
    void UpdateButtonGeometry();

    bool BtnProcess();

    void RenderHotKeyItemCount();
    void StageMainFrame();
    void FillExperienceRequest(UI::Modern::RmlMainFrameRequest &request) const;

  public:
    __int64 m_loPreExp;
    __int64 m_loGetExp;

  private:
    CUIFriendMenu &g_pFriendMenu;
    CInGameShopSystem &g_InGameShopSystem;
    SessionRenderUnit &renderer_;
    CNewUIManager *m_pNewUIMng;
    CNewUI3DRenderMng *m_pNewUI3DRenderMng;

    CNewUIItemHotKey m_ItemHotKey;

    bool m_bExpEffect;
    DWORD m_dwExpEffectTime;

    __int64 m_dwPreExp;
    __int64 m_dwGetExp;

#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIButton m_BtnCShop;
#endif //defined PBG_ADD_INGAMESHOP_UI_MAINFRAME
    CNewUIButton m_BtnChaInfo;
    CNewUIButton m_BtnMyInven;
    CNewUIButton m_BtnQuest;
    CNewUIButton m_BtnFriend;
    CNewUIButton m_BtnWindow;

    bool m_bButtonBlink;
};
} // namespace SEASON3B

class CErrorReport;

constexpr auto MAX_MASTER_SKILL_DATA = 512;

constexpr auto MAX_MASTER_SKILL_CATEGORY = 3;
constexpr auto MAX_MASTER_SKILL_REQUIRES = 2;
constexpr auto MASTER_SKILL_LEVEL_REQ_FOR_NEXT_RANK = 10;
constexpr auto MAX_MASTER_TREE_RANK = 10;

enum MASTER_SKILL_TREE_CLASS : WORD
{
    MASTER_SKILL_TREE_CLASS_NONE = 0,
    MASTER_SKILL_TREE_CLASS_BLADEMASTER = 1,
    MASTER_SKILL_TREE_CLASS_GRANDMASTER = 2,

    MASTER_SKILL_TREE_CLASS_HIGHELF = 4,
    MASTER_SKILL_TREE_CLASS_DIMENSIONMASTER = 8,
    MASTER_SKILL_TREE_CLASS_DUELMASTER = 16,
    MASTER_SKILL_TREE_CLASS_LORDEMPEROR = 32,
    MASTER_SKILL_TREE_CLASS_TEMPLEKNIGHT = 64,
};

DEFINE_ENUM_FLAG_OPERATORS(MASTER_SKILL_TREE_CLASS);

struct _MASTER_SKILLTREE_DATA
{
    WORD Index;
    MASTER_SKILL_TREE_CLASS ClassCode;
    BYTE Group;
    BYTE RequiredPoints;
    BYTE MaxLevel;
    BYTE ArrowDirection;
    ActionSkillType RequireSkill[MAX_MASTER_SKILL_REQUIRES];
    ActionSkillType Skill;
    float DefValue;
};

struct _MASTER_SKILL_TOOLTIP
{
    ActionSkillType SkillNumber;
    MASTER_SKILL_TREE_CLASS ClassCode;
    wchar_t Info1[64];
    wchar_t Info2[256];
    wchar_t Info3[32];
    wchar_t Info4[64];
    wchar_t Info5[64];
    wchar_t Info6[64];
    wchar_t Info7[64];
};

struct _MASTER_SKILL_TOOLTIP_FILE
{
    int SkillNumber;
    MASTER_SKILL_TREE_CLASS ClassCode;
    char Info1[64];
    char Info2[256];
    char Info3[32];
    char Info4[64];
    char Info5[64];
    char Info6[64];
    char Info7[64];
};

namespace SEASON3B
{
//size = 404
class CNewUIMasterLevel : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIMasterLevel(SessionKeeper &keeper);
    ~CNewUIMasterLevel() override;

    BYTE GetConsumePoint() const;
    int GetCurSkillID() const;
    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    void OpenMasterSkillTreeData(const wchar_t *path);
    void OpenMasterSkillTooltip(const wchar_t *path);
    void InitMasterSkillPoint();
    void SetMasterType(CLASS_TYPE Class);

    bool SetMasterSkillTreeInfo(int index, BYTE skillLevel, float value, float nextValue);
    void SkillUpgrade(int index, BYTE skillLevel, float value, float nextValue);

    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool Render() override;
    bool Update() override;
    bool UpdateMouseEvent() override;
    bool UpdateKeyEvent() override;
    float GetLayerDepth() override;

  private:
    SessionRenderUnit &renderer_;
    UI::Modern::RmlMasterSkillTreePanel modernPanel_;
    UI::Modern::RmlMasterSkillTreePanel::State modernContent_;
    std::array<std::uint64_t, 8> modernTextKey_{};
    bool modernTextSet_ = false;
    int CategoryPoint[MAX_MASTER_SKILL_CATEGORY];
    int skillPoint[MAX_MASTER_SKILL_CATEGORY][MAX_MASTER_TREE_RANK];
    BYTE ConsumePoint;
    int CurSkillID;
    MASTER_SKILL_TREE_CLASS classCode;
    DWORD CategoryTextIndex;
    DWORD ClassNameTextIndex;

    std::map<ActionSkillType, _MASTER_SKILL_TOOLTIP> map_masterSkillToolTip;
    std::map<BYTE, _MASTER_SKILLTREE_DATA> map_masterData;
    _MASTER_SKILLTREE_DATA m_stMasterSkillTreeData[MAX_MASTER_SKILL_DATA]{};
    _MASTER_SKILL_TOOLTIP m_stMasterSkillTooltip[MAX_MASTER_SKILL_DATA]{};

    CNewUIManager *m_pNewUIMng;

    int SetDivideString(wchar_t *text, int isItemTollTip, int TextNum, int iTextColor,
                        int iTextBold, bool isPercent);

    bool TryUpgradeSkill(const _MASTER_SKILLTREE_DATA &skillData);
    bool CheckSkillPoint(WORD mLevelUpPoint, const _MASTER_SKILLTREE_DATA &skillData,
                         BYTE skillLevel);
    bool CheckParentSkill(const _MASTER_SKILLTREE_DATA &masterSkill);
    bool CheckRankPoint(BYTE group, BYTE rank, BYTE skillLevel);
    bool CheckBeforeSkill(ActionSkillType skill, BYTE skillLevel);

    int GetBeforeSkillID(int index);

    void SetMasterSkillTreeData();
    void SetMasterSkillToolTipData();

    void ClearSkillTreeData();
    void ClearSkillTooltipData();

    void UpdateModernContent();
    void UpdateModernLabels();
    void RenderToolTip();
    void RenderSkillToolTip(const _MASTER_SKILLTREE_DATA &data,
                            const UI::Modern::RmlMasterSkillTreePanel::Hover &hover);
    void ConsumeModernActions();
};
} // namespace SEASON3B

class CmuConsoleDebug;

class SessionRenderUnit;

namespace SEASON3B
{
class CNewUIMoveCommandWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  private:
    CNewUIManager *m_pNewUIMng;
    std::vector<CMoveCommandData::MOVEINFODATA *> moveEntries_;
    std::array<int, UI::Modern::RmlMoveCommandFavoriteRows> favoriteIndices_{};
    std::size_t scrollOffset_ = 0;
    int hoveredMainRow_ = -1;
    int hoveredFavoriteRow_ = -1;
    bool thumbDragging_ = false;
    int thumbDragMouseY_ = 0;
    std::size_t thumbDragStartOffset_ = 0;
    DWORD m_dwMoveCommandKey = 0;
    SessionRenderUnit &renderer_;
    CNewUIButton showMapButton_;
    CNewUIButton closeButton_;
    CNewUIButton scrollUpButton_;
    CNewUIButton scrollDownButton_;

  public:
    explicit CNewUIMoveCommandWindow(SessionKeeper &keeper);
    virtual ~CNewUIMoveCommandWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();

    bool BtnProcess();

    virtual void OpenningProcess();
    void ClosingProcess();
    float GetLayerDepth();

    bool IsLuckySealBuff();
    bool IsMapMove(const std::wstring &src);

    void SetMoveCommandKey(DWORD dwKey);
    DWORD GetMoveCommandKey();

    BOOL IsTheMapInDifferentServer(const int iFromMapIndex, const int iToMapIndex) const;
    int GetMapIndexFromMovereq(const wchar_t *pszMapName);

  private:
    void SetStrifeMap();
    void SettingCanMoveMap();
    bool CanMoveToMap(CMoveCommandData::MOVEINFODATA &moveInfo) const;
    int AdjustedRequiredLevel(const CMoveCommandData::MOVEINFODATA &moveInfo) const;
    void StageMoveCommand();
    void UpdateButtonGeometry();
    void UpdateHoveredRows();
    void ScrollBy(int amount);
    void ToggleFavorite(std::size_t mapIndex);
    void RemoveFavorite(std::size_t favoriteIndex);
    void WarpTo(std::size_t mapIndex);
    CMoveCommandData::MOVEINFODATA *MoveAt(std::size_t index) const;
    std::size_t MaximumScrollOffset() const noexcept;
    bool IsFavorite(std::size_t mapIndex) const noexcept;
    UI::Modern::RmlMoveCommandRect ReferenceRect(float x, float y, float width,
                                                 float height) const noexcept;
    bool MouseIn(const UI::Modern::RmlMoveCommandRect &rect) const;
    static ButtonVisualState ToButtonState(BUTTON_STATE state) noexcept;
};
}; // namespace SEASON3B

namespace SEASON3B
{
class CNewUIQuickCommandWindow : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIQuickCommandWindow(SessionKeeper &keeper);
    virtual ~CNewUIQuickCommandWindow();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void SetPos(int x, int y);

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    bool PrepareModernUiOnWorker(int width, int height);
    bool ProcessModernUiInput(const SessionInputEvent &event);

    float GetLayerDepth();    //. 2.0f
    float GetKeyEventOrder(); // 10.f;

    void OpenningProcess();
    void ClosingProcess();
    void OpenQuickCommand(const wchar_t *strID, int iIndex, int x, int y);
    void CloseQuickCommand();
    void SetID(const wchar_t *strID);
    void SetSelectedCharacterIndex(int iIndex);

  private:
    void ExecuteCommand(int command);
    bool IsTargetAvailable() const;
    void StageModernContent();

  private:
    CNewUIManager *m_pNewUIMng;
    POINT m_Pos;

    wchar_t m_strID[32];
    int m_iSelectedCharacterIndex;
    SessionRenderUnit &renderer_;
    UI::Modern::RmlContextMenuPanel modernPanel_;
    UI::Modern::RmlContextMenuPanel::Content modernContent_;
};
} // namespace SEASON3B

class SessionKeeper;

namespace UI
{
class NoticeBoard;

class NoticeLegacyCalls : protected SessionUiLegacyBindings
{
  protected:
    NoticeLegacyCalls(SessionKeeper &keeper, NoticeBoard &owner) noexcept;
    void Clear();

    void Scroll();                               // OMF-01936
    void Create(const wchar_t *text, int color); // OMF-01938

  private:
    NoticeBoard &owner_;
};

class NoticeBoard final : protected NoticeLegacyCalls
{
  public:
    explicit NoticeBoard(SessionKeeper &keeper) noexcept;

    void Create(const wchar_t *text, int color);
    void Clear();
    void Move(float animationFactor);
    void Render();

  private:
    friend class NoticeLegacyCalls;
    friend class ::CharacterRetirementTestPeer;

    static constexpr int MaxNotices = 6;
    static constexpr int NoticeLifetime = 300;
    static constexpr float BlinkCycleFrames = 10.f;
    static constexpr int NoticeTextMax = 256;

    struct Notice
    {
        wchar_t text[NoticeTextMax]{};
        int lifeTime = 0;
        BYTE color = 0;
    };

    void Scroll();

    int count_ = 0;
    float time_ = NoticeLifetime;
    float blinkPhase_ = 0.f;
    Notice notices_[MaxNotices]{};
};
} // namespace UI

namespace SEASON3B
{
class CNewUIMuHelper : public CNewUIObj, protected SessionUiLegacyBindings
{
  public:
    explicit CNewUIMuHelper(SessionKeeper &keeper);
    ~CNewUIMuHelper();

    bool Create(CNewUIManager *pNewUIMng, int x, int y);
    void Release();

    void Show(bool bShow);
    bool Render();
    bool Update();
    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool PrepareModernUiOnWorker(int viewportWidth, int viewportHeight);
    bool ProcessModernUiInput(const SessionInputEvent &event);
    bool HasTextInputFocus() const noexcept;
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;

    float GetLayerDepth();
    float GetKeyEventOrder();

  public:
    void Reset();
    void LoadSavedConfig(const MUHelper::ConfigData &config);
    void AssignSkill(int iSkill);

  private:
    static constexpr int MAX_SKILLS_SLOT = 6;

    int GetSkillIndex(int iSkill);
    bool IsSkillAssigned(int iSkill);

    void ApplyConfig();

    void ApplyConfigFromSkillSlot(int iSlot, int iSkill);
    void ApplyModernChanges();
    void FillModernSkills(UI::Modern::PC::MuHelper::RmlMuHelperContent &content) const;
    void ApplyModernForm(const UI::Modern::PC::MuHelper::RmlMuHelperFormValues &form);
    UI::Modern::PC::MuHelper::RmlMuHelperContent ModernContent() const;
    void OpenModernSubPage(int request);
    void ResetModernSubPage();
    void CancelModernSubPage();

  private:
    MUHelper::SessionMuHelperUnit &muHelper_;
    MUHelper::ConfigData &_TempConfig;
    UI::Modern::PC::MuHelper::RmlMuHelperPanel m_modernPanel;
    MUHelper::ConfigData m_modernSubBackup{};
    CNewUIManager *m_pNewUIMng;

    int m_iCurrentOpenTab;
    int m_iCurrentOpenSubWin;
    int m_iSelectedSkillSlot;
    std::array<int, MAX_SKILLS_SLOT> m_aiSelectedSkills;
    std::wstring m_modernItemInput;
};

class CNewUIMuHelperSkillList : public CNewUIObj, protected SessionUiLegacyBindings
{

  public:
    explicit CNewUIMuHelperSkillList(SessionKeeper &keeper);
    ~CNewUIMuHelperSkillList();

    bool Create(CNewUIManager *pNewUIMng);
    void Release();

    bool UpdateMouseEvent();
    bool UpdateKeyEvent();
    bool Update();
    bool Render();
    float GetLayerDepth();

    void FilterByAttackSkills();
    void FilterByBuffSkills();
    const std::vector<int> &Skills() const noexcept;

  private:
  private:
    void PrepareSkillsToRender();

    bool IsAttackSkill(int iSkillType);
    bool IsBuffSkill(int iSkillType);
    bool IsHealingSkill(int iSkillType);
    bool IsDefenseSkill(int iSkillType);

  private:
    CNewUIManager *m_pNewUIMng;

    bool m_bFilterByAttackSkills = false;
    bool m_bFilterByBuffSkills = false;
    std::vector<int> m_aiSkillsToRender;
};

} // namespace SEASON3B

#define BUFFINDEX(buff) static_cast<eBuffState>(buff)
#define BUFFTIMEINDEX(timetype) static_cast<eBuffTimeType>(timetype)
#define ITEMINDEX(type, index) static_cast<DWORD>((type * MAX_ITEM_INDEX) + index)

namespace MainFrameDetail
{

#pragma pack(push)
#pragma pack()
enum class MainFrameDesignKey
{
    MainFrameSkillIconWidth,
    MainFrameSkillIconHeight,
    MainFrameCurrentSkillX,
    MainFrameSkillY,
    MainFrameHotKeyFirstX,
    MainFrameHotKeyStep,
    MainFrameSkillPageButtonX,
    MainFrameSkillPageButtonY,
    MainFrameSkillPageButtonWidth,
    MainFrameSkillPageButtonHeight,
    MainFrameItemFirstX,
    MainFrameItemY,
    MainFrameItemStep,
    MainFrameItemWidth,
    MainFrameItemHeight,
    MainFrameItemCountImageX,
    MainFrameItemCountImageY,
    MainFrameItemCountScaleX,
    MainFrameItemCountScaleY,
    MainFrameItemCountDigitStep,
    MainFrameItemCountDigitWidth,
    MainFrameItemCountDigitHeight,
    MainFrameItemCountDigitLimit,
    SkillListSlotWidth,
    SkillListSlotHeight,
    SkillIconSourceWidth,
    SkillIconSourceHeight,
    MainFrameButtonY,
    MainFrameButtonWidth,
    MainFrameButtonHeight,
    MainFrameButtonX,
    SkillListIconOffset,
    ExperienceEffectDuration,
    NumberAtlasWidth,
    NumberAtlasHeight,
    SkillAtlasSize,
    MasterSkillAtlasSize,
    HotKeyNumberColor,
    HotKeyNumberBaseScale,
    HotKeyNumberWidthScale,
    HotKeyNumberOffsetY,
    CooldownColor,
    SkillListCenteredCount,
    SkillListFirstRowCount,
    SkillListLeftRunStart,
    SkillListSecondRowStart
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
struct SkillListGeometry final
{
    float originX;
    float originY;
    float petOriginX;
    float petOriginY;
    float slotWidth;
    float slotHeight;
    float iconOffsetX;
    float iconOffsetY;
    float iconWidth;
    float iconHeight;
};
#pragma pack(pop)

} // namespace MainFrameDetail

namespace MoveCommandDetail
{
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
inline constexpr int MapNameCount = 6;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum class DesignKey
{
    PanelWidth,
    PanelHeight,
    MainListX,
    MainListY,
    FavoriteListY,
    ShowMapX,
    CloseX,
    TextButtonY,
    TextButtonWidth,
    TextButtonHeight,
    ScrollBarX,
    ScrollBarY,
    RowHeight,
    RowWidth,
    CheckBoxX,
    CheckBoxWidth,
    ScrollUpRect,
    ScrollThumbRect,
    ScrollDownY,
    ScrollTrackHeight
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr wchar_t FavoriteLabel[] = L"Favorite";
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr wchar_t ShowMapLabel[] = L"Show Map";
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr std::array<const wchar_t *, MapNameCount> LuckySealMapNames{
    L"Lorencia", L"Noria", L"Elbeland", L"Dungeon", L"Devias", L"LostTower"};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t Size>
void CopyText(std::array<wchar_t, Size> &destination, const wchar_t *source) noexcept
{
    std::wcsncpy(destination.data(), source, Size - 1);
    destination[Size - 1] = L'\0';
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
template <std::size_t Size>
void CopyNumber(std::array<wchar_t, Size> &destination, int value) noexcept
{
    _itow_s(value, destination.data(), destination.size(), 10);
}
#pragma pack(pop)

} // namespace MoveCommandDetail

namespace MuHelperPanelDetail
{
using namespace MUHelper;
using namespace SEASON3B;
#pragma pack(push)
#pragma pack()
enum ESkillSlotImg : uint16_t
{
    SKILL_SLOT_SKILL1 = 0,
    SKILL_SLOT_SKILL2 = 1,
    SKILL_SLOT_SKILL3 = 2,
    SKILL_SLOT_BUFF1 = 3,
    SKILL_SLOT_BUFF2 = 4,
    SKILL_SLOT_BUFF3 = 5
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
inline constexpr int MAX_HUNTING_RANGE = 6;
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
enum ESkillSlot
{
    SUB_PAGE_SKILL2_CONFIG = 2,
    SUB_PAGE_SKILL3_CONFIG,
    SUB_PAGE_POTION_CONFIG_ELF,
    SUB_PAGE_POTION_CONFIG_SUMMY,
    SUB_PAGE_POTION_CONFIG,
    SUB_PAGE_PARTY_CONFIG,
    SUB_PAGE_PARTY_CONFIG_ELF
};
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
using UI::Modern::PC::MuHelper::RmlMuHelperFormValues;
#pragma pack(pop)

} // namespace MuHelperPanelDetail

namespace MainFrameDetail
{

const UI::Modern::RmlUiDesign &MainFrameDesign();
UI::Modern::RmlMainFrameRect MainFrameReferenceRect(int viewportWidth, int viewportHeight,
                                                    float maximumScale, float x, float y,
                                                    float width, float height) noexcept;
UI::Modern::RmlMainFrameRect RoundedMainFrameReferenceRect(int viewportWidth, int viewportHeight,
                                                           float maximumScale, float x, float y,
                                                           float width, float height) noexcept;
UI::Modern::RmlMainFrameRect CalculateSkillListSlot(
    const MainFrameDetail::SkillListGeometry &geometry, int index);
MainFrameDetail::SkillListGeometry CalculateSkillListGeometry(int viewportWidth, int viewportHeight,
                                                              float maximumScale) noexcept;
} // namespace MainFrameDetail

namespace MoveCommandDetail
{
using namespace SEASON3B;
UI::Modern::RmlMoveCommandRect MoveThumbRect(std::size_t position, std::size_t maximum) noexcept;
} // namespace MoveCommandDetail

namespace MuHelperPanelDetail
{
using namespace MUHelper;
using namespace SEASON3B;
void WriteCombatForm(const MuHelperPanelDetail::RmlMuHelperFormValues &form, ConfigData &config);
} // namespace MuHelperPanelDetail
