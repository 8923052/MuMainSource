#include "domain/ItemsSkills.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "support/CoreMath.h"
#include "ui/session/UiSessionLogic.h"
#include "session/SessionKeeper.h"
#include "data/Localization.h"
#include "domain/CharacterSystem.h"
#include "app/ApplicationLoopFrame.h"
#include "I18N/All.h"
#include "render/Textures.h"
#include "data/GameData.h"
#include "render/ModelResources.h"
#include "session/SessionRender.h"
#include "domain/CharacterPresentation.h"
#include "app/ApplicationNetwork.h"
#include "session/SessionGameplay.h"
#include "domain/Events.h"
#include "data/ItemData.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "domain/EffectsUpdate.h"
#include "domain/MapSimulation.h"
#include "session/SessionNetwork.h"
#include "data/WorldData.h"
#include "render/Terrain.h"
#include "domain/WorldSimulation.h"
#include "render/World.h"
#include "domain/MovementAI.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "app/ApplicationAudio.h"
#include "session/SessionUi.h"
#include "ui/runtime/UiControls.h"
#include "render/Text.h"
#include "support/Scenes.h"
#include "domain/Quests.h"
#include "app/AppWindow.h"
#include "domain/Shop.h"
#include "support/Camera.h"
#include "render/ModelGeometry.h"
#include "session/SessionPresentation.h"
#include "domain/WorldPhysics.h"
#include "session/SessionWorkspace.h"
#include "domain/ChatSocial.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "domain/Automation.h"

namespace
{
void GetTokenBufflist(std::list<eBuffState> &outtokenbufflist, const eBuffState curbufftype)
{
    if (curbufftype >= eBuff_CastleRegimentDefense && curbufftype <= eBuff_CastleRegimentAttack3)
    {
        outtokenbufflist.push_back(eBuff_CastleRegimentDefense);
        outtokenbufflist.push_back(eBuff_CastleRegimentAttack1);
        outtokenbufflist.push_back(eBuff_CastleRegimentAttack2);
        outtokenbufflist.push_back(eBuff_CastleRegimentAttack3);
    }
    if (curbufftype >= eBuff_CrywolfAltarEnable && curbufftype <= eBuff_CrywolfNPCHide)
    {
        outtokenbufflist.push_back(eBuff_CrywolfAltarEnable);
        outtokenbufflist.push_back(eBuff_CrywolfAltarDisable);
        outtokenbufflist.push_back(eBuff_CrywolfAltarContracted);
        outtokenbufflist.push_back(eBuff_CrywolfAltarAttempt);
        outtokenbufflist.push_back(eBuff_CrywolfAltarOccufied);
        outtokenbufflist.push_back(eBuff_CrywolfHeroContracted);
        outtokenbufflist.push_back(eBuff_CrywolfNPCHide);
    }
    if ((curbufftype >= eBuff_PcRoomSeal1 && curbufftype <= eBuff_PcRoomSeal3) ||
        curbufftype == eBuff_NewWealthSeal)
    {
        outtokenbufflist.push_back(eBuff_NewWealthSeal);
        outtokenbufflist.push_back(eBuff_PcRoomSeal1);
        outtokenbufflist.push_back(eBuff_PcRoomSeal2);
        outtokenbufflist.push_back(eBuff_PcRoomSeal3);
    }
    // eBuff_Seal_HpRecovery, eBuff_Seal_MpRecovery
    if ((curbufftype >= eBuff_Seal1 && curbufftype <= eBuff_Seal4) ||
        curbufftype == eBuff_AscensionSealMaster || curbufftype == eBuff_WealthSealMaster)
    {
        outtokenbufflist.push_back(eBuff_Seal1);
        outtokenbufflist.push_back(eBuff_Seal2);
        outtokenbufflist.push_back(eBuff_Seal3);
        outtokenbufflist.push_back(eBuff_Seal4);
        outtokenbufflist.push_back(eBuff_Seal_HpRecovery);
        outtokenbufflist.push_back(eBuff_Seal_MpRecovery);
        outtokenbufflist.push_back(eBuff_AscensionSealMaster);
        outtokenbufflist.push_back(eBuff_WealthSealMaster);
    }

    if (curbufftype >= eBuff_EliteScroll1 && curbufftype <= eBuff_EliteScroll6)
    {
        outtokenbufflist.push_back(eBuff_EliteScroll1);
        outtokenbufflist.push_back(eBuff_EliteScroll2);
        outtokenbufflist.push_back(eBuff_EliteScroll3);
        outtokenbufflist.push_back(eBuff_EliteScroll4);
        outtokenbufflist.push_back(eBuff_EliteScroll5);
        outtokenbufflist.push_back(eBuff_EliteScroll6);
        outtokenbufflist.push_back(eBuff_Scroll_Battle);
        outtokenbufflist.push_back(eBuff_Scroll_Strengthen);
    }
    if (curbufftype >= eBuff_SecretPotion1 && curbufftype <= eBuff_SecretPotion5)
    {
        outtokenbufflist.push_back(eBuff_SecretPotion1);
        outtokenbufflist.push_back(eBuff_SecretPotion2);
        outtokenbufflist.push_back(eBuff_SecretPotion3);
        outtokenbufflist.push_back(eBuff_SecretPotion4);
        outtokenbufflist.push_back(eBuff_SecretPotion5);
    }
}
} // namespace

BuffPtr Buff::Make()
{
    BuffPtr buff(new Buff());
    return buff;
}

Buff::Buff()
{
}

Buff::~Buff()
{
    ClearBuff();
}

bool Buff::isBuff() const
{
    if (m_Buff.size() != 0)
        return true;
    return false;
}

bool Buff::isBuff(eBuffState buffstate) const
{
    if (!isBuff())
        return false;

    auto iter = m_Buff.find(buffstate);

    if (iter != m_Buff.end())
    {
        return true;
    }

    return false;
}

const eBuffState Buff::isBuff(std::list<eBuffState> buffstatelist) const
{
    if (!isBuff())
        return eBuffNone;

    for (auto iter = buffstatelist.begin(); iter != buffstatelist.end();)
    {
        auto Tempiter = iter;
        ++iter;
        eBuffState tempbufftype = (*Tempiter);

        if (isBuff(tempbufftype))
            return tempbufftype;
    }

    return eBuffNone;
}

void Buff::TokenBuff(eBuffState curbufftype)
{
    std::list<eBuffState> tokenbufflist;
    GetTokenBufflist(tokenbufflist, curbufftype);
    UnRegisterBuff(tokenbufflist);
    RegisterBuff(curbufftype);
}

const DWORD Buff::GetBuffCount(eBuffState buffstate) const
{
    DWORD tempcount = 0;

    if (!isBuff())
        return tempcount;

    auto iter = m_Buff.find(buffstate);

    if (iter != m_Buff.end())
    {
        tempcount = (*iter).second;
        return tempcount;
    }

    return tempcount;
}

const DWORD Buff::GetBuffSize() const
{
    return m_Buff.size();
}

const eBuffState Buff::GetBuff(int iterindex) const
{
    if (iterindex >= (int)GetBuffSize())
        return eBuffNone;

    int i = 0;

    for (auto iter = m_Buff.begin(); iter != m_Buff.end();)
    {
        auto tempiter = iter;
        ++iter;

        if (i == iterindex)
        {
            return (*tempiter).first;
        }

        i += 1;
    }

    return eBuffNone;
}

bool BuffStateSystem::IsEqualBuffType(Buff &buff, IN int iBuffType, OUT wchar_t *szBuffName)
{
    auto iter = buff.m_Buff.begin();
    BuffInfo buffinfo;

    while (iter != buff.m_Buff.end())
    {
        buffinfo = TheBuffInfo().GetBuffinfo(iter->first);
        if (buffinfo.s_BuffEffectType == iBuffType)
        {
            wcscpy(szBuffName, buffinfo.s_BuffName);
            return true;
        }

        iter++;
    }

    return false;
}

void Buff::RegisterBuff(eBuffState buffstate)
{
    auto iter = m_Buff.find(buffstate);

    if (iter == m_Buff.end())
    {
        m_Buff.insert(std::make_pair(buffstate, 1));
        ++revision_;
    }
}

void Buff::RegisterBuff(std::list<eBuffState> buffstate)
{
    for (auto iter = buffstate.begin(); iter != buffstate.end();)
    {
        auto tempiter = iter;
        ++iter;
        eBuffState &tempdata = (*tempiter);

        RegisterBuff(tempdata);
    }
}

void Buff::UnRegisterBuff(eBuffState buffstate)
{
    if (!isBuff())
    {
        return;
    }

    auto iter = m_Buff.find(buffstate);

    if (iter != m_Buff.end())
    {
        m_Buff.erase(iter);
        ++revision_;
    }
    else
    {
        return;
    }
}

void Buff::UnRegisterBuff(std::list<eBuffState> buffstate)
{
    for (auto iter = buffstate.begin(); iter != buffstate.end();)
    {
        auto tempiter = iter;
        ++iter;
        eBuffState &tempdata = (*tempiter);

        UnRegisterBuff(tempdata);
    }
}

void Buff::ClearBuff()
{
    if (m_Buff.empty())
        return;
    m_Buff.clear();
    ++revision_;
}

// Construction/Destruction

namespace
{
void CutTokenString(const wchar_t *pcCuttoken, std::list<std::wstring> &out)
{
    size_t length = 0;
    while (length < MAX_DESCRIPT_LENGTH && pcCuttoken[length] != L'\0')
    {
        ++length;
    }

    if (length == 0)
        return;

    size_t cutpos = 0;
    for (size_t i = 0; i <= length; ++i)
    {
        if (i == length || pcCuttoken[i] == L'/')
        {
            out.emplace_back(pcCuttoken + cutpos, i - cutpos);
            cutpos = i + 1;
        }
    }
}
} // namespace

BuffInfo::BuffInfo()
    : s_ItemType(255), s_ItemIndex(255), s_BuffIndex(0), s_BuffEffectType(0), s_BuffClassType(0),
      s_NoticeType(0), s_ClearType(0)
{
    memset(s_BuffName, 0, sizeof(s_BuffName));
    memset(s_BuffDescript, 0, sizeof(s_BuffDescript));
}

BuffInfo::~BuffInfo()
{
}

BuffScriptLoaderPtr BuffScriptLoader::Make(SessionKeeper &keeper)
{
    BuffScriptLoaderPtr info(new BuffScriptLoader(keeper));
    return info;
}

BuffScriptLoader::BuffScriptLoader(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_strSelectedML(keeper.AssetLanguage()),
      g_ErrorReport(keeper.ErrorReport()), g_hWnd(keeper.PlatformWindowHandle())
{
    std::wstring filename =
        L"data/local/" + g_strSelectedML + L"/BuffEffect_" + g_strSelectedML + L".bmd";

    if (!Load(filename))
    {
        assert(0);
    }
}

BuffScriptLoader::~BuffScriptLoader()
{
}

bool BuffScriptLoader::Load(const std::wstring &pchFileName)
{
    FILE *fp = _wfopen(pchFileName.c_str(), L"rb");

    if (fp != NULL)
    {
        DWORD structsize = sizeof(_BUFFINFO);

        DWORD listsize;
        fread(&listsize, sizeof(DWORD), 1, fp);

        BYTE *Buffer = new BYTE[structsize * listsize];
        fread(Buffer, structsize * listsize, 1, fp);

        DWORD dwCheckSum;
        fread(&dwCheckSum, sizeof(DWORD), 1, fp);

        fclose(fp);
        if (dwCheckSum != GenerateCheckSum2(Buffer, structsize * listsize, 0xE2F1))
        {
            wchar_t Text[256];
            mu_swprintf(Text, L"%ls - File corrupted.", pchFileName.c_str());
            g_ErrorReport.Write(Text);
            MessageBox(g_hWnd, Text, NULL, MB_OK);
            SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        }
        else
        {
            BYTE *pSeek = Buffer;

            for (DWORD i = 0; i < listsize; i++)
            {
                _BUFFINFO tempbuffinfo;

                BuxConvert(pSeek, structsize);
                memcpy(&tempbuffinfo, pSeek, structsize);

                BuffInfo buffinfo;
                buffinfo.s_BuffIndex = tempbuffinfo.s_BuffIndex;
                buffinfo.s_BuffEffectType = tempbuffinfo.s_BuffEffectType;
                buffinfo.s_ItemType = tempbuffinfo.s_ItemType;
                buffinfo.s_ItemIndex = tempbuffinfo.s_ItemIndex;

                CMultiLanguage::ConvertFromUtf8(buffinfo.s_BuffName, tempbuffinfo.s_BuffName,
                                                MAX_BUFF_NAME_LENGTH);
                CMultiLanguage::ConvertFromUtf8(buffinfo.s_BuffDescript,
                                                tempbuffinfo.s_BuffDescript, MAX_DESCRIPT_LENGTH);
                buffinfo.s_BuffName[MAX_BUFF_NAME_LENGTH] = L'\0';
                buffinfo.s_BuffDescript[MAX_DESCRIPT_LENGTH] = L'\0';

                buffinfo.s_BuffClassType = tempbuffinfo.s_BuffClassType;
                buffinfo.s_NoticeType = tempbuffinfo.s_NoticeType;
                buffinfo.s_ClearType = tempbuffinfo.s_ClearType;

                CutTokenString(buffinfo.s_BuffDescript, buffinfo.s_BuffDescriptlist);
                m_Info.insert(
                    std::make_pair(static_cast<eBuffState>(buffinfo.s_BuffIndex), buffinfo));

                pSeek += structsize;
            }
        }
        delete[] Buffer;
    }
    else
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", pchFileName.c_str());
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, NULL, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
    }

    return true;
}

const BuffInfo BuffScriptLoader::GetBuffinfo(eBuffState type) const
{
    if (type >= eBuff_Count)
        return BuffInfo();

    auto iter = m_Info.find(type);

    if (iter != m_Info.end())
    {
        return (*iter).second;
    }

    return BuffInfo();
}

eBuffClass BuffScriptLoader::IsBuffClass(eBuffState type) const
{
    if (type >= eBuff_Count)
        return eBuffClass_Count;

    auto iter = m_Info.find(type);

    if (iter != m_Info.end())
    {
        return static_cast<eBuffClass>((*iter).second.s_BuffClassType);
    }
    else
    {
        return eBuffClass_Count;
    }
}

#ifdef KJH_PBG_ADD_INGAMESHOP_SYSTEM
int BuffScriptLoader::GetBuffIndex(int iItemCode)
{
    auto iter = m_Info.begin();

    int iterItemCode = 0;

    while (iter != m_Info.end())
    {
        iterItemCode = ITEMINDEX(iter->second.s_ItemType, iter->second.s_ItemIndex);

        if (iterItemCode == iItemCode)
            return iter->second.s_BuffIndex;

        iter++;
    }

    return -1;
}

int BuffScriptLoader::GetBuffType(int iItemCode)
{
    auto iter = m_Info.begin();

    int iterItemCode = 0;

    while (iter != m_Info.end())
    {
        iterItemCode = ITEMINDEX(iter->second.s_ItemType, iter->second.s_ItemIndex);

        if (iterItemCode == iItemCode)
            return iter->second.s_BuffEffectType;

        iter++;
    }

    return -1;
}
#endif // KJH_PBG_ADD_INGAMESHOP_SYSTEM

// Construction/Destruction

BuffStateSystem::BuffStateSystem(SessionKeeper &keeper) noexcept : SessionLegacyCalls(keeper)
{
}

BuffStateSystem::~BuffStateSystem()
{
    Destroy();
}

bool BuffStateSystem::Initialize()
{
    if (m_BuffInfo || m_BuffTimeControl || m_BuffStateValueControl)
    {
        return false;
    }

    m_BuffInfo = BuffScriptLoader::Make(sessionKeeper_);
    m_BuffTimeControl = BuffTimeControl::Make(sessionKeeper_);
    m_BuffStateValueControl = BuffStateValueControl::Make(sessionKeeper_);
    return m_BuffInfo && m_BuffTimeControl && m_BuffStateValueControl;
}

void BuffStateSystem::Destroy()
{
}
// Construction/Destruction

BuffStateValueControlPtr BuffStateValueControl::Make(SessionKeeper &keeper)
{
    BuffStateValueControlPtr buffstatecontrol(new BuffStateValueControl(keeper));
    return buffstatecontrol;
}

BuffStateValueControl::BuffStateValueControl(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
{
    Initialize();
}

BuffStateValueControl::~BuffStateValueControl()
{
    Destroy();
}

void BuffStateValueControl::Initialize()
{
    for (int i = eBuff_Attack; i < eBuff_Count; ++i)
    {
        CheckValue(static_cast<eBuffState>(i));
    }
}

void BuffStateValueControl::Destroy()
{
}

eBuffValueLoadType BuffStateValueControl::CheckValue(eBuffState bufftype)
{
    if (bufftype >= eBuff_Attack && bufftype <= eBuff_GMEffect)
    {
        return eBuffValueLoad_None;
    }
    else
    {
        return eBuffValueLoad_ItemAddOption;
    }
}

void BuffStateValueControl::SetValue(eBuffState bufftype, BuffStateValueInfo &valueinfo)
{
    eBuffValueLoadType loadtype = CheckValue(bufftype);

    if (eBuffValueLoad_ItemAddOption == loadtype)
    {
        const BuffInfo buffinfo = g_BuffInfo(bufftype);
        const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(
            ITEMINDEX(buffinfo.s_ItemType, buffinfo.s_ItemIndex));

        valueinfo.s_Value1 = Item_data.m_byValue1;
        valueinfo.s_Value2 = Item_data.m_byValue2;
        valueinfo.s_Value2 = Item_data.m_Time;
    }
    else
    {
        valueinfo.s_Value1 = 0;
        valueinfo.s_Value2 = 0;
        valueinfo.s_Value2 = 0;
    }
}

const BuffStateValueControl::BuffStateValueInfo BuffStateValueControl::GetValue(eBuffState bufftype)
{
    BuffStateValueInfo tempvalueinfo;

    auto iter = m_BuffStateValue.find(bufftype);

    if (iter == m_BuffStateValue.end())
    {
        const BuffInfo buffinfo = g_BuffInfo(bufftype);

        if (buffinfo.s_ItemType != 255)
        {
            SetValue(bufftype, tempvalueinfo);
        }

        m_BuffStateValue.insert(std::make_pair(bufftype, tempvalueinfo));
    }
    else
    {
        tempvalueinfo = (*iter).second;
    }

    return tempvalueinfo;
}

void BuffStateValueControl::GetBuffInfoString(std::list<std::wstring> &outstr, eBuffState bufftype)
{
    BuffStateValueInfo tempvalueinfo;
    tempvalueinfo = GetValue(bufftype);

    const BuffInfo buffinfo = g_BuffInfo(bufftype);

    outstr = buffinfo.s_BuffDescriptlist;
    outstr.push_front(L"\n");
    outstr.push_front(buffinfo.s_BuffName);
}

void BuffStateValueControl::GetBuffValueString(std::wstring &outstr, eBuffState bufftype)
{
    BuffStateValueInfo tempvalueinfo;
    tempvalueinfo = GetValue(bufftype);

    wchar_t buff[60];

    if (tempvalueinfo.s_Value1 != 0)
    {
        mu_swprintf(buff, L"%d", tempvalueinfo.s_Value1);
        outstr = buff;
    }
    else
    {
        outstr = L"";
    }
}

BuffTimeControlPtr BuffTimeControl::Make(SessionKeeper &keeper)
{
    BuffTimeControlPtr bufftimecontrol(new BuffTimeControl(keeper));
    return bufftimecontrol;
}

BuffTimeControl::BuffTimeControl(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ConsoleDebug(keeper.ConsoleDebug())
{
}

BuffTimeControl::~BuffTimeControl()
{
    for (auto iter = m_BuffTimeList.begin(); iter != m_BuffTimeList.end();)
    {
        auto tempiter = iter;
        ++iter;

        auto bufftimetype = static_cast<eBuffTimeType>((*tempiter).first);

        frameTimerScheduler_.Kill(this, bufftimetype);
        m_BuffTimeList.erase(tempiter);
    }

    m_BuffTimeList.clear();
}

eBuffTimeType BuffTimeControl::CheckBuffTimeType(eBuffState bufftype)
{
    if (g_IsBuffClass(bufftype) == eBuffClass_Count)
    {
        return eBuffTime_None;
    }

    BuffInfo bInfo = g_BuffInfo(bufftype);

    return eBuffTimeType(1005 + bInfo.s_BuffEffectType);
}

DWORD BuffTimeControl::GetBuffEventTime(eBuffTimeType bufftimetype)
{
    return 1000;
}

DWORD BuffTimeControl::GetBuffMaxTime(eBuffState bufftype, DWORD curbufftime)
{
    if (curbufftime == 0)
    {
        const BuffInfo &buffinfo = g_BuffInfo(bufftype);

        if (buffinfo.s_ItemType == 255)
        {
            return -1;
        }
        else
        {
            const ITEM_ADD_OPTION &Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(
                ITEMINDEX(buffinfo.s_ItemType, buffinfo.s_ItemIndex));

            if (Item_data.m_Time == 0)
                return -1;

            return Item_data.m_Time;
        }
    }

    return curbufftime;
}

bool BuffTimeControl::IsBuffTime(eBuffTimeType bufftype)
{
    auto iter = m_BuffTimeList.find(bufftype);

    if (iter == m_BuffTimeList.end())
    {
        return false;
    }
    else
    {
        return true;
    }
}

void BuffTimeControl::RegisterBuffTime(eBuffState bufftype, DWORD curbufftime)
{
    eBuffTimeType bufftimetype = CheckBuffTimeType(bufftype);

    curbufftime = GetBuffMaxTime(bufftype, curbufftime);

    if (bufftimetype == eBuffTime_None || curbufftime == -1)
    {
        return;
    }

    if (IsBuffTime(bufftimetype))
        return;

    BuffTimeInfo buffinfo;
    buffinfo.s_BuffType = bufftype;
    buffinfo.s_CurBuffTime = curbufftime * 1000;
    buffinfo.s_EventBuffTime = GetTickCount();

    m_BuffTimeList.insert(std::make_pair(bufftimetype, buffinfo));

    // Tick the buff's remaining time every 900ms; the timer removes itself once
    // the buff expires (replaces the per-buff SetTimer/WM_TIMER).
    frameTimerScheduler_.SetRepeating(this, bufftimetype, 900, [this, bufftimetype] {
        if (!CheckBuffTime(bufftimetype))
            frameTimerScheduler_.Kill(this, bufftimetype);
    });
}

bool BuffTimeControl::UnRegisterBuffTime(eBuffState bufftype)
{
    eBuffTimeType bufftimetype = CheckBuffTimeType(bufftype);

    auto iter = m_BuffTimeList.find(bufftimetype);

    if (iter != m_BuffTimeList.end())
    {
        frameTimerScheduler_.Kill(this, bufftimetype);
        g_ConsoleDebug.Write(MCD_NORMAL, L"[Buff End] No. %d\r\n", bufftimetype);

        m_BuffTimeList.erase(iter);
        return true;
    }

    return false;
}

void BuffTimeControl::GetBuffStringTime(eBuffState bufftype, std::wstring &timeText)
{
    for (BuffTimeInfoMap::iterator iter = m_BuffTimeList.begin(); iter != m_BuffTimeList.end();
         ++iter)
    {
        BuffTimeInfo &bufftimeinfo = (*iter).second;

        if (bufftimeinfo.s_BuffType == bufftype)
        {
            float fTime = bufftimeinfo.s_CurBuffTime * 0.001f;
            GetStringTime(fTime, timeText, true);
        }
    }
}

void BuffTimeControl::GetBuffStringTime(DWORD type, std::wstring &timeText, bool issecond)
{
    auto iter = m_BuffTimeList.find(type);

    if (iter != m_BuffTimeList.end())
    {
        BuffTimeInfo &bufftimeinfo = (*iter).second;
        float fTime = bufftimeinfo.s_CurBuffTime * 0.001f;
        GetStringTime(fTime, timeText, issecond);
    }
}

const DWORD BuffTimeControl::GetBuffTime(DWORD type)
{
    auto iter = m_BuffTimeList.find(type);

    if (iter != m_BuffTimeList.end())
    {
        BuffTimeInfo &bufftimeinfo = (*iter).second;

        return bufftimeinfo.s_CurBuffTime;
    }
    return 0;
}

void BuffTimeControl::GetStringTime(DWORD time, std::wstring &timeText, bool isSecond)
{
    wchar_t buffer[100];

    if (isSecond)
    {
        DWORD day = time / (1440 * 60);
        DWORD oClock = (time - (day * (1440 * 60))) / 3600;
        DWORD minutes = (time - ((oClock * 3600) + (day * (1440 * 60)))) / 60;
        DWORD second = time % 60;

        if (day != 0)
        {
            mu_swprintf(buffer, L"%d %ls %d %ls %d %ls %d %ls", day, I18N::Game::Day, oClock,
                        I18N::Game::Hour, minutes, I18N::Game::Minute, second, I18N::Game::Second);
            timeText = buffer;
        }
        else if (day == 0 && oClock != 0)
        {
            mu_swprintf(buffer, L"%d %ls %d %ls %d %ls", oClock, I18N::Game::Hour, minutes,
                        I18N::Game::Minute, second, I18N::Game::Second);
            timeText = buffer;
        }
        else if (day == 0 && oClock == 0 && minutes != 0)
        {
            mu_swprintf(buffer, L"%d %ls %d %ls", minutes, I18N::Game::Minute, second,
                        I18N::Game::Second);
            timeText = buffer;
        }
        else if (day == 0 && oClock == 0 && minutes == 0)
        {
            mu_swprintf(buffer, L"%ls", I18N::Game::LessThan1Minutes);
            timeText = buffer;
        }
    }
    else
    {
        DWORD day = time / 1440;
        DWORD oClock = (time - (day * 1440)) / 60;
        DWORD minutes = time % 60;

        if (day != 0)
        {
            mu_swprintf(buffer, L"%d %ls %d %ls %d %ls", day, I18N::Game::Day, oClock,
                        I18N::Game::Hour, minutes, I18N::Game::Minute);
            timeText = buffer;
        }
        else if (day == 0 && oClock != 0)
        {
            mu_swprintf(buffer, L"%d %ls %d %ls", oClock, I18N::Game::Hour, minutes,
                        I18N::Game::Minute);
            timeText = buffer;
        }
        else if (day == 0 && oClock == 0 && minutes != 0)
        {
            mu_swprintf(buffer, L"%d %ls", minutes, I18N::Game::Minute);
            timeText = buffer;
        }
    }
}

bool BuffTimeControl::CheckBuffTime(DWORD type)
{
    auto iter = m_BuffTimeList.find(type);

    if (iter != m_BuffTimeList.end())
    {
        BuffTimeInfo &bufftimeinfo = (*iter).second;

        DWORD iCurBufftime = bufftimeinfo.s_CurBuffTime;
        if (iCurBufftime > GetTickCount() - bufftimeinfo.s_EventBuffTime)
        {
            iCurBufftime -= GetTickCount() - bufftimeinfo.s_EventBuffTime;
        }
        else
        {
            iCurBufftime = 0;
        }
        bufftimeinfo.s_EventBuffTime = GetTickCount();

        if (iCurBufftime <= 0)
        {
            bufftimeinfo.s_CurBuffTime = 0;
            return false;
        }
        else
        {
            bufftimeinfo.s_CurBuffTime = iCurBufftime;
            return true;
        }
    }
    return false;
}

// common

namespace
{
template <typename Container, typename T>
constexpr bool Contains(const Container &container, const T &value)
{
    for (const auto &item : container)
    {
        if (item == value)
        {
            return true;
        }
    }
    return false;
}

constexpr std::array<int, 12> kDarkLordHairModels{
    MODEL_SKELETON1,
    MODEL_SKELETON2,
    MODEL_SKELETON3,
    MODEL_SKELETON_PCBANG,
    MODEL_HALLOWEEN,
    MODEL_XMAS_EVENT_CHANGE_GIRL,
    MODEL_GM_CHARACTER,
    MODEL_CURSEDTEMPLE_ALLIED_PLAYER,
    MODEL_CURSEDTEMPLE_ILLUSION_PLAYER,
    MODEL_XMAS2008_SNOWMAN,
    MODEL_PANDA,
    MODEL_SKELETON_CHANGED,
};

constexpr std::array<int, 6> kDarkCloakModels{
    MODEL_HALLOWEEN, MODEL_XMAS_EVENT_CHANGE_GIRL, MODEL_GM_CHARACTER, MODEL_XMAS2008_SNOWMAN,
    MODEL_PANDA,     MODEL_SKELETON_CHANGED,
};

constexpr std::array<short, 8> kChangeRingTypes{
    ITEM_TRANSFORMATION_RING,
    ITEM_ELITE_TRANSFER_SKELETON_RING,
    ITEM_JACK_OLANTERN_TRANSFORMATION_RING,
    ITEM_CHRISTMAS_TRANSFORMATION_RING,
    ITEM_GAME_MASTER_TRANSFORMATION_RING,
    ITEM_SNOWMAN_TRANSFORMATION_RING,
    ITEM_PANDA_TRANSFORMATION_RING,
    ITEM_SKELETON_TRANSFORMATION_RING,
};

constexpr std::array<short, 6> kIcarusBanRingTypes{
    ITEM_TRANSFORMATION_RING,
    ITEM_ELITE_TRANSFER_SKELETON_RING,
    ITEM_JACK_OLANTERN_TRANSFORMATION_RING,
    ITEM_CHRISTMAS_TRANSFORMATION_RING,
    ITEM_GAME_MASTER_TRANSFORMATION_RING,
    ITEM_SNOWMAN_TRANSFORMATION_RING,
};
} // namespace

bool CChangeRingManager::CheckDarkLordHair(int iType) const
{
    return Contains(kDarkLordHairModels, iType);
}

bool CChangeRingManager::CheckDarkCloak(CLASS_TYPE iClass, int iType) const
{
    if (iClass != CLASS_DARK)
        return false;

    return Contains(kDarkCloakModels, iType);
}

bool CChangeRingManager::CheckChangeRing(short RingType) const
{
    return Contains(kChangeRingTypes, RingType);
}

bool CChangeRingManager::CheckRepair(int iType) const
{
    return Contains(kChangeRingTypes, iType);
}

bool CChangeRingManager::CheckMoveMap(short sLeftRingType, short sRightRingType) const
{
    return Contains(kChangeRingTypes, sLeftRingType) || Contains(kChangeRingTypes, sRightRingType);
}

bool CChangeRingManager::CheckBanMoveIcarusMap(short sLeftRingType, short sRightRingType) const
{
    return Contains(kIcarusBanRingTypes, sLeftRingType) ||
           Contains(kIcarusBanRingTypes, sRightRingType);
}

bool SessionGameplayUnit::EquipItem(int iIndex, std::span<const BYTE> pbyItemPacket)
{
    if (iIndex < 0 || iIndex >= MAX_EQUIPMENT_INDEX || !ItemStore() || !CharacterMachine)
    {
        return false;
    }

    ITEM *pTargetItemSlot = &CharacterMachine->Equipment[iIndex];
    if (pTargetItemSlot->Type > 0)
    {
        UnequipItem(iIndex);
    }

    ITEM *pTempItem = ItemStore()->CreateItem(pbyItemPacket);

    if (nullptr == pTempItem)
    {
        return false;
    }

    if (pTempItem->Type == ITEM_DARK_HORSE_ITEM)
    {
        SocketClient->ToGameServer()->SendPetInfoRequest(PetType::DarkHorse, StorageType::Inventory,
                                                         iIndex);
    }

    if (pTempItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        CreatePetDarkSpirit(Hero);
        SocketClient->ToGameServer()->SendPetInfoRequest(PetType::DarkRaven, StorageType::Inventory,
                                                         iIndex);
    }

    pTempItem->lineal_pos = iIndex;
    pTempItem->ex_src_type = ITEM_EX_SRC_EQUIPMENT;
    memcpy(pTargetItemSlot, pTempItem, sizeof(ITEM));
    ItemStore()->DeleteItem(pTempItem);

    CreateEquippingEffect(pTargetItemSlot);

    return true;
}

void SessionGameplayUnit::UnequipItem(int iIndex)
{
    if (iIndex >= 0 && iIndex < MAX_EQUIPMENT_INDEX && ItemStore() && CharacterMachine)
    {
        ITEM *pEquippedItem = &CharacterMachine->Equipment[iIndex];

        if (pEquippedItem && pEquippedItem->Type != -1)
        {
            if (pEquippedItem->Type == ITEM_DARK_HORSE_ITEM)
            {
                Hero->InitPetInfo(PET_TYPE_DARK_HORSE);
            }
            else if (pEquippedItem->Type == ITEM_DARK_RAVEN_ITEM)
            {
                DeletePet(Hero);
                Hero->InitPetInfo(PET_TYPE_DARK_SPIRIT);
            }

            if (pEquippedItem->Type != ITEM_DARK_RAVEN_ITEM)
                DeleteEquippingEffectBug(pEquippedItem);

            pEquippedItem->Type = -1;
            pEquippedItem->Level = 0;
            pEquippedItem->Number = -1;
            pEquippedItem->ExcellentFlags = 0;
            pEquippedItem->Durability = 0;
            pEquippedItem->AncientDiscriminator = 0;
            pEquippedItem->AncientBonusOption = 0;
            pEquippedItem->SocketCount = 0;
            for (int i = 0; i < MAX_SOCKETS; ++i)
            {
                pEquippedItem->SocketSeedID[i] = SOCKET_EMPTY;
                pEquippedItem->SocketSphereLv[i] = 0;
            }
            pEquippedItem->SocketSeedSetOption = 0;
            DeleteEquippingEffect();
        }
    }
}

void SessionGameplayUnit::UnequipAllItems()
{
    if (CharacterMachine)
    {
        for (int i = 0; i < MAX_EQUIPMENT_INDEX; i++)
        {
            UnequipItem(i);
        }
    }
}

bool SessionGameplayUnit::IsEquipable(int iIndex, ITEM *pItem) const
{
    if (pItem == nullptr)
        return false;

    if (!EquipmentSlotAccepts(iIndex, *pItem))
        return false;

    const WORD wStrength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
    const WORD wDexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
    const WORD wEnergy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
    const WORD wVitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
    const WORD wCharisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
    const WORD wLevel = CharacterAttribute->Level;

    const int iItemLevel = pItem->Level;

    int iDecNeedStrength = 0, iDecNeedDex = 0;

    if (iItemLevel >= pItem->Jewel_Of_Harmony_OptionLevel)
    {
        StrengthenCapability SC;
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, pItem, 0);

        if (SC.SI_isNB)
        {
            iDecNeedStrength = SC.SI_NB.SI_force;
            iDecNeedDex = SC.SI_NB.SI_activity;
        }
    }
    if (pItem->SocketCount > 0)
    {
        for (int i = 0; i < pItem->SocketCount; ++i)
        {
            if (pItem->SocketSeedID[i] == 38)
            {
                const int iReqStrengthDown = g_SocketItemMgr.GetSocketOptionValue(pItem, i);
                iDecNeedStrength += iReqStrengthDown;
            }
            else if (pItem->SocketSeedID[i] == 39)
            {
                const int iReqDexterityDown = g_SocketItemMgr.GetSocketOptionValue(pItem, i);
                iDecNeedDex += iReqDexterityDown;
            }
        }
    }

    if (pItem->RequireStrength - iDecNeedStrength > wStrength)
        return false;
    if (pItem->RequireDexterity - iDecNeedDex > wDexterity)
        return false;
    if (pItem->RequireEnergy > wEnergy)
        return false;
    if (pItem->RequireVitality > wVitality)
        return false;
    if (pItem->RequireCharisma > wCharisma)
        return false;
    if (pItem->RequireLevel > wLevel)
        return false;

    if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        const auto pPetInfo = GetPetInfo(pItem);
        if (pPetInfo->m_dwPetType == PET_TYPE_NONE)
        {
            return false;
        }

        const auto requiredCharisma = (185 + (pPetInfo->m_wLevel * 15));
        if (requiredCharisma > wCharisma)
        {
            return false;
        }
    }

    if (gMapManager.ContextMap() == WD_7ATLANSE &&
        (pItem->Type >= ITEM_HORN_OF_UNIRIA && pItem->Type <= ITEM_HORN_OF_DINORANT))
    {
        return false;
    }
    if (pItem->Type == ITEM_HORN_OF_UNIRIA && gMapManager.ContextMap() == WD_10HEAVEN)
    {
        return false;
    }
    if (pItem->Type == ITEM_HORN_OF_UNIRIA &&
        g_Direction.m_CKanturu.IsMayaScene(gMapManager.ContextMap()))
    {
        return false;
    }
    if (gMapManager.InChaosCastle() ||
        (Get_State_Only_Elf() && g_isCharacterBuff((&Hero->Object), eBuff_CrywolfHeroContracted)))
    {
        if ((pItem->Type >= ITEM_HORN_OF_UNIRIA && pItem->Type <= ITEM_DARK_RAVEN_ITEM) ||
            pItem->Type == ITEM_HORN_OF_FENRIR)
            return false;
    }
    else if ((pItem->Type >= ITEM_HORN_OF_UNIRIA && pItem->Type <= ITEM_DARK_HORSE_ITEM ||
              pItem->Type == ITEM_HORN_OF_FENRIR) &&
             Hero->Object.CurrentAction >= PLAYER_SIT1 &&
             Hero->Object.CurrentAction <= PLAYER_SIT_FEMALE2)
    {
        return false;
    }

    return true;
}

void SessionGameplayUnit::CreateEquippingEffect(ITEM *pItem)
{
    SetCharacterClass(Hero);
    OBJECT *pHeroObject = &Hero->Object;
    if (false == gMapManager.InChaosCastle())
    {
        switch (pItem->Type)
        {
        case ITEM_HELPER:
            CreateMount(MODEL_HELPER, pHeroObject->Position, pHeroObject);
            break;
        case ITEM_HORN_OF_UNIRIA:
            CreateMount(MODEL_UNICON, pHeroObject->Position, pHeroObject);
            if (!Hero->SafeZone)
                CreateEffect(BITMAP_MAGIC + 1, pHeroObject->Position, pHeroObject->Angle,
                             pHeroObject->Light, 1, pHeroObject);
            break;
        case ITEM_HORN_OF_DINORANT:
            CreateMount(MODEL_PEGASUS, pHeroObject->Position, pHeroObject);
            if (!Hero->SafeZone)
                CreateEffect(BITMAP_MAGIC + 1, pHeroObject->Position, pHeroObject->Angle,
                             pHeroObject->Light, 1, pHeroObject);
            break;
        case ITEM_DARK_HORSE_ITEM:
            CreateMount(MODEL_DARK_HORSE, pHeroObject->Position, pHeroObject);
            if (!Hero->SafeZone)
                CreateEffect(BITMAP_MAGIC + 1, pHeroObject->Position, pHeroObject->Angle,
                             pHeroObject->Light, 1, pHeroObject);
            break;
        case ITEM_HORN_OF_FENRIR:
            Hero->Helper.ExcellentFlags = pItem->ExcellentFlags;
            if (pItem->ExcellentFlags == 0x01)
            {
                CreateMount(MODEL_FENRIR_BLACK, pHeroObject->Position, pHeroObject);
            }
            else if (pItem->ExcellentFlags == 0x02)
            {
                CreateMount(MODEL_FENRIR_BLUE, pHeroObject->Position, pHeroObject);
            }
            else if (pItem->ExcellentFlags == 0x04)
            {
                CreateMount(MODEL_FENRIR_GOLD, pHeroObject->Position, pHeroObject);
            }
            else
            {
                CreateMount(MODEL_FENRIR_RED, pHeroObject->Position, pHeroObject);
            }

            if (!Hero->SafeZone)
            {
                CreateEffect(BITMAP_MAGIC + 1, pHeroObject->Position, pHeroObject->Angle,
                             pHeroObject->Light, 1, pHeroObject);
            }
            break;
        case ITEM_DEMON:
            g_petProcess.CreatePet(pItem->Type, MODEL_DEMON, pHeroObject->Position, Hero);
            break;
        case ITEM_SPIRIT_OF_GUARDIAN:
            g_petProcess.CreatePet(pItem->Type, MODEL_SPIRIT_OF_GUARDIAN, pHeroObject->Position,
                                   Hero);
            break;
        case ITEM_PET_RUDOLF:
            g_petProcess.CreatePet(pItem->Type, MODEL_PET_RUDOLF, pHeroObject->Position, Hero);
            break;
        case ITEM_PET_PANDA:
            g_petProcess.CreatePet(pItem->Type, MODEL_PET_PANDA, pHeroObject->Position, Hero);
            break;
        case ITEM_PET_UNICORN:
            g_petProcess.CreatePet(pItem->Type, MODEL_PET_UNICORN, pHeroObject->Position, Hero);
            break;
        case ITEM_PET_SKELETON:
            g_petProcess.CreatePet(pItem->Type, MODEL_PET_SKELETON, pHeroObject->Position, Hero);
            break;
        }
    }
    if (Hero->EtcPart <= 0 || Hero->EtcPart > 3)
    {
        if (pItem->Type == ITEM_WIZARDS_RING && pItem->Level == 3)
        {
            DeleteParts(Hero);
            Hero->EtcPart = PARTS_LION;
        }
    }
    if (pItem->Type == ITEM_WING_OF_RUIN || pItem->Type == ITEM_CAPE_OF_LORD ||
        pItem->Type == ITEM_WING + 130 ||
        (pItem->Type >= ITEM_CAPE_OF_FIGHTER && pItem->Type <= ITEM_CAPE_OF_OVERRULE) ||
        (pItem->Type == ITEM_WING + 135) || pItem->Type == ITEM_CAPE_OF_EMPEROR)
    {
        DeleteCloth(Hero, &Hero->Object);
    }
}

void SessionGameplayUnit::DeleteEquippingEffectBug(ITEM *pItem)
{
    if (g_petProcess.IsPet(pItem->Type) == true)
    {
        g_petProcess.DeletePet(Hero, pItem->Type);
    }

    switch (pItem->Type)
    {
    case ITEM_CAPE_OF_LORD:
    case ITEM_WING_OF_RUIN:
    case ITEM_CAPE_OF_EMPEROR:
    case ITEM_WING + 130:
    case ITEM_CAPE_OF_FIGHTER:
    case ITEM_CAPE_OF_OVERRULE:
    case ITEM_WING + 135:
        DeleteCloth(Hero, &Hero->Object);
        return;
    }

    if (IsMount(pItem) == true)
    {
        DeleteMount(&Hero->Object);
    }
}

void SessionGameplayUnit::DeleteEquippingEffect()
{
    if (Hero->EtcPart < PARTS_ATTACK_TEAM_MARK)
    {
        DeleteParts(Hero);
        if (Hero->EtcPart > 3)
        {
            Hero->EtcPart = 0;
        }
    }

    SetCharacterClass(Hero);
}

bool SessionGameplayUnit::EquipmentSlotAccepts(int iIndex, const ITEM &item) const
{
    const ITEM *pItem = &item;
    const ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];
    bool bEquipable = false;
    if (pItemAttr->RequireClass[gCharacterManager.GetBaseClass(Hero->Class)])
        bEquipable = true;

    else if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK &&
             pItemAttr->RequireClass[CLASS_WIZARD] && pItemAttr->RequireClass[CLASS_KNIGHT])
        bEquipable = true;

    const BYTE byFirstClass = gCharacterManager.GetBaseClass(Hero->Class);
    const BYTE byStepClass = gCharacterManager.GetStepClass(Hero->Class);
    if (pItemAttr->RequireClass[byFirstClass] > byStepClass)
    {
        return false;
    }

    if (bEquipable == false)
        return false;

    bEquipable = false;
    if (pItemAttr->m_byItemSlot == iIndex)
        bEquipable = true;

    else if (pItemAttr->m_byItemSlot == EQUIPMENT_WEAPON_RIGHT && iIndex == EQUIPMENT_WEAPON_LEFT)
    {
        if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_KNIGHT ||
            gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK ||
            gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
        {
            if (!pItemAttr->TwoHand)
                bEquipable = true;
#ifdef PBG_FIX_EQUIP_TWOHANDSWORD
            else
            {
                bEquipable = false;
                return false;
            }
#endif //PBG_FIX_EQUIP_TWOHANDSWORD
        }
        else if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_SUMMONER &&
                 !(pItem->Type >= ITEM_STAFF && pItem->Type <= ITEM_STAFF + MAX_ITEM_INDEX))
            bEquipable = true;
    }
    else if (pItemAttr->m_byItemSlot == EQUIPMENT_RING_RIGHT && iIndex == EQUIPMENT_RING_LEFT)
        bEquipable = true;

    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF)
    {
        const ITEM *l = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
        if (iIndex == EQUIPMENT_WEAPON_RIGHT && l->Type != ITEM_BOLT &&
            (l->Type >= ITEM_BOW && l->Type < ITEM_BOW + MAX_ITEM_INDEX))
        {
            if (pItem->Type != ITEM_ARROWS)
                bEquipable = false;
        }
    }

    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
    {
        if (iIndex == EQUIPMENT_GLOVES)
            bEquipable = false;
        else if (pItemAttr->m_byItemSlot == EQUIPMENT_WEAPON_RIGHT)
            bEquipable = g_CMonkSystem.RageEquipmentWeapon(iIndex, pItem->Type);
    }

    if (bEquipable == false)
        return false;

    return bEquipable;
}

InventoryGrid::InventoryGrid(SessionKeeper &keeper, SessionItemStore &store, InventoryRole role)
    : SessionLegacyCalls(keeper), store_(store), g_SocketItemMgr(keeper.SocketItemManager()),
      role_(role)
{
    switch (role)
    {
    case InventoryRole::Player:
        storageType_ = STORAGE_TYPE::INVENTORY;
        rows_ = ROW_INVENTORY;
        offset_ = MAX_EQUIPMENT;
        break;
    case InventoryRole::PlayerExtension0:
    case InventoryRole::PlayerExtension1:
    case InventoryRole::PlayerExtension2:
    case InventoryRole::PlayerExtension3:
        storageType_ = STORAGE_TYPE::INVENTORY;
        offset_ = MAX_MY_INVENTORY_INDEX +
                  MAX_INVENTORY_EXT_ONE *
                      (static_cast<int>(role) - static_cast<int>(InventoryRole::PlayerExtension0));
        break;
    case InventoryRole::Vault:
    case InventoryRole::VaultExtension:
        storageType_ = STORAGE_TYPE::VAULT;
        rows_ = MAX_SHOP_INVENTORY / columns_;
        if (role == InventoryRole::VaultExtension)
            offset_ = MAX_SHOP_INVENTORY;
        break;
    case InventoryRole::TradeOwn:
        storageType_ = STORAGE_TYPE::TRADE;
        break;
    case InventoryRole::Crafting:
        storageType_ = STORAGE_TYPE::CHAOS_MIX;
        break;
    case InventoryRole::LuckyCrafting:
        storageType_ = STORAGE_TYPE::LUCKYITEM_TRADE;
        break;
    case InventoryRole::MyShop:
        storageType_ = STORAGE_TYPE::MYSHOP;
        offset_ = MAX_MY_INVENTORY_EX_INDEX;
        break;
    case InventoryRole::OtherShop:
        offset_ = MAX_MY_INVENTORY_EX_INDEX;
        break;
    case InventoryRole::NpcShop:
        rows_ = MAX_SHOP_INVENTORY / columns_;
        break;
    default:
        break;
    }
    cells_.resize(columns_ * rows_);
    items_.reserve(cells_.size());
}

InventoryGrid::~InventoryGrid()
{
    RemoveAllItems();
}

ITEM *InventoryGrid::GetItem(int index) const
{
    return index >= 0 && static_cast<size_t>(index) < items_.size() ? items_[index] : nullptr;
}

ITEM *InventoryGrid::FindItem(int slot) const
{
    slot -= offset_;
    return slot >= 0 && static_cast<size_t>(slot) < cells_.size() ? cells_[slot] : nullptr;
}

ITEM *InventoryGrid::FindItem(int column, int row) const
{
    if (column < 0 || column >= columns_ || row < 0 || row >= rows_)
        return nullptr;
    return cells_[row * columns_ + column];
}

ITEM *InventoryGrid::FindItemByKey(DWORD key) const
{
    for (auto *item : items_)
        if (item->Key == key)
            return item;
    return nullptr;
}

ITEM *InventoryGrid::FindTypeItem(short type) const
{
    for (auto *item : items_)
        if (item->Type == type)
            return item;
    return nullptr;
}

int InventoryGrid::GetItemCount(short type, int level) const
{
    int count = 0;
    for (auto *item : items_)
        if (item->Type == type && (level < 0 || item->Level == level))
            count += item->Durability == 0 ? 1 : item->Durability;
    return count;
}

int InventoryGrid::FindItemIndex(short type, int level) const
{
    for (auto *item : items_)
        if (item->Type == type && (level < 0 || item->Level == level))
            return GetIndexByItem(item);
    return -1;
}

int InventoryGrid::FindItemReverseIndex(short type, int level) const
{
    ITEM *selected = nullptr;
    for (auto *item : items_)
    {
        if (item->Type != type || (level >= 0 && item->Level != level))
            continue;
        if (!selected || item->x > selected->x || (item->x == selected->x && item->y > selected->y))
            selected = item;
    }
    return GetIndexByItem(selected);
}

short InventoryGrid::FindItemTypeByPos(int column, int row) const
{
    const auto *item = FindItem(column, row);
    return item ? item->Type : -1;
}

int InventoryGrid::GetNumItemByKey(DWORD key) const
{
    // Preserve the legacy full-grid count contract used by existing callers.
    int count = 0;
    for (auto *item : cells_)
    {
        if (!item)
            return 0;
        if (item->Key == key)
            ++count;
    }
    return count;
}

int InventoryGrid::GetNumItemByType(short type) const
{
    return static_cast<int>(std::count_if(cells_.begin(), cells_.end(), [type](const ITEM *item) {
        return item && item->Type == type;
    }));
}

int InventoryGrid::GetEmptySlotCount() const
{
    return static_cast<int>(std::count(cells_.begin(), cells_.end(), nullptr));
}

bool InventoryGrid::CheckSlot(int column, int row, int width, int height) const
{
    if (column < 0 || row < 0 || width <= 0 || height <= 0 || column + width > columns_ ||
        row + height > rows_)
        return false;
    for (int y = row; y < row + height; ++y)
        for (int x = column; x < column + width; ++x)
            if (cells_[y * columns_ + x])
                return false;
    return true;
}

int InventoryGrid::FindEmptySlot(int width, int height) const
{
    int column, row;
    return FindEmptySlot(width, height, column, row) ? GetIndex(column, row) : -1;
}

bool InventoryGrid::FindEmptySlot(int width, int height, int &column, int &row) const
{
    for (int y = 0; y < rows_; ++y)
        for (int x = 0; x < columns_; ++x)
            if (CheckSlot(x, y, width, height))
            {
                column = x;
                row = y;
                return true;
            }
    return false;
}

bool InventoryGrid::CanMove(int slot, ITEM *item) const
{
    slot -= offset_;
    if (slot < 0 || static_cast<size_t>(slot) >= cells_.size())
        return false;
    return CanMove(slot % columns_, slot / columns_, item);
}

bool InventoryGrid::CanMove(int column, int row, ITEM *item) const
{
    const auto &size = ItemAttribute[item->Type];
    return CheckSlot(column, row, size.Width, size.Height);
}

void InventoryGrid::FillItemCells(ITEM &item, ITEM *value)
{
    const auto &size = ItemAttribute[item.Type];
    for (int y = item.y; y < item.y + size.Height; ++y)
        std::fill_n(cells_.begin() + y * columns_ + item.x, size.Width, value);
}

bool InventoryGrid::InsertOwned(int column, int row, ITEM *item)
{
    if (!item)
        return false;
    if (!CanMove(column, row, item))
    {
        store_.DeleteItem(item);
        return false;
    }
    item->x = column;
    item->y = row;
    FillItemCells(*item, item);
    items_.push_back(item);
    return true;
}

bool InventoryGrid::AddItem(int slot, std::span<const BYTE> packet)
{
    slot -= offset_;
    if (slot < 0 || static_cast<size_t>(slot) >= cells_.size())
        return false;
    return AddItem(slot % columns_, slot / columns_, packet);
}

bool InventoryGrid::AddItem(int column, int row, std::span<const BYTE> packet)
{
    return InsertOwned(column, row, store_.CreateItem(packet));
}

bool InventoryGrid::AddItem(int column, int row, ITEM *item)
{
    return InsertOwned(column, row, store_.CreateItem(item));
}

bool InventoryGrid::AddItem(int column, int row, BYTE type, BYTE subtype, BYTE level,
                            BYTE durability, BYTE option, BYTE ancient, BYTE guardian, BYTE harmony)
{
    return InsertOwned(
        column, row,
        store_.CreateItem(type, subtype, level, durability, option, ancient, guardian, harmony));
}

void InventoryGrid::RemoveItem(ITEM *item)
{
    const auto found = std::find(items_.begin(), items_.end(), item);
    if (found == items_.end())
        return;
    FillItemCells(*item, nullptr);
    items_.erase(found);
    store_.DeleteItem(item);
}

bool InventoryGrid::RemoveItemAt(int slot)
{
    auto *item = FindItem(slot);
    if (!item)
        return false;
    RemoveItem(item);
    return true;
}

void InventoryGrid::RemoveAllItems()
{
    std::fill(cells_.begin(), cells_.end(), nullptr);
    for (auto *item : items_)
        store_.DeleteItem(item);
    items_.clear();
}

bool InventoryGrid::CanPreviewDrop(ITEM *pPickItem, ITEM *pTargetItem)
{
    bool bSuccess = false;
    const int iType = pTargetItem->Type;
    const int iDurability = pTargetItem->Durability;

    if ((pPickItem->Type == ITEM_JEWEL_OF_BLESS) || (pPickItem->Type == ITEM_JEWEL_OF_SOUL))
    {
        bSuccess = CanUpgradeItem(pPickItem, pTargetItem);
    }
    else if (pPickItem->Type == ITEM_JEWEL_OF_HARMONY)
    {
        if (pTargetItem->Jewel_Of_Harmony_Option == 0)
        {
            const StrengthenItem strengthitem =
                g_pUIJewelHarmonyinfo->GetItemType(static_cast<int>(pTargetItem->Type));

            if ((strengthitem != SI_None) && (!g_SocketItemMgr.IsSocketItem(pTargetItem)) &&
                (pTargetItem->AncientDiscriminator > 0))
            {
                bSuccess = true;
            }
        }
    }
    else if (pPickItem->Type == ITEM_LOWER_REFINE_STONE ||
             pPickItem->Type == ITEM_HIGHER_REFINE_STONE)
    {
        if (pTargetItem->Jewel_Of_Harmony_Option != 0)
        {
            bSuccess = true;
        }
    }

    if (pPickItem->Type == ITEM_JEWEL_OF_BLESS && iType == ITEM_HORN_OF_FENRIR &&
        iDurability != 255)
    {
        bSuccess = true;
    }

    if (bSuccess == false && role_ == InventoryRole::Player)
    {
        bSuccess = AreItemsStackable(pPickItem, pTargetItem);
    }
    if (Check_LuckyItem(pTargetItem->Type))
    {
        bSuccess = false;
        if (pPickItem->Type == ITEM_POTION + 161)
        {
            if (pTargetItem->Jewel_Of_Harmony_Option == 0)
                bSuccess = true;
        }
        else if (pPickItem->Type == ITEM_POTION + 160)
        {
            if (pTargetItem->Durability > 0)
                bSuccess = true;
        }
    }
    return bSuccess;
}

bool InventoryGrid::AreItemsStackable(ITEM *pSourceItem, ITEM *pTargetItem)
{
    if (pSourceItem == nullptr || pTargetItem == nullptr)
    {
        return false;
    }

    const int iSrcType = pSourceItem->Type;
    const int iTarType = pTargetItem->Type;
    const int iSrcLevel = pSourceItem->Level;
    const int iTarLevel = pTargetItem->Level;
    const int iSrcDurability = pSourceItem->Durability;
    const int iTarDurability = pTargetItem->Durability;

    if (iSrcType != iTarType)
    {
        return false;
    }

    if (iSrcType == ITEM_SIEGE_POTION && iTarType == ITEM_SIEGE_POTION &&
        (iSrcDurability < 250 && iTarDurability < 250))
    {
        return true;
    }

    if ((iSrcType >= ITEM_POTION && iSrcType <= ITEM_ANTIDOTE && iSrcType != ITEM_SIEGE_POTION) &&
        (iTarType >= ITEM_POTION && iTarType <= ITEM_ANTIDOTE && iTarType != ITEM_SIEGE_POTION) &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if ((iSrcType >= ITEM_SMALL_COMPLEX_POTION && iSrcType <= ITEM_LARGE_COMPLEX_POTION) &&
        (iTarType >= ITEM_SMALL_COMPLEX_POTION && iTarType <= ITEM_LARGE_COMPLEX_POTION) &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if ((iSrcType == ITEM_BOLT && iTarType == ITEM_BOLT) && (iSrcLevel == iTarLevel))
    {
        return true;
    }

    if ((iSrcType == ITEM_ARROWS && iTarType == ITEM_ARROWS) && (iSrcLevel == iTarLevel))
    {
        return true;
    }

    if (iSrcType == ITEM_SYMBOL_OF_KUNDUN && iTarType == ITEM_SYMBOL_OF_KUNDUN)
    {
        return true;
    }

    if ((iSrcType >= ITEM_SPLINTER_OF_ARMOR && iSrcType <= ITEM_CLAW_OF_BEAST) &&
        (iTarType >= ITEM_SPLINTER_OF_ARMOR && iTarType <= ITEM_CLAW_OF_BEAST))
    {
        return true;
    }

    if ((iSrcType >= ITEM_JACK_OLANTERN_BLESSINGS && iSrcType <= ITEM_JACK_OLANTERN_DRINK) &&
        (iTarType >= ITEM_JACK_OLANTERN_BLESSINGS && iTarType <= ITEM_JACK_OLANTERN_DRINK) &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 70 && iTarType == ITEM_POTION + 70 &&
        (iSrcDurability < 50 && iTarDurability < 50))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 71 && iTarType == ITEM_POTION + 71 &&
        (iSrcDurability < 50 && iTarDurability < 50))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 78 && iTarType == ITEM_POTION + 78 &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 79 && iTarType == ITEM_POTION + 79 &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 80 && iTarType == ITEM_POTION + 80 &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 81 && iTarType == ITEM_POTION + 81 &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 82 && iTarType == ITEM_POTION + 82 &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 94 && iTarType == ITEM_POTION + 94 &&
        (iSrcDurability < 50 && iTarDurability < 50))
    {
        return true;
    }

    if (iSrcType == ITEM_CHERRY_BLOSSOM_WINE && iTarType == ITEM_CHERRY_BLOSSOM_WINE &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_CHERRY_BLOSSOM_RICE_CAKE && iTarType == ITEM_CHERRY_BLOSSOM_RICE_CAKE &&
        (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_CHERRY_BLOSSOM_FLOWER_PETAL &&
        iTarType == ITEM_CHERRY_BLOSSOM_FLOWER_PETAL && (iSrcDurability < 3 && iTarDurability < 3))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 88 && iTarType == ITEM_POTION + 88 &&
        (iSrcDurability < 10 && iTarDurability < 10))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 89 && iTarType == ITEM_POTION + 89 &&
        (iSrcDurability < 30 && iTarDurability < 30))
    {
        return true;
    }

    if (iSrcType == ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH &&
        iTarType == ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH &&
        (iSrcDurability < 50 && iTarDurability < 50))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 100 && (iSrcDurability < 255 && iTarDurability < 255))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 110 && iTarType == ITEM_POTION + 110)
    {
        return true;
    }

    if (iSrcType == ITEM_SUSPICIOUS_SCRAP_OF_PAPER && iTarType == ITEM_SUSPICIOUS_SCRAP_OF_PAPER &&
        (iSrcDurability < 5 && iTarDurability < 5))
    {
        return true;
    }

    if (iSrcType == ITEM_POTION + 133 && (iSrcDurability < 50 && iTarDurability < 50))
    {
        return true;
    }

    return false;
}

bool InventoryGrid::CanUpgradeItem(ITEM *pSourceItem, ITEM *pTargetItem)
{
    const int iTargetLevel = pTargetItem->Level;

    if (((pTargetItem->Type >= ITEM_SWORD && pTargetItem->Type < ITEM_WING) &&
         (pTargetItem->Type != ITEM_BOLT) && (pTargetItem->Type != ITEM_ARROWS)) ||
        (pTargetItem->Type >= ITEM_WING && pTargetItem->Type <= ITEM_WINGS_OF_DARKNESS) ||
        (pTargetItem->Type >= ITEM_WING_OF_STORM && pTargetItem->Type <= ITEM_WING_OF_DIMENSION))
    {
        if ((pSourceItem->Type == ITEM_JEWEL_OF_BLESS) && (iTargetLevel >= 0 && iTargetLevel <= 5))
        {
            return true;
        }

        if ((pSourceItem->Type == ITEM_JEWEL_OF_SOUL) && (iTargetLevel >= 0 && iTargetLevel <= 8))
        {
            return true;
        }
    }

    return false;
}

bool SessionGameDataUnit::BeginItemMove(InventoryGrid *source, ITEM *item)
{
    auto &picked = sessionKeeper_.InventoryStorage().picked;
    if (!item || picked.item)
        return false;
    picked.item = items_->DuplicateItem(item);
    picked.source = source;
    picked.sourceSlot =
        source ? source->GetIndexByItem(item) : (item->ex_src_type > 0 ? item->lineal_pos : -1);
    picked.storage = source
                         ? source->GetStorageType()
                         : (item->ex_src_type == ITEM_EX_SRC_EQUIPMENT ? STORAGE_TYPE::INVENTORY
                                                                       : STORAGE_TYPE::UNDEFINED);
    if (picked.storage == STORAGE_TYPE::CHAOS_MIX)
        picked.storage = sessionKeeper_.MixRecipeManager().GetMixInventoryEquipmentIndex();
    return true;
}

PickedInventoryItem *SessionGameDataUnit::GetPickedItem() const noexcept
{
    auto &picked = sessionKeeper_.InventoryStorage().picked;
    return picked.item ? &picked : nullptr;
}

void SessionGameDataUnit::ClearPickedItem()
{
    auto &picked = sessionKeeper_.InventoryStorage().picked;
    items_->DeleteItem(picked.item);
    picked = {};
}

void SessionGameplayUnit::RequestInventoryRefresh()
{
    auto &last = sessionKeeper_.InterfaceStorage().lastInventoryRefreshRequestTick;
    constexpr DWORD interval = 1000;
    const DWORD now = GetTickCount();
    if (now - last < interval)
        return;
    last = now;
    if (SocketClient && SocketClient->ToGameServer())
        SocketClient->ToGameServer()->SendInventoryRequest();
}

bool SessionGameplayUnit::RestorePickedItem()
{
    auto &data = *sessionKeeper_.GameData();
    auto *picked = data.GetPickedItem();
    if (!picked || EquipmentItem)
        return false;
    auto *item = picked->item;
    if (picked->source)
    {
        if (!picked->source->AddItem(item->x, item->y, item))
        {
            RequestInventoryRefresh();
            return false;
        }
    }
    else if (item->ex_src_type == ITEM_EX_SRC_EQUIPMENT)
    {
        auto *equipment = &CharacterMachine->Equipment[picked->sourceSlot];
        memcpy(equipment, item, sizeof(ITEM));
        CreateEquippingEffect(equipment);
        if (equipment->Type == ITEM_DARK_RAVEN_ITEM && !gMapManager.InChaosCastle())
        {
            CreatePetDarkSpirit_Now(Hero);
            if (auto *pet = ResolvePetSystem(Hero))
                pet->SetPetInfo(GetPetInfo(equipment));
        }
    }
    else
        return false;
    data.ClearPickedItem();
    return true;
}

void SessionGameDataUnit::ClearPlayerInventory()
{
    ClearPickedItem();
    for (auto role :
         {InventoryRole::Player, InventoryRole::PlayerExtension0, InventoryRole::PlayerExtension1,
          InventoryRole::PlayerExtension2, InventoryRole::PlayerExtension3, InventoryRole::MyShop})
        Inventory(role).RemoveAllItems();
}

bool SessionGameDataUnit::InsertInventoryItem(int slot, std::span<const BYTE> packet)
{
    auto *grid =
        IsMyShopSlot(slot) ? &Inventory(InventoryRole::MyShop) : PlayerInventoryForSlot(slot);
    return grid && grid->AddItem(slot, packet);
}

void SessionGameDataUnit::DeleteInventoryItem(int slot)
{
    auto *grid =
        IsMyShopSlot(slot) ? &Inventory(InventoryRole::MyShop) : PlayerInventoryForSlot(slot);
    if (!grid || grid->RemoveItemAt(slot))
        return;
    auto *picked = GetPickedItem();
    if (picked && picked->source == grid && picked->sourceSlot == slot)
        ClearPickedItem();
}

bool SessionGameDataUnit::HasInventoryItem(short type, bool includePicked) const
{
    const auto *picked = GetPickedItem();
    if (includePicked && picked && picked->item->Type == type)
        return true;
    return sessionKeeper_.InventoryStorage()
        .grids[static_cast<size_t>(InventoryRole::Player)]
        ->IsItem(type);
}

void SessionGameDataUnit::TrackItemMove(STORAGE_TYPE storage, int sourceSlot)
{
    auto &state = sessionKeeper_.InventoryStorage();
    state.pendingMoveSlot = sourceSlot;
    state.pendingMoveSource = nullptr;
    if (GetPickedItem())
        return;
    switch (storage)
    {
    case STORAGE_TYPE::INVENTORY:
        state.pendingMoveSource = PlayerInventoryForSlot(sourceSlot);
        break;
    case STORAGE_TYPE::MYSHOP:
        state.pendingMoveSource = &Inventory(InventoryRole::MyShop);
        break;
    case STORAGE_TYPE::VAULT:
        state.pendingMoveSource = &Inventory(
            sourceSlot < MAX_SHOP_INVENTORY ? InventoryRole::Vault : InventoryRole::VaultExtension);
        break;
    default:
        break;
    }
}

void SessionGameDataUnit::CompleteItemMove(bool success)
{
    auto &state = sessionKeeper_.InventoryStorage();
    if (success)
    {
        if (state.pendingMoveSource)
            state.pendingMoveSource->RemoveItemAt(state.pendingMoveSlot);
        ClearPickedItem();
    }
    state.pendingMoveSource = nullptr;
    state.pendingMoveSlot = -1;
}

bool SessionGameDataUnit::CompleteLuckyMix(BYTE result, std::span<const BYTE> packet)
{
    auto &request = sessionKeeper_.InventoryStorage().luckyMixRequest;
    if (request == 0)
        return false;
    constexpr int refineryRequest = 52;
    auto &grid = Inventory(InventoryRole::LuckyCrafting);
    ClearPickedItem();
    if (result == 1 || request == refineryRequest)
        grid.RemoveAllItems();
    if (result == 1)
        grid.AddItem(0, packet);
    request = 0;
    return true;
}

void SessionGameplayUnit::ClearInventoryState()
{
    auto &data = *sessionKeeper_.GameData();
    data.ClearPickedItem();
    data.ClearInventoryContainers();
    EquipmentItem = false;
    // World retirement releases character attachments. Do not recalculate stats
    // or issue normal equipment actions while their world is being retired.
    if (CharacterMachine)
        for (auto &item : CharacterMachine->Equipment)
        {
            item = {};
            item.Type = -1;
            item.Number = -1;
        }
}

InventoryGrid *SessionGameDataUnit::PlayerInventoryForSlot(int slot) const noexcept
{
    if (!IsPlayerInventorySlot(slot))
        return nullptr;
    const auto role =
        IsMainInventorySlot(slot)
            ? InventoryRole::Player
            : static_cast<InventoryRole>(static_cast<int>(InventoryRole::PlayerExtension0) +
                                         (slot - MAX_MY_INVENTORY_INDEX) / MAX_INVENTORY_EXT_ONE);
    return sessionKeeper_.InventoryStorage().grids[static_cast<std::size_t>(role)].get();
}

ITEM *SessionGameDataUnit::FindInventoryItemBySlot(int slot) const
{
    const auto *grid = PlayerInventoryForSlot(slot);
    return grid ? grid->FindItem(slot) : nullptr;
}

int SessionGameDataUnit::FindManaItemIndex() const
{
    const auto &grid =
        *sessionKeeper_.InventoryStorage().grids[static_cast<std::size_t>(InventoryRole::Player)];
    for (int type = ITEM_LARGE_MANA_POTION; type >= ITEM_SMALL_MANA_POTION; --type)
        if (const int slot = grid.FindItemReverseIndex(type, -1); slot >= 0)
            return slot;
    return -1;
}

int SessionGameDataUnit::FindHealingItemIndex() const
{
    const auto &grid =
        *sessionKeeper_.InventoryStorage().grids[static_cast<std::size_t>(InventoryRole::Player)];
    for (int type = ITEM_LARGE_HEALING_POTION; type >= ITEM_APPLE; --type)
        if (const int slot = grid.FindItemReverseIndex(type, -1); slot >= 0)
            return slot;
    return -1;
}

const ITEM *SessionLegacyCalls::FindInventoryItemBySlot(int slot) const
{
    return sessionKeeper_.GameData()->FindInventoryItemBySlot(slot);
}

std::size_t GameLogic::Items::ItemPacketLength(std::span<const BYTE> packet)
{
    constexpr std::size_t baseSize = 5;
    if (packet.size() < baseSize)
        return 0;
    const auto flags = static_cast<ItemOptionFlags>(packet[4]);
    std::size_t size = baseSize;
    for (auto flag : {ItemOptionFlags::HasOption, ItemOptionFlags::HasExcellent,
                      ItemOptionFlags::HasAncient, ItemOptionFlags::HasHarmony})
        if (flags & flag)
            ++size;
    if (flags & ItemOptionFlags::HasSockets)
    {
        if (size >= packet.size())
            return 0;
        const auto count = packet[size] & 0xf;
        if (count > MAX_SOCKETS)
            return 0;
        size += 1 + count;
    }
    return size <= packet.size() ? size : 0;
}

std::optional<ItemCreationParams> GameLogic::Items::ParseItemData(std::span<const BYTE> itemData)
{
    ItemCreationParams params = {};

    if (ItemPacketLength(itemData) == 0)
        return std::nullopt;

    params.Group = (itemData[0] >> 4) & 0xF;
    params.Number = ((itemData[0] & 0xF) << 8) + itemData[1];
    params.Level = itemData[2];
    params.Durability = itemData[3];
    auto flags = static_cast<ItemOptionFlags>(itemData[4]);
    params.WithLuck = flags & ItemOptionFlags::HasLuck;
    params.WithSkill = flags & ItemOptionFlags::HasSkill;

    int offset = 0;
    if (flags & ItemOptionFlags::HasOption)
    {
        params.OptionLevel = itemData[5] & 0xF;
        params.OptionType = (itemData[5] >> 4) & 0xF;
        offset++;
    }

    if (flags & ItemOptionFlags::HasExcellent)
    {
        params.ExcellentFlags = itemData[5 + offset];
        offset++;
    }

    if (flags & ItemOptionFlags::HasAncient)
    {
        params.AncientDiscriminator = itemData[5 + offset] & 0xF;
        params.AncientBonusOption = (itemData[5 + offset] >> 4) & 0xF;
        offset++;
    }

    if (flags & ItemOptionFlags::HasHarmony)
    {
        params.HasHarmonyOption = true;
        params.HarmonyOptionLevel = itemData[5 + offset] & 0xF;
        params.HarmonyOptionType = (itemData[5 + offset] >> 4) & 0xF;
        offset++;
    }

    if (flags & ItemOptionFlags::HasSockets)
    {
        params.SocketBonusOption = (itemData[5 + offset] >> 4) & 0xF;
        params.SocketCount = itemData[5 + offset] & 0xF;

        for (int i = 0; i < params.SocketCount; ++i)
        {
            params.SocketOptions[i] = itemData[6 + offset + i];
        }
    }

    return params;
}

using namespace SEASON3A;

void CMixItem::Reset()
{
    m_sType = 0;
    m_iLevel = 0;
    m_iOption = 0;
    m_iDurability = 0;
    m_dwSpecialItem = 0;
    m_b380AddedItem = FALSE;
    m_bFenrirAddedItem = FALSE;
    m_bIsCharmItem = FALSE;
    m_bIsChaosCharmItem = FALSE;
    m_bIsJewelItem = FALSE;
    m_wHarmonyOption = 0;
    m_wHarmonyOptionLevel = 0;
    m_bMixLuck = FALSE;
    m_bIsEquipment = FALSE;
    m_bIsWing = FALSE;
    m_bIsUpgradedWing = FALSE;
    m_bIs3rdUpgradedWing = FALSE;
    m_bySocketCount = 0;
    for (int i = 0; i < MAX_SOCKETS; ++i)
    {
        m_bySocketSeedID[i] = SOCKET_EMPTY;
        m_bySocketSphereLv[i] = 0;
    }
    m_bCanStack = FALSE;
    m_dwMixValue = 0;
    m_iCount = 0;
    m_iTestCount = 0;
}

void CMixItem::SetItem(ITEM *pItem, DWORD dwMixValue)
{
    Reset();

    m_sType = pItem->Type;
    m_iLevel = pItem->Level;

    m_iDurability = pItem->Durability;
    for (int i = 0; i < pItem->SpecialNum; i++)
    {
        switch (pItem->Special[i])
        {
        case AT_IMPROVE_MAGIC:
        case AT_IMPROVE_CURSE:
        case AT_IMPROVE_DAMAGE:
        case AT_IMPROVE_DEFENSE:
        case AT_IMPROVE_BLOCKING:
            m_iOption = pItem->SpecialValue[i];
            break;
        case AT_LIFE_REGENERATION:
            m_iOption = pItem->SpecialValue[i] * 4;
            break;
        case AT_LUCK:
            m_bMixLuck = TRUE;
            break;
        }
    }
    if (pItem->ExcellentFlags > 0)
        m_dwSpecialItem |= RCP_SP_EXCELLENT;
    if (pItem->RequireLevel >= 380)
        m_dwSpecialItem |= RCP_SP_ADD380ITEM;
    if (pItem->AncientDiscriminator > 0)
        m_dwSpecialItem |= RCP_SP_SETITEM;
    m_b380AddedItem = pItem->option_380;

    if (pItem->Type >= ITEM_SWORD && pItem->Type <= ITEM_BOOTS + MAX_ITEM_INDEX - 1)
        m_bIsEquipment = TRUE;

    if (pItem->Type == ITEM_HORN_OF_FENRIR && pItem->ExcellentFlags != 0)
        m_bFenrirAddedItem = TRUE;

    if (pItem->Type == ITEM_POTION + 53)
        m_bIsCharmItem = TRUE;

    if (pItem->Type == ITEM_POTION + 96)
        m_bIsChaosCharmItem = TRUE;

    if (pItem->Type == ITEM_JEWEL_OF_CHAOS || pItem->Type == ITEM_PACKED_JEWEL_OF_BLESS ||
        pItem->Type == ITEM_PACKED_JEWEL_OF_SOUL || pItem->Type == ITEM_JEWEL_OF_BLESS ||
        pItem->Type == ITEM_JEWEL_OF_SOUL || pItem->Type == ITEM_JEWEL_OF_LIFE ||
        pItem->Type == ITEM_JEWEL_OF_CREATION || pItem->Type == ITEM_JEWEL_OF_GUARDIAN ||
        pItem->Type == ITEM_JEWEL_OF_HARMONY)
        m_bIsJewelItem = TRUE;

    m_bySocketCount = pItem->SocketCount;
    if (m_bySocketCount > 0)
    {
        m_dwSpecialItem |= RCP_SP_SOCKETITEM;
        for (int i = 0; i < MAX_SOCKETS; ++i)
        {
            m_bySocketSeedID[i] = pItem->SocketSeedID[i];
            m_bySocketSphereLv[i] = pItem->SocketSphereLv[i];
        }
        m_dwSpecialItem ^= RCP_SP_ADD380ITEM;
    }
    m_bySeedSphereID = SEASON4A::CSocketItemMgr::GetSeedShpereSeedID(pItem);

    if (pItem->Jewel_Of_Harmony_Option > 0)
    {
        m_dwSpecialItem |= RCP_SP_HARMONY;
        m_wHarmonyOption = pItem->Jewel_Of_Harmony_Option;
        m_wHarmonyOptionLevel = pItem->Jewel_Of_Harmony_OptionLevel;
    }

    switch (pItem->Type)
    {
    case ITEM_WING:
    case ITEM_WINGS_OF_HEAVEN:
    case ITEM_WINGS_OF_SATAN:
    case ITEM_WING_OF_CURSE:
        m_bIsWing = TRUE;
        break;
    case ITEM_WINGS_OF_SPIRITS:
    case ITEM_WINGS_OF_SOUL:
    case ITEM_WINGS_OF_DRAGON:
    case ITEM_WINGS_OF_DARKNESS:
    case ITEM_CAPE_OF_LORD:
    case ITEM_WINGS_OF_DESPAIR:
    case ITEM_CAPE_OF_FIGHTER:
        m_bIsUpgradedWing = TRUE;
        break;
    case ITEM_WING_OF_STORM:
    case ITEM_WING_OF_ETERNAL:
    case ITEM_WING_OF_ILLUSION:
    case ITEM_WING_OF_RUIN:
    case ITEM_CAPE_OF_EMPEROR:
    case ITEM_WING_OF_DIMENSION:
    case ITEM_CAPE_OF_OVERRULE:
        m_bIs3rdUpgradedWing = TRUE;
    }

    if (m_bIsWing || m_bIsUpgradedWing || m_bIs3rdUpgradedWing)
    {
        if (m_dwSpecialItem & RCP_SP_EXCELLENT)
            m_dwSpecialItem ^= RCP_SP_EXCELLENT;
    }
    switch (pItem->Type)
    {
    case ITEM_LARGE_HEALING_POTION:
    case ITEM_SMALL_COMPLEX_POTION:
    case ITEM_MEDIUM_COMPLEX_POTION:
    case ITEM_POTION + 53:
    case ITEM_POTION + 88:
    case ITEM_POTION + 89:
    case ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH:
    case ITEM_POTION + 100:
        m_bCanStack = TRUE;
        break;
    }
    m_dwMixValue = dwMixValue;

    if (m_bCanStack == TRUE)
        m_iCount = m_iDurability;
    else
        m_iCount = 1;
}

int CMixItemInventory::AddItem(ITEM *pItem)
{
    BOOL bFind = FALSE;

    for (int i = 0; i < m_iNumMixItems; ++i)
    {
        if (m_MixItems[i] == pItem)
        {
            bFind = TRUE;
            m_MixItems[i].m_dwMixValue += EvaluateMixItemValue(pItem);
            if (m_MixItems[i].m_bCanStack == TRUE)
            {
                m_MixItems[i].m_iCount += pItem->Durability;
            }
            else
            {
                ++m_MixItems[i].m_iCount;
            }
            break;
        }
    }
    if (bFind == FALSE)
    {
        m_MixItems[m_iNumMixItems++].SetItem(pItem, EvaluateMixItemValue(pItem));
    }

    return 0;
}

DWORD CMixItemInventory::EvaluateMixItemValue(ITEM *pItem)
{
    DWORD dwMixValue = 0;
    switch (pItem->Type)
    {
    case ITEM_JEWEL_OF_CHAOS:
        dwMixValue = 40000;
        break;
    case ITEM_JEWEL_OF_BLESS:
        dwMixValue = 100000;
        break;
    case ITEM_JEWEL_OF_SOUL:
        dwMixValue = 70000;
        break;
    case ITEM_JEWEL_OF_CREATION:
        dwMixValue = 450000;
        break;
    case ITEM_JEWEL_OF_LIFE:
        dwMixValue = 0;
        break;
    case ITEM_JEWEL_OF_GUARDIAN:
    default:
        dwMixValue = ItemValue(pItem, 0);
        break;
    }
    return dwMixValue;
}

void CMixRecipes::Reset()
{
    ClearCheckRecipeResult();
    std::vector<MIX_RECIPE *>::iterator iter;
    for (iter = m_Recipes.begin(); iter != m_Recipes.end(); ++iter)
    {
        if (*iter != NULL)
        {
            delete *iter;
            *iter = NULL;
        }
    }
    m_Recipes.clear();
}

void CMixRecipes::AddRecipe(MIX_RECIPE *pMixRecipe)
{
    if (pMixRecipe != NULL)
        m_Recipes.push_back(pMixRecipe);
}

BOOL CMixRecipes::IsMixSource(ITEM *pItem)
{
    CMixItem mixitem;
    mixitem.SetItem(pItem, 0);

    if (Check_LuckyItem(pItem->Type) && owner_->GetMixInventoryType() != MIXTYPE_JERRIDON)
        return FALSE;

    if (IsCharmItem(mixitem))
    {
        if ((GetCurRecipe() == NULL || GetCurRecipe()->m_bCharmOption == 'A') &&
            m_wTotalCharmBonus + mixitem.m_iCount <= 10)
        {
            return TRUE;
        }
    }

    if (IsChaosCharmItem(mixitem))
    {
        if ((GetCurRecipe() == NULL || GetCurRecipe()->m_bCharmOption == 'A') &&
            m_wTotalChaosCharmCount < 1)
        {
            return TRUE;
        }
    }

    for (std::vector<MIX_RECIPE *>::iterator iter = m_Recipes.begin(); iter != m_Recipes.end();
         ++iter)
    {
        for (int j = 0; j < (*iter)->m_iNumMixSoruces; ++j)
        {
            if (m_wTotalCharmBonus > 0 && (*iter)->m_bCharmOption != 'A')
            {
                continue;
            }
            if (CheckItem((*iter)->m_MixSources[j], mixitem) &&
                !((*iter)->m_bMixOption == 'B' && IsChaosItem(mixitem)) &&
                !((*iter)->m_bMixOption == 'C' && Is380AddedItem(mixitem)) &&
                !((*iter)->m_bMixOption == 'D' && IsFenrirAddedItem(mixitem)) &&
                !((*iter)->m_bMixOption == 'E' && !IsUpgradableItem(mixitem)) &&
                !((*iter)->m_bMixOption == 'G' && !IsSourceOfRefiningStone(mixitem)))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

void CMixRecipes::ClearCheckRecipeResult()
{
    m_iCurMixIndex = 0;
    m_iMostSimilarMixIndex = 0;
    m_iSuccessRate = 0;
    m_dwRequiredZen = 0;
    m_bFindMixLuckItem = FALSE;
    m_dwTotalItemValue = 0;
    m_dwExcellentItemValue = 0;
    m_dwEquipmentItemValue = 0;
    m_dwWingItemValue = 0;
    m_dwSetItemValue = 0;
    m_iFirstItemLevel = 0;
    m_iFirstItemType = 0;
    m_dwTotalNonJewelItemValue = 0;
    m_byFirstItemSocketCount = 0;
    for (int i = 0; i < MAX_SOCKETS; ++i)
    {
        m_byFirstItemSocketSeedID[i] = SOCKET_EMPTY;
        m_byFirstItemSocketSphereLv[i] = 0;
    }
    m_wTotalCharmBonus = 0;
}

int CMixRecipes::CheckRecipe(int iNumMixItems, CMixItem *pMixItems)
{
    m_iCurMixIndex = 0;

    std::vector<MIX_RECIPE *>::iterator iter;
    for (iter = m_Recipes.begin(); iter != m_Recipes.end(); ++iter)
    {
        for (int i = 0; i < iNumMixItems; ++i)
        {
            pMixItems[i].m_iTestCount = pMixItems[i].m_iCount;
        }
        if (CheckRecipeSub(iter, iNumMixItems, pMixItems) == TRUE)
        {
            m_iCurMixIndex = (*iter)->m_iMixIndex + 1;
            EvaluateMixItems(iNumMixItems, pMixItems);
            CalcCharmBonusRate(iNumMixItems, pMixItems);
            CalcMixRate(iNumMixItems, pMixItems);
            CalcMixReqZen(iNumMixItems, pMixItems);
            return GetCurRecipe()->m_iMixID;
        }
        else
        {
            m_iSuccessRate = 0;
            m_dwRequiredZen = 0;
        }
    }
    return (-1);
}

BOOL CMixRecipes::CheckRecipeSub(std::vector<MIX_RECIPE *>::iterator iter, int iNumMixItems,
                                 CMixItem *pMixItems)
{
    BOOL bFind = FALSE;
    int iMixRecipeTest[MAX_MIX_SOURCES];
    memset(iMixRecipeTest, 0, sizeof(int) * MAX_MIX_SOURCES);

    for (int j = 0; j < (*iter)->m_iNumMixSoruces; ++j)
    {
        if (!IsOptionItem((*iter)->m_MixSources[j]))
            bFind = FALSE;
        for (int i = 0; i < iNumMixItems; ++i)
        {
            if (CheckItem((*iter)->m_MixSources[j], pMixItems[i]) &&
                pMixItems[i].m_iTestCount > 0 &&
                (*iter)->m_MixSources[j].m_iCountMax >=
                    iMixRecipeTest[j] + pMixItems[i].m_iTestCount &&
                !((*iter)->m_bMixOption == 'H' &&
                  IsSourceOfAttachSeedSphereToArmor(pMixItems[i])) &&
                !((*iter)->m_bMixOption == 'I' && IsSourceOfAttachSeedSphereToWeapon(pMixItems[i])))
            {
                if (pMixItems[i].m_iTestCount >= (*iter)->m_MixSources[j].m_iCountMax)
                {
                    iMixRecipeTest[j] += (*iter)->m_MixSources[j].m_iCountMax;
                    pMixItems[i].m_iTestCount -= (*iter)->m_MixSources[j].m_iCountMax;
                }
                else
                {
                    iMixRecipeTest[j] += pMixItems[i].m_iTestCount;
                    pMixItems[i].m_iTestCount = 0;
                }
                bFind = TRUE;
                if (j == 0)
                {
                    m_iFirstItemLevel = pMixItems[i].m_iLevel;
                    m_iFirstItemType = pMixItems[i].m_sType;
                    m_byFirstItemSocketCount = pMixItems[i].m_bySocketCount;
                    if (m_byFirstItemSocketCount > 0)
                    {
                        for (int k = 0; k < MAX_SOCKETS; ++k)
                        {
                            m_byFirstItemSocketSeedID[k] = pMixItems[i].m_bySocketSeedID[k];
                            m_byFirstItemSocketSphereLv[k] = pMixItems[i].m_bySocketSphereLv[k];
                        }
                    }
                }
            }
        }
        if (bFind == FALSE || (*iter)->m_MixSources[j].m_iCountMin > iMixRecipeTest[j])
        {
            return FALSE;
        }
    }

    for (int i = 0; i < iNumMixItems; ++i)
    {
        if (pMixItems[i].m_iTestCount > 0)
        {
            if (pMixItems[i].m_bIsCharmItem && (*iter)->m_bCharmOption == 'A')
                ;
            else if (pMixItems[i].m_bIsChaosCharmItem && (*iter)->m_bChaosCharmOption == 'A')
                ;
            else
                return FALSE;
        }
    }
    return TRUE;
}

int CMixRecipes::CheckRecipeSimilarity(int iNumMixItems, CMixItem *pMixItems)
{
    if (iNumMixItems == 0 && m_Recipes.size() == 1)
    {
        m_iMostSimilarMixIndex = 1;
        for (int i = 0; i < (*m_Recipes.begin())->m_iNumMixSoruces; ++i)
        {
            m_iMostSimilarMixSourceTest[i] = (*m_Recipes.begin())->m_MixSources[0].m_iCountMax;
        }
        return m_iMostSimilarMixIndex;
    }

    int iMostSimiliarRecipe = 0;
    int iMostSimiliarityPoint = 0;
    memset(m_iMostSimilarMixSourceTest, 0, sizeof(int) * MAX_MIX_SOURCES);
    memset(m_iMixSourceTest, 0, sizeof(int) * MAX_MIX_SOURCES);

    int iSimilarityPoint;
    for (auto iter = m_Recipes.begin(); iter != m_Recipes.end(); ++iter)
    {
        memset(m_iMixSourceTest, 0, sizeof(int) * MAX_MIX_SOURCES);
        for (int i = 0; i < (*iter)->m_iNumMixSoruces; ++i)
            m_iMixSourceTest[i] = (*iter)->m_MixSources[i].m_iCountMax;

        for (int i = 0; i < iNumMixItems; ++i)
            pMixItems[i].m_iTestCount = pMixItems[i].m_iCount;

        iSimilarityPoint = CheckRecipeSimilaritySub(iter, iNumMixItems, pMixItems);
        if (iSimilarityPoint == 1 && m_Recipes.size() > 1)
            iSimilarityPoint = 0;
        if (iSimilarityPoint > iMostSimiliarityPoint ||
            (iSimilarityPoint == iMostSimiliarityPoint && iSimilarityPoint > 0 &&
             m_iCurMixIndex == (*iter)->m_iMixIndex + 1))
        {
            iMostSimiliarityPoint = iSimilarityPoint;
            iMostSimiliarRecipe = (*iter)->m_iMixIndex + 1;
            memset(m_iMostSimilarMixSourceTest, 0, sizeof(int) * MAX_MIX_SOURCES);
            for (int i = 0; i < (*iter)->m_iNumMixSoruces; ++i)
            {
                m_iMostSimilarMixSourceTest[i] = m_iMixSourceTest[i];
            }
        }
    }
    m_iMostSimilarMixIndex = iMostSimiliarRecipe;
    return iMostSimiliarRecipe;
}

int CMixRecipes::CheckRecipeSimilaritySub(std::vector<MIX_RECIPE *>::iterator iter,
                                          int iNumMixItems, CMixItem *pMixItems)
{
    int iFindTotalPoint = 0;
    int iFindPoint = 0;

    for (int j = 0; j < (*iter)->m_iNumMixSoruces; ++j)
    {
        for (int i = 0; i < iNumMixItems; ++i)
        {
            if (CheckItem((*iter)->m_MixSources[j], pMixItems[i]) &&
                pMixItems[i].m_iTestCount > 0 &&
                !((*iter)->m_bMixOption == 'H' &&
                  IsSourceOfAttachSeedSphereToArmor(pMixItems[i])) &&
                !((*iter)->m_bMixOption == 'I' && IsSourceOfAttachSeedSphereToWeapon(pMixItems[i])))
            {
                if (IsChaosJewel(pMixItems[i]))
                    iFindPoint = 1;
                else if ((*iter)->m_MixSources[j].m_iCountMax < pMixItems[i].m_iTestCount)
                    iFindPoint = 1;
                else if (j == 0)
                    iFindPoint = 10;
                else if (j == 1)
                    iFindPoint = 5;
                else
                    iFindPoint = 3;

                iFindTotalPoint += iFindPoint;

                if (pMixItems[i].m_iTestCount > 0 && m_iMixSourceTest[j] > 0)
                {
                    m_iMixSourceTest[j] -= pMixItems[i].m_iTestCount;
                    pMixItems[i].m_iTestCount -= (*iter)->m_MixSources[j].m_iCountMax;
                }
            }
        }
    }
    for (int i = 0; i < iNumMixItems; ++i)
    {
        if (pMixItems[i].m_iTestCount > 0)
        {
            if (pMixItems[i].m_bIsCharmItem && (*iter)->m_bCharmOption == 'A')
                ;
            else if (pMixItems[i].m_bIsChaosCharmItem && (*iter)->m_bChaosCharmOption == 'A')
                ;
            else
                return 0;
        }
    }
    return iFindTotalPoint;
}

bool CMixRecipes::CheckItem(MIX_RECIPE_ITEM &rItem, CMixItem &rSource)
{
    if (rItem.m_sTypeMin <= rSource.m_sType && rItem.m_sTypeMax >= rSource.m_sType &&
        rItem.m_iLevelMin <= rSource.m_iLevel && rItem.m_iLevelMax >= rSource.m_iLevel &&
        rItem.m_iDurabilityMin <= rSource.m_iDurability &&
        rItem.m_iDurabilityMax >= rSource.m_iDurability &&
        rItem.m_iOptionMin <= rSource.m_iOption && rItem.m_iOptionMax >= rSource.m_iOption &&
        (rItem.m_dwSpecialItem & RCP_SP_EXCELLENT) <=
            (rSource.m_dwSpecialItem & RCP_SP_EXCELLENT) &&
        (rItem.m_dwSpecialItem & RCP_SP_ADD380ITEM) <=
            (rSource.m_dwSpecialItem & RCP_SP_ADD380ITEM) &&
        (rItem.m_dwSpecialItem & RCP_SP_SETITEM) <= (rSource.m_dwSpecialItem & RCP_SP_SETITEM) &&
        (rItem.m_dwSpecialItem & RCP_SP_HARMONY) <= (rSource.m_dwSpecialItem & RCP_SP_HARMONY) &&
        (rItem.m_dwSpecialItem & RCP_SP_SOCKETITEM) <=
            (rSource.m_dwSpecialItem & RCP_SP_SOCKETITEM))
    {
        return true;
    }
    return false;
}

MIX_RECIPE *CMixRecipes::GetCurRecipe()
{
    if (m_iCurMixIndex == 0)
        return NULL;
    return m_Recipes[m_iCurMixIndex - 1];
}

MIX_RECIPE *CMixRecipes::GetMostSimilarRecipe()
{
    if (m_iMostSimilarMixIndex == 0)
        return NULL;
    return m_Recipes[m_iMostSimilarMixIndex - 1];
}

int CMixRecipes::GetCurMixID()
{
    if (m_iCurMixIndex == 0)
        return 0;
    return m_Recipes[m_iCurMixIndex - 1]->m_iMixID;
}

BOOL CMixRecipes::GetCurRecipeName(wchar_t *pszNameOut, int iNameLine)
{
    if (!IsReadyToMix())
    {
        if (iNameLine == 1)
        {
            switch (owner_->GetMixInventoryType())
            {
            case MIXTYPE_TRAINER:
                mu_swprintf(pszNameOut, I18N::Game::ItemInappropriateForS,
                            I18N::Game::Resurrection);
                break;
            case MIXTYPE_OSBOURNE:
                mu_swprintf(pszNameOut, I18N::Game::ItemInappropriateForS, I18N::Game::Refine);
                break;
            case MIXTYPE_JERRIDON:
                mu_swprintf(pszNameOut, I18N::Game::ItemInappropriateForS, I18N::Game::Restore);
                break;
            case MIXTYPE_ELPIS:
                mu_swprintf(pszNameOut, I18N::Game::ItemInappropriateForS, I18N::Game::Refine);
                break;
            default:
                mu_swprintf(pszNameOut, L"%ls", I18N::Game::ImproperItemsForCombination);
                break;
            }
            return TRUE;
        }
        else
            return FALSE;
    }
    return GetRecipeName(GetCurRecipe(), pszNameOut, iNameLine, FALSE);
}

BOOL CMixRecipes::GetRecipeName(MIX_RECIPE *pRecipe, wchar_t *pszNameOut, int iNameLine,
                                BOOL bSimilarRecipe)
{
    if (pRecipe == NULL)
        return FALSE;
    if (iNameLine > 2 || iNameLine < 1)
        return FALSE;
    if (pRecipe->m_bMixOption == 'C')
    {
        std::vector<std::wstring> optionTextlist;

        owner_->ItemAddOptionInfoObject()->GetItemAddOtioninfoText(optionTextlist,
                                                                   m_iFirstItemType);
        if (optionTextlist.empty() || bSimilarRecipe)
        {
            if (iNameLine == 1)
            {
                mu_swprintf(pszNameOut, L"%ls", I18N::Game::Add380ItemOption);
                return TRUE;
            }
            return FALSE;
        }
        assert(optionTextlist.size() == 2 && L"옵션은 2개여야 함");
        if (iNameLine == 1)
        {
            wcscpy(pszNameOut, optionTextlist[0].c_str());
            return TRUE;
        }
        else if (iNameLine == 2)
        {
            wcscpy(pszNameOut, optionTextlist[1].c_str());
            return TRUE;
        }
        return FALSE;
    }
    else
    {
        if (iNameLine == 1)
        {
            if (pRecipe->m_iMixName[1] == 0)
                mu_swprintf(pszNameOut, L"%ls", I18N::Game::Lookup(pRecipe->m_iMixName[0]));
            else if (pRecipe->m_iMixName[2] == 0)
                mu_swprintf(pszNameOut, L"%ls %ls", I18N::Game::Lookup(pRecipe->m_iMixName[0]),
                            I18N::Game::Lookup(pRecipe->m_iMixName[1]));
            else
                mu_swprintf(pszNameOut, L"%ls %ls %ls", I18N::Game::Lookup(pRecipe->m_iMixName[0]),
                            I18N::Game::Lookup(pRecipe->m_iMixName[1]),
                            I18N::Game::Lookup(pRecipe->m_iMixName[2]));
            return TRUE;
        }
        return FALSE;
    }
}

BOOL CMixRecipes::GetCurRecipeDesc(wchar_t *pszDescOut, int iDescLine)
{
    if (iDescLine > 3 || iDescLine < 1)
        return FALSE;
    if (GetCurRecipe() == NULL)
        return FALSE;
    if (GetCurRecipe()->m_iMixDesc[iDescLine - 1] > 0)
        wcscpy(pszDescOut, I18N::Game::Lookup(GetCurRecipe()->m_iMixDesc[iDescLine - 1]));
    else
        return FALSE;
    return TRUE;
}

BOOL CMixRecipes::GetMostSimilarRecipeName(wchar_t *pszNameOut, int iNameLine)
{
    return GetRecipeName(GetMostSimilarRecipe(), pszNameOut, iNameLine, TRUE);
}

BOOL CMixRecipes::GetRecipeAdvice(wchar_t *pszAdviceOut, int iAdivceLine)
{
    if (GetMostSimilarRecipe() == NULL)
        return FALSE;
    if (iAdivceLine > 3 || iAdivceLine < 1)
        return FALSE;

    if (GetMostSimilarRecipe()->m_iMixAdvice[iAdivceLine - 1] > 0)
        wcscpy(pszAdviceOut,
               I18N::Game::Lookup(GetMostSimilarRecipe()->m_iMixAdvice[iAdivceLine - 1]));
    else
        return FALSE;
    return TRUE;
}

int CMixRecipes::GetSourceName(int iItemNum, wchar_t *pszNameOut, int iNumMixItems,
                               CMixItem *pMixItems)
{
    return owner_->GetMixSourceName(*this, iItemNum, pszNameOut, iNumMixItems, pMixItems);
}

int CMixRecipeMgr::GetMixSourceName(CMixRecipes &recipes, int iItemNum, wchar_t *pszNameOut,
                                    int iNumMixItems, CMixItem *pMixItems) const
{
    if (iNumMixItems < 0)
        return MIX_SOURCE_ERROR;
    if (pMixItems == NULL)
        return MIX_SOURCE_ERROR;

    if (recipes.GetMostSimilarRecipe() == NULL)
        return MIX_SOURCE_ERROR;
    if (iItemNum >= recipes.GetMostSimilarRecipe()->m_iNumMixSoruces)
        return MIX_SOURCE_ERROR;

    MIX_RECIPE_ITEM *pMixRecipeItem = &recipes.GetMostSimilarRecipe()->m_MixSources[iItemNum];

    wchar_t szTempName[100];
    GetItemName(pMixRecipeItem->m_sTypeMin, pMixRecipeItem->m_iLevelMin, szTempName);

    if (pMixRecipeItem->m_sTypeMin == pMixRecipeItem->m_sTypeMax &&
        (pMixRecipeItem->m_iLevelMin == pMixRecipeItem->m_iLevelMax ||
         (pMixRecipeItem->m_iLevelMin == 0 && pMixRecipeItem->m_iLevelMax == 255)) &&
        (pMixRecipeItem->m_iOptionMin == pMixRecipeItem->m_iOptionMax ||
         (pMixRecipeItem->m_iOptionMin == 0 && pMixRecipeItem->m_iOptionMax == 255)))
    {
        if (pMixRecipeItem->m_iDurabilityMin == pMixRecipeItem->m_iDurabilityMax)
            mu_swprintf(szTempName, L"%ls(%d)", szTempName, pMixRecipeItem->m_iDurabilityMin);
    }
    else
    {
        if (pMixRecipeItem->m_dwSpecialItem & RCP_SP_ADD380ITEM)
            mu_swprintf(szTempName, I18N::Game::_380LevelItem);
        else if (pMixRecipeItem->m_sTypeMin == 0 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_BOOTS + MAX_ITEM_INDEX - 1)
            mu_swprintf(szTempName, I18N::Game::EquipmentItem);
        else if (pMixRecipeItem->m_sTypeMin == 0 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_HELPER + MAX_ITEM_INDEX - 1)
            mu_swprintf(szTempName, I18N::Game::EquipmentItem);
        else if (pMixRecipeItem->m_sTypeMin == 0 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_STAFF + MAX_ITEM_INDEX - 1)
            mu_swprintf(szTempName, I18N::Game::WeaponItem);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SHIELD &&
                 pMixRecipeItem->m_sTypeMax == ITEM_BOOTS + MAX_ITEM_INDEX - 1)
            mu_swprintf(szTempName, I18N::Game::DefenseItem);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_WING &&
                 pMixRecipeItem->m_sTypeMax == ITEM_WINGS_OF_SATAN)
            mu_swprintf(szTempName, I18N::Game::BasicWing);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_WINGS_OF_SPIRITS &&
                 pMixRecipeItem->m_sTypeMax == ITEM_WINGS_OF_DARKNESS)
            mu_swprintf(szTempName, I18N::Game::_2ndWing);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_WING_OF_CURSE &&
                 pMixRecipeItem->m_sTypeMax == ITEM_WING_OF_CURSE)
            mu_swprintf(szTempName, I18N::Game::BasicWing);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_WINGS_OF_DESPAIR &&
                 pMixRecipeItem->m_sTypeMax == ITEM_WINGS_OF_DESPAIR)
            mu_swprintf(szTempName, I18N::Game::_2ndWing);
        else if (pMixRecipeItem->m_sTypeMin == pMixRecipeItem->m_sTypeMax &&
                 (pMixRecipeItem->m_sTypeMin == ITEM_CHAOS_DRAGON_AXE ||
                  pMixRecipeItem->m_sTypeMin == ITEM_CHAOS_NATURE_BOW ||
                  pMixRecipeItem->m_sTypeMin == ITEM_CHAOS_LIGHTNING_STAFF))
            mu_swprintf(szTempName, I18N::Game::ChaosWeapon);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SEED_FIRE &&
                 pMixRecipeItem->m_sTypeMax == ITEM_SEED_EARTH)
            mu_swprintf(szTempName, I18N::Game::Seed);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SPHERE_MONO &&
                 pMixRecipeItem->m_sTypeMax == ITEM_SPHERE_5)
            mu_swprintf(szTempName, I18N::Game::Sphere);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SEED_SPHERE_FIRE_1 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_SEED_SPHERE_EARTH_5)
            mu_swprintf(szTempName, I18N::Game::SeedSphere);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SEED_SPHERE_FIRE_1 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_SEED_SPHERE_LIGHTNING_5)
            mu_swprintf(szTempName, L"%ls (%ls)", I18N::Game::SeedSphere,
                        I18N::Game::FireIceLightning);
        else if (pMixRecipeItem->m_sTypeMin == ITEM_SEED_SPHERE_WATER_1 &&
                 pMixRecipeItem->m_sTypeMax == ITEM_SEED_SPHERE_EARTH_5)
            mu_swprintf(szTempName, L"%ls (%ls)", I18N::Game::SeedSphere,
                        I18N::Game::WaterWindEarth);
        else
        {
            int iNameLen = wcslen(szTempName);
            for (int j = 1; j <= 3 && iNameLen - j - 1 >= 0; ++j)
                if (szTempName[iNameLen - j] == '+')
                    szTempName[iNameLen - j - 1] = '\0';
        }
        if (pMixRecipeItem->m_iDurabilityMin == pMixRecipeItem->m_iDurabilityMax)
            mu_swprintf(szTempName, L"%ls(%d)", szTempName, pMixRecipeItem->m_iDurabilityMin);

        if (pMixRecipeItem->m_iLevelMin == 0 && pMixRecipeItem->m_iLevelMax == 255)
            ;
        else if (pMixRecipeItem->m_iLevelMin == pMixRecipeItem->m_iLevelMax)
            mu_swprintf(szTempName, L"%ls +%d", szTempName, pMixRecipeItem->m_iLevelMin);
        else if (pMixRecipeItem->m_iLevelMin == 0)
            mu_swprintf(szTempName, L"%ls +%d%ls", szTempName, pMixRecipeItem->m_iLevelMax,
                        I18N::Game::Maximum);
        else if (pMixRecipeItem->m_iLevelMax == 255)
            mu_swprintf(szTempName, L"%ls +%d%ls", szTempName, pMixRecipeItem->m_iLevelMin,
                        I18N::Game::Minimum);
        else
            mu_swprintf(szTempName, L"%ls +%d~%d", szTempName, pMixRecipeItem->m_iLevelMin,
                        pMixRecipeItem->m_iLevelMax);

        if (pMixRecipeItem->m_iOptionMin == 0 && pMixRecipeItem->m_iOptionMax == 255)
            ;
        else if (pMixRecipeItem->m_iOptionMin == pMixRecipeItem->m_iOptionMax)
            mu_swprintf(szTempName, L"%ls +%d%ls", szTempName, pMixRecipeItem->m_iOptionMin,
                        I18N::Game::Option385);
        else if (pMixRecipeItem->m_iOptionMin == 0)
            mu_swprintf(szTempName, L"%ls +%d%ls%ls", szTempName, pMixRecipeItem->m_iOptionMax,
                        I18N::Game::Option385, I18N::Game::Maximum);
        else if (pMixRecipeItem->m_iOptionMax == 255)
            mu_swprintf(szTempName, L"%ls +%d%ls%ls", szTempName, pMixRecipeItem->m_iOptionMin,
                        I18N::Game::Option385, I18N::Game::Minimum);
        else
            mu_swprintf(szTempName, L"%ls +%d~%d%ls", szTempName, pMixRecipeItem->m_iOptionMin,
                        pMixRecipeItem->m_iOptionMax, I18N::Game::Option385);
    }

    if (pMixRecipeItem->m_iCountMin == 0 && pMixRecipeItem->m_iCountMax == 255)
        mu_swprintf(szTempName, L"%ls (%ls)", szTempName, I18N::Game::RateIncrease);
    else if (pMixRecipeItem->m_iCountMin == pMixRecipeItem->m_iCountMax)
        mu_swprintf(szTempName, L"%ls %d%ls", szTempName, pMixRecipeItem->m_iCountMin,
                    I18N::Game::Quantity);
    else if (pMixRecipeItem->m_iCountMin == 0)
        mu_swprintf(szTempName, L"%ls %d%ls %ls", szTempName, pMixRecipeItem->m_iCountMax,
                    I18N::Game::Quantity, I18N::Game::Maximum);
    else if (pMixRecipeItem->m_iCountMax == 255)
        mu_swprintf(szTempName, L"%ls %d%ls %ls", szTempName, pMixRecipeItem->m_iCountMin,
                    I18N::Game::Quantity, I18N::Game::Minimum);
    else
        mu_swprintf(szTempName, L"%ls %d~%d%ls", szTempName, pMixRecipeItem->m_iCountMin,
                    pMixRecipeItem->m_iCountMax, I18N::Game::Quantity);

    BOOL bPreName = FALSE;
    if (pMixRecipeItem->m_dwSpecialItem & RCP_SP_EXCELLENT)
    {
        mu_swprintf(pszNameOut, L"%ls %ls", I18N::Game::Excellent, szTempName);
        bPreName = TRUE;
    }
    if (pMixRecipeItem->m_dwSpecialItem & RCP_SP_SETITEM)
    {
        mu_swprintf(pszNameOut, L"%ls %ls", I18N::Game::Set, szTempName);
        bPreName = TRUE;
    }
    if (pMixRecipeItem->m_dwSpecialItem & RCP_SP_HARMONY)
    {
        mu_swprintf(pszNameOut, L"%ls %ls", I18N::Game::Improve, szTempName);
        bPreName = TRUE;
    }
    if (pMixRecipeItem->m_dwSpecialItem & RCP_SP_SOCKETITEM)
    {
        mu_swprintf(pszNameOut, L"%ls %ls", I18N::Game::Socket, szTempName);
        bPreName = TRUE;
    }
    if (bPreName == FALSE)
    {
        wcscpy(pszNameOut, szTempName);
    }

    if (recipes.owner_->IsMixInit())
    {
        if (pMixRecipeItem->m_iCountMin == 0)
            return MIX_SOURCE_PARTIALLY;
        else
            return MIX_SOURCE_NO;
    }

    if (recipes.m_iMostSimilarMixSourceTest[iItemNum] == 0)
        return MIX_SOURCE_YES;
    else if (recipes.m_iMostSimilarMixSourceTest[iItemNum] < pMixRecipeItem->m_iCountMax)
    {
        if (pMixRecipeItem->m_iCountMin <= 1)
            return MIX_SOURCE_YES;
        else
            return MIX_SOURCE_PARTIALLY;
    }
    else
    {
        if (pMixRecipeItem->m_iCountMin == 0)
            return MIX_SOURCE_PARTIALLY;
        else
            return MIX_SOURCE_NO;
    }
}

void CMixRecipes::EvaluateMixItems(int iNumMixItems, CMixItem *pMixItems)
{
    m_bFindMixLuckItem = FALSE;
    m_dwTotalItemValue = 0;
    m_dwExcellentItemValue = 0;
    m_dwEquipmentItemValue = 0;
    m_dwWingItemValue = 0;
    m_dwSetItemValue = 0;
    m_dwTotalNonJewelItemValue = 0;

    for (int i = 0; i < iNumMixItems; ++i)
    {
        if (pMixItems[i].m_bMixLuck == TRUE)
            m_bFindMixLuckItem = TRUE;
        if (pMixItems[i].m_dwSpecialItem & RCP_SP_EXCELLENT)
            m_dwExcellentItemValue += pMixItems[i].m_dwMixValue;
        if (pMixItems[i].m_bIsEquipment == TRUE)
            m_dwEquipmentItemValue += pMixItems[i].m_dwMixValue;
        if (pMixItems[i].m_bIsWing == TRUE)
            m_dwWingItemValue += pMixItems[i].m_dwMixValue;
        if (pMixItems[i].m_dwSpecialItem & RCP_SP_SETITEM)
            m_dwSetItemValue += pMixItems[i].m_dwMixValue;
        if (pMixItems[i].m_bIsJewelItem == FALSE)
            m_dwTotalNonJewelItemValue += pMixItems[i].m_dwMixValue;
        m_dwTotalItemValue += pMixItems[i].m_dwMixValue;
    }
}

void CMixRecipes::CalcCharmBonusRate(int iNumMixItems, CMixItem *pMixItems)
{
    m_wTotalCharmBonus = 0;
    for (int i = 0; i < iNumMixItems; ++i)
    {
        if (pMixItems[i].m_bIsCharmItem == TRUE)
            m_wTotalCharmBonus += pMixItems[i].m_iCount;
    }
}

void CMixRecipes::CalcChaosCharmCount(int iNumMixItems, CMixItem *pMixItems)
{
    m_wTotalChaosCharmCount = 0;
    for (int i = 0; i < iNumMixItems; ++i)
    {
        if (pMixItems[i].m_bIsChaosCharmItem == TRUE)
            m_wTotalChaosCharmCount += 1;
    }
}

void CMixRecipes::CalcMixRate(int iNumMixItems, CMixItem *pMixItems)
{
    if (iNumMixItems < 0)
        return;
    if (pMixItems == NULL)
        return;

    m_iSuccessRate = 0;
    if (GetCurRecipe() == NULL)
        return;

    m_pMixRates = GetCurRecipe()->m_RateToken;
    m_iMixRateIter = 0;
    m_iSuccessRate = (int)MixrateAddSub();

    if (m_iSuccessRate > GetCurRecipe()->m_iSuccessRate)
    {
        m_iSuccessRate = GetCurRecipe()->m_iSuccessRate;
    }
    if (GetCurRecipe()->m_bCharmOption == 'A')
    {
        m_iSuccessRate += m_wTotalCharmBonus;
    }
    if (m_iSuccessRate > 100)
    {
        m_iSuccessRate = 100;
    }
}

float CMixRecipes::MixrateAddSub()
{
    float fLvalue = 0;
    while (1)
    {
        if (m_iMixRateIter >= GetCurRecipe()->m_iNumRateData ||
            m_pMixRates[m_iMixRateIter].op == MRCP_RP)
        {
            return fLvalue;
        }
        switch (m_pMixRates[m_iMixRateIter].op)
        {
        case MRCP_ADD:
            ++m_iMixRateIter;
            fLvalue += MixrateMulDiv();
            break;
        case MRCP_SUB:
            ++m_iMixRateIter;
            fLvalue -= MixrateMulDiv();
            break;
        default:
            fLvalue = MixrateMulDiv();
            break;
        };
    }
}

float CMixRecipes::MixrateMulDiv()
{
    float fLvalue = 0;
    while (1)
    {
        if (m_iMixRateIter >= GetCurRecipe()->m_iNumRateData ||
            m_pMixRates[m_iMixRateIter].op == MRCP_RP)
        {
            return fLvalue;
        }
        switch (m_pMixRates[m_iMixRateIter].op)
        {
        case MRCP_ADD:
        case MRCP_SUB:
            return fLvalue;
        case MRCP_MUL:
            ++m_iMixRateIter;
            fLvalue *= MixrateFactor();
            break;
        case MRCP_DIV:
            ++m_iMixRateIter;
            fLvalue /= MixrateFactor();
            break;
        default:
            fLvalue = MixrateFactor();
            break;
        };
    }
}

float CMixRecipes::MixrateFactor()
{
    float fValue = 0;
    switch (m_pMixRates[m_iMixRateIter].op)
    {
    case MRCP_LP:
        ++m_iMixRateIter;
        fValue = MixrateAddSub();
        break;
    case MRCP_INT:
        ++m_iMixRateIter;
        if (m_pMixRates[m_iMixRateIter].op != MRCP_LP)
            assert(!"m_pMixRates error");
        ++m_iMixRateIter;
        fValue = int(MixrateAddSub());
        break;
    case MRCP_NUMBER:
        fValue = (float)m_pMixRates[m_iMixRateIter].value;
        break;
    case MRCP_MAXRATE:
        fValue = GetCurRecipe()->m_iSuccessRate;
        break;
    case MRCP_ITEM:
        fValue = m_dwTotalItemValue;
        break;
    case MRCP_WING:
        fValue = m_dwWingItemValue;
        break;
    case MRCP_EXCELLENT:
        fValue = m_dwExcellentItemValue;
        break;
    case MRCP_EQUIP:
        fValue = m_dwEquipmentItemValue;
        break;
    case MRCP_SET:
        fValue = m_dwSetItemValue;
        break;
    case MRCP_LUCKOPT:
        if (m_bFindMixLuckItem)
            fValue = 25;
        else
            fValue = 0;
        break;
    case MRCP_LEVEL1:
        fValue = m_iFirstItemLevel;
        break;
    case MRCP_NONJEWELITEM:
        fValue = m_dwTotalNonJewelItemValue;
        break;
    }
    ++m_iMixRateIter;
    return fValue;
}

void CMixRecipes::CalcMixReqZen(int iNumMixItems, CMixItem *pMixItems)
{
    if (iNumMixItems < 0)
        return;
    if (pMixItems == NULL)
        return;

    m_dwRequiredZen = 0;
    if (GetCurRecipe() == NULL)
        return;
    switch (GetCurRecipe()->m_bRequiredZenType)
    {
    case 'A':
    case 'C':
        m_dwRequiredZen = GetCurRecipe()->m_dwRequiredZen;
        break;
    case 'B':
        m_dwRequiredZen = m_iSuccessRate * GetCurRecipe()->m_dwRequiredZen;
        break;
    case 'D': {
        int iItemType = 0;
        if (ITEM_SWORD <= pMixItems[0].m_sType && ITEM_STAFF > pMixItems[0].m_sType)
        {
            iItemType = SI_Weapon;
        }
        else if (ITEM_STAFF <= pMixItems[0].m_sType && ITEM_SHIELD > pMixItems[0].m_sType)
        {
            iItemType = SI_Staff;
        }
        else if (ITEM_SHIELD <= pMixItems[0].m_sType && ITEM_WING > pMixItems[0].m_sType)
        {
            iItemType = SI_Defense;
        }
        m_dwRequiredZen = owner_->JewelHarmonyInfoObject()
                              ->GetHarmonyJewelOptionInfo(iItemType, pMixItems[0].m_wHarmonyOption)
                              .Zen[pMixItems[0].m_wHarmonyOptionLevel];
    }
    break;
    default:
        break;
    }
}

BOOL CMixRecipes::IsChaosItem(CMixItem &rSource)
{
    if (rSource.m_sType == ITEM_CHAOS_DRAGON_AXE || rSource.m_sType == ITEM_CHAOS_NATURE_BOW ||
        rSource.m_sType == ITEM_CHAOS_LIGHTNING_STAFF)
        return TRUE;
    return FALSE;
}

BOOL CMixRecipes::IsChaosJewel(CMixItem &rSource)
{
    if (rSource.m_sType == ITEM_JEWEL_OF_CHAOS)
        return TRUE;
    return FALSE;
}

BOOL CMixRecipes::Is380AddedItem(CMixItem &rSource)
{
    return rSource.m_b380AddedItem;
}

BOOL CMixRecipes::IsFenrirAddedItem(CMixItem &rSource)
{
    return rSource.m_bFenrirAddedItem;
}

BOOL CMixRecipes::IsUpgradableItem(CMixItem &rSource)
{
    return (rSource.m_bIsEquipment || rSource.m_bIsWing || rSource.m_bIsUpgradedWing ||
            rSource.m_bIs3rdUpgradedWing);
}

BOOL CMixRecipes::IsSourceOfRefiningStone(CMixItem &rSource)
{
    if (rSource.m_iLevel < 4)
    {
        switch (rSource.m_sType)
        {
        case ITEM_KRIS:
        case ITEM_SHORT_SWORD:
        case ITEM_RAPIER:
        case ITEM_SWORD_OF_ASSASSIN:
        case ITEM_SMALL_AXE:
        case ITEM_HAND_AXE:
        case ITEM_DOUBLE_AXE:
        case ITEM_SMALLMACE:
        case ITEM_MORNING_STAR:
        case ITEM_FLAIL:
        case ITEM__SPEAR:
        case ITEM_DRAGON_LANCE:
        case ITEM_GIANT_TRIDENT:
        case ITEM_DOUBLE_POLEAXE:
        case ITEM_HALBERD:
        case ITEM_SHORT_BOW:
        case ITEM_SMALL_BOW:
        case ITEM_ELVEN_BOW:
        case ITEM_BATTLE_BOW:
        case ITEM_CROSSBOW:
        case ITEM_GOLDEN_CROSSBOW:
        case ITEM_ARQUEBUS:
        case ITEM_LIGHT_CROSSBOW:
        case ITEM_SKULL_STAFF:
        case ITEM_ANGELIC_STAFF:
        case ITEM_SERPENT_STAFF:
        case ITEM_SMALL_SHIELD:
        case ITEM_HORN_SHIELD:
        case ITEM_KITE_SHIELD:
        case ITEM_ELVEN_SHIELD:
        case ITEM_BUCKLER:
        case ITEM_SKULL_SHIELD:
        case ITEM_SPIKED_SHIELD:
        case ITEM_PLATE_SHIELD:
        case ITEM_BIG_ROUND_SHIELD:
        case ITEM_BRONZE_HELM:
        case ITEM_PAD_HELM:
        case ITEM_BONE_HELM:
        case ITEM_LEATHER_HELM:
        case ITEM_SCALE_HELM:
        case ITEM_SPHINX_MASK:
        case ITEM_BRASS_HELM:
        case ITEM_VINE_HELM:
        case ITEM_SILK_HELM:
        case ITEM_WIND_HELM:
        case ITEM_BRONZE_ARMOR:
        case ITEM_PAD_ARMOR:
        case ITEM_BONE_ARMOR:
        case ITEM_LEATHER_ARMOR:
        case ITEM_SCALE_ARMOR:
        case ITEM_SPHINX_ARMOR:
        case ITEM_BRASS_ARMOR:
        case ITEM_VINE_ARMOR:
        case ITEM_SILK_ARMOR:
        case ITEM_WIND_ARMOR:
        case ITEM_BRONZE_PANTS:
        case ITEM_PAD_PANTS:
        case ITEM_BONE_PANTS:
        case ITEM_LEATHER_PANTS:
        case ITEM_SCALE_PANTS:
        case ITEM_SPHINX_PANTS:
        case ITEM_BRASS_PANTS:
        case ITEM_VINE_PANTS:
        case ITEM_SILK_PANTS:
        case ITEM_WIND_PANTS:
        case ITEM_BRONZE_GLOVES:
        case ITEM_PAD_GLOVES:
        case ITEM_BONE_GLOVES:
        case ITEM_LEATHER_GLOVES:
        case ITEM_SCALE_GLOVES:
        case ITEM_SPHINX_GLOVES:
        case ITEM_BRASS_GLOVES:
        case ITEM_VINE_GLOVES:
        case ITEM_SILK_GLOVES:
        case ITEM_WIND_GLOVES:
        case ITEM_BRONZE_BOOTS:
        case ITEM_PAD_BOOTS:
        case ITEM_BONE_BOOTS:
        case ITEM_LEATHER_BOOTS:
        case ITEM_SCALE_BOOTS:
        case ITEM_SPHINX_BOOTS:
        case ITEM_BRASS_BOOTS:
        case ITEM_VINE_BOOTS:
        case ITEM_SILK_BOOTS:
        case ITEM_WIND_BOOTS:
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CMixRecipes::IsSourceOfAttachSeedSphereToWeapon(CMixItem &rSource)
{
    if (rSource.m_sType >= ITEM_SEED_SPHERE_FIRE_1 && rSource.m_sType <= ITEM_SEED_SPHERE_EARTH_5)
    {
        int iSeedSphereType = rSource.m_sType - ITEM_WING;
        if (iSeedSphereType % 2 == 0)
            return TRUE;
    }
    return FALSE;
}

BOOL CMixRecipes::IsSourceOfAttachSeedSphereToArmor(CMixItem &rSource)
{
    if (rSource.m_sType >= ITEM_SEED_SPHERE_FIRE_1 && rSource.m_sType <= ITEM_SEED_SPHERE_EARTH_5)
    {
        int iSeedSphereType = rSource.m_sType - ITEM_WING;

        if (iSeedSphereType % 2 == 1)
            return TRUE;
    }
    return FALSE;
}

BOOL CMixRecipes::IsCharmItem(CMixItem &rSource)
{
    return rSource.m_bIsCharmItem;
}

BOOL CMixRecipes::IsChaosCharmItem(CMixItem &rSource)
{
    return rSource.m_bIsChaosCharmItem;
}

BOOL CMixRecipes::IsJewelItem(CMixItem &rSource)
{
    return rSource.m_bIsJewelItem;
}

CMixRecipeMgr::CMixRecipeMgr(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ErrorReport(keeper.ErrorReport()),
      g_hWnd(keeper.PlatformWindowHandle()), m_MixItemInventory(keeper), m_iMixType(0),
      m_bIsMixInit(TRUE)
{
    for (auto &recipe : m_MixRecipe)
    {
        recipe.BindOwner(*this);
    }
    m_iMixSubType = 0;
    m_btPlusChaosRate = 0;
}

void CMixRecipeMgr::Initialize()
{
    OpenRecipeFile(L"Data\\Local\\Mix.bmd");
}

void CMixRecipeMgr::OpenRecipeFile(const wchar_t *szFileName)
{
    int i, j;
    for (j = 0; j < MAX_MIX_TYPES; ++j)
    {
        m_MixRecipe[j].Reset();
    }

    FILE *fp = _wfopen(szFileName, L"rb");
    if (fp == NULL)
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", szFileName);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, NULL, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        exit(0);
    }

    int iNumMixRecipes[MAX_MIX_TYPES];
    int iSize = sizeof(int) * MAX_MIX_TYPES;
    fread(iNumMixRecipes, iSize, 1, fp);
    BuxConvert((BYTE *)iNumMixRecipes, iSize);

    iSize = sizeof(MIX_RECIPE);
    for (j = 0; j < MAX_MIX_TYPES; ++j)
    {
        if (feof(fp) || iNumMixRecipes[j] > 1000)
        {
            wchar_t Text[256];
            mu_swprintf(Text, L"%ls - Version not matched.", szFileName);
            g_ErrorReport.Write(Text);
            MessageBox(g_hWnd, Text, NULL, MB_OK);
            SendMessage(g_hWnd, WM_DESTROY, 0, 0);
            fclose(fp);
            exit(0);
        }
        for (i = 0; i < iNumMixRecipes[j]; ++i)
        {
            auto *pMixRecipe = new MIX_RECIPE;
            fread(pMixRecipe, iSize, 1, fp);
            BuxConvert((BYTE *)pMixRecipe, iSize);
            m_MixRecipe[j].AddRecipe(pMixRecipe);
        }
    }
    fclose(fp);
}

int CMixRecipeMgr::GetMixInventoryType()
{
    assert(m_iMixType >= MIXTYPE_GOBLIN_NORMAL && m_iMixType < MAX_MIX_TYPES &&
           "Undefined combination");
    return m_iMixType;
}

STORAGE_TYPE CMixRecipeMgr::GetMixInventoryEquipmentIndex()
{
    switch (GetMixInventoryType())
    {
    case SEASON3A::MIXTYPE_GOBLIN_NORMAL:
    case SEASON3A::MIXTYPE_GOBLIN_CHAOSITEM:
    case SEASON3A::MIXTYPE_GOBLIN_ADD380:
        return STORAGE_TYPE::CHAOS_MIX;
    case SEASON3A::MIXTYPE_CASTLE_SENIOR:
        return STORAGE_TYPE::CHAOS_MIX;
    case SEASON3A::MIXTYPE_TRAINER:
        return STORAGE_TYPE::TRAINER_MIX;
    case SEASON3A::MIXTYPE_OSBOURNE:
        return STORAGE_TYPE::OSBOURNE_MIX;
    case SEASON3A::MIXTYPE_JERRIDON:
        return STORAGE_TYPE::JERRIDON_MIX;
    case SEASON3A::MIXTYPE_ELPIS:
        return STORAGE_TYPE::ELPIS_MIX;
    case SEASON3A::MIXTYPE_CHAOS_CARD:
        return STORAGE_TYPE::CHAOS_CARD_MIX;
    case SEASON3A::MIXTYPE_CHERRYBLOSSOM:
        return STORAGE_TYPE::CHERRYBLOSSOM_MIX;
    case SEASON3A::MIXTYPE_EXTRACT_SEED:
        return STORAGE_TYPE::EXTRACT_SEED_MIX;
    case SEASON3A::MIXTYPE_SEED_SPHERE:
        return STORAGE_TYPE::SEED_SPHERE_MIX;
    case SEASON3A::MIXTYPE_ATTACH_SOCKET:
        return STORAGE_TYPE::ATTACH_SOCKET_MIX;
    case SEASON3A::MIXTYPE_DETACH_SOCKET:
        return STORAGE_TYPE::DETACH_SOCKET_MIX;
    default:
        assert(!"Mix error");
        return STORAGE_TYPE::CHAOS_MIX;
    }
}

void CMixRecipeMgr::ResetMixItemInventory()
{
    m_MixItemInventory.Reset();
}

void CMixRecipeMgr::AddItemToMixItemInventory(ITEM *pItem)
{
    m_MixItemInventory.AddItem(pItem);
}

void CMixRecipeMgr::CheckMixInventory()
{
    if (m_MixItemInventory.GetNumMixItems() == 0)
        m_bIsMixInit = TRUE;
    else
        m_bIsMixInit = FALSE;

    CheckRecipe(m_MixItemInventory.GetNumMixItems(), m_MixItemInventory.GetMixItems());
    CheckRecipeSimilarity(m_MixItemInventory.GetNumMixItems(), m_MixItemInventory.GetMixItems());
}

int CMixRecipeMgr::GetSeedSphereID(int iOrder)
{
    int iCurrOrder = 0;
    CMixItem *pItems = m_MixItemInventory.GetMixItems();
    for (int i = 0; i < m_MixItemInventory.GetNumMixItems(); ++i)
    {
        if (pItems[i].m_bySeedSphereID != SOCKET_EMPTY)
        {
            if (iCurrOrder == iOrder)
            {
                return pItems[i].m_bySeedSphereID;
            }
            else
            {
                ++iCurrOrder;
            }
        }
    }
    return SOCKET_EMPTY;
}

namespace
{
int GetTextLines(const wchar_t *inText, wchar_t *outText, int maxLine, int lineSize)
{
    int iLine = 0;
    const wchar_t *lpLineStart = inText;
    wchar_t *lpDst = outText;
    const wchar_t *lpSpace = NULL;
    int iMbclen = 0;
    for (const wchar_t *lpSeek = inText; *lpSeek; lpSeek += iMbclen, lpDst += iMbclen)
    {
        iMbclen = _mbclen((unsigned char *)lpSeek);
        if (iMbclen + (int)(lpSeek - lpLineStart) >= lineSize)
        {
            if (lpSpace && (int)(lpSeek - lpSpace) < std::min<int>(10, lineSize / 2))
            {
                lpDst -= (lpSeek - lpSpace - 1);
                lpSeek = lpSpace + 1;
            }

            lpLineStart = lpSeek;
            *lpDst = '\0';
            if (iLine >= maxLine - 1)
            {
                break;
            }
            ++iLine;
            lpDst = outText + iLine * lineSize;
            lpSpace = NULL;
        }

        memcpy(lpDst, lpSeek, iMbclen);
        if (*lpSeek == ' ')
        {
            lpSpace = lpSeek;
        }
    }
    *lpDst = '\0';

    return (iLine + 1);
}
} // namespace

JewelHarmonyInfo *JewelHarmonyInfo::MakeInfo(SessionKeeper &keeper)
{
    auto *info = new JewelHarmonyInfo(keeper);
    return info;
}

JewelHarmonyInfo::JewelHarmonyInfo(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_strSelectedML(keeper.AssetLanguage()),
      g_ErrorReport(keeper.ErrorReport()), g_hWnd(keeper.PlatformWindowHandle())
{
    bool Result = true;
    const std::wstring harmonyOptionFile =
        L"Data\\Local\\" + g_strSelectedML + L"\\JewelOfHarmonyOption_" + g_strSelectedML + L".bmd";
    if (!OpenJewelHarmonyInfoFile(harmonyOptionFile))
    {
        Result = false;
    }

    if (!Result)
    {
        wchar_t szMessage[256];
        ::mu_swprintf(szMessage, L"%ls file not found.\r\n",
                      L"JewelOfHarmonyOption.bmd && JewelOfHarmonySmelt.bmd");
        g_ErrorReport.Write(szMessage);
        ::MessageBox(g_hWnd, szMessage, NULL, MB_OK);
        ::PostMessage(g_hWnd, WM_DESTROY, 0, 0);
    }
}

JewelHarmonyInfo::~JewelHarmonyInfo()
{
}

const bool JewelHarmonyInfo::OpenJewelHarmonyInfoFile(const std::wstring &filename)
{
    constexpr size_t NAME_SIZE = 60;
    constexpr size_t LEVEL_COUNT = 14;
    constexpr size_t ENTRY_SIZE = sizeof(int) +               // OptionType
                                  NAME_SIZE +                 // Name
                                  sizeof(int) +               // Minlevel
                                  LEVEL_COUNT * sizeof(int) + // HarmonyJewelLevel
                                  LEVEL_COUNT * sizeof(int);  // Zen

    FILE *fp = ::_wfopen(filename.c_str(), L"rb");
    if (fp != NULL)
    {
        int nEntries = MAXHARMONYJEWELOPTIONTYPE * MAXHARMONYJEWELOPTIONINDEX;
        size_t nSize = ENTRY_SIZE * nEntries;

        std::vector<BYTE> tempBuffer(nSize);

        ::fread(tempBuffer.data(), nSize, 1, fp);
        ::BuxConvert((BYTE *)tempBuffer.data(), nSize);
        ::fclose(fp);

        // Copy to the new HarmonyJewelOption array that uses wchar_t[] for the Name
        for (int i = 0; i < nEntries; ++i)
        {
            int type = i / MAXHARMONYJEWELOPTIONINDEX;
            int option = i % MAXHARMONYJEWELOPTIONINDEX;

            // Calculate base offset for the current entry
            BYTE *entry = tempBuffer.data() + i * ENTRY_SIZE;

            // Read OptionType
            m_OptionData[type][option].OptionType = *reinterpret_cast<int *>(entry);

            // Read Name and convert to wchar_t
            char *name = reinterpret_cast<char *>(entry + sizeof(int));
            pMultiLanguage->ConvertFromUtf8(m_OptionData[type][option].Name, name, NAME_SIZE);

            // Read Minlevel
            m_OptionData[type][option].Minlevel =
                *reinterpret_cast<int *>(entry + sizeof(int) + NAME_SIZE);

            // Read HarmonyJewelLevel[14]
            memcpy(m_OptionData[type][option].HarmonyJewelLevel,
                   entry + sizeof(int) + NAME_SIZE + sizeof(int), LEVEL_COUNT * sizeof(int));

            // Read Zen[14]
            memcpy(m_OptionData[type][option].Zen,
                   entry + sizeof(int) + NAME_SIZE + sizeof(int) + LEVEL_COUNT * sizeof(int),
                   LEVEL_COUNT * sizeof(int));
        }

        return true;
    }
    return false;
}

const StrengthenItem JewelHarmonyInfo::GetItemType(int type)
{
    StrengthenItem itemtype = SI_None;

    if (ITEM_SWORD <= type && ITEM_STAFF > type)
    {
        itemtype = SI_Weapon;
        return itemtype;
    }
    else if (ITEM_STAFF <= type && ITEM_SHIELD > type)
    {
        itemtype = SI_Staff;
        return itemtype;
    }
    else if (ITEM_SHIELD <= type && ITEM_WING > type)
    {
        itemtype = SI_Defense;
        return itemtype;
    }

    return itemtype;
}

const bool JewelHarmonyInfo::IsHarmonyJewelOption(int type, int option)
{
    bool isSuccess = false;

    switch (type)
    {
    case SI_Weapon: {
        if (0 <= option && (MAXHARMONYJEWELOPTIONINDEX + 1) > option)
        {
            isSuccess = true;
        }
    }
    break;
    case SI_Staff: {
        if (0 <= option && (MAXHARMONYJEWELOPTIONINDEX + 1) - 2 > option)
        {
            isSuccess = true;
        }
    }
    break;
    case SI_Defense: {
        if (0 <= option && (MAXHARMONYJEWELOPTIONINDEX + 1) - 2 > option)
        {
            isSuccess = true;
        }
    }
    break;
    }

    return isSuccess;
}

void JewelHarmonyInfo::GetStrengthenCapability(StrengthenCapability *pitemSC, const ITEM *pitem,
                                               const int index)
{
    if (pitem->Jewel_Of_Harmony_Option != 0)
    {
        StrengthenItem type = GetItemType(static_cast<int>(pitem->Type));

        if (type < SI_None)
        {
            if (IsHarmonyJewelOption(type, pitem->Jewel_Of_Harmony_Option))
            {
                HARMONYJEWELOPTION harmonyjewel =
                    GetHarmonyJewelOptionInfo(type, pitem->Jewel_Of_Harmony_Option);

                if (index == 0)
                {
                    if (type == SI_Weapon)
                    {
                        pitemSC->SI_isNB = true;

                        if (pitem->Jewel_Of_Harmony_Option == 3)
                        {
                            pitemSC->SI_NB.SI_force =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 4)
                        {
                            pitemSC->SI_NB.SI_activity =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                    }
                    else if (type == SI_Staff)
                    {
                        pitemSC->SI_isNB = true;

                        if (pitem->Jewel_Of_Harmony_Option == 2)
                        {
                            pitemSC->SI_NB.SI_force =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 3)
                        {
                            pitemSC->SI_NB.SI_activity =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                    }
                }
                else if (index == 1)
                {
                    if (type == SI_Weapon)
                    {
                        pitemSC->SI_isSP = true;

                        if (pitem->Jewel_Of_Harmony_Option == 1)
                        {
                            pitemSC->SI_SP.SI_minattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 2)
                        {
                            pitemSC->SI_SP.SI_maxattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 5)
                        {
                            pitemSC->SI_SP.SI_minattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                            pitemSC->SI_SP.SI_maxattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 7)
                        {
                            pitemSC->SI_SP.SI_skillattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 8)
                        {
                            pitemSC->SI_SP.SI_attackpowerRate =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                    }
                    else if (type == SI_Staff)
                    {
                        pitemSC->SI_isSP = true;

                        if (pitem->Jewel_Of_Harmony_Option == 1)
                        {
                            pitemSC->SI_SP.SI_magicalpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 4)
                        {
                            pitemSC->SI_SP.SI_skillattackpower =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 7)
                        {
                            pitemSC->SI_SP.SI_attackpowerRate =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                    }
                }

                else if (index == 2)
                {
                    if (SI_Defense == type)
                    {
                        pitemSC->SI_isSD = true;

                        if (pitem->Jewel_Of_Harmony_Option == 1)
                        {
                            pitemSC->SI_SD.SI_defense =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 2)
                        {
                            pitemSC->SI_SD.SI_AG =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 3)
                        {
                            pitemSC->SI_SD.SI_HP =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                        else if (pitem->Jewel_Of_Harmony_Option == 6)
                        {
                            pitemSC->SI_SD.SI_defenseRate =
                                harmonyjewel.HarmonyJewelLevel[pitem->Jewel_Of_Harmony_OptionLevel];
                        }
                    }
                }
            }
        }
    }
}

#define ITEMADDOPTION_DATA_FILE L"Data\\Local\\ItemAddOption.bmd"

ItemAddOptioninfo *ItemAddOptioninfo::MakeInfo(SessionKeeper &keeper)
{
    auto *option = new ItemAddOptioninfo(keeper);
    return option;
}

ItemAddOptioninfo::ItemAddOptioninfo(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ErrorReport(keeper.ErrorReport()),
      g_hWnd(keeper.PlatformWindowHandle())
{
    bool Result = true;

    Result = OpenItemAddOptionInfoFile(ITEMADDOPTION_DATA_FILE);

    if (!Result)
    {
        wchar_t szMessage[256];
        ::mu_swprintf(szMessage, L"%ls file not found.\r\n", ITEMADDOPTION_DATA_FILE);
        g_ErrorReport.Write(szMessage);
        ::MessageBox(g_hWnd, szMessage, NULL, MB_OK);
        ::PostMessage(g_hWnd, WM_DESTROY, 0, 0);
    }
}

ItemAddOptioninfo::~ItemAddOptioninfo()
{
}

const bool ItemAddOptioninfo::OpenItemAddOptionInfoFile(const std::wstring &filename)
{
    FILE *fp = ::_wfopen(filename.c_str(), L"rb");
    if (fp != NULL)
    {
        int nSize = sizeof(ITEM_ADD_OPTION) * MAX_ITEM;

        ::fread(m_ItemAddOption, nSize, 1, fp);
        ::BuxConvert((BYTE *)m_ItemAddOption, nSize);
        ::fclose(fp);

        return true;
    }
    return false;
}

void ItemAddOptioninfo::GetItemAddOtioninfoText(std::vector<std::wstring> &outtextlist, int type)
{
    int optiontype = 0;
    int optionvalue = 0;

    for (int i = 0; i < 2; ++i)
    {
        std::wstring text;
        wchar_t TempText[100];

        if (i == 0)
        {
            optiontype = m_ItemAddOption[type].m_byOption1;
            optionvalue = m_ItemAddOption[type].m_byValue1;
        }
        else
        {
            optiontype = m_ItemAddOption[type].m_byOption2;
            optionvalue = m_ItemAddOption[type].m_byValue2;
        }

        switch (optiontype)
        {
        case 1:
            mu_swprintf(TempText, I18N::Game::AttackSucessRateIncreaseD, optionvalue);
            break;
        case 2:
            mu_swprintf(TempText, I18N::Game::AdditionalDamageD, optionvalue);
            break;
        case 3:
            mu_swprintf(TempText, I18N::Game::DefenseSuccessRateIncreaseD, optionvalue);
            break;
        case 4:
            mu_swprintf(TempText, I18N::Game::DefensiveSkillD, optionvalue);
            break;
        case 5:
            mu_swprintf(TempText, I18N::Game::MaxHPIncreaseD, optionvalue);
            break;
        case 6:
            mu_swprintf(TempText, I18N::Game::MaxSDIncreaseD, optionvalue);
            break;
        case 7:
            mu_swprintf(TempText, I18N::Game::SDAutoRecovery);
            break;
        case 8:
            mu_swprintf(TempText, I18N::Game::SDRecoveryRateIncreaseD, optionvalue);
            break;
        }

        text = TempText;
        outtextlist.push_back(text);
    }
}

CSkillEffectMgr::CSkillEffectMgr(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
{
}

CSkillEffectMgr::~CSkillEffectMgr()
{
}

OBJECT *CSkillEffectMgr::GetEffect(int iIndex)
{
    return &m_SkillEffects[iIndex];
}

BOOL CSkillEffectMgr::IsSkillEffect(int Type, vec3_t Position, vec3_t Angle, vec3_t Light,
                                    int SubType, OBJECT *Owner, short PKKey, WORD SkillIndex,
                                    WORD Skill, WORD SkillSerialNum, float Scale, int sTargetIndex)
{
    if (Owner != &Hero->Object)
        return FALSE;

    switch (Type)
    {
    case BITMAP_BOSS_LASER:
    case MODEL_SKILL_BLAST:
    case MODEL_DARK_SCREAM:
    case BITMAP_SWORD_FORCE:
        return TRUE;
    case MODEL_SKILL_INFERNO:
        if (SubType < 2)
            return TRUE;
        break;
    case MODEL_CIRCLE:
        if (SubType == 0)
            return TRUE;
        break;
    case BITMAP_FLAME:
        if (SubType == 0)
            return TRUE;
        break;
    case MODEL_STORM:
        if (SubType == 0)
            return TRUE;
        break;

        //     case MODEL_ARROW_DOUBLE:
        // 		if(SubType==1) return TRUE;
        // 		break;
        // 	case MODEL_ARROW:
        // 		if( SubType!=3 && SubType!=4 ) return TRUE;
        // 		break;
        // 	case MODEL_ARROW_BEST_CROSSBOW :
        // 	case MODEL_ARROW_STEEL:
        // 	case MODEL_ARROW_THUNDER:
        // 	case MODEL_ARROW_LASER:
        // 	case MODEL_ARROW_V:
        // 	case MODEL_ARROW_SAW:
        // 	case MODEL_ARROW_NATURE:
        // 	case MODEL_ARROW_WING:
        //     case MODEL_LACEARROW:
        // 	case MODEL_ARROW_SPARK:
        // 	case MODEL_ARROW_RING:
        // 	case MODEL_ARROW_BOMB:
        // 	case MODEL_ARROW_DARKSTINGER:
        // 	case MODEL_ARROW_GAMBLE:
        // 	case MODEL_ARROW_DRILL:
        // 		return TRUE;
    }
    return FALSE;
}

OBJECT *CSkillEffectMgr::CreateEffect()
{
    return &m_SkillEffects[m_SkillEffects.Allocate()];
}

bool CSkillEffectMgr::DeleteEffect(int Type, OBJECT *Owner, int iSubType)
{
    bool bDelete = false;
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == Type)
        {
            if (iSubType == -1 || iSubType == o->SubType)
            {
                if (o->Owner == Owner)
                {
                    EffectDestructor(o);
                    bDelete = true;
                }
            }
        }
    }

    return bDelete;
}

void CSkillEffectMgr::DeleteEffect(int efftype)
{
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == efftype)
        {
            EffectDestructor(o);
        }
    }
}

void CSkillEffectMgr::DeleteAllEffects()
{
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        EffectDestructor(&*cursor);
    }
}

bool CSkillEffectMgr::SearchEffect(int iType, OBJECT *pOwner, int iSubType)
{
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == iType && o->Owner == pOwner)
        {
            if (iSubType == -1 || o->SubType == iSubType)
            {
                return true;
            }
        }
    }

    return false;
}

BOOL CSkillEffectMgr::FindSameEffectOfSameOwner(int iType, OBJECT *pOwner)
{
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        if (o->Type == iType && o->Owner == pOwner)
        {
            return (TRUE);
        }
    }

    return (FALSE);
}

void CSkillEffectMgr::MoveEffects()
{
    for (auto cursor = m_SkillEffects.begin(), end = m_SkillEffects.end(); cursor != end; ++cursor)
    {
        const int i = cursor.Index();
        OBJECT *o = &*cursor;
        EffectBirthStep birthStep(sessionKeeper_.FrameAnimationFactor(), o->BirthTiming);
        if (sessionKeeper_.FrameAnimationFactor() <= 0.f)
            continue;
        const auto birthSerial = o->BirthTiming.serial;
        MoveEffect(o, i);
        if (o->BirthTiming.serial != birthSerial)
            continue;
        AdvanceMapEffectVisual(*o);
        TheMapProcess().AdvanceObjectFade(*o);
    }
}

namespace
{
// Energy requirement formula constants. The scaling formula for a skill is
// `<base> + (BMD.Energy * BMD.Level * <scale>) / 100`, where <base> and
// <scale> vary by class and skill family.
constexpr int ENERGY_REQ_BASE_DEFAULT = 20;
constexpr int ENERGY_REQ_BASE_KNIGHT = 10;
constexpr int ENERGY_REQ_SCALE_DEFAULT_PERCENT = 4;
constexpr int ENERGY_REQ_SCALE_SUMMON_PERCENT = 3;
} // namespace

CSkillManager::CSkillManager(SessionKeeper &keeper) // OK
    : SessionLegacyCalls(keeper), gMapManager(keeper.MapManagerObject()),
      m_cachedEmpireGuardian(false)
{
    m_bSkillAttributeRequirementsCacheDirty = true;
    memset(m_aSkillAttributeRequirementsMet, 0, sizeof(m_aSkillAttributeRequirementsMet));
}

CSkillManager::~CSkillManager() // OK
{
}

bool CSkillManager::FindHeroSkill(ActionSkillType eSkillType)
{
    for (int i = 0; i < CharacterAttribute->SkillNumber; ++i)
    {
        if (CharacterAttribute->Skill[i] == eSkillType)
        {
            return true;
        }
    }
    return false;
}

void CSkillManager::GetSkillInformation(int iType, int iLevel, wchar_t *lpszName, int *piMana,
                                        int *piDistance, int *piSkillMana)
{
    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];
    if (lpszName)
    {
        wcscpy(lpszName, p->Name);
        // int wchars_num = MultiByteToWideChar(CP_UTF8, 0, p->Name, -1, NULL, 0);
        // MultiByteToWideChar(CP_UTF8, 0, p->Name, -1, lpszName, wchars_num);
    }
    if (piMana)
    {
        *piMana = p->Mana;
    }
    if (piDistance)
    {
        *piDistance = p->Distance;
    }
    if (piSkillMana)
    {
        *piSkillMana = p->AbilityGuage;
    }
}

void CSkillManager::GetSkillInformation_Energy(int iType, int *piEnergy)
{
    if (!piEnergy)
        return;

    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];

    // Skills with no energy cost in the BMD are free regardless of their
    // character-level requirement. Without this short-circuit the formula
    // below would charge a fixed ENERGY_REQ_BASE_DEFAULT (20) for any
    // Level>0 skill that the BMD author left at Energy=0, which gates many
    // low-tier or special skills incorrectly.
    if (p->Energy == 0)
    {
        *piEnergy = 0;
        return;
    }

    // BMD Energy is dual-purpose. For Level=0 skills it's the direct cost
    // (Summon Goblin=30, Summon Soldier=300, etc.). For Level>0 skills it's
    // a per-level scaling factor that the formula multiplies up.
    if (p->Level == 0)
    {
        *piEnergy = p->Energy;
        return;
    }

    *piEnergy =
        ENERGY_REQ_BASE_DEFAULT + (p->Energy * p->Level * ENERGY_REQ_SCALE_DEFAULT_PERCENT / 100);

    if (iType == AT_SKILL_SUMMON_EXPLOSION || iType == AT_SKILL_SUMMON_REQUIEM)
    {
        *piEnergy = ENERGY_REQ_BASE_DEFAULT +
                    (p->Energy * p->Level * ENERGY_REQ_SCALE_SUMMON_PERCENT / 100);
    }

    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_KNIGHT)
    {
        *piEnergy = ENERGY_REQ_BASE_KNIGHT +
                    (p->Energy * p->Level * ENERGY_REQ_SCALE_DEFAULT_PERCENT / 100);
    }
}

void CSkillManager::GetSkillInformation_Charisma(int iType, int *piCharisma)
{
    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];

    if (piCharisma)
    {
        *piCharisma = p->Charisma;
    }
}

void CSkillManager::GetSkillInformation_Damage(int iType, int *piDamage)
{
    SKILL_ATTRIBUTE *p = &SkillAttribute[iType];

    if (piDamage)
    {
        *piDamage = p->Damage;
    }
}

float CSkillManager::GetSkillDistance(int Index, CHARACTER *c)
{
    auto Distance = (float)(SkillAttribute[Index].Distance);

    if (c != nullptr && c->Helper.Type == MODEL_DARK_HORSE_ITEM)
    {
        Distance += 2;
    }

    return Distance;
}

bool CSkillManager::CheckSkillDelay(int SkillIndex)
{
    int Skill = CharacterAttribute->Skill[SkillIndex];

    int Delay = SkillAttribute[Skill].Delay;

    if (!CheckAttack() && (Skill == AT_SKILL_CHAIN_DRIVE || Skill == AT_SKILL_CHAIN_DRIVE_STR ||
                           Skill == AT_SKILL_DRAGON_ROAR || Skill == AT_SKILL_DRAGON_ROAR_STR ||
                           Skill == AT_SKILL_DRAGON_KICK))
    {
        return false;
    }

    if (Delay > 0)
    {
        if (CharacterAttribute->SkillDelay[SkillIndex] > 0)
        {
            return false;
        }

        int iCharisma;
        GetSkillInformation_Charisma(Skill, &iCharisma);
        if (iCharisma > (CharacterAttribute->Charisma + CharacterAttribute->AddCharisma))
        {
            return false;
        }

        CharacterAttribute->SkillDelay[SkillIndex] = Delay;
    }
    return true;
}
void CSkillManager::CalcSkillDelay(int time)
{
    int iSkillNumber;
    iSkillNumber = CharacterAttribute->SkillNumber + 2;
    iSkillNumber = std::min<int>(iSkillNumber, MAX_SKILLS);

    for (int i = 0; i < iSkillNumber; ++i)
    {
        if (CharacterAttribute->SkillDelay[i] <= 0)
            continue;

        CharacterAttribute->SkillDelay[i] -= time;
        if (CharacterAttribute->SkillDelay[i] < 0)
        {
            CharacterAttribute->SkillDelay[i] = 0;
        }
    }
}

BYTE CSkillManager::GetSkillMasteryType(ActionSkillType iType)
{
    BYTE MasteryType = 255;
    if (const SKILL_ATTRIBUTE *p = &SkillAttribute[iType])
    {
        MasteryType = p->MasteryType;
    }

    return MasteryType;
}

ActionSkillType CSkillManager::MasterSkillToBaseSkillIndex(ActionSkillType masterSkill)
{
    auto baseSkill = masterSkill;

    while (true)
    {
        if (auto search = SKILL_REPLACEMENTS.find(baseSkill); search != SKILL_REPLACEMENTS.end())
        {
            baseSkill = search->second;
        }
        else
        {
            break;
        }
    }

    return baseSkill;
}

bool CSkillManager::skillVScharactorCheck(const DemendConditionInfo &basicInfo,
                                          const DemendConditionInfo &heroInfo)
{
    if (basicInfo <= heroInfo)
    {
        return true;
    }
    return false;
}

bool CSkillManager::AreSkillAttributeRequirementsMet(ActionSkillType skillType)
{
    if (skillType >= MAX_SKILLS)
    {
        return false;
    }

    const bool isGuardian = gMapManager.IsEmpireGuardian();
    const DemendConditionInfo currentHeroRequirements = [this] {
        DemendConditionInfo info;
        info.SkillLevel = CharacterMachine->Character.Level;
        info.SkillStrength =
            CharacterMachine->Character.Strength + CharacterMachine->Character.AddStrength;
        info.SkillDexterity =
            CharacterMachine->Character.Dexterity + CharacterMachine->Character.AddDexterity;
        info.SkillVitality =
            CharacterMachine->Character.Vitality + CharacterMachine->Character.AddVitality;
        info.SkillEnergy =
            CharacterMachine->Character.Energy + CharacterMachine->Character.AddEnergy;
        info.SkillCharisma =
            CharacterMachine->Character.Charisma + CharacterMachine->Character.AddCharisma;
        return info;
    }();
    if (m_cachedEmpireGuardian != isGuardian ||
        m_cachedHeroRequirements.SkillLevel != currentHeroRequirements.SkillLevel ||
        m_cachedHeroRequirements.SkillStrength != currentHeroRequirements.SkillStrength ||
        m_cachedHeroRequirements.SkillDexterity != currentHeroRequirements.SkillDexterity ||
        m_cachedHeroRequirements.SkillVitality != currentHeroRequirements.SkillVitality ||
        m_cachedHeroRequirements.SkillEnergy != currentHeroRequirements.SkillEnergy ||
        m_cachedHeroRequirements.SkillCharisma != currentHeroRequirements.SkillCharisma)
    {
        m_bSkillAttributeRequirementsCacheDirty = true;
    }

    // Rebuild cache if dirty (on first call after stat, world, or skill changes).
    if (m_bSkillAttributeRequirementsCacheDirty)
    {
        RebuildSkillAttributeRequirementsCache();
    }

    return m_aSkillAttributeRequirementsMet[skillType];
}

void CSkillManager::InvalidateSkillAttributeRequirementsCache()
{
    m_bSkillAttributeRequirementsCacheDirty = true;
}

void CSkillManager::InitializeSkillAttributeRequirementsCache()
{
    m_bSkillAttributeRequirementsCacheDirty = true;
    RebuildSkillAttributeRequirementsCache();
}

void CSkillManager::RebuildSkillAttributeRequirementsCache()
{
    if (!m_bSkillAttributeRequirementsCacheDirty)
    {
        return;
    }

    const bool isGuardian = gMapManager.IsEmpireGuardian();

    DemendConditionInfo heroCharacterInfo;
    heroCharacterInfo.SkillLevel = CharacterMachine->Character.Level;
    heroCharacterInfo.SkillStrength =
        CharacterMachine->Character.Strength + CharacterMachine->Character.AddStrength;
    heroCharacterInfo.SkillDexterity =
        CharacterMachine->Character.Dexterity + CharacterMachine->Character.AddDexterity;
    heroCharacterInfo.SkillVitality =
        CharacterMachine->Character.Vitality + CharacterMachine->Character.AddVitality;
    heroCharacterInfo.SkillEnergy =
        CharacterMachine->Character.Energy + CharacterMachine->Character.AddEnergy;
    heroCharacterInfo.SkillCharisma =
        CharacterMachine->Character.Charisma + CharacterMachine->Character.AddCharisma;

    m_cachedEmpireGuardian = isGuardian;
    m_cachedHeroRequirements = heroCharacterInfo;

    for (int skillType = 0; skillType < MAX_SKILLS; ++skillType)
    {
        ActionSkillType baseSkill =
            MasterSkillToBaseSkillIndex(static_cast<ActionSkillType>(skillType));
        if (isGuardian && (baseSkill == AT_SKILL_TELEPORT_ALLY || baseSkill == AT_SKILL_TELEPORT))
        {
            m_aSkillAttributeRequirementsMet[skillType] = false;
            continue;
        }

        DemendConditionInfo skillRequirements;
        skillRequirements.SkillLevel = SkillAttribute[baseSkill].Level;
        skillRequirements.SkillStrength = SkillAttribute[baseSkill].Strength;
        skillRequirements.SkillDexterity = SkillAttribute[baseSkill].Dexterity;
        skillRequirements.SkillVitality = 0;
        int reqEnergy = 0;
        GetSkillInformation_Energy(baseSkill, &reqEnergy);
        skillRequirements.SkillEnergy = static_cast<WORD>(reqEnergy);
        skillRequirements.SkillCharisma = SkillAttribute[baseSkill].Charisma;

        m_aSkillAttributeRequirementsMet[skillType] = (skillRequirements <= heroCharacterInfo);
    }

    m_bSkillAttributeRequirementsCacheDirty = false;
}

int CSkillManager::GetSkillIndex(int iSkillType) const
{
    // special handling for skills with different skill id for the trigger
    if (iSkillType == AT_SKILL_NOVA_BEGIN)
    {
        iSkillType = AT_SKILL_NOVA;
    }

    int iReturn = -1;
    for (int i = 0; i < MAX_MAGIC; ++i)
    {
        if (CharacterAttribute->Skill[i] == iSkillType)
        {
            iReturn = i;
            break;
        }
    }

    return iReturn;
}

void CSkillManager::SelectHeroSkill(int slot)
{
    priorSkill_ = CharacterAttribute->Skill[Hero->CurrentSkill];
    Hero->CurrentSkill = slot;
}

SessionItemStore::SessionItemStore(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_SocketItemMgr(keeper.SocketItemManager())
{
    m_dwAlternate = 0;
    m_dwAvailableKeyStream = 0x80000000;
}

SessionItemStore::~SessionItemStore()
{
    DeleteAllItems();
}

ITEM *SessionItemStore::CreateItem(std::span<const BYTE> itemData)
{

    return CreateItemExtended(itemData);
}

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
ITEM *SessionItemStore::CreateItemExtended(std::span<const BYTE> itemData)
{
    const auto params = GameLogic::Items::ParseItemData(itemData);
    return params ? CreateItemByParameters(&*params) : nullptr;
}

ITEM *SessionItemStore::CreateItemOld(std::span<const BYTE> pbyItemPacket)
{
    WORD wType = ExtractItemType(pbyItemPacket);
    BYTE byOption380 = 0, byOptionHarmony = 0;

    byOption380 = pbyItemPacket[5];
    byOptionHarmony = pbyItemPacket[6];

    BYTE bySocketOption[5] = {pbyItemPacket[7], pbyItemPacket[8], pbyItemPacket[9],
                              pbyItemPacket[10], pbyItemPacket[11]};

    return SessionItemStore::CreateItem(
        wType / MAX_ITEM_INDEX, wType % MAX_ITEM_INDEX, pbyItemPacket[1], pbyItemPacket[2],
        pbyItemPacket[3], pbyItemPacket[4], byOption380, byOptionHarmony, bySocketOption);
}

ITEM *SessionItemStore::CreateItemByParameters(const ItemCreationParams *parameters)
{
    if (parameters == nullptr)
    {
        return nullptr;
    }

    ITEM *pNewItem = new ITEM;
    memset(pNewItem, 0, sizeof(ITEM));
    pNewItem->RefCount = 1;
    pNewItem->bPeriodItem = parameters->WithExpiration;
    pNewItem->bExpiredPeriod = parameters->IsExpired;
    // pNewItem->lExpireTime is received by another packet? should we integrate that?
    pNewItem->Key = GenerateItemKey();
    pNewItem->Type = parameters->Group * MAX_ITEM_INDEX + parameters->Number;
    pNewItem->Level = parameters->Level;
    pNewItem->Durability = parameters->Durability;
    pNewItem->HasLuck = parameters->WithLuck;
    pNewItem->HasSkill = parameters->WithSkill;
    pNewItem->OptionType = parameters->OptionType;
    pNewItem->OptionLevel = parameters->OptionLevel;
    pNewItem->ExcellentFlags = parameters->ExcellentFlags;
    pNewItem->AncientDiscriminator = parameters->AncientDiscriminator;
    pNewItem->AncientBonusOption = parameters->AncientBonusOption;
    pNewItem->Jewel_Of_Harmony_Option = parameters->HarmonyOptionType;
    pNewItem->Jewel_Of_Harmony_OptionLevel = parameters->HarmonyOptionLevel;
    pNewItem->option_380 = parameters->HasGuardianOption;
    pNewItem->SocketCount = parameters->SocketCount;
    pNewItem->SocketSeedSetOption = parameters->SocketBonusOption;
    for (int i = 0; i < MAX_SOCKETS; ++i)
    {
        pNewItem->bySocketOption[i] = parameters->SocketOptions[i];
        if (pNewItem->bySocketOption[i] == SOCKET_EMPTY)
        {
            pNewItem->SocketSeedID[i] = SOCKET_EMPTY;
        }
        else
        {
            pNewItem->SocketSeedID[i] = pNewItem->bySocketOption[i] % SEASON4A::MAX_SOCKET_OPTION;
            pNewItem->SocketSphereLv[i] =
                (pNewItem->bySocketOption[i] / SEASON4A::MAX_SOCKET_OPTION) + 1;
        }
    }

    pNewItem->byColorState = ITEM_COLOR_NORMAL;

    SetItemAttributes(pNewItem);
    // SetItemAttr(pNewItem, byLevel, byOption1, ancientByte);

    m_listItem.push_back(pNewItem);
    return pNewItem;
}

ITEM *SessionItemStore::CreateItem(BYTE byType, BYTE bySubType, BYTE byLevel, BYTE byDurability,
                                   BYTE byOption1, BYTE ancientByte, BYTE byOption380,
                                   BYTE byOptionHarmony, BYTE *pbySocketOptions)
{
    ITEM *pNewItem = new ITEM;
    memset(pNewItem, 0, sizeof(ITEM));

    WORD wType = byType * MAX_ITEM_INDEX + bySubType;
    pNewItem->Key = GenerateItemKey();
    pNewItem->Type = wType;
    pNewItem->Durability = byDurability;
    pNewItem->ExcellentFlags = byOption1;
    pNewItem->AncientDiscriminator = ancientByte & 0x03;
    pNewItem->AncientBonusOption = (ancientByte & (0x04 + 0x08)) >> 2;
    if ((((byOption380 & 0x08) << 4) >> 7) > 0)
        pNewItem->option_380 = true;
    else
        pNewItem->option_380 = false;
    pNewItem->Jewel_Of_Harmony_Option = (byOptionHarmony & 0xf0) >> 4;
    pNewItem->Jewel_Of_Harmony_OptionLevel = byOptionHarmony & 0x0f;

    if (pbySocketOptions == NULL)
    {
        pNewItem->SocketCount = 0;
        assert(!"Socket options expected but missing");
    }
    else
    {
        pNewItem->SocketCount = MAX_SOCKETS;

        for (int i = 0; i < MAX_SOCKETS; ++i)
        {
            pNewItem->bySocketOption[i] = pbySocketOptions[i];
        }

        for (int i = 0; i < MAX_SOCKETS; ++i)
        {
            if (pbySocketOptions[i] == 0xFF)
            {
                pNewItem->SocketCount = i;
                break;
            }
            else if (pbySocketOptions[i] == 0xFE)
            {
                pNewItem->SocketSeedID[i] = SOCKET_EMPTY;
            }
            else
            {
                pNewItem->SocketSeedID[i] = pbySocketOptions[i] % SEASON4A::MAX_SOCKET_OPTION;
                pNewItem->SocketSphereLv[i] =
                    int(pbySocketOptions[i] / SEASON4A::MAX_SOCKET_OPTION) + 1;
            }
        }

        if (g_SocketItemMgr.IsSocketItem(pNewItem))
        {
            pNewItem->SocketSeedSetOption = byOptionHarmony;
            pNewItem->Jewel_Of_Harmony_Option = 0;
            pNewItem->Jewel_Of_Harmony_OptionLevel = 0;
        }
        else
        {
            pNewItem->SocketSeedSetOption = SOCKET_EMPTY;
        }
    }

    pNewItem->byColorState = ITEM_COLOR_NORMAL;

    pNewItem->RefCount = 1;

    if (((byOption380 & 0x02) >> 1) > 0)
    {
        pNewItem->bPeriodItem = true;
    }
    else
    {
        pNewItem->bPeriodItem = false;
    }

    if (((byOption380 & 0x04) >> 2) > 0)
    {
        pNewItem->bExpiredPeriod = true;
    }
    else
    {
        pNewItem->bExpiredPeriod = false;
    }

    SetItemAttributes(pNewItem);

    m_listItem.push_back(pNewItem);
    return pNewItem;
}

ITEM *SessionItemStore::CreateItem(ITEM *pItem)
{
    pItem->RefCount++;
    return pItem;
}

ITEM *SessionItemStore::DuplicateItem(ITEM *pItem)
{
    ITEM *pNewItem = new ITEM;
    memcpy(pNewItem, pItem, sizeof(ITEM));
    pNewItem->Key = GenerateItemKey();
    pNewItem->RefCount = 1;
    m_listItem.push_back(pNewItem);
    return pNewItem;
}

void SessionItemStore::DeleteItem(ITEM *pItem)
{
    if (pItem == NULL)
        return;

    if (--pItem->RefCount <= 0)
    {
        auto li = m_listItem.begin();
        for (; li != m_listItem.end(); li++)
        {
            if ((*li) == pItem)
            {
                SAFE_DELETE(*li);
                m_listItem.erase(li);
                break;
            }
        }
    }
}

void SessionItemStore::DeleteDuplicatedItem(ITEM *item)
{
    DeleteItem(item);
}

void SessionItemStore::DeleteAllItems()
{
    auto li = m_listItem.begin();
    for (; li != m_listItem.end(); li++)
    {
        SAFE_DELETE(*li);
    }
    m_listItem.clear();

    m_dwAlternate = 0;
    m_dwAvailableKeyStream = 0x80000000;
}

bool SessionItemStore::IsEmpty()
{
    return m_listItem.empty();
}

DWORD SessionItemStore::GenerateItemKey()
{
    DWORD dwAvailableItemKey = FindAvailableKeyIndex(m_dwAvailableKeyStream);
    if (dwAvailableItemKey >= 0x8F000000)
    {
        m_dwAvailableKeyStream = 0;
        m_dwAlternate++;
        dwAvailableItemKey = FindAvailableKeyIndex(m_dwAvailableKeyStream);
    }
    return m_dwAvailableKeyStream = dwAvailableItemKey;
}

DWORD SessionItemStore::FindAvailableKeyIndex(DWORD dwSeed)
{
    if (m_dwAlternate > 0)
    {
        auto li = m_listItem.begin();
        for (; li != m_listItem.end(); li++)
        {
            ITEM *pItem = (*li);
            if (pItem->Key == dwSeed + 1)
                return FindAvailableKeyIndex(dwSeed + 1);
        }
    }
    return dwSeed + 1;
}

WORD SessionItemStore::ExtractItemType(std::span<const BYTE> pbyItemPacket)
{
    return pbyItemPacket[0] + (pbyItemPacket[3] & 128) * 2 + (pbyItemPacket[5] & 240) * 32;
}

bool SessionUiUnit::CheckUseMasterSkill(CHARACTER *c, int Index)
{
    BYTE GuildStatus = c->GuildStatus;
    BYTE Class = gCharacterManager.GetBaseClass(c->Class);
    bool bUse = false;

    if (SkillAttribute[Index].RequireClass[Class] != 0)
    {
        if ((GuildStatus == G_MASTER && SkillAttribute[Index].RequireDutyClass[0]) ||
            (GuildStatus == G_SUB_MASTER && SkillAttribute[Index].RequireDutyClass[1]) ||
            (GuildStatus == G_BATTLE_MASTER && SkillAttribute[Index].RequireDutyClass[2]))
        {
            bUse = true;
        }
    }

    return bUse;
}

bool SessionLegacyCalls::CheckUseMasterSkill(CHARACTER *character, int index)
{
    return sessionKeeper_.Ui()->CheckUseMasterSkill(character, index);
}

void SessionUiUnit::UseBattleMasterSkill(void)
{
    if (!(Hero->EtcPart == PARTS_ATTACK_TEAM_MARK || Hero->EtcPart == PARTS_ATTACK_TEAM_MARK2 ||
          Hero->EtcPart == PARTS_ATTACK_TEAM_MARK3 || Hero->EtcPart == PARTS_DEFENSE_TEAM_MARK))
    {
        return;
    }

    if (Hero->GuildStatus == G_PERSON)
    {
        return;
    }

    int MaxKillCount = SkillAttribute[Hero->GuildSkill].KillCount;

    if (Hero->GuildMasterKillCount >= MaxKillCount)
    {
        if (IsKeyDown(VK_SHIFT))
        {
            if (Hero->BackupCurrentSkill == 255)
            {
                Hero->BackupCurrentSkill = Hero->CurrentSkill;
            }
            Hero->CurrentSkill = FindHotKey(Hero->GuildSkill);
        }
        else
        {
            if (Hero->BackupCurrentSkill != 255)
            {
                Hero->CurrentSkill = Hero->BackupCurrentSkill;
                Hero->BackupCurrentSkill = 255;
            }
        }
    }
}

void SessionLegacyCalls::UseBattleMasterSkill()
{
    sessionKeeper_.Ui()->UseBattleMasterSkill();
}

using namespace SEASON4A;

CSocketItemMgr::CSocketItemMgr(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_ErrorReport(keeper.ErrorReport())
{
    m_iNumEquitSetBonusOptions = 0;
    memset(m_SocketOptionInfo, 0, sizeof(SOCKET_OPTION_INFO) * MAX_SOCKET_OPTION);
    memset(&m_StatusBonus, 0, sizeof(SOCKET_OPTION_STATUS_BONUS));
}

CSocketItemMgr::~CSocketItemMgr()
{
}

BOOL CSocketItemMgr::IsSocketItem(const ITEM *pItem)
{
    return IsSocketItem(pItem->Type);
}

BOOL CSocketItemMgr::IsSocketItem(const OBJECT *pObject)
{
    return IsSocketItem(pObject->Type - MODEL_SWORD);
}

BOOL CSocketItemMgr::IsSocketItem(int iItemType)
{
    switch (iItemType)
    {
    case ITEM_FLAMBERGE:
    case ITEM_SWORD_BREAKER:
    case ITEM_IMPERIAL_SWORD:
    case ITEM_FROST_MACE:
    case ITEM_ABSOLUTE_SCEPTER:
    case ITEM_STINGER_BOW:
    case ITEM_DEADLY_STAFF:
    case ITEM_IMPERIAL_STAFF:
    case ITEM_STAFF + 32:
    case ITEM_CRIMSONGLORY:
    case ITEM_SALAMANDER_SHIELD:
    case ITEM_FROST_BARRIER:
    case ITEM_GUARDIAN_SHILED:
    case ITEM_TITAN_HELM:
    case ITEM_TITAN_ARMOR:
    case ITEM_TITAN_PANTS:
    case ITEM_TITAN_GLOVES:
    case ITEM_TITAN_BOOTS:
    case ITEM_BRAVE_HELM:
    case ITEM_BRAVE_ARMOR:
    case ITEM_BRAVE_PANTS:
    case ITEM_BRAVE_GLOVES:
    case ITEM_BRAVE_BOOTS:
    case ITEM_HELM + 47:
    case ITEM_DESTORY_ARMOR:
    case ITEM_DESTORY_PANTS:
    case ITEM_DESTORY_GLOVES:
    case ITEM_DESTORY_BOOTS:
    case ITEM_HELM + 48:
    case ITEM_PHANTOM_ARMOR:
    case ITEM_PHANTOM_PANTS:
    case ITEM_PHANTOM_GLOVES:
    case ITEM_PHANTOM_BOOTS:
    case ITEM_SERAPHIM_HELM:
    case ITEM_SERAPHIM_ARMOR:
    case ITEM_SERAPHIM_PANTS:
    case ITEM_SERAPHIM_GLOVES:
    case ITEM_SERAPHIM_BOOTS:
    case ITEM_FAITH_HELM:
    case ITEM_FAITH_ARMOR:
    case ITEM_FAITH_PANTS:
    case ITEM_FAITH_GLOVES:
    case ITEM_FAITH_BOOTS:
    case ITEM_PAEWANG_MASK:
    case ITEM_PAEWANG_ARMOR:
    case ITEM_PAEWANG_PANTS:
    case ITEM_PAEWANG_GLOVES:
    case ITEM_PHAEWANG_BOOTS:
    case ITEM_HADES_HELM:
    case ITEM_HADES_ARMOR:
    case ITEM_HADES_PANTS:
    case ITEM_HADES_GLOVES:
    case ITEM_HADES_BOOTS:
    case ITEM_HELM + 53:
    case ITEM_ARMOR + 53:
    case ITEM_PANTS + 53:
    case ITEM_GLOVES + 53:
    case ITEM_BOOTS + 53:
        return TRUE;
    default:
        return FALSE;
    }
    // 	return (pItem->SocketCount > 0);
}

int CSocketItemMgr::GetSocketCategory(int iSeedID)
{
    if (iSeedID == SOCKET_EMPTY)
        return SOCKET_EMPTY;

    SOCKET_OPTION_INFO *pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][iSeedID];
    return pInfo->m_iOptionCategory;
}

int CSocketItemMgr::GetSeedShpereSeedID(const ITEM *pItem)
{
    BYTE bySocketSeedID = SOCKET_EMPTY;

    if (pItem->Type >= ITEM_SEED_SPHERE_FIRE_1 && pItem->Type <= ITEM_SEED_SPHERE_EARTH_5)
    {
        int iCategoryIndex = (pItem->Type - (ITEM_SEED_SPHERE_FIRE_1)) % 6 + 1;
        int iLevel = pItem->Level;
        switch (iCategoryIndex)
        {
        case 1: // 0~9
            bySocketSeedID = 0 + iLevel;
            break;
        case 2: // 10~15
            bySocketSeedID = 10 + iLevel;
            break;
        case 3: // 16~20
            bySocketSeedID = 16 + iLevel;
            break;
        case 4: // 21~28
            bySocketSeedID = 21 + iLevel;
            break;
        case 5: // 29~33
            bySocketSeedID = 29 + iLevel;
            break;
        case 6: // 34~40
            bySocketSeedID = 34 + iLevel;
            break;
        }
    }

    return bySocketSeedID;
}

__int64 CSocketItemMgr::CalcSocketBonusItemValue(const ITEM *pItem, __int64 iOrgGold)
{
    __int64 iGoldResult = 0;

    if (IsSocketItem(pItem))
    {
        iGoldResult += iOrgGold * (pItem->SocketCount * 0.8f);

        ITEM TempSeedSphere;
        for (int i = 0; i < pItem->SocketCount; ++i)
        {
            if (pItem->SocketSeedID[i] == SOCKET_EMPTY)
                continue;

            int iSeedSphereType = 0;
            if (pItem->SocketSeedID[i] >= 0 && pItem->SocketSeedID[i] <= 9)
                iSeedSphereType = 0;
            else if (pItem->SocketSeedID[i] >= 10 && pItem->SocketSeedID[i] <= 15)
                iSeedSphereType = 1;
            else if (pItem->SocketSeedID[i] >= 16 && pItem->SocketSeedID[i] <= 20)
                iSeedSphereType = 2;
            else if (pItem->SocketSeedID[i] >= 21 && pItem->SocketSeedID[i] <= 28)
                iSeedSphereType = 3;
            else if (pItem->SocketSeedID[i] >= 29 && pItem->SocketSeedID[i] <= 33)
                iSeedSphereType = 4;
            else if (pItem->SocketSeedID[i] >= 34 && pItem->SocketSeedID[i] <= 40)
                iSeedSphereType = 5;

            TempSeedSphere.Type = ITEM_SEED_SPHERE_FIRE_1 +
                                  (pItem->SocketSphereLv[i] - 1) * MAX_SOCKET_TYPES +
                                  iSeedSphereType;
            iGoldResult += ItemValue(&TempSeedSphere, 0);
        }
    }

    return iGoldResult;
}

int CSocketItemMgr::CalcSocketOptionValue(int iOptionType, float fOptionValue)
{
    switch (iOptionType)
    {
    case 1:
        return int(fOptionValue);
    case 2:
        return int(fOptionValue);
    case 3: {
        WORD wLevel;

        if (gCharacterManager.IsMasterLevel(CharacterAttribute->Class) == true)
            wLevel = CharacterAttribute->Level + Master_Level_Data.nMLevel;
        else
            wLevel = CharacterAttribute->Level;

        return int((float)wLevel / fOptionValue);
    }
    case 4: {
        DWORD wLifeMax;

        if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
            wLifeMax = Master_Level_Data.wMaxLife;
        else
            wLifeMax = CharacterAttribute->LifeMax;

        return int((float)wLifeMax / fOptionValue);
    }
    case 5: {
        DWORD wManaMax;
        if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
            wManaMax = Master_Level_Data.wMaxMana;
        else
            wManaMax = CharacterAttribute->ManaMax;

        return int((float)wManaMax / fOptionValue);
    }
    default:
        return 0;
    }
}

void CSocketItemMgr::CalcSocketOptionValueText(wchar_t *pszOptionValueText, int iOptionType,
                                               float fOptionValue)
{
    switch (iOptionType)
    {
    case 2:
        mu_swprintf(pszOptionValueText, L"+%d%%", CalcSocketOptionValue(iOptionType, fOptionValue));
        break;
    default:
        mu_swprintf(pszOptionValueText, L"+%d", CalcSocketOptionValue(iOptionType, fOptionValue));
        break;
    }
}

void CSocketItemMgr::CreateSocketOptionText(wchar_t *pszOptionText, int iSeedID, int iSphereLv)
{
    if (pszOptionText == NULL)
        return;

    wchar_t szOptionValueText[16] = {
        0,
    };

    SOCKET_OPTION_INFO *pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][iSeedID];

    auto fOptionValue = (float)pInfo->m_iOptionValue[iSphereLv - 1];

    CalcSocketOptionValueText(szOptionValueText, pInfo->m_bOptionType, fOptionValue);

    mu_swprintf(pszOptionText, L"%ls(%ls %ls)",
                I18N::Game::Lookup(2640 + pInfo->m_iOptionCategory - 1), pInfo->m_szOptionName,
                szOptionValueText);
}

int CSocketItemMgr::AttachToolTipForSocketItem(const ITEM *pItem, int iTextNum)
{
    if (pItem->SocketCount == 0)
        return iTextNum;

    mu_swprintf(TextList[iTextNum], L"\n");
    ++iTextNum;
    ++SkipNum;
    mu_swprintf(TextList[iTextNum], L"%ls %ls", I18N::Game::Socket, I18N::Game::ItemOptionInfo);
    TextListColor[iTextNum] = TEXT_COLOR_PURPLE;
    TextBold[iTextNum] = false;
    ++iTextNum;
    mu_swprintf(TextList[iTextNum], L"\n");
    ++iTextNum;
    ++SkipNum;

    wchar_t szOptionText[64] = {
        0,
    };
    wchar_t szOptionValueText[16] = {
        0,
    };

    for (int i = 0; i < pItem->SocketCount; ++i)
    {
        if (pItem->SocketSeedID[i] == SOCKET_EMPTY)
        {
            mu_swprintf(szOptionText, I18N::Game::NoItemApplication);
            TextListColor[iTextNum] = TEXT_COLOR_GRAY;
        }
        else if (pItem->SocketSeedID[i] < MAX_SOCKET_OPTION)
        {
            CreateSocketOptionText(szOptionText, pItem->SocketSeedID[i], pItem->SocketSphereLv[i]);
            TextListColor[iTextNum] = TEXT_COLOR_BLUE;
        }
        else
        {
            assert(!"Socket index error");
        }

        mu_swprintf(TextList[iTextNum], I18N::Game::SocketDS, i + 1, szOptionText);
        TextBold[iTextNum] = false;
        ++iTextNum;
    }

    if (pItem->SocketSeedSetOption < MAX_SOCKET_OPTION)
    {
        SOCKET_OPTION_INFO *pInfo =
            &m_SocketOptionInfo[SOT_MIX_SET_BONUS_OPTIONS][pItem->SocketSeedSetOption];
        if (pInfo && pInfo->m_iOptionValue[0] > 0)
        {
            mu_swprintf(TextList[iTextNum], L"\n");
            ++iTextNum;
            ++SkipNum;

            mu_swprintf(TextList[iTextNum], L"%ls", I18N::Game::BonusSocketOption);
            TextListColor[iTextNum] = TEXT_COLOR_PURPLE;
            TextBold[iTextNum] = false;
            ++iTextNum;
            mu_swprintf(TextList[iTextNum], L"\n");
            ++iTextNum;
            ++SkipNum;

            CalcSocketOptionValueText(szOptionValueText, pInfo->m_bOptionType,
                                      (float)pInfo->m_iOptionValue[0]);
            mu_swprintf(TextList[iTextNum], L"%ls %ls", pInfo->m_szOptionName, szOptionValueText);
            TextListColor[iTextNum] = TEXT_COLOR_BLUE;
            TextBold[iTextNum] = false;
            ++iTextNum;
        }
    }
    return iTextNum;
}

int CSocketItemMgr::AttachToolTipForSeedSphereItem(const ITEM *pItem, int iTextNum)
{
    SOCKET_OPTION_INFO *pInfo = NULL;

    if (pItem->Type >= ITEM_SEED_FIRE && pItem->Type <= ITEM_SEED_EARTH)
    {
        int iCategoryIndex = pItem->Type - (ITEM_SEED_FIRE) + 1;
        mu_swprintf(TextList[iTextNum], I18N::Game::ElementS,
                    I18N::Game::Lookup(2640 + iCategoryIndex - 1));
        TextListColor[iTextNum] = TEXT_COLOR_WHITE;
        TextBold[iTextNum] = false;
        ++iTextNum;

        mu_swprintf(TextList[iTextNum], L"\n");
        ++iTextNum;
        ++SkipNum;

        int iSocketSeedID = 0;
        int iLevel = pItem->Level;
        switch (iCategoryIndex)
        {
        case 1: // 0~9
            iSocketSeedID = 0 + iLevel;
            break;
        case 2: // 10~15
            iSocketSeedID = 10 + iLevel;
            break;
        case 3: // 16~20
            iSocketSeedID = 16 + iLevel;
            break;
        case 4: // 21~28
            iSocketSeedID = 21 + iLevel;
            break;
        case 5: // 29~33
            iSocketSeedID = 29 + iLevel;
            break;
        case 6: // 34~40
            iSocketSeedID = 34 + iLevel;
            break;
        }
        pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][iSocketSeedID];
        mu_swprintf(TextList[iTextNum], pInfo->m_szOptionName);
        TextListColor[iTextNum] = TEXT_COLOR_BLUE;
        TextBold[iTextNum] = false;
        ++iTextNum;
    }
    else if (pItem->Type >= ITEM_SPHERE_MONO && pItem->Type <= ITEM_SPHERE_5)
    {
        int iSphereLevel = pItem->Type - (ITEM_SPHERE_MONO) + 1;
        mu_swprintf(TextList[iTextNum], I18N::Game::LevelD, iSphereLevel);
        TextListColor[iTextNum] = TEXT_COLOR_WHITE;
        TextBold[iTextNum] = false;
        ++iTextNum;
    }
    else if (pItem->Type >= ITEM_SEED_SPHERE_FIRE_1 && pItem->Type <= ITEM_SEED_SPHERE_EARTH_5)
    {
        int iCategoryIndex = (pItem->Type - (ITEM_SEED_SPHERE_FIRE_1)) % 6 + 1;
        mu_swprintf(TextList[iTextNum], I18N::Game::ElementS,
                    I18N::Game::Lookup(2640 + iCategoryIndex - 1));
        TextListColor[iTextNum] = TEXT_COLOR_WHITE;
        TextBold[iTextNum] = false;
        ++iTextNum;

        mu_swprintf(TextList[iTextNum], L"\n");
        ++iTextNum;
        ++SkipNum;

        int iSocketSeedID = 0;
        int iLevel = pItem->Level;
        switch (iCategoryIndex)
        {
        case 1: // 0~9
            iSocketSeedID = 0 + iLevel;
            break;
        case 2: // 10~15
            iSocketSeedID = 10 + iLevel;
            break;
        case 3: // 16~20
            iSocketSeedID = 16 + iLevel;
            break;
        case 4: // 21~28
            iSocketSeedID = 21 + iLevel;
            break;
        case 5: // 29~33
            iSocketSeedID = 29 + iLevel;
            break;
        case 6: // 34~40
            iSocketSeedID = 34 + iLevel;
            break;
        }

        pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][iSocketSeedID];

        wchar_t szOptionValueText[16] = {
            0,
        };

        auto fOptionValue =
            (float)pInfo->m_iOptionValue[(pItem->Type - (ITEM_SEED_SPHERE_FIRE_1)) / 6];
        CalcSocketOptionValueText(szOptionValueText, pInfo->m_bOptionType, fOptionValue);

        mu_swprintf(TextList[iTextNum], L"%ls %ls", pInfo->m_szOptionName, szOptionValueText);
        TextListColor[iTextNum] = TEXT_COLOR_BLUE;
        TextBold[iTextNum] = false;
        ++iTextNum;
    }

    return iTextNum;
}

BOOL CSocketItemMgr::IsSocketSetOptionEnabled()
{
    return (!m_EquipSetBonusList.empty());
}

void CSocketItemMgr::CheckSocketSetOption()
{
    m_EquipSetBonusList.clear();

    int iSeedSum[6] = {0, 0, 0, 0, 0, 0};
    ITEM *pItem = NULL;
    SOCKET_OPTION_INFO *pInfo = NULL;

    for (int i = 0; i < MAX_EQUIPMENT; ++i)
    {
        pItem = &CharacterMachine->Equipment[i];
        for (int j = 0; j < pItem->SocketCount; ++j)
        {
            if (pItem->SocketSeedID[j] != SOCKET_EMPTY)
            {
                pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][pItem->SocketSeedID[j]];
                ++iSeedSum[pInfo->m_iOptionCategory - 1];
            }
        }
    }

    for (int i = 0; i < m_iNumEquitSetBonusOptions; ++i)
    {
        int icnt = 0;
        BYTE *pbySetTest = m_SocketOptionInfo[SOT_EQUIP_SET_BONUS_OPTIONS][i].m_bySocketCheckInfo;
        for (; icnt < 6; ++icnt)
        {
            if (iSeedSum[icnt] < pbySetTest[icnt])
                break;
        }
        if (icnt < 6)
            continue;

        m_EquipSetBonusList.push_back(
            m_SocketOptionInfo[SOT_EQUIP_SET_BONUS_OPTIONS][i].m_iOptionID);
    }
}

int CSocketItemMgr::GetSocketOptionValue(const ITEM *pItem, int iSocketIndex)
{
    if (pItem->SocketCount > 0 && pItem->SocketSeedID[iSocketIndex] != SOCKET_EMPTY)
    {
        SOCKET_OPTION_INFO *pInfo = NULL;
        pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][pItem->SocketSeedID[iSocketIndex]];
        auto fOptionValue = (float)pInfo->m_iOptionValue[pItem->SocketSphereLv[iSocketIndex] - 1];
        return CalcSocketOptionValue(pInfo->m_bOptionType, fOptionValue);
    }
    else
    {
        return 0;
    }
}

void CSocketItemMgr::CalcSocketStatusBonus()
{
    memset(&m_StatusBonus, 0, sizeof(SOCKET_OPTION_STATUS_BONUS));
    m_StatusBonus.m_fDefenceRateBonus = 1.0f;

    ITEM *pItem = NULL;
    SOCKET_OPTION_INFO *pInfo = NULL;

    for (int i = 0; i < MAX_EQUIPMENT; ++i)
    {
        pItem = &CharacterMachine->Equipment[i];

        if (!IsSocketItem(pItem))
            continue;

        for (int j = 0; j < pItem->SocketCount; ++j)
        {
            if (pItem->SocketSeedID[j] != SOCKET_EMPTY)
            {
                pInfo = &m_SocketOptionInfo[SOT_SOCKET_ITEM_OPTIONS][pItem->SocketSeedID[j]];
                auto fOptionValue = (float)pInfo->m_iOptionValue[pItem->SocketSphereLv[j] - 1];
                int iBonus = CalcSocketOptionValue(pInfo->m_bOptionType, fOptionValue);

                switch (pInfo->m_iOptionID)
                {
                case SOPT_ATTACK_N_MAGIC_DAMAGE_BONUS_BY_LEVEL:
                case SOPT_ATTACK_N_MAGIC_DAMAGE_BONUS:
                    m_StatusBonus.m_iAttackDamageMinBonus += iBonus;
                    m_StatusBonus.m_iAttackDamageMaxBonus += iBonus;
                    break;
                case SOPT_ATTACK_SPEED_BONUS:
                    m_StatusBonus.m_iAttackSpeedBonus += iBonus;
                    break;
                case SOPT_ATTACT_N_MAGIC_DAMAGE_MAX_BONUS:
                    m_StatusBonus.m_iAttackDamageMaxBonus += iBonus;
                    break;
                case SOPT_ATTACK_N_MAGIC_DAMAGE_MIN_BONUS:
                    m_StatusBonus.m_iAttackDamageMinBonus += iBonus;
                    break;
                case SOPT_DEFENCE_RATE_BONUS:
                    m_StatusBonus.m_fDefenceRateBonus *= 1.0f + iBonus * 0.01f;
                    break;
                case SOPT_DEFENCE_BONUS:
                    m_StatusBonus.m_iDefenceBonus += iBonus;
                    break;
                case SOPT_SHIELD_DEFENCE_BONUS:
                    m_StatusBonus.m_iShieldDefenceBonus += iBonus;
                    break;
                case SOPT_SKILL_DAMAGE_BONUS:
                    m_StatusBonus.m_iSkillAttackDamageBonus += iBonus;
                    break;
                case SOPT_ATTACK_RATE_BONUS:
                    m_StatusBonus.m_iAttackRateBonus += iBonus;
                    break;
                case SOPT_STRENGTH_BONUS:
                    m_StatusBonus.m_iStrengthBonus += iBonus;
                    break;
                case SOPT_DEXTERITY_BONUS:
                    m_StatusBonus.m_iDexterityBonus += iBonus;
                    break;
                case SOPT_VITALITY_BONUS:
                    m_StatusBonus.m_iVitalityBonus += iBonus;
                    break;
                case SOPT_ENERGY_BONUS:
                    m_StatusBonus.m_iEnergyBonus += iBonus;
                    break;
                }
            }
        }

        if (pItem->SocketSeedSetOption != SOCKET_EMPTY)
        {
            pInfo = &m_SocketOptionInfo[SOT_MIX_SET_BONUS_OPTIONS][pItem->SocketSeedSetOption];
            int iBonus =
                CalcSocketOptionValue(pInfo->m_bOptionType, (float)pInfo->m_iOptionValue[0]);

            switch (pInfo->m_iOptionID)
            {
            case SBOPT_ATTACK_DAMAGE_BONUS:
                m_StatusBonus.m_iAttackDamageMinBonus += iBonus;
                m_StatusBonus.m_iAttackDamageMaxBonus += iBonus;
                break;
            case SBOPT_SKILL_DAMAGE_BONUS:
            case SBOPT_SKILL_DAMAGE_BONUS_2:
                m_StatusBonus.m_iSkillAttackDamageBonus += iBonus;
                break;
            case SBOPT_MAGIC_POWER_BONUS:
                m_StatusBonus.m_iAttackDamageMinBonus += iBonus;
                m_StatusBonus.m_iMagicPowerBonus += iBonus;
                break;
            case SBOPT_DEFENCE_BONUS:
                m_StatusBonus.m_iDefenceBonus += iBonus;
                break;
            }
        }
    }
}

bool CSocketItemMgr::OpenSocketItemScript(const wchar_t *szFileName)
{
    FILE *fp = _wfopen(szFileName, L"rb");
    if (fp == NULL)
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", szFileName);
        g_ErrorReport.Write(Text);
        return false;
    }

    int iSize = sizeof(SOCKET_OPTION_INFO_FILE);
    for (int j = 0; j < MAX_SOCKET_OPTION_TYPES; ++j)
    {
        for (int i = 0; i < MAX_SOCKET_OPTION; ++i)
        {
            SOCKET_OPTION_INFO_FILE current;
            fread(&current, iSize, 1, fp);
            BuxConvert((BYTE *)&current, iSize);

            auto target = &m_SocketOptionInfo[j][i];
            target->m_iOptionCategory = current.m_iOptionCategory;
            target->m_iOptionID = current.m_iOptionID;
            target->m_bOptionType = current.m_bOptionType;
            target->m_iOptionValue[0] = current.m_iOptionValue[0];
            target->m_iOptionValue[1] = current.m_iOptionValue[1];
            target->m_iOptionValue[2] = current.m_iOptionValue[2];
            target->m_iOptionValue[3] = current.m_iOptionValue[3];
            target->m_iOptionValue[4] = current.m_iOptionValue[4];
            target->m_bySocketCheckInfo[0] = current.m_bySocketCheckInfo[0];
            target->m_bySocketCheckInfo[1] = current.m_bySocketCheckInfo[1];
            target->m_bySocketCheckInfo[2] = current.m_bySocketCheckInfo[2];
            target->m_bySocketCheckInfo[3] = current.m_bySocketCheckInfo[3];
            target->m_bySocketCheckInfo[4] = current.m_bySocketCheckInfo[4];
            target->m_bySocketCheckInfo[5] = current.m_bySocketCheckInfo[5];

            CMultiLanguage::ConvertFromUtf8(target->m_szOptionName, current.m_szOptionName);
        }
    }

    fclose(fp);

    for (int i = 0; i < MAX_SOCKET_OPTION; ++i)
    {
        m_iNumEquitSetBonusOptions = i;
        BYTE *pbySetTest = m_SocketOptionInfo[SOT_EQUIP_SET_BONUS_OPTIONS][i].m_bySocketCheckInfo;
        if (pbySetTest[0] + pbySetTest[1] + pbySetTest[2] + pbySetTest[3] + pbySetTest[4] +
                pbySetTest[5] ==
            0)
            break;
    }
    return true;
}

namespace
{
using INTBYTEPAIR = std::pair<int, BYTE>;
constexpr int kAttachValue = 1000000;
constexpr int kDetachUnitValue = 50000;
} // namespace

void SessionGameplayUnit::SendReqUnMix()
{
    SocketClient->ToGameServer()->SendLahapJewelMixRequest(
        MixType::Unmix, static_cast<ItemType>(sessionKeeper_.ComGemStorage().m_cGemType / 2),
        static_cast<StackSize>(sessionKeeper_.ComGemStorage().iUnMixLevel),
        sessionKeeper_.ComGemStorage().iUnMixIndex);
}

void SessionGameplayUnit::SendReqMix()
{
    SocketClient->ToGameServer()->SendLahapJewelMixRequest(
        MixType::Mix, static_cast<ItemType>(sessionKeeper_.ComGemStorage().m_cGemType / 2),
        static_cast<StackSize>(sessionKeeper_.ComGemStorage().m_cComType / 10 - 1), 0);
}

void SessionGameplayUnit::ProcessCSAction()
{
    if (sessionKeeper_.ComGemStorage().m_cState == COMGEM::STATE_HOLD ||
        sessionKeeper_.ComGemStorage().m_cErr != COMGEM::NOERR)
        return;
    SetState(COMGEM::STATE_HOLD);

    if (isComMode())
        SendReqMix();
    else
        SendReqUnMix();
}

int SessionGameplayUnit::GetUnMixGemLevel() const
{
    return sessionKeeper_.ComGemStorage().iUnMixLevel;
}

char SessionGameplayUnit::CheckOneItem(const ITEM *p) const
{
    return Check_Jewel(p->Type);
}

bool SessionGameplayUnit::CheckMyInvValid()
{
    using namespace COMGEM;
    sessionKeeper_.ComGemStorage().m_cPercent = 0;
    sessionKeeper_.ComGemStorage().m_cCount = 0;

    if (sessionKeeper_.ComGemStorage().m_bType == ATTACH)
    {
        for (int slot = MAX_EQUIPMENT_INDEX; slot < MAX_MY_INVENTORY_EX_INDEX; ++slot)
        {
            const ITEM *pItem = FindInventoryItemBySlot(slot);
            if (!pItem)
            {
                continue;
            }

            if (sessionKeeper_.ComGemStorage().m_cGemType == Check_Jewel_Unit(pItem->Type))
                ++sessionKeeper_.ComGemStorage().m_cCount;

            if (sessionKeeper_.ComGemStorage().m_cCount ==
                sessionKeeper_.ComGemStorage().m_cComType)
            {
                sessionKeeper_.ComGemStorage().m_cPercent = 100;
                CalcGen();
                return true;
            }
        }
        if ((sessionKeeper_.ComGemStorage().m_cCount < sessionKeeper_.ComGemStorage().m_cComType) ||
            (sessionKeeper_.ComGemStorage().m_cComType == NOCOM))
        {
            sessionKeeper_.ComGemStorage().m_cPercent = 0;
            sessionKeeper_.ComGemStorage().m_cErr = COMERROR_NOTALLOWED;
            return false;
        }
    }
    else if (sessionKeeper_.ComGemStorage().m_bType == DETACH)
    {
        if (sessionKeeper_.ComGemStorage().iUnMixIndex < MAX_EQUIPMENT_INDEX ||
            sessionKeeper_.ComGemStorage().iUnMixIndex >= MAX_MY_INVENTORY_EX_INDEX)
        {
            sessionKeeper_.ComGemStorage().m_cErr = DEERROR_NOTALLOWED;
            sessionKeeper_.ComGemStorage().m_cPercent = 0;
            return false;
        }

        const ITEM *pItem = FindInventoryItemBySlot(sessionKeeper_.ComGemStorage().iUnMixIndex);
        if (pItem != nullptr && isCompiledGem(pItem))
        {
            ++sessionKeeper_.ComGemStorage().m_cCount;
            sessionKeeper_.ComGemStorage().m_cPercent = 100;
            CalcGen();
            return true;
        }

        sessionKeeper_.ComGemStorage().m_cErr = DEERROR_NOTALLOWED;
        sessionKeeper_.ComGemStorage().m_cPercent = 0;
        return false;
    }
    sessionKeeper_.ComGemStorage().m_cErr = ERROR_UNKNOWN;
    return false;
}

void SessionGameplayUnit::CalcGen()
{
    sessionKeeper_.ComGemStorage().m_iValue = 0;
    if (sessionKeeper_.ComGemStorage().m_bType)
    {
        sessionKeeper_.ComGemStorage().m_iValue = kAttachValue;
    }
    else
    {
        sessionKeeper_.ComGemStorage().m_iValue =
            sessionKeeper_.ComGemStorage().m_cComType * kDetachUnitValue;
    }
}

char SessionGameplayUnit::CalcCompiledCount(const ITEM *p) const
{
    using namespace COMGEM;
    if (CheckOneItem(p) % 2)
        return (p->Level + 1) * FIRST;
    return 0;
}

int SessionGameplayUnit::CalcItemValue(const ITEM *p) const
{
    using namespace COMGEM;
    int Level = p->Level;
    switch (CheckOneItem(p))
    {
    case NOGEM:
        return 0;
    case eBLESS_C:
        return 9000000 * (Level + 1) * FIRST;
    case eSOUL_C:
        return 6000000 * (Level + 1) * FIRST;
    case eLIFE_C:
        return 45000000 * (Level + 1) * FIRST;
    case eCREATE_C:
        return 36000000 * (Level + 1) * FIRST;
    case ePROTECT_C:
        return 60000000 * (Level + 1) * FIRST;
    case eCHAOS_C:
        return 810000 * (Level + 1) * FIRST;
    case eGEMSTONE_C:
    case eHARMONY_C:
    case eLOW_C:
    case eUPPER_C:
        return 18600 * (Level + 1) * FIRST;
    default:
        return 0;
    }
}

int SessionGameplayUnit::CalcEmptyInv() const
{
    return sessionKeeper_.GameData()->Inventory(InventoryRole::Player).GetEmptySlotCount();
}

void SessionGameplayUnit::Init()
{
    sessionKeeper_.ComGemStorage().m_bType = COMGEM::ATTACH;
    sessionKeeper_.ComGemStorage().m_cState = COMGEM::STATE_READY;
    sessionKeeper_.ComGemStorage().m_cErr = COMGEM::NOERR;
    sessionKeeper_.ComGemStorage().m_cGemType = -1;
    sessionKeeper_.ComGemStorage().m_cComType = -1;
    sessionKeeper_.ComGemStorage().m_cCount = 0;
    sessionKeeper_.ComGemStorage().m_iValue = -1;
    sessionKeeper_.ComGemStorage().m_cPercent = 0;
    sessionKeeper_.ComGemStorage().iUnMixIndex = -1;
    sessionKeeper_.ComGemStorage().iUnMixLevel = -1;
}

void SessionGameplayUnit::GetBack()
{
    if (sessionKeeper_.ComGemStorage().m_cState == COMGEM::STATE_HOLD)
    {
        sessionKeeper_.ComGemStorage().m_cState = COMGEM::STATE_READY;
    }

    sessionKeeper_.ComGemStorage().m_cErr = COMGEM::NOERR;
    sessionKeeper_.ComGemStorage().m_cGemType = COMGEM::NOGEM;

    if (sessionKeeper_.ComGemStorage().m_bType != COMGEM::ATTACH &&
        sessionKeeper_.ComGemStorage().m_bType != COMGEM::DETACH)
    {
        Exit();
    }
}

void SessionGameplayUnit::Exit()
{
    Init();

    SocketClient->ToGameServer()->SendCloseNpcRequest();
}

int SessionGameplayUnit::GetJewelRequireCount(int i) const
{
    using namespace COMGEM;
    switch (i)
    {
    case 0:
        return FIRST;
    case 1:
        return SECOND;
    case 2:
        return THIRD;
    default:
        return -1;
    }
}

int SessionGameplayUnit::Check_Jewel(int _nJewel, int _nType, bool _bModel) const
{
    using namespace COMGEM;
    bool bCom = true;
    bool bNon = true;

    if (_bModel)
        _nJewel -= MODEL_ITEM;
    if (_nType & 1)
        bCom = false;
    if (_nType & 2)
        bNon = false;

    if (bNon)
    {
        if (_nJewel == ITEM_JEWEL_OF_BLESS)
            return eBLESS;
        if (_nJewel == ITEM_JEWEL_OF_SOUL)
            return eSOUL;
        if (_nJewel == ITEM_JEWEL_OF_LIFE)
            return eLIFE;
        if (_nJewel == ITEM_JEWEL_OF_CREATION)
            return eCREATE;
        if (_nJewel == ITEM_JEWEL_OF_GUARDIAN)
            return ePROTECT;
        if (_nJewel == ITEM_GEMSTONE)
            return eGEMSTONE;
        if (_nJewel == ITEM_JEWEL_OF_HARMONY)
            return eHARMONY;
        if (_nJewel == ITEM_JEWEL_OF_CHAOS)
            return eCHAOS;
        if (_nJewel == ITEM_LOWER_REFINE_STONE)
            return eLOW;
        if (_nJewel == ITEM_HIGHER_REFINE_STONE)
            return eUPPER;
    }

    if (bCom)
    {
        if (_nJewel == ITEM_PACKED_JEWEL_OF_BLESS)
            return eBLESS_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_SOUL)
            return eSOUL_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_LIFE)
            return eLIFE_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_CREATION)
            return eCREATE_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_GUARDIAN)
            return ePROTECT_C;
        if (_nJewel == ITEM_PACKED_GEMSTONE)
            return eGEMSTONE_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_HARMONY)
            return eHARMONY_C;
        if (_nJewel == ITEM_PACKED_JEWEL_OF_CHAOS)
            return eCHAOS_C;
        if (_nJewel == ITEM_PACKED_LOWER_REFINE_STONE)
            return eLOW_C;
        if (_nJewel == ITEM_PACKED_HIGHER_REFINE_STONE)
            return eUPPER_C;
    }

    return NOGEM;
}

int SessionGameplayUnit::GetJewelIndex(int _nJewel, int _nType) const
{
    using namespace COMGEM;
    switch (_nJewel)
    {
    case eBLESS:
        if (_nType == eGEM_NAME)
            return 1806;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_BLESS;
        break;
    case eBLESS_C:
        if (_nType == eGEM_NAME)
            return 1806;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_GREATER_FORTITUDE;
        break;
    case eSOUL:
        if (_nType == eGEM_NAME)
            return 1807;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_SOUL;
        break;
    case eSOUL_C:
        if (_nType == eGEM_NAME)
            return 1807;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eLIFE:
        if (_nType == eGEM_NAME)
            return 3312;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_LIFE;
        break;
    case eLIFE_C:
        if (_nType == eGEM_NAME)
            return 3312;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eCREATE:
        if (_nType == eGEM_NAME)
            return 3313;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_CREATION;
        break;
    case eCREATE_C:
        if (_nType == eGEM_NAME)
            return 3313;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case ePROTECT:
        if (_nType == eGEM_NAME)
            return 3314;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_GUARDIAN;
        break;
    case ePROTECT_C:
        if (_nType == eGEM_NAME)
            return 3314;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eGEMSTONE:
        if (_nType == eGEM_NAME)
            return 2081;
        if (_nType == eGEM_INDEX)
            return ITEM_GEMSTONE;
        break;
    case eGEMSTONE_C:
        if (_nType == eGEM_NAME)
            return 2081;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eHARMONY:
        if (_nType == eGEM_NAME)
            return 3315;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_HARMONY;
        break;
    case eHARMONY_C:
        if (_nType == eGEM_NAME)
            return 3315;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eCHAOS:
        if (_nType == eGEM_NAME)
            return 3316;
        if (_nType == eGEM_INDEX)
            return ITEM_JEWEL_OF_CHAOS;
        break;
    case eCHAOS_C:
        if (_nType == eGEM_NAME)
            return 3316;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eLOW:
        if (_nType == eGEM_NAME)
            return 3317;
        if (_nType == eGEM_INDEX)
            return ITEM_LOWER_REFINE_STONE;
        break;
    case eLOW_C:
        if (_nType == eGEM_NAME)
            return 3317;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    case eUPPER:
        if (_nType == eGEM_NAME)
            return 3318;
        if (_nType == eGEM_INDEX)
            return ITEM_HIGHER_REFINE_STONE;
        break;
    case eUPPER_C:
        if (_nType == eGEM_NAME)
            return 3318;
        if (_nType == eGEM_INDEX)
            return ITEM_ORB_OF_IMPALE;
        break;
    }

    return -1;
}

void SessionGameplayUnit::SetMode(BOOL mode)
{
    sessionKeeper_.ComGemStorage().m_bType = mode;
}
void SessionGameplayUnit::SetGem(char gem)
{
    sessionKeeper_.ComGemStorage().m_cGemType = gem;
}
void SessionGameplayUnit::SetComType(char type)
{
    sessionKeeper_.ComGemStorage().m_cComType = type;
}
void SessionGameplayUnit::SetState(char state)
{
    sessionKeeper_.ComGemStorage().m_cState = state;
}
void SessionGameplayUnit::SetError(char error)
{
    sessionKeeper_.ComGemStorage().m_cErr = error;
}
char SessionGameplayUnit::GetError() const
{
    return sessionKeeper_.ComGemStorage().m_cErr;
}
bool SessionGameplayUnit::isComMode() const
{
    return sessionKeeper_.ComGemStorage().m_bType == COMGEM::ATTACH;
}
int SessionGameplayUnit::Check_Jewel_Unit(int jewel, bool model) const
{
    return Check_Jewel(jewel, 1, model);
}
int SessionGameplayUnit::Check_Jewel_Com(int jewel, bool model) const
{
    return Check_Jewel(jewel, 2, model);
}
bool SessionGameplayUnit::isCompiledGem(const ITEM *item) const
{
    return Check_Jewel_Com(item->Type) != COMGEM::NOGEM;
}
bool SessionGameplayUnit::isAble() const
{
    return sessionKeeper_.ComGemStorage().m_cState == COMGEM::STATE_READY;
}

// Construction/Destruction
CItemEqualType::CItemEqualType()
{
    m_nModelType = 0;
    m_nSubLeftType = 0;
    m_nSubRightType = 0;
}

CItemEqualType::~CItemEqualType()
{
}

void CItemEqualType::SetModelType(int _ModelIndex, int _Left, int _Right)
{
    m_nModelType = _ModelIndex;
    m_nSubLeftType = _Left;
    m_nSubRightType = _Right;
}

CMonkSystem::CMonkSystem(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
{
    m_nRepeatedlyCnt = 0;
    memset(m_arrRepeatedly, 0, sizeof(DamageInfo) * MAX_REPEATEDLY);
    Init();
    RegistItem();

    m_nTotalCnt = m_mapItemEqualType.size();
    SetSwordformGlovesItemType();
    InitLower();
}

CMonkSystem::~CMonkSystem()
{
    Destroy();
}

void CMonkSystem::Init()
{
    m_mapItemEqualType.clear();
    memset(&m_cItemEqualType, 0, sizeof(CItemEqualType));
    m_listGloveformSword.clear();

    InitEffectOnce();
}

void CMonkSystem::Destroy()
{
    m_mapItemEqualType.clear();
    m_listGloveformSword.clear();
    m_DarksideIndex.clear();
}

void CMonkSystem::RegistItem()
{
    m_cItemEqualType.SetModelType(MODEL_SACRED_GLOVE, MODEL_SWORD_32_LEFT, MODEL_SWORD_32_RIGHT);
    m_mapItemEqualType.insert(
        tm_ItemEqualType::value_type(m_cItemEqualType.GetModelType(), m_cItemEqualType));

    m_cItemEqualType.SetModelType(MODEL_STORM_HARD_GLOVE, MODEL_SWORD_33_LEFT,
                                  MODEL_SWORD_33_RIGHT);
    m_mapItemEqualType.insert(
        tm_ItemEqualType::value_type(m_cItemEqualType.GetModelType(), m_cItemEqualType));

    m_cItemEqualType.SetModelType(MODEL_PIERCING_BLADE_GLOVE, MODEL_SWORD_34_LEFT,
                                  MODEL_SWORD_34_RIGHT);
    m_mapItemEqualType.insert(
        tm_ItemEqualType::value_type(m_cItemEqualType.GetModelType(), m_cItemEqualType));

    m_cItemEqualType.SetModelType(MODEL_PHOENIX_SOUL_STAR, MODEL_SWORD_35_LEFT,
                                  MODEL_SWORD_35_RIGHT);
    m_mapItemEqualType.insert(
        tm_ItemEqualType::value_type(m_cItemEqualType.GetModelType(), m_cItemEqualType));
}

void CMonkSystem::LoadModelItem()
{
    gLoadData.AccessModel(MODEL_SACRED_GLOVE, L"Data\\Item\\", L"Sword33");
    gLoadData.AccessModel(MODEL_SWORD_32_LEFT, L"Data\\Item\\", L"SwordL33");
    gLoadData.AccessModel(MODEL_SWORD_32_RIGHT, L"Data\\Item\\", L"SwordR33");

    gLoadData.AccessModel(MODEL_STORM_HARD_GLOVE, L"Data\\Item\\", L"Sword34");
    gLoadData.AccessModel(MODEL_SWORD_33_LEFT, L"Data\\Item\\", L"SwordL34");
    gLoadData.AccessModel(MODEL_SWORD_33_RIGHT, L"Data\\Item\\", L"SwordR34");

    gLoadData.AccessModel(MODEL_PIERCING_BLADE_GLOVE, L"Data\\Item\\", L"Sword35");
    gLoadData.AccessModel(MODEL_SWORD_34_LEFT, L"Data\\Item\\", L"SwordL35");
    gLoadData.AccessModel(MODEL_SWORD_34_RIGHT, L"Data\\Item\\", L"SwordR35");

    gLoadData.AccessModel(MODEL_PHOENIX_SOUL_STAR, L"Data\\Item\\", L"Sword36");
    gLoadData.AccessModel(MODEL_SWORD_35_LEFT, L"Data\\Item\\", L"Sword36L");
    gLoadData.AccessModel(MODEL_SWORD_35_RIGHT, L"Data\\Item\\", L"Sword36R");

    gLoadData.AccessModel(MODEL_ARMORINVEN_60, L"Data\\player\\", L"Armor_inventory60");
    gLoadData.AccessModel(MODEL_ARMORINVEN_61, L"Data\\player\\", L"ArmorMale61_inventory");
    gLoadData.AccessModel(MODEL_ARMORINVEN_62, L"Data\\player\\", L"ArmorMale62_inventory");
    gLoadData.AccessModel(MODEL_ARMORINVEN_74, L"Data\\player\\", L"ArmorMale74_inven");
}

void CMonkSystem::LoadModelItemTexture()
{
    gLoadData.OpenTexture(MODEL_SACRED_GLOVE, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_32_LEFT, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_32_RIGHT, L"player\\");

    gLoadData.OpenTexture(MODEL_STORM_HARD_GLOVE, L"Item\\");
    gLoadData.OpenTexture(MODEL_SWORD_33_LEFT, L"Item\\");
    gLoadData.OpenTexture(MODEL_SWORD_33_RIGHT, L"Item\\");

    gLoadData.OpenTexture(MODEL_PIERCING_BLADE_GLOVE, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_34_LEFT, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_34_RIGHT, L"player\\");

    gLoadData.OpenTexture(MODEL_PHOENIX_SOUL_STAR, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_35_LEFT, L"player\\");
    gLoadData.OpenTexture(MODEL_SWORD_35_RIGHT, L"player\\");

    gLoadData.OpenTexture(MODEL_ARMORINVEN_60, L"player\\");
    gLoadData.OpenTexture(MODEL_ARMORINVEN_61, L"player\\");
    gLoadData.OpenTexture(MODEL_ARMORINVEN_62, L"player\\");
    gLoadData.OpenTexture(MODEL_ARMORINVEN_74, L"player\\");
}

int CMonkSystem::GetSubItemType(int _Type, int _Left)
{
    int iType = _Type;
    auto iter = m_mapItemEqualType.find(iType);

    CItemEqualType _SubType;
    if (iter == m_mapItemEqualType.end())
        return iType;

    _SubType = (CItemEqualType)iter->second;

    if (_Left)
        return _SubType.GetSubLeftType();
    else
        return _SubType.GetSubRightType();

    return iType;
}

int CMonkSystem::GetModelItemType(int _Type)
{
    // RegistItem's left/right glove subtypes occupy this contiguous enum range.
    if (_Type < MODEL_SWORD_32_LEFT || _Type > MODEL_SWORD_35_RIGHT)
        return _Type;
    for (auto iter = m_mapItemEqualType.begin(); iter != m_mapItemEqualType.end(); ++iter)
    {
        CItemEqualType _ItemType;
        if (iter == m_mapItemEqualType.end())
            return _Type;

        _ItemType = (CItemEqualType)iter->second;
        if (_ItemType.GetSubLeftType() == _Type || _ItemType.GetSubRightType() == _Type)
            return _ItemType.GetModelType();
    }
    return _Type;
}

int CMonkSystem::OrginalTypeCommonItemMonk(int _ModifyType)
{
    if (_ModifyType >= MODEL_HELM_MONK &&
        _ModifyType <= MODEL_BOOTS_MONK + MODEL_ITEM_COMMONCNT_RAGEFIGHTER)
    {
        int nItemType = (_ModifyType - MODEL_HELM_MONK) / MODEL_ITEM_COMMONCNT_RAGEFIGHTER + 7;
        int nItemSubType = (_ModifyType - MODEL_HELM_MONK) % MODEL_ITEM_COMMONCNT_RAGEFIGHTER + 5;

        int OrgItemType = (nItemType == 10) ? nItemType + 1 : nItemType;
        int OrgItemSubType = (nItemSubType >= 7) ? nItemSubType + 1 : nItemSubType;
        _ModifyType = OrgItemType * MAX_ITEM_INDEX + OrgItemSubType + MODEL_ITEM;
    }
    return _ModifyType;
}

int CMonkSystem::ModifyTypeCommonItemMonk(int _OrginalType)
{
    int nItemType = (_OrginalType - MODEL_ITEM) / MAX_ITEM_INDEX;
    int nItemSubType = (_OrginalType - MODEL_ITEM) % MAX_ITEM_INDEX;
    int nCommonItem[MODEL_ITEM_COMMONCNT_RAGEFIGHTER] = {5, 6, 8, 9};

    if (nItemType >= 7 && nItemType <= 11)
    {
        for (int i = 0; i < MODEL_ITEM_COMMONCNT_RAGEFIGHTER; ++i)
        {
            if (nItemSubType == nCommonItem[i])
            {
                int _TempItemType = (nItemType == 11) ? nItemType - 1 : nItemType;
                _OrginalType =
                    MODEL_HELM_MONK + (_TempItemType - 7) * MODEL_ITEM_COMMONCNT_RAGEFIGHTER + i;
                break;
            }
        }
    }

    return _OrginalType;
}

bool CMonkSystem::IsRagefighterCommonWeapon(CLASS_TYPE _Class, int _Type)
{
    if ((gCharacterManager.GetBaseClass(_Class) == CLASS_RAGEFIGHTER) &&
        ((_Type == MODEL_KRIS) || (_Type == MODEL_SHORT_SWORD) || (_Type == MODEL_SMALL_AXE) ||
         (_Type == MODEL_HAND_AXE) || (_Type == MODEL_TOMAHAWK) || (_Type == MODEL_SMALLMACE) ||
         (_Type == MODEL_MORNING_STAR) || (_Type == MODEL_FLAIL) || (_Type == MODEL_GREAT_HAMMER) ||
         (_Type == MODEL_CRYSTAL_MORNING_STAR)))
    {
        return true;
    }

    return false;
}

bool CMonkSystem::IsSwordformGloves(int _Type)
{
    _Type = EqualItemModelType(_Type);

    auto iter = m_mapItemEqualType.find(_Type);

    CItemEqualType _SubType;
    if (iter == m_mapItemEqualType.end())
        return false;

    _SubType = (CItemEqualType)iter->second;

    if (_SubType.GetModelType() == _Type)
        return true;

    return false;
}

int CMonkSystem::ModifyTypeSwordformGloves(int _ModelType, int _LeftHand)
{
    return GetSubItemType(_ModelType, _LeftHand);
}

int CMonkSystem::EqualItemModelType(int _Type)
{
    return GetModelItemType(_Type);
}

void CMonkSystem::MoveBlurEffect(CHARACTER *_pCha, OBJECT *_pObj, BMD *pModel,
                                 float animationFactor)
{
    if (_pCha == NULL)
        return;
    if (_pObj == NULL)
        return;
    if (pModel == NULL)
        return;

    BMD *b = &Models[_pObj->Type];
    vec3_t Light;
    vec3_t StartPos, StartLocal, EndPos, EndLocal;
    float fDelay = 10.0f;
    float fPlaySpeed = b->Actions[_pObj->CurrentAction].PlaySpeed * animationFactor;
    float fSpeedPerFrame = fPlaySpeed / fDelay;
    float fAnimationFrame = _pObj->AnimationFrame - fPlaySpeed;

    if (fAnimationFrame > 5.5f)
        return;

    for (int i = 0; i < fDelay; i++)
    {
        b->AnimationAtFrame(BoneTransform, fAnimationFrame, _pObj->PriorAnimationFrame,
                            _pObj->PriorAction, _pObj->Angle, _pObj->HeadAngle);

        Vector(1.0f, 1.0f, 1.0f, Light);
        int _LeftHand = 0;
        if (_pObj->CurrentAction == PLAYER_ATTACK_SWORD_LEFT1 ||
            _pObj->CurrentAction == PLAYER_ATTACK_SWORD_LEFT2)
            _LeftHand = 1;

        Vector(-30.0f, 0.0f, 0.0f, StartLocal);
        Vector(30.0f, 0.0f, 0.0f, EndLocal);

        b->TransformPosition(BoneTransform[_pCha->Weapon[_LeftHand].LinkBone], StartLocal, StartPos,
                             false);
        b->TransformPosition(BoneTransform[_pCha->Weapon[_LeftHand].LinkBone], EndLocal, EndPos,
                             false);
        CreateBlur(_pCha, StartPos, EndPos, Light, 5, true);

        fAnimationFrame += fSpeedPerFrame;
    }
}

void CMonkSystem::SetSwordformGlovesItemType()
{
    for (auto iter = m_mapItemEqualType.begin(); iter != m_mapItemEqualType.end(); ++iter)
    {
        CItemEqualType _ItemType;
        if (iter == m_mapItemEqualType.end())
            return;

        _ItemType = (CItemEqualType)iter->second;
        m_listGloveformSword.push_back(_ItemType.GetModelType() % MODEL_ITEM);
    }
}

bool CMonkSystem::IsSwordformGlovesItemType(int _Type)
{
    for (list_ItemType::iterator iter = m_listGloveformSword.begin();
         iter != m_listGloveformSword.end(); ++iter)
    {
        if (*iter == _Type)
            return true;
    }
    return false;
}

bool CMonkSystem::RageEquipmentWeapon(int _Index, short _ItemType)
{
    int _OtherEquip =
        (_Index == EQUIPMENT_WEAPON_LEFT) ? EQUIPMENT_WEAPON_RIGHT : EQUIPMENT_WEAPON_LEFT;
    ITEM *pOtherHand = &CharacterMachine->Equipment[_OtherEquip];
    //글러브형 무기는 글러브형무기하고만 착용가능
    if (IsSwordformGlovesItemType(_ItemType))
    {
        if (pOtherHand->Type == -1)
            return true;
        else if (!IsSwordformGlovesItemType(pOtherHand->Type))
            return false;
    }
    else
    {
        if (pOtherHand->Type == -1)
            return true;
        else if (IsSwordformGlovesItemType(pOtherHand->Type))
            return false;
    }
    return true;
}

bool CMonkSystem::SetRepeatedly(int _Damage, int _DamageType, bool _Double, bool _bEndRepeatedly)
{
    if (m_nRepeatedlyCnt < MAX_REPEATEDLY)
    {
        if (!_bEndRepeatedly)
        {
            if (m_nRepeatedlyCnt == MAX_REPEATEDLY - 1)
            {
                m_nRepeatedlyCnt = MAX_REPEATEDLY - 1;
                return false;
            }
        }

        m_arrRepeatedly[m_nRepeatedlyCnt].m_Damage = _Damage;
        m_arrRepeatedly[m_nRepeatedlyCnt].m_DamageType = _DamageType;
        m_arrRepeatedly[m_nRepeatedlyCnt].m_Double = _Double;
        m_nRepeatedlyCnt++;

        return true;
    }
    return false;
}

int CMonkSystem::GetRepeatedlyDamage(int _index)
{
    return m_arrRepeatedly[_index].m_Damage;
}

int CMonkSystem::GetRepeatedlyDamageType(int _index)
{
    return m_arrRepeatedly[_index].m_DamageType;
}

bool CMonkSystem::GetRepeatedlyDouble(int _index)
{
    return m_arrRepeatedly[_index].m_Double;
}

int CMonkSystem::GetRepeatedlyCnt()
{
    return m_nRepeatedlyCnt;
}

bool CMonkSystem::SetRageSkillAni(int _nSkill, CHARACTER &character)
{
    auto *_pObj = &character.Object;
    switch (_nSkill)
    {
    case AT_SKILL_KILLING_BLOW:
    case AT_SKILL_KILLING_BLOW_STR:
    case AT_SKILL_KILLING_BLOW_MASTERY:
        SetAction(_pObj, PLAYER_SKILL_THRUST);
        return true;
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY:
        SetAction(_pObj, PLAYER_SKILL_STAMP);
        return true;
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR:
        SetAction(_pObj, PLAYER_SKILL_GIANTSWING);
        return true;
    case AT_SKILL_DARKSIDE:
    case AT_SKILL_DARKSIDE_STR:
        ++character.Darkside.revision;
        character.Darkside.attackCount = 0;
        character.Darkside.targetCount = 0;
        character.Darkside.primaryTarget = {};
        if (_pObj->m_sTargetIndex >= 0)
        {
            const auto &target = CharactersClient[_pObj->m_sTargetIndex];
            character.Darkside.primaryTarget = {target.Key, target.SocketSource};
        }
        SetAction(_pObj, PLAYER_SKILL_DARKSIDE_READY);
        return true;
    case AT_SKILL_DRAGON_ROAR:
    case AT_SKILL_DRAGON_ROAR_STR:
        SetAction(_pObj, PLAYER_SKILL_DRAGONLORE);
        return true;
    case AT_SKILL_DRAGON_KICK:
        SetAction(_pObj, PLAYER_SKILL_DRAGONKICK);
        return true;
    case AT_SKILL_ATT_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY:
        return false;
    }
    return false;
}

bool CMonkSystem::IsRageHalfwaySkillAni(int _nSkill)
{
    switch (_nSkill)
    {
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY:
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR:
    case AT_SKILL_DRAGON_KICK:
        return true;
    default:
        return false;
    }
    return false;
}

bool CMonkSystem::RageFighterEffect(const ObjectDrawInput &draw, int _Type)
{
    BMD *b = &Models[_Type];
    if (draw.action == PLAYER_SKILL_DARKSIDE_READY)
    {
        Vector(1.f, 1.f, 1.f, b->BodyLight);
        float fAlpha = 1.0f;
        int _nAniKey = Models[draw.type].Actions[PLAYER_SKILL_DARKSIDE_READY].NumAnimationKeys;
        float _nRoop = (float)(180.0f / _nAniKey) * draw.animationFrame + 180.0f;
        fAlpha = sinf(_nRoop * Q_PI / 180) * 0.7f + 1.0f;
        b->RenderBody(RENDER_TEXTURE, fAlpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);
        return true;
    }
    else if (draw.action == PLAYER_SKILL_DARKSIDE_ATTACK)
    {
        Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        float fAlpha = draw.alpha;
        VectorScale(b->BodyLight, fAlpha, b->BodyLight);
        b->RenderBody(RENDER_TEXTURE | RENDER_BRIGHT, fAlpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        return true;
    }
    else if (draw.action == PLAYER_SKILL_ATT_UP_OURFORCES ||
             draw.action == PLAYER_SKILL_HP_UP_OURFORCES)
    {
        float fAlpha = 1.0f;
        if (draw.animationFrame < 4)
        {
            fAlpha = fAlpha - draw.animationFrame * 0.1f;
        }
        else if (draw.animationFrame > 8)
        {
            fAlpha = (draw.animationFrame - 6) * 0.1f + 0.4f;
        }
        else
        {
            fAlpha = 0.0f;
        }
        Vector(1.0f, 1.0f, 1.0f, b->BodyLight);
        b->RenderBody(RENDER_BRIGHT | RENDER_COLOR, draw.alpha, draw.blendMesh, draw.blendLight,
                      draw.blendU, draw.blendV);
        b->RenderBody(RENDER_TEXTURE, fAlpha, draw.blendMesh, draw.blendLight, draw.blendU,
                      draw.blendV);

        return false;
    }
    return false;
}

bool CMonkSystem::SetDarksideTargetIndexState(CHARACTER &character, WORD *targetKeys)
{
    auto &facts = character.Darkside;
    ++facts.revision;
    facts.targetCount = 0;
    facts.attackCount = 0;
    facts.primaryTarget = {};
    m_nDarksideCnt = 0;
    m_DarksideIndex.assign(targetKeys, targetKeys + DARKSIDE_TARGET_MAX);
    static_assert(DARKSIDE_TARGET_MAX == CharacterDarksideState::TargetCount);
    for (int i = 0; i < DARKSIDE_TARGET_MAX; ++i)
    {
        if (targetKeys[i] == DS_TARGET_NONE)
            continue;
        const int index = CharactersClient.FindIndexByKey(targetKeys[i]);
        if (index < 0)
            continue;
        const auto &target = CharactersClient[index];
        facts.targets[facts.targetCount++] = {target.Key, target.SocketSource};
        if (facts.targetCount == 1)
            facts.primaryTarget = facts.targets[0];
    }
    return facts.targetCount != 0;
}

void CMonkSystem::SetDarksideCnt(CHARACTER &character, const CHARACTER &target)
{
    character.Darkside.primaryTarget = {target.Key, target.SocketSource};
    ++character.Darkside.attackCount;
}

bool CMonkSystem::SendDarksideAtt(OBJECT *_pObj)
{
    if (_pObj == NULL)
        return false;

    if (m_nDarksideCnt >= DARKSIDE_TARGET_MAX)
    {
        InitDarksideTarget();
        return false;
    }

    auto iter = m_DarksideIndex.begin();
    if (iter == m_DarksideIndex.end())
    {
        return false;
    }

    int _DarksideTargetIndex = m_DarksideIndex[m_nDarksideCnt];

    if (_DarksideTargetIndex == DS_TARGET_NONE)
    {
        return false;
    }

    // SendRequestMagic(AT_SKILL_DARKSIDE, _DarksideTargetIndex);
    m_nDarksideCnt++;
    Hero->Darkside.attackCount = m_nDarksideCnt;
    return true;
}

void CMonkSystem::InitDarksideTarget()
{
    m_nDarksideCnt = 0;
    m_DarksideIndex.clear();
    auto &facts = Hero->Darkside;
    ++facts.revision;
    facts.targetCount = facts.attackCount = 0;
}

bool CMonkSystem::CalculateDarksideTrans(CharacterAfterImagePose &pose, vec3_t _vPos, float _fAni,
                                         float _fNextAni)
{
    vec3_t _StartPos, _TargetPos;
    vec3_t _DirectionVec;
    float _Distance = _fAni * _fAni * 0.05f;
    float _NextDistance = _fNextAni * _fNextAni * 0.05f;

    VectorCopy(pose.position, _StartPos);
    VectorCopy(_vPos, _TargetPos);
    VectorSubtract(_TargetPos, _StartPos, _DirectionVec);
    VectorNormalize(_DirectionVec);
    VectorScale(_DirectionVec, _Distance, _DirectionVec);
    VectorAdd(pose.position, _DirectionVec, pose.position);

    vec3_t vAngle;
    VectorCopy(pose.angle, vAngle);
    vAngle[2] = CreateAngle2D(_StartPos, _TargetPos);
    VectorCopy(vAngle, pose.angle);

    if (_fNextAni)
    {
        vec3_t _NextStartPos;
        VectorCopy(_StartPos, _NextStartPos);
        VectorCopy(_vPos, _TargetPos);
        VectorSubtract(_TargetPos, _NextStartPos, _DirectionVec);
        VectorNormalize(_DirectionVec);
        VectorScale(_DirectionVec, _NextDistance, _DirectionVec);
        VectorAdd(_NextStartPos, _DirectionVec, _NextStartPos);

        VectorSubtract(_StartPos, _TargetPos, _TargetPos);
        float fTargetLen = VectorLength(_TargetPos);

        VectorSubtract(_StartPos, _NextStartPos, _NextStartPos);
        float fNextLen = VectorLength(_NextStartPos);

        VectorCopy(pose.position, _NextStartPos);
        VectorSubtract(_StartPos, _NextStartPos, _NextStartPos);
        float fObjLen = VectorLength(_NextStartPos);

        if (fTargetLen > fObjLen)
        {
            if (fTargetLen < fNextLen)
            {
                return true;
            }
        }
    }
    return false;
}

namespace MonkPresentationDetail
{
CharacterAfterImagePose CaptureDarksidePose(const OBJECT &object)
{
    CharacterAfterImagePose pose;
    VectorCopy(object.Position, pose.position);
    VectorCopy(object.StartPosition, pose.startPosition);
    VectorCopy(object.Angle, pose.angle);
    pose.animationFrame = object.AnimationFrame;
    pose.alpha = object.Alpha;
    return pose;
}

} // namespace MonkPresentationDetail

void CMonkSystem::SetDummy(CharacterDarksideVisual &visual, vec3_t pos, vec3_t target)
{
    const int dummyindex = visual.dummyCount++;
    float Matrix[3][4];
    vec3_t vAngle, tempPos, outPos, vRangePos;
    VectorCopy(pos, tempPos);
    Vector(150.0f, 150.0f, 0.0f, vRangePos);
    Vector(0.0f, 0.0f, 60.0f * dummyindex, vAngle);
    AngleMatrix(vAngle, Matrix);
    VectorRotate(vRangePos, Matrix, outPos);
    VectorAdd(pos, outPos, pos);
    pos[2] = tempPos[2];

    visual.dummies[dummyindex].emplace(sessionKeeper_).Init(pos, target);
}

void CMonkSystem::InitConsecutiveState(float _fFirstFrame, float _fSecondFrame, BYTE _btAttState)
{
    m_fFirstFrame = _fFirstFrame;

    if (_fFirstFrame < _fSecondFrame)
        m_fSecondFrame = _fSecondFrame;
    else
        m_fSecondFrame = 9999.f;

    m_btAttState = _btAttState;
}

bool CMonkSystem::IsConsecutiveAtt(float _fAttFrame)
{
    if (_fAttFrame >= m_fFirstFrame && _fAttFrame < m_fSecondFrame)
    {
        if (m_btAttState == FRAME_FIRSTATT)
            return false;
        else
        {
            m_btAttState = FRAME_FIRSTATT;
            return true;
        }
    }
    else if (_fAttFrame >= m_fSecondFrame)
    {
        if (m_btAttState == FRAME_SECONDATT)
            return false;
        else
        {
            m_btAttState = FRAME_SECONDATT;
            return true;
        }
    }
    return false;
}

bool CMonkSystem::IsRideNotUseSkill(int _nSkill, short _Type)
{
    if (_Type != MODEL_HORN_OF_FENRIR && _Type != MODEL_HORN_OF_UNIRIA &&
        _Type != MODEL_HORN_OF_DINORANT)
        return false;

    // 탈것타고 있을 경우 사용 불가능한 스킬
    switch (_nSkill)
    {
    case AT_SKILL_KILLING_BLOW:
    case AT_SKILL_KILLING_BLOW_STR:
    case AT_SKILL_KILLING_BLOW_MASTERY:
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY:
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR:
    case AT_SKILL_DRAGON_KICK:
    case AT_SKILL_OCCUPY:
        return true;
    default:
        return false;
    }
    return false;
}

bool CMonkSystem::IsSwordformGlovesUseSkill(int _nSkill)
{
    switch (_nSkill)
    {
    case AT_SKILL_CHAIN_DRIVE: //여기 스킬들은 장갑형 무기를 착용시에만 사용가능
    case AT_SKILL_DRAGON_ROAR:
    case AT_SKILL_DRAGON_ROAR_STR:
    case AT_SKILL_DRAGON_KICK: {
        ITEM *pOtherHand = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
        if (IsSwordformGlovesItemType(pOtherHand->Type))
        {
            return true;
        }
        else
        {
            pOtherHand = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
            if (IsSwordformGlovesItemType(pOtherHand->Type))
            {
                return true;
            }
        }
    }
        return false;
    default:
        return true;
    }
    return true;
}

bool CMonkSystem::IsChangeringNotUseSkill(short _LType, short _RType, int _LLevel, int _RLevel)
{
    if (_LType == ITEM_SNOWMAN_TRANSFORMATION_RING || _RType == ITEM_SNOWMAN_TRANSFORMATION_RING)
    {
        return true;
    }

    if (((_LType == ITEM_TRANSFORMATION_RING) &&
         (_LLevel == 0 || _LLevel == 8 || _LLevel == 24 || _LLevel == 32 || _LLevel == 40)) ||
        ((_RType == ITEM_TRANSFORMATION_RING) &&
         (_RLevel == 0 || _RLevel == 8 || _RLevel == 24 || _RLevel == 32 || _RLevel == 40)))
    {
        return true;
    }

    return false;
}

void CMonkSystem::InitEffectOnce()
{
    m_bUseEffectOnce = false;
}

bool CMonkSystem::GetSkillUseState()
{
    return m_bUseEffectOnce;
}

bool CMonkSystem::RageCreateEffect(OBJECT *_pObj, int _nSkill)
{
    switch (_nSkill)
    {
    case AT_SKILL_KILLING_BLOW:
    case AT_SKILL_KILLING_BLOW_STR:
    case AT_SKILL_KILLING_BLOW_MASTERY: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;
        vec3_t vPosition;
        VectorCopy(CharactersClient[_pObj->m_sTargetIndex].Object.Position, vPosition);
        _pObj->Angle[2] = CreateAngle2D(_pObj->Position, vPosition);
        CreateEffect(MODEL_WOLF_HEAD_EFFECT, _pObj->Position, _pObj->Angle, _pObj->Light, 0, _pObj);
        CreateEffect(BITMAP_SBUMB, vPosition, _pObj->Angle, _pObj->Light, 0, _pObj, -1, 0, 0, 0,
                     2.1f);

        PlayBuffer(SOUND_RAGESKILL_THRUST);
    }
        return true;
    case AT_SKILL_BEAST_UPPERCUT:
    case AT_SKILL_BEAST_UPPERCUT_STR:
    case AT_SKILL_BEAST_UPPERCUT_MASTERY: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;
        CreateEffect(MODEL_DOWN_ATTACK_DUMMY_R, _pObj->Position, _pObj->Angle, _pObj->Light, 0,
                     _pObj);

        PlayBuffer(SOUND_RAGESKILL_STAMP);
    }
        return true;
    case AT_SKILL_CHAIN_DRIVE:
    case AT_SKILL_CHAIN_DRIVE_STR: {
        CreateEffect(BITMAP_SWORDEFF, _pObj->Position, _pObj->Angle, _pObj->Light, 0, _pObj);

        PlayBuffer(SOUND_RAGESKILL_GIANTSWING);
    }
        return true;
    case AT_SKILL_ATT_UP_OURFORCES: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(0.2f, 0.4f, 1.0f, vLight);

        vec3_t _vAngle;
        VectorCopy(_pObj->Angle, _vAngle);
        CreateEffect(BITMAP_SWORD_EFFECT_MONO, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1,
                     0, 0, 0, 1.0f);

        for (int i = 0; i < 3; ++i)
        {
            Vector(0.6f, 0.65f, 1.0f, vLight);
            CreateEffect(MODEL_SHOCKWAVE_GROUND01, _pObj->Position, _pObj->Angle, vLight, 0, _pObj,
                         -1, 0, 0, 0, 1.0f - (i * 0.15f));

            Vector(0.65f, 0.8f, 1.0f, vLight);
            CreateEffect(BITMAP_EVENT_CLOUD, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1, 0,
                         0, 0, 1.0f);
        }
        Vector(0.5f, 0.55f, 1.0f, vLight);
        CreateEffect(MODEL_WINDFOCE, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1, 0, 0, 0,
                     1.0f);

        Vector(0.5f, 0.55f, 0.9f, vLight);
        CreateEffect(BITMAP_MAGIC, _pObj->Position, _pObj->Angle, vLight, 13);
    }
        return true;
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(0.5f, 0.28f, 1.0f, vLight);
        vec3_t _vAngle;
        VectorCopy(_pObj->Angle, _vAngle);
        CreateEffect(BITMAP_SWORD_EFFECT_MONO, _pObj->Position, _pObj->Angle, vLight, 1, _pObj, -1,
                     0, 0, 0, 1.0f);
        for (int i = 0; i < 3; ++i)
        {
            Vector(1.0f, 0.35f, 1.0f, vLight);
            CreateEffect(MODEL_SHOCKWAVE_GROUND01, _pObj->Position, _pObj->Angle, vLight, 0, _pObj,
                         -1, 0, 0, 0, 1.0f - (i * 0.15f));

            Vector(0.8f, 0.4f, 1.0f, vLight);
            CreateEffect(BITMAP_EVENT_CLOUD, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1, 0,
                         0, 0, 1.0f);
        }
        Vector(0.85f, 0.2f, 1.0f, vLight);
        CreateEffect(MODEL_WINDFOCE, _pObj->Position, _pObj->Angle, vLight, 4, _pObj, -1, 0, 0, 0,
                     1.0f);

        Vector(0.78f, 0.2f, 1.0f, vLight);
        CreateEffect(BITMAP_MAGIC, _pObj->Position, _pObj->Angle, vLight, 13);
    }
        return true;
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(1.0f, 0.15f, 0.0f, vLight);
        vec3_t _vAngle;
        VectorCopy(_pObj->Angle, _vAngle);
        CreateEffect(BITMAP_SWORD_EFFECT_MONO, _pObj->Position, _pObj->Angle, vLight, 2, _pObj, -1,
                     0, 0, 0, 1.0f);

        for (int i = 0; i < 3; ++i)
        {
            Vector(1.0f, 0.1f, 0.0f, vLight);
            CreateEffect(MODEL_SHOCKWAVE_GROUND01, _pObj->Position, _pObj->Angle, vLight, 0, _pObj,
                         -1, 0, 0, 0, 1.0f - (i * 0.15f));

            Vector(1.0f, 0.3f, 0.2f, vLight);
            CreateEffect(BITMAP_EVENT_CLOUD, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1, 0,
                         0, 0, 1.0f);
        }
        Vector(1.0f, 0.2f, 0.0f, vLight);
        CreateEffect(MODEL_WINDFOCE, _pObj->Position, _pObj->Angle, vLight, 5, _pObj, -1, 0, 0, 0,
                     1.0f);

        Vector(1.0f, 0.15f, 0.0f, vLight);
        CreateEffect(BITMAP_MAGIC, _pObj->Position, _pObj->Angle, vLight, 13);
    }
        return true;
    case AT_SKILL_DRAGON_KICK: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(0.7f, 0.7f, 1.0f, vLight);
        CreateEffect(MODEL_DRAGON_KICK_DUMMY, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1,
                     0, 0, 0, 1.0f);

        vec3_t Light, Position, P, dp, vAngle;

        if (_pObj->m_sTargetIndex < 0)
            return true;

        VectorCopy(CharactersClient[_pObj->m_sTargetIndex].Object.Position, Position);
        VectorCopy(_pObj->Angle, vAngle);
        vAngle[2] = CreateAngle2D(_pObj->Position, Position);

        float Matrix[3][4];
        Vector(0.f, 20.f, 0.f, P);
        AngleMatrix(vAngle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);
        Vector(0.8f, 0.9f, 1.6f, Light);
        CreateEffect(MODEL_MULTI_SHOT3, Position, vAngle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT3, Position, vAngle, Light, 0);

        Vector(0.f, 0.f, 0.f, P);
        AngleMatrix(vAngle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);

        CreateEffect(MODEL_MULTI_SHOT1, Position, vAngle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT1, Position, vAngle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT1, Position, vAngle, Light, 0);

        Vector(0.f, 20.f, 0.f, P);
        AngleMatrix(vAngle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);
        CreateEffect(MODEL_MULTI_SHOT2, Position, vAngle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT2, Position, vAngle, Light, 0);

        PlayBuffer(SOUND_RAGESKILL_DRAGONKICK);
    }
        return true;
    case AT_SKILL_DRAGON_ROAR:
    case AT_SKILL_DRAGON_ROAR_STR: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(1.0f, 1.0f, 1.0f, vLight);
        CreateEffect(BITMAP_LAVA, _pObj->Position, _pObj->Angle, vLight, 0, _pObj, -1, 0, 0, 0,
                     1.0f);

        CreateEffect(MODEL_BLOW_OF_DESTRUCTION, _pObj->Position, _pObj->Angle, vLight, 2, _pObj);

        InitLower();

        PlayBuffer(SOUND_RAGESKILL_DRAGONLOWER);
    }
    break;
    case AT_SKILL_DARKSIDE:
    case AT_SKILL_DARKSIDE_STR: {
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        vec3_t vLight;
        Vector(1.0f, 0.6f, 0.6f, vLight);
        CreateEffect(BITMAP_EVENT_CLOUD, _pObj->Position, _pObj->Angle, vLight, 1, _pObj, -1, 0, 0,
                     0, 1.0f);

        Vector(1.0f, 0.4f, 0.0f, vLight);
        CreateEffect(BITMAP_MAGIC, _pObj->Position, _pObj->Angle, vLight, 14);

        Vector(1.0f, 0.5f, 0.2f, vLight);
        CreateEffect(MODEL_WINDFOCE, _pObj->Position, _pObj->Angle, vLight, 2, _pObj, -1, 0, 0, 0,
                     1.0f);

        PlayBuffer(SOUND_RAGESKILL_DARKSIDE);
    }
    break;
    case AT_SKILL_OCCUPY: {
        vec3_t Light, Position, P, dp;

        float Matrix[3][4];
        Vector(0.f, 20.f, 0.f, P);
        AngleMatrix(_pObj->Angle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);
        Vector(0.8f, 0.9f, 1.6f, Light);
        CreateEffect(MODEL_MULTI_SHOT3, Position, _pObj->Angle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT3, Position, _pObj->Angle, Light, 0);

        Vector(0.f, 0.f, 0.f, P);
        AngleMatrix(_pObj->Angle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);

        CreateEffect(MODEL_MULTI_SHOT1, Position, _pObj->Angle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT1, Position, _pObj->Angle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT1, Position, _pObj->Angle, Light, 0);

        Vector(0.f, 20.f, 0.f, P);
        AngleMatrix(_pObj->Angle, Matrix);
        VectorRotate(P, Matrix, dp);
        VectorAdd(dp, _pObj->Position, Position);
        CreateEffect(MODEL_MULTI_SHOT2, Position, _pObj->Angle, Light, 0);
        CreateEffect(MODEL_MULTI_SHOT2, Position, _pObj->Angle, Light, 0);
    }
    break;
    case AT_SKILL_PHOENIX_SHOT:
        if (m_bUseEffectOnce)
            return false;

        m_bUseEffectOnce = true;

        if (_pObj->m_sTargetIndex < 0)
            return true;

        vec3_t Position, vAngle, Light;
        _pObj->Owner = &CharactersClient[_pObj->m_sTargetIndex].Object;
        VectorCopy(CharactersClient[_pObj->m_sTargetIndex].Object.Position, Position);
        VectorCopy(_pObj->Angle, vAngle);
        vAngle[2] = CreateAngle2D(_pObj->Position, Position);
        Vector(1.f, 1.f, 1.f, Light);
        CreateEffect(MODEL_PHOENIX_SHOT, Position, vAngle, Light, 0, _pObj, -1, 0, 0, 0, 1.f);
        break;
    default:
        break;
    }
    return false;
}

void CMonkSystem::InitLower()
{
    m_nLowerEffCnt = 0;
}

int CMonkSystem::GetLowerEffCnt()
{
    return m_nLowerEffCnt;
}

bool CMonkSystem::SetLowerEffEct()
{
    if (m_nLowerEffCnt > 3)
    {
        return false;
    }
    m_nLowerEffCnt++;

    return true;
}

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL
// namespace

void SessionGameplayUnit::SendRequestUse(int Index, int Target, bool addPoints)
{
    if (!IsCanUseItem())
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotUseYourItemsWhileUsingTheVaultOrWhileTrading,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return;
    }
    if (EnableUse > 0)
    {
        return;
    }

    EnableUse = 10;
    SocketClient->ToGameServer()->SendConsumeItemRequest(
        Index, Target, addPoints ? FruitUsage::AddPoints : FruitUsage::RemovePoints);
    g_ConsoleDebug.Write(MCD_SEND, L"0x26 [SendRequestUse(%d)]", Index);
}

bool SessionGameplayUnit::SendRequestEquipmentItem(STORAGE_TYPE iSrcType, int iSrcIndex,
                                                   ITEM *pItem, STORAGE_TYPE iDstType,
                                                   int iDstIndex)
{
    if (EquipmentItem || !pItem)
        return false;
    EquipmentItem = true;
    sessionKeeper_.GameData()->TrackItemMove(iSrcType, iSrcIndex);
    SocketClient->ToGameServer()->SendItemMoveRequestExtended(
        static_cast<ItemStorageKind>(iSrcType), iSrcIndex, static_cast<ItemStorageKind>(iDstType),
        iDstIndex);
    g_ConsoleDebug.Write(MCD_SEND, L"0x24 [SendRequestEquipmentItem(%d %d %d %d)]", iSrcIndex,
                         iDstIndex, iSrcType, iDstType);
    return true;
}

int64_t CalcRepairCost(int64_t ItemValue, int Durability, int MaxDurability, short Type,
                       bool SelfRepair)
{
    // Cap ItemValue at 400M
    int64_t repairGold = std::min<int64_t>(ItemValue, 400000000LL);

    // Calculate percentage of durability lost
    float percent = 1.f - static_cast<float>(Durability) / MaxDurability;
    if (percent > 0.f)
    {
        // Repair cost calculation
        double fRoot = sqrt(static_cast<double>(repairGold));
        double fRootRoot = sqrt(fRoot);
        repairGold = static_cast<int64_t>(3.5 * fRoot * fRootRoot * percent) + 1;

        // Adjust for 0 durability
        if (Durability <= 0)
        {
            if (Type == ITEM_DARK_HORSE_ITEM || Type == ITEM_DARK_RAVEN_ITEM)
            {
                repairGold *= 2;
            }
            else
            {
                repairGold = static_cast<int64_t>(repairGold * 1.4);
            }
        }
    }
    else
    {
        repairGold = 0; // No cost if no durability loss
    }

    if (SelfRepair)
    {
        // +150% adjustment for self-repair (vs doing it in shop)
        repairGold = static_cast<int64_t>(repairGold * 2.5);
    }

    // Round cost to the nearest 100 or 10
    if (repairGold >= 1000)
    {
        repairGold = (repairGold / 100) * 100;
    }
    else if (repairGold >= 100)
    {
        repairGold = (repairGold / 10) * 10;
    }

    return repairGold;
}

int64_t CalcSelfRepairCost(int64_t ItemValue, int Durability, int MaxDurability, short Type)
{
    return CalcRepairCost(ItemValue, Durability, MaxDurability, Type, true);
}

WORD CalcMaxDurability(const ITEM *ip, ITEM_ATTRIBUTE *p, int Level)
{
    WORD maxDurability = p->Durability;

    if (ip->Type >= ITEM_STAFF && ip->Type < ITEM_STAFF + MAX_ITEM_INDEX)
    {
        maxDurability = p->MagicDur;
    }
    for (int i = 0; i < Level; i++)
    {
        if (ip->Type >= ITEM_SCROLL_OF_BLOOD)
        {
            break;
        }
        else if (i >= 14)
        {
            maxDurability = (maxDurability + 8 >= 255 ? 255 : maxDurability + 8);
        }
        else if (i >= 13) // 14
        {
            maxDurability = (maxDurability + 7 >= 255 ? 255 : maxDurability + 7);
        }
        else if (i >= 12) // 13
        {
            maxDurability += 6;
        }
        else if (i >= 11) // 12
        {
            maxDurability += 5;
        }
        else if (i >= 10) // 11
        {
            maxDurability += 4;
        }
        else if (i >= 9) // 10
        {
            maxDurability += 3;
        }
        else if (i >= 4) // 5~9
        {
            maxDurability += 2;
        }
        else // 1~4
        {
            maxDurability++;
        }
    }

    if (ip->Type == ITEM_DARK_HORSE_ITEM || ip->Type == ITEM_DARK_RAVEN_ITEM)
    {
        maxDurability = 255;
    }

    if (ip->AncientDiscriminator > 0)
    {
        maxDurability += 20;
    }
    else if (ip->ExcellentFlags > 0 &&
             (ip->Type < ITEM_WINGS_OF_SPIRITS || ip->Type > ITEM_WINGS_OF_DARKNESS) &&
             !ItemRulesDetail::IsDivineArchangelWeaponItem(ip->Type) &&
             ip->Type != ITEM_CAPE_OF_LORD &&
             (ip->Type < ITEM_WING_OF_STORM || ip->Type > ITEM_CAPE_OF_EMPEROR) &&
             (ip->Type < ITEM_WINGS_OF_DESPAIR || ip->Type > ITEM_WING_OF_DIMENSION) &&
             !(ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE))
    {
        maxDurability += 15;
    }

    if (Check_LuckyItem(ip->Type))
    {
        maxDurability = 255;
    }

    return maxDurability;
}

bool SessionGameplayUnit::GetAttackDamage(int *iMinDamage, int *iMaxDamage) const
{
    int AttackDamageMin;
    int AttackDamageMax;

    ITEM *r = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
    ITEM *l = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];
    if (PickItem.Number > 0 && SrcInventory == Inventory)
    {
        switch (SrcInventoryIndex)
        {
        case EQUIPMENT_WEAPON_RIGHT:
            r = &PickItem;
            break;
        case EQUIPMENT_WEAPON_LEFT:
            l = &PickItem;
            break;
        }
    }
    if (GetEquipedBowType() == BOWTYPE_CROSSBOW)
    {
        AttackDamageMin = CharacterAttribute->AttackDamageMinRight;
        AttackDamageMax = CharacterAttribute->AttackDamageMaxRight;
    }
    else if (GetEquipedBowType() == BOWTYPE_BOW)
    {
        AttackDamageMin = CharacterAttribute->AttackDamageMinLeft;
        AttackDamageMax = CharacterAttribute->AttackDamageMaxLeft;
    }
    else if (r->Type == -1)
    {
        AttackDamageMin = CharacterAttribute->AttackDamageMinLeft;
        AttackDamageMax = CharacterAttribute->AttackDamageMaxLeft;
    }
    else if (r->Type >= ITEM_STAFF && r->Type < ITEM_SHIELD)
    {
        AttackDamageMin = CharacterAttribute->AttackDamageMinLeft;
        AttackDamageMax = CharacterAttribute->AttackDamageMaxLeft;
    }
    else
    {
        AttackDamageMin = CharacterAttribute->AttackDamageMinRight;
        AttackDamageMax = CharacterAttribute->AttackDamageMaxRight;
    }

    bool Alpha = false;
    if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_KNIGHT ||
        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK)
    {
        if (l->Type >= ITEM_SWORD && l->Type < ITEM_STAFF + MAX_ITEM_INDEX &&
            r->Type >= ITEM_SWORD && r->Type < ITEM_STAFF + MAX_ITEM_INDEX)
        {
            Alpha = true;
            AttackDamageMin = ((CharacterAttribute->AttackDamageMinRight * 55) / 100 +
                               (CharacterAttribute->AttackDamageMinLeft * 55) / 100);
            AttackDamageMax = ((CharacterAttribute->AttackDamageMaxRight * 55) / 100 +
                               (CharacterAttribute->AttackDamageMaxLeft * 55) / 100);
        }
    }
    else if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_ELF)
    {
        if ((r->Type >= ITEM_BOW && r->Type < ITEM_BOW + MAX_ITEM_INDEX) &&
            (l->Type >= ITEM_BOW && l->Type < ITEM_BOW + MAX_ITEM_INDEX))
        {
            if ((l->Type == ITEM_BOLT && l->Level >= 1) ||
                (r->Type == ITEM_ARROWS && r->Level >= 1))
            {
                Alpha = true;
            }
        }
    }
    else if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER)
    {
        if (l->Type >= ITEM_SWORD && l->Type < ITEM_MACE + MAX_ITEM_INDEX &&
            r->Type >= ITEM_SWORD && r->Type < ITEM_MACE + MAX_ITEM_INDEX)
        {
            Alpha = true;
            AttackDamageMin = ((CharacterAttribute->AttackDamageMinRight +
                                CharacterAttribute->AttackDamageMinLeft) *
                               60 / 100);
            AttackDamageMax = ((CharacterAttribute->AttackDamageMaxRight +
                                CharacterAttribute->AttackDamageMaxLeft) *
                               65 / 100);
        }
    }

    if (CharacterAttribute->Ability & ABILITY_PLUS_DAMAGE)
    {
        AttackDamageMin += 15;
        AttackDamageMax += 15;
    }

    *iMinDamage = AttackDamageMin;
    *iMaxDamage = AttackDamageMax;

    return Alpha;
}
// namespace

int CompareItem(ITEM item1, ITEM item2)
{
    int equal = 0;
    if (item1.Type != item2.Type)
    {
        return 2;
    }
    else if (item1.Class == item2.Class && item1.Type == item2.Type)
    {
        int level1 = item1.Level;
        int level2 = item2.Level;
        int option1 = item1.ExcellentFlags;
        int option2 = item2.ExcellentFlags;
        bool skill1 = item1.HasSkill;
        bool skill2 = item2.HasSkill;

        equal = 1;
        if (level1 == level2)
        {
            equal = 0;
        }
        else if (level1 < level2)
        {
            equal = -1;
        }
        if (equal == 0)
        {
            if (skill1 < skill2)
            {
                equal = -1;
            }
            else if (skill1 > skill2)
            {
                equal = 1;
            }
        }
        if (equal == 0)
        {
            if (option1 < option2)
            {
                equal = -1;
            }
            else if (option1 > option2)
            {
                equal = 1;
            }
        }
        if (equal == 0)
        {
            if (item1.SpecialNum < item2.SpecialNum)
            {
                equal = -1;
            }
            else if (item1.SpecialNum > item2.SpecialNum)
            {
                equal = 1;
            }
            else
            {
                int Num = std::max<int>(item1.SpecialNum, item2.SpecialNum);
                int addOption1 = 0;
                int addOptionV1 = 0;
                int addOption2 = 0;
                int addOptionV2 = 0;
                for (int i = 0; i < Num; ++i)
                {
                    switch (item1.Special[i])
                    {
                    case AT_IMPROVE_DAMAGE:
                    case AT_IMPROVE_MAGIC:
                    case AT_IMPROVE_CURSE:
                    case AT_IMPROVE_BLOCKING:
                    case AT_IMPROVE_DEFENSE:
                        addOption1 = 1;
                        addOptionV1 = item1.SpecialValue[i];
                        break;
                    }
                    switch (item2.Special[i])
                    {
                    case AT_IMPROVE_DAMAGE:
                    case AT_IMPROVE_MAGIC:
                    case AT_IMPROVE_CURSE:
                    case AT_IMPROVE_BLOCKING:
                    case AT_IMPROVE_DEFENSE:
                        addOption2 = 1;
                        addOptionV2 = item2.SpecialValue[i];
                        break;
                    }
                }

                if (addOption1 < addOption2 || addOptionV1 < addOptionV2)
                {
                    equal = -1;
                }
                else if (addOption1 != addOption2 && addOptionV1 != addOptionV2)
                {
                    equal = 1;
                }
            }
        }
        if (equal == 0)
        {
            if (item1.Durability < item2.Durability)
                equal = -1;
            if (item1.Durability > item2.Durability)
                equal = 1;
        }
    }
    return equal;
}

bool IsPartChargeItem(ITEM *pItem)
{
    if ((pItem->Type >= ITEM_HELPER + 46 && pItem->Type <= ITEM_HELPER + 48) ||
        (pItem->Type == ITEM_POTION + 54) ||
        (pItem->Type >= ITEM_POTION + 58 && pItem->Type <= ITEM_POTION + 62) ||
        (pItem->Type >= ITEM_POTION + 145 && pItem->Type <= ITEM_POTION + 150) ||
        (pItem->Type >= ITEM_HELPER + 125 && pItem->Type <= ITEM_HELPER + 127) ||
        pItem->Type == ITEM_POTION + 53 ||
        (pItem->Type >= ITEM_HELPER + 43 && pItem->Type <= ITEM_HELPER + 45) ||
        (pItem->Type >= ITEM_POTION + 70 && pItem->Type <= ITEM_POTION + 71) ||
        (pItem->Type >= ITEM_POTION + 72 && pItem->Type <= ITEM_POTION + 77) ||
        (pItem->Type == ITEM_HELPER + 59) ||
        (pItem->Type >= ITEM_HELPER + 54 && pItem->Type <= ITEM_HELPER + 58) ||
        (pItem->Type >= ITEM_POTION + 78 && pItem->Type <= ITEM_POTION + 82) ||
        (pItem->Type == ITEM_HELPER + 60) || (pItem->Type == ITEM_HELPER + 61) ||
        (pItem->Type == ITEM_POTION + 91) ||
        (pItem->Type >= ITEM_POTION + 92 && pItem->Type <= ITEM_POTION + 93) ||
        (pItem->Type == ITEM_POTION + 95) || (pItem->Type == ITEM_POTION + 94) ||
        (pItem->Type >= ITEM_HELPER + 62 && pItem->Type <= ITEM_HELPER + 63) ||
        (pItem->Type >= ITEM_POTION + 97 && pItem->Type <= ITEM_POTION + 98) ||
        (pItem->Type == ITEM_POTION + 96) ||
        (pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN) ||
        (pItem->Type == ITEM_HELPER + 69) || (pItem->Type == ITEM_HELPER + 70) ||
        pItem->Type == ITEM_HELPER + 81 || pItem->Type == ITEM_HELPER + 82 ||
        pItem->Type == ITEM_HELPER + 93 || pItem->Type == ITEM_HELPER + 94 ||
        pItem->Type == ITEM_HELPER + 107 || pItem->Type == ITEM_HELPER + 104 ||
        pItem->Type == ITEM_HELPER + 105 || pItem->Type == ITEM_HELPER + 103 ||
        pItem->Type == ITEM_POTION + 133 || pItem->Type == ITEM_HELPER + 109 ||
        pItem->Type == ITEM_HELPER + 110 || pItem->Type == ITEM_HELPER + 111 ||
        pItem->Type == ITEM_HELPER + 112 || pItem->Type == ITEM_HELPER + 113 ||
        pItem->Type == ITEM_HELPER + 114 || pItem->Type == ITEM_HELPER + 115 ||
        pItem->Type == ITEM_POTION + 112 || pItem->Type == ITEM_POTION + 113 ||
        pItem->Type == ITEM_POTION + 120 || pItem->Type == ITEM_POTION + 123 ||
        pItem->Type == ITEM_POTION + 124 || pItem->Type == ITEM_POTION + 134 ||
        pItem->Type == ITEM_POTION + 135 || pItem->Type == ITEM_POTION + 136 ||
        pItem->Type == ITEM_POTION + 137 || pItem->Type == ITEM_POTION + 138 ||
        pItem->Type == ITEM_POTION + 139 || pItem->Type == ITEM_WING + 130 ||
        pItem->Type == ITEM_WING + 131 || pItem->Type == ITEM_WING + 132 ||
        pItem->Type == ITEM_WING + 133 || pItem->Type == ITEM_WING + 134 ||
        pItem->Type == ITEM_WING + 135 || pItem->Type == ITEM_HELPER + 116 ||
        pItem->Type == ITEM_PET_UNICORN || pItem->Type == ITEM_HELPER + 124 ||
        pItem->Type == ITEM_POTION + 114 || pItem->Type == ITEM_POTION + 115 ||
        pItem->Type == ITEM_POTION + 116 || pItem->Type == ITEM_POTION + 117 ||
        pItem->Type == ITEM_POTION + 118 || pItem->Type == ITEM_POTION + 119 ||
        pItem->Type == ITEM_POTION + 126 || pItem->Type == ITEM_POTION + 127 ||
        pItem->Type == ITEM_POTION + 128 || pItem->Type == ITEM_POTION + 129 ||
        pItem->Type == ITEM_POTION + 130 || pItem->Type == ITEM_POTION + 131 ||
        pItem->Type == ITEM_POTION + 132 || pItem->Type == ITEM_HELPER + 121 ||
        pItem->Type == ITEM_POTION + 140)
    {
        return true;
    }

    return false;
}

bool IsPersonalShopBan(ITEM *pItem)
{
    if (pItem == NULL)
    {
        return false;
    }

#ifdef KJH_FIX_PERSONALSHOP_BAN_CASHITEM
    if (pItem->bPeriodItem)
    {
        return true;
    }
#endif // KJH_FIX_PERSONALSHOP_BAN_CASHITEM

    if ((!pItem->bPeriodItem) && pItem->Type == ITEM_DEMON ||
        pItem->Type == ITEM_SPIRIT_OF_GUARDIAN || pItem->Type == ITEM_PET_PANDA ||
        pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (g_pMyInventory->IsInvenItem(pItem->Type) && pItem->Durability == 255)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (pItem->Type == ITEM_WIZARDS_RING && pItem->Level == 0))
    {
        return false;
    }

    if (pItem->Type == ITEM_MOONSTONE_PENDANT
        || pItem->Type == ITEM_ELITE_TRANSFER_SKELETON_RING
        || (pItem->Type == ITEM_POTION + 21 && pItem->Level != 3)
        || (pItem->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR && pItem->Type <= ITEM_SOUL_SHARD_OF_WIZARD)
        || pItem->Type == ITEM_WEAPON_OF_ARCHANGEL
        || (pItem->Type == ITEM_BOX_OF_LUCK && pItem->Level == 13)
        || (pItem->Type >= ITEM_HELPER + 43 && pItem->Type <= ITEM_HELPER + 45)
        || (pItem->Type == ITEM_WIZARDS_RING && pItem->Level != 0)
        || pItem->Type == ITEM_FLAME_OF_DEATH_BEAM_KNIGHT
        || pItem->Type == ITEM_HORN_OF_HELL_MAINE
        || pItem->Type == ITEM_FEATHER_OF_DARK_PHOENIX
        || pItem->Type == ITEM_EYE_OF_ABYSSAL
        || IsPartChargeItem(pItem)
        || pItem->Type == ITEM_HELPER + 97
        || pItem->Type == ITEM_HELPER + 98
        || pItem->Type == ITEM_POTION + 91
        || pItem->Type == ITEM_HELPER + 99
        || pItem->Type == ITEM_PET_PANDA
        || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING
        || pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING
        || pItem->Type == ITEM_PET_SKELETON
        || (pItem->Type == ITEM_POTION + 96)
        || pItem->Type == ITEM_HELPER + 109
        || pItem->Type == ITEM_HELPER + 110
        || pItem->Type == ITEM_HELPER + 111
        || pItem->Type == ITEM_HELPER + 112
        || pItem->Type == ITEM_HELPER + 113
        || pItem->Type == ITEM_HELPER + 114
        || pItem->Type == ITEM_HELPER + 115
#ifdef LDK_MOD_INGAMESHOP_WIZARD_RING_PERSONALSHOPBAN
        || (pItem->Type == ITEM_WIZARDS_RING && (pItem->Level == 0)
#endif //LDK_MOD_INGAMESHOP_WIZARD_RING_PERSONALSHOPBAN
#ifdef ASG_ADD_TIME_LIMIT_QUEST_ITEM
        || (pItem->Type >= ITEM_POTION + 151 && pItem->Type <= ITEM_POTION + 156)
#endif // ASG_ADD_TIME_LIMIT_QUEST_ITEM
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || g_pMyInventory->IsInvenItem(pItem->Type)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        )
    {
        return true;
    }
    if (Check_ItemAction(pItem, eITEM_PERSONALSHOP))	return true;

    return false;
}

bool IsStoreBan(ITEM *pItem)
{
    if ((pItem->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR &&
         pItem->Type <= ITEM_SOUL_SHARD_OF_WIZARD) ||
        (pItem->Type == ITEM_POTION + 21 && pItem->Level != 3) ||
        pItem->Type == ITEM_WEAPON_OF_ARCHANGEL ||
        (pItem->Type == ITEM_BOX_OF_LUCK && pItem->Level == 13) ||
        (pItem->Type >= ITEM_HELPER + 43 && pItem->Type <= ITEM_HELPER + 45) ||
        pItem->Type == ITEM_HELPER + 93 || pItem->Type == ITEM_HELPER + 94 ||
        (pItem->Type == ITEM_WIZARDS_RING && pItem->Level != 0) ||
        pItem->Type == ITEM_FLAME_OF_DEATH_BEAM_KNIGHT || pItem->Type == ITEM_HORN_OF_HELL_MAINE ||
        pItem->Type == ITEM_FEATHER_OF_DARK_PHOENIX || pItem->Type == ITEM_EYE_OF_ABYSSAL ||
        (pItem->Type == ITEM_HELPER + 70 && pItem->Durability == 1)
#ifdef ASG_ADD_TIME_LIMIT_QUEST_ITEM
        || (pItem->Type >= ITEM_POTION + 151 && pItem->Type <= ITEM_POTION + 156)
#endif // ASG_ADD_TIME_LIMIT_QUEST_ITEM
#ifdef KJH_ADD_PERIOD_ITEM_SYSTEM
        || (pItem->bPeriodItem == true)
#endif // KJH_ADD_PERIOD_ITEM_SYSTEM
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (g_pMyInventory->IsInvenItem(pItem->Type) && pItem->Durability == 254)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    )
    {
        return true;
    }

    if (Check_ItemAction(pItem, eITEM_STORE))
        return true;

    return false;
}

sItemAct Set_ItemActOption(int _nIndex, int _nOption)
{
    sItemAct sItem;
    // eITEM_PERSONALSHOP = ????, eITEM_STORE = ??, eITEM_TRADE = ??, eITEM_DROP = ???, eITEM_SELL = ??, eITEM_REPAIR = ??
    int nItemOption[][eITEM_END] = {0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, -1};

    sItem.s_nItemIndex = _nIndex;

    for (int i = 0; i < eITEM_END; i++)
    {
        sItem.s_bType[i] = nItemOption[_nOption][i];
    }
    return sItem;
}

bool Check_ItemAction(ITEM *_pItem, ITEMSETOPTION _eAction, bool _bType)
{
    std::vector<sItemAct> sItem;
    std::vector<sItemAct>::iterator li;
    int i = 0;

    // Restricted ITEM_HELPER special items starting at local index 135.
    for (i = 0; i < RESTRICTED_SPECIAL_MISC_COUNT; i++)
    {
        sItem.push_back(Set_ItemActOption(ITEM_HELPER + RESTRICTED_SPECIAL_MISC_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_STORAGE_TRADE));
    }
    // Restricted ITEM_POTION special jewels starting at local index 160.
    for (i = 0; i < RESTRICTED_SPECIAL_JEWEL_COUNT; i++)
    {
        sItem.push_back(Set_ItemActOption(ITEM_POTION + RESTRICTED_SPECIAL_JEWEL_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_STORAGE_TRADE));
    }
    for (i = 0; i < LUCKY_SET_ARMOR_COUNT; i++)
    {
        sItem.push_back(Set_ItemActOption(ITEM_ARMOR + LUCKY_SET_ARMOR_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_SELL_ONLY));
        sItem.push_back(Set_ItemActOption(ITEM_HELM + LUCKY_SET_ARMOR_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_SELL_ONLY));
        sItem.push_back(Set_ItemActOption(ITEM_BOOTS + LUCKY_SET_ARMOR_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_SELL_ONLY));
        sItem.push_back(Set_ItemActOption(ITEM_GLOVES + LUCKY_SET_ARMOR_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_SELL_ONLY));
        sItem.push_back(Set_ItemActOption(ITEM_PANTS + LUCKY_SET_ARMOR_START_INDEX + i,
                                          ITEM_ACTION_BLOCK_SELL_ONLY));
    }

    for (li = sItem.begin(); li != sItem.end(); li++)
    {
        if (li->s_nItemIndex == _pItem->Type)
        {
            _bType = (li->s_bType[_eAction]) ^ (!_bType);
            return _bType;
        }
    }

    // ???? ?? ???? ??.
    return false;
}

bool Check_LuckyItem(int _nIndex, int _nType)
{
    int nItemTabIndex = (_nIndex + _nType) % MAX_ITEM_INDEX;

    if (_nIndex < ITEM_HELM || _nIndex > ITEM_WING)
        return false;
    if (nItemTabIndex >= LUCKY_SET_ARMOR_START_INDEX &&
        nItemTabIndex < LUCKY_SET_ARMOR_START_INDEX + LUCKY_SET_ARMOR_COUNT)
        return true;

    return false;
}

bool IsLuckySetItem(int iType)
{
    int iItemIndex = iType % MAX_ITEM_INDEX;

#ifdef LEM_FIX_SELL_LUCKYITEM_BOOTS_POPUP
    if ((iType >= ITEM_HELM && iType <= ITEM_WING)
#else  // LEM_FIX_SELL_LUCKYITEM_BOOTS_POPUP
    if ((iType >= ITEM_HELM && iType <= ITEM_BOOTS)
#endif // LEM_FIX_SELL_LUCKYITEM_BOOTS_POPUP
        && (iItemIndex >= LUCKY_SET_ARMOR_START_INDEX &&
            iItemIndex < LUCKY_SET_ARMOR_START_INDEX + LUCKY_SET_ARMOR_COUNT))
    {
        return true;
    }

    return false;
}

bool IsDropBan(ITEM *pItem)
{
    if ((!pItem->bPeriodItem) &&
        (pItem->Type == ITEM_POTION + 96 || pItem->Type == ITEM_POTION + 54 ||
         pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN ||
         pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
         pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON ||
         (pItem->Type == ITEM_WIZARDS_RING && pItem->Level == 0)))
    {
        return false;
    }

    if (true == false || pItem->Type == ITEM_POTION + 123 || pItem->Type == ITEM_POTION + 124)
    {
        return false;
    }

    if ((pItem->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR &&
         pItem->Type <= ITEM_SOUL_SHARD_OF_WIZARD) ||
        (pItem->Type >= ITEM_FLAME_OF_DEATH_BEAM_KNIGHT && pItem->Type <= ITEM_EYE_OF_ABYSSAL) ||
        IsPartChargeItem(pItem) ||
        ((pItem->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN) &&
         (pItem->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)) ||
        pItem->Type == ITEM_HELPER + 97 || pItem->Type == ITEM_HELPER + 98 ||
        pItem->Type == ITEM_POTION + 91 || pItem->Type == ITEM_HELPER + 99 ||
        pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON ||
        pItem->Type == ITEM_POTION + 121 || pItem->Type == ITEM_POTION + 122
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || g_pMyInventory->IsInvenItem(pItem->Type)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
    )
    {
        return true;
    }

    if (Check_ItemAction(pItem, eITEM_DROP))
        return true;

    return false;
}

bool IsSellingBan(ITEM *pItem)
{
    int Level = pItem->Level;

    if (true == false || pItem->Type == ITEM_POTION + 112 || pItem->Type == ITEM_POTION + 113 ||
        pItem->Type == ITEM_POTION + 121 || pItem->Type == ITEM_POTION + 122 ||
        pItem->Type == ITEM_POTION + 123 || pItem->Type == ITEM_POTION + 124 ||
        pItem->Type == ITEM_WING + 130 || pItem->Type == ITEM_WING + 131 ||
        pItem->Type == ITEM_WING + 132 || pItem->Type == ITEM_WING + 133 ||
        pItem->Type == ITEM_WING + 134 || pItem->Type == ITEM_WING + 135 ||
        pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON ||
        pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN ||
        pItem->Type == ITEM_HELPER + 109 || pItem->Type == ITEM_HELPER + 110 ||
        pItem->Type == ITEM_HELPER + 111 || pItem->Type == ITEM_HELPER + 112 ||
        pItem->Type == ITEM_HELPER + 113 || pItem->Type == ITEM_HELPER + 114 ||
        pItem->Type == ITEM_HELPER + 115
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (g_pMyInventory->IsInvenItem(pItem->Type) && pItem->Durability != 254)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || ((pItem->Type == ITEM_WIZARDS_RING) && (Level == 0)) ||
        (pItem->Type == ITEM_PET_UNICORN) || (pItem->Type == ITEM_HELPER + 107))
    {
        if (true == pItem->bPeriodItem && true == pItem->bExpiredPeriod)
        {
            return false;
        }
    }

    if (pItem->Type == ITEM_BOX_OF_LUCK || (pItem->Type == ITEM_POTION + 21 && Level == 1) ||
        ((pItem->bPeriodItem == true) && (pItem->bExpiredPeriod == false) &&
         (pItem->Type == ITEM_WIZARDS_RING) && (Level == 0)) ||
        (pItem->Type == ITEM_WIZARDS_RING && (Level == 1 || Level == 2)) ||
        pItem->Type == ITEM_WEAPON_OF_ARCHANGEL ||
        (pItem->Type == ITEM_POTION + 20 && Level >= 1 && Level <= 5) || IsPartChargeItem(pItem) ||
        ((pItem->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN) &&
         (pItem->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)) ||
        pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (g_pMyInventory->IsInvenItem(pItem->Type))
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (pItem->Type == ITEM_PET_UNICORN) || (pItem->Type == ITEM_HELPER + 107)
#ifdef KJH_FIX_SELL_LUCKYITEM
        || ((IsLuckySetItem(pItem->Type) == true) && (pItem->Durability > 0))
#endif // KJH_FIX_SELL_LUCKYITEM
    )
    {
        return true;
    }

    if (Check_ItemAction(pItem, eITEM_SELL))
        return true;

    return false;
}

bool IsWingItem(ITEM *pItem)
{
    switch (pItem->Type)
    {
    case ITEM_WING:
    case ITEM_WINGS_OF_HEAVEN:
    case ITEM_WINGS_OF_SATAN:
    case ITEM_WINGS_OF_SPIRITS:
    case ITEM_WINGS_OF_SOUL:
    case ITEM_WINGS_OF_DRAGON:
    case ITEM_WINGS_OF_DARKNESS:
    case ITEM_CAPE_OF_LORD:
    case ITEM_WING_OF_STORM:
    case ITEM_WING_OF_ETERNAL:
    case ITEM_WING_OF_ILLUSION:
    case ITEM_WING_OF_RUIN:
    case ITEM_CAPE_OF_EMPEROR:
    case ITEM_WING_OF_CURSE:
    case ITEM_WINGS_OF_DESPAIR:
    case ITEM_WING_OF_DIMENSION:
    case ITEM_WING + 130:
    case ITEM_WING + 131:
    case ITEM_WING + 132:
    case ITEM_WING + 133:
    case ITEM_WING + 134:
    case ITEM_CAPE_OF_FIGHTER:
    case ITEM_CAPE_OF_OVERRULE:
    case ITEM_WING + 135:
        return true;
    }

    return false;
}

bool IsJewelItem(ITEM *pItem)
{
    if (pItem->Type == ITEM_JEWEL_OF_BLESS || pItem->Type == ITEM_JEWEL_OF_SOUL ||
        pItem->Type == ITEM_JEWEL_OF_LIFE || pItem->Type == ITEM_JEWEL_OF_CHAOS ||
        pItem->Type == ITEM_JEWEL_OF_CREATION || pItem->Type == ITEM_JEWEL_OF_GUARDIAN)
    {
        return true;
    }

    return false;
}

bool IsExcellentItem(ITEM *pItem)
{
    return pItem->ExcellentFlags;
}

bool IsAncientItem(ITEM *pItem)
{
    return pItem->AncientDiscriminator;
}

bool IsMoneyItem(ITEM *pItem)
{
    return pItem->Type == ITEM_ZEN;
}

bool CheckEmptyInventory(ITEM *Inv, int InvWidth, int InvHeight)
{
    bool Empty = true;
    for (int y = 0; y < InvHeight; y++)
    {
        for (int x = 0; x < InvWidth; x++)
        {
            int Index = y * InvWidth + x;
            ITEM *p = &Inv[Index];
            if (p->Type != -1 && p->Number > 0)
            {
                Empty = false;
            }
        }
    }
    return Empty;
}

//#define MAX_LENGTH_CMB	( 26)
#define NUM_LINE_CMB (7)

BYTE SessionGameplayUnit::CaculateFreeTicketLevel(int iType) const
{
    int iChaLevel = CharacterAttribute->Level;
    int iChaClass = gCharacterManager.GetBaseClass(Hero->Class);
    int iChaExClass = gCharacterManager.IsSecondClass(Hero->Class);

    int iItemLevel = 0;

    if (iType == FREETICKET_TYPE_DEVILSQUARE)
    {
        if (iChaClass == CLASS_DARK)
        {
            if (iChaLevel >= 15 && iChaLevel <= 110)
            {
                iItemLevel = 0;
            }
            else if (iChaLevel >= 111 && iChaLevel <= 160)
            {
                iItemLevel = 1;
            }
            else if (iChaLevel >= 161 && iChaLevel <= 210)
            {
                iItemLevel = 2;
            }
            else if (iChaLevel >= 211 && iChaLevel <= 260)
            {
                iItemLevel = 3;
            }
            else if (iChaLevel >= 261 && iChaLevel <= 310)
            {
                iItemLevel = 4;
            }
            else if (iChaLevel >= 311 && iChaLevel <= 400)
            {
                iItemLevel = 5;
            }
        }
        else
        {
            if (iChaLevel >= 15 && iChaLevel <= 130)
            {
                iItemLevel = 0;
            }
            else if (iChaLevel >= 131 && iChaLevel <= 180)
            {
                iItemLevel = 1;
            }
            else if (iChaLevel >= 181 && iChaLevel <= 230)
            {
                iItemLevel = 2;
            }
            else if (iChaLevel >= 231 && iChaLevel <= 280)
            {
                iItemLevel = 3;
            }
            else if (iChaLevel >= 281 && iChaLevel <= 330)
            {
                iItemLevel = 4;
            }
            else if (iChaLevel >= 331 && iChaLevel <= 400)
            {
                iItemLevel = 5;
            }
        }
        return iItemLevel;
    }

    if (iType == FREETICKET_TYPE_BLOODCASTLE)
    {
        if (iChaClass == CLASS_DARK)
        {
            if (iChaLevel >= 15 && iChaLevel <= 60)
            {
                iItemLevel = 0;
            }
            else if (iChaLevel >= 61 && iChaLevel <= 110)
            {
                iItemLevel = 1;
            }
            else if (iChaLevel >= 111 && iChaLevel <= 160)
            {
                iItemLevel = 2;
            }
            else if (iChaLevel >= 161 && iChaLevel <= 210)
            {
                iItemLevel = 3;
            }
            else if (iChaLevel >= 211 && iChaLevel <= 260)
            {
                iItemLevel = 4;
            }
            else if (iChaLevel >= 261 && iChaLevel <= 310)
            {
                iItemLevel = 5;
            }
            else if (iChaLevel >= 311 && iChaLevel <= 400)
            {
                iItemLevel = 6;
            }
        }
        else
        {
            if (iChaLevel >= 15 && iChaLevel <= 80)
            {
                iItemLevel = 0;
            }
            else if (iChaLevel >= 81 && iChaLevel <= 130)
            {
                iItemLevel = 1;
            }
            else if (iChaLevel >= 131 && iChaLevel <= 180)
            {
                iItemLevel = 2;
            }
            else if (iChaLevel >= 181 && iChaLevel <= 230)
            {
                iItemLevel = 3;
            }
            else if (iChaLevel >= 231 && iChaLevel <= 280)
            {
                iItemLevel = 4;
            }
            else if (iChaLevel >= 281 && iChaLevel <= 330)
            {
                iItemLevel = 5;
            }
            else if (iChaLevel >= 331 && iChaLevel <= 400)
            {
                iItemLevel = 6;
            }
        }
        return iItemLevel;
    }

    if (iType == FREETICKET_TYPE_CURSEDTEMPLE)
    {
        if (g_pCursedTempleEnterWindow->CheckEnterLevel(iItemLevel))
        {
            return iItemLevel;
        }
    }
    else if (iType == FREETICKED_TYPE_CHAOSCASTLE)
    {
        if (g_pCursedTempleEnterWindow->CheckEnterLevel(iItemLevel))
        {
            return iItemLevel;
        }
    }

    return 0;
}

void SessionGameplayUnit::ClearItems()
{
    std::vector<OBJECT *> targets;
    for (auto &item : Items)
        if (item.Object.Live)
            targets.push_back(&item.Object);
    RetireCharacterEffectTargets(targets);
    for (auto *target : targets)
        target->Live = false;
}

void SessionGameplayUnit::ItemObjectAttribute(OBJECT *o)
{
    Vector(0.3f, 0.3f, 0.3f, o->Light);
    o->SetLightEnable(true);
    o->AlphaEnable = false;
    o->EnableShadow = false;
    o->Velocity = 0.f;
    o->CollisionRange = 50.f;
    o->PriorAnimationFrame = 0.f;
    o->AnimationFrame = 0.f;
    o->PriorAction = 0;
    o->CurrentAction = 0;
    o->SetHiddenMesh(-1);
    o->Gravity = 0.f;
    o->Alpha = 1.f;
    o->SetBlendMesh(-1);
    o->BlendMeshLight = 1.f;
    o->BlendMeshTexCoordU = 0.f;
    o->BlendMeshTexCoordV = 0.f;
    o->SetHiddenMesh(-1);
    o->Scale = 0.8f;
    g_CharacterClearBuff(o);
    if (o->Type >= MODEL_SPEAR && o->Type <= MODEL_PLATINA_STAFF)
        o->Scale = 0.7f;
    sessionKeeper_.Visual()->UpdateItemObjectMaterial(*o);
}

bool ItemAngleRF(OBJECT *o)
{
    if (o->Type == MODEL_CAPE_OF_FIGHTER)
    {
        o->Angle[0] = 270.0f;
        o->Angle[1] = 180.0f;
        o->Angle[2] = 45.0f;
        o->Scale = 0.7f;
        return true;
    }

    if (o->Type == MODEL_CAPE_OF_OVERRULE)
    {
        o->Angle[0] = 250.0f;
        o->Angle[1] = 180.0f;
        o->Angle[2] = 45.0f;
        return true;
    }

    if (o->Type >= MODEL_SACRED_HELM && o->Type <= MODEL_SACRED_HELM + 2)
    {
        o->Scale = 1.0f;
        o->Angle[2] = 45.0f;
        return true;
    }

    if (o->Type >= MODEL_CHAIN_DRIVE_PARCHMENT && o->Type <= MODEL_INCREASE_BLOCK_PARCHMENT)
    {
        o->Angle[0] = 270.f;
        o->Scale = 0.8f;
        return true;
    }

    return false;
}

void ItemAngle(OBJECT *o)
{
    Vector(0.f, 0.f, -45.f, o->Angle);

    o->Angle[0] = 0.f;

    if (o->Type >= MODEL_SWORD && o->Type < MODEL_AXE + MAX_ITEM_INDEX)
    {
        o->Angle[0] = 60.f;
        if (o->Type == MODEL_DIVINE_SWORD_OF_ARCHANGEL)
            o->Scale = 0.7f;
    }
    else if (o->Type == MODEL_ARROW_VIPER_BOW || o->Type == MODEL_SYLPH_WIND_BOW ||
             o->Type == MODEL_ALBATROSS_BOW)
    {
        o->Angle[0] = 0.f;
        o->Angle[1] = 0.f;
    }
    else if ((o->Type >= MODEL_CROSSBOW && o->Type < MODEL_CELESTIAL_BOW) ||
             (o->Type >= MODEL_DIVINE_CB_OF_ARCHANGEL && o->Type < MODEL_ARROW_VIPER_BOW))
    {
        o->Angle[0] = 90.f;
        o->Angle[1] = 0.f;
    }
    else if (o->Type >= MODEL_MACE && o->Type < MODEL_STAFF + MAX_ITEM_INDEX)
    {
        o->Angle[0] = 0.f;
        o->Angle[1] = 270.f;
    }
    else if (o->Type >= MODEL_SHIELD && o->Type < MODEL_SHIELD + MAX_ITEM_INDEX)
    {
        o->Angle[1] = 270.f;
        o->Angle[2] = 270.f - 45.f;
    }
    else if (MODEL_MISTERY_HELM <= o->Type && MODEL_LILIUM_HELM >= o->Type)
    {
        o->Scale = 1.5f;
        o->Angle[2] = 45.f;
    }
    else if (o->Type >= MODEL_ARMOR && o->Type < MODEL_GLOVES + MAX_ITEM_INDEX)
    {
        o->Angle[0] = 270.f;
    }
    else if (o->Type >= MODEL_RED_RIBBON_BOX && o->Type <= MODEL_BLUE_RIBBON_BOX)
    {
        o->Scale = 0.3f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_SEED_FIRE && o->Type <= MODEL_SEED_EARTH)
    {
        o->Angle[0] = 0.f;
        o->Scale = 0.6f;
    }
    else if (o->Type >= MODEL_SPHERE_MONO && o->Type <= MODEL_SPHERE_5)
    {
        o->Angle[0] = 0.f;
        o->Scale = 0.6f;
    }
    else if (o->Type >= MODEL_SEED_SPHERE_FIRE_1 && o->Type <= MODEL_SEED_SPHERE_EARTH_5)
    {
        o->Angle[0] = 0.f;
        o->Scale = 0.6f;
    }
    else if (o->Type == MODEL_PUMPKIN_OF_LUCK)
    {
        o->Scale = 0.9f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_JACK_OLANTERN_BLESSINGS && o->Type <= MODEL_JACK_OLANTERN_CRY)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_JACK_OLANTERN_FOOD)
    {
        o->Scale = 0.9f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_JACK_OLANTERN_DRINK)
    {
        o->Scale = 0.26f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_PINK_CHOCOLATE_BOX && o->Type <= MODEL_BLUE_CHOCOLATE_BOX)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_EVENT + 21 && o->Type <= MODEL_EVENT + 23)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 0.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_HELPER + 46 && o->Type <= MODEL_HELPER + 48)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 54)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 58)
    {
        o->Scale = 0.3f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 59 || o->Type == MODEL_POTION + 60)
    {
        o->Scale = 0.3f;
        o->Angle[0] = 90.f;
        o->Angle[1] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 61 || o->Type == MODEL_POTION + 62)
    {
        o->Scale = 0.3f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 53)
    {
        o->Scale = 0.2f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 43 || o->Type == MODEL_HELPER + 44 ||
             o->Type == MODEL_HELPER + 45)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type >= MODEL_POTION + 70 && o->Type <= MODEL_POTION + 71)
    {
        o->Scale = 0.6f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_POTION + 72 && o->Type <= MODEL_POTION + 77)
    {
        o->Scale = 0.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 59)
    {
        o->Scale = 0.2f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_HELPER + 54 && o->Type <= MODEL_HELPER + 58)
    {
        o->Scale = 0.7f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_POTION + 78 && o->Type <= MODEL_POTION + 82)
    {
        o->Scale = 0.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 60)
    {
        o->Scale = 1.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 61)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 83)
    {
        o->Scale = 0.3f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type >= MODEL_POTION + 145 && o->Type <= MODEL_POTION + 150)
    {
        o->Scale = 0.3f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type >= MODEL_HELPER + 125 && o->Type <= MODEL_HELPER + 127)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 91)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 92)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 93)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 95)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 94)
    {
        o->Scale = 0.6f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_CHERRY_BLOSSOM_PLAYBOX)
    {
        o->Scale = 0.8f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_CHERRY_BLOSSOM_WINE)
    {
        o->Scale = 0.9f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_CHERRY_BLOSSOM_RICE_CAKE)
    {
        o->Scale = 0.7f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_CHERRY_BLOSSOM_FLOWER_PETAL)
    {
        o->Scale = 1.3f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 88)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 180.f;
        o->Angle[1] = 180.f;
        //o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 89)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 30.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_GOLDEN_CHERRY_BLOSSOM_BRANCH)
    {
        o->Scale = 0.7f;
        o->Angle[0] = 30.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_HELPER + 62 && o->Type <= MODEL_HELPER + 63)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type >= MODEL_POTION + 97 && o->Type <= MODEL_POTION + 98)
    {
        o->Scale = 0.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 140)
    {
        o->Scale = 0.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 96)
    {
        o->Scale = 0.2f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type >= MODEL_DEMON && o->Type <= MODEL_SPIRIT_OF_GUARDIAN)
    {
        switch (o->Type)
        {
        case MODEL_DEMON:
            o->Scale = 0.21f;
            break;
        case MODEL_SPIRIT_OF_GUARDIAN:
            o->Scale = 0.5f;
            break;
        }
        o->Angle[2] = 70.f;
    }

    else if (o->Type == MODEL_OLD_SCROLL)
    {
        o->Angle[0] = 90.f;
        o->Angle[1] = 0.f;
        o->Scale = 0.3f;
    }
    else if (o->Type == MODEL_ILLUSION_SORCERER_COVENANT)
    {
        o->Angle[0] = 0.f;
        o->Scale = 0.6f;
    }
    else if (o->Type == MODEL_SCROLL_OF_BLOOD)
    {
        o->Angle[0] = 90.f;
        o->Scale = 0.45f;
    }
    else if (o->Type == MODEL_POTION + 64)
    {
        o->Angle[0] = 0.f;
        o->Scale = 0.8f;
    }
    else if (o->Type == MODEL_FLAME_OF_CONDOR)
    {
        o->Angle[0] = 0.f;
        o->Scale = 1.2f;
    }
    else if (o->Type == MODEL_FEATHER_OF_CONDOR)
    {
        o->Angle[0] = 0.f;
        o->Scale = 1.2f;
    }
    else if (o->Type == MODEL_FLAME_OF_DEATH_BEAM_KNIGHT)
    {
        o->Angle[0] = 90.f;
        o->Scale = 0.6f;
    }
    else if (o->Type == MODEL_HORN_OF_HELL_MAINE)
    {
        o->Angle[0] = 90.f;
        o->Scale = 0.8f;
    }
    else if (o->Type == MODEL_FEATHER_OF_DARK_PHOENIX)
    {
        o->Angle[0] = 270.f;
        o->Scale = 0.8f;
    }
    else if (o->Type == MODEL_EYE_OF_ABYSSAL)
    {
        o->Angle[2] = -135.f;
        o->Scale = 0.6f;
    }
    else if (o->Type == MODEL_EVENT + 4)
    {
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_EVENT + 8 || o->Type == MODEL_EVENT + 9)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = 45.f;
    }
    else if (o->Type == MODEL_EVENT + 10)
    {
        o->Scale = .2f;
    }
    else if (o->Type == MODEL_EVENT + 5)
    {
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_SCROLL_OF_EMPEROR_RING_OF_HONOR)
    {
        o->Angle[1] = 45.f;
        o->Angle[2] = 45.f;
    }
    else if (o->Type == MODEL_BROKEN_SWORD_DARK_STONE)
    {
        o->Angle[2] = 45.f;
    }
    else if ((o->Type >= MODEL_TEAR_OF_ELF && o->Type < MODEL_POTION + 27) ||
             o->Type == MODEL_LOCHS_FEATHER)
    {
        o->Angle[2] = 45.f;
    }
    else if (o->Type == MODEL_DEVILS_EYE)
    {
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_FIRECRACKER)
    {
        o->Angle[0] = 70.f;
        o->Scale = 1.5f;
    }
    else if (o->Type == MODEL_CHRISTMAS_FIRECRACKER)
    {
        o->Angle[0] = 70.f;
        o->Angle[2] = 0.f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_GM_GIFT)
    {
        o->Angle[2] = -10.f;
        o->Scale = 0.4f;
    }
    else if (o->Type == MODEL_DEVILS_KEY)
    {
        o->Angle[0] = o->Angle[2] = 270.f;
    }
    else if (o->Type == MODEL_DEVILS_INVITATION)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_SYMBOL_OF_KUNDUN)
    {
        o->Angle[0] = 90.f;
        o->Angle[2] = 70.f;
    }
    else if (o->Type == MODEL_EVENT + 11)
    {
        o->Angle[0] = 115.f;
        o->Angle[1] = 75.f;
        o->Angle[2] = 8.f;
        o->Scale = 0.4f;
    }
    else if (o->Type == MODEL_SCROLL_OF_ARCHANGEL || o->Type == MODEL_BLOOD_BONE)
    {
        o->Angle[0] = -45.f;
        o->Angle[1] = -5.f;
        o->Angle[2] = 18.f;
        o->Scale = 0.48f;
    }
    else if (o->Type == MODEL_INVISIBILITY_CLOAK)
    {
        o->Angle[0] = 165.f;
        o->Angle[1] = -168.f;
        o->Angle[2] = 198.f;
        o->Scale = 0.48f;
    }
    else if (o->Type == MODEL_CAPE_OF_LORD)
    {
        o->Angle[0] = -45.f;
        o->Angle[1] = 0.f;
        o->Angle[2] = 45.f;
        o->Scale = 0.5f;
    }
    else if (o->Type == MODEL_EVENT + 16)
    {
        o->Angle[2] = 45.f;
        o->Scale = 0.5f;
    }
    else if (o->Type == MODEL_EVENT + 12)
    {
        o->Angle[0] = 160.f;
        o->Angle[1] = -183.f;
        o->Angle[2] = 198.f;
        o->Scale = 0.38f;
    }
    else if (o->Type == MODEL_EVENT + 13)
    {
        o->Angle[0] = 160.f;
        o->Angle[1] = -183.f;
        o->Angle[2] = 198.f;
        o->Scale = 0.54f;
    }
    else if (o->Type == MODEL_POTION + 21)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_EVENT + 7)
    {
        o->Angle[2] = 45.f;
    }
    else if (o->Type == MODEL_POTION + 20)
    {
        o->Angle[2] = 45.f;
    }
    else if (o->Type >= MODEL_RING_OF_FIRE && o->Type <= MODEL_RING_OF_MAGIC)
    {
        o->Angle[2] = 20.f;
    }
    else if (o->Type == MODEL_BLESS_OF_GUARDIAN)
    {
        o->Angle[2] = 45.f;
        o->Scale = 1.2f;
    }
    else if (o->Type == MODEL_CLAW_OF_BEAST)
    {
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_FRAGMENT_OF_HORN)
    {
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_BROKEN_HORN)
    {
        o->Angle[2] = 90.f;
        o->Scale = 1.3f;
    }
    else if (o->Type == MODEL_HORN_OF_FENRIR)
    {
        o->Angle[2] = 180.f;
    }
    else if (o->Type == MODEL_JEWEL_OF_LIFE)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = 90.f - 45.f;
    }
    else if (o->Type == MODEL_JEWEL_OF_HARMONY)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = -15.f;
        o->Scale = 1.3f;
    }
    else if (o->Type == MODEL_LOWER_REFINE_STONE || o->Type == MODEL_HIGHER_REFINE_STONE)
    {
        o->Angle[0] = 270.f;
        o->Angle[2] = -15.f;
        o->Scale = 1.0f;
    }
    else if (o->Type >= MODEL_CHAIN_LIGHTNING_PARCHMENT && o->Type <= MODEL_INNOVATION_PARCHMENT)
    {
        o->Angle[0] = 270.f;
        o->Scale = 0.8f;
    }
    else if (o->Type == MODEL_HELPER + 66)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_POTION + 100)
    {
        o->Angle[0] = 180.0f;
        //	o->Angle[2] = 0.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type >= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_BEGIN &&
             o->Type <= static_cast<int>(MODEL_TYPE_CHARM_MIXWING) + EWS_END)
    {
        o->Scale = 0.5f;
        o->Angle[2] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 97 || o->Type == MODEL_HELPER + 98 ||
             o->Type == MODEL_POTION + 91)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 99)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_POTION + 110 || o->Type == MODEL_POTION + 111)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 107)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 104)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 105)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 103)
    {
        o->Angle[0] = 0.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_POTION + 133)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 109)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 110)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 111)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 112)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 113)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 114)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 115)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_POTION + 112)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_POTION + 113)
    {
        o->Angle[0] = 270.0f;
        o->Scale = 1.0f;
    }
    else if (o->Type == MODEL_HELPER + 116)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_HELPER + 121)
    {
        o->Scale = 0.5f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_PET_SKELETON)
    {
        o->Scale = 0.4f;
        o->Angle[0] = 30.f;
    }
    else if (o->Type >= MODEL_WING && o->Type < MODEL_WING + MAX_ITEM_INDEX)
    {
        {
            o->Angle[0] = 270.f;
            o->Angle[2] = 90.f - 45.f;
        }
    }
    else if (o->Type >= MODEL_HELPER + 135 && o->Type <= MODEL_HELPER + 145)
    {
        o->Scale = 0.2f;
        o->Angle[0] = 90.f;
    }
    else if (o->Type == MODEL_POTION + 160 || o->Type == MODEL_POTION + 161)
    {
        o->Scale = 0.2f;
        o->Angle[0] = 90.f;
    }
    else if (Check_LuckyItem(o->Type - MODEL_ITEM))
    {
        o->Angle[0] = 250.0f;
        o->Angle[1] = 180.0f;
        o->Angle[2] = 45.0f;
    }
}

void SessionGameplayUnit::CreateItemDrop(ITEM_t *ip, ItemCreationParams params, vec3_t position,
                                         bool isFreshDrop)
{
    sessionKeeper_.Gameplay()->RetireGroundItem(*ip);
    ++ip->Generation;
    int Type = params.Group * MAX_ITEM_INDEX + params.Number;
    ITEM *n = &ip->Item;
    n->Type = Type;
    n->Level = params.Level;
    n->HasSkill = params.WithSkill;
    n->HasLuck = params.WithLuck;
    n->OptionLevel = params.OptionLevel;
    n->OptionType = params.OptionType;
    n->Durability = params.Durability;
    n->ExcellentFlags = params.ExcellentFlags;
    n->AncientDiscriminator = params.AncientDiscriminator;
    n->AncientBonusOption = params.AncientBonusOption;
    n->option_380 = params.HasGuardianOption;

    if (isFreshDrop)
    {
        if (Type == ITEM_JEWEL_OF_BLESS || Type == ITEM_JEWEL_OF_SOUL ||
            Type == ITEM_JEWEL_OF_LIFE || Type == ITEM_JEWEL_OF_CHAOS ||
            Type == ITEM_JEWEL_OF_CREATION || Type == ITEM_JEWEL_OF_GUARDIAN)
            PlayBuffer(SOUND_JEWEL01, &ip->Object);
        else if (Type == ITEM_GEMSTONE)
            PlayBuffer(SOUND_JEWEL02, &ip->Object);
        else
            PlayBuffer(SOUND_DROP_ITEM01, &ip->Object);
    }

    OBJECT *o = &ip->Object;
    o->Live = true;
    o->Type = MODEL_ITEM + Type;
    o->SubType = 1;
    if (Type == (int)(ITEM_BOX_OF_LUCK))
    {
        switch (n->Level)
        {
        case 1:
            o->Type = MODEL_EVENT + 4;
            break;
        case 2:
            o->Type = MODEL_EVENT + 5;
            break;
        case 3:
            o->Type = MODEL_EVENT + 6;
            break;
        case 5:
            o->Type = MODEL_EVENT + 8;
            break;
        case 6:
            o->Type = MODEL_EVENT + 9;
            break;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            o->Type = MODEL_EVENT + 10;
            break;
        case 13:
            o->Type = MODEL_EVENT + 6;
            break;
        case 14:
        case 15:
            o->Type = MODEL_EVENT + 5;
        }
    }
    if (Type >= ITEM_JACK_OLANTERN_BLESSINGS && Type <= ITEM_JACK_OLANTERN_CRY)
    {
        o->Type = MODEL_ITEM + Type;
    }
    if (Type >= ITEM_PINK_CHOCOLATE_BOX && Type <= ITEM_BLUE_CHOCOLATE_BOX)
    {
        if (n->Level == 1)
        {
            int Num = Type - (ITEM_PINK_CHOCOLATE_BOX);
            o->Type = MODEL_EVENT + 21 + Num;
        }
    }
    else if (Type == (int)(ITEM_POTION + 21))
    {
        switch (n->Level)
        {
        case 1:
        case 2:
            o->Type = MODEL_EVENT + 11;
            break;
        }
    }
    else if (Type == (int)(ITEM_WEAPON_OF_ARCHANGEL))
    {
        switch (n->Level)
        {
        case 0:
            o->Type = MODEL_DIVINE_STAFF_OF_ARCHANGEL;
            n->Level = 0;
            break;
        case 1:
            o->Type = MODEL_DIVINE_SWORD_OF_ARCHANGEL;
            n->Level = 0;
            break;
        case 2:
            o->Type = MODEL_DIVINE_CB_OF_ARCHANGEL;
            n->Level = 0;
            break;
        }
    }
    else if (Type == ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR)
    {
        switch (n->Level)
        {
        case 1:
            o->Type = MODEL_EVENT + 12;
            break;
            break;
        }
    }
    else if (Type == ITEM_BROKEN_SWORD_DARK_STONE)
    {
        switch (n->Level)
        {
        case 1:
            o->Type = MODEL_EVENT + 13;
            break;
        }
    }
    else if (Type == ITEM_WIZARDS_RING)
    {
        switch (n->Level)
        {
        case 0:
            o->Type = MODEL_EVENT + 15;
            break;
        case 1:
        case 2:
        case 3:
            o->Type = MODEL_EVENT + 14;
            break;
        }
    }
    else if (Type == (int)(ITEM_ALE))
    {
        switch (n->Level)
        {
        case 1:
            o->Type = MODEL_EVENT + 7;
            break;
        }
    }
    else if (Type == ITEM_LOCHS_FEATHER && n->Level == 1)
    {
        o->Type = MODEL_EVENT + 16;
    }
    else if (Type == ITEM_LIFE_STONE_ITEM && n->Level == 1)
    {
        o->Type = MODEL_EVENT + 18;
    }
    else if (Type == ITEM_GEMSTONE)
    {
        o->Type = MODEL_GEMSTONE;
    }
    else if (Type == ITEM_JEWEL_OF_HARMONY)
    {
        o->Type = MODEL_JEWEL_OF_HARMONY;
    }
    else if (Type == ITEM_LOWER_REFINE_STONE)
    {
        o->Type = MODEL_LOWER_REFINE_STONE;
    }
    else if (Type == ITEM_HIGHER_REFINE_STONE)
    {
        o->Type = MODEL_HIGHER_REFINE_STONE;
    }
    ItemObjectAttribute(o);
    Vector(-30.f, -30.f, -30.f, o->BoundingBoxMin);
    Vector(30.f, 30.f, 30.f, o->BoundingBoxMax);
    VectorCopy(position, o->Position);
    if (isFreshDrop)
    {
        if (o->Type == MODEL_EVENT + 8 || o->Type == MODEL_EVENT + 9)
        {
            short scale = 55;
            vec3_t Angle;
            vec3_t light;
            Vector(1.f, 1.f, 1.f, light);
            Vector(0.f, 0.f, 0.f, Angle);
            vec3_t NewPosition;
            VectorCopy(position, NewPosition);
            NewPosition[2] = RequestTerrainHeight(position[0], position[1]) + 3;
            CreateEffect(MODEL_SKILL_FURY_STRIKE + 6, NewPosition, Angle, light, 0, o, scale);
            CreateEffect(MODEL_SKILL_FURY_STRIKE + 4, NewPosition, Angle, light, 0, o, scale);
            //CreateEffect(MODEL_SKILL_FURY_STRIKE+5,NewPosition,Angle,light,0,o,scale);

            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 3.f;
            o->Gravity = 50.f;
        }
        else
        {
            o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 180.f;
            o->Gravity = 20.f;
        }
    }
    else
    {
        o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]);
    }

    ItemAngle(o);
}

// Helper: Handle item falling animation
void SessionGameplayUnit::HandleItemFalling(OBJECT *o)
{
    if (o->Type >= MODEL_SHIELD && o->Type < MODEL_SHIELD + MAX_ITEM_INDEX)
        o->Angle[1] = -o->Gravity * 10.f;
    else
        o->Angle[0] = -o->Gravity * 10.f;
}

// Helper: Handle item on ground (set angle and camera rotation)
void SessionGameplayUnit::HandleItemOnGround(OBJECT *o)
{
    o->Position[2] = RequestTerrainHeight(o->Position[0], o->Position[1]) + 30.f;
    if (o->Type >= MODEL_SWORD && o->Type < MODEL_STAFF + MAX_ITEM_INDEX)
        o->Position[2] += 40.f;

    // Set default item angle
    ItemAngle(o);
}

void SessionGameplayUnit::MoveItems()
{
    for (int i = 0; i < MAX_ITEMS; i++)
    {
        OBJECT *o = &Items[i].Object;
        if (o->Live)
        {
            TheMapProcess().AdvanceObjectFade(*o);
            if (o->Type == MODEL_GRAND_SOUL_SHIELD || o->Type == MODEL_ELEMENTAL_SHIELD)
                sessionKeeper_.Visual()->UpdateItemObjectMaterial(*o);
            // Apply gravity physics
            constexpr float DropAcceleration = -6.f;
            Core::Time::Advance(o->Position[2], o->Gravity, DropAcceleration, FPS_ANIMATION_FACTOR);

            // Calculate ground height
            float groundHeight = RequestTerrainHeight(o->Position[0], o->Position[1]) + 30.f;
            if (o->Type >= MODEL_SWORD && o->Type < MODEL_STAFF + MAX_ITEM_INDEX)
                groundHeight += 40.f;

            // Check if item is on ground or still falling
            if (o->Position[2] <= groundHeight)
            {
                HandleItemOnGround(o);
            }
            else
            {
                HandleItemFalling(o);
            }

            // Create shiny particle effect
            if (rand_fps_check(1))
            {
                CreateShiny(o);
            }
            sessionKeeper_.Gameplay()->AdvanceDroppedItemVisual(*o, i);
        }
    }
}

namespace
{

ItemOptionDataDetail::FilePtr OpenBinaryFile(const wchar_t *filename)
{
    return ItemOptionDataDetail::FilePtr(_wfopen(filename, L"rb"), &std::fclose);
}

std::wstring ReportFileIssue(CErrorReport &errorReport, const wchar_t *filename,
                             const wchar_t *issue)
{
    wchar_t text[256]{};
    std::swprintf(text, std::size(text), L"%ls - %ls.", filename, issue);
    errorReport.Write(text);
    return text;
}
} // namespace

CSItemOption::CSItemOption(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), g_strSelectedML(keeper.AssetLanguage()),
      g_ErrorReport(keeper.ErrorReport())
{
    init();
}

void CSItemOption::BindCharacterState(CHARACTER_MACHINE &characterMachine) noexcept
{
    CharacterMachine = &characterMachine;
    CharacterAttribute = &characterMachine.Character;
}

std::wstring CSItemOption::OpenItemSetScript()
{
    std::wstring strFileName = L"";
    const std::wstring strTest = L"";

    strFileName = L"Data\\Local\\ItemSetType" + strTest + L".bmd";
    if (auto failure = OpenItemSetType(strFileName.c_str()); !failure.empty())
        return failure;

    strFileName = L"Data\\Local\\" + g_strSelectedML + L"\\ItemSetOption" + strTest + L"_" +
                  g_strSelectedML + L".bmd";
    return OpenItemSetOption(strFileName.c_str());
}

std::wstring CSItemOption::OpenItemSetType(const wchar_t *filename)
{
    auto file = OpenBinaryFile(filename);
    if (!file)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"File does not exist.");
    }

    const std::size_t entrySize = sizeof(ITEM_SET_TYPE);
    std::vector<std::uint8_t> buffer(entrySize * MAX_ITEM);
    if (std::fread(buffer.data(), buffer.size(), 1, file.get()) != 1)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"Failed to read content.");
    }

    std::uint32_t checksum{};
    if (std::fread(&checksum, sizeof(checksum), 1, file.get()) != 1)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"Failed to read checksum.");
    }

    if (checksum != GenerateCheckSum2(buffer.data(), static_cast<int>(buffer.size()), 0xE5F1))
    {
        return ReportFileIssue(g_ErrorReport, filename, L"File corrupted.");
    }

    auto *pSeek = buffer.data();
    for (int i = 0; i < MAX_ITEM; ++i)
    {
        BuxConvert(pSeek, static_cast<int>(entrySize));
        std::memcpy(&m_ItemSetType[i], pSeek, entrySize);
        pSeek += entrySize;
    }

    return {};
}

std::wstring CSItemOption::OpenItemSetOption(const wchar_t *filename)
{
    auto file = OpenBinaryFile(filename);
    if (!file)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"File does not exist.");
    }

    const std::size_t entrySize = sizeof(ItemOptionDataDetail::ITEM_SET_OPTION_FILE);
    std::vector<std::uint8_t> buffer(entrySize * MAX_SET_OPTION);
    if (std::fread(buffer.data(), buffer.size(), 1, file.get()) != 1)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"Failed to read content.");
    }

    std::uint32_t checksum{};
    if (std::fread(&checksum, sizeof(checksum), 1, file.get()) != 1)
    {
        return ReportFileIssue(g_ErrorReport, filename, L"Failed to read checksum.");
    }

    if (checksum != GenerateCheckSum2(buffer.data(), static_cast<int>(buffer.size()), 0xA2F1))
    {
        return ReportFileIssue(g_ErrorReport, filename, L"File corrupted.");
    }

    auto *pSeek = buffer.data();
    for (int i = 0; i < MAX_SET_OPTION; ++i)
    {
        BuxConvert(pSeek, static_cast<int>(entrySize));

        ItemOptionDataDetail::ITEM_SET_OPTION_FILE current{};
        std::memcpy(&current, pSeek, entrySize);
        auto *target = &m_ItemSetOption[i];

        CMultiLanguage::ConvertFromUtf8(target->strSetName, current.strSetName);
        target->byOptionCount = current.byOptionCount;
        target->bySetItemCount = 0; // Is calculated below
        target->byStandardOption = current.byStandardOption;
        target->byStandardOptionValue = current.byStandardOptionValue;
        target->byExtOption = current.byExtOption;
        target->byExtOptionValue = current.byExtOptionValue;
        target->byFullOption = current.byFullOption;
        target->byFullOptionValue = current.byFullOptionValue;
        target->byRequireClass = current.byRequireClass;

        pSeek += entrySize;
    }

    for (int j = 0; j < MAX_ITEM; ++j)
    {
        const auto &temptype = m_ItemSetType[j];
        for (int k = 0; k < MAX_ITEM_SETS_PER_ITEM; ++k)
        {
            const auto optionNumber = temptype.byOption[k];
            if (optionNumber < MAX_SET_OPTION)
            {
                m_ItemSetOption[optionNumber].bySetItemCount++;
            }
        }
    }

    return {};
}

std::uint8_t CSItemOption::IsChangeSetItem(const int Type, const int SubType)
{
    const ITEM_SET_TYPE &itemSType = m_ItemSetType[Type];

    if (SubType == -1)
    {
        return itemSType.byOption[0] == ItemOptionDataDetail::EMPTY_OPTION &&
                       itemSType.byOption[1] == ItemOptionDataDetail::EMPTY_OPTION
                   ? 0
                   : 255;
    }

    return itemSType.byOption[SubType] == ItemOptionDataDetail::EMPTY_OPTION
               ? 0
               : static_cast<std::uint8_t>(SubType + 1);
}

std::uint16_t CSItemOption::GetMixItemLevel(const int Type) const
{
    if (Type < 0)
        return 0;

    const ITEM_SET_TYPE &itemSType = m_ItemSetType[Type];

    return static_cast<std::uint16_t>(
        itemSType.byMixItemLevel[0] |
        (static_cast<std::uint16_t>(itemSType.byMixItemLevel[1]) << 8));
}

void CSItemOption::checkItemType(SET_SEARCH_RESULT *optionList, const int iType,
                                 const int ancientDiscriminator) const
{
    if (ancientDiscriminator <= 0)
    {
        return;
    }

    const auto setTypeIndex = static_cast<std::uint8_t>(ancientDiscriminator - 1);

    const ITEM_SET_TYPE &itemSetType = m_ItemSetType[iType];
    const auto itemSetNumber = itemSetType.byOption[setTypeIndex];

    if (itemSetNumber != 255 && itemSetNumber != 0)
    {
        // add set item to list
        for (int i = 0; i < MAX_EQUIPPED_SET_ITEMS; ++i)
        {
            const auto current = &optionList[i];
            if (current->SetNumber == 0)
            {
                // The set wasn't found in another item yet, so add it
                current->SetNumber = itemSetNumber;
                current->CompleteSetItemCount = m_ItemSetOption[itemSetNumber].bySetItemCount;
                current->ItemCount++;
                current->SetTypeIndex = setTypeIndex;
                wcscpy(current->SetName, m_ItemSetOption[itemSetNumber].strSetName);
                break;
            }

            if (current->SetNumber == itemSetNumber)
            {
                current->ItemCount++;
                current->SetTypeIndex = setTypeIndex;
                break;
            }
        }
    }
}

bool CSItemOption::isClassRequirementFulfilled(const ITEM_SET_OPTION &setOptions,
                                               const int firstClass, int secondClass)
{
    bool RequireClass = false;
    for (int i = CLASS_WIZARD; i <= CLASS_RAGEFIGHTER; i++)
    {
        if (setOptions.byRequireClass[i] == 1 && firstClass == i)
        {
            RequireClass = true;
        }

        if (setOptions.byRequireClass[i] == 2 && firstClass == i && secondClass)
        {
            RequireClass = true;
        }
    }

    return RequireClass;
}

void CSItemOption::TryAddSetOption(std::uint8_t option, int value, int optionIndex,
                                   SET_SEARCH_RESULT_OPT &set, const ITEM_SET_OPTION &setOptions,
                                   bool isThisSetComplete, bool isFullOption, bool isExtOption,
                                   bool fulfillsClassRequirement, int firstClass, int secondClass)
{
    if (option == ItemOptionDataDetail::EMPTY_OPTION || value == 0 ||
        (option >= MASTERY_OPTION && (setOptions.byRequireClass[firstClass] &&
                                      secondClass >= setOptions.byRequireClass[firstClass] - 1)))
    {
        return;
    }

    const auto setOption = &set.SetOption[set.SetOptionCount];
    setOption->OptionNumber = option;
    setOption->FulfillsClassRequirement = fulfillsClassRequirement;
    setOption->Value = value;
    setOption->IsActive = isThisSetComplete || optionIndex < set.ItemCount - 1;
    setOption->IsFullOption = isFullOption;
    setOption->IsExtOption = isExtOption;

    set.SetOptionCount++;
}

void CSItemOption::calcSetOptionList(const SET_SEARCH_RESULT *optionList)
{
    const int firstClass = gCharacterManager.GetBaseClass(Hero->Class);
    const int secondClass = gCharacterManager.IsSecondClass(Hero->Class);

    int setCount = 0;
    for (int i = 0; i < MAX_EQUIPPED_SET_ITEMS; ++i)
    {
        if (optionList[i].SetNumber == 0)
        {
            break;
        }

        if (optionList[i].ItemCount >= 2)
        {
            memcpy(&m_SetSearchResult[setCount], &optionList[i], sizeof(SET_SEARCH_RESULT));
            setCount++;
        }
    }

    m_SetSearchResultCount = setCount;

    // now we have all our equipped sets together and can continue to
    // build up the corresponding option list

    m_bySameSetItem = 0;

    Hero->ExtendState = 0;
    for (int i = 0; i < m_SetSearchResultCount; ++i)
    {
        auto &set = m_SetSearchResult[i];

        const ITEM_SET_OPTION &setOptions = m_ItemSetOption[set.SetNumber];
        bool isThisSetComplete = false;
        if (set.CompleteSetItemCount <= set.ItemCount)
        {
            Hero->ExtendState = 1;
            isThisSetComplete = true;
        }

        const auto requireClass = isClassRequirementFulfilled(setOptions, firstClass, secondClass);
        const auto standardOptionCount =
            std::min<int>(set.CompleteSetItemCount - 1, MAX_ITEM_SET_STANDARD_OPTION_COUNT);
        for (int o = 0; o < standardOptionCount; ++o)
        {
            for (int n = 0; n < MAX_ITEM_SET_STANDARD_OPTION_PER_ITEM_COUNT; ++n)
            {
                TryAddSetOption(setOptions.byStandardOption[o][n],
                                setOptions.byStandardOptionValue[o][n], o, set, setOptions,
                                isThisSetComplete, false, false, requireClass, firstClass,
                                secondClass);
            }
        }

        for (int o = 0; o < MAX_ITEM_SET_EXT_OPTION_COUNT; ++o)
        {
            TryAddSetOption(setOptions.byExtOption[o], setOptions.byExtOptionValue[o], 0, set,
                            setOptions, isThisSetComplete, false, true, requireClass, firstClass,
                            secondClass);
        }

        for (int o = 0; o < MAX_ITEM_SET_FULL_OPTION_COUNT; ++o)
        {
            TryAddSetOption(setOptions.byFullOption[o], setOptions.byFullOptionValue[o], 255, set,
                            setOptions, isThisSetComplete, true, false, requireClass, firstClass,
                            secondClass);
        }
    }
}

bool CSItemOption::getExplainText(wchar_t *text, std::uint8_t option, int value)
{
    if (option == ItemOptionDataDetail::EMPTY_OPTION)
    {
        return false;
    }

    switch (option + AT_SET_OPTION_IMPROVE_STRENGTH)
    {
    case AT_SET_OPTION_IMPROVE_MAGIC_POWER:
        mu_swprintf(text, I18N::Game::IncreaseWizardryDmgD, value);
        return true;

    case AT_SET_OPTION_IMPROVE_STRENGTH:
    case AT_SET_OPTION_IMPROVE_DEXTERITY:
    case AT_SET_OPTION_IMPROVE_ENERGY:
    case AT_SET_OPTION_IMPROVE_VITALITY:
    case AT_SET_OPTION_IMPROVE_CHARISMA:
    case AT_SET_OPTION_IMPROVE_ATTACK_MIN:
    case AT_SET_OPTION_IMPROVE_ATTACK_MAX:
        mu_swprintf(text, I18N::Game::Lookup(950 + option), value);
        return true;

    case AT_SET_OPTION_IMPROVE_DAMAGE:
    case AT_SET_OPTION_IMPROVE_ATTACKING_PERCENT:
    case AT_SET_OPTION_IMPROVE_DEFENCE:
    case AT_SET_OPTION_IMPROVE_MAX_LIFE:
    case AT_SET_OPTION_IMPROVE_MAX_MANA:
    case AT_SET_OPTION_IMPROVE_MAX_AG:
    case AT_SET_OPTION_IMPROVE_ADD_AG:
    case AT_SET_OPTION_IMPROVE_CRITICAL_DAMAGE_PERCENT:
    case AT_SET_OPTION_IMPROVE_CRITICAL_DAMAGE:
    case AT_SET_OPTION_IMPROVE_EXCELLENT_DAMAGE_PERCENT:
    case AT_SET_OPTION_IMPROVE_EXCELLENT_DAMAGE:
    case AT_SET_OPTION_IMPROVE_SKILL_ATTACK:
    case AT_SET_OPTION_DOUBLE_DAMAGE:
        mu_swprintf(text, I18N::Game::Lookup(949 + option), value);
        return true;

    case AT_SET_OPTION_DISABLE_DEFENCE:
        mu_swprintf(text, I18N::Game::IgnoreEnemiesDefensiveSkillD, value);
        return true;

    case AT_SET_OPTION_TWO_HAND_SWORD_IMPROVE_DAMAGE:
        mu_swprintf(text, I18N::Game::IncreaseDamageWhenUsingTwoHandedWeaponsD, value);
        return true;

    case AT_SET_OPTION_IMPROVE_SHIELD_DEFENCE:
        mu_swprintf(text, I18N::Game::IncreaseDefensiveSkillWhenUsingShieldWeaponsD, value);
        return true;

    case AT_SET_OPTION_IMPROVE_ATTACK_1:
    case AT_SET_OPTION_IMPROVE_ATTACK_2:
    case AT_SET_OPTION_IMPROVE_MAGIC:
        //	case AT_SET_OPTION_IMPROVE_DEFENCE_1:
        //	case AT_SET_OPTION_IMPROVE_DEFENCE_2:
    case AT_SET_OPTION_IMPROVE_DEFENCE_3:
    case AT_SET_OPTION_IMPROVE_DEFENCE_4:
    case AT_SET_OPTION_FIRE_MASTERY:
    case AT_SET_OPTION_ICE_MASTERY:
    case AT_SET_OPTION_THUNDER_MASTERY:
    case AT_SET_OPTION_POSION_MASTERY:
    case AT_SET_OPTION_WATER_MASTERY:
    case AT_SET_OPTION_WIND_MASTERY:
    case AT_SET_OPTION_EARTH_MASTERY:
        mu_swprintf(text,
                    I18N::Game::Lookup(971 + (option + AT_SET_OPTION_IMPROVE_STRENGTH -
                                              AT_SET_OPTION_IMPROVE_ATTACK_2)),
                    value);
        return true;
    default:
        return false;
    }
}

int CSItemOption::AggregateOptionValue(int optionNumber) const
{
    int result = 0;
    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        const auto &set = m_SetSearchResult[i];
        for (int j = 0; j < set.SetOptionCount; j++)
        {
            const auto &option = set.SetOption[j];

            if (option.IsActive && option.OptionNumber == optionNumber && option.Value != 0)
            {
                result += option.Value;
            }
        }
    }

    return result;
}

void CSItemOption::PlusSpecial(std::uint16_t *Value, int Special) const
{
    Special -= AT_SET_OPTION_IMPROVE_STRENGTH;

    if (const int optionValue = AggregateOptionValue(Special))
    {
        *Value += optionValue;
    }
}

void CSItemOption::PlusSpecialPercent(std::uint16_t *Value, int Special) const
{
    Special -= AT_SET_OPTION_IMPROVE_STRENGTH;

    if (const int optionValue = AggregateOptionValue(Special))
    {
        *Value += ((*Value) * optionValue) / 100;
    }
}

void CSItemOption::PlusSpecialLevel(std::uint16_t *Value, std::uint16_t SrcValue, int Special) const
{
    Special -= AT_SET_OPTION_IMPROVE_STRENGTH;
    int optionValue = 0;
    int count = 0;

    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        const auto &set = m_SetSearchResult[i];
        for (int j = 0; j < set.SetOptionCount; j++)
        {
            const auto &option = set.SetOption[j];
            if (option.IsActive && option.OptionNumber == Special && option.Value != 0)
            {
                optionValue += option.Value;
                count++;
            }
        }
    }

    if (optionValue)
    {
        optionValue = SrcValue * optionValue / 100;
        *Value += (optionValue * count);
    }
}

void CSItemOption::PlusMastery(int *Value, std::uint8_t MasteryType) const
{
    int optionValue = 0;

    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        const auto &set = m_SetSearchResult[i];
        for (int j = 0; j < set.SetOptionCount; j++)
        {
            const auto &option = set.SetOption[j];
            if (option.IsActive && option.OptionNumber >= MASTERY_OPTION &&
                (option.OptionNumber - MASTERY_OPTION - 5) == MasteryType && option.Value != 0)
            {
                optionValue += option.Value;
            }
        }
    }

    if (optionValue)
    {
        *Value += optionValue;
    }
}

int CSItemOption::GetDefaultOptionValue(ITEM *ip, std::uint16_t *Value) const
{
    if (ip->Type > MAX_ITEM)
    {
        return -1;
    }

    *Value = ip->AncientBonusOption;

    const ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];

    return p->AttType;
}

bool CSItemOption::IsNonWeaponSkillOrIsSkillEquipped(ActionSkillType skill) const
{
    bool checkForWeaponSkill;
    auto baseSkill = CSkillManager::MasterSkillToBaseSkillIndex(skill);
    switch (baseSkill)
    {
    case AT_SKILL_POWER_SLASH:
    case AT_SKILL_TRIPLE_SHOT:
    case AT_SKILL_FALLING_SLASH:
    case AT_SKILL_LUNGE:
    case AT_SKILL_UPPERCUT:
    case AT_SKILL_CYCLONE:
    case AT_SKILL_SLASH:
        checkForWeaponSkill = true;
        break;
    default:
        checkForWeaponSkill = false;
        break;
    }

    if (!checkForWeaponSkill)
    {
        return true;
    }

    for (int i = 0; i < 2; i++)
    {
        const ITEM *item = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT + i];
        if (item == nullptr || item->Type <= -1)
        {
            continue;
        }

        for (int j = 0; j < item->SpecialNum; j++)
        {
            if (item->Special[j] == baseSkill)
            {
                return true;
            }
        }
    }

    return false;
}

void CSItemOption::getAllAddState(std::uint16_t *Strength, std::uint16_t *Dexterity,
                                  std::uint16_t *Energy, std::uint16_t *Vitality,
                                  std::uint16_t *Charisma) const
{
    for (int i = EQUIPMENT_WEAPON_RIGHT; i < MAX_EQUIPMENT; ++i)
    {
        ITEM *item = &CharacterMachine->Equipment[i];

        if (item->Durability <= 0)
        {
            continue;
        }

        std::uint16_t Result = 0;
        switch (GetDefaultOptionValue(item, &Result))
        {
        case SET_OPTION_STRENGTH:
            *Strength += Result * 5;
            break;

        case SET_OPTION_DEXTERITY:
            *Dexterity += Result * 5;
            break;

        case SET_OPTION_ENERGY:
            *Energy += Result * 5;
            break;

        case SET_OPTION_VITALITY:
            *Vitality += Result * 5;
            break;
        }
    }

    AddStatsBySetOptions(Strength, Dexterity, Energy, Vitality, Charisma);
}

void CSItemOption::AddStatsBySetOptions(std::uint16_t *Strength, std::uint16_t *Dexterity,
                                        std::uint16_t *Energy, std::uint16_t *Vitality,
                                        std::uint16_t *Charisma) const
{
    for (int i = 0; i < m_SetSearchResultCount; i++)
    {
        auto &set = m_SetSearchResult[i];
        for (int j = 0; j < set.SetOptionCount; j++)
        {
            auto &option = set.SetOption[j];
            if (!option.IsActive)
            {
                continue;
            }

            switch (option.OptionNumber)
            {
            case AT_SET_OPTION_IMPROVE_STRENGTH:
                *Strength += option.Value;
                break;

            case AT_SET_OPTION_IMPROVE_DEXTERITY:
                *Dexterity += option.Value;
                break;

            case AT_SET_OPTION_IMPROVE_ENERGY:
                *Energy += option.Value;
                break;

            case AT_SET_OPTION_IMPROVE_VITALITY:
                *Vitality += option.Value;
                break;

            case AT_SET_OPTION_IMPROVE_CHARISMA:
                *Charisma += option.Value;
                break;
            default:
                // other options are not handled here.
                break;
            }
        }
    }
}

void CSItemOption::getAllAddStateOnlyAddValue(std::uint16_t *AddStrength,
                                              std::uint16_t *AddDexterity, std::uint16_t *AddEnergy,
                                              std::uint16_t *AddVitality,
                                              std::uint16_t *AddCharisma) const
{
    *AddStrength = *AddDexterity = *AddEnergy = *AddVitality = *AddCharisma = 0;
    getAllAddState(AddStrength, AddDexterity, AddEnergy, AddVitality, AddCharisma);
}

void CSItemOption::getAllAddOptionStatesbyCompare(
    std::uint16_t *Strength, std::uint16_t *Dexterity, std::uint16_t *Energy,
    std::uint16_t *Vitality, std::uint16_t *Charisma, std::uint16_t iCompareStrength,
    std::uint16_t iCompareDexterity, std::uint16_t iCompareEnergy, std::uint16_t iCompareVitality,
    std::uint16_t iCompareCharisma)
{
    for (int i = EQUIPMENT_WEAPON_RIGHT; i < MAX_EQUIPMENT; ++i)
    {
        ITEM *item = &CharacterMachine->Equipment[i];

        if (item->RequireStrength > iCompareStrength ||
            item->RequireDexterity > iCompareDexterity || item->RequireEnergy > iCompareEnergy)
        {
            continue;
        }

        if (item->Durability <= 0)
        {
            continue;
        }

        std::uint16_t Result = 0;
        switch (GetDefaultOptionValue(item, &Result))
        {
        case SET_OPTION_STRENGTH:
            *Strength += Result * 5;
            break;

        case SET_OPTION_DEXTERITY:
            *Dexterity += Result * 5;
            break;

        case SET_OPTION_ENERGY:
            *Energy += Result * 5;
            break;

        case SET_OPTION_VITALITY:
            *Vitality += Result * 5;
            break;
        }
    }

    AddStatsBySetOptions(Strength, Dexterity, Energy, Vitality, Charisma);
}

void CSItemOption::CheckItemSetOptions()
{
    SET_SEARCH_RESULT byOptionList[MAX_EQUIPPED_SET_ITEMS] = {};

    const ITEM *itemRight = nullptr;

    std::fill(std::begin(m_SetSearchResult), std::end(m_SetSearchResult), SET_SEARCH_RESULT_OPT{});

    for (int i = 0; i < MAX_EQUIPMENT_INDEX; ++i)
    {
        if (i == EQUIPMENT_WING || i == EQUIPMENT_HELPER)
        {
            continue;
        }

        const ITEM *item = &CharacterMachine->Equipment[i];

        if (item->Durability <= 0)
        {
            continue;
        }

        if ((i == EQUIPMENT_WEAPON_LEFT || i == EQUIPMENT_RING_LEFT) && itemRight != nullptr &&
            itemRight->Type == item->Type &&
            (itemRight->AncientDiscriminator == item->AncientDiscriminator))
        {
            // same item of a set should only count once
            continue;
        }

        if (item->Type > -1)
        {
            checkItemType(byOptionList, item->Type, item->AncientDiscriminator);
        }

        if (i == EQUIPMENT_WEAPON_RIGHT || i == EQUIPMENT_RING_RIGHT)
        {
            itemRight = item;
        }
    }

    calcSetOptionList(byOptionList);
    getAllAddStateOnlyAddValue(&CharacterAttribute->AddStrength, &CharacterAttribute->AddDexterity,
                               &CharacterAttribute->AddEnergy, &CharacterAttribute->AddVitality,
                               &CharacterAttribute->AddCharisma);

    const auto AllStrength =
        static_cast<std::uint16_t>(CharacterAttribute->Strength + CharacterAttribute->AddStrength);
    const auto AllDexterity = static_cast<std::uint16_t>(CharacterAttribute->Dexterity +
                                                         CharacterAttribute->AddDexterity);
    const auto AllEnergy =
        static_cast<std::uint16_t>(CharacterAttribute->Energy + CharacterAttribute->AddEnergy);
    auto AllVitality =
        static_cast<std::uint16_t>(CharacterAttribute->Vitality + CharacterAttribute->AddVitality);
    const auto AllCharisma =
        static_cast<std::uint16_t>(CharacterAttribute->Charisma + CharacterAttribute->AddCharisma);
    const auto AllLevel = static_cast<std::uint16_t>(CharacterAttribute->Level);

    // And now we're doing all that again, just for checking the required stats?!
    // TODO: How can this be improved?

    std::fill(std::begin(m_SetSearchResult), std::end(m_SetSearchResult), SET_SEARCH_RESULT_OPT{});
    std::fill(std::begin(byOptionList), std::end(byOptionList), SET_SEARCH_RESULT{});

    for (int i = 0; i < MAX_EQUIPMENT_INDEX; ++i)
    {
        if (i == EQUIPMENT_WING || i == EQUIPMENT_HELPER)
        {
            continue;
        }

        ITEM *ip = &CharacterMachine->Equipment[i];

        if (ip->RequireDexterity > AllDexterity || ip->RequireEnergy > AllEnergy ||
            ip->RequireStrength > AllStrength || ip->RequireLevel > AllLevel ||
            ip->RequireCharisma > AllCharisma || ip->Durability <= 0 ||
            (IsRequireEquipItem(ip) == false))
        {
            continue;
        }

        if ((i == EQUIPMENT_WEAPON_LEFT || i == EQUIPMENT_RING_LEFT) && itemRight != nullptr &&
            itemRight->Type == ip->Type &&
            itemRight->AncientDiscriminator == ip->AncientDiscriminator)
        {
            continue;
        }

        if (ip->Type > -1)
        {
            checkItemType(byOptionList, ip->Type, ip->AncientDiscriminator);
        }

        if (i == EQUIPMENT_WEAPON_RIGHT || i == EQUIPMENT_RING_RIGHT)
        {
            itemRight = ip;
        }
    }

    calcSetOptionList(byOptionList);
}

CHARACTER_MACHINE::CHARACTER_MACHINE(SessionKeeper &keeper) noexcept
    : SessionLegacyCalls(keeper), g_SocketItemMgr(keeper.SocketItemManager()),
      g_csItemOption(keeper.ItemOptionManager())
{
}

BOOL IsValidateSkillIdx(INT iSkillIdx)
{
    if (iSkillIdx >= MAX_SKILLS || iSkillIdx < 0)
    {
        return FALSE;
    }

    return TRUE;
}

BOOL SessionGameDataUnit::IsCorrectSkillType(INT iSkillSeq, eTypeSkill iSkillTypeIdx)
{
    if (IsValidateSkillIdx(iSkillSeq) == FALSE)
    {
        return FALSE;
    }

    SKILL_ATTRIBUTE &CurSkillAttribute = SkillAttribute[iSkillSeq];

    if (CurSkillAttribute.TypeSkill == (BYTE)iSkillTypeIdx)
    {
        return TRUE;
    }

    return FALSE;
}

BOOL SessionGameDataUnit::IsCorrectSkillType_FrendlySkill(INT iSkillSeq)
{
    return IsCorrectSkillType(iSkillSeq, eTypeSkill_FrendlySkill);
}

BOOL SessionGameDataUnit::IsCorrectSkillType_Buff(INT iSkillSeq)
{
    return IsCorrectSkillType(iSkillSeq, eTypeSkill_Buff);
}

BOOL SessionGameDataUnit::IsCorrectSkillType_DeBuff(INT iSkillSeq)
{
    return IsCorrectSkillType(iSkillSeq, eTypeSkill_DeBuff);
}

BOOL SessionGameDataUnit::IsCorrectSkillType_CommonAttack(INT iSkillSeq)
{
    return IsCorrectSkillType(iSkillSeq, eTypeSkill_CommonAttack);
}

// dialog

// item

ActionSkillType GetSkillByBook(int Type)
{
    switch (Type)
    {
    case ITEM_SCROLL_OF_POISON:
        return AT_SKILL_POISON;
    case ITEM_SCROLL_OF_METEORITE:
        return AT_SKILL_METEO;
    case ITEM_SCROLL_OF_LIGHTING:
        return AT_SKILL_LIGHTNING;
    case ITEM_SCROLL_OF_FIRE_BALL:
        return AT_SKILL_FIREBALL;
    case ITEM_SCROLL_OF_FLAME:
        return AT_SKILL_FLAME;
    case ITEM_SCROLL_OF_TELEPORT:
        return AT_SKILL_TELEPORT;
    case ITEM_SCROLL_OF_ICE:
        return AT_SKILL_ICE;
    case ITEM_SCROLL_OF_TWISTER:
        return AT_SKILL_STORM;
    case ITEM_SCROLL_OF_EVIL_SPIRIT:
        return AT_SKILL_EVIL_SPIRIT;
    case ITEM_SCROLL_OF_HELLFIRE:
        return AT_SKILL_HELL_FIRE;
    case ITEM_SCROLL_OF_POWER_WAVE:
        return AT_SKILL_POWERWAVE;
    case ITEM_SCROLL_OF_AQUA_BEAM:
        return AT_SKILL_FLASH;
    case ITEM_SCROLL_OF_COMETFALL:
        return AT_SKILL_BLAST;
    case ITEM_SCROLL_OF_INFERNO:
        return AT_SKILL_INFERNO;
    case ITEM_SCROLL_OF_TELEPORT_ALLY:
        return AT_SKILL_TELEPORT_ALLY;
    case ITEM_SCROLL_OF_SOUL_BARRIER:
        return AT_SKILL_SOUL_BARRIER;
    case ITEM_SCROLL_OF_DECAY:
        return AT_SKILL_DECAY;
    case ITEM_SCROLL_OF_ICE_STORM:
        return AT_SKILL_ICE_STORM;
    case ITEM_SCROLL_OF_NOVA:
        return AT_SKILL_NOVA;
    case ITEM_CHAIN_LIGHTNING_PARCHMENT:
        return AT_SKILL_ALICE_CHAINLIGHTNING;
    case ITEM_DRAIN_LIFE_PARCHMENT:
        return AT_SKILL_ALICE_DRAINLIFE;
    case ITEM_LIGHTNING_SHOCK_PARCHMENT:
        return AT_SKILL_LIGHTNING_SHOCK;
    case ITEM_BERSERKER_PARCHMENT:
        return AT_SKILL_ALICE_BERSERKER;
    case ITEM_SLEEP_PARCHMENT:
        return AT_SKILL_ALICE_SLEEP;
    case ITEM_WEAKNESS_PARCHMENT:
        return AT_SKILL_ALICE_WEAKNESS;
    case ITEM_SCROLL_OF_WIZARDRY_ENHANCE:
        return AT_SKILL_EXPANSION_OF_WIZARDRY;
    case ITEM_SCROLL_OF_GIGANTIC_STORM:
        return AT_SKILL_GIGANTIC_STORM;
    case ITEM_CHAIN_DRIVE_PARCHMENT:
        return AT_SKILL_CHAIN_DRIVE;
    case ITEM_DARK_SIDE_PARCHMENT:
        return AT_SKILL_DARKSIDE;
    case ITEM_DRAGON_ROAR_PARCHMENT:
        return AT_SKILL_DRAGON_ROAR;
    default:
        return AT_SKILL_UNDEFINED;
    }
}

bool IsCepterItem(int iType)
{
    if ((iType >= ITEM_BATTLE_SCEPTER && iType <= ITEM_SHINING_SCEPTER) ||
        iType == ITEM_ABSOLUTE_SCEPTER || iType == ITEM_STRYKER_SCEPTER)
    {
        return true;
    }
    return false;
}

void ItemConvert(ITEM *ip, BYTE Attribute1, BYTE Attribute2, BYTE ancientDiscriminator)
{
    // todo...
}

bool IsWing(ITEM *ip)
{
    return (ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
           (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
           (ip->Type >= ITEM_WINGS_OF_DESPAIR && ip->Type <= ITEM_WING_OF_DIMENSION) ||
           ip->Type == ITEM_DIVINE_SWORD_OF_ARCHANGEL || ip->Type == ITEM_DIVINE_CB_OF_ARCHANGEL ||
           ip->Type == ITEM_DIVINE_STAFF_OF_ARCHANGEL ||
           ip->Type == ITEM_DIVINE_STICK_OF_ARCHANGEL ||
           ip->Type == ITEM_DIVINE_SCEPTER_OF_ARCHANGEL || ip->Type == ITEM_CAPE_OF_LORD ||
           (ITEM_WING + 130 <= ip->Type && ip->Type <= ITEM_WING + 134) ||
           (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE) ||
           (ip->Type == ITEM_WING + 135);
}

int GetDropLevel(ITEM_ATTRIBUTE *p)
{
    return p->Level + 30;
}

int SessionGameDataUnit::GetExcellentAddValue(ITEM *ip) const
{
    if (ip->Type == ITEM_CHAOS_DRAGON_AXE)
    {
        return 15;
    }

    if (ip->Type == ITEM_CHAOS_NATURE_BOW)
    {
        return 30;
    }

    if (ip->Type == ITEM_CHAOS_LIGHTNING_STAFF)
    {
        return 25;
    }

    return 0;
}

void SessionGameDataUnit::CalcDamageMin(ITEM *ip, ITEM_ATTRIBUTE *p, int excelAddValue) const
{
    //ITEM_ATTRIBUTE* p = &ItemAttribute[ip->Type];
    if (p->DamageMin <= 0)
    {
        return;
    }

    if (IsWing(ip))
    {
    }
    if (ip->ExcellentFlags > 0) // TODO: For wings, too?
    {
        if (p->Level)
        {
            if (excelAddValue)
                ip->DamageMin += excelAddValue;
            else
                ip->DamageMin += p->DamageMin * 25 / p->Level + 5;
        }
    }

    if (ip->AncientDiscriminator > 0)
    {
        ip->DamageMin += 5 + (GetDropLevel(p) / 40);
    }

    ip->DamageMin += (std::min<int>(9, ip->Level) * 3);

    if (ip->Level - 9 > 0)
    {
        ip->DamageMin += ip->Level - 6;
    }
}

void SessionGameDataUnit::CalcDamageMax(ITEM *ip, ITEM_ATTRIBUTE *p, int excelAddValue) const
{
    if (p->DamageMax <= 0)
    {
        return;
    }

    if (ip->ExcellentFlags > 0)
    {
        if (p->Level)
        {
            if (excelAddValue)
                ip->DamageMax += excelAddValue;
            else
                ip->DamageMax += p->DamageMax * 25 / p->Level + 5;
        }
    }

    if (ip->AncientDiscriminator > 0)
    {
        ip->DamageMax += 5 + (GetDropLevel(p) / 40);
    }

    ip->DamageMax += (std::min<int>(9, ip->Level) * 3);

    if (ip->Level - 9 > 0)
    {
        ip->DamageMax += ip->Level - 6;
    }
}

void SessionGameDataUnit::CalcMagicPower(ITEM *ip, ITEM_ATTRIBUTE *p, int excelAddValue) const
{
    if (p->MagicPower <= 0)
    {
        return;
    }

    if (ip->ExcellentFlags > 0)
    {
        if (p->Level)
        {
            if (excelAddValue)
                ip->MagicPower += excelAddValue;
            else
                ip->MagicPower += p->MagicPower * 25 / p->Level + 5;
        }
    }

    if (ip->AncientDiscriminator > 0)
    {
        ip->MagicPower += 2 + (GetDropLevel(p) / 60);
    }

    ip->MagicPower += (std::min<int>(9, ip->Level) * 3);

    if (ip->Level - 9 > 0)
    {
        ip->MagicPower += ip->Level - 6;
    }

    ip->MagicPower /= 2;

    if (!IsCepterItem(ip->Type))
    {
        ip->MagicPower += ip->Level * 2;
    }
}

void SessionGameDataUnit::CalcSuccessfulBlocking(ITEM *ip, ITEM_ATTRIBUTE *p) const
{
    if (p->SuccessfulBlocking == 0)
    {
        return;
    }

    if (ip->ExcellentFlags > 0)
    {
        if (p->Level)
            ip->SuccessfulBlocking += p->SuccessfulBlocking * 25 / p->Level + 5;
    }
    ip->SuccessfulBlocking += (std::min<int>(9, p->Level) * 3); // ~ +9
    if (p->Level - 9 > 0)
    {
        ip->SuccessfulBlocking += p->Level - 6;
    }
}

void SessionGameDataUnit::CalcDefense(ITEM *ip, ITEM_ATTRIBUTE *p) const
{
    if (p->MagicDefense > 0)
    {
        ip->MagicDefense += (std::min<int>(9, p->Level) * 3); // ~ +9
        if (p->Level - 9 > 0)
        {
            ip->MagicDefense += p->Level - 6;
        }
    }

    if (ip->Type == ITEM_CAPE_OF_LORD)
    {
        p->Defense = 15;
        ip->Defense = 15;
    }

    if (p->Defense == 0)
    {
        return;
    }

    auto isAncientItem = ip->AncientDiscriminator > 0;
    auto isExcellent = ip->ExcellentFlags > 0;
    auto setItemDropLevel = GetDropLevel(p);

    if (ip->Type >= ITEM_SHIELD && ip->Type < ITEM_SHIELD + MAX_ITEM_INDEX)
    {
        ip->Defense += ip->Level;
        if (isAncientItem)
        {
            ip->Defense = ip->Defense + (ip->Defense * 20 / setItemDropLevel + 2);
        }

        return;
    }

    if (isExcellent && p->Level > 0)
    {
        ip->Defense += p->Defense * 12 / p->Level + 4 + p->Level / 5;
    }

    if (isAncientItem)
    {
        ip->Defense +=
            p->Defense + (ip->Defense * 3 / setItemDropLevel + 2 + setItemDropLevel / 30);
    }

    if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        ip->Type == ITEM_WINGS_OF_DESPAIR)
    {
        ip->Defense += (std::min<int>(9, ip->Level) * 2); // ~ +9
    }
    else if (ip->Type == ITEM_CAPE_OF_LORD || ip->Type == ITEM_CAPE_OF_FIGHTER)
    {
        ip->Defense += (std::min<int>(9, ip->Level) * 2); // ~ +9
    }
    else if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
             ip->Type == ITEM_WING_OF_DIMENSION || (ip->Type == ITEM_CAPE_OF_OVERRULE))
    {
        ip->Defense += (std::min<int>(9, ip->Level) * 4); // ~ +9
    }
    else
    {
        ip->Defense += (std::min<int>(9, ip->Level) * 3); // ~ +9
    }
    if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
        ip->Type == ITEM_WING_OF_DIMENSION || ip->Type == ITEM_CAPE_OF_OVERRULE)
    {
        if (ip->Level - 9 > 0)
        {
            ip->Defense += ip->Level - 5;
        }
    }
    else
    {
        if (ip->Level - 9 > 0)
        {
            ip->Defense += ip->Level - 6;
        }
    }
}

void SessionGameDataUnit::CalcRequirements(ITEM *ip, ITEM_ATTRIBUTE *p) const
{
    bool isExcellent = ip->ExcellentFlags > 0;
    bool isAncientItem = ip->AncientDiscriminator > 0;
    int ItemLevel = p->Level;

    if (isExcellent)
    {
        ItemLevel = p->Level + 25;
    }
    else if (isAncientItem)
    {
        ItemLevel = p->Level + 30;
    }

    int addValue = 4;

    if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        ip->Type == ITEM_WINGS_OF_DESPAIR)
    {
        addValue = 5;
    }

    if (p->RequireLevel &&
        ((ip->Type >= ITEM_SWORD && ip->Type < ITEM_WING) || (ip->Type == ITEM_HORN_OF_FENRIR) ||
         (ip->Type >= ITEM_ORB_OF_TWISTING_SLASH && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
         (ip->Type >= ITEM_WING_OF_DIMENSION && ip->Type < ITEM_CAPE_OF_EMPEROR) &&
             (ip->Type != ITEM_CAPE_OF_FIGHTER)))
    {
        ip->RequireLevel = p->RequireLevel;
    }
    else if (p->RequireLevel &&
             ((ip->Type >= ITEM_WING && ip->Type <= ITEM_ORB_OF_TWISTING_SLASH) ||
              (ip->Type >= ITEM_WING_OF_CURSE && ip->Type <= ITEM_WINGS_OF_DESPAIR) ||
              (ip->Type == ITEM_CAPE_OF_FIGHTER) || ip->Type >= ITEM_HELPER))
    {
        ip->RequireLevel = p->RequireLevel + ip->Level * addValue;
    }
    else
        ip->RequireLevel = 0;

    if (p->RequireStrength)
        ip->RequireStrength = 20 + (p->RequireStrength) * (ItemLevel + ip->Level * 3) * 3 / 100;
    else
        ip->RequireStrength = 0;

    if (p->RequireDexterity)
        ip->RequireDexterity = 20 + (p->RequireDexterity) * (ItemLevel + ip->Level * 3) * 3 / 100;
    else
        ip->RequireDexterity = 0;

    if (p->RequireVitality)
        ip->RequireVitality = 20 + (p->RequireVitality) * (ItemLevel + ip->Level * 3) * 3 / 100;
    else
        ip->RequireVitality = 0;

    if (p->RequireEnergy)
    {
        if (ip->Type >= ITEM_BOOK_OF_SAHAMUTT && ip->Type <= ITEM_STAFF + 29)
        {
            ip->RequireEnergy = 20 + (p->RequireEnergy) * (ItemLevel + ip->Level * 1) * 3 / 100;
        }
        else

            if ((p->RequireLevel > 0) &&
                (ip->Type >= ITEM_ETC && ip->Type < ITEM_ETC + MAX_ITEM_INDEX))
        {
            ip->RequireEnergy = 20 + (p->RequireEnergy) * (p->RequireLevel) * 4 / 100;
        }
        else

        {
            ip->RequireEnergy = 20 + (p->RequireEnergy) * (ItemLevel + ip->Level * 3) * 4 / 100;
        }
    }
    else
    {
        ip->RequireEnergy = 0;
    }

    if (p->RequireCharisma)
        ip->RequireCharisma = 20 + (p->RequireCharisma) * (ItemLevel + ip->Level * 3) * 3 / 100;
    else
        ip->RequireCharisma = 0;

    if (ip->Type == ITEM_ORB_OF_SUMMONING)
    {
        WORD Energy = 0;

        switch (ip->Level)
        {
        case 0:
            Energy = 30;
            break;
        case 1:
            Energy = 60;
            break;
        case 2:
            Energy = 90;
            break;
        case 3:
            Energy = 130;
            break;
        case 4:
            Energy = 170;
            break;
        case 5:
            Energy = 210;
            break;
        case 6:
            Energy = 300;
            break;
        case 7:
            Energy = 500;
            break;
        }
        ip->RequireEnergy = Energy;
    }

    if (p->RequireCharisma)
    {
        if (ip->Type == MODEL_DARK_RAVEN_ITEM)
            ip->RequireCharisma = (185 + (p->RequireCharisma * 15));
        else
            ip->RequireCharisma = p->RequireCharisma;
    }

    if (ip->Type == ITEM_TRANSFORMATION_RING)
    {
        if (ip->Level <= 2)
            ip->RequireLevel = 20;
        else
            ip->RequireLevel = 50;
    }

    if ((ip->Type >= ITEM_DRAGON_KNIGHT_HELM && ip->Type <= ITEM_SUNLIGHT_MASK) ||
        (ip->Type >= ITEM_DRAGON_KNIGHT_ARMOR && ip->Type <= ITEM_SUNLIGHT_ARMOR) ||
        (ip->Type >= ITEM_DRAGON_KNIGHT_PANTS && ip->Type <= ITEM_SUNLIGHT_PANTS) ||
        (ip->Type >= ITEM_DRAGON_KNIGHT_GLOVES && ip->Type <= ITEM_SUNLIGHT_GLOVES) ||
        (ip->Type >= ITEM_DRAGON_KNIGHT_BOOTS && ip->Type <= ITEM_SUNLIGHT_BOOTS) ||
        ip->Type == ITEM_BONE_BLADE || ip->Type == ITEM_EXPLOSION_BLADE ||
        ip->Type == ITEM_GRAND_VIPER_STAFF || ip->Type == ITEM_SYLPH_WIND_BOW ||
        ip->Type == ITEM_SOLEIL_SCEPTER || ITEM_STORM_BLITZ_STICK == ip->Type ||
        ITEM_AURA_HELM == ip->Type || ITEM_AURA_ARMOR == ip->Type || ITEM_AURA_PANTS == ip->Type ||
        ITEM_AURA_GLOVES == ip->Type || ITEM_AURA_BOOTS == ip->Type || Check_LuckyItem(ip->Type))
    {
        isExcellent = false;
    }

    if (isExcellent && ip->RequireLevel > 0 && !IsWingItem(ip) && ip->Type != ITEM_HORN_OF_FENRIR)
    {
        ip->RequireLevel += 20;
    }
}

void SessionGameDataUnit::CalcWingOptions(ITEM *ip) const
{
    if ((ip->Type >= ITEM_WINGS_OF_SPIRITS && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
        ip->Type == ITEM_WINGS_OF_DESPAIR)
    {
        if (ip->ExcellentFlags & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 50 + ip->Level * 5;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_HP_MAX;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 1) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 50 + ip->Level * 5;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_MP_MAX;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 2) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 3;
            ip->Special[ip->SpecialNum] = AT_ONE_PERCENT_DAMAGE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 3) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 50;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_AG_MAX;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 4) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 5;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
            ip->SpecialNum++;
        }
    }
    else if (ip->Type == ITEM_CAPE_OF_LORD || ip->Type == ITEM_CAPE_OF_FIGHTER)
    {
        if (ip->ExcellentFlags & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 50 + ip->Level * 5;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_HP_MAX;
            ip->SpecialNum++;
        }

        if ((ip->ExcellentFlags >> 1) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 50 + ip->Level * 5;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_MP_MAX;
            ip->SpecialNum++;
        }

        if ((ip->ExcellentFlags >> 2) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 3;
            ip->Special[ip->SpecialNum] = AT_ONE_PERCENT_DAMAGE;
            ip->SpecialNum++;
        }

        if ((ip->ExcellentFlags >> 3) & 0x01 && (ip->Type != ITEM_CAPE_OF_FIGHTER))
        {
            ip->SpecialValue[ip->SpecialNum] = 10 + ip->Level * 5;
            ip->Special[ip->SpecialNum] = AT_SET_OPTION_IMPROVE_CHARISMA;
            ip->SpecialNum++;
        }
    }
    else if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
             ip->Type == ITEM_WING_OF_DIMENSION || (ip->Type == ITEM_CAPE_OF_OVERRULE))
    {
        if (ip->ExcellentFlags & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 5;
            ip->Special[ip->SpecialNum] = AT_ONE_PERCENT_DAMAGE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 1) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 5;
            ip->Special[ip->SpecialNum] = AT_DAMAGE_REFLECTION;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 2) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 5;
            ip->Special[ip->SpecialNum] = AT_RECOVER_FULL_LIFE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 3) & 0x01)
        {
            ip->SpecialValue[ip->SpecialNum] = 5;
            ip->Special[ip->SpecialNum] = AT_RECOVER_FULL_MANA;
            ip->SpecialNum++;
        }
    }
}

void SessionGameDataUnit::CalcExcellentOptions(ITEM *ip) const
{
    if ((ip->Type >= ITEM_SHIELD && ip->Type < ITEM_BOOTS + MAX_ITEM_INDEX) ||
        (ip->Type >= ITEM_RING_OF_ICE && ip->Type <= ITEM_RING_OF_POISON) ||
        (ip->Type >= ITEM_RING_OF_FIRE && ip->Type <= ITEM_RING_OF_MAGIC))
    {
        // todo: add enum for these flags
        if ((ip->ExcellentFlags >> 5) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_LIFE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 4) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_MANA;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 3) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_DECREASE_DAMAGE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 2) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_REFLECTION_DAMAGE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 1) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_BLOCKING_PERCENT;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_GAIN_GOLD;
            ip->SpecialNum++;
        }
    }
    if ((ip->Type >= ITEM_SWORD && ip->Type < ITEM_STAFF + MAX_ITEM_INDEX) ||
        (ip->Type >= ITEM_PENDANT_OF_LIGHTING && ip->Type <= ITEM_PENDANT_OF_FIRE) ||
        (ip->Type >= ITEM_PENDANT_OF_ICE && ip->Type <= ITEM_PENDANT_OF_ABILITY))
    {
        if ((ip->ExcellentFlags >> 5) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_EXCELLENT_DAMAGE;
            ip->SpecialNum++;
        }
        if ((ip->Type >= ITEM_STAFF && ip->Type < ITEM_STAFF + MAX_ITEM_INDEX) ||
            (ip->Type == ITEM_PENDANT_OF_LIGHTING) ||
            (ip->Type == ITEM_PENDANT_OF_ICE || ip->Type == ITEM_PENDANT_OF_WATER))
        {
            if ((ip->ExcellentFlags >> 4) & 1)
            {
                ip->SpecialValue[ip->SpecialNum] = CharacterAttribute->Level / 20;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC_LEVEL;
                ip->SpecialNum++;
            }
            if ((ip->ExcellentFlags >> 3) & 1)
            {
                ip->SpecialValue[ip->SpecialNum] = 2;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC_PERCENT;
                ip->SpecialNum++;
            }
        }
        else
        {
            if ((ip->ExcellentFlags >> 4) & 1)
            {
                ip->SpecialValue[ip->SpecialNum] = CharacterAttribute->Level / 20;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE_LEVEL;
                ip->SpecialNum++;
            }
            if ((ip->ExcellentFlags >> 3) & 1)
            {
                ip->SpecialValue[ip->SpecialNum] = 2;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE_PERCENT;
                ip->SpecialNum++;
            }
        }
        if ((ip->ExcellentFlags >> 2) & 1)
        {
            ip->SpecialValue[ip->SpecialNum] = 7;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags >> 1) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_GAIN_LIFE;
            ip->SpecialNum++;
        }
        if ((ip->ExcellentFlags) & 1)
        {
            ip->Special[ip->SpecialNum] = AT_IMPROVE_GAIN_MANA;
            ip->SpecialNum++;
        }
    }
    if (ip->Type == ITEM_WIZARDS_RING)
    {
        switch (ip->Level)
        {
        case 0:
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC_PERCENT;
            ip->SpecialNum++;
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE_PERCENT;
            ip->SpecialNum++;
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
            ip->SpecialNum++;
            break;
        case 3:
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC_PERCENT;
            ip->SpecialNum++;
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE_PERCENT;
            ip->SpecialNum++;
            ip->SpecialValue[ip->SpecialNum] = 10;
            ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
            ip->SpecialNum++;
            break;
        }
    }
    if (ip->Type == ITEM_HELPER + 107)
    {
        ip->SpecialValue[ip->SpecialNum] = 15;
        ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC_PERCENT;
        ip->SpecialNum++;
        ip->SpecialValue[ip->SpecialNum] = 15;
        ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE_PERCENT;
        ip->SpecialNum++;
        ip->SpecialValue[ip->SpecialNum] = 10;
        ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
        ip->SpecialNum++;
    }
}

void SessionGameDataUnit::CalcPartType(ITEM *ip) const
{
    //part
    if (ip->Type >= ITEM_BOW && ip->Type < ITEM_CROSSBOW || ip->Type == ITEM_CELESTIAL_BOW)
        ip->Part = EQUIPMENT_WEAPON_LEFT;
    if (ip->Type >= ITEM_BOOK_OF_SAHAMUTT && ip->Type <= ITEM_STAFF + 29)
        ip->Part = EQUIPMENT_WEAPON_LEFT;
    else if (ip->Type >= ITEM_SWORD && ip->Type < ITEM_STAFF + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_WEAPON_RIGHT;
    else if (ip->Type >= ITEM_SHIELD && ip->Type < ITEM_SHIELD + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_WEAPON_LEFT;
    else if (ip->Type >= ITEM_HELM && ip->Type < ITEM_HELM + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_HELM;
    else if (ip->Type >= ITEM_ARMOR && ip->Type < ITEM_ARMOR + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_ARMOR;
    else if (ip->Type >= ITEM_PANTS && ip->Type < ITEM_PANTS + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_PANTS;
    else if (ip->Type >= ITEM_GLOVES && ip->Type < ITEM_GLOVES + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_GLOVES;
    else if (ip->Type >= ITEM_BOOTS && ip->Type < ITEM_BOOTS + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_BOOTS;
    else if (ip->Type >= ITEM_WING && ip->Type < ITEM_ORB_OF_TWISTING_SLASH)
        ip->Part = EQUIPMENT_WING;
    else if (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_WING_OF_DIMENSION)
        ip->Part = EQUIPMENT_WING;
    else if (ip->Type == ITEM_DARK_RAVEN_ITEM)
        ip->Part = EQUIPMENT_WEAPON_LEFT;
    else if (ip->Type >= ITEM_HELPER && ip->Type < ITEM_RING_OF_ICE)
        ip->Part = EQUIPMENT_HELPER;
    else if ((ip->Type >= ITEM_RING_OF_ICE && ip->Type < ITEM_PENDANT_OF_LIGHTING) ||
             (ip->Type == ITEM_WIZARDS_RING && ip->Level == 0) ||
             (ip->Type == ITEM_WIZARDS_RING && ip->Level == 3))
        ip->Part = EQUIPMENT_RING_RIGHT;
    else if (ip->Type >= ITEM_RING_OF_FIRE && ip->Type <= ITEM_RING_OF_MAGIC)
        ip->Part = EQUIPMENT_RING_RIGHT;
    else if (ip->Type >= ITEM_PENDANT_OF_ICE && ip->Type <= ITEM_PENDANT_OF_ABILITY)
        ip->Part = EQUIPMENT_AMULET;
    else if (ip->Type >= ITEM_PENDANT_OF_LIGHTING && ip->Type < ITEM_HELPER + MAX_ITEM_INDEX)
        ip->Part = EQUIPMENT_AMULET;
    else if (ip->Type >= ITEM_CAPE_OF_FIGHTER && ip->Type <= ITEM_CAPE_OF_OVERRULE)
        ip->Part = EQUIPMENT_WING;
    else
        ip->Part = -1;
}

void SessionGameDataUnit::SetItemAttributes(ITEM *ip) const
{
    int excelAddValue = GetExcellentAddValue(ip);

    ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];
    ip->TwoHand = p->TwoHand;
    ip->WeaponSpeed = p->WeaponSpeed;
    ip->DamageMin = p->DamageMin;
    ip->DamageMax = p->DamageMax;
    ip->SuccessfulBlocking = p->SuccessfulBlocking;
    ip->Defense = p->Defense;
    ip->MagicDefense = p->MagicDefense;
    ip->WalkSpeed = p->WalkSpeed;
    ip->MagicPower = p->MagicPower;

    CalcDamageMin(ip, p, excelAddValue);
    CalcDamageMax(ip, p, excelAddValue);
    CalcMagicPower(ip, p, excelAddValue);
    CalcSuccessfulBlocking(ip, p);
    CalcDefense(ip, p);
    CalcRequirements(ip, p);

    ip->SpecialNum = 0;

    CalcWingOptions(ip);

    if (ip->HasSkill)
    {
        if (p->m_wSkillIndex != 0)
        {
            ip->Special[ip->SpecialNum] = p->m_wSkillIndex;
            ip->SpecialNum++;
        }
    }

    if (ip->HasLuck)
    {
        if (ip->Type >= ITEM_SWORD && ip->Type < ITEM_BOOTS + MAX_ITEM_INDEX)
        {
            if (ip->Type != ITEM_BOLT && ip->Type != ITEM_ARROWS)
            {
                ip->Special[ip->SpecialNum] = AT_LUCK;
                ip->SpecialNum++;
            }
        }
        if ((ip->Type >= ITEM_WING && ip->Type <= ITEM_WINGS_OF_DARKNESS) ||
            (ip->Type >= ITEM_WING_OF_CURSE && ip->Type <= ITEM_WINGS_OF_DESPAIR))
        {
            ip->Special[ip->SpecialNum] = AT_LUCK;
            ip->SpecialNum++;
        }
        if (ip->Type == ITEM_CAPE_OF_LORD || ip->Type == ITEM_CAPE_OF_FIGHTER)
        {
            ip->Special[ip->SpecialNum] = AT_LUCK;
            ip->SpecialNum++;
        }
        if ((ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_CAPE_OF_EMPEROR) ||
            ip->Type == ITEM_WING_OF_DIMENSION || (ip->Type == ITEM_CAPE_OF_OVERRULE))
        {
            ip->Special[ip->SpecialNum] = AT_LUCK;
            ip->SpecialNum++;
        }
    }

    int Option3 = ip->OptionLevel;
    if (Option3)
    {
        if (ip->Type == ITEM_HORN_OF_DINORANT)
        {
            if (Option3 & 0x01)
            {
                ip->SpecialValue[ip->SpecialNum] = 5;
                ip->Special[ip->SpecialNum] = AT_DAMAGE_ABSORB;
                ip->SpecialNum++;
            }
            if ((Option3 >> 1) & 0x01)
            {
                ip->SpecialValue[ip->SpecialNum] = 50;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_AG_MAX;
                ip->SpecialNum++;
            }
            if ((Option3 >> 2) & 0x01)
            {
                ip->SpecialValue[ip->SpecialNum] = 5;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_ATTACK_SPEED;
                ip->SpecialNum++;
            }
        }
        else
        {
            if (ip->Type >= ITEM_SWORD && ip->Type < ITEM_BOW + MAX_ITEM_INDEX)
            {
                if (ip->Type != ITEM_BOLT && ip->Type != ITEM_ARROWS)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                    ip->RequireStrength += Option3 * 5;
                }
            }
            if (ip->Type >= ITEM_STAFF && ip->Type < ITEM_STAFF + MAX_ITEM_INDEX)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                if (ip->Type >= ITEM_BOOK_OF_SAHAMUTT && ip->Type <= ITEM_STAFF + 29)
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_CURSE;
                else
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                ip->SpecialNum++;
                ip->RequireStrength += Option3 * 5;
            }
            if (ip->Type >= ITEM_SHIELD && ip->Type < ITEM_SHIELD + MAX_ITEM_INDEX)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 5;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_BLOCKING;
                ip->SpecialNum++;
                ip->RequireStrength += Option3 * 5;
            }
            if (ip->Type >= ITEM_HELM && ip->Type < ITEM_BOOTS + MAX_ITEM_INDEX)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_DEFENSE;
                ip->SpecialNum++;
                ip->RequireStrength += Option3 * 5;
            }
            if (ip->Type >= ITEM_RING_OF_ICE && ip->Type < ITEM_HELPER + MAX_ITEM_INDEX &&
                ip->Type != ITEM_CAPE_OF_LORD)
            {
                if (ip->Type == ITEM_RING_OF_MAGIC)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAX_MANA;
                    ip->SpecialNum++;
                }
                else if (ip->Type == ITEM_PENDANT_OF_ABILITY)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAX_AG;
                    ip->SpecialNum++;
                }
                else
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
            }
            if (ip->Type == ITEM_WING)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3;
                ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                ip->SpecialNum++;
            }
            else if (ip->Type == ITEM_WINGS_OF_HEAVEN || ip->Type == ITEM_WING_OF_CURSE)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                ip->SpecialNum++;
            }
            else if (ip->Type == ITEM_WINGS_OF_SATAN)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                ip->SpecialNum++;
            }
            else if (ip->Type == ITEM_WINGS_OF_SPIRITS)
            {
                if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WINGS_OF_SOUL)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WINGS_OF_DRAGON)
            {
                if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WINGS_OF_DARKNESS)
            {
                if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_CAPE_OF_LORD)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                ip->SpecialNum++;
            }
            else if (ip->Type == ITEM_CAPE_OF_FIGHTER)
            {
                if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WINGS_OF_DESPAIR)
            {
                ip->SpecialValue[ip->SpecialNum] = Option3 * 4;

                if (ip->OptionType == 2)
                {
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                }
                if (ip->OptionType == 0)
                {
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_CURSE;
                }

                ip->SpecialNum++;
            }
            else if (ip->Type == ITEM_WING_OF_STORM)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DEFENSE;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WING_OF_ETERNAL)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DEFENSE;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WING_OF_ILLUSION)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DEFENSE;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WING_OF_RUIN)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_CAPE_OF_EMPEROR || ip->Type == ITEM_CAPE_OF_OVERRULE)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DEFENSE;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_DAMAGE;
                    ip->SpecialNum++;
                }
            }
            else if (ip->Type == ITEM_WING_OF_DIMENSION)
            {
                if (ip->OptionType == 0)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3;
                    ip->Special[ip->SpecialNum] = AT_LIFE_REGENERATION;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 2)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_CURSE;
                    ip->SpecialNum++;
                }
                else if (ip->OptionType == 3)
                {
                    ip->SpecialValue[ip->SpecialNum] = Option3 * 4;
                    ip->Special[ip->SpecialNum] = AT_IMPROVE_MAGIC;
                    ip->SpecialNum++;
                }
            }
        }
    }
    if (ip->Type == ITEM_DARK_HORSE_ITEM)
    {
        SetPetItemConvert(ip, GetPetInfo(ip));
    }

    CalcExcellentOptions(ip);

    CalcPartType(ip);
}

int64_t SessionGameDataUnit::ItemValue(ITEM *ip, int goldType) const
{
    if (ip->Type == -1)
        return 0;

    ITEM_ATTRIBUTE *p = &ItemAttribute[ip->Type];

    int64_t Gold = 0;

    if (p->iZen != 0)
    {
        Gold = p->iZen;

        if (goldType)
        {
            Gold = Gold / 3;
        }

        if (Gold >= 1000)
            Gold = Gold / 100 * 100;
        else if (Gold >= 100)
            Gold = Gold / 10 * 10;

        return (int)Gold;
    }

    int Type = ip->Type / MAX_ITEM_INDEX;
    int Level = ip->Level;
    bool Excellent = false;
    for (int i = 0; i < ip->SpecialNum; i++)
    {
        switch (ip->Special[i])
        {
        case AT_IMPROVE_LIFE:
        case AT_IMPROVE_MANA:
        case AT_DECREASE_DAMAGE:
        case AT_REFLECTION_DAMAGE:
        case AT_IMPROVE_BLOCKING_PERCENT:
        case AT_IMPROVE_GAIN_GOLD:
        case AT_EXCELLENT_DAMAGE:
        case AT_IMPROVE_DAMAGE_LEVEL:
        case AT_IMPROVE_DAMAGE_PERCENT:
        case AT_IMPROVE_MAGIC_LEVEL:
        case AT_IMPROVE_MAGIC_PERCENT:
        case AT_IMPROVE_ATTACK_SPEED:
        case AT_IMPROVE_GAIN_LIFE:
        case AT_IMPROVE_GAIN_MANA:
            Excellent = true;
            break;
        }
    }
    int Level2 = p->Level + Level * 3;
    if (Excellent)
        Level2 += 25;

    if (ip->Type == ITEM_BOLT)
    {
        int sellMoney = 0;

        switch (Level)
        {
        case 0:
            sellMoney = 100;
            break;
        case 1:
            sellMoney = 1400;
            break;
        case 2:
            sellMoney = 2200;
            break;
        case 3:
            sellMoney = 3000;
            break;
        }
        if (p->Durability > 0)
            Gold = (sellMoney * ip->Durability / p->Durability); //+(170*(Level*2));
    }
    else if (ip->Type == ITEM_ARROWS)
    {
        int sellMoney = 0;

        switch (Level)
        {
        case 0:
            sellMoney = 70;
            break;
        case 1:
            sellMoney = 1200;
            break;
        case 2:
            sellMoney = 2000;
            break;
        case 3:
            sellMoney = 2800;
            break;
        }
        if (p->Durability > 0)
            Gold = (sellMoney * ip->Durability / p->Durability); //+(80*(Level*2));
    }
    else if (isCompiledGem(ip))
    {
        Gold = CalcItemValue(ip);
    }
    else if (ip->Type == ITEM_JEWEL_OF_BLESS)
    {
        Gold = 9000000;
    }
    else if (ip->Type == ITEM_JEWEL_OF_SOUL)
    {
        Gold = 6000000;
    }
    else if (ip->Type == ITEM_JEWEL_OF_CHAOS)
    {
        Gold = 810000;
    }
    else if (ip->Type == ITEM_JEWEL_OF_LIFE)
    {
        Gold = 45000000;
    }
    else if (ip->Type == ITEM_JEWEL_OF_CREATION)
    {
        Gold = 36000000;
    }
    else if (ip->Type == ITEM_POTION + 141)
    {
        Gold = 224000 * 3;
    }
    else if (ip->Type == ITEM_POTION + 142)
    {
        Gold = 182000 * 3;
    }
    else if (ip->Type == ITEM_POTION + 143)
    {
        Gold = 157000 * 3;
    }
    else if (ip->Type == ITEM_POTION + 144)
    {
        Gold = 121000 * 3;
    }

    else if (ip->Type == ITEM_LOCHS_FEATHER)
    {
        switch (Level)
        {
        case 0:
            Gold = 180000;
            break;
        case 1:
            Gold = 7500000;
            break;
        }
    }
    else if (ip->Type == ITEM_HORN_OF_DINORANT)
    {
        Gold = 960000;
        for (int i = 0; i < ip->SpecialNum; i++)
        {
            switch (ip->Special[i])
            {
            case AT_DAMAGE_ABSORB:
            case AT_IMPROVE_AG_MAX:
            case AT_IMPROVE_ATTACK_SPEED:
                Gold += 300000;
                break;
            }
        }
    }
    else if (ip->Type == ITEM_FRUITS)
    {
        Gold = 33000000;
    }
    else if (ip->Type == ITEM_SCROLL_OF_ARCHANGEL || ip->Type == ITEM_BLOOD_BONE)
    {
        switch (Level)
        {
        case 1:
            Gold = 10000;
            break;
        case 2:
            Gold = 50000;
            break;
        case 3:
            Gold = 100000;
            break;
        case 4:
            Gold = 300000;
            break;
        case 5:
            Gold = 500000;
            break;
        case 6:
            Gold = 800000;
            break;
        case 7:
            Gold = 1000000;
            break;
        case 8:
            Gold = 1200000;
            break;
        default:
            Gold = 0;
        }
    }
    else if (ip->Type == ITEM_INVISIBILITY_CLOAK)
    {
        Gold = (long long)(200000 + 20000 * (Level - 1));
        if (Level == 1)
        {
            Gold = 50000;
        }
        Gold *= 3;
    }
    else if (ip->Type == ITEM_LOST_MAP)
    {
        Gold = 200000 * 3;
    }
    else if (ip->Type == ITEM_SYMBOL_OF_KUNDUN)
    {
        Gold = (long long)(ip->Durability * 10000) * 3;
    }
    else if (ip->Type == ITEM_POTION + 111)
    {
        Gold = 200000 * 3;
    }
    else if (ip->Type == ITEM_POTION + 110)
    {
        Gold = (long long)(ip->Durability * 10000) * 3;
    }
    else if (ITEM_GAIONS_ORDER == ip->Type)
    {
        Gold = 10000 * 3;
    }
    else if (ITEM_COMPLETE_SECROMICON == ip->Type)
    {
        Gold = 10000 * 3;
    }
    else if (ITEM_SUSPICIOUS_SCRAP_OF_PAPER == ip->Type ||
             ITEM_FIRST_SECROMICON_FRAGMENT <= ip->Type &&
                 ip->Type <= ITEM_SIXTH_SECROMICON_FRAGMENT)
    {
        Gold = (long long)((long long)ip->Durability * 10000) * 3;
    }
    else if (ip->Type == ITEM_ARMOR_OF_GUARDSMAN)
    {
        Gold = 5000;
    }
    else if (ip->Type == ITEM_POTION + 21)
    {
        if (Level == 0)
            Gold = 9000;
        else if (Level == 1)
            Gold = 9000;
        else if (Level == 2)
            Gold = 3000 * 3;
        else if (Level == 3)
        {
            Gold = (long long)ip->Durability * 3900;
        }
    }
    else if (ip->Type == ITEM_DEVILS_EYE)
    {
        int iValue[8] = {30000, 10000, 50000, 100000, 300000, 500000, 800000, 1000000};
        Gold = iValue[std::min<int>(std::max<int>(0, Level), 7)];
    }
    else if (ip->Type == ITEM_DEVILS_KEY)
    {
        int iValue[8] = {30000, 15000, 75000, 150000, 450000, 750000, 1200000, 1500000};
        Gold = iValue[std::min<int>(std::max<int>(0, Level), 7)];
    }
    else if (ip->Type == ITEM_DEVILS_INVITATION)
    {
        int iValue[8] = {120000, 60000, 84000, 120000, 180000, 240000, 300000, 180000};
        Gold = iValue[std::min<int>(std::max<int>(0, Level), 7)];
    }
    else if (ip->Type == ITEM_OLD_SCROLL || ip->Type == ITEM_ILLUSION_SORCERER_COVENANT ||
             ip->Type == ITEM_SCROLL_OF_BLOOD)
    {
        switch (Level)
        {
        case 1:
            Gold = 500000;
            break;
        case 2:
            Gold = 600000;
            break;
        case 3:
            Gold = 800000;
            break;
        case 4:
            Gold = 1000000;
            break;
        case 5:
            Gold = 1200000;
            break;
        case 6:
            Gold = 1400000;
            break;
        default:
            Gold = 3000 * 3;
        }
    }
    else if (ip->Type == ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR ||
             ip->Type == ITEM_BROKEN_SWORD_DARK_STONE || ip->Type == ITEM_TEAR_OF_ELF ||
             ip->Type == ITEM_SOUL_SHARD_OF_WIZARD || ip->Type == ITEM_FLAME_OF_DEATH_BEAM_KNIGHT ||
             ip->Type == ITEM_HORN_OF_HELL_MAINE || ip->Type == ITEM_FEATHER_OF_DARK_PHOENIX ||
             ip->Type == ITEM_EYE_OF_ABYSSAL)
    {
        Gold = 9000;
    }
    else if (ip->Type == ITEM_ALE && Level == 1)
    {
        Gold = 1000;
    }
    else if (ip->Type == ITEM_POTION + 20)
    {
        Gold = 900;
    }
    else if (ip->Type == ITEM_CHRISTMAS_STAR)
    {
        Gold = 200000;
    }
    else if (ip->Type == ITEM_FIRECRACKER)
    {
        Gold = 200000;
    }
    else if (ip->Type == ITEM_GM_GIFT)
    {
        Gold = 33 * 3;
    }
    else if (ip->Type == ITEM_TOWN_PORTAL_SCROLL)
    {
        Gold = 750;
    }
    else if (ip->Type == ITEM_JEWEL_OF_GUARDIAN)
    {
        Gold = 60000000;
    }
    else if (ip->Type == ITEM_SIEGE_POTION)
    {
        switch (Level)
        {
        case 0:
            Gold = (long long)900000 * ip->Durability;
            break;
        case 1:
            Gold = (long long)450000 * ip->Durability;
            break;
        }
    }
    else if (ip->Type == ITEM_HELPER + 7)
    {
        switch (Level)
        {
        case 0:
            Gold = 1500000;
            break;
        case 1:
            Gold = 1200000;
            break;
        }
    }
    else if (ip->Type == ITEM_LIFE_STONE_ITEM)
    {
        switch (Level)
        {
        case 0:
            Gold = 100000;
            break;
        case 1:
            Gold = 2400000;
            break;
        }
    }
    else if (ip->Type >= ITEM_SMALL_SHIELD_POTION && ip->Type <= ITEM_LARGE_SHIELD_POTION)
    {
        switch (ip->Type)
        {
        case ITEM_SMALL_SHIELD_POTION:
            Gold = 2000;
            break;
        case ITEM_MEDIUM_SHIELD_POTION:
            Gold = 4000;
            break;
        case ITEM_LARGE_SHIELD_POTION:
            Gold = 6000;
            break;
        }
        Gold *= ip->Durability;
    }
    else if (ip->Type >= ITEM_SMALL_COMPLEX_POTION && ip->Type <= ITEM_LARGE_COMPLEX_POTION)
    {
        switch (ip->Type)
        {
        case ITEM_SMALL_COMPLEX_POTION:
            Gold = 2500;
            break;
        case ITEM_MEDIUM_COMPLEX_POTION:
            Gold = 5000;
            break;
        case ITEM_LARGE_COMPLEX_POTION:
            Gold = 7500;
            break;
        }
        Gold *= ip->Durability;
    }
    else if (ip->Type == ITEM_POTION + 100)
    {
        Gold = 100 * 3;
        Gold *= ip->Durability;
    }
    else if (ip->Type == ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH ||
             ip->Type == ITEM_CHERRY_BLOSSOM_WINE || ip->Type == ITEM_CHERRY_BLOSSOM_RICE_CAKE ||
             ip->Type == ITEM_CHERRY_BLOSSOM_FLOWER_PETAL)
    {
        Gold = 300;
        Gold *= ip->Durability;
    }

    else if (p->Value > 0)
    {
        Gold = p->Value * p->Value * 10 / 12;

        if (ip->Type == ITEM_LARGE_HEALING_POTION || ip->Type == ITEM_LARGE_MANA_POTION)
        {
            Gold = 1500;
        }

        if (ip->Type >= ITEM_POTION && ip->Type <= ITEM_ANTIDOTE)
        {
            if (Level > 0)
                Gold *= (int64_t)pow((double)2, (double)Level);

            Gold = Gold / 10 * 10;
            Gold *= (int64_t)ip->Durability;

            if (goldType)
            {
                Gold = Gold / 3;
                Gold = Gold / 10 * 10;
            }
            return (int64_t)Gold;
        }
    }
    else if (ip->Type == ITEM_WIZARDS_RING)
    {
        if (Level == 0)
            Gold = 3000;
        else if (Level == 1)
            Gold = 0;
    }
    else if (ip->Type == ITEM_FLAME_OF_CONDOR || ip->Type == ITEM_FEATHER_OF_CONDOR)
    {
        Gold = 3000000;
    }
    else if (ip->Type == ITEM_SPIRIT)
    {
        switch (Level)
        {
        case 0:
            Gold = 10000000 * 3;
            break;
        case 1:
            Gold = 5000000 * 3;
            break;
        }
    }
    else if (((Type == 12 &&
               (ip->Type > ITEM_WINGS_OF_DARKNESS &&
                !(ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_WING_OF_DIMENSION) &&
                (ip->Type != ITEM_CAPE_OF_OVERRULE))) ||
              Type == 13 || Type == 15))
    {
        Gold = (long long)100 + Level2 * Level2 * Level2;

        for (int i = 0; i < ip->SpecialNum; i++)
        {
            switch (ip->Special[i])
            {
            case AT_LIFE_REGENERATION:
                Gold += Gold * ip->SpecialValue[i];
                break;
            }
        }
    }
    else if (ip->Type == ITEM_POTION + 53)
    {
        Gold = 0;
    }
    else
    {
        switch (Level)
        {
        case 5:
            Level2 += 4;
            break;
        case 6:
            Level2 += 10;
            break;
        case 7:
            Level2 += 25;
            break;
        case 8:
            Level2 += 45;
            break;
        case 9:
            Level2 += 65;
            break;
        case 10:
            Level2 += 95;
            break;
        case 11:
            Level2 += 135;
            break;
        case 12:
            Level2 += 185;
            break;
        case 13:
            Level2 += 245;
            break;
        case 14:
            Level2 += 305;
            break;
        case 15:
            Level2 += 365;
            break;
        }
        if ((Type == 12 && ip->Type <= ITEM_WINGS_OF_DARKNESS) || ip->Type == ITEM_CAPE_OF_LORD ||
            (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_WING_OF_DIMENSION) ||
            (ip->Type == ITEM_CAPE_OF_OVERRULE))
        {
            Gold = (long long)(40000000 + ((40 + Level2) * Level2 * Level2 * 11));
        }
        else
        {
            Gold = (long long)100 + (40 + Level2) * Level2 * Level2 / 8;
        }
        if (Type >= 0 && Type <= 6)
        {
            if (!p->TwoHand)
                Gold = Gold * 80 / 100;
        }
        for (int i = 0; i < ip->SpecialNum; i++)
        {
            switch (ip->Special[i])
            {
            case AT_SKILL_BLOCKING:
            case AT_SKILL_FALLING_SLASH:
            case AT_SKILL_FALLING_SLASH_STR:
            case AT_SKILL_LUNGE:
            case AT_SKILL_LUNGE_STR:
            case AT_SKILL_UPPERCUT:
            case AT_SKILL_CYCLONE:
            case AT_SKILL_CYCLONE_STR:
            case AT_SKILL_CYCLONE_STR_MG:
            case AT_SKILL_SLASH:
            case AT_SKILL_SLASH_STR:
            case AT_SKILL_TRIPLE_SHOT:
            case AT_SKILL_TRIPLE_SHOT_STR:
            case AT_SKILL_TRIPLE_SHOT_MASTERY:
            case AT_SKILL_RECOVER:
            case AT_SKILL_CHAOTIC_DISEIER:
            case AT_SKILL_MULTI_SHOT:
            case AT_SKILL_POWER_SLASH:
            case AT_SKILL_POWER_SLASH_STR:
            case AT_SKILL_KILLING_BLOW:
            case AT_SKILL_KILLING_BLOW_STR:
            case AT_SKILL_KILLING_BLOW_MASTERY:
            case AT_SKILL_BEAST_UPPERCUT:
            case AT_SKILL_BEAST_UPPERCUT_STR:
            case AT_SKILL_BEAST_UPPERCUT_MASTERY:
                Gold += (long long)((double)Gold * 1.5f);
                break;
            case AT_IMPROVE_DAMAGE:
            case AT_IMPROVE_MAGIC:
            case AT_IMPROVE_CURSE:
            case AT_IMPROVE_DEFENSE:
            case AT_LIFE_REGENERATION:
                if ((Type == 12 && ip->Type <= ITEM_WINGS_OF_DARKNESS)

                    || (ip->Type >= ITEM_WING_OF_STORM && ip->Type <= ITEM_WING_OF_DIMENSION) ||
                    (ip->Type >= ITEM_CAPE_OF_FIGHTER &&
                     ip->Type <= ITEM_CAPE_OF_OVERRULE)) //  날개.
                {
                    int iOption = ip->SpecialValue[i];
                    if (AT_LIFE_REGENERATION == ip->Special[i])
                    {
                        iOption *= 4;
                    }
                    switch (iOption)
                    {
                    case 4:
                        Gold += (int64_t)((double)Gold * 6 / 10);
                        break;
                    case 8:
                        Gold += (int64_t)((double)Gold * 14 / 10);
                        break;
                    case 12:
                        Gold += (int64_t)((double)Gold * 28 / 10);
                        break;
                    case 16:
                        Gold += (int64_t)((double)Gold * 56 / 10);
                        break;
                    }
                }
                else
                {
                    switch (ip->SpecialValue[i])
                    {
                    case 4:
                        Gold += (int64_t)((double)Gold * 6 / 10);
                        break;
                    case 8:
                        Gold += (int64_t)((double)Gold * 14 / 10);
                        break;
                    case 12:
                        Gold += (int64_t)((double)Gold * 28 / 10);
                        break;
                    case 16:
                        Gold += (int64_t)((double)Gold * 56 / 10);
                        break;
                    }
                }
                break;
            case AT_IMPROVE_BLOCKING:
                switch (ip->SpecialValue[i])
                {
                case 5:
                    Gold += (int64_t)((double)Gold * 6 / 10);
                    break;
                case 10:
                    Gold += (int64_t)((double)Gold * 14 / 10);
                    break;
                case 15:
                    Gold += (int64_t)((double)Gold * 28 / 10);
                    break;
                case 20:
                    Gold += (int64_t)((double)Gold * 56 / 10);
                    break;
                }
                break;
            case AT_LUCK:
                Gold += (int64_t)((double)Gold * 25 / 100);
                break;
            case AT_IMPROVE_LIFE:
            case AT_IMPROVE_MANA:
            case AT_DECREASE_DAMAGE:
            case AT_REFLECTION_DAMAGE:
            case AT_IMPROVE_BLOCKING_PERCENT:
            case AT_IMPROVE_GAIN_GOLD:
            case AT_EXCELLENT_DAMAGE:
            case AT_IMPROVE_DAMAGE_LEVEL:
            case AT_IMPROVE_DAMAGE_PERCENT:
            case AT_IMPROVE_MAGIC_LEVEL:
            case AT_IMPROVE_MAGIC_PERCENT:
            case AT_IMPROVE_GAIN_LIFE:
            case AT_IMPROVE_GAIN_MANA:
            case AT_IMPROVE_ATTACK_SPEED:
                Gold += Gold;
                break;
            case AT_IMPROVE_EVADE:
                Gold += Gold;
                break;
            case AT_IMPROVE_HP_MAX:
            case AT_IMPROVE_MP_MAX:
            case AT_ONE_PERCENT_DAMAGE:
            case AT_IMPROVE_AG_MAX:
            case AT_DAMAGE_ABSORB:
            case AT_DAMAGE_REFLECTION:
            case AT_RECOVER_FULL_LIFE:
            case AT_RECOVER_FULL_MANA:
                Gold += (int64_t)((double)Gold * 25 / 100);
                break;
            }
        }
        Gold += g_SocketItemMgr.CalcSocketBonusItemValue(ip, Gold);
    }
    Gold = std::min<int64_t>(Gold, 3000000000LL);

    if (goldType == 2)
    {
        if (Gold >= 1000)
        {
            Gold = (Gold / 100) * 100;
        }
        else if (Gold >= 100)
        {
            Gold = (Gold / 10) * 10;
        }
    }

    if (goldType)
    {
        Gold = Gold / 3;
    }

    if (ip->Type == ITEM_SPLINTER_OF_ARMOR)
        Gold = (long long)ip->Durability * 50;
    else if (ip->Type == ITEM_BLESS_OF_GUARDIAN)
        Gold = (long long)ip->Durability * 100;
    else if (ip->Type == ITEM_CLAW_OF_BEAST)
        Gold = (long long)ip->Durability * 1000;
    else if (ip->Type == ITEM_FRAGMENT_OF_HORN)
        Gold = (long long)ip->Durability * 10000;
    else if (ip->Type == ITEM_BROKEN_HORN)
        Gold = 30000;
    else if (ip->Type == ITEM_HORN_OF_FENRIR)
        Gold = 50000;

    if (ip->Type >= ITEM_PUMPKIN_OF_LUCK && ip->Type <= ITEM_JACK_OLANTERN_DRINK)
    {
        Gold = (long long)ip->Durability * 50;
    }

    if (ip->Type == ITEM_HELPER + 71 || ip->Type == ITEM_HELPER + 72 ||
        ip->Type == ITEM_HELPER + 73 || ip->Type == ITEM_HELPER + 74 ||
        ip->Type == ITEM_HELPER + 75)
    {
        Gold = 2000000;
    }

    if ((ip->Type == ITEM_DARK_HORSE_ITEM) || (ip->Type == ITEM_DARK_RAVEN_ITEM))
    {
        PET_INFO *pPetInfo = GetPetInfo(ip);

        if (pPetInfo->m_dwPetType == PET_TYPE_NONE)
            return -1;

        Gold = GetPetItemValue(pPetInfo);
    }

    switch (ip->Type)
    {
    case ITEM_POTION + 112:
    case ITEM_POTION + 113:
    case ITEM_POTION + 121:
    case ITEM_POTION + 122:
    case ITEM_POTION + 123:
    case ITEM_POTION + 124:
    case ITEM_PET_PANDA:
    case ITEM_PANDA_TRANSFORMATION_RING:
    case ITEM_DEMON:
    case ITEM_SPIRIT_OF_GUARDIAN:
    case ITEM_HELPER + 109:
    case ITEM_HELPER + 110:
    case ITEM_HELPER + 111:
    case ITEM_HELPER + 112:
    case ITEM_HELPER + 113:
    case ITEM_HELPER + 114:
    case ITEM_HELPER + 115:
        Gold = 1000;
        break;
    case ITEM_SKELETON_TRANSFORMATION_RING:
    case ITEM_PET_SKELETON:
        Gold = 2000;
        break;
    case ITEM_WING + 130:
    case ITEM_WING + 131:
    case ITEM_WING + 132:
    case ITEM_WING + 133:
    case ITEM_WING + 134:
    case ITEM_WING + 135:
        Gold = 80;
        break;
    }

    if (ip->Type == ITEM_TRANSFORMATION_RING || ip->Type == ITEM_WIZARDS_RING ||
        ip->Type == ITEM_ARMOR_OF_GUARDSMAN)
        goto EXIT_CALCULATE;
    if (ip->Type == ITEM_BOLT || ip->Type == ITEM_ARROWS || ip->Type >= ITEM_POTION)
        goto EXIT_CALCULATE;
    if (ip->Type >= ITEM_ORB_OF_TWISTING_SLASH && ip->Type <= ITEM_ORB_OF_DEATH_STAB)
        goto EXIT_CALCULATE;
    if ((ip->Type >= ITEM_LOCHS_FEATHER && ip->Type <= ITEM_WEAPON_OF_ARCHANGEL) ||
        ip->Type == ITEM_POTION + 21)
        goto EXIT_CALCULATE;
    if (ip->Type == ITEM_SIEGE_POTION || ip->Type == ITEM_HELPER + 7 ||
        ip->Type == ITEM_LIFE_STONE_ITEM)
        goto EXIT_CALCULATE;
    if ((ip->Type >= ITEM_OLD_SCROLL) && (ip->Type <= ITEM_SCROLL_OF_BLOOD))
        goto EXIT_CALCULATE;

    switch (ip->Type)
    {
    case ITEM_POTION + 112:
        goto EXIT_CALCULATE;
        // MODEL_POTION+113
    case ITEM_POTION + 113:
        goto EXIT_CALCULATE;
    case ITEM_POTION + 121:
        goto EXIT_CALCULATE;
    case ITEM_POTION + 122:
        goto EXIT_CALCULATE;
    case ITEM_POTION + 123:
        goto EXIT_CALCULATE;
    case ITEM_POTION + 124:
        goto EXIT_CALCULATE;
    case ITEM_WING + 130:
    case ITEM_WING + 131:
    case ITEM_WING + 132:
    case ITEM_WING + 133:
    case ITEM_WING + 134:
    case ITEM_WING + 135:
        goto EXIT_CALCULATE;
    case ITEM_SKELETON_TRANSFORMATION_RING:
        goto EXIT_CALCULATE;
    case ITEM_PET_SKELETON:
        goto EXIT_CALCULATE;
    case ITEM_PET_PANDA:
        goto EXIT_CALCULATE;
    case ITEM_PANDA_TRANSFORMATION_RING:
        goto EXIT_CALCULATE;
    case ITEM_DEMON:
    case ITEM_SPIRIT_OF_GUARDIAN:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 109:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 110:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 111:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 112:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 113:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 114:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 115:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 128:
    case ITEM_HELPER + 129:
    case ITEM_HELPER + 134:
        goto EXIT_CALCULATE;
    case ITEM_HELPER + 130:
    case ITEM_HELPER + 131:
    case ITEM_HELPER + 132:
    case ITEM_HELPER + 133:
        goto EXIT_CALCULATE;
    }

    if (goldType == 1 && !(ip->Type >= ITEM_SPLINTER_OF_ARMOR && ip->Type <= ITEM_HORN_OF_FENRIR))
    {
        //        wchar_t Text[100];
        //int repairGold = ConvertRepairGold(Gold,ip->Durability, p->Durability, ip->Type, Text);
        DWORD maxDurability = CalcMaxDurability(ip, p, Level);
        float persent = 1.f - ((float)ip->Durability / (float)maxDurability);
        auto repairGold = (DWORD)(Gold * 0.6f * persent);

        if (ip->Type == ITEM_SPIRIT)
            repairGold = 0;

        Gold -= repairGold;
    }
EXIT_CALCULATE:

    if (Gold >= 1000)
    {
        Gold = (Gold / 100) * 100;
    }
    else if (Gold >= 100)
    {
        Gold = (Gold / 10) * 10;
    }
    if (Check_LuckyItem(ip->Type))
        Gold = 0;

    return (int64_t)Gold;
}

bool SessionGameDataUnit::IsRequireEquipItem(ITEM *pItem)
{
    if (pItem == NULL)
    {
        return false;
    }

    ITEM_ATTRIBUTE *pItemAttr = &ItemAttribute[pItem->Type];

    bool bEquipable = false;

    if (pItemAttr->RequireClass[gCharacterManager.GetBaseClass(Hero->Class)])
    {
        bEquipable = true;
    }
    else if (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK &&
             pItemAttr->RequireClass[CLASS_WIZARD] && pItemAttr->RequireClass[CLASS_KNIGHT])
    {
        bEquipable = true;
    }

    BYTE byFirstClass = gCharacterManager.GetBaseClass(Hero->Class);
    BYTE byStepClass = gCharacterManager.GetStepClass(Hero->Class);
    if (pItemAttr->RequireClass[byFirstClass] > byStepClass)
    {
        return false;
    }

    if (bEquipable == false)
        return false;

    WORD wStrength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
    WORD wDexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
    WORD wEnergy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
    WORD wVitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
    WORD wCharisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
    WORD wLevel = CharacterAttribute->Level;

    int iItemLevel = pItem->Level;

    int iDecNeedStrength = 0, iDecNeedDex = 0;

    if (iItemLevel >= pItem->Jewel_Of_Harmony_OptionLevel)
    {
        StrengthenCapability SC;
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, pItem, 0);

        if (SC.SI_isNB)
        {
            iDecNeedStrength = SC.SI_NB.SI_force;
            iDecNeedDex = SC.SI_NB.SI_activity;
        }
    }

    if (pItem->RequireStrength - iDecNeedStrength > wStrength)
        return false;
    if (pItem->RequireDexterity - iDecNeedDex > wDexterity)
        return false;
    if (pItem->RequireEnergy > wEnergy)
        return false;
    if (pItem->RequireVitality > wVitality)
        return false;
    if (pItem->RequireCharisma > wCharisma)
        return false;
    if (pItem->RequireLevel > wLevel)
        return false;

    if (pItem->Type == ITEM_DARK_RAVEN_ITEM)
    {
        PET_INFO *pPetInfo = GetPetInfo(pItem);
        WORD wRequireCharisma = static_cast<WORD>((185 + pPetInfo->m_wLevel * 15) & 0xFFFF);
        if (wRequireCharisma > wCharisma)
        {
            return false;
        }
    }

    return bEquipable;
}

void SessionGameDataUnit::PlusSpecial(WORD *Value, int Special, ITEM *Item)
{
    if (Item->Type == -1)
    {
        return;
    }

    if (IsRequireEquipItem(Item))
    {
        for (int i = 0; i < Item->SpecialNum; i++)
        {
            if (Item->Special[i] == Special && Item->Durability != 0)
                *Value += Item->SpecialValue[i];
        }
    }
}

void SessionGameDataUnit::PlusSpecialPercent(WORD *Value, int Special, ITEM *Item, WORD Percent)
{
    if (Item->Type == -1)
        return;

    if (IsRequireEquipItem(Item))
    {
        for (int i = 0; i < Item->SpecialNum; i++)
        {
            if (Item->Special[i] == Special && Item->Durability != 0)
                *Value += *Value * Percent / 100;
        }
    }
}

void SessionGameDataUnit::PlusSpecialPercent2(WORD *Value, int Special, ITEM *Item)
{
    if (Item->Type == -1)
        return;

    if (IsRequireEquipItem(Item))
    {
        for (int i = 0; i < Item->SpecialNum; i++)
        {
            if (Item->Special[i] == Special && Item->Durability != 0)
                *Value += (unsigned short)(*Value * (Item->SpecialValue[i] / 100.f));
        }
    }
}

WORD SessionGameDataUnit::ItemDefense(ITEM *Item)
{
    if (Item->Type == -1)
        return 0;
    WORD Defense = Item->Defense;
    PlusSpecial(&Defense, AT_IMPROVE_DEFENSE, Item);
    return Defense;
}

WORD SessionGameDataUnit::ItemMagicDefense(ITEM *Item)
{
    if (Item->Type == -1)
        return 0;
    WORD MagicDefense = Item->MagicDefense;
    //PlusSpecial(&MagicDefense,PLUS_MAGIC_DEFENSE,Item);
    return MagicDefense;
}

WORD SessionGameDataUnit::ItemWalkSpeed(ITEM *Item)
{
    if (Item->Type == -1)
        return 0;
    WORD WalkSpeed = Item->WalkSpeed;
    //PlusSpecial(&WalkSpeed,PLUS_WALK_SPEED,Item);
    return WalkSpeed;
}

void SessionGameDataUnit::MonsterConvert(MONSTER *m, int Level)
{
    MONSTER_SCRIPT *p = &MonsterScript[m->Type];
    MONSTER_ATTRIBUTE *c = &p->Attribute;
    m->Level = Level;
    m->Attribute.AttackSpeed = c->AttackSpeed;
    m->Attribute.AttackDamageMin = c->AttackDamageMin / 2 + c->AttackDamageMin / 2 * (m->Level) / 9;
    m->Attribute.AttackDamageMax = c->AttackDamageMax / 2 + c->AttackDamageMax / 2 * (m->Level) / 9;
    m->Attribute.Defense = c->Defense / 2 + c->Defense / 2 * (m->Level) / 9;
    m->Attribute.AttackRating = c->AttackRating / 2 + c->AttackRating / 2 * (m->Level) / 9;
    m->Attribute.SuccessfulBlocking =
        c->SuccessfulBlocking / 2 + c->SuccessfulBlocking / 2 * (m->Level) / 9;
}

float CalcDurabilityPercent(BYTE dur, BYTE maxDur, int Level, int excellentFlags,
                            int ancientDiscriminator)
{
    int maxDurability = maxDur;
    for (int i = 0; i < Level; i++)
    {
        if (i >= 4)
        {
            maxDurability += 2;
        }
        else
        {
            maxDurability++;
        }
    }

    if (ancientDiscriminator > 0)
    {
        maxDurability += 20;
    }
    else if ((excellentFlags & 63) > 0)
    {
        maxDurability += 15;
    }

    float durP = 1.f - (dur / (float)maxDurability);

    if (durP > 0.8f)
    {
        return 0.5f;
    }
    else if (durP > 0.7f)
    {
        return 0.3f;
    }
    else if (durP > 0.5f)
    {
        return 0.2f;
    }
    else
    {
        return 0.f;
    }
    return 0.f;
}

void CHARACTER_MACHINE::Init()
{
    Character = {};
    for (ITEM &item : Equipment)
    {
        item = {};
        item.Type = -1;
        item.Number = -1;
    }
    Gold = 0;
    StorageGold = 0;
    Enemy = {};
    AttackDamageRight = 0;
    AttackDamageLeft = 0;
    CriticalDamage = 0;
    FinalAttackDamageRight = 0;
    FinalAttackDamageLeft = 0;
    FinalHitPoint = 0;
    FinalAttackRating = 0;
    FinalDefenseRating = 0;
    FinalSuccessAttack = false;
    FinalSuccessDefense = false;
    PacketSerial = 0;
    InfinityArrowAdditionalMana = 0;
}

void CHARACTER_MACHINE::InitAddValue()
{
    Character.AddStrength = 0;
    Character.AddDexterity = 0;
    Character.AddVitality = 0;
    Character.AddEnergy = 0;
    Character.AddCharisma = 0;
    Character.AddLifeMax = 0;
    Character.AddManaMax = 0;
}

void SessionGameDataUnit::InitializeCharacter(CHARACTER_MACHINE &characterMachine, CLASS_TYPE Class)
{
    CLASS_ATTRIBUTE *c = &ClassAttribute[Class];
    characterMachine.Character.Class = Class;
    characterMachine.Character.Level = 1;
    characterMachine.Character.Strength = c->Strength;
    characterMachine.Character.Dexterity = c->Dexterity;
    characterMachine.Character.Vitality = c->Vitality;
    characterMachine.Character.Energy = c->Energy;
    characterMachine.Character.Life = c->Life;
    characterMachine.Character.Mana = c->Mana;
    characterMachine.Character.LifeMax = c->Life;
    characterMachine.Character.ManaMax = c->Mana;
    characterMachine.Character.Shield = c->Shield;
    characterMachine.Character.ShieldMax = c->Shield;

    characterMachine.InitAddValue();

    for (int j = 0; j < MAX_SKILLS; j++)
    {
        characterMachine.Character.Skill[j] = AT_SKILL_UNDEFINED;
    }
}

void CHARACTER_MACHINE::InputEnemyAttribute(MONSTER *e)
{
    memcpy(&Enemy, e, sizeof(MONSTER));
}

void CHARACTER_MACHINE::CalculateDamage()
{
    WORD DamageMin, DamageMax;

    ITEM *Right = &Equipment[EQUIPMENT_WEAPON_RIGHT];
    ITEM *Left = &Equipment[EQUIPMENT_WEAPON_LEFT];
    ITEM *Amulet = &Equipment[EQUIPMENT_AMULET];
    ITEM *RRing = &Equipment[EQUIPMENT_RING_RIGHT];
    ITEM *LRing = &Equipment[EQUIPMENT_RING_LEFT];

    WORD Strength = Character.Strength + Character.AddStrength;
    WORD Dexterity = Character.Dexterity + Character.AddDexterity;
    WORD Energy = Character.Energy + Character.AddEnergy;
    WORD Vitality = Character.Vitality + Character.AddVitality;

    int CharacterClass = gCharacterManager.GetBaseClass(Character.Class);

    if (((gCharacterManager.GetEquipedBowType(Left) == BOWTYPE_BOW) && (Left->Durability != 0)) ||
        ((gCharacterManager.GetEquipedBowType(Right) == BOWTYPE_CROSSBOW) &&
         (Right->Durability != 0)))
    {
        Character.AttackDamageMinRight = Dexterity / 7 + Strength / 14;
        Character.AttackDamageMaxRight = Dexterity / 4 + Strength / 8;
        Character.AttackDamageMinLeft = Dexterity / 7 + Strength / 14;
        Character.AttackDamageMaxLeft = Dexterity / 4 + Strength / 8;
    }
    else
    {
        switch (CharacterClass)
        {
        case CLASS_ELF:
            Character.AttackDamageMinRight = (Strength + Dexterity) / 7;
            Character.AttackDamageMaxRight = (Strength + Dexterity) / 4;
            Character.AttackDamageMinLeft = (Strength + Dexterity) / 7;
            Character.AttackDamageMaxLeft = (Strength + Dexterity) / 4;
            break;

        case CLASS_KNIGHT: {
            int minValue = 7;
            int maxValue = 4;

            minValue = 6;
            maxValue = 4;

            Character.AttackDamageMinRight = Strength / minValue;
            Character.AttackDamageMaxRight = Strength / maxValue;
            Character.AttackDamageMinLeft = Strength / minValue;
            Character.AttackDamageMaxLeft = Strength / maxValue;
        }
        break;

        case CLASS_DARK: {
            int minValue[2] = {7, 10};
            int maxValue[2] = {4, 5};

            minValue[0] = 6;
            minValue[1] = 12;
            maxValue[0] = 4;
            maxValue[1] = 9;

            Character.AttackDamageMinRight = (Strength / minValue[0]) + Energy / minValue[1];
            Character.AttackDamageMaxRight = (Strength / maxValue[0]) + Energy / maxValue[1];
            Character.AttackDamageMinLeft = (Strength / minValue[0]) + Energy / minValue[1];
            Character.AttackDamageMaxLeft = (Strength / maxValue[0]) + Energy / maxValue[1];
        }
        break;
        case CLASS_DARK_LORD:
            Character.AttackDamageMinRight = Strength / 7 + Energy / 14;
            Character.AttackDamageMaxRight = Strength / 5 + Energy / 10;
            Character.AttackDamageMinLeft = Strength / 7 + Energy / 14;
            Character.AttackDamageMaxLeft = Strength / 5 + Energy / 10;
            break;

        case CLASS_SUMMONER:
            Character.AttackDamageMinRight = (Strength + Dexterity) / 7;
            Character.AttackDamageMaxRight = (Strength + Dexterity) / 4;
            Character.AttackDamageMinLeft = (Strength + Dexterity) / 7;
            Character.AttackDamageMaxLeft = (Strength + Dexterity) / 4;
            break;
        case CLASS_RAGEFIGHTER:
            Character.AttackDamageMinRight = Strength / 7 + Vitality / 15;
            Character.AttackDamageMaxRight = Strength / 5 + Vitality / 12;
            Character.AttackDamageMinLeft = Strength / 7 + Vitality / 15;
            Character.AttackDamageMaxLeft = Strength / 5 + Vitality / 12;
            break;
        default:
            Character.AttackDamageMinRight = Strength / 8;
            Character.AttackDamageMaxRight = Strength / 4;
            Character.AttackDamageMinLeft = Strength / 8;
            Character.AttackDamageMaxLeft = Strength / 4;
            break;
        }
    }

    if (Equipment[EQUIPMENT_WING].Type != -1)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Equipment[EQUIPMENT_WING].Type];

        if (p->Durability != 0)
        {
            float percent = CalcDurabilityPercent(Equipment[EQUIPMENT_WING].Durability,
                                                  p->Durability, Equipment[EQUIPMENT_WING].Level, 0,
                                                  0); //Equipment[EQUIPMENT_WING].Option1);

            DamageMin = 0;
            DamageMax = 0;
            PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, &Equipment[EQUIPMENT_WING]);
            PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, &Equipment[EQUIPMENT_WING]);

            DamageMin = DamageMin - (WORD)(DamageMin * percent);
            DamageMax = DamageMax - (WORD)(DamageMax * percent);

            Character.AttackDamageMinRight += DamageMin;
            Character.AttackDamageMaxRight += DamageMax;
            Character.AttackDamageMinLeft += DamageMin;
            Character.AttackDamageMaxLeft += DamageMax;
        }
    }

    if (Right->Type != -1 && Right->Durability != 0)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Right->Type];
        float percent = CalcDurabilityPercent(Right->Durability, p->Durability, Right->Level,
                                              Right->ExcellentFlags, Right->AncientDiscriminator);

        DamageMin = Right->DamageMin;
        DamageMax = Right->DamageMax;

        PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, Right);
        PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, Right);

        DamageMin = DamageMin - (WORD)(DamageMin * percent);
        DamageMax = DamageMax - (WORD)(DamageMax * percent);

        if (Right->Type >= ITEM_STAFF && Right->Type <= ITEM_STAFF + MAX_ITEM_INDEX)
        {
            Character.AttackDamageMinLeft += (WORD)(DamageMin);
            Character.AttackDamageMaxLeft += (WORD)(DamageMax);
        }
        else
        {
            Character.AttackDamageMinRight += DamageMin;
            Character.AttackDamageMaxRight += DamageMax;
        }

        PlusSpecial(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_LEVEL, Right);
        PlusSpecial(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_LEVEL, Right);
        PlusSpecialPercent(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_PERCENT, Right, 2);
        PlusSpecialPercent(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_PERCENT, Right, 2);
    }

    if (Left->Type != -1 && Left->Durability != 0)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Left->Type];
        float percent = CalcDurabilityPercent(Left->Durability, p->Durability, Left->Level,
                                              Left->ExcellentFlags, Left->AncientDiscriminator);

        DamageMin = Left->DamageMin;
        DamageMax = Left->DamageMax;

        PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, Left);
        PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, Left);

        DamageMin = DamageMin - (WORD)(DamageMin * percent);
        DamageMax = DamageMax - (WORD)(DamageMax * percent);

        if (Left->Type >= ITEM_STAFF && Left->Type <= ITEM_STAFF + MAX_ITEM_INDEX)
        {
            Character.AttackDamageMinLeft += (WORD)(DamageMin);
            Character.AttackDamageMaxLeft += (WORD)(DamageMax);
        }
        else
        {
            Character.AttackDamageMinLeft += DamageMin;
            Character.AttackDamageMaxLeft += DamageMax;
        }

        PlusSpecial(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_LEVEL, Left);
        PlusSpecial(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_LEVEL, Left);
        PlusSpecialPercent(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_PERCENT, Left, 2);
        PlusSpecialPercent(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_PERCENT, Left, 2);
    }

    if (Amulet->Type != -1 && Amulet->Durability != 0)
    {
        PlusSpecial(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_LEVEL, Amulet);
        PlusSpecial(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_LEVEL, Amulet);
        PlusSpecialPercent(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_PERCENT, Amulet, 2);
        PlusSpecialPercent(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_PERCENT, Amulet, 2);

        PlusSpecial(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_LEVEL, Amulet);
        PlusSpecial(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_LEVEL, Amulet);
        PlusSpecialPercent(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_PERCENT, Amulet, 2);
        PlusSpecialPercent(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_PERCENT, Amulet, 2);
    }
    if (RRing->Type != -1 && RRing->Durability != 0)
    {
        PlusSpecialPercent(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_PERCENT, RRing,
                           RRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_PERCENT, RRing,
                           RRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_PERCENT, RRing,
                           RRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_PERCENT, RRing,
                           RRing->SpecialValue[1]);
    }
    if (LRing->Type != -1 && LRing->Durability != 0)
    {
        PlusSpecialPercent(&Character.AttackDamageMinRight, AT_IMPROVE_DAMAGE_PERCENT, LRing,
                           LRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMaxRight, AT_IMPROVE_DAMAGE_PERCENT, LRing,
                           LRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMinLeft, AT_IMPROVE_DAMAGE_PERCENT, LRing,
                           LRing->SpecialValue[1]);
        PlusSpecialPercent(&Character.AttackDamageMaxLeft, AT_IMPROVE_DAMAGE_PERCENT, LRing,
                           LRing->SpecialValue[1]);
    }

    WORD Damage = 0;

    g_csItemOption.PlusSpecial(&Damage, AT_SET_OPTION_IMPROVE_ATTACK_MIN);
    Character.AttackDamageMinLeft += Damage;
    Character.AttackDamageMinRight += Damage;

    Damage = 0;
    g_csItemOption.PlusSpecial(&Damage, AT_SET_OPTION_IMPROVE_ATTACK_MAX);
    Character.AttackDamageMaxLeft += Damage;
    Character.AttackDamageMaxRight += Damage;

    Damage = 0;
    g_csItemOption.PlusSpecialLevel(&Damage, Dexterity, AT_SET_OPTION_IMPROVE_ATTACK_1);
    Character.AttackDamageMinRight += Damage;
    Character.AttackDamageMaxRight += Damage;
    Character.AttackDamageMinLeft += Damage;
    Character.AttackDamageMaxLeft += Damage;

    Damage = 0;
    g_csItemOption.PlusSpecialLevel(&Damage, Strength, AT_SET_OPTION_IMPROVE_ATTACK_2);
    Character.AttackDamageMinRight += Damage;
    Character.AttackDamageMaxRight += Damage;
    Character.AttackDamageMinLeft += Damage;
    Character.AttackDamageMaxLeft += Damage;

    if ((Right->Type >= ITEM_BOW && Right->Type < ITEM_BOW + MAX_ITEM_INDEX) &&
        (Left->Type >= ITEM_BOW && Left->Type < ITEM_BOW + MAX_ITEM_INDEX))
    {
        int LLevel = Left->Level;
        int RLevel = Right->Level;

        if (Left->Type == ITEM_BOLT && LLevel >= 1)
        {
            Character.AttackDamageMinRight +=
                (WORD)(Character.AttackDamageMinRight * ((LLevel * 2 + 1) * 0.01f) + 1);
            Character.AttackDamageMaxRight +=
                (WORD)(Character.AttackDamageMaxRight * ((LLevel * 2 + 1) * 0.01f) + 1);
        }
        else if (Right->Type == ITEM_ARROWS && RLevel >= 1)
        {
            Character.AttackDamageMinLeft +=
                (WORD)(Character.AttackDamageMinLeft * ((RLevel * 2 + 1) * 0.01f) + 1);
            Character.AttackDamageMaxLeft +=
                (WORD)(Character.AttackDamageMaxLeft * ((RLevel * 2 + 1) * 0.01f) + 1);
        }
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_EliteScroll3))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 74);
        Character.AttackDamageMinRight += Item_data.m_byValue1;
        Character.AttackDamageMaxRight += Item_data.m_byValue1;
        Character.AttackDamageMinLeft += Item_data.m_byValue1;
        Character.AttackDamageMaxLeft += Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_Hellowin2))
    {
        ITEM_ADD_OPTION Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_JACK_OLANTERN_WRATH);
        Character.AttackDamageMinRight += Item_data.m_byValue1;
        Character.AttackDamageMaxRight += Item_data.m_byValue1;
        Character.AttackDamageMinLeft += Item_data.m_byValue1;
        Character.AttackDamageMaxLeft += Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_CherryBlossom_Petal))
    {
        const ITEM_ADD_OPTION &Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_CHERRY_BLOSSOM_FLOWER_PETAL);
        Character.AttackDamageMinRight += Item_data.m_byValue1;
        Character.AttackDamageMaxRight += Item_data.m_byValue1;
        Character.AttackDamageMinLeft += Item_data.m_byValue1;
        Character.AttackDamageMaxLeft += Item_data.m_byValue1;
    }
    Character.AttackDamageMinRight += g_SocketItemMgr.m_StatusBonus.m_iAttackDamageMinBonus;
    Character.AttackDamageMaxRight += g_SocketItemMgr.m_StatusBonus.m_iAttackDamageMaxBonus;
    Character.AttackDamageMinLeft += g_SocketItemMgr.m_StatusBonus.m_iAttackDamageMinBonus;
    Character.AttackDamageMaxLeft += g_SocketItemMgr.m_StatusBonus.m_iAttackDamageMaxBonus;
    if (g_isCharacterBuff((&Hero->Object), eBuff_BlessingOfXmax)) //크리스마스의 축복
    {
        int _Temp = 0;
        _Temp = Character.Level / 3 + 45;

        Character.AttackDamageMinRight += _Temp;
        Character.AttackDamageMaxRight += _Temp;
        Character.AttackDamageMinLeft += _Temp;
        Character.AttackDamageMaxLeft += _Temp;
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_StrengthOfSanta)) //산타의 강화
    {
        int _Temp = 30;

        Character.AttackDamageMinRight += _Temp;
        Character.AttackDamageMaxRight += _Temp;
        Character.AttackDamageMinLeft += _Temp;
        Character.AttackDamageMaxLeft += _Temp;
    }
}

void CHARACTER_MACHINE::CalculateCriticalDamage()
{
    Character.CriticalDamage = 0;
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_WEAPON_RIGHT]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_WEAPON_LEFT]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_HELM]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_ARMOR]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_PANTS]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_GLOVES]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_BOOTS]);
    PlusSpecial(&Character.CriticalDamage, AT_LUCK, &Equipment[EQUIPMENT_WING]);
}

void CHARACTER_MACHINE::CalculateMagicDamage()
{
    WORD Energy;
    Energy = Character.Energy + Character.AddEnergy;
    Character.MagicDamageMin = Energy / 9;
    Character.MagicDamageMax = Energy / 4;

    ITEM *Right = &Equipment[EQUIPMENT_WEAPON_RIGHT];
    ITEM *Left = &Equipment[EQUIPMENT_WEAPON_LEFT];
    ITEM *Amulet = &Equipment[EQUIPMENT_AMULET];
    ITEM *RRing = &Equipment[EQUIPMENT_RING_RIGHT];
    ITEM *LRing = &Equipment[EQUIPMENT_RING_LEFT];
    float percent;
    WORD DamageMin = 0;
    WORD DamageMax = 0;

    // 날개
    if (Equipment[EQUIPMENT_WING].Type != -1)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Equipment[EQUIPMENT_WING].Type];
        ITEM *ipWing = &Equipment[EQUIPMENT_WING];
        percent = CalcDurabilityPercent(ipWing->Durability, p->Durability, ipWing->Level,
                                        0); //ipWing->Option1);

        DamageMin = 0;
        DamageMax = 0;
        PlusSpecial(&DamageMin, AT_IMPROVE_MAGIC, &Equipment[EQUIPMENT_WING]);
        PlusSpecial(&DamageMax, AT_IMPROVE_MAGIC, &Equipment[EQUIPMENT_WING]);

        DamageMin = DamageMin - (WORD)(DamageMin * percent);
        DamageMax = DamageMax - (WORD)(DamageMax * percent);

        Character.MagicDamageMin += DamageMin;
        Character.MagicDamageMax += DamageMax;
    }

    if (Right->Type != -1 && Right->Durability != 0)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Right->Type];
        percent = CalcDurabilityPercent(Right->Durability, p->Durability, Right->Level,
                                        Right->ExcellentFlags, Right->AncientDiscriminator);
        DamageMin = 0;
        DamageMax = 0;

        if (Right->Type == ITEM_DARK_REIGN_BLADE || Right->Type == ITEM_RUNE_BLADE ||
            Right->Type == ITEM_EXPLOSION_BLADE || Right->Type == ITEM_SWORD_DANCER ||
            Right->Type == ITEM_IMPERIAL_SWORD)
        {
            PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, Right);
            PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, Right);
        }
        else
        {
            PlusSpecial(&DamageMin, AT_IMPROVE_MAGIC, Right);
            PlusSpecial(&DamageMax, AT_IMPROVE_MAGIC, Right);
        }

        Character.MagicDamageMin += DamageMin - (WORD)(DamageMin * percent);
        Character.MagicDamageMax += DamageMax - (WORD)(DamageMax * percent);

        PlusSpecial(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_LEVEL, Right);
        PlusSpecial(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_LEVEL, Right);
        PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, Right, 2);
        PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, Right, 2);
    }

    if (Left->Type != -1 && Left->Durability != 0)
    {
        // CalculateCurseDamage()
        if (CLASS_SUMMONER != gCharacterManager.GetBaseClass(Character.Class))
        {
            ITEM_ATTRIBUTE *p = &ItemAttribute[Left->Type];
            percent = CalcDurabilityPercent(Left->Durability, p->Durability, Left->Level,
                                            Left->ExcellentFlags, Left->AncientDiscriminator);
            DamageMin = 0;
            DamageMax = 0;

            if (Left->Type >= ITEM_SWORD && Left->Type < ITEM_SHIELD)
            {
                if (Left->Type == ITEM_IMPERIAL_SWORD)
                {
                    PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, Left);
                    PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, Left);
                }
                else
                {
                    PlusSpecial(&DamageMin, AT_IMPROVE_MAGIC, Left);
                    PlusSpecial(&DamageMax, AT_IMPROVE_MAGIC, Left);
                }

                if (gCharacterManager.GetBaseClass(Character.Class) != CLASS_DARK)
                {
                    Character.MagicDamageMin += DamageMin - (WORD)(DamageMin * percent);
                    Character.MagicDamageMax += DamageMax - (WORD)(DamageMax * percent);
                }

                PlusSpecial(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_LEVEL, Left);
                PlusSpecial(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_LEVEL, Left);
                PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, Left, 2);
                PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, Left, 2);
            }
        }
    }

    if (Left->Type != -1 && Left->Durability != 0)
    {
        if (CLASS_SUMMONER != gCharacterManager.GetBaseClass(Character.Class))
        {
            ITEM_ATTRIBUTE *p = &ItemAttribute[Left->Type];
            percent = CalcDurabilityPercent(Left->Durability, p->Durability, Left->Level,
                                            Left->ExcellentFlags, Left->AncientDiscriminator);
            DamageMin = 0;
            DamageMax = 0;

            if (Right->Type == ITEM_IMPERIAL_SWORD)
            {
                PlusSpecial(&DamageMin, AT_IMPROVE_DAMAGE, Right);
                PlusSpecial(&DamageMax, AT_IMPROVE_DAMAGE, Right);
            }
            else
            {
                PlusSpecial(&DamageMin, AT_IMPROVE_MAGIC, Right);
                PlusSpecial(&DamageMax, AT_IMPROVE_MAGIC, Right);
            }

            if (gCharacterManager.GetBaseClass(Character.Class) != CLASS_DARK)
            {
                Character.MagicDamageMin += DamageMin - (WORD)(DamageMin * percent);
                Character.MagicDamageMax += DamageMax - (WORD)(DamageMax * percent);
            }

            PlusSpecial(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_LEVEL, Left);
            PlusSpecial(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_LEVEL, Left);
            PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, Left, 2);
            PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, Left, 2);
        }
    }

    if (Amulet->Type != -1 && Amulet->Durability != 0)
    {
        PlusSpecial(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_LEVEL, Amulet);
        PlusSpecial(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_LEVEL, Amulet);
        PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, Amulet, 2);
        PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, Amulet, 2);
    }

    if (RRing->Type != -1 && RRing->Durability != 0)
    {
        PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, RRing,
                           RRing->SpecialValue[0]);
        PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, RRing,
                           RRing->SpecialValue[0]);
    }
    if (LRing->Type != -1 && LRing->Durability != 0)
    {
        PlusSpecialPercent(&Character.MagicDamageMin, AT_IMPROVE_MAGIC_PERCENT, LRing,
                           LRing->SpecialValue[0]);
        PlusSpecialPercent(&Character.MagicDamageMax, AT_IMPROVE_MAGIC_PERCENT, LRing,
                           LRing->SpecialValue[0]);
    }

    WORD MagicDamage = 0;

    MagicDamage = 0;
    g_csItemOption.PlusSpecialLevel(&MagicDamage, Energy, AT_SET_OPTION_IMPROVE_MAGIC);
    Character.MagicDamageMin += MagicDamage;
    Character.MagicDamageMax += MagicDamage;

    if (g_isCharacterBuff((&Hero->Object), eBuff_EliteScroll4))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 75);
        Character.MagicDamageMin += Item_data.m_byValue1;
        Character.MagicDamageMax += Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_Hellowin2))
    {
        ITEM_ADD_OPTION Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_JACK_OLANTERN_WRATH);
        Character.MagicDamageMin += Item_data.m_byValue1;
        Character.MagicDamageMax += Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_CherryBlossom_Petal))
    {
        const ITEM_ADD_OPTION &Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_CHERRY_BLOSSOM_FLOWER_PETAL);
        Character.MagicDamageMin += Item_data.m_byValue1;
        Character.MagicDamageMax += Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_SwellOfMagicPower))
    {
        int iAdd_MP = (CharacterAttribute->Energy / 9) * 0.2f;
        Character.MagicDamageMin += iAdd_MP;
    }
    Character.MagicDamageMin += g_SocketItemMgr.m_StatusBonus.m_iSkillAttackDamageBonus;
    Character.MagicDamageMax += g_SocketItemMgr.m_StatusBonus.m_iSkillAttackDamageBonus;
    Character.MagicDamageMin += g_SocketItemMgr.m_StatusBonus.m_iMagicPowerBonus;
    Character.MagicDamageMax += g_SocketItemMgr.m_StatusBonus.m_iMagicPowerBonus;

    if (g_isCharacterBuff((&Hero->Object), eBuff_StrengthOfSanta))
    {
        int _Temp = 30;

        Character.MagicDamageMin += _Temp;
        Character.MagicDamageMax += _Temp;
    }
}

void CHARACTER_MACHINE::CalculateCurseDamage()
{
    if (CLASS_SUMMONER != gCharacterManager.GetBaseClass(Character.Class))
        return;

    WORD wEnergy = Character.Energy + Character.AddEnergy;
    Character.CurseDamageMin = wEnergy / 9;
    Character.CurseDamageMax = wEnergy / 4;

    ITEM *pEquipWing = &Equipment[EQUIPMENT_WING];
    if (pEquipWing->Type != -1)
    {
        ITEM_ATTRIBUTE *pAttribute = &ItemAttribute[pEquipWing->Type];
        WORD wDamageMin = 0;
        WORD wDamageMax = 0;

        PlusSpecial(&wDamageMin, AT_IMPROVE_CURSE, pEquipWing);
        PlusSpecial(&wDamageMax, AT_IMPROVE_CURSE, pEquipWing);

        float fPercent = ::CalcDurabilityPercent(pEquipWing->Durability, pAttribute->Durability,
                                                 pEquipWing->Level, 0);
        wDamageMin -= WORD(wDamageMin * fPercent);
        wDamageMax -= WORD(wDamageMax * fPercent);

        Character.CurseDamageMin += wDamageMin;
        Character.CurseDamageMax += wDamageMax;
    }

    ITEM *pEquipLeft = &Equipment[EQUIPMENT_WEAPON_LEFT];
    if (pEquipLeft->Type != -1 && pEquipLeft->Durability != 0)
    {
        ITEM_ATTRIBUTE *pAttribute = &ItemAttribute[pEquipLeft->Type];
        WORD wDamageMin = 0;
        WORD wDamageMax = 0;

        PlusSpecial(&wDamageMin, AT_IMPROVE_CURSE, pEquipLeft);
        PlusSpecial(&wDamageMax, AT_IMPROVE_CURSE, pEquipLeft);

        float fPercent =
            CalcDurabilityPercent(pEquipLeft->Durability, pAttribute->Durability, pEquipLeft->Level,
                                  pEquipLeft->ExcellentFlags, pEquipLeft->AncientDiscriminator);

        Character.CurseDamageMin += wDamageMin - WORD(wDamageMin * fPercent);
        Character.CurseDamageMax += wDamageMax - WORD(wDamageMax * fPercent);

        PlusSpecial(&Character.CurseDamageMin, AT_IMPROVE_MAGIC_LEVEL, pEquipLeft);
        PlusSpecial(&Character.CurseDamageMax, AT_IMPROVE_MAGIC_LEVEL, pEquipLeft);
        PlusSpecialPercent(&Character.CurseDamageMin, AT_IMPROVE_MAGIC_PERCENT, pEquipLeft, 2);
        PlusSpecialPercent(&Character.CurseDamageMax, AT_IMPROVE_MAGIC_PERCENT, pEquipLeft, 2);
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_StrengthOfSanta))
    {
        int _Temp = 30;

        Character.CurseDamageMin += _Temp;
        Character.CurseDamageMax += _Temp;
    }
}

void CHARACTER_MACHINE::CalculateAttackRating()
{
    WORD Strength = Character.Strength + Character.AddStrength;
    WORD Dexterity = Character.Dexterity + Character.AddDexterity;
    WORD Charisma = Character.Charisma + Character.AddCharisma;

    if (gCharacterManager.GetBaseClass(Character.Class) == CLASS_DARK_LORD)
    {
        Character.AttackRating = static_cast<WORD>(((Character.Level * 5) + (Dexterity * 5) / 2) +
                                                       (Strength / 6) + (Charisma / 10) &
                                                   0xFFFF);
    }
    else if (gCharacterManager.GetBaseClass(Character.Class) == CLASS_RAGEFIGHTER)
    {
        Character.AttackRating =
            ((Character.Level * 3) + (Dexterity * 5) / 4) + (Strength / 6) & 0xFFFF;
    }
    else
    {
        Character.AttackRating = static_cast<WORD>(
            (((Character.Level * 5) + (Dexterity * 3) / 2) + (Strength / 4)) & 0xFFFF);
    }

    g_csItemOption.PlusSpecial(&Character.AttackRating, AT_SET_OPTION_IMPROVE_ATTACKING_PERCENT);

    Character.AttackRating += g_SocketItemMgr.m_StatusBonus.m_iAttackRateBonus;
}

void CHARACTER_MACHINE::CalculateAttackRatingPK()
{
    WORD Dexterity;
    Dexterity = Character.Dexterity + Character.AddDexterity;
    int CharacterClass = gCharacterManager.GetBaseClass(Character.Class);

    float tmpf = 0.f;
    switch (CharacterClass)
    {
    case CLASS_KNIGHT:
        tmpf = (float)Character.Level * 3 + (float)Dexterity * 4.5f;
        break;
    case CLASS_DARK_LORD:
        tmpf = (float)Character.Level * 3 + (float)Dexterity * 4.f;
        break;
    case CLASS_ELF:
        tmpf = (float)Character.Level * 3 + (float)Dexterity * 0.6f;
        break;
    case CLASS_DARK:
    case CLASS_SUMMONER:
        tmpf = (float)Character.Level * 3 + (float)Dexterity * 3.5f;
        break;
    case CLASS_WIZARD:
        tmpf = (float)Character.Level * 3 + (float)Dexterity * 4.f;
        break;
    case CLASS_RAGEFIGHTER:
        tmpf = (float)Character.Level * 2.6f + (float)Dexterity * 3.6f;
        break;
    }

    Character.AttackRatingPK = (WORD)tmpf;
}

void CHARACTER_MACHINE::CalculateSuccessfulBlockingPK()
{
    WORD Dexterity;
    Dexterity = Character.Dexterity + Character.AddDexterity;
    int CharacterClass = gCharacterManager.GetBaseClass(Character.Class);

    float tmpf = 0.f;
    switch (CharacterClass)
    {
    case CLASS_KNIGHT:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.5f;
        break;
    case CLASS_DARK_LORD:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.5f;
        break;
    case CLASS_ELF:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.1f;
        break;
    case CLASS_DARK:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.25f;
        break;
    case CLASS_WIZARD:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.25f;
        break;
    case CLASS_SUMMONER:
        tmpf = (float)Character.Level * 2 + (float)Dexterity * 0.5f;
        break;
    case CLASS_RAGEFIGHTER:
        tmpf = (float)Character.Level * 1.5f + (float)Dexterity * 0.2f;
        break;
    }
    Character.SuccessfulBlockingPK = (WORD)tmpf;
}

void CHARACTER_MACHINE::CalculateSuccessfulBlocking()
{
    WORD Dexterity = Character.Dexterity + Character.AddDexterity;

    int CharacterClass = gCharacterManager.GetBaseClass(Character.Class);

    if (CharacterClass == CLASS_ELF || CharacterClass == CLASS_SUMMONER)
    {
        Character.SuccessfulBlocking = Dexterity / 4;
    }
    else if (CharacterClass == CLASS_DARK_LORD)
    {
        Character.SuccessfulBlocking = Dexterity / 7;
    }
    else if (CharacterClass == CLASS_RAGEFIGHTER)
    {
        Character.SuccessfulBlocking = Dexterity / 10;
    }
    else
    {
        Character.SuccessfulBlocking = Dexterity / 3;
    }

    ITEM *Left = &Equipment[EQUIPMENT_WEAPON_LEFT];
    if (Left->Type != -1 && Left->Durability != 0)
    {
        ITEM_ATTRIBUTE *p = &ItemAttribute[Left->Type];
        float percent = CalcDurabilityPercent(Left->Durability, p->Durability, Left->Level,
                                              Left->ExcellentFlags, Left->AncientDiscriminator);

        WORD SuccessBlocking =
            Left->SuccessfulBlocking - (WORD)(Left->SuccessfulBlocking * percent);

        Character.SuccessfulBlocking += SuccessBlocking;

        SuccessBlocking = 0;
        PlusSpecial(&SuccessBlocking, AT_IMPROVE_BLOCKING, Left);

        Character.SuccessfulBlocking += SuccessBlocking - (WORD)(SuccessBlocking * percent);

        PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT, Left, 10);
    }

    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_HELM], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_ARMOR], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_PANTS], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_GLOVES], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_BOOTS], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_RING_LEFT], 10);
    PlusSpecialPercent(&Character.SuccessfulBlocking, AT_IMPROVE_BLOCKING_PERCENT,
                       &Equipment[EQUIPMENT_RING_RIGHT], 10);

    Character.SuccessfulBlocking *= g_SocketItemMgr.m_StatusBonus.m_fDefenceRateBonus;
}

void CHARACTER_MACHINE::CalculateDefense()
{
    WORD Dexterity, Vitality;

    Dexterity = Character.Dexterity + Character.AddDexterity;
    Vitality = Character.Vitality + Character.AddVitality;

    int CharacterClass = gCharacterManager.GetBaseClass(Character.Class);

    if (CharacterClass == CLASS_ELF)
    {
        Character.Defense = Dexterity / 10;
    }
    else if (CharacterClass == CLASS_KNIGHT || CharacterClass == CLASS_SUMMONER)
    {
        Character.Defense = Dexterity / 3;
    }
    else if (CharacterClass == CLASS_WIZARD)
    {
        Character.Defense = Dexterity / 4;
    }
    else if (CharacterClass == CLASS_DARK_LORD)
    {
        Character.Defense = Dexterity / 7;
    }
    else if (CharacterClass == CLASS_RAGEFIGHTER)
    {
        Character.Defense = Dexterity / 8;
    }
    else
    {
        Character.Defense = Dexterity / 5;
    }

    WORD Defense = 0;
    for (int i = EQUIPMENT_WEAPON_LEFT; i <= EQUIPMENT_WING; ++i)
    {
        if (Equipment[i].Durability != 0)
        {
            WORD defense = ItemDefense(&Equipment[i]);

            ITEM_ATTRIBUTE *p = &ItemAttribute[Equipment[i].Type];
            float percent;
            if (i == EQUIPMENT_WING)
            {
                percent = CalcDurabilityPercent(Equipment[i].Durability, p->Durability,
                                                Equipment[i].Level, 0); //Equipment[i].Option1);
            }
            else
            {
                percent = CalcDurabilityPercent(Equipment[i].Durability, p->Durability,
                                                Equipment[i].Level, Equipment[i].ExcellentFlags,
                                                Equipment[i].AncientDiscriminator);
            }

            defense -= (WORD)(defense * percent);

            Defense += defense;
        }
    }
    Character.Defense += Defense;

    if (g_bAddDefense)
    {
        float addDefense = 0.f;
        switch (EquipmentLevelSet)
        {
        case 10:
            addDefense = 0.05f;
            break;
        case 11:
            addDefense = 0.1f;
            break;
        case 12:
            addDefense = 0.15f;
            break;
        case 13:
            addDefense = 0.2f;
            break;
        case 14:
            addDefense = 0.25f;
            break;
        case 15:
            addDefense = 0.3f;
            break;
        }
        Character.Defense += (WORD)(Character.Defense * addDefense);
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_EliteScroll2))
    {
        const ITEM_ADD_OPTION &Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 73);
        Character.Defense += (WORD)Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_Hellowin3))
    {
        ITEM_ADD_OPTION Item_data =
            g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_JACK_OLANTERN_CRY);
        Character.Defense += (WORD)Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_BlessingOfXmax))
    {
        int _Temp = 0;
        _Temp = Character.Level / 5 + 50;

        Character.Defense += _Temp;
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_DefenseOfSanta))
    {
        int _Temp = 100;
        Character.Defense += _Temp;
    }

    g_csItemOption.PlusSpecial(&Character.Defense, AT_SET_OPTION_IMPROVE_DEFENCE);
    g_csItemOption.PlusSpecialLevel(&Character.Defense, Dexterity, AT_SET_OPTION_IMPROVE_DEFENCE_3);
    g_csItemOption.PlusSpecialLevel(&Character.Defense, Vitality, AT_SET_OPTION_IMPROVE_DEFENCE_4);

    if (Equipment[EQUIPMENT_WEAPON_LEFT].Type >= ITEM_SHIELD &&
        Equipment[EQUIPMENT_WEAPON_LEFT].Type < ITEM_SHIELD + MAX_ITEM_INDEX)
    {
        g_csItemOption.PlusSpecialPercent(&Character.Defense, AT_SET_OPTION_IMPROVE_SHIELD_DEFENCE);
    }

    PlusSpecial(&Character.Defense, AT_SET_OPTION_IMPROVE_DEFENCE, &Equipment[EQUIPMENT_HELPER]);
    if (Equipment[EQUIPMENT_WING].Type == ITEM_CAPE_OF_LORD ||
        Equipment[EQUIPMENT_WING].Type == ITEM_CAPE_OF_FIGHTER)
    {
        PlusSpecial(&Character.Defense, AT_SET_OPTION_IMPROVE_DEFENCE, &Equipment[EQUIPMENT_WING]);
    }

    Character.Defense += g_SocketItemMgr.m_StatusBonus.m_iDefenceBonus;

    if (Equipment[EQUIPMENT_WEAPON_LEFT].Type != -1 &&
        Equipment[EQUIPMENT_WEAPON_LEFT].Durability != 0)
    {
        Character.Defense += Equipment[EQUIPMENT_WEAPON_LEFT].Defense *
                             g_SocketItemMgr.m_StatusBonus.m_iShieldDefenceBonus * 0.01f;
    }
}

void CHARACTER_MACHINE::CalculateMagicDefense()
{
    for (int i = EQUIPMENT_HELM; i <= EQUIPMENT_WING; ++i)
    {
        if (Equipment[i].Durability != 0)
        {
            Character.MagicDefense = ItemMagicDefense(&Equipment[i]);
        }
    }
}

void CHARACTER_MACHINE::CalculateWalkSpeed()
{
    if (Equipment[EQUIPMENT_BOOTS].Durability != 0)
    {
        Character.WalkSpeed = ItemWalkSpeed(&Equipment[EQUIPMENT_BOOTS]);
    }

    if (Equipment[EQUIPMENT_WING].Durability != 0)
    {
        Character.WalkSpeed += ItemWalkSpeed(&Equipment[EQUIPMENT_WING]);
    }
}

void CHARACTER_MACHINE::CalculateNextExperince()
{
    const uint64_t characterLevel = static_cast<uint64_t>(Character.Level);

    Character.Experience = Character.NextExperience;
    Character.NextExperience = (9ull + characterLevel) * characterLevel * characterLevel * 10ull;

    if (Character.Level > 255)
    {
        const uint64_t levelOverN = static_cast<uint64_t>(Character.Level - 255);
        Character.NextExperience += (9ull + levelOverN) * levelOverN * levelOverN * 1000ull;
    }
}

void CHARACTER_MACHINE::CalulateMasterLevelNextExperience()
{
    Master_Level_Data.lMasterLevel_Experince = Master_Level_Data.lNext_MasterLevel_Experince;

    int64_t iTotalLevel =
        (int64_t)CharacterAttribute->Level + (int64_t)Master_Level_Data.nMLevel + 1;
    int64_t iTOverLevel = iTotalLevel - 255;
    int64_t iBaseExperience = 0;

    int64_t iData_Master = (((int64_t)9 + (int64_t)iTotalLevel) * (int64_t)iTotalLevel *
                            (int64_t)iTotalLevel * (int64_t)10) +
                           (((int64_t)9 + (int64_t)iTOverLevel) * (int64_t)iTOverLevel *
                            (int64_t)iTOverLevel * (int64_t)1000);

    Master_Level_Data.lNext_MasterLevel_Experince =
        (iData_Master - (int64_t)3892250000) / (int64_t)2;
}

bool CHARACTER_MACHINE::IsZeroDurability()
{
    for (int i = EQUIPMENT_WEAPON_RIGHT; i < MAX_EQUIPMENT; ++i)
    {
        if (Equipment[i].Durability == 0)
        {
            return true;
        }
    }

    return false;
}

void CHARACTER_MACHINE::CalculateBasicState()
{
    if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion1))
    {
        auto Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 78);
        Character.AddStrength += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion2))
    {
        auto Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 79);
        Character.AddDexterity += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion3))
    {
        auto Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 80);
        Character.AddVitality += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion4))
    {
        auto Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 81);
        Character.AddEnergy += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion5))
    {
        auto Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 82);
        Character.AddCharisma += (WORD)Item_data.m_byValue1;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_Hp_up_Ourforces))
    {
        WORD _AddStat = 0;
        if (Character.Energy >= 132)
        {
            _AddStat = (WORD)(30 + (WORD)((Character.Energy - 132) / 10));
        }
        if (_AddStat > 200)
        {
            _AddStat = 200;
        }
        Character.AddVitality += _AddStat;
    }
}

void CHARACTER_MACHINE::getAllAddStateOnlyExValues(int &iAddStrengthExValues,
                                                   int &iAddDexterityExValues,
                                                   int &iAddVitalityExValues,
                                                   int &iAddEnergyExValues,
                                                   int &iAddCharismaExValues)
{
    if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion1))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 78);
        iAddStrengthExValues += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion2))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 79);
        iAddDexterityExValues += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion3))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 80);
        iAddVitalityExValues += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion4))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 81);
        iAddEnergyExValues += (WORD)Item_data.m_byValue1;
    }
    else if (g_isCharacterBuff((&Hero->Object), eBuff_SecretPotion5))
    {
        ITEM_ADD_OPTION Item_data = g_pItemAddOptioninfo->GetItemAddOtioninfo(ITEM_POTION + 82);
        iAddCharismaExValues += (WORD)Item_data.m_byValue1;
    }

    iAddStrengthExValues += g_SocketItemMgr.m_StatusBonus.m_iStrengthBonus;
    iAddDexterityExValues += g_SocketItemMgr.m_StatusBonus.m_iDexterityBonus;
    iAddVitalityExValues += g_SocketItemMgr.m_StatusBonus.m_iVitalityBonus;
    iAddEnergyExValues += g_SocketItemMgr.m_StatusBonus.m_iEnergyBonus;
}

void CHARACTER_MACHINE::CalculateAll()
{
    CalculateBasicState();
    g_csItemOption.CheckItemSetOptions();
    InitAddValue();

    g_SocketItemMgr.CheckSocketSetOption();
    g_SocketItemMgr.CalcSocketStatusBonus();
    CharacterMachine->Character.AddStrength += g_SocketItemMgr.m_StatusBonus.m_iStrengthBonus;
    CharacterMachine->Character.AddDexterity += g_SocketItemMgr.m_StatusBonus.m_iDexterityBonus;
    CharacterMachine->Character.AddVitality += g_SocketItemMgr.m_StatusBonus.m_iVitalityBonus;
    CharacterMachine->Character.AddEnergy += g_SocketItemMgr.m_StatusBonus.m_iEnergyBonus;

    CalculateBasicState();

    WORD wAddStrength = 0, wAddDexterity = 0, wAddEnergy = 0, wAddVitality = 0, wStrengthResult = 0,
         wDexterityResult = 0, wEnergyResult = 0, wVitalityResult = 0, wAddCharisma = 0,
         wCharismaResult = 0;

    g_csItemOption.getAllAddStateOnlyAddValue(&wAddStrength, &wAddDexterity, &wAddEnergy,
                                              &wAddVitality, &wAddCharisma);

    int iAddStrengthByExValues = 0, iAddDexterityByExValues = 0, iAddEnergyByExValues = 0,
        iAddVitalityByExValues = 0, iAddCharismaExValues = 0;

    getAllAddStateOnlyExValues(iAddStrengthByExValues, iAddDexterityByExValues,
                               iAddVitalityByExValues, iAddEnergyByExValues, iAddCharismaExValues);

    wAddStrength += iAddStrengthByExValues;
    wAddDexterity += iAddDexterityByExValues;
    wAddEnergy += iAddEnergyByExValues;
    wAddVitality += iAddVitalityByExValues;

    wStrengthResult = CharacterMachine->Character.Strength + wAddStrength;
    wDexterityResult = CharacterMachine->Character.Dexterity + wAddDexterity;
    wEnergyResult = CharacterMachine->Character.Energy + wAddEnergy;
    wVitalityResult = CharacterMachine->Character.Vitality + wAddVitality;

    wCharismaResult = CharacterMachine->Character.Charisma + wAddCharisma;

    g_csItemOption.getAllAddOptionStatesbyCompare(
        &Character.AddStrength, &Character.AddDexterity, &Character.AddEnergy,
        &Character.AddVitality, &Character.AddCharisma, wStrengthResult, wDexterityResult,
        wEnergyResult, wVitalityResult, wCharismaResult);
    g_csItemOption.CheckItemSetOptions();

    if ((CharacterMachine->Equipment[EQUIPMENT_WING].Type + MODEL_ITEM) == MODEL_CAPE_OF_LORD)
    {
        PlusSpecial(&Character.AddCharisma, AT_SET_OPTION_IMPROVE_CHARISMA,
                    &CharacterMachine->Equipment[EQUIPMENT_WING]);
    }

    CalculateBasicState();
    CalculateDamage();
    CalculateMagicDamage();
    CalculateCurseDamage();
    CalculateAttackRating();
    CalculateSuccessfulBlocking();
    CalculateDefense();
    CalculateMagicDefense();
    CalculateWalkSpeed();
    //CalculateNextExperince();
    CalculateSuccessfulBlockingPK();
    CalculateAttackRatingPK();

    MONSTER_ATTRIBUTE *c = &Enemy.Attribute;
    FinalAttackDamageRight = AttackDamageRight - c->Defense;
    FinalAttackDamageLeft = AttackDamageLeft - c->Defense;
    int EnemyAttackDamage =
        c->AttackDamageMin + rand() % (c->AttackDamageMax - c->AttackDamageMin + 1);
    FinalHitPoint = EnemyAttackDamage - Character.Defense;
    FinalAttackRating = Character.AttackRating - c->SuccessfulBlocking;
    FinalDefenseRating = Character.SuccessfulBlocking - c->AttackRating;

    if (FinalAttackDamageRight < 0)
        FinalAttackDamageRight = 0;
    if (FinalAttackDamageLeft < 0)
        FinalAttackDamageLeft = 0;
    if (FinalHitPoint < 0)
        FinalHitPoint = 0;
    if (FinalAttackRating < 0)
        FinalAttackRating = 0;
    else if (FinalAttackRating > 100)
        FinalAttackRating = 100;
    if (FinalDefenseRating < 0)
        FinalDefenseRating = 0;
    else if (FinalDefenseRating > 100)
        FinalDefenseRating = 100;
    if (rand() % 100 < FinalAttackRating)
        FinalSuccessAttack = true;
    else
        FinalSuccessAttack = false;
    if (rand() % 100 < FinalDefenseRating)
        FinalSuccessDefense = true;
    else
        FinalSuccessDefense = false;

    // Stats recalculated (equipment/items changed), invalidate skill requirements cache
}

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

// Latched when a click opens an NPC conversation while the button is still held.
// The world click handler ignores the held button until it is physically released, so the
// same click can't fall through to ground movement and instantly close the NPC window.
// A fresh press (e.g. deliberately clicking the ground to walk away) still works normally.
// 4 Seconds
void SessionGameplayUnit::SendRequestMagic(int Type, int Key)
{
    if (!IsCanBCSkill(Type))
        return;

    if (Type == 40 || Type == 263 || Type == 261 ||
        abs((int)(GetTickCount() - g_dwLatestMagicTick)) > 300)
    {
        g_dwLatestMagicTick = GetTickCount();
        SocketClient->ToGameServer()->SendTargetedSkill(Type, Key);
        g_ConsoleDebug.Write(MCD_SEND, L"0x19 [SendRequestMagic(%d %d)]", Type, Key);
    }
}

BYTE SessionGameplayUnit::MakeSkillSerialNumber(BYTE *pSerialNumber)
{
    if (pSerialNumber == NULL)
        return 0;

    ++g_byLastSkillSerialNumber;
    if (g_byLastSkillSerialNumber > 50)
        g_byLastSkillSerialNumber = 1;

    *pSerialNumber = g_byLastSkillSerialNumber;
    return g_byLastSkillSerialNumber;
}

void SessionGameplayUnit::SendRequestMagicContinue(int Type, int x, int y, int Angle, BYTE Dest,
                                                   BYTE Tpos, WORD TKey, BYTE *pSkillSerial)
{
    CurrentSkill = Type;

    SocketClient->ToGameServer()->SendAreaSkill(Type, x, y, Angle, TKey,
                                                MakeSkillSerialNumber(pSkillSerial));

    g_ConsoleDebug.Write(MCD_SEND, L"0x1E [SendRequestMagicContinue]");
}

//bool IsWebzenCharacter()
//{
//    const std::wstring character_name = std::wstring(Hero->ID);
//    return character_name.find(L"webzen") >= 0;
//}

bool SessionGameplayUnit::SkillKeyPush(int Skill)
{
    if (Skill == AT_SKILL_NOVA && MouseLButtonPush)
    {
        return true;
    }
    return false;
}

// While the hero slides to a server-set position (the basic weapon skills reposition the
// hero on every cast), MoveHero would freeze all input until the slide ended, which capped
// those skills' auto-attack cadence below the player's attack speed (issue #350). Keep the
// auto-attack re-cast running through the slide; the slide still animates smoothly and only
// manual move/click input stays suppressed. Returns true when the hero is mid-slide, in
// which case MoveHero should stop after this.

bool SessionGameplayUnit::IsCanBCSkill(int Type)
{
    if (Type == 44 || Type == 45 || Type == 46 || Type == 57 || Type == 73 || Type == 74 ||
        Type == AT_SKILL_OCCUPY)
    {
        if (!gMapManager.InBattleCastle() || !IsBattleCastleStart())
        {
            return false;
        }
    }

    return true;
}

bool SessionGameplayUnit::CheckSkillUseCondition(OBJECT *o, int Type)
{
    if (IsCanBCSkill(Type) == false)
    {
        return false;
    }

    BYTE SkillUseType = SkillAttribute[Type].SkillUseType;

    if (SkillUseType == SKILL_USE_TYPE_BRAND && g_isCharacterBuff(o, eBuff_AddSkill) == false)
    {
        return false;
    }

    return true;
}
