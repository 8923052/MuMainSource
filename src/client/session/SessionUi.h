#pragma once
#include "support/CoreMath.h"

#include "data/GameData.h"
#include "render/FrameTape.h"
#include "session/SessionRuntime.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/session/UiSessionLogic.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

class SessionUiView
{
  public:
    virtual ~SessionUiView() = default;

    virtual bool BeginPresentation() noexcept = 0;
    virtual bool FinishPresentation() noexcept = 0;
    virtual bool UsesSessionOwnedLegacyBehavior() const noexcept
    {
        return false;
    }
};

class LegacySessionUiView final : public SessionUiView
{
  public:
    bool BeginPresentation() noexcept override;
    bool FinishPresentation() noexcept override;
    bool UsesSessionOwnedLegacyBehavior() const noexcept override;
};

class SessionKeeper;
class SessionLifecycleObserver;
class GameSession;
class SessionRenderUnit;
struct SessionPersonalItemPriceStorage;
class CErrorReport;
class CmuConsoleDebug;
class CTimer;
class GameSessionTestPeer;
class CCharMakeWin;
class ApplicationAudio;
class ApplicationConfigUnit;
class AppWindow;
class CUIUnmixgemList;
struct SessionInputEvent;
namespace SEASON3B
{
class CNewUIObj;
class CNewUIInventoryCtrl;
class CNewUISystem;
} // namespace SEASON3B
typedef int POPUP_RESULT;

class SessionUiUnit final : protected SessionUiLegacyBindings
{
  public:
    struct ScreenshotRequest final
    {
        std::uint64_t requestId = 0;
        std::uint64_t sourceFrameSequence = 0;
        SessionId sessionId;
        SessionGeneration generation;
        std::uint64_t sourceSurfaceGeneration = 0;
        RenderTapeRect rect;
        std::wstring fileName;
    };

    using SessionUiLegacyBindings::PlayBuffer;

    SessionUiUnit(SessionKeeper &keeper, SessionLifecycleObserver *observer) noexcept;
    ~SessionUiUnit();

    bool AdvanceTitleSceneOnOwner();
    bool AdvanceLoadingSceneOnOwner(bool visible);
    void InitializeLoadingScene();
    void ClearInput(BOOL clearWhisperTarget = TRUE);
    bool BeginPresentation() noexcept;
    bool FinishPresentation() noexcept;
    CUIMng &LegacyUiManager() noexcept
    {
        return *legacyUiManager_;
    }
    bool InitializeLegacyUi();
    bool ProcessModernUiInput(const SessionInputEvent &event);
    std::optional<UI::Modern::RmlTextInputArea> ModernTextInputArea() const;
    SEASON3B::CNewUIObj *FocusedModernUiObject() const noexcept;
    void SaveMacro(const wchar_t *fileName);
    void OpenMacro(const wchar_t *fileName);
    void SaveOptions();
    bool CheckName();
    bool IsCanUseItem();                                                              // OMF-00501
    bool IsCanTrade();                                                                // OMF-00502
    int IsPurchaseShop();                                                             // OMF-01969
    bool CheckMouseIn(int x, int y, int width, int height) const;                     // OMF-01970
    BOOL CheckMouseIn(int x, int y, int width, int height, int coordinateType) const; // OMF-01867
    int GetScreenWidth();                                                             // OMF-00547
    void ConvertTaxGold(DWORD gold, wchar_t *text);                                   // OMF-00511
    int64_t ConvertRepairGold(int64_t gold, int durability, int maxDurability, short type,
                              wchar_t *text);                 // OMF-00515
    void SetItemColor(int index, ITEM *inventory, int color); // OMF-00549
    void InitPartyList();                                     // OMF-00576
    BOOL CheckOptionMouseClick(int optionPositionY, BOOL playClickSound = TRUE);
    bool IsCorrectShopTitle(const wchar_t *shopTitle);
    bool CreateOkMessageBox(const std::wstring &message, DWORD color = 0xffffffff,
                            float priority = 3.0f);
    void RequestCreateCharacter(CCharMakeWin &window);
    void SetPersonalShopTitleIfValid(const wchar_t *title);
    SessionKeeper &OriginatingSession() const noexcept
    {
        return SessionOrigin();
    }
    CUIMapName &MapName() noexcept
    {
        return mapName_;
    }
    UI::ReconnectDialog &ReconnectOverlay() noexcept
    {
        return reconnectDialog_;
    }
    UI::NoticeBoard &Notices() noexcept
    {
        return notices_;
    }
    CUIUnmixgemList &UnmixGemList() noexcept
    {
        return *unmixGemList_;
    }
    void ResetWantedList();
    bool FindWantedList();
    void MoveUnMixList();
    void RenderUnMixList();
    void UpdateResolutionDependentSystems();
    std::optional<ScreenshotRequest> TakeScreenshotRequest() noexcept;
    bool SubmitScreenshotRequest(ScreenshotRequest request, std::uint64_t frameSequence) noexcept;
    void RestoreScreenshotRequest(ScreenshotRequest request) noexcept;
    bool CompleteScreenshot(const RenderOwnerRequestCompletion &completion,
                            std::span<const std::byte> rgba8) noexcept;
    bool RegisterChatConnection(std::int32_t handle, DWORD windowId);
    void UnregisterChatConnection(std::int32_t handle) noexcept;
    void ProcessChatPacket(std::int32_t handle, const BYTE *receiveBuffer, std::int32_t size);
    BOOL ShowCheckBox(int num, int index, int message);
    void RepairAllGold();
    SEASON3B::CNewUIInventoryCtrl *GetInventoryCtrl() const;
    void SelectFromList(int index, int level);
    bool CheckInv();
    int CalcRecoveryZen(BYTE type, wchar_t *text);
    void ConvertChaosTaxGold(DWORD gold, wchar_t *text);
    bool IsExistUndecidedPrice();
    void RecoverPet(BYTE type);
    void MovePetCommand(CHARACTER *character);
    void FormatPetSpecialOptions(const ITEM &item, const PET_INFO &pet, int &textNum,
                                 int &skipNum);
    bool RequestPetInfo(int x, int y, ITEM *item, bool periodic = false);

  private:
    friend class GameSession;
    friend class SessionRenderUnit;
    friend class SessionLegacyCalls;
    friend class GameSessionTestPeer;

    bool InitializeConnectedChildren();

    bool HasAccountBlockedCharacter(); // OMF-00109
    bool HasEmptyCharacterSlot();      // OMF-00110
    bool HasLiveCharacter();           // OMF-00111
    CHARACTER *GetSelectedCharacter(); // OMF-00112
    void RenderAccountBlockMessage();  // OMF-00113

    bool BeginScenePresentationUpdate();
    bool BeginMainScenePresentationUpdate();
    void InitializeMainScene();
    void InitializeSceneFrame();
    bool FinishScenePresentationUpdate();
    void GenerateScreenshotFilename(wchar_t *outFileName, wchar_t *outMessage);
    void CaptureScreenshot();
    void HandleScreenshotCapture();
    bool UpdateLegacyUiState();
    void ShutdownLegacyUi() noexcept;
    void InitializeLegacyManager();
    void ShutdownLegacyManager() noexcept;
    void UpdateLoginAndCharacterScenes();
    bool CheckAbuseFilter(wchar_t *text, bool checkSlash = true);
    bool CheckAbuseNameFilter(wchar_t *text);
    void RequireClass(ITEM_ATTRIBUTE *item);
    bool CheckUseMasterSkill(CHARACTER *character, int index); // OMF-00863
    void UseBattleMasterSkill();
    void SetupItemUseStateMessage(int num, int index, int message);
    void SetupPersonalShopWarningMessage(int num, int index);
    void SetupChaosCastleCheckMessage(int num, int index);
    void SetupGemIntegrationMessage();
    void SetupCancelSkillMessage(int index);
    void SetupGenericMessage(int num, int index);
    void ConfigureMessageBoxButtons(int message);
    void MovePersonalShop();
    void MoveServerDivisionInventory();
    void ClearInventory();
    void OpenPersonalShopMsgWnd(int messageType);
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
    bool RenderPetCmdInfo(int sx, int sy, int type);
    bool RenderPetItemInfo(int sx, int sy, ITEM *item, int inventoryType);
    int CutStr(const wchar_t *source, wchar_t *output, const int targetPixelWidth,
               const int maxOutputLines, const int outputLength, const int firstLineTab = 0);
    int CutText3(const wchar_t *text, wchar_t *output, const int targetWidth,
                 const int maxOutputLines, const int outputLength, const int firstLineTab = 0,
                 const BOOL reverseWrite = FALSE);
    int DivideStringByPixel(wchar_t *output, int outputRows, int outputColumns,
                            const wchar_t *source, int pixelsPerLine, bool insertSpace = true,
                            const wchar_t newlineCharacter = L';');
    void RenderCheckBox(int x, int y, BOOL selected);
    DWORD CreateUIID();
    void RenderGoldRect(float x, float y, float width, float height, int fillType = 0);
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height);
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV);
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV, DWORD color);
    void RenderImage(std::uint32_t imageType, float x, float y, float width, float height,
                     float sourceU, float sourceV, float uWidth, float vHeight,
                     DWORD color = 0xffffffff);
    void RenderImageStretch(std::uint32_t imageType, float x, float y, float width, float height,
                            float sourceX, float sourceY, float sourceWidth, float sourceHeight,
                            DWORD color = 0xffffffff);
    void SetLineColor(int type, float alphaRate = 1.0f);
    void RenderWindowVLine(float x, float y, float height);
    void RenderWindowHLine(float x, float y, float width);
    void RenderTabLine(int x, int y, int tabWidth, int tabHeight, int tabCount, int selectedTab);
    void ReceiveChatRoomConnectResult(DWORD windowUiId, const BYTE *receiveBuffer);   // OMF-01886
    void ReceiveChatRoomUserStateChange(DWORD windowUiId, const BYTE *receiveBuffer); // OMF-01887
    void ReceiveChatRoomUserList(DWORD windowUiId, const BYTE *receiveBuffer);        // OMF-01888
    void ReceiveChatRoomChatText(DWORD windowUiId, const BYTE *receiveBuffer);        // OMF-01889
    void ReceiveChatRoomNoticeText(DWORD windowUiId, const BYTE *receiveBuffer);      // OMF-01890
    void TranslateChattingProtocol(DWORD windowUiId, const BYTE *receiveBuffer,
                                   int size); // OMF-01891
    void CloseNPCGMWindow();                  // OMF-00460

    wchar_t (&AbuseFilter)[MAX_FILTERS][20];
    wchar_t (&AbuseNameFilter)[MAX_NAMEFILTERS][20];
    int &AbuseFilterNumber;
    int &AbuseNameFilterNumber;
    CTimer *&g_pTimer;
    DWORD &g_dwLastUIID;
    int &g_nChaosTaxRate;
    std::unique_ptr<CUIMng> legacyUiManager_;
    CPersonalShopTitleImp personalShopTitle_;
    std::unique_ptr<SEASON3B::CNewUISystem> newUiSystem_;
    CUIMapName mapName_;
    CUIMapName &g_pUIMapName;
    std::unique_ptr<CUIUnmixgemList> unmixGemList_;
    UI::ReconnectDialog reconnectDialog_;
    UI::NoticeBoard notices_;
    std::chrono::steady_clock::time_point petInfoRequestTime_{};
    bool legacyUiInitialized_ = false;
    bool titleSceneStarted_ = false;
    unsigned int loadingSceneUpdates_ = 0;
    std::map<std::int32_t, DWORD> chatConnections_;
    SessionLifecycleObserver *observer_;
    std::optional<ScreenshotRequest> pendingScreenshot_;
    std::optional<ScreenshotRequest> submittedScreenshot_;
    std::uint64_t submittedScreenshotFrameSequence_ = 0;
    std::uint64_t nextScreenshotRequestId_ = 1;
    mutable std::atomic<SEASON3B::CNewUIObj *> focusedModernUiObject_ = nullptr;
};

struct GroundItemLabelCacheKey final
{
    int Type = -1;
    int Level = 0;
    BYTE ExcellentFlags = 0;
    BYTE AncientDiscriminator = 0;
    BYTE FeatureFlags = 0;

    bool operator==(const GroundItemLabelCacheKey &other) const noexcept
    {
        return Type == other.Type && Level == other.Level &&
               ExcellentFlags == other.ExcellentFlags &&
               AncientDiscriminator == other.AncientDiscriminator &&
               FeatureFlags == other.FeatureFlags;
    }
};

struct GroundItemLabelCacheKeyHasher final
{
    std::size_t operator()(const GroundItemLabelCacheKey &key) const noexcept
    {
        std::size_t seed = std::hash<int>{}(key.Type);
        seed ^= std::hash<int>{}(key.Level) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^=
            std::hash<unsigned int>{}(key.ExcellentFlags) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<unsigned int>{}(key.AncientDiscriminator) + 0x9e3779b9 + (seed << 6) +
                (seed >> 2);
        seed ^=
            std::hash<unsigned int>{}(key.FeatureFlags) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct GroundItemLabelCacheEntry final
{
    unsigned int TextureId = 0;
    int TextWidth = 0;
    int TextHeight = 0;
    int TextureWidth = 0;
    int TextureHeight = 0;
    DWORD BgColor = 0;
    DWORD LastUsedTick = 0;
};

struct SessionGroundItemLabelStorage final
{
    std::unordered_map<GroundItemLabelCacheKey, GroundItemLabelCacheEntry,
                       GroundItemLabelCacheKeyHasher>
        cache;
    int buildBudgetRemaining = 0;
    DWORD lastPruneTick = 0;
};
