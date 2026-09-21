#include "session/SessionKeeper.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationKeeper.h"
#include "domain/Events.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/Textures.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionRuntime.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Shell/ShellLogic.h"
#include "ui/features/Social/SocialLogic.h"

SessionRenderText::SessionRenderText(SessionKeeper &keeper) noexcept
    : sessionKeeper_(keeper), rasterizer_(keeper.ApplicationKeeperRef().GraphicsRenderText())
{
}

SessionLegacyCalls::SessionLegacyCalls(SessionKeeper &keeper) noexcept
    : ApplicationLegacyCalls(keeper.ApplicationKeeperRef()), m_Username(keeper.Username()),
      m_Password(keeper.Password()), m_RememberMe(keeper.RememberMe()),
      g_fScreenRate_x(keeper.PlatformScreenRateX()), g_fScreenRate_y(keeper.PlatformScreenRateY()),
      Bitmaps(keeper.BitmapRegistry(), keeper.TextureNamespace()), sessionKeeper_(keeper),
      GuildMark(keeper.GuildMarks()), SelectMarkColor(keeper.SelectedGuildMarkColor()),
      SelectFlag(keeper.TerrainSelectFlag()), g_iKeyPadEnable(keeper.KeyPadEnable()),
      g_GuildCache(keeper.GuildCacheObject()), g_GambleSystem(keeper.GambleSystemObject()),
      g_MoveCommandData(keeper.MoveCommandDataObject()), g_Time(keeper.TimeCheckObject()),
      g_SenatusInfo(keeper.SenatusInfoObject()), path(keeper.PathObject()),
      g_MovementSkill(keeper.InterfaceStorage().g_MovementSkill),
      g_dwLatestMagicTick(keeper.InterfaceStorage().g_dwLatestMagicTick),
      ItemHelp(keeper.InterfaceStorage().ItemHelp),
      MouseUpdateTime(keeper.InterfaceStorage().MouseUpdateTime),
      MouseUpdateTimeMax(keeper.InterfaceStorage().MouseUpdateTimeMax),
      s_bIgnoreHeldClickAfterNpcTalk(keeper.InterfaceStorage().s_bIgnoreHeldClickAfterNpcTalk),
      WhisperEnable(keeper.InterfaceStorage().WhisperEnable),
      ChatWindowEnable(keeper.InterfaceStorage().ChatWindowEnable),
      InputFrame(keeper.InterfaceStorage().InputFrame),
      EditFlag(keeper.InterfaceStorage().EditFlag),
      ColorTable(keeper.InterfaceStorage().ColorTable),
      SelectMonster(keeper.InterfaceStorage().SelectMonster),
      SelectModel(keeper.InterfaceStorage().SelectModel),
      SelectMapping(keeper.InterfaceStorage().SelectMapping),
      SelectColor(keeper.InterfaceStorage().SelectColor),
      SelectWall(keeper.InterfaceStorage().SelectWall),
      SelectMappingAngle(keeper.InterfaceStorage().SelectMappingAngle),
      DebugEnable(keeper.InterfaceStorage().DebugEnable),
      SelectedItem(keeper.InterfaceStorage().SelectedItem),
      SelectedNpc(keeper.InterfaceStorage().SelectedNpc),
      SelectedCharacter(keeper.InterfaceStorage().SelectedCharacter),
      SelectedOperate(keeper.InterfaceStorage().SelectedOperate),
      Attacking(keeper.InterfaceStorage().Attacking),
      g_iFollowCharacter(keeper.InterfaceStorage().g_iFollowCharacter),
      g_bAutoGetItem(keeper.InterfaceStorage().g_bAutoGetItem),
      g_bRenderGameCursor(keeper.InterfaceStorage().g_bRenderGameCursor),
      LButtonPopTime(keeper.InterfaceStorage().LButtonPopTime),
      LButtonPressTime(keeper.InterfaceStorage().LButtonPressTime),
      RButtonPopTime(keeper.InterfaceStorage().RButtonPopTime),
      RButtonPressTime(keeper.InterfaceStorage().RButtonPressTime),
      BrushSize(keeper.InterfaceStorage().BrushSize), HeroTile(keeper.InterfaceStorage().HeroTile),
      TargetNpc(keeper.InterfaceStorage().TargetNpc),
      TargetType(keeper.InterfaceStorage().TargetType), TargetX(keeper.InterfaceStorage().TargetX),
      TargetY(keeper.InterfaceStorage().TargetY),
      TargetAngle(keeper.InterfaceStorage().TargetAngle),
      TradeNpc(keeper.InterfaceStorage().TradeNpc), DontMove(keeper.InterfaceStorage().DontMove),
      ServerHide(keeper.InterfaceStorage().ServerHide),
      SkillEnable(keeper.InterfaceStorage().SkillEnable),
      MouseOnWindow(keeper.InterfaceStorage().MouseOnWindow),
      TerrainWallType(keeper.InterfaceStorage().TerrainWallType),
      TerrainWallAngle(keeper.InterfaceStorage().TerrainWallAngle),
      PickObject(keeper.InterfaceStorage().PickObject),
      PickObjectAngle(keeper.InterfaceStorage().PickObjectAngle),
      PickObjectHeight(keeper.InterfaceStorage().PickObjectHeight),
      PickObjectLockHeight(keeper.InterfaceStorage().PickObjectLockHeight),
      EnableRandomObject(keeper.InterfaceStorage().EnableRandomObject),
      WallAngle(keeper.InterfaceStorage().WallAngle),
      LockInputStatus(keeper.InterfaceStorage().LockInputStatus),
      GuildInputEnable(keeper.InterfaceStorage().GuildInputEnable),
      TabInputEnable(keeper.InterfaceStorage().TabInputEnable),
      GoldInputEnable(keeper.InterfaceStorage().GoldInputEnable),
      InputEnable(keeper.InterfaceStorage().InputEnable),
      g_bScratchTicket(keeper.InterfaceStorage().g_bScratchTicket),
      InputGold(keeper.InterfaceStorage().InputGold),
      InputNumber(keeper.InterfaceStorage().InputNumber),
      InputTextWidth(keeper.InterfaceStorage().InputTextWidth),
      InputIndex(keeper.InterfaceStorage().InputIndex),
      InputResidentNumber(keeper.InterfaceStorage().InputResidentNumber),
      InputTextMax(keeper.InterfaceStorage().InputTextMax),
      InputText(keeper.InterfaceStorage().InputText),
      InputTextIME(keeper.InterfaceStorage().InputTextIME),
      InputTextHide(keeper.InterfaceStorage().InputTextHide),
      InputLength(keeper.InterfaceStorage().InputLength),
      LastMacroTime(keeper.InterfaceStorage().LastMacroTime),
      WhisperIDCurrent(keeper.InterfaceStorage().WhisperIDCurrent),
      WhisperID(keeper.InterfaceStorage().WhisperID),
      g_dwOneToOneTick(keeper.InterfaceStorage().g_dwOneToOneTick),
      g_bGMObservation(keeper.InterfaceStorage().g_bGMObservation),
      DebugText(keeper.InterfaceStorage().DebugText),
      DebugTextLength(keeper.InterfaceStorage().DebugTextLength),
      DebugTextCount(keeper.InterfaceStorage().DebugTextCount),
      ItemKey(keeper.InterfaceStorage().ItemKey),
      ActionTarget(keeper.InterfaceStorage().ActionTarget),
      StandTime(keeper.InterfaceStorage().StandTime),
      HeroAngle(keeper.InterfaceStorage().HeroAngle),
      EnableFastInput(keeper.InterfaceStorage().EnableFastInput),
      g_bWhileMovingZone(keeper.InterfaceStorage().g_bWhileMovingZone),
      g_dwLatestZoneMoving(keeper.InterfaceStorage().g_dwLatestZoneMoving),
      TotalPacketSize(keeper.InterfaceStorage().TotalPacketSize),
      OldTime(keeper.InterfaceStorage().OldTime), g_iWidthEx(keeper.InterfaceStorage().g_iWidthEx),
      g_bUseChatListBox(keeper.InterfaceStorage().g_bUseChatListBox),
      g_dwActiveUIID(keeper.InterfaceStorage().g_dwActiveUIID),
      g_dwMouseUseUIID(keeper.InterfaceStorage().g_dwMouseUseUIID),
      g_dwTopWindow(keeper.InterfaceStorage().g_dwTopWindow),
      g_dwCurrentPressedButtonID(keeper.InterfaceStorage().g_dwCurrentPressedButtonID),
      g_dwLastLetterID(keeper.InterfaceStorage().g_dwLastLetterID),
      g_iNoticeInverse(keeper.InterfaceStorage().g_iNoticeInverse),
      g_pUIManager(keeper.InterfaceStorage().g_pUIManager),
      g_pNewUISystem(keeper.InterfaceStorage().g_pNewUISystem),
      Time_Effect(keeper.InterfaceStorage().Time_Effect), ashies(keeper.InterfaceStorage().ashies),
      weather(keeper.InterfaceStorage().weather),
      MousePosition(keeper.InterfaceStorage().MousePosition),
      MouseTarget(keeper.InterfaceStorage().MouseTarget), MouseX(keeper.InterfaceStorage().MouseX),
      MouseY(keeper.InterfaceStorage().MouseY),
      g_iMousePopPosition_x(keeper.InterfaceStorage().g_iMousePopPosition_x),
      g_iMousePopPosition_y(keeper.InterfaceStorage().g_iMousePopPosition_y),
      MouseLButton(keeper.InterfaceStorage().MouseLButton),
      MouseLButtonPop(keeper.InterfaceStorage().MouseLButtonPop),
      MouseLButtonPush(keeper.InterfaceStorage().MouseLButtonPush),
      MouseRButton(keeper.InterfaceStorage().MouseRButton),
      MouseRButtonPop(keeper.InterfaceStorage().MouseRButtonPop),
      MouseRButtonPush(keeper.InterfaceStorage().MouseRButtonPush),
      MouseLButtonDBClick(keeper.InterfaceStorage().MouseLButtonDBClick),
      MouseMButton(keeper.InterfaceStorage().MouseMButton),
      MouseMButtonPop(keeper.InterfaceStorage().MouseMButtonPop),
      MouseMButtonPush(keeper.InterfaceStorage().MouseMButtonPush),
      MouseRButtonPress(keeper.InterfaceStorage().MouseRButtonPress),
      GrabEnable(keeper.InterfaceStorage().GrabEnable),
      GrabFileName(keeper.InterfaceStorage().GrabFileName),
      GrabScreen(keeper.InterfaceStorage().GrabScreen),
      radioButtonIterIndex(keeper.InterfaceStorage().radioButtonIterIndex),
      g_pUIGateKeeper(keeper.InterfaceStorage().g_pUIGateKeeper),
      g_pUIJewelHarmonyinfo(keeper.InterfaceStorage().g_pUIJewelHarmonyinfo),
      g_pItemAddOptioninfo(keeper.InterfaceStorage().g_pItemAddOptioninfo),
      g_pUIPopup(keeper.InterfaceStorage().g_pUIPopup),
      HeroInventoryEnable(keeper.InterfaceStorage().HeroInventoryEnable),
      StorageInventoryEnable(keeper.InterfaceStorage().StorageInventoryEnable),
      g_bPersonalShopWnd(keeper.InterfaceStorage().g_bPersonalShopWnd),
      g_bServerDivisionEnable(keeper.InterfaceStorage().g_bServerDivisionEnable),
      optionFontLabels(keeper.InterfaceStorage().optionFontLabels),
      g_iLetterReadNextPos_x(keeper.InterfaceStorage().g_iLetterReadNextPos_x),
      g_iLetterReadNextPos_y(keeper.InterfaceStorage().g_iLetterReadNextPos_y),
      SelectedHero(keeper.InterfaceStorage().SelectedHero),
      InitLogIn(keeper.InterfaceStorage().InitLogIn),
      InitLoading(keeper.InterfaceStorage().InitLoading),
      InitCharacterScene(keeper.InterfaceStorage().InitCharacterScene),
      InitMainScene(keeper.InterfaceStorage().InitMainScene),
      MainSceneReady(keeper.NetworkStorage().MainSceneReady),
      g_fMULogoAlpha(keeper.InterfaceStorage().g_fMULogoAlpha),
      g_shCameraLevel(keeper.InterfaceStorage().g_shCameraLevel),
      szServerIpAddress(keeper.InterfaceStorage().szServerIpAddress),
      g_ServerPort(keeper.InterfaceStorage().g_ServerPort),
      SceneFlag(keeper.InterfaceStorage().SceneFlag),
      g_iCustomMessageBoxButton(keeper.InterfaceStorage().g_iCustomMessageBoxButton),
      g_iCustomMessageBoxButton_Cancel(keeper.InterfaceStorage().g_iCustomMessageBoxButton_Cancel),
      g_iCancelSkillTarget(keeper.InterfaceStorage().g_iCancelSkillTarget),
      DeleteGuildIndex(keeper.InterfaceStorage().DeleteGuildIndex),
      ErrorMessage(keeper.InterfaceStorage().ErrorMessage),
      g_Luminosity(keeper.InterfaceStorage().g_Luminosity),
      EnableEvent(keeper.InterfaceStorage().EnableEvent),
      g_currentCastleLevel(keeper.ChaosCastleStorage().currentLevel),
      g_actionMatch(keeper.ChaosCastleStorage().actionMatch),
      DeleteIndex(keeper.InterfaceStorage().DeleteIndex),
      AppointStatus(keeper.InterfaceStorage().AppointStatus),
      DeleteID(keeper.InterfaceStorage().DeleteID),
      s_szTargetID(keeper.InterfaceStorage().s_szTargetID),
      s_nTargetFireMemberIndex(keeper.InterfaceStorage().s_nTargetFireMemberIndex),
      AppointType(keeper.InterfaceStorage().AppointType),
      TerrainFlag(keeper.TerrainStorage().TerrainFlag),
      ActiveTerrain(keeper.TerrainStorage().ActiveTerrain),
      TerrainGrassEnable(keeper.TerrainStorage().TerrainGrassEnable),
      DetailLowEnable(keeper.TerrainStorage().DetailLowEnable),
      TerrainNormal(keeper.TerrainStorage().TerrainNormal),
      PrimaryTerrainLight(keeper.TerrainStorage().PrimaryTerrainLight),
      BackTerrainLight(keeper.TerrainStorage().BackTerrainLight),
      TerrainLight(keeper.TerrainStorage().TerrainLight),
      BackTerrainHeight(keeper.TerrainStorage().BackTerrainHeight),
      TerrainMappingLayer1(keeper.TerrainStorage().TerrainMappingLayer1),
      TerrainMappingLayer2(keeper.TerrainStorage().TerrainMappingLayer2),
      TerrainMappingAlpha(keeper.TerrainStorage().TerrainMappingAlpha),
      TerrainGrassTexture(keeper.TerrainStorage().TerrainGrassTexture),
      TerrainGrassWind(keeper.TerrainStorage().TerrainGrassWind),
      g_fTerrainGrassWind1(keeper.TerrainStorage().g_fTerrainGrassWind1),
      TerrainWall(keeper.TerrainStorage().TerrainWall),
      WaterMove(keeper.TerrainStorage().WaterMove),
      CurrentLayer(keeper.TerrainStorage().CurrentLayer),
      g_fSpecialHeight(keeper.TerrainStorage().g_fSpecialHeight),
      g_fFrustumRange(keeper.TerrainStorage().g_fFrustumRange),
      BMPHeader(keeper.TerrainStorage().BMPHeader),
      TerrainIndex1(keeper.TerrainStorage().TerrainIndex1),
      TerrainIndex2(keeper.TerrainStorage().TerrainIndex2),
      TerrainIndex3(keeper.TerrainStorage().TerrainIndex3),
      TerrainIndex4(keeper.TerrainStorage().TerrainIndex4), Index0(keeper.TerrainStorage().Index0),
      Index1(keeper.TerrainStorage().Index1), Index2(keeper.TerrainStorage().Index2),
      Index3(keeper.TerrainStorage().Index3), Index01(keeper.TerrainStorage().Index01),
      Index12(keeper.TerrainStorage().Index12), Index23(keeper.TerrainStorage().Index23),
      Index30(keeper.TerrainStorage().Index30), Index02(keeper.TerrainStorage().Index02),
      TerrainVertex(keeper.TerrainStorage().TerrainVertex),
      TerrainVertex01(keeper.TerrainStorage().TerrainVertex01),
      TerrainVertex12(keeper.TerrainStorage().TerrainVertex12),
      TerrainVertex23(keeper.TerrainStorage().TerrainVertex23),
      TerrainVertex30(keeper.TerrainStorage().TerrainVertex30),
      TerrainVertex02(keeper.TerrainStorage().TerrainVertex02),
      TerrainTextureCoord(keeper.TerrainStorage().TerrainTextureCoord),
      TerrainTextureCoord01(keeper.TerrainStorage().TerrainTextureCoord01),
      TerrainTextureCoord12(keeper.TerrainStorage().TerrainTextureCoord12),
      TerrainTextureCoord23(keeper.TerrainStorage().TerrainTextureCoord23),
      TerrainTextureCoord30(keeper.TerrainStorage().TerrainTextureCoord30),
      TerrainTextureCoord02(keeper.TerrainStorage().TerrainTextureCoord02),
      TerrainMappingAlpha01(keeper.TerrainStorage().TerrainMappingAlpha01),
      TerrainMappingAlpha12(keeper.TerrainStorage().TerrainMappingAlpha12),
      TerrainMappingAlpha23(keeper.TerrainStorage().TerrainMappingAlpha23),
      TerrainMappingAlpha30(keeper.TerrainStorage().TerrainMappingAlpha30),
      TerrainMappingAlpha02(keeper.TerrainStorage().TerrainMappingAlpha02),
      WaterTextureNumber(keeper.TerrainStorage().WaterTextureNumber),
      FrustrumBoundMinX(keeper.TerrainStorage().FrustrumBoundMinX),
      FrustrumBoundMinY(keeper.TerrainStorage().FrustrumBoundMinY),
      FrustrumBoundMaxX(keeper.TerrainStorage().FrustrumBoundMaxX),
      FrustrumBoundMaxY(keeper.TerrainStorage().FrustrumBoundMaxY),
      FrustrumX(keeper.TerrainStorage().FrustrumX), FrustrumY(keeper.TerrainStorage().FrustrumY),
      FrustrumCount(keeper.TerrainStorage().FrustrumCount),
      FrustrumVertex(keeper.TerrainStorage().FrustrumVertex),
      FrustrumFaceNormal(keeper.TerrainStorage().FrustrumFaceNormal),
      FrustrumFaceD(keeper.TerrainStorage().FrustrumFaceD), Sun(keeper.TerrainStorage().Sun),
      BoundingVertices(keeper.BmdStorage().BoundingVertices),
      BoundingMin(keeper.BmdStorage().BoundingMin), BoundingMax(keeper.BmdStorage().BoundingMax),
      BoneTransform(keeper.BmdStorage().BoneTransform),
      VertexTransform(keeper.BmdStorage().VertexTransform),
      NormalTransform(keeper.BmdStorage().NormalTransform),
      IntensityTransform(keeper.BmdStorage().IntensityTransform),
      LightTransform(keeper.BmdStorage().LightTransform),
      RenderArrayVertices(keeper.BmdStorage().RenderArrayVertices),
      RenderArrayColors(keeper.BmdStorage().RenderArrayColors),
      RenderArrayTexCoords(keeper.BmdStorage().RenderArrayTexCoords),
      ParentMatrix(keeper.BmdStorage().ParentMatrix), LightVector(keeper.BmdStorage().LightVector),
      LightVector2(keeper.BmdStorage().LightVector2), HighLight(keeper.BmdStorage().HighLight),
      BoneScale(keeper.BmdStorage().BoneScale), g_vright(keeper.BmdStorage().g_vright),
      g_smodels_total(keeper.BmdStorage().g_smodels_total), g_chrome(keeper.BmdStorage().g_chrome),
      g_chromeage(keeper.BmdStorage().g_chromeage), g_chromeup(keeper.BmdStorage().g_chromeup),
      g_chromeright(keeper.BmdStorage().g_chromeright), SubPixel(keeper.BmdStorage().SubPixel),
      Master_Level_Data(keeper.NetworkStorage().Master_Level_Data),
      Switch_Info(keeper.NetworkStorage().Switch_Info), HeroKey(keeper.NetworkStorage().HeroKey),
      CurrentProtocolState(keeper.NetworkStorage().CurrentProtocolState),
      Password(keeper.NetworkStorage().Password), HeroIndex(keeper.NetworkStorage().HeroIndex),
      Question(keeper.NetworkStorage().Question),
      AttackPlayer(keeper.NetworkStorage().AttackPlayer), LogIn(keeper.NetworkStorage().LogIn),
      LogInID(keeper.NetworkStorage().LogInID), LogOut(keeper.NetworkStorage().LogOut),
      ChatWhisperID(keeper.NetworkStorage().ChatWhisperID),
      CurrentSkill(keeper.NetworkStorage().CurrentSkill), BuyCost(keeper.NetworkStorage().BuyCost),
      EnableUse(keeper.NetworkStorage().EnableUse),
      SendGetItem(keeper.NetworkStorage().SendGetItem),
      SendDropItem(keeper.NetworkStorage().SendDropItem),
      g_bPacketAfter_EquipmentItem(keeper.NetworkStorage().g_bPacketAfter_EquipmentItem),
      g_byPacketAfter_EquipmentItem(keeper.NetworkStorage().g_byPacketAfter_EquipmentItem),
      EnableGuildWar(keeper.NetworkStorage().EnableGuildWar),
      GuildWarIndex(keeper.NetworkStorage().GuildWarIndex),
      GuildWarName(keeper.NetworkStorage().GuildWarName),
      GuildWarScore(keeper.NetworkStorage().GuildWarScore),
      EnableSoccer(keeper.NetworkStorage().EnableSoccer),
      HeroSoccerTeam(keeper.NetworkStorage().HeroSoccerTeam),
      SoccerTime(keeper.NetworkStorage().SoccerTime),
      SoccerTeamName(keeper.NetworkStorage().SoccerTeamName),
      SoccerObserver(keeper.NetworkStorage().SoccerObserver),
      g_CharCardEnable(keeper.NetworkStorage().g_CharCardEnable),
      g_iMaxLetterCount(keeper.NetworkStorage().g_iMaxLetterCount),
      SummonLife(keeper.NetworkStorage().SummonLife),
      g_GuildNotice(keeper.InventoryStorage().g_GuildNotice),
      GuildList(keeper.InventoryStorage().GuildList),
      g_nGuildMemberCount(keeper.InventoryStorage().g_nGuildMemberCount),
      GuildTotalScore(keeper.InventoryStorage().GuildTotalScore),
      GuildPlayerKey(keeper.InventoryStorage().GuildPlayerKey),
      Party(keeper.InventoryStorage().Party), PartyNumber(keeper.InventoryStorage().PartyNumber),
      PartyKey(keeper.InventoryStorage().PartyKey), PickItem(keeper.InventoryStorage().PickItem),
      TargetItem(keeper.InventoryStorage().TargetItem),
      Inventory(keeper.InventoryStorage().Inventory),
      InventoryExt(keeper.InventoryStorage().InventoryExt),
      ShopInventory(keeper.InventoryStorage().ShopInventory),
      g_PersonalShopInven(keeper.InventoryStorage().g_PersonalShopInven),
      g_PersonalShopBackup(keeper.InventoryStorage().g_PersonalShopBackup),
      g_bEnablePersonalShop(keeper.InventoryStorage().g_bEnablePersonalShop),
      g_iPShopWndType(keeper.InventoryStorage().g_iPShopWndType),
      g_ptPersonalShop(keeper.InventoryStorage().g_ptPersonalShop),
      g_iPersonalShopMsgType(keeper.InventoryStorage().g_iPersonalShopMsgType),
      g_szPersonalShopTitle(keeper.InventoryStorage().g_szPersonalShopTitle),
      g_PersonalShopSeller(keeper.InventoryStorage().g_PersonalShopSeller),
      g_bIsTooltipOn(keeper.InventoryStorage().g_bIsTooltipOn),
      CheckSkill(keeper.InventoryStorage().CheckSkill),
      CheckInventory(keeper.InventoryStorage().CheckInventory),
      EquipmentSuccess(keeper.InventoryStorage().EquipmentSuccess),
      CheckShop(keeper.InventoryStorage().CheckShop), CheckX(keeper.InventoryStorage().CheckX),
      CheckY(keeper.InventoryStorage().CheckY),
      SrcInventory(keeper.InventoryStorage().SrcInventory),
      SrcInventoryIndex(keeper.InventoryStorage().SrcInventoryIndex),
      DstInventoryIndex(keeper.InventoryStorage().DstInventoryIndex),
      AllRepairGold(keeper.InventoryStorage().AllRepairGold),
      StorageGoldFlag(keeper.InventoryStorage().StorageGoldFlag),
      ListCount(keeper.InventoryStorage().ListCount),
      GuildListPage(keeper.InventoryStorage().GuildListPage),
      g_bEventChipDialogEnable(keeper.InventoryStorage().g_bEventChipDialogEnable),
      g_shEventChipCount(keeper.InventoryStorage().g_shEventChipCount),
      g_shMutoNumber(keeper.InventoryStorage().g_shMutoNumber),
      g_bServerDivisionAccept(keeper.InventoryStorage().g_bServerDivisionAccept),
      g_strGiftName(keeper.InventoryStorage().g_strGiftName),
      RepairShop(keeper.InventoryStorage().RepairShop),
      RepairEnable(keeper.InventoryStorage().RepairEnable),
      AskYesOrNo(keeper.InventoryStorage().AskYesOrNo),
      OkYesOrNo(keeper.InventoryStorage().OkYesOrNo),
      g_wStoragePassword(keeper.InventoryStorage().g_wStoragePassword),
      g_nKeyPadMapping(keeper.InventoryStorage().g_nKeyPadMapping),
      g_lpszKeyPadInput(keeper.InventoryStorage().g_lpszKeyPadInput),
      BuyItem(keeper.InventoryStorage().BuyItem), TextList(keeper.InventoryStorage().TextList),
      TextListColor(keeper.InventoryStorage().TextListColor),
      TextBold(keeper.InventoryStorage().TextBold), Size(keeper.InventoryStorage().Size),
      TextNum(keeper.InventoryStorage().TextNum), SkipNum(keeper.InventoryStorage().SkipNum),
      InventoryStartX(keeper.InventoryStorage().InventoryStartX),
      InventoryStartY(keeper.InventoryStorage().InventoryStartY),
      ShopInventoryStartX(keeper.InventoryStorage().ShopInventoryStartX),
      ShopInventoryStartY(keeper.InventoryStorage().ShopInventoryStartY),
      TradeInventoryStartX(keeper.InventoryStorage().TradeInventoryStartX),
      TradeInventoryStartY(keeper.InventoryStorage().TradeInventoryStartY),
      CharacterInfoStartX(keeper.InventoryStorage().CharacterInfoStartX),
      CharacterInfoStartY(keeper.InventoryStorage().CharacterInfoStartY),
      GuildStartX(keeper.InventoryStorage().GuildStartX),
      GuildStartY(keeper.InventoryStorage().GuildStartY),
      GuildListStartX(keeper.InventoryStorage().GuildListStartX),
      GuildListStartY(keeper.InventoryStorage().GuildListStartY),
      EquipmentItem(keeper.InventoryStorage().EquipmentItem),
      ObjectSelect(keeper.InventoryStorage().ObjectSelect),
      MarkColor(keeper.InventoryStorage().MarkColor), Models(keeper.ModelPoolObject()),
      gLoadData(keeper.ModelLoaderObject()), SocketClient(keeper.NetworkConnection()),
      g_bGameServerConnected(keeper.GameServerConnected()),
      g_ServerListManager(keeper.ServerListManagerObject()),
      bCheckNPC(keeper.QuestDialogStorage().bCheckNPC),
      g_iNumLineMessageBoxCustom(keeper.QuestDialogStorage().g_iNumLineMessageBoxCustom),
      g_lpszMessageBoxCustom(keeper.QuestDialogStorage().g_lpszMessageBoxCustom),
      g_iCurrentDialogScript(keeper.QuestDialogStorage().g_iCurrentDialogScript),
      g_iNumAnswer(keeper.QuestDialogStorage().g_iNumAnswer),
      g_lpszDialogAnswer(keeper.QuestDialogStorage().g_lpszDialogAnswer),
      g_pPartyManager(&keeper.PartyManagerObject()), g_tabBar(keeper.PetManagerStorage().g_tabBar),
      g_renderItemIndexBackup(keeper.PetManagerStorage().g_renderItemIndexBackup),
      g_renderItemInfoBackup(keeper.PetManagerStorage().g_renderItemInfoBackup),
      gs_PetInfo(keeper.PetManagerStorage().gs_PetInfo), g_DuelMgr(keeper.DuelManagerObject()),
      g_GuardsMan(keeper.GuardsManObject()), CharactersClient(keeper.CharactersClientStorage()),
      CharacterView(keeper.CharacterViewStorage()), Hero(keeper.HeroStorage()),
      CharacterMachine(keeper.CharacterMachineStorage()),
      CharacterAttribute(keeper.CharacterAttributeStorage()),
      g_fBoneSave(keeper.CharacterPopulationStorage().g_fBoneSave),
      EquipmentLevelSet(keeper.CharacterPopulationStorage().EquipmentLevelSet),
      g_bAddDefense(keeper.CharacterPopulationStorage().g_bAddDefense),
      g_iLimitAttackTime(keeper.CharacterPopulationStorage().g_iLimitAttackTime),
      g_iOldPositionX(keeper.CharacterPopulationStorage().g_iOldPositionX),
      g_iOldPositionY(keeper.CharacterPopulationStorage().g_iOldPositionY),
      g_fStopTime(keeper.CharacterPopulationStorage().g_fStopTime),
      playerNpcDeviasTextIndex(keeper.CharacterPopulationStorage().playerNpcDeviasTextIndex),
      playerNpcLorenciaTextIndex(keeper.CharacterPopulationStorage().playerNpcLorenciaTextIndex),
      activeChars(keeper.CharacterPopulationStorage().activeChars),
      g_ItemObject(keeper.CharacterPopulationStorage().g_ItemObject),
      swordDancerPosition(keeper.CharacterPopulationStorage().swordDancerPosition),
      swordDancerRandom(keeper.CharacterPopulationStorage().swordDancerRandom),
      EarthQuake(keeper.CharacterPopulationStorage().EarthQuake),
      visibleObject(keeper.CharacterPopulationStorage().visibleObject),
      g_CloudsLow(keeper.CharacterPopulationStorage().g_CloudsLow),
      objectTextureAnimation(keeper.CharacterPopulationStorage().objectTextureAnimation),
      objectMeshLight(keeper.CharacterPopulationStorage().objectMeshLight),
      objectMeshLightDelta(keeper.CharacterPopulationStorage().objectMeshLightDelta),
      objectPriorAnimationFrame(keeper.CharacterPopulationStorage().objectPriorAnimationFrame),
      RainTarget(keeper.CharacterPopulationStorage().RainTarget),
      RainCurrent(keeper.CharacterPopulationStorage().RainCurrent),
      RainSpeed(keeper.CharacterPopulationStorage().RainSpeed),
      RainAngle(keeper.CharacterPopulationStorage().RainAngle),
      RainPosition(keeper.CharacterPopulationStorage().RainPosition),
      MonsterKey(keeper.CharacterPopulationStorage().MonsterKey),
      m_nCurrMode(keeper.GuildMasterStorage().m_nCurrMode),
      m_eCurrStep(keeper.GuildMasterStorage().m_eCurrStep),
      m_byRelationShipType(keeper.GuildMasterStorage().m_byRelationShipType),
      m_byRelationShipRequestType(keeper.GuildMasterStorage().m_byRelationShipRequestType),
      m_byTargetUserIndexH(keeper.GuildMasterStorage().m_byTargetUserIndexH),
      m_byTargetUserIndexL(keeper.GuildMasterStorage().m_byTargetUserIndexL),
      g_iCurrentItem(keeper.ItemHelpStorage().g_iCurrentItem),
      g_iItemInfo(keeper.ItemHelpStorage().g_iItemInfo), s_fWind(keeper.PhysicsStorage().clothWind),
      s_vWind(keeper.PhysicsStorage().clothWindVector),
      g_vParticleWind(keeper.PhysicsStorage().particleWind),
      g_vParticleWindVelo(keeper.PhysicsStorage().particleWindVelocity)
{
}

SessionKeeper::SessionKeeper(ApplicationKeeper &applicationKeeper, SessionId id,
                             SessionSlotId slotId, const SessionConfigValues &initialConfig,
                             SessionLifecycleObserver *observer)
    : applicationKeeper_(applicationKeeper),
      g_bMinimizedEnabled(applicationKeeper.MinimizedEnabled()), id_(id), slotId_(slotId),
      config_(initialConfig), cameraZoom_(applicationKeeper.ApplicationConfig().initialCameraZoom),
      observer_(observer), interfaceStorage_(applicationKeeper.BootstrapServerIp(),
                                             applicationKeeper.BootstrapServerPort()),
      textureNamespace_(applicationKeeper.BitmapRegistry()), sessionText_(*this),
      mixRecipeManager_(*this), socketItemManager_(*this), itemOptionManager_(*this),
      senatusInfoObject_(CreateSessionSenatusInfo()), partyManager_(*this), duelManager_(*this),
      guardsManObject_(*this), guildCache_(*this), modelPool_(*this), modelLoader_(*this),
      buffStateSystemObject_(*this), directionObject_(*this), serverListManagerObject_(*this),
      blurStorage_(std::make_unique<SessionBlurStorage>()),
      spriteStorage_(std::make_unique<SessionSpriteStorage>()),
      shadowVolumeStorage_(std::make_unique<SessionShadowVolumeStorage>()),
      leafStorage_(std::make_unique<SessionLeafStorage>()),
      welfareTempleStorage_(std::make_unique<SessionWelfareTempleStorage>()),
      cameraMoveObject_(*this), mapManagerObject_(*this), skillManagerObject_(*this),
      skillEffectManagerObject_(*this), summonSystemObject_(*this), monkSystemObject_(*this),
      portalManagerObject_(*this), cursedTempleObject_(new SEASON3A::CursedTemple(*this)),
      thirdChangeObject_(SEASON3A::CGM3rdChangeUp::Make(*this))
{
    characterMachine_ = std::make_unique<CHARACTER_MACHINE>(*this);
    itemOptionManager_.BindCharacterState(*characterMachine_);
    questObject_ = std::make_unique<CSQuest>(*this);
    questManagerObject_ = std::make_unique<CQuestMng>(*this);
    audioOutput_ = applicationKeeper_.ApplicationAudioObject().WorkspaceView().Audio(id_);
    messageBoxManagerObject_ = CreateSessionMessageBoxManager(*this);
    inGameShopSystemObject_ = CreateSessionInGameShopSystem(*this);
    g_petProcess = PetProcess::Make(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::KeeperConstructed);
    }
}

SessionKeeper::~SessionKeeper()
{
    if (attachedToApplication_)
    {
        applicationKeeper_.DetachSession();
    }
    DestroySessionInGameShopSystem(inGameShopSystemObject_);
    inGameShopSystemObject_ = nullptr;
    DestroySessionFriendMenu(friendMenuObject_);
    friendMenuObject_ = nullptr;
    DestroySessionMessageBoxManager(messageBoxManagerObject_);
    messageBoxManagerObject_ = nullptr;
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::KeeperDestroyed);
    }
}

SessionId SessionKeeper::Id() const noexcept
{
    return id_;
}

SessionSlotId SessionKeeper::SlotId() const noexcept
{
    return slotId_;
}

int &SessionKeeper::CameraZoom() noexcept
{
    return cameraZoom_;
}

const int &SessionKeeper::CameraZoom() const noexcept
{
    return cameraZoom_;
}

wchar_t (&SessionKeeper::Username() noexcept)[MAX_USERNAME_SIZE + 1]
{
    return username_;
}

wchar_t (&SessionKeeper::Password() noexcept)[MAX_PASSWORD_SIZE + 1]
{
    return password_;
}

int &SessionKeeper::RememberMe() noexcept
{
    return rememberMe_;
}

void SessionKeeper::RequestDiscardSession() noexcept
{
    if (ApplicationSessionRuntime *runtime = applicationKeeper_.ApplicationSessionRuntimeUnit())
    {
        runtime->QueueDiscard(id_);
    }
}

void SessionKeeper::RekeySlot(SessionSlotId slotId) noexcept
{
    slotId_ = slotId;
}

SessionConfigValues &SessionKeeper::Config() noexcept
{
    return config_;
}

const SessionConfigValues &SessionKeeper::Config() const noexcept
{
    return config_;
}

ApplicationKeeper &SessionKeeper::ApplicationKeeperRef() noexcept
{
    return applicationKeeper_;
}

const ApplicationKeeper &SessionKeeper::ApplicationKeeperRef() const noexcept
{
    return applicationKeeper_;
}

BOOL &SessionKeeper::MinimizedEnabled() noexcept
{
    return g_bMinimizedEnabled;
}

const BOOL &SessionKeeper::MinimizedEnabled() const noexcept
{
    return g_bMinimizedEnabled;
}

ApplicationConfigValues &SessionKeeper::ApplicationConfig() noexcept
{
    return applicationKeeper_.ApplicationConfig();
}

const ApplicationConfigValues &SessionKeeper::ApplicationConfig() const noexcept
{
    return static_cast<const ApplicationKeeper &>(applicationKeeper_).ApplicationConfig();
}

wchar_t (&SessionKeeper::AbuseFilterStorage() noexcept)[MAX_FILTERS][20]
{
    return applicationKeeper_.AbuseFilterStorage();
}

wchar_t (&SessionKeeper::AbuseNameFilterStorage() noexcept)[MAX_NAMEFILTERS][20]
{
    return applicationKeeper_.AbuseNameFilterStorage();
}

int &SessionKeeper::AbuseFilterCount() noexcept
{
    return applicationKeeper_.AbuseFilterCount();
}

int &SessionKeeper::AbuseNameFilterCount() noexcept
{
    return applicationKeeper_.AbuseNameFilterCount();
}

GATE_ATTRIBUTE *&SessionKeeper::GateAttributes() noexcept
{
    return applicationKeeper_.GateAttributes();
}

MONSTER_SCRIPT (&SessionKeeper::MonsterScripts() noexcept)[MAX_MONSTER]
{
    return applicationKeeper_.MonsterScripts();
}

Script_Skill (&SessionKeeper::MonsterSkills() noexcept)[MODEL_MONSTER_END]
{
    return applicationKeeper_.MonsterSkills();
}

CLASS_ATTRIBUTE (&SessionKeeper::ClassAttributes() noexcept)[MAX_CLASS]
{
    return applicationKeeper_.ClassAttributes();
}

int &SessionKeeper::EditMonsterCount() noexcept
{
    return applicationKeeper_.EditMonsterCount();
}

std::wstring &SessionKeeper::AssetLanguage() noexcept
{
    return applicationKeeper_.AssetLanguage();
}

float &SessionKeeper::LoginSceneOffsetX() noexcept
{
    return applicationKeeper_.LoginSceneOffsetX();
}

float &SessionKeeper::LoginSceneOffsetY() noexcept
{
    return applicationKeeper_.LoginSceneOffsetY();
}

float &SessionKeeper::LoginSceneOffsetZ() noexcept
{
    return applicationKeeper_.LoginSceneOffsetZ();
}

float &SessionKeeper::LoginSceneAnglePitch() noexcept
{
    return applicationKeeper_.LoginSceneAnglePitch();
}

float &SessionKeeper::LoginSceneAngleYaw() noexcept
{
    return applicationKeeper_.LoginSceneAngleYaw();
}

HWND &SessionKeeper::PlatformWindowHandle() noexcept
{
    return applicationKeeper_.PlatformStorageRef().g_hWnd;
}

bool &SessionKeeper::PlatformWindowActive() noexcept
{
    return applicationKeeper_.PlatformStorageRef().g_bWndActive;
}

int &SessionKeeper::PlatformNoMouseTime() noexcept
{
    return applicationKeeper_.PlatformStorageRef().g_iNoMouseTime;
}

bool &SessionKeeper::PlatformDestroyRequested() noexcept
{
    return applicationKeeper_.PlatformStorageRef().Destroy;
}

bool &SessionKeeper::GraphicsFogEnabled() noexcept
{
    return Renderer()->FogEnable;
}

float (&SessionKeeper::GraphicsFogColor() noexcept)[4]
{
    return Renderer()->FogColor;
}

CErrorReport &SessionKeeper::ErrorReport() noexcept
{
    return applicationKeeper_.ErrorReport();
}

CmuConsoleDebug &SessionKeeper::ConsoleDebug() noexcept
{
    return applicationKeeper_.ConsoleDebug();
}

CGlobalBitmap &SessionKeeper::BitmapRegistry() noexcept
{
    return applicationKeeper_.BitmapRegistry();
}

double SessionKeeper::DiagnosticsCpuAverage() const noexcept
{
    const ApplicationDiagnostics *const diagnostics =
        applicationKeeper_.ApplicationDiagnosticsUnit();
    return diagnostics == nullptr ? 0.0 : diagnostics->CpuAverage();
}

double SessionKeeper::DiagnosticsSessionCpuPercent() const noexcept
{
    const SessionManager *const sessions = applicationKeeper_.SessionManagerUnit();
    return sessions == nullptr ? 0.0 : sessions->SessionCpuPercent(id_);
}

std::size_t SessionKeeper::DiagnosticsSessionOwnedBytes() const noexcept
{
    const SessionManager *const sessions = applicationKeeper_.SessionManagerUnit();
    return sessions == nullptr ? 0 : sessions->SessionOwnedStorageBytes(id_);
}

std::size_t SessionKeeper::DiagnosticsAppRingOwnedBytes() const noexcept
{
    return applicationKeeper_.DiagnosticsStorageRef().appRingOwnedBytes;
}

std::size_t SessionKeeper::DiagnosticsProcessMemoryBytes() const noexcept
{
    const ApplicationDiagnostics *const diagnostics =
        applicationKeeper_.ApplicationDiagnosticsUnit();
    return diagnostics == nullptr ? 0 : diagnostics->ProcessMemoryBytes();
}

double SessionKeeper::DiagnosticsGpuUsagePercent() const noexcept
{
    const ApplicationDiagnostics *const diagnostics =
        applicationKeeper_.ApplicationDiagnosticsUnit();
    return diagnostics == nullptr ? 0.0 : diagnostics->GpuUsagePercent();
}

HotspotProfilerState SessionKeeper::DiagnosticsHotspotProfileState() const noexcept
{
    const ApplicationDiagnostics *const diagnostics =
        applicationKeeper_.ApplicationDiagnosticsUnit();
    return diagnostics != nullptr ? diagnostics->HotspotProfileState() : HotspotProfilerState::Idle;
}

wchar_t (&SessionKeeper::ExecutableVersion() noexcept)[11]
{
    return applicationKeeper_.ExecutableVersion();
}

SessionModelPool &SessionKeeper::ModelPoolObject() noexcept
{
    return modelPool_;
}

SessionModelLoader &SessionKeeper::ModelLoaderObject() noexcept
{
    return modelLoader_;
}

CBoneManager &SessionKeeper::BoneManagerObject() noexcept
{
    return boneManager_;
}

BuffStateSystem &SessionKeeper::BuffStateSystemObject() noexcept
{
    return buffStateSystemObject_;
}

bool &SessionKeeper::DirectionTimeCheckFlag() noexcept
{
    return directionTimeCheckFlag_;
}

int &SessionKeeper::DirectionBackupTime() noexcept
{
    return directionBackupTime_;
}

CDirection &SessionKeeper::DirectionObject() noexcept
{
    return directionObject_;
}

CServerListManager &SessionKeeper::ServerListManagerObject() noexcept
{
    return serverListManagerObject_;
}

LoginCameraState &SessionKeeper::LoginCameraStateObject() noexcept
{
    return loginCameraState_;
}

SessionEffectPool<OBJECT> &SessionKeeper::EffectsStorage() noexcept
{
    return effects_;
}

SessionEffectPool<PARTICLE> &SessionKeeper::ParticlesStorage() noexcept
{
    return particles_;
}

OBJECT (&SessionKeeper::MountsStorage() noexcept)[MAX_MOUNTS]
{
    return mounts_;
}

OBJECT (&SessionKeeper::BoidsStorage() noexcept)[MAX_BOIDS]
{
    return boids_;
}

OBJECT (&SessionKeeper::FishsStorage() noexcept)[MAX_FISHS]
{
    return fishs_;
}

OPERATE (&SessionKeeper::OperatesStorage() noexcept)[MAX_OPERATES]
{
    return operates_;
}

ITEM_t (&SessionKeeper::ItemsStorage() noexcept)[MAX_ITEMS]
{
    return items_;
}

WORD &SessionKeeper::LastSkillSerialNumber() noexcept
{
    return lastSkillSerialNumber_;
}

OBJECT_BLOCK (&SessionKeeper::ObjectBlocks() noexcept)[256]
{
    return objectBlocks_;
}

int &SessionKeeper::ActionObjectType() noexcept
{
    return actionObjectType_;
}
int &SessionKeeper::ActionWorld() noexcept
{
    return actionWorld_;
}
float &SessionKeeper::ActionTime() noexcept
{
    return actionTime_;
}
float &SessionKeeper::ActionObjectVelocity() noexcept
{
    return actionObjectVelocity_;
}

wchar_t (&SessionKeeper::MacroTexts() noexcept)[10][256]
{
    return macroTexts_;
}

SessionBlurStorage &SessionKeeper::BlurStorage() noexcept
{
    return *blurStorage_;
}

SessionSpriteStorage &SessionKeeper::SpritesStorage() noexcept
{
    return *spriteStorage_;
}

SessionShadowVolumeStorage &SessionKeeper::ShadowVolumeStorage() noexcept
{
    return *shadowVolumeStorage_;
}

float &SessionKeeper::CollisionDistance() noexcept
{
    return collisionDistance_;
}

float (&SessionKeeper::CollisionPosition() noexcept)[3]
{
    return collisionPosition_;
}

bool &SessionKeeper::TerrainSelectFlag() noexcept
{
    return terrainSelectFlag_;
}

float &SessionKeeper::TerrainSelectX() noexcept
{
    return terrainSelectX_;
}

float &SessionKeeper::TerrainSelectY() noexcept
{
    return terrainSelectY_;
}

UI::Login::RememberPasswordChoice &SessionKeeper::RememberPasswordChoiceStorage() noexcept
{
    return rememberPasswordChoice_;
}

DWORD &SessionKeeper::LastUiId() noexcept
{
    return lastUiId_;
}

SEASON3B::CNewUIPickedItem *&SessionKeeper::PickedItem() noexcept
{
    return interfaceStorage_.ms_pPickedItem;
}

SessionLeafStorage &SessionKeeper::LeafStorage() noexcept
{
    return *leafStorage_;
}

SessionWelfareTempleStorage &SessionKeeper::WelfareTempleStorage() noexcept
{
    return *welfareTempleStorage_;
}

int &SessionKeeper::TaxRate() noexcept
{
    return taxRate_;
}

int &SessionKeeper::ChaosTaxRate() noexcept
{
    return chaosTaxRate_;
}

int &SessionKeeper::GoalEffect() noexcept
{
    return goalEffect_;
}

SessionGroundItemLabelStorage &SessionKeeper::GroundItemLabelStorage() noexcept
{
    return groundItemLabelStorage_;
}

SessionItemHelpStorage &SessionKeeper::ItemHelpStorage() noexcept
{
    return itemHelpStorage_;
}

SessionPersonalItemPriceStorage &SessionKeeper::PersonalItemPriceStorage() noexcept
{
    return personalItemPriceStorage_;
}

SessionComGemStorage &SessionKeeper::ComGemStorage() noexcept
{
    return comGemStorage_;
}

SEASON3A::CMixRecipeMgr &SessionKeeper::MixRecipeManager() noexcept
{
    return mixRecipeManager_;
}

SEASON4A::CSocketItemMgr &SessionKeeper::SocketItemManager() noexcept
{
    return socketItemManager_;
}

CSItemOption &SessionKeeper::ItemOptionManager() noexcept
{
    return itemOptionManager_;
}

PATH &SessionKeeper::PathObject() noexcept
{
    return pathObject_;
}

SessionChaosCastleStorage &SessionKeeper::ChaosCastleStorage() noexcept
{
    return chaosCastleStorage_;
}

CTimeCheck &SessionKeeper::TimeCheckObject() noexcept
{
    return timeCheckObject_;
}

CSenatusInfo &SessionKeeper::SenatusInfoObject() noexcept
{
    return *senatusInfoObject_;
}

GambleSystem &SessionKeeper::GambleSystemObject() noexcept
{
    return gambleSystem_;
}

SEASON3B::CMoveCommandData &SessionKeeper::MoveCommandDataObject() noexcept
{
    return moveCommandData_;
}

SessionQuestDialogStorage &SessionKeeper::QuestDialogStorage() noexcept
{
    return questDialogStorage_;
}

SessionPetManagerStorage &SessionKeeper::PetManagerStorage() noexcept
{
    return petManagerStorage_;
}

SessionInterfaceStorage &SessionKeeper::InterfaceStorage() noexcept
{
    return interfaceStorage_;
}

SessionInventoryStorage &SessionKeeper::InventoryStorage() noexcept
{
    return inventoryStorage_;
}

SessionTerrainStorage &SessionKeeper::TerrainStorage() noexcept
{
    return terrainStorage_;
}

bool SessionKeeper::LoadSessionBitmap(const wchar_t *fileName, std::uint32_t logicalIndex,
                                      LegacyTextureFilter filter, LegacyTextureWrap wrapMode,
                                      bool check, bool fullPath)
{
    if (SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        return applicationKeeper_.LoadApplicationBitmap(fileName, logicalIndex, filter, wrapMode,
                                                        fullPath);
    }
    if (fileName == nullptr || !IsValid(filter) || !IsValid(wrapMode))
    {
        return false;
    }
    wchar_t resolvedPath[256] = {};
    if (fullPath)
    {
        wcscpy_s(resolvedPath, fileName);
    }
    else
    {
        wcscpy_s(resolvedPath, L"Data\\");
        wcscat_s(resolvedPath, fileName);
    }
    (void)check;
    return textureNamespace_.Load(logicalIndex, resolvedPath, applicationKeeper_.ErrorReport(),
                                  filter, wrapMode);
}

void SessionKeeper::DeleteSessionBitmap(std::uint32_t logicalIndex) noexcept
{
    if (SessionTextureNamespace::IsApplicationSharedLogicalIndex(logicalIndex))
    {
        applicationKeeper_.DeleteApplicationBitmap(logicalIndex);
    }
    else
    {
        textureNamespace_.Unload(logicalIndex);
    }
}

SessionTextureNamespace &SessionKeeper::TextureNamespace() noexcept
{
    return textureNamespace_;
}

SessionRenderText &SessionKeeper::SessionText() noexcept
{
    return sessionText_;
}

SessionNetworkStorage &SessionKeeper::NetworkStorage() noexcept
{
    return networkStorage_;
}

SessionBmdStorage &SessionKeeper::BmdStorage() noexcept
{
    return bmdStorage_;
}

SessionPhysicsStorage &SessionKeeper::PhysicsStorage() noexcept
{
    return physicsStorage_;
}

SessionCharacterPopulationStorage &SessionKeeper::CharacterPopulationStorage() noexcept
{
    return characterPopulationStorage_;
}

SessionGuildMasterStorage &SessionKeeper::GuildMasterStorage() noexcept
{
    return guildMasterStorage_;
}

bool SessionKeeper::InitializeCharacterPopulation() noexcept
{
    return characterPopulationStorage_.Initialize(applicationKeeper_.SharedCharacters(), id_,
                                                  *characterMachine_, characterMachine_->Character);
}

SessionCharacterPopulationStorage &SessionKeeper::CharactersClientStorage() noexcept
{
    return characterPopulationStorage_;
}

CHARACTER &SessionKeeper::CharacterViewStorage() noexcept
{
    return characterPopulationStorage_.CharacterView;
}

CHARACTER *&SessionKeeper::HeroStorage() noexcept
{
    return characterPopulationStorage_.Hero;
}

CHARACTER_MACHINE *&SessionKeeper::CharacterMachineStorage() noexcept
{
    return characterPopulationStorage_.CharacterMachine;
}

CHARACTER_ATTRIBUTE *&SessionKeeper::CharacterAttributeStorage() noexcept
{
    return characterPopulationStorage_.CharacterAttribute;
}

CSQuest &SessionKeeper::QuestObject() noexcept
{
    return *questObject_;
}

CQuestMng &SessionKeeper::QuestManagerObject() noexcept
{
    return *questManagerObject_;
}

SEASON3B::CPartyManager &SessionKeeper::PartyManagerObject() noexcept
{
    return partyManager_;
}

CDuelMgr &SessionKeeper::DuelManagerObject() noexcept
{
    return duelManager_;
}

CUIGuardsMan &SessionKeeper::GuardsManObject() noexcept
{
    return guardsManObject_;
}

CHARACTER_MACHINE &SessionKeeper::CharacterMachineObject() noexcept
{
    return *characterMachine_;
}

CSBaseMatch *&SessionKeeper::EventMatch() noexcept
{
    return eventMatch_;
}

int &SessionKeeper::KeyPadEnable() noexcept
{
    return keyPadEnable_;
}

std::span<MARK_t> SessionKeeper::GuildMarks() noexcept
{
    return guildMarks_;
}

int &SessionKeeper::SelectedGuildMarkColor() noexcept
{
    return selectedGuildMarkColor_;
}

CGuildCache &SessionKeeper::GuildCacheObject() noexcept
{
    return guildCache_;
}

SessionEffectPool<JOINT> &SessionKeeper::JointsStorage() noexcept
{
    return joints_;
}

SessionEffectPool<PARTICLE> &SessionKeeper::PointsStorage() noexcept
{
    return points_;
}

SessionEffectPool<PARTICLE> &SessionKeeper::PointersStorage() noexcept
{
    return pointers_;
}

const MapDefinition *SessionKeeper::WorldContextDefinition() const noexcept
{
    return worldPreviewActive_ ? MapDefinition::Find(WD_0LORENCIA) : worldBinding_.definition;
}

CMapManager &SessionKeeper::MapManagerObject() noexcept
{
    return mapManagerObject_;
}

CCameraMove &SessionKeeper::CameraMoveObject() noexcept
{
    return cameraMoveObject_;
}

CSkillManager &SessionKeeper::SkillManagerObject() noexcept
{
    return skillManagerObject_;
}

CSkillEffectMgr &SessionKeeper::SkillEffectManagerObject() noexcept
{
    return skillEffectManagerObject_;
}

CSummonSystem &SessionKeeper::SummonSystemObject() noexcept
{
    return summonSystemObject_;
}

CMonkSystem &SessionKeeper::MonkSystemObject() noexcept
{
    return monkSystemObject_;
}

CPortalMgr &SessionKeeper::PortalManagerObject() noexcept
{
    return portalManagerObject_;
}

bool &SessionKeeper::KanturuSuccessMap() noexcept
{
    return kanturuSuccessMap_;
}
bool &SessionKeeper::KanturuSuccessMapBackup() noexcept
{
    return kanturuSuccessMapBackup_;
}
int &SessionKeeper::KanturuMayaAction() noexcept
{
    return kanturuMayaAction_;
}
bool &SessionKeeper::KanturuMayaSkill2() noexcept
{
    return kanturuMayaSkill2_;
}
int &SessionKeeper::KanturuMayaSkill2Counter() noexcept
{
    return kanturuMayaSkill2Counter_;
}
int &SessionKeeper::KanturuMayaDieCounter() noexcept
{
    return kanturuMayaDieCounter_;
}
int &SessionKeeper::KanturuResult() noexcept
{
    return kanturuResult_;
}
float &SessionKeeper::KanturuResultAlpha() noexcept
{
    return kanturuResultAlpha_;
}
int &SessionKeeper::KanturuUserCount() noexcept
{
    return kanturuUserCount_;
}
int &SessionKeeper::KanturuMonsterCount() noexcept
{
    return kanturuMonsterCount_;
}

UI::Chat::Storage &SessionKeeper::ChatStorage() noexcept
{
    return chatStorage_;
}

SEASON3B::CNewUIMessageBoxMng &SessionKeeper::MessageBoxManagerObject() noexcept
{
    return *messageBoxManagerObject_;
}

CUIFriendMenu &SessionKeeper::FriendMenuObject() noexcept
{
    return *friendMenuObject_;
}

CUITextInputBox *&SessionKeeper::SingleTextInputBox() noexcept
{
    return singleTextInputBox_;
}

CUITextInputBox *&SessionKeeper::SinglePasswordInputBox() noexcept
{
    return singlePasswordInputBox_;
}

CUIMercenaryInputBox *&SessionKeeper::MercenaryInputBox() noexcept
{
    return mercenaryInputBox_;
}

CUITextInputBox *&SessionKeeper::FocusedTextInputBox() noexcept
{
    return focusedTextInputBox_;
}

DWORD &SessionKeeper::KeyFocusUiId() noexcept
{
    return keyFocusUiId_;
}

int &SessionKeeper::MouseWheelState() noexcept
{
    return mouseWheel_;
}

bool SessionKeeper::InputKeyIsNone(int virtualKey) const noexcept
{
    return virtualKey >= 0 && virtualKey < static_cast<int>(inputKeyStates_.size()) &&
           inputKeyStates_[virtualKey] == 0;
}

bool SessionKeeper::InputKeyIsRelease(int virtualKey) const noexcept
{
    return virtualKey >= 0 && virtualKey < static_cast<int>(inputKeyStates_.size()) &&
           inputKeyStates_[virtualKey] == 1;
}

bool SessionKeeper::InputKeyIsPress(int virtualKey) const noexcept
{
    return virtualKey >= 0 && virtualKey < static_cast<int>(inputKeyStates_.size()) &&
           inputKeyStates_[virtualKey] == 2;
}

bool SessionKeeper::InputKeyIsRepeat(int virtualKey) const noexcept
{
    return virtualKey >= 0 && virtualKey < static_cast<int>(inputKeyStates_.size()) &&
           inputKeyStates_[virtualKey] == 3;
}

bool SessionKeeper::InputKeyIsDown(int virtualKey) const noexcept
{
    return InputKeyIsPress(virtualKey) || InputKeyIsRepeat(virtualKey);
}

void SessionKeeper::SetInputKeyState(int virtualKey, int state) noexcept
{
    if (virtualKey >= 0 && virtualKey < static_cast<int>(inputKeyStates_.size()) && state >= 0 &&
        state <= 3)
    {
        inputKeyStates_[virtualKey] = static_cast<BYTE>(state);
    }
}

void SessionKeeper::ApplyInputKeySnapshot(const BYTE *states, bool focused) noexcept
{
    if (!focused || states == nullptr)
    {
        inputKeyStates_.fill(0);
        return;
    }
    std::copy_n(states, inputKeyStates_.size(), inputKeyStates_.begin());
}

CInGameShopSystem &SessionKeeper::InGameShopSystemObject() noexcept
{
    return *inGameShopSystemObject_;
}

PetProcess &SessionKeeper::PetProcessObject() noexcept
{
    if (!g_petProcess)
    {
        std::terminate();
    }
    return *g_petProcess;
}

SEASON3A::CursedTemple &SessionKeeper::CursedTempleObject() noexcept
{
    return *cursedTempleObject_;
}

SEASON3A::CGM3rdChangeUp &SessionKeeper::ThirdChangeObject() noexcept
{
    return *thirdChangeObject_;
}

double &SessionKeeper::FrameFpsAverage() noexcept
{
    return applicationKeeper_.FrameStorageRef().FPS_AVG;
}

bool &SessionKeeper::FrameDebugInfoEnabled() noexcept
{
    return applicationKeeper_.FrameStorageRef().g_bShowDebugInfo;
}

bool &SessionKeeper::FrameFpsCounterEnabled() noexcept
{
    return applicationKeeper_.FrameStorageRef().g_bShowFpsCounter;
}

float (&SessionKeeper::FrameTimesMs() noexcept)[ApplicationFrameStorage::FrameHistorySize]
{
    return applicationKeeper_.FrameStorageRef().s_frameTimesMs;
}

int &SessionKeeper::FrameHistoryIndex() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_frameIndex;
}

int &SessionKeeper::FrameHistoryCount() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_frameCount;
}

double &SessionKeeper::FrameHighestFps() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_highestFps;
}

float &SessionKeeper::FrameAverageFps() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_avgFps;
}

float &SessionKeeper::FrameOnePercentLow() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_onePercentLow;
}

float &SessionKeeper::FrameSlowestFps() noexcept
{
    return applicationKeeper_.FrameStorageRef().s_slowestFrameFps;
}

float (&SessionKeeper::FrameProfilerAccumulatorMs() noexcept)
    [ApplicationFrameStorage::ProfilerPassCount]
{
    return applicationKeeper_.FrameStorageRef().profilerAccumulatorMs;
}

CTimer *&SessionKeeper::FrameTimer() noexcept
{
    return applicationKeeper_.FrameStorageRef().g_pTimer;
}

float &SessionKeeper::FrameAnimationFactor() noexcept
{
    return frameAnimationFactor_;
}

double &SessionKeeper::FrameElapsedMilliseconds() noexcept { return frameElapsedMilliseconds_; }

double &SessionKeeper::FrameWorldTime() noexcept
{
    return applicationKeeper_.FrameStorageRef().WorldTime;
}

FrameTimingState &SessionKeeper::FrameTiming() noexcept
{
    return applicationKeeper_.FrameStorageRef().g_frameTiming;
}

std::chrono::steady_clock::time_point &SessionKeeper::FrameTimer2StartTickTime() noexcept
{
    return applicationKeeper_.FrameStorageRef().timer2StartTickTime;
}

bool &SessionKeeper::FrameRenderBoundingBox() noexcept
{
    return applicationKeeper_.FrameStorageRef().g_bRenderBoundingBox;
}

BOOL &SessionKeeper::PlatformWindowMode() noexcept
{
    return applicationKeeper_.PlatformStorageRef().g_bUseWindowMode;
}

int &SessionKeeper::PlatformOpenglWindowX() noexcept
{
    return DisplayForConstruction().OpenGlX();
}

int &SessionKeeper::PlatformOpenglWindowY() noexcept
{
    return DisplayForConstruction().OpenGlY();
}

int &SessionKeeper::PlatformOpenglWindowWidth() noexcept
{
    return DisplayForConstruction().OpenGlWidth();
}

int &SessionKeeper::PlatformOpenglWindowHeight() noexcept
{
    return DisplayForConstruction().OpenGlHeight();
}

unsigned int &SessionKeeper::PlatformWindowWidth() noexcept
{
    return DisplayForConstruction().WindowWidth();
}

unsigned int &SessionKeeper::PlatformWindowHeight() noexcept
{
    return DisplayForConstruction().WindowHeight();
}

float &SessionKeeper::PlatformScreenRateX() noexcept
{
    return DisplayForConstruction().ScreenRateX();
}

float &SessionKeeper::PlatformScreenRateY() noexcept
{
    return DisplayForConstruction().ScreenRateY();
}

SessionDisplayView &SessionKeeper::DisplayForConstruction() noexcept
{
    SessionWorkspace *workspace = applicationKeeper_.SessionWorkspaceUnit();
    SessionDisplayView *display = workspace != nullptr ? workspace->Display(id_) : nullptr;
    if (display == nullptr)
    {
        std::terminate();
    }
    return *display;
}

CameraState &SessionKeeper::CameraStateObject() noexcept
{
    return cameraState_;
}

CameraManager &SessionKeeper::CameraManagerObject() noexcept
{
    return GameplayForConstruction().CameraManagerObject();
}

CameraManager *SessionKeeper::CameraManagerUnit() const noexcept
{
    SessionGameplayUnit *gameplay = Gameplay();
    return gameplay != nullptr ? &gameplay->CameraManagerObject() : nullptr;
}

CameraProjection &SessionKeeper::CameraProjectionObject() noexcept
{
    return GameplayForConstruction().CameraProjectionObject();
}

SessionConfigStore &SessionKeeper::SessionConfigStoreObject() noexcept
{
    return applicationKeeper_.SessionConfigStoreObject();
}

ApplicationAudio &SessionKeeper::ApplicationAudioObject() noexcept
{
    return applicationKeeper_.ApplicationAudioObject();
}

Connection *&SessionKeeper::NetworkConnection() noexcept
{
    return socketClient_;
}

BOOL &SessionKeeper::GameServerConnected() noexcept
{
    return gameServerConnected_;
}

SessionKeeper::RepresentativeScalarStorage &SessionKeeper::RepresentativeScalar() noexcept
{
    return representativeScalar_;
}

SessionKeeper::RepresentativeFixedArrayStorage &SessionKeeper::RepresentativeFixedArray() noexcept
{
    return representativeFixedArray_;
}

SessionKeeper::RepresentativeMatrixStorage &SessionKeeper::RepresentativeMatrix() noexcept
{
    return representativeMatrix_;
}

SessionKeeper::RepresentativePointerStorage &SessionKeeper::RepresentativePointer() noexcept
{
    return representativePointer_;
}

SessionKeeper::RepresentativeContainerStorage &SessionKeeper::RepresentativeContainer() noexcept
{
    return representativeContainer_;
}

SessionKeeper::RepresentativeCallbackStorage &SessionKeeper::RepresentativeCallback() noexcept
{
    return representativeCallback_;
}

SessionKeeper::RepresentativeAtomicStorage &SessionKeeper::RepresentativeAtomic() noexcept
{
    return representativeAtomic_;
}

SessionDisplayView *SessionKeeper::Display() const noexcept
{
    return AccessWhenReady(display_);
}

SessionInputView *SessionKeeper::Input() const noexcept
{
    return AccessWhenReady(input_);
}

SessionAudioBusView *SessionKeeper::AudioOutput() const noexcept
{
    return AccessWhenReady(audioOutput_);
}

SessionFrameView *SessionKeeper::Frame() const noexcept
{
    return AccessWhenReady(frame_);
}

SessionClock *SessionKeeper::Clock() const noexcept
{
    return AccessWhenReady(clock_);
}

SessionRandom *SessionKeeper::Random() const noexcept
{
    return AccessWhenReady(random_);
}

SessionRandom &SessionKeeper::RandomForConstruction() noexcept
{
    if (random_ == nullptr)
    {
        std::terminate();
    }
    return *random_;
}

SessionLifecycleState *SessionKeeper::Lifecycle() const noexcept
{
    return AccessWhenReady(lifecycle_);
}

World *SessionKeeper::WorldUnit() const noexcept
{
    return AccessWhenReady(world_);
}

SessionUiUnit *SessionKeeper::Ui() const noexcept
{
    return AccessWhenReady(ui_);
}

SessionInteractionUnit *SessionKeeper::Interaction() const noexcept
{
    return AccessWhenReady(interaction_);
}

SessionPresentationUnit *SessionKeeper::Presentation() const noexcept
{
    return AccessWhenReady(presentation_);
}

SessionGameplayUnit *SessionKeeper::Gameplay() const noexcept
{
    return AccessWhenReady(gameplay_);
}

SessionGameDataUnit *SessionKeeper::GameData() const noexcept
{
    return AccessWhenReady(gameData_);
}

MUHelper::SessionMuHelperUnit *SessionKeeper::MuHelper() const noexcept
{
    return AccessWhenReady(muHelper_);
}

SessionNetworkUnit *SessionKeeper::Network() const noexcept
{
    return AccessWhenReady(network_);
}

SessionVisualUnit *SessionKeeper::Visual() const noexcept
{
    return AccessWhenReady(visual_);
}

SessionAudioLogicUnit *SessionKeeper::AudioLogic() const noexcept
{
    return AccessWhenReady(audioLogic_);
}

SessionAdvanceUnit *SessionKeeper::Advance() const noexcept
{
    return AccessWhenReady(advance_);
}

SessionRenderUnit *SessionKeeper::Renderer() const noexcept
{
    return AccessWhenReady(renderer_);
}

SessionUiView *SessionKeeper::UiView() const noexcept
{
    return AccessWhenReady(uiView_);
}

SessionVisualView *SessionKeeper::VisualView() const noexcept
{
    return AccessWhenReady(visualView_);
}

SessionAudioLogicView *SessionKeeper::AudioLogicView() const noexcept
{
    return AccessWhenReady(audioLogicView_);
}

WorldReadView *SessionKeeper::WorldRead() const noexcept
{
    World *world = WorldUnit();
    return world != nullptr ? std::addressof(world->ReadView()) : nullptr;
}

WorldCommandView *SessionKeeper::WorldCommands() const noexcept
{
    World *world = WorldUnit();
    return world != nullptr ? std::addressof(world->Commands()) : nullptr;
}

bool SessionKeeper::RegisterClock(SessionClock &clock) noexcept
{
    return RegisterShortcut(clock_, clock);
}

bool SessionKeeper::RegisterRandom(SessionRandom &random) noexcept
{
    return RegisterShortcut(random_, random);
}

bool SessionKeeper::RegisterLifecycle(SessionLifecycleState &lifecycle) noexcept
{
    return RegisterShortcut(lifecycle_, lifecycle);
}

bool SessionKeeper::RegisterWorld(World &world) noexcept
{
    return RegisterShortcut(world_, world);
}

bool SessionKeeper::RegisterUi(SessionUiUnit &ui) noexcept
{
    return RegisterShortcut(ui_, ui);
}

bool SessionKeeper::InitializeFriendMenuForConstruction()
{
    if (state_ != State::Linking || ui_ == nullptr || friendMenuObject_ != nullptr)
    {
        linkError_ = true;
        return false;
    }

    friendMenuObject_ = CreateSessionFriendMenu(*this);
    if (friendMenuObject_ == nullptr)
    {
        linkError_ = true;
        return false;
    }
    return true;
}

CUIFriendMenu *&SessionKeeper::FriendMenuForConstruction() noexcept
{
    return friendMenuObject_;
}

bool SessionKeeper::RegisterInteraction(SessionInteractionUnit &interaction) noexcept
{
    return RegisterShortcut(interaction_, interaction);
}

bool SessionKeeper::RegisterPresentation(SessionPresentationUnit &presentation) noexcept
{
    return RegisterShortcut(presentation_, presentation);
}

bool SessionKeeper::RegisterGameplay(SessionGameplayUnit &gameplay) noexcept
{
    return RegisterShortcut(gameplay_, gameplay);
}

bool SessionKeeper::RegisterGameData(SessionGameDataUnit &gameData) noexcept
{
    return RegisterShortcut(gameData_, gameData);
}

bool SessionKeeper::RegisterMuHelper(MUHelper::SessionMuHelperUnit &muHelper) noexcept
{
    return RegisterShortcut(muHelper_, muHelper);
}

bool SessionKeeper::RegisterNetwork(SessionNetworkUnit &network) noexcept
{
    return RegisterShortcut(network_, network);
}

bool SessionKeeper::RegisterVisual(SessionVisualUnit &visual) noexcept
{
    return RegisterShortcut(visual_, visual);
}

bool SessionKeeper::RegisterAudioLogic(SessionAudioLogicUnit &audioLogic) noexcept
{
    return RegisterShortcut(audioLogic_, audioLogic);
}

bool SessionKeeper::RegisterAdvance(SessionAdvanceUnit &advance) noexcept
{
    return RegisterShortcut(advance_, advance);
}

bool SessionKeeper::RegisterRenderer(SessionRenderUnit &renderer) noexcept
{
    return RegisterShortcut(renderer_, renderer);
}

SessionGameplayUnit &SessionKeeper::GameplayForConstruction() noexcept
{
    if (gameplay_ == nullptr)
    {
        std::terminate();
    }
    return *gameplay_;
}

SessionUiUnit &SessionKeeper::UiForConstruction() noexcept
{
    if (ui_ == nullptr)
    {
        std::terminate();
    }
    return *ui_;
}

SessionRenderUnit &SessionKeeper::RendererForConstruction() noexcept
{
    if (renderer_ == nullptr)
    {
        std::terminate();
    }
    return *renderer_;
}

SessionNetworkUnit &SessionKeeper::NetworkForConstruction() noexcept
{
    if (network_ == nullptr)
    {
        std::terminate();
    }
    return *network_;
}

SessionGameDataUnit &SessionKeeper::GameDataForConstruction() noexcept
{
    if (gameData_ == nullptr)
    {
        std::terminate();
    }
    return *gameData_;
}

MUHelper::SessionMuHelperUnit &SessionKeeper::MuHelperForConstruction() noexcept
{
    if (muHelper_ == nullptr)
    {
        std::terminate();
    }
    return *muHelper_;
}

ApplicationNetwork &SessionKeeper::ApplicationNetworkForConstruction() noexcept
{
    return applicationKeeper_.ApplicationNetworkForConstruction();
}

ManagedBindingUnit &SessionKeeper::ManagedBindingsForConstruction() noexcept
{
    return applicationKeeper_.ManagedBindingsForConstruction();
}

SessionManager &SessionKeeper::SessionManagerForConstruction() noexcept
{
    return applicationKeeper_.SessionManagerForConstruction();
}

ApplicationConfigUnit &SessionKeeper::ApplicationConfigForConstruction() noexcept
{
    return applicationKeeper_.ApplicationConfigForConstruction();
}

ApplicationAudio &SessionKeeper::ApplicationAudioForConstruction() noexcept
{
    return applicationKeeper_.ApplicationAudioForConstruction();
}

AppWindow &SessionKeeper::AppWindowForConstruction() noexcept
{
    return applicationKeeper_.AppWindowForConstruction();
}

CInput &SessionKeeper::ApplicationInputForConstruction() noexcept
{
    return applicationKeeper_.InputForConstruction();
}

CUIMng &SessionKeeper::LegacyUiManagerForConstruction() noexcept
{
    return UiForConstruction().LegacyUiManager();
}

SessionAudioBusView &SessionKeeper::AudioOutputForConstruction() noexcept
{
    if (audioOutput_ == nullptr)
    {
        std::terminate();
    }
    return *audioOutput_;
}

bool SessionKeeper::ResolveApplicationResources() noexcept
{
    ApplicationAudio *audio = applicationKeeper_.ApplicationAudioUnit();
    ApplicationSessionRuntime *runtime = applicationKeeper_.ApplicationSessionRuntimeUnit();
    SessionWorkspace *workspace = applicationKeeper_.SessionWorkspaceUnit();
    if (audio == nullptr || runtime == nullptr || workspace == nullptr)
    {
        return false;
    }

    display_ = workspace->Display(id_);
    input_ = workspace->Input(id_);
    audioOutput_ = audio->WorkspaceView().Audio(id_);
    frame_ = runtime->Frame();
    uiView_ = runtime->Ui();
    visualView_ = runtime->Visual();
    audioLogicView_ = runtime->AudioLogic();

    return display_ != nullptr && display_->Id() == id_ && input_ != nullptr &&
           input_->Id() == id_ && audioOutput_ != nullptr && audioOutput_->Id() == id_ &&
           frame_ != nullptr && uiView_ != nullptr && visualView_ != nullptr &&
           audioLogicView_ != nullptr;
}

bool SessionKeeper::CompleteLinks() noexcept
{
    if (state_ != State::Linking || linkError_ || !applicationKeeper_.IsReady() ||
        !ResolveApplicationResources() || clock_ == nullptr || random_ == nullptr ||
        lifecycle_ == nullptr || world_ == nullptr || ui_ == nullptr || interaction_ == nullptr ||
        presentation_ == nullptr || gameplay_ == nullptr || gameData_ == nullptr ||
        muHelper_ == nullptr || network_ == nullptr || visual_ == nullptr ||
        audioLogic_ == nullptr || advance_ == nullptr || renderer_ == nullptr ||
        friendMenuObject_ == nullptr)
    {
        return false;
    }
    if (!applicationKeeper_.AttachSession())
    {
        return false;
    }

    attachedToApplication_ = true;
    state_ = State::Ready;
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::KeeperLinksCompleted);
    }
    return true;
}

bool SessionKeeper::InitializeBuffStateSystem()
{
    return state_ == State::Ready && buffStateSystemObject_.Initialize();
}

bool SessionKeeper::BeginShutdown() noexcept
{
    if (state_ != State::Ready)
    {
        return false;
    }

    state_ = State::Shutdown;
    return true;
}

SessionLifecycleState::SessionLifecycleState(SessionKeeper &keeper,
                                             SessionLifecycleObserver *observer) noexcept
    : SessionLegacyCalls(keeper), observer_(observer)
{
    (void)sessionKeeper_.RegisterLifecycle(*this);
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::LifecycleStateConstructed);
    }
}

SessionLifecycleState::~SessionLifecycleState()
{
    if (observer_ != nullptr)
    {
        observer_->OnSessionLifecycleEvent(SessionLifecycleEvent::LifecycleStateDestroyed);
    }
}

void SessionLifecycleState::MarkAdvanced() noexcept
{
    ++advanceCount_;
    canRender_ = true;
}

void SessionLifecycleState::MarkRendered() noexcept
{
    ++renderCount_;
    canRender_ = false;
}

void SessionLifecycleState::BeginAdvance() noexcept
{
    canRender_ = false;
}

bool SessionLifecycleState::BeginRender() noexcept
{
    if (!canRender_)
    {
        return false;
    }

    canRender_ = false;
    return true;
}

std::uint64_t SessionLifecycleState::AdvanceCount() const noexcept
{
    return advanceCount_;
}

std::uint64_t SessionLifecycleState::RenderCount() const noexcept
{
    return renderCount_;
}
