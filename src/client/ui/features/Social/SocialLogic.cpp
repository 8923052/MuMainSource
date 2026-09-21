#include "ui/features/Social/SocialLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/ItemData.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ChatServer.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialRender.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

bool TestAlphabeticOrder(const wchar_t *pszText1, const wchar_t *pszText2, BOOL *pbEqual = FALSE)
{
    if (pbEqual != NULL)
        *pbEqual = FALSE;
    int iLength = std::min<int>(wcslen(pszText1), wcslen(pszText2));
    for (int i = 0; i < iLength; ++i)
    {
        if (pszText1[i] == pszText2[i])
            ;
        else if (pszText1[i] > pszText2[i])
            return true;
        else
            return false;
    }
    if (pbEqual != NULL)
        *pbEqual = TRUE;
    return false; // 완전히 동일
}

bool FriendListSortByID(const GUILDLIST_TEXT &lhs, const GUILDLIST_TEXT &rhs)
{
    return TestAlphabeticOrder(lhs.m_szID, rhs.m_szID);
}

bool FriendListSortByServer(const GUILDLIST_TEXT &lhs, const GUILDLIST_TEXT &rhs)
{
    return (lhs.m_Server > rhs.m_Server);
}

bool LetterListSortByRead(const LETTERLIST_TEXT &lhs, const LETTERLIST_TEXT &rhs)
{
    return (lhs.m_bIsRead == TRUE && rhs.m_bIsRead == FALSE);
}

bool LetterListSortByID(const LETTERLIST_TEXT &lhs, const LETTERLIST_TEXT &rhs)
{
    return TestAlphabeticOrder(lhs.m_szID, rhs.m_szID);
}

bool LetterListSortByTime(const LETTERLIST_TEXT &lhs, const LETTERLIST_TEXT &rhs)
{
    BOOL bEqual = FALSE;
    bool bResult = TestAlphabeticOrder(rhs.m_szDate, lhs.m_szDate, &bEqual);
    if (bEqual == TRUE)
        return TestAlphabeticOrder(rhs.m_szTime, lhs.m_szTime);
    else
        return bResult;
}

bool LetterListSortByTitle(const LETTERLIST_TEXT &lhs, const LETTERLIST_TEXT &rhs)
{
    return TestAlphabeticOrder(lhs.m_szText, rhs.m_szText);
}

namespace UI::Modern::PC::Chat
{
namespace
{
bool SameName(std::wstring_view left, std::wstring_view right) noexcept
{
    return left.size() == right.size() && _wcsnicmp(left.data(), right.data(), left.size()) == 0;
}
} // namespace

BlockedChatList::BlockedChatList(std::filesystem::path path, std::size_t maxNameLength)
    : path_(std::move(path)), maxNameLength_(maxNameLength)
{
}

bool BlockedChatList::Load()
{
    names_.clear();
    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open())
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(path_, error);
        return !exists && !error;
    }

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::wstring name = StringUtils::NarrowToWide(line.c_str());
        if (name.empty() || name.size() > maxNameLength_ || Contains(name))
        {
            continue;
        }
        names_.push_back(std::move(name));
    }
    return !file.bad();
}

bool BlockedChatList::Add(std::wstring name)
{
    if (name.empty() || name.size() > maxNameLength_ || Contains(name))
        return false;
    names_.push_back(std::move(name));
    return Save();
}

bool BlockedChatList::Remove(std::wstring_view name)
{
    const auto found =
        std::find_if(names_.begin(), names_.end(),
                     [name](const std::wstring &entry) { return SameName(entry, name); });
    if (found == names_.end())
        return false;
    names_.erase(found);
    return Save();
}

bool BlockedChatList::Contains(std::wstring_view name) const noexcept
{
    return std::any_of(names_.begin(), names_.end(),
                       [name](const std::wstring &entry) { return SameName(entry, name); });
}

bool BlockedChatList::Save() const
{
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    for (const std::wstring &name : names_)
    {
        file << StringUtils::WideToNarrow(name.c_str()) << '\n';
    }
    return file.good();
}
} // namespace UI::Modern::PC::Chat

using namespace SEASON3B;

CPartyManager::CPartyManager(SessionKeeper &keeper) noexcept : SessionLegacyCalls(keeper)
{
}

CPartyManager::~CPartyManager()
{
    Release();
}

bool CPartyManager::Create()
{
    return true;
}

void CPartyManager::Release()
{
}

bool CPartyManager::Update()
{
    return true;
}

void CPartyManager::SearchPartyMember()
{
    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if (o->Type == MODEL_PLAYER && o->Kind == KIND_PLAYER && o->Live && o->Visible &&
            o->Alpha > 0.f && c->Dead == 0)
        {
            for (int j = 0; j < PartyNumber; ++j)
            {
                PARTY_t *p = &Party[j];

                if (p->index != -2)
                    continue;
                if (p->index > -1)
                    continue;

                int length = std::max<int>(wcslen(p->Name), std::max<int>(1, wcslen(c->ID)));

                if (!wcsncmp(p->Name, c->ID, length))
                {
                    p->index = i;
                    break;
                }
            }
        }
    }

    for (int j = 0; j < PartyNumber; ++j)
    {
        PARTY_t *p = &Party[j];

        if (p->index >= 0)
            continue;

        int length = std::max<int>(wcslen(p->Name), std::max<int>(1, wcslen(Hero->ID)));

        if (!wcsncmp(p->Name, Hero->ID, length))
        {
            p->index = -3;
        }
        else
        {
            p->index = -1;
        }
    }
}

bool CPartyManager::IsPartyActive()
{
    return PartyNumber > 1;
}

bool CPartyManager::IsPartyMember(int index)
{
    CHARACTER *c = &CharactersClient[index];
    return IsPartyMemberChar(c);
}

bool CPartyManager::IsPartyMemberChar(CHARACTER *c)
{
    for (int i = 0; i < PartyNumber; ++i)
    {
        PARTY_t *p = &Party[i];

        int length = std::max<int>(1, wcslen(c->ID));
        if (!wcsncmp(p->Name, c->ID, length))
            return true;
    }

    return false;
}

CHARACTER *CPartyManager::GetPartyMemberChar(PARTY_t *pMember)
{
    if (pMember == nullptr || pMember->Name[0] == L'\0')
    {
        return NULL;
    }

    return FindCharacterByID(pMember->Name);
}

CNewUIGuildInfoWindow::CNewUIGuildInfoWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIGuildInfoWindow::~CNewUIGuildInfoWindow()
{
    Release();
}
bool CNewUIGuildInfoWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_GUILDINFO, this);
    Show(false);
    return true;
}
void CNewUIGuildInfoWindow::Release()
{
    panel_.Release();
    content_ = {};
    visible_ = false;
    staged_.reset();
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

CNewUIGuildInfoWindow *CNewUIGuildInfoWindow::GetGuildInfo() const
{
    return const_cast<CNewUIGuildInfoWindow *>(this);
}
int CNewUIGuildInfoWindow::GetGuildMemberIndex(const wchar_t *name)
{
    for (int i = 0; i < g_nGuildMemberCount; ++i)
        if (!wcscmp(GuildList[i].Name, name))
            return i;
    return -1;
}
void CNewUIGuildInfoWindow::Act(Panel::Action action)
{
    if (!content_.enabled[std::size_t(action)] || !g_MessageBox.IsEmpty())
        return;
    if (action == Panel::Action::Leave)
    {
        DeleteIndex = GetGuildMemberIndex(Hero->ID);
        if (DeleteIndex < 0)
            return;
        if (Hero->GuildStatus == G_MASTER)
        {
            if (!wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                        GuildMark[Hero->GuildMarkIndex].UnionName))
                CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGuildOutPerson, SessionOrigin()));
            else
                CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGuildBreakMsgBoxLayout, SessionOrigin()));
        }
        else
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGuildBreakPasswordMsgBoxLayout, SessionOrigin()));
    }
    else if (action == Panel::Action::Appoint || action == Panel::Action::Demote ||
             action == Panel::Action::Kick)
    {
        const auto &member = members_[*selectedMember_];
        DeleteIndex = GetGuildMemberIndex(member.name.c_str());
        if (DeleteIndex < 0)
            return;
        AppointStatus = member.role;
        if (action == Panel::Action::Appoint)
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGuild_ToPerson_PositionLayout, SessionOrigin()));
        else if (action == Panel::Action::Kick)
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CGuildPerson_Get_Out, SessionOrigin()));
        else
        {
            CNewUICommonMessageBox *box = nullptr;
            CreateMessageBox(
                MSGBOX_LAYOUT_CLASS(CGuildPerson_Cancel_Position_MsgBoxLayout, SessionOrigin()),
                &box);
            if (box)
            {
                wchar_t text[256]{};
                mu_swprintf(text, I18N::Game::CharacterS, member.name.c_str());
                box->AddMsg(text);
                box->AddMsg(I18N::Game::WouldYouLikeToCancelTheRanking);
            }
        }
    }
    else if (action == Panel::Action::RemoveAllianceGuild)
    {
        wcscpy(DeleteID, alliance_[*selectedAlliance_].name.c_str());
        CreateMessageBox(MSGBOX_LAYOUT_CLASS(CUnionGuild_Break_MsgBoxLayout, SessionOrigin()));
    }
    else if (action == Panel::Action::LeaveAlliance)
    {
        if (!wcscmp(GuildMark[Hero->GuildMarkIndex].GuildName,
                    GuildMark[Hero->GuildMarkIndex].UnionName))
            CreateMessageBox(MSGBOX_LAYOUT_CLASS(CUnionGuild_Out_MsgBoxLayout, SessionOrigin()));
        else
        {
            SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
                GuildRelationshipType::Alliance, GuildRequestType::Leave, Hero->Key);
            content_.enabled[std::size_t(action)] = false;
            ++revision_;
        }
    }
    else
    {
        g_pNewUISystem->Hide(INTERFACE_GUILDINFO);
        g_pNewUISystem->Show(INTERFACE_COMMAND);
        g_pCommandWindow->BeginTargetCommand(action == Panel::Action::AddRival ? COMMAND_RIVAL
                                                                               : COMMAND_RIVALOFF);
    }
}
void CNewUIGuildInfoWindow::ProcessChanges(const Panel::Changes &changes)
{
    if (changes.close)
    {
        g_pNewUISystem->Hide(INTERFACE_GUILDINFO);
        return;
    }
    if (changes.revision != revision_)
        return;
    if (changes.tab && *changes.tab != tab_)
    {
        tab_ = *changes.tab;
        if (tab_ == Panel::Tab::Members)
            SocketClient->ToGameServer()->SendGuildListRequest();
        if (tab_ == Panel::Tab::Alliance && content_.hasAlliance && !m_bRequestUnionList)
        {
            SocketClient->ToGameServer()->SendRequestAllianceList();
            m_bRequestUnionList = true;
        }
        PlayBuffer(SOUND_CLICK01);
        return;
    }
    if (changes.member)
        selectedMember_ = changes.member;
    if (changes.alliance)
        selectedAlliance_ = changes.alliance;
    if (changes.action)
        Act(*changes.action);
}
bool CNewUIGuildInfoWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
    {
        StageContent();
        ProcessChanges(changes);
        StageContent();
    }
    visible_ = IsVisible();
    return true;
}

bool CNewUIGuildInfoWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIGuildInfoWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_GUILDINFO);
    return false;
}

float CNewUIGuildInfoWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}
void CNewUIGuildInfoWindow::OpenningProcess()
{
    visible_ = true;
    tab_ = Panel::Tab::Members;
    selectedMember_.reset();
    selectedAlliance_.reset();
    staged_.reset();
    SocketClient->ToGameServer()->SendGuildListRequest();
    StageContent();
}
void CNewUIGuildInfoWindow::ClosingProcess()
{
    visible_ = false;
    m_bRequestUnionList = false;
    PlayBuffer(SOUND_CLICK01);
}
void CNewUIGuildInfoWindow::AddGuildNotice(wchar_t *text)
{
    if (!notice_.empty())
        notice_ += L"\n";
    notice_ += text;
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::SetRivalGuildName(wchar_t *name)
{
    rival_ = name;
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::AddGuildMember(GUILD_LIST_t *member)
{
    members_.push_back({member->Name, member->Number, member->Server, member->GuildStatus});
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::GuildClear()
{
    members_.clear();
    selectedMember_.reset();
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::NoticeClear()
{
    notice_.clear();
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::UnionGuildClear()
{
    alliance_.clear();
    selectedAlliance_.reset();
    m_bRequestUnionList = false;
    ++dataRevision_;
}
void CNewUIGuildInfoWindow::AddUnionList(const BYTE *decoded, wchar_t *name, int count)
{
    auto &entry = alliance_.emplace_back();
    entry.name = name;
    entry.count = count;
    std::copy_n(decoded, entry.mark.size(), entry.mark.begin());
    ++dataRevision_;
    m_bRequestUnionList = false;
}
int CNewUIGuildInfoWindow::GetUnionCount()
{
    return int(alliance_.size());
}
void CNewUIGuildInfoWindow::InvalidateGuildMark()
{
    ++dataRevision_;
}

bool CNewUIGuildInfoWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
void SEASON3B::CNewUIGuildInfoWindow::ReceiveGuildRelationShip(
    GuildRelationshipType byRelationShipType, GuildRequestType byRequestType,
    BYTE byTargetUserIndexH, BYTE byTargetUserIndexL)
{
    if (!g_MessageBox.IsEmpty())
    {
        SocketClient->ToGameServer()->SendGuildRelationshipChangeResponse(
            byRelationShipType, byRequestType, 0x00,
            MAKEWORD(byTargetUserIndexL, byTargetUserIndexH));
    }
    else
    {
        m_MessageInfo.s_byRelationShipType = byRelationShipType;
        m_MessageInfo.s_byRelationShipRequestType = byRequestType;
        m_MessageInfo.s_byTargetUserIndexH = byTargetUserIndexH;
        m_MessageInfo.s_byTargetUserIndexL = byTargetUserIndexL;

        int nCharKey =
            MAKEWORD(m_MessageInfo.s_byTargetUserIndexL, m_MessageInfo.s_byTargetUserIndexH);
        int nIndex = FindCharacterIndex(nCharKey);
        if (!CharactersClient.IsValidIndex(nIndex))
            return;
        CHARACTER *pPlayer = &CharactersClient[nIndex];

        wchar_t szText[3][64];
        ZeroMemory(szText, sizeof(szText));

        if (m_MessageInfo.s_byRelationShipType == GuildRelationshipType::Alliance)
        {
            if (m_MessageInfo.s_byRelationShipRequestType == GuildRequestType::Join)
            {
                mu_swprintf(szText[0], I18N::Game::FromSForAGuildAlliance, pPlayer->ID);
                mu_swprintf(szText[1], I18N::Game::ReceivedARegistrationRequest);
                mu_swprintf(szText[2], I18N::Game::Approve);
            }
            else // Break Off
            {
                mu_swprintf(szText[0], I18N::Game::FromSForAGuildAlliance, pPlayer->ID);
                mu_swprintf(szText[1], I18N::Game::ReceivedAWithdrawalRequest);
                mu_swprintf(szText[2], I18N::Game::Approve);
            }
        }
        else if (m_MessageInfo.s_byRelationShipType == GuildRelationshipType::Hostility)
        {
            if (m_MessageInfo.s_byRelationShipRequestType == GuildRequestType::Join)
            {
                mu_swprintf(szText[0], I18N::Game::FromSForAHostileGuild, pPlayer->ID);
                mu_swprintf(szText[1], I18N::Game::ReceivedApprovalRequest);
                mu_swprintf(szText[2], I18N::Game::Approve);
            }
            else
            {
                mu_swprintf(szText[0], I18N::Game::FromSForAHostileGuild, pPlayer->ID);
                mu_swprintf(szText[1], I18N::Game::ReceivedCancellationRequest);
                mu_swprintf(szText[2], I18N::Game::Approve);
            }
        }

        SEASON3B::CNewUICommonMessageBox *pMsgBox = NULL;
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CGuildRelationShipMsgBoxLayout, SessionOrigin()),
            &pMsgBox);
        if (pMsgBox)
        {
            pMsgBox->AddMsg(szText[0]);
            pMsgBox->AddMsg(szText[1]);
            pMsgBox->AddMsg(szText[2]);
        }
    }
}

CNewUIGuildMakeWindow::CNewUIGuildMakeWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), panel_(keeper)
{
}
CNewUIGuildMakeWindow::~CNewUIGuildMakeWindow()
{
    Release();
}
bool CNewUIGuildMakeWindow::Create(CNewUIManager *manager, int, int)
{
    if (!manager)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_NPCGUILDMASTER, this);
    Show(false);
    return true;
}
void CNewUIGuildMakeWindow::Release()
{
    panel_.Release();
    visible_ = false;
    locale_.clear();
    if (!manager_)
        return;
    manager_->RemoveUIObj(this);
    manager_ = nullptr;
}

const POINT &CNewUIGuildMakeWindow::GetPos()
{
    return position_;
}

void CNewUIGuildMakeWindow::OpeningProcess()
{
    visible_ = true;
    ++content_.revision;
    content_.step = Panel::Step::Info;
    content_.name = GuildMark[MARK_EDIT].GuildName;
    std::copy_n(GuildMark[MARK_EDIT].Mark, content_.mark.size(), content_.mark.begin());
    g_GuildCache.PrepareMark(MARK_EDIT);
    content_.color = static_cast<std::uint8_t>(SelectMarkColor);
    locale_.clear();
    StageLabels();
}

void CNewUIGuildMakeWindow::ClosingProcess()
{
    ApplyEdits(panel_.TakeChanges());
    visible_ = false;
    ++content_.revision;
    content_.step = Panel::Step::Info;
    SocketClient->ToGameServer()->SendGuildMasterAnswer(false);
}
bool CNewUIGuildMakeWindow::Validate()
{
    constexpr std::size_t MinimumGuildNameLength = 4;
    if (CheckSpecialText(content_.name.data()))
        CreateOkMessageBox(I18N::Game::CannotUseSymbols);
    else if (content_.name.size() < MinimumGuildNameLength)
        CreateOkMessageBox(I18N::Game::TypeMoreThan4Letters);
    else if (std::none_of(content_.mark.begin(), content_.mark.end(),
                          [](auto color) { return color != 0; }))
        CreateOkMessageBox(I18N::Game::PleaseDrawYourGuildEmblem);
    else
        return true;
    return false;
}
void CNewUIGuildMakeWindow::Advance()
{
    if (content_.step == Panel::Step::Edit)
    {
        if (Validate())
            content_.step = Panel::Step::Review;
        return;
    }
    if (content_.step != Panel::Step::Review)
        return;
    std::array<BYTE, UI::Modern::RmlMuPalette::PixelCount / 2> packed{};
    for (std::size_t i = 0; i < packed.size(); ++i)
        packed[i] = (content_.mark[i * 2] << 4) | content_.mark[i * 2 + 1];
    SocketClient->ToGameServer()->SendGuildCreateRequest(content_.name.c_str(), packed.data(),
                                                         int(packed.size()));
    g_pNewUISystem->Hide(INTERFACE_NPCGUILDMASTER);
}
void CNewUIGuildMakeWindow::ProcessAction(const Panel::Changes &changes)
{
    if (changes.close)
    {
        g_pNewUISystem->Hide(INTERFACE_NPCGUILDMASTER);
        return;
    }
    if (changes.step != content_.step)
        return;
    const auto previousStep = content_.step;
    if (changes.create && content_.step == Panel::Step::Info)
    {
        SocketClient->ToGameServer()->SendGuildMasterAnswer(true);
        content_.step = Panel::Step::Edit;
    }
    else if (changes.previous && content_.step != Panel::Step::Info)
    {
        content_.step =
            content_.step == Panel::Step::Review ? Panel::Step::Edit : Panel::Step::Info;
    }
    else if (changes.next)
        Advance();
    if (previousStep != content_.step)
        ++content_.revision;
    if (changes.create || changes.previous || changes.next)
        PlayBuffer(SOUND_CLICK01);
}
bool CNewUIGuildMakeWindow::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && manager_)
        manager_->BringToFront(this);
    if (IsVisible())
    {
        ApplyEdits(changes);
        ProcessAction(changes);
        StageLabels();
    }
    visible_ = IsVisible();
    return true;
}
bool CNewUIGuildMakeWindow::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
bool CNewUIGuildMakeWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    g_pNewUISystem->Hide(INTERFACE_NPCGUILDMASTER);
    PlayBuffer(SOUND_CLICK01);
    return false;
}

float CNewUIGuildMakeWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

bool CNewUIGuildMakeWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
std::optional<UI::Modern::RmlTextInputArea> CNewUIGuildMakeWindow::ModernTextInputArea() const
{
    return IsVisible() ? panel_.TextInputArea() : std::nullopt;
}

//  UIGuildInfo.cpp

CUIGuildInfo::CUIGuildInfo(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
}
void CUIGuildInfo::Open()
{
    g_pNewUISystem->Show(SEASON3B::INTERFACE_GUILDINFO);
}
bool CUIGuildInfo::IsOpen()
{
    return g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GUILDINFO);
}
void CUIGuildInfo::Close()
{
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_GUILDINFO);
}
void CUIGuildInfo::SetRivalGuildName(wchar_t *name)
{
    g_pGuildInfoWindow->SetRivalGuildName(name);
}
void CUIGuildInfo::AddGuildNotice(wchar_t *text)
{
    g_pGuildInfoWindow->AddGuildNotice(text);
}
void CUIGuildInfo::ClearGuildLog()
{
    g_pGuildInfoWindow->NoticeClear();
}
void CUIGuildInfo::AddMemberList(GUILD_LIST_t *member)
{
    g_pGuildInfoWindow->AddGuildMember(member);
}
void CUIGuildInfo::ClearMemberList()
{
    g_pGuildInfoWindow->GuildClear();
}
void CUIGuildInfo::AddUnionList(BYTE *mark, wchar_t *name, int count)
{
    g_pGuildInfoWindow->AddUnionList(mark, name, count);
}
int CUIGuildInfo::GetUnionCount()
{
    return g_pGuildInfoWindow->GetUnionCount();
}
void CUIGuildInfo::ClearUnionList()
{
    g_pGuildInfoWindow->UnionGuildClear();
}

CUIGuildMaster::CUIGuildMaster(SessionKeeper &keeper) : SessionUiLegacyBindings(keeper)
{
}
void CUIGuildMaster::Open()
{
    g_pNewUISystem->Show(SEASON3B::INTERFACE_NPCGUILDMASTER);
}
bool CUIGuildMaster::IsOpen()
{
    return g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCGUILDMASTER);
}
void CUIGuildMaster::Close()
{
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_NPCGUILDMASTER);
}

// Construction/Destruction

SEASON3B::CNewUIFriendWindow::CNewUIFriendWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_pNewUIMng(NULL), m_pFriendWindowMgr(NULL),
      g_pFriendMenu(keeper.FriendMenuObject()), m_renderer(RendererForConstruction())
{
}

SEASON3B::CNewUIFriendWindow::~CNewUIFriendWindow()
{
    Release();
}

bool SEASON3B::CNewUIFriendWindow::Create(CNewUIManager *pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_FRIEND, this);

    m_pFriendWindowMgr = new CUIWindowMgr(SessionOrigin());
    m_pFriendWindowMgr->Reset();

    GetFriendList()->ClearFriendList();
    GetLetterList()->ClearLetterList();
    GetFriendMenu()->Reset();

    Show(false);

    return true;
}

void SEASON3B::CNewUIFriendWindow::Reset()
{
    m_pFriendWindowMgr->Reset();

    GetFriendList()->ClearFriendList();
    GetLetterList()->ClearLetterList();
    GetFriendMenu()->Reset();
}

void SEASON3B::CNewUIFriendWindow::Release()
{
    SAFE_DELETE(m_pFriendWindowMgr);
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIFriendWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUIFriendWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_FRIEND) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_FRIEND);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUIFriendWindow::Update()
{
    if (m_pFriendWindowMgr != nullptr)
        m_pFriendWindowMgr->ApplyModernUiChanges();
    return true;
}

bool SEASON3B::CNewUIFriendWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_pFriendWindowMgr != nullptr && m_pFriendWindowMgr->ProcessModernUiInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> SEASON3B::CNewUIFriendWindow::ModernTextInputArea()
    const
{
    return m_pFriendWindowMgr != nullptr ? m_pFriendWindowMgr->ModernTextInputArea() : std::nullopt;
}

float SEASON3B::CNewUIFriendWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

CFriendList *SEASON3B::CNewUIFriendWindow::GetFriendList()
{
    return &friendList_;
}
CLetterList *SEASON3B::CNewUIFriendWindow::GetLetterList()
{
    return &letterList_;
}
CUIFriendMenu *SEASON3B::CNewUIFriendWindow::GetFriendMenu()
{
    return &g_pFriendMenu;
}

namespace
{
constexpr unsigned short ChatServerPort = 55980;
}

void RenderColor(float x, float y, float Width, float Height, float Alpha, int Flag);

CUIChatWindow::CUIChatWindow(SessionKeeper &keeper)
    : CUIBaseWindow(keeper), _connection(nullptr), sessionNetwork_(NetworkForConstruction()),
      sessionUi_(UiForConstruction()), m_iShowType(1), m_TextInputBox(keeper),
      m_ChatListBox(keeper), m_PalListBox(keeper), m_InviteButton(keeper),
      m_InvitePalListBox(keeper), m_CloseInviteButton(keeper), m_dwRoomNumber(0),
      m_ModernPanel(keeper, UI::Modern::PC::Friend::RmlFriendPanelMode::Chat)
{
}

CUIChatWindow::~CUIChatWindow()
{
    DisconnectToChatServer();
}

void CUIChatWindow::InitControls()
{
    m_TextInputBox.Init(238, 14, 50);
    m_TextInputBox.SetSize(180, 14);
    m_TextInputBox.SetParentUIID(m_dwUIID);
    m_TextInputBox.SetFont(LegacyFontRole::Normal);

    m_TextInputBox.SetOption(UIOPTION_ENTERIMECHKOFF);
    m_TextInputBox.SetBackColor(0, 0, 0, 0);

    m_TextInputBox.SetParentUIID(GetUIID());
    m_TextInputBox.SetArrangeType(2, 2, 12);
    m_TextInputBox.SetState(UISTATE_NORMAL);
    m_TextInputBox.SetTextLimit(MAX_CHATROOM_TEXT_LENGTH - 1);

    m_InviteButton.Init(1, I18N::Game::Invite);
    m_InviteButton.SetParentUIID(GetUIID());
    m_InviteButton.SetSize(53, 13);
    m_InviteButton.SetArrangeType(3, 54, 14);

    m_CloseInviteButton.Init(2, I18N::Game::Invite);
    m_CloseInviteButton.SetParentUIID(GetUIID());
    m_CloseInviteButton.SetSize(73, 13);
    m_CloseInviteButton.SetArrangeType(3, 74, 14);
    Refresh();
}

void CUIChatWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    memset(m_szLastText, 0, MAX_CHATROOM_TEXT_LENGTH);

    SetTitle(pszTitle);
    SetParentUIID(dwParentID);

    SetPosition(UI::Modern::PC::Friend::RmlFriendInitialX(),
                UI::Modern::PC::Friend::RmlFriendInitialY());
    SetSize(250, 170);
    SetLimitSize(250, 150);

    m_ChatListBox.SetParentUIID(GetUIID());
    m_ChatListBox.SetArrangeType(2, 0, 16);
    m_ChatListBox.SetResizeType(3, 0, -16);

    m_PalListBox.SetParentUIID(GetUIID());
    m_PalListBox.SetArrangeType(3, 75, 16);
    m_PalListBox.SetResizeType(2, 75, -16);

    //	m_PalListBox.AddText(L"이름네자", 1, 1);
    //	m_PalListBox.AddText(L"이름넉자", 1, 1);
    //	m_PalListBox.AddText(L"이름수넷", 1, 1);
    //	m_PalListBox.AddText(L"이름1자", 1, 1);
    //	m_PalListBox.AddText(L"이름2자", 1, 1);
    //	m_PalListBox.AddText(L"이름3넷", 1, 1);
    //	m_PalListBox.AddText(L"이름4자", 1, 1);
    //	m_PalListBox.AddText(L"이름5자", 1, 1);
    //	m_PalListBox.AddText(L"이름6넷", 1, 1);
    //	m_PalListBox.AddText(L"이름7넷", 1, 1);
    //	m_PalListBox.AddText(L"이름8넷", 1, 1);
    //	m_PalListBox.AddText(L"이름9넷", 1, 1);

    m_InvitePalListBox.SetParentUIID(GetUIID());
    m_InvitePalListBox.SetArrangeType(3, 75, 16);
    m_InvitePalListBox.SetResizeType(2, 75, -16);

    m_iPrevWidth = 0;
    m_ModernPanel.Create();
}

bool CUIChatWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

std::optional<UI::Modern::RmlTextInputArea> CUIChatWindow::ModernTextInputArea() const
{
    return m_ModernPanel.TextInputArea();
}

void CUIChatWindow::Refresh()
{
    m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_InviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_CloseInviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
    m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);

    m_bHaveTextBox = TRUE;
    g_dwKeyFocusUIID = m_PalListBox.GetUIID();
}

void CUIChatWindow::ConnectToChatServer(const wchar_t *pszIP, DWORD dwRoomNumber, DWORD dwTicket)
{
    m_dwRoomNumber = dwRoomNumber;

    DisconnectToChatServer();

    g_ErrorReport.Write(L"> Chat server connect: %ls:%u.\r\n", pszIP, ChatServerPort);
    std::unique_ptr<Connection> connection =
        sessionNetwork_.CreateConnection(pszIP, ChatServerPort, true);

    if (!connection->IsConnected())
    {
        g_ErrorReport.Write(L"> Chat server connection failed: %ls:%u.\r\n", pszIP, ChatServerPort);
        return;
    }

    const std::int32_t handle = connection->GetHandle();
    if (!sessionUi_.RegisterChatConnection(handle, GetUIID()) ||
        !sessionNetwork_.PublishConnection(*connection, ConnectionRole::ChatServer))
    {
        sessionUi_.UnregisterChatConnection(handle);
        return;
    }
    _connection = connection.release();
    _connection->ToChatServer()->SendAuthenticateExt(dwRoomNumber, dwTicket);
}

void CUIChatWindow::DisconnectToChatServer()
{
    if (_connection != nullptr)
    {
        Connection *connection = _connection;
        _connection = nullptr;
        const std::int32_t handle = connection->GetHandle();
        if (connection->IsConnected())
        {
            connection->ToChatServer()->SendLeaveChatRoom();
        }
        connection->Close();

        sessionUi_.UnregisterChatConnection(handle);
        delete connection;
    }
}

int CUIChatWindow::AddChatPal(const wchar_t *pszID, BYTE Number, BYTE Server)
{
    BOOL bFind = FALSE;
    for (std::deque<GUILDLIST_TEXT>::iterator iter = m_PalListBox.GetFriendList().begin();
         iter != m_PalListBox.GetFriendList().end(); ++iter)
    {
        wchar_t *n = iter->m_szID;

        if (wcscmp(iter->m_szID, pszID) == 0)
        {
            iter->m_Number = Number;
            iter->m_Server = Server;
            bFind = TRUE;
            break;
        }
    }
    if (bFind == FALSE)
        m_PalListBox.AddText(pszID, Number, Server);

    wchar_t szTitle[128] = {0};
    wcsncpy(szTitle, I18N::Game::Talking, wcslen(I18N::Game::Talking));
    m_PalListBox.MakeTitleText(szTitle);
    SetTitle(szTitle);
    g_pWindowMgr->RefreshMainWndChatRoomList();

    if (m_PalListBox.GetLineNum() >= 2)
    {
        Lock(FALSE);
    }

    if (m_PalListBox.GetLineNum() > 2)
    {
        if (m_iShowType >= 2)
            m_ChatListBox.SetResizeType(3, -80 - 80, -16);
        else
            m_ChatListBox.SetResizeType(3, -80, -16);
        m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    }

    return m_PalListBox.GetLineNum();
}

void CUIChatWindow::RemoveChatPal(const wchar_t *pszID)
{
    if (m_PalListBox.GetLineNum() > 2)
    {
        m_PalListBox.DeleteText(pszID);

        wchar_t szTitle[128] = {0};
        wcsncpy(szTitle, I18N::Game::Talking, wcslen(I18N::Game::Talking));
        m_PalListBox.MakeTitleText(szTitle);
        SetTitle(szTitle);
        g_pWindowMgr->RefreshMainWndChatRoomList();
    }

    if (m_PalListBox.GetLineNum() <= 2)
    {
        if (m_iShowType >= 2)
            m_ChatListBox.SetResizeType(3, -80 - 80, -16);
        else
            m_ChatListBox.SetResizeType(3, 0, -16);
        m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    }
}

void CUIChatWindow::AddChatText(BYTE byIndex, const wchar_t *pszText, int iType, int iColor)
{
    const wchar_t *pszID = m_PalListBox.GetNameByNumber(byIndex);
    m_ChatListBox.AddText((pszID != NULL ? pszID : L""), pszText, iType, iColor);
}

void CUIChatWindow::UpdateInvitePalList()
{
    g_pFriendList->UpdateFriendList(m_InvitePalListBox.GetFriendList(), NULL);

    std::deque<GUILDLIST_TEXT>::iterator iter;

    for (iter = m_PalListBox.GetFriendList().begin(); iter != m_PalListBox.GetFriendList().end();
         ++iter)
    {
        if (wcscmp(iter->m_szID, Hero->ID) != 0)
            m_InvitePalListBox.DeleteText(iter->m_szID);
    }

    for (iter = m_InvitePalListBox.GetFriendList().begin();
         iter != m_InvitePalListBox.GetFriendList().end();)
    {
        if (iter->m_Server >= 253)
        {
            m_InvitePalListBox.DeleteText(iter->m_szID);
            iter = m_InvitePalListBox.GetFriendList().begin();
        }
        else
        {
            ++iter;
        }
    }
    m_InvitePalListBox.Scrolling(0);
}

BOOL CUIChatWindow::HandleMessage()
{
    if (m_WorkMessage.m_iMessage == UI_MESSAGE_LISTDBLCLICK)
    {
        if (m_WorkMessage.m_iParam1 == (int)(m_InvitePalListBox.GetUIID()))
        {
            PlayBuffer(SOUND_CLICK01);
            m_WorkMessage.m_iMessage = UI_MESSAGE_BTNLCLICK;
            m_WorkMessage.m_iParam1 = 2;
        }
    }
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        m_TextInputBox.GiveFocus();
        g_dwKeyFocusUIID = m_PalListBox.GetUIID();
        break;
    case UI_MESSAGE_TEXTINPUT: {
        wchar_t pszText[MAX_CHATROOM_TEXT_LENGTH] = {};

        m_TextInputBox.GetText(pszText, MAX_CHATROOM_TEXT_LENGTH);
        //if (CheckAbuseFilter(pszText, false))
        //{
        //    wcsncpy(pszText, I18N::Game::PwnedByTheFilter, sizeof pszText);
        //}

        if (wcsncmp(m_szLastText, pszText, MAX_CHATROOM_TEXT_LENGTH) != 0)
        {
            wcsncpy(m_szLastText, pszText, MAX_CHATROOM_TEXT_LENGTH);

            if (pszText[0] != L'\0')
            {
                int iSize = wcslen(pszText);
                if (_connection != nullptr)
                {
                    _connection->ToChatServer()->SendChatMessageExt(0, pszText);
                }
            }
        }

        m_TextInputBox.SetText(NULL);
        if (m_PalListBox.GetLineNum() < 2)
        {
            Lock(TRUE);
        }
    }
    break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;
        DWORD dwUIID = 0;
        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            if (m_iShowType == 1)
            {
                m_iShowType = 2;
                SetSize(GetWidth() + 80, GetHeight());
                SetLimitSize(250 + 80, 150, 540 / g_fScreenRate_x + 80);
                m_ChatListBox.SetResizeType(3, -80 - 80, -16);
                m_PalListBox.SetArrangeType(3, 75 + 80, 16);
                m_InviteButton.SetArrangeType(3, 54 + 80, 14);
                m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
                m_CloseInviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                m_InviteButton.SetCaption(I18N::Game::CloseInvitation);

                UpdateInvitePalList();

                if (m_iPos_x + m_iWidth > REFERENCE_WIDTH)
                    m_iPos_x = REFERENCE_WIDTH - m_iWidth;
                Refresh();
            }
            else if (m_iShowType >= 2)
            {
                m_iShowType = 1;
                SetSize(GetWidth() - 80, GetHeight());
                SetLimitSize(250, 150, 540 / g_fScreenRate_x);
                if (m_PalListBox.GetLineNum() > 2)
                    m_ChatListBox.SetResizeType(3, -80, -16);
                else
                    m_ChatListBox.SetResizeType(3, 0, -16);
                m_PalListBox.SetArrangeType(3, 75, 16);
                m_InviteButton.SetArrangeType(3, 54, 14);
                m_ChatListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
                m_CloseInviteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                m_InvitePalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                m_InviteButton.SetCaption(I18N::Game::Invite);
            }
            break;
        case 2:
            if (m_TextInputBox.IsLocked() == FALSE && m_InvitePalListBox.GetSelectedText() != NULL)
            {
                if (m_PalListBox.GetLineNum() <= 1)
                    ;
                else if (m_PalListBox.GetLineNum() >= 30)
                {
                    AddChatText(255, I18N::Game::YouHaveReachedTheMaximumNumberOfFriendsYouCanList,
                                1, 0);
                }
                else
                {
                    SocketClient->ToGameServer()->SendChatRoomInvitationRequest(
                        m_InvitePalListBox.GetSelectedText()->m_szID, m_dwRoomNumber, GetUIID());
                }
            }
            break;
        default:
            break;
        }
    }
    break;
    default:
        break;
    }
    return FALSE;
}

void CUIChatWindow::DoActionSub(BOOL bMessageOnly)
{
    if (m_iPrevWidth != 0 && m_iPrevWidth != m_iWidth)
    {
        int iNewWidth = RWidth() - m_InviteButton.GetWidth() - 7;
        if (m_iShowType == 2)
            iNewWidth -= m_CloseInviteButton.GetWidth() + 7;
        m_TextInputBox.SetSize(iNewWidth, 14);
    }
    m_iPrevWidth = m_iWidth;

    m_InviteButton.DoAction(bMessageOnly);
    m_ChatListBox.DoAction(bMessageOnly);
    m_PalListBox.DoAction(bMessageOnly);
    m_InvitePalListBox.DoAction(bMessageOnly);
    m_TextInputBox.DoAction(bMessageOnly);
    m_CloseInviteButton.DoAction(bMessageOnly);
}

void CUIChatWindow::DoMouseActionSub()
{
    //	if (g_dwMouseUseUIID == GetUIID() && MouseLButton == true)
    //	{
    //		m_TextInputBox.GiveFocus();
    //	}
}

const wchar_t *CUIChatWindow::GetChatFriend(int *piResult)
{
    if (m_PalListBox.GetFriendList().size() > 2)
    {
        if (piResult != NULL)
            *piResult = 2;
        return NULL;
    }
    else
    {
        std::deque<GUILDLIST_TEXT> &pPalList = m_PalListBox.GetFriendList();
        for (std::deque<GUILDLIST_TEXT>::iterator PalListIter = pPalList.begin();
             PalListIter != pPalList.end(); ++PalListIter)
        {
            if (wcsncmp(PalListIter->m_szID, Hero->ID, MAX_USERNAME_SIZE) != 0)
            {
                if (piResult != NULL)
                    *piResult = 1;
                return PalListIter->m_szID;
            }
        }
        if (piResult != NULL)
            *piResult = 0;
        return NULL;
    }
}

void CUIChatWindow::Lock(BOOL bFlag)
{
    if (bFlag == TRUE)
    {
        m_TextInputBox.Lock(TRUE);
        wchar_t szTitle[128] = {0};
        if (wcsncmp(GetTitle(), I18N::Game::Offline, wcslen(I18N::Game::Offline)) != 0)
        {
            wcsncpy(szTitle, I18N::Game::Offline, wcslen(I18N::Game::Offline));
        }
        wcsncat(szTitle, GetTitle(), 128);
        SetTitle(szTitle);
    }
    else
    {
        m_TextInputBox.Lock(FALSE);
        if (wcsncmp(GetTitle(), I18N::Game::Offline, wcslen(I18N::Game::Offline)) == 0)
        {
            wchar_t szTitle[128] = {0};
            wcsncpy(szTitle, GetTitle() + wcslen(I18N::Game::Offline), 128);
            SetTitle(szTitle);
        }
    }
}

extern void MoveCharacter(CHARACTER *c, OBJECT *o);

void CFriendList::AddFriend(const wchar_t *pszID, BYTE Number, BYTE Server)
{
    GUILDLIST_TEXT FriendData{};
    wcsncpy(FriendData.m_szID, pszID, MAX_USERNAME_SIZE);
    FriendData.m_szID[MAX_USERNAME_SIZE] = '\0';
    FriendData.m_Number = Number;
    FriendData.m_Server = Server;

    m_FriendList.insert(m_FriendList.end(), FriendData);
}

void CFriendList::RemoveFriend(const wchar_t *pszID)
{
    for (m_FriendListIter = m_FriendList.begin(); m_FriendListIter != m_FriendList.end();
         ++m_FriendListIter)
    {
        if (wcsncmp(m_FriendListIter->m_szID, pszID, MAX_USERNAME_SIZE) == 0)
        {
            m_FriendList.erase(m_FriendListIter);
            break;
        }
    }
}

void CFriendList::ClearFriendList()
{
    m_FriendList.clear();
    m_FriendListIter = m_FriendList.begin();
}

int CFriendList::UpdateFriendList(std::deque<GUILDLIST_TEXT> &pDestData, const wchar_t *pszID)
{
    pDestData.clear();
    int i = 1, iResult = 0;
    for (m_FriendListIter = m_FriendList.begin(); m_FriendListIter != m_FriendList.end();
         ++m_FriendListIter, ++i)
    {
        pDestData.push_back(*m_FriendListIter);
        if (pszID != NULL && wcsncmp(m_FriendListIter->m_szID, pszID, MAX_USERNAME_SIZE) == 0)
            iResult = i;
    }
    return iResult;
}

void CFriendList::UpdateFriendState(const wchar_t *pszID, BYTE Number, BYTE Server)
{
    for (m_FriendListIter = m_FriendList.begin(); m_FriendListIter != m_FriendList.end();
         ++m_FriendListIter)
    {
        if (wcsncmp(m_FriendListIter->m_szID, pszID, MAX_USERNAME_SIZE) == 0)
        {
            m_FriendListIter->m_Number = Number;
            m_FriendListIter->m_Server = Server;
            break;
        }
    }
}

void CFriendList::UpdateAllFriendState(BYTE Number, BYTE Server)
{
    for (m_FriendListIter = m_FriendList.begin(); m_FriendListIter != m_FriendList.end();
         ++m_FriendListIter)
    {
        m_FriendListIter->m_Number = Number;
        m_FriendListIter->m_Server = Server;
    }
}

void CFriendList::Sort(int iType)
{
    if (iType != -1)
        m_iCurrentSortType = iType;
    switch (m_iCurrentSortType)
    {
    case 0:
        sort(m_FriendList.begin(), m_FriendList.end(), FriendListSortByID);
        break;
    case 1:
        sort(m_FriendList.begin(), m_FriendList.end(), FriendListSortByServer);
        break;
    default:
        return;
        break;
    }
}

void CUIFriendListTabWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);
    SetOption(UIWINDOWSTYLE_NULL);

    SetPosition(50, 50);
    SetSize(250, 170);
    SetLimitSize(250, 150);

    RefreshPalList();

    m_PalListBox.SetParentUIID(GetUIID());
    m_PalListBox.SetArrangeType(2, 0, 22);
    m_PalListBox.SetResizeType(3, 0, -39);
    m_PalListBox.SetLayout(1);

    m_AddFriendButton.Init(1, I18N::Game::AddFriend);
    m_AddFriendButton.SetParentUIID(GetUIID());
    m_AddFriendButton.SetArrangeType(2, 2, 17);
    m_AddFriendButton.SetSize(50, 14);

    m_DelFriendButton.Init(2, I18N::Game::DeleteFriend);
    m_DelFriendButton.SetParentUIID(GetUIID());
    m_DelFriendButton.SetArrangeType(2, 53, 17);
    m_DelFriendButton.SetSize(50, 14);

    m_TalkButton.Init(3, I18N::Game::Chat);
    m_TalkButton.SetParentUIID(GetUIID());
    m_TalkButton.SetArrangeType(2, 104, 17);
    m_TalkButton.SetSize(50, 14);

    m_LetterButton.Init(4, I18N::Game::Write);
    m_LetterButton.SetParentUIID(GetUIID());
    m_LetterButton.SetArrangeType(2, 155, 17);
    m_LetterButton.SetSize(50, 14);
}

void CUIFriendListTabWindow::Refresh()
{
    m_AddFriendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_DelFriendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_TalkButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_LetterButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_PalListBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
}

const wchar_t *CUIFriendListTabWindow::GetCurrentSelectedFriend(BYTE *pNumber, BYTE *pServer)
{
    if (m_PalListBox.GetSelectedText() == NULL)
        return NULL;
    else
    {
        if (pNumber != NULL)
            *pNumber = m_PalListBox.GetSelectedText()->m_Number;
        if (pServer != NULL)
            *pServer = m_PalListBox.GetSelectedText()->m_Server;
        return m_PalListBox.GetSelectedText()->m_szID;
    }
}

void CUIFriendListTabWindow::AddReturnedFriend()
{
    const std::wstring text = std::move(m_WorkMessage.m_Text);
    if (text.empty())
        return;
    SocketClient->ToGameServer()->SendFriendAddRequest(text.c_str());
}

BOOL CUIFriendListTabWindow::HandleMessage()
{
    if (m_WorkMessage.m_iMessage == UI_MESSAGE_LISTDBLCLICK)
    {
        PlayBuffer(SOUND_CLICK01);
        m_WorkMessage.m_iMessage = UI_MESSAGE_BTNLCLICK;
        m_WorkMessage.m_iParam1 = 3;
    }

    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;
        DWORD dwUIID = 0;
        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            dwUIID =
                g_pWindowMgr->AddWindow(UIWNDTYPE_TEXTINPUT, UIWND_DEFAULT, UIWND_DEFAULT,
                                        I18N::Game::EnterTheIDOfTheFriendYouDLikeToAdd, GetUIID());
            g_pWindowMgr->SetAddFriendWindow(dwUIID);
            break;
        case 2: {
            if (GetCurrentSelectedFriend() == NULL)
                break;
            wchar_t tempTxt[MAX_TEXT_LENGTH + 1] = {0};
            mu_swprintf(tempTxt, L"%ls %ls", I18N::Game::DoYouReallyWishToDeleteThisFriend,
                        GetCurrentSelectedFriend()); // "Do you really wish to delete this friend?"
            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                             tempTxt, GetUIID());
        }
        break;
        case 3: {
            if (GetCurrentSelectedFriend() == NULL)
                break;
            wchar_t pszName[MAX_USERNAME_SIZE] = {0};
            BYTE Server;
            wcsncpy(pszName, GetCurrentSelectedFriend(NULL, &Server), MAX_USERNAME_SIZE);
            if (Server <= 0xFC)
            {
                DWORD dwDuplicationCheck =
                    SessionOrigin().FriendMenuObject().CheckChatRoomDuplication(pszName);
                if (dwDuplicationCheck == 0)
                {
                    if (g_pWindowMgr->GetChatReject() == FALSE &&
                        SessionOrigin().FriendMenuObject().IsRequestWindow(pszName) == FALSE)
                    {
                        SessionOrigin().FriendMenuObject().AddRequestWindow(pszName);
                        SocketClient->ToGameServer()->SendChatRoomCreateRequest(pszName);
                    }
                }
                else if (dwDuplicationCheck == -1)
                    ;
                else
                {
                    g_pWindowMgr->GetWindow(dwDuplicationCheck)->SetState(UISTATE_HIDE);
                    g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, dwDuplicationCheck, 0);
                }
            }
        }
        break;
        case 4: // 편지쓰기
        {
            wchar_t temp[MAX_TEXT_LENGTH + 1];
            mu_swprintf(temp, I18N::Game::WriteLetterCostDZen, g_cdwLetterCost);
            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_WRITELETTER, 100, 100, temp); // "편지쓰기"
            if (dwUIID == 0)
                break;
            if (GetCurrentSelectedFriend() != NULL)
                ((CUILetterWriteWindow *)g_pWindowMgr->GetWindow(dwUIID))
                    ->SetMailtoText((const wchar_t *)GetCurrentSelectedFriend());
        }
        break;
        default:
            break;
        }
        if (dwUIID != 0)
        {
            CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(dwUIID);
            if (pWindow != NULL)
            {
                pWindow->SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                pWindow->SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            }
        }
    }
    break;
    case UI_MESSAGE_TXTRETURN:
        AddReturnedFriend();
        break;
    case UI_MESSAGE_YNRETURN:
        if (m_WorkMessage.m_iParam2 == 1)
        {
            if (GetCurrentSelectedFriend() == NULL)
                break;
            SocketClient->ToGameServer()->SendFriendDelete(GetCurrentSelectedFriend());
        }
        break;
    default:
        break;
    }
    return FALSE;
}

void CUIFriendListTabWindow::DoActionSub(BOOL bMessageOnly)
{
    m_PalListBox.DoAction(bMessageOnly);

    m_AddFriendButton.DoAction(bMessageOnly);
    m_DelFriendButton.DoAction(bMessageOnly);
    m_TalkButton.DoAction(bMessageOnly);
    m_LetterButton.DoAction(bMessageOnly);
}

void CUIFriendListTabWindow::DoMouseActionSub()
{
    if (MouseLButton)
    {
        if (CheckMouseIn(RPos_x(0) + m_PalListBox.GetColumnPos_x(0), RPos_y(0),
                         m_PalListBox.GetColumnWidth(0), 19) == TRUE)
        {
            if (g_pFriendList->GetCurrentSortType() != 0)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pFriendList->Sort(0);
                RefreshPalList();
            }
            MouseLButton = FALSE;
        }
        else if (CheckMouseIn(RPos_x(0) + m_PalListBox.GetColumnPos_x(1), RPos_y(0),
                              m_PalListBox.GetColumnWidth(1), 19) == TRUE)
        {
            if (g_pFriendList->GetCurrentSortType() != 1)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pFriendList->Sort(0);
                g_pFriendList->Sort(1);
                RefreshPalList();
            }
            MouseLButton = FALSE;
        }
    }
}

void CUIFriendListTabWindow::RefreshPalList()
{
    wchar_t szID[MAX_USERNAME_SIZE + 1] = {0};
    if (m_PalListBox.SLGetSelectLineNum() > 0)
        wcsncpy(szID, m_PalListBox.SLGetSelectLine()->m_szID, MAX_USERNAME_SIZE);
    int iSelectNum =
        g_pFriendList->UpdateFriendList(m_PalListBox.GetFriendList(), (wchar_t *)&szID);
    m_PalListBox.SLSetSelectLine(iSelectNum);
    m_PalListBox.Scrolling(0);
}

void CUIFriendListTabWindow::SortModern(int column)
{
    if (column < 0 || column > 1 || g_pFriendList->GetCurrentSortType() == column)
    {
        return;
    }
    PlayBuffer(SOUND_CLICK01);
    g_pFriendList->Sort(column);
    RefreshPalList();
}

/*
void CChatRoomSocketList::ProcessSocketMessage(DWORD dwSocketID, WORD wMessage)
{
    CHATROOM_SOCKET * pChatroomSocket = GetChatRoomSocketData(GetChatRoomSocketID(dwSocketID));
    if (pChatroomSocket == NULL) return;
    Connection* pSocketClient = &pChatroomSocket->m_WSClient;

    if (pSocketClient == NULL)
    {
        return;
    }
    switch(wMessage)
    {
    case FD_CONNECT:
        break;
    case FD_READ :
        // pSocketClient->nRecv();
        break;
    case FD_WRITE :
        // pSocketClient->FDWriteSend();
        break;
    case FD_CLOSE :
        CUIChatWindow * pWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(pChatroomSocket->m_dwWindowUIID);
        if (pWindow != NULL)
            pWindow->AddChatText(255, I18N::Game::YouAreDisconnectedFromTheServer, 1, 0);
        pSocketClient->Close();
        break;
    }
}

//void CChatRoomSocketList::ProtocolCompile()
//{
//	// TODO: Change that
//	for (m_ChatRoomSocketMapIter = m_ChatRoomSocketMap.begin(); m_ChatRoomSocketMapIter != m_ChatRoomSocketMap.end(); ++m_ChatRoomSocketMapIter)
//	{
//		ProtocolCompiler(&m_ChatRoomSocketMapIter->second->m_WSClient, 1, m_ChatRoomSocketMapIter->second->m_dwWindowUIID);
//	}
//}
*/

void CUIChatRoomListTabWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);
    SetOption(UIWINDOWSTYLE_NULL);

    SetPosition(50, 50);
    //SetSize(213, 170);
    SetSize(250, 170);
    SetLimitSize(250, 150);

    m_WindowListBox.SetParentUIID(GetUIID());
    m_WindowListBox.SetArrangeType(2, 0, 22);
    m_WindowListBox.SetResizeType(3, 0, -39);

    m_HideAllButton.Init(1, I18N::Game::HideAll);
    m_HideAllButton.SetParentUIID(GetUIID());
    m_HideAllButton.SetArrangeType(2, 2, 17);
    m_HideAllButton.SetSize(50, 14);
}

void CUIChatRoomListTabWindow::Refresh()
{
    m_HideAllButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_WindowListBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
}

void CUIChatRoomListTabWindow::AddWindow(DWORD dwUIID, const wchar_t *pszTitle)
{
    m_WindowListBox.AddText(dwUIID, pszTitle);
}

void CUIChatRoomListTabWindow::RemoveWindow(DWORD dwUIID)
{
    m_WindowListBox.DeleteText(dwUIID);
    m_WindowListBox.Scrolling(0);
}

DWORD CUIChatRoomListTabWindow::GetCurrentSelectedWindow()
{
    if (m_WindowListBox.GetSelectedText() == nullptr)
        return 0;
    else
        return m_WindowListBox.GetSelectedText()->m_dwUIID;
}

BOOL CUIChatRoomListTabWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        break;
    case UI_MESSAGE_LISTDBLCLICK:
        if (g_pWindowMgr->GetWindow(GetCurrentSelectedWindow())->GetState() == UISTATE_HIDE ||
            g_pWindowMgr->GetTopNotMainWindowUIID() != GetCurrentSelectedWindow())
        {
            PlayBuffer(SOUND_CLICK01);
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, GetCurrentSelectedWindow(), 0);
            g_pWindowMgr->HideAllWindowClear();
            MouseLButton = false;
        }
        else
        {
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, GetCurrentSelectedWindow(), 0);
        }
        break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;
        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            g_pWindowMgr->HideAllWindow(TRUE);
            break;
        default:
            break;
        }
    }
    default:
        break;
    }
    return FALSE;
}

void CUIChatRoomListTabWindow::DoActionSub(BOOL bMessageOnly)
{
    m_WindowListBox.DoAction(bMessageOnly);
    m_HideAllButton.DoAction(bMessageOnly);
}

void CUIChatRoomListTabWindow::DoMouseActionSub()
{
}

void CLetterList::AddLetter(DWORD dwLetterID, const wchar_t *pszID, const wchar_t *pszText,
                            const wchar_t *pszDate, const wchar_t *pszTime, BOOL bIsRead)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
        {
            return;
        }
    }

    LETTERLIST_TEXT text{};
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE);
    text.m_szID[MAX_USERNAME_SIZE] = '\0';
    wcsncpy(text.m_szText, pszText, 32);
    text.m_szText[32] = '\0';
    wcsncpy(text.m_szDate, pszDate, 16);
    wcsncpy(text.m_szTime, pszTime, 16);
    text.m_bIsRead = bIsRead;
    text.m_dwLetterID = dwLetterID;

    //m_LetterList.insert(m_LetterList.end(), text);
    m_LetterList.push_back(text);
}

void CLetterList::RemoveLetter(DWORD dwLetterID)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
        {
            m_LetterList.erase(m_LetterListIter);
            break;
        }
    }
}

void CLetterList::ClearLetterList()
{
    m_LetterList.clear();
    m_LetterListIter = m_LetterList.begin();
    ClearLetterTextCache();
}

int CLetterList::UpdateLetterList(std::deque<LETTERLIST_TEXT> &pDestData, DWORD dwSelectLineNum)
{
    pDestData.clear();
    int i = 1, iResult = 0;
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter, ++i)
    {
        pDestData.push_back(*m_LetterListIter);
        if (m_LetterListIter->m_dwLetterID == dwSelectLineNum)
            iResult = i;
    }
    return iResult;
}

void CLetterList::Sort(int iType)
{
    if (iType != -1)
        m_iCurrentSortType = iType;
    switch (m_iCurrentSortType)
    {
    case 0:
        sort(m_LetterList.begin(), m_LetterList.end(), LetterListSortByRead);
        break;
    case 1:
        sort(m_LetterList.begin(), m_LetterList.end(), LetterListSortByID);
        break;
    case 2:
        sort(m_LetterList.begin(), m_LetterList.end(), LetterListSortByTime);
        break;
    case 3:
        sort(m_LetterList.begin(), m_LetterList.end(), LetterListSortByTitle);
        break;
    default:
        return;
        break;
    }
}

DWORD CLetterList::GetPrevLetterID(DWORD dwLetterID)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
            break;
    }
    if (m_LetterListIter == m_LetterList.end())
        return 0;
    ++m_LetterListIter;
    if (m_LetterListIter == m_LetterList.end())
        return 0;
    return m_LetterListIter->m_dwLetterID;
}

DWORD CLetterList::GetNextLetterID(DWORD dwLetterID)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
            break;
    }
    if (m_LetterListIter == m_LetterList.end())
        return 0;
    if (m_LetterListIter == m_LetterList.begin())
        return 0;
    --m_LetterListIter;
    return m_LetterListIter->m_dwLetterID;
}

LETTERLIST_TEXT *CLetterList::GetLetter(DWORD dwLetterID)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
            break;
    }
    if (m_LetterListIter == m_LetterList.end())
        return NULL;
    return &(*m_LetterListIter);
}

void CLetterList::ResetLetterSelect(BOOL bFlag)
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        m_LetterListIter->m_bIsSelected = bFlag;
    }
}

BOOL CLetterList::CheckNoReadLetter()
{
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        if (m_LetterListIter->m_bIsRead == FALSE)
            return TRUE;
    }
    return FALSE;
}

void CLetterList::CacheLetterText(DWORD dwIndex, LPFS_LETTER_TEXT pLetterText)
{
    m_LetterCache.insert(std::pair<DWORD, FS_LETTER_TEXT>(dwIndex, *pLetterText));
}

LPFS_LETTER_TEXT CLetterList::GetLetterText(DWORD dwIndex)
{
    m_LetterCacheIter = m_LetterCache.find(dwIndex);
    if (m_LetterCacheIter == m_LetterCache.end())
        return NULL;
    return &m_LetterCacheIter->second;
}

void CLetterList::RemoveLetterTextCache(DWORD dwIndex)
{
    m_LetterCacheIter = m_LetterCache.find(dwIndex);
    if (m_LetterCacheIter != m_LetterCache.end())
    {
        m_LetterCache.erase(m_LetterCacheIter);
    }
}

void CLetterList::ClearLetterTextCache()
{
    m_LetterCache.clear();
}

int CLetterList::GetLineNum(DWORD dwLetterID)
{
    int iCount = 0;
    for (m_LetterListIter = m_LetterList.begin(); m_LetterListIter != m_LetterList.end();
         ++m_LetterListIter)
    {
        ++iCount;
        if (m_LetterListIter->m_dwLetterID == dwLetterID)
        {
            return iCount;
        }
    }
    return 0;
}

void CUIFriendWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);

    SetPosition(UI::Modern::PC::Friend::RmlFriendInitialX(),
                UI::Modern::PC::Friend::RmlFriendInitialY());
    //SetSize(213, 170);
    SetSize(250, 170);
    SetLimitSize(250, 150);

    m_FriendListWnd.Init(I18N::Game::FriendsList, GetUIID());
    m_ChatRoomListWnd.Init(I18N::Game::WindowList, GetUIID());
    m_LetterBoxWnd.Init(I18N::Game::LetterBox, GetUIID());

    g_pWindowMgr->AddWindowFinder(&m_FriendListWnd);
    g_pWindowMgr->AddWindowFinder(&m_ChatRoomListWnd);
    g_pWindowMgr->AddWindowFinder(&m_LetterBoxWnd);

    m_FriendListWnd.SetArrangeType(0, 0, 21);
    m_FriendListWnd.SetResizeType(3, 0, -19);

    m_ChatRoomListWnd.SetArrangeType(0, 0, 21);
    m_ChatRoomListWnd.SetResizeType(3, 0, -19);

    m_LetterBoxWnd.SetArrangeType(0, 0, 21);
    m_LetterBoxWnd.SetResizeType(3, 0, -19);
    m_ModernPanel.Create();
}

bool CUIFriendWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}

void CUIFriendWindow::SetTabIndex(int index)
{
    m_iTabIndex = index;
    if (index == 1)
        SocketClient->ToGameServer()->SendLetterListRequest();
}

void CUIFriendWindow::Reset()
{
    SetTabIndex(0);
}

void CUIFriendWindow::Close()
{
    if (g_pWindowMgr->GetWindow(GetUIID())->GetState() == UISTATE_NORMAL)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}

void CUIFriendWindow::Refresh()
{
    m_FriendListWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_ChatRoomListWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_LetterBoxWnd.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    m_FriendListWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_ChatRoomListWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_LetterBoxWnd.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);

    m_FriendListWnd.Refresh();
    m_ChatRoomListWnd.Refresh();
    m_LetterBoxWnd.Refresh();

    g_dwKeyFocusUIID = m_FriendListWnd.GetKeyMoveListUIID();
}

BOOL CUIFriendWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        switch (m_iTabIndex)
        {
        case 0:
            g_dwKeyFocusUIID = m_FriendListWnd.GetKeyMoveListUIID();
            break;
        case 1:
            g_dwKeyFocusUIID = m_LetterBoxWnd.GetKeyMoveListUIID();
            break;
        case 2:
            g_dwKeyFocusUIID = m_ChatRoomListWnd.GetKeyMoveListUIID();
            break;
        default:
            break;
        }
        break;
    case UI_MESSAGE_YNRETURN:
        if (m_WorkMessage.m_iParam2 == 1)
        {
            if (g_pWindowMgr->GetChatReject() == FALSE)
            {
                SocketClient->ToGameServer()->SendSetFriendOnlineState(0);
                g_pWindowMgr->SetChatReject(TRUE);
                SessionOrigin().FriendMenuObject().CloseAllChatWindow();
            }
        }
        break;
    default:
        break;
    }

    return FALSE;
}

void CUIFriendWindow::DoActionSub(BOOL bMessageOnly)
{
    if (GetState() == UISTATE_MOVE || GetState() == UISTATE_RESIZE)
        ;
    else
    {
        switch (m_iTabIndex)
        {
        case 0:
            m_FriendListWnd.DoAction(bMessageOnly);
            break;
        case 1:
            m_LetterBoxWnd.DoAction(bMessageOnly);
            break;
        case 2:
            m_ChatRoomListWnd.DoAction(bMessageOnly);
            break;
        default:
            break;
        }
    }
}

void CUIFriendWindow::DoMouseActionSub()
{
    if (GetState() == UISTATE_RESIZE)
        return;

    BOOL bChangeTab = FALSE;
    if (CheckMouseIn(RPos_x(0), RPos_y(2), 53, 19) == TRUE)
    {
        m_iTabMouseOverIndex = 0;
        if (MouseLButton)
        {
            if (m_iTabIndex != 0)
            {
                bChangeTab = TRUE;
                PlayBuffer(SOUND_CLICK01);
            }
            m_iTabIndex = 0;
            g_dwKeyFocusUIID = m_FriendListWnd.GetKeyMoveListUIID();
            MouseLButton = FALSE;
            Refresh();
        }
    }
    else if (CheckMouseIn(RPos_x(53), RPos_y(2), 53, 19) == TRUE)
    {
        m_iTabMouseOverIndex = 1;
        if (MouseLButton)
        {
            if (m_iTabIndex != 1)
            {
                bChangeTab = TRUE;
                PlayBuffer(SOUND_CLICK01);
            }
            m_iTabIndex = 1;
            g_dwKeyFocusUIID = m_LetterBoxWnd.GetKeyMoveListUIID();
            MouseLButton = FALSE;
            Refresh();
        }
    }
    else if (CheckMouseIn(RPos_x(106), RPos_y(2), 53, 19) == TRUE)
    {
        m_iTabMouseOverIndex = 2;
        if (MouseLButton)
        {
            if (m_iTabIndex != 2)
            {
                bChangeTab = TRUE;
                PlayBuffer(SOUND_CLICK01);
            }
            m_iTabIndex = 2;
            g_dwKeyFocusUIID = m_ChatRoomListWnd.GetKeyMoveListUIID();
            MouseLButton = FALSE;
            Refresh();
        }
    }
    else
    {
        m_iTabMouseOverIndex = m_iTabIndex;
        SIZE TextSize;

        g_RenderText.MeasureText(I18N::Game::RefuseChat, wcslen(I18N::Game::RefuseChat), &TextSize);

        if (CheckMouseIn(RPos_x(0) + RWidth() - TextSize.cx - 2 - 14, RPos_y(4),
                         TextSize.cx + 2 + 14, 20) == TRUE)
        {
            if (MouseLButtonPop)
            {
                PlayBuffer(SOUND_CLICK01);
                if (g_pWindowMgr->GetChatReject() == TRUE)
                {
                    SocketClient->ToGameServer()->SendSetFriendOnlineState(1);
                    g_pWindowMgr->SetChatReject(FALSE);
                }
                else
                {
                    g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                            I18N::Game::IfYouRefuseChatAllChatWindowsWillClose,
                                            GetUIID());
                }
                MouseLButtonPop = FALSE;
            }
        }
    }

    if (bChangeTab == TRUE)
    {
        m_FriendListWnd.Refresh();
        m_ChatRoomListWnd.Refresh();
        m_LetterBoxWnd.Refresh();
    }
}

void CUIFriendMenu::Reset()
{
    if (m_WindowList.size() != 0)
        m_WindowListSelectIter = m_WindowList.end();

    m_WindowList.clear();
    m_NewChatWindowList.clear();
    RemoveAllRequestWindow();
}

void CUIFriendMenu::Init()
{
    m_WorkMessage = {};
    m_iMouseClickPos_x = 0;
    m_iMouseClickPos_y = 0;
    m_iMinWidth = 100;
    m_iMinHeight = 100;
    m_iMaxWidth = 0;
    m_iMaxHeight = 0;
    SetOption(UIWINDOWSTYLE_NULL);
    m_bHaveTextBox = FALSE;
    m_iControlButtonClick = 0;

    m_iFriendMenuPos_y = 459 - 24;
    m_iFriendMenuHeight = 18;
    SetPosition(582, m_iFriendMenuPos_y);
    SetSize(52, 0); //m_iFriendMenuHeight);
    m_fLineHeight = 0;
    m_WindowListSelectIter = m_WindowList.end();
    SetState(UISTATE_HIDE);
    m_fMenuAlpha = 0;
    m_fMenuAlphaAdd = 0;
    m_bNewMailAlert = FALSE;
    m_iBlinkTemp = 0;
    m_iLetterBlink = 0;
    m_bHotKey = FALSE;
}

void CUIFriendMenu::AddWindow(DWORD dwUIID, CUIBaseWindow *pWindow)
{
    if (pWindow == NULL)
        return;

    m_WindowList.push_back(dwUIID);
}

void CUIFriendMenu::RemoveWindow(DWORD dwUIID)
{
    BOOL bFind = FALSE;
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        if (*m_WindowListIter == dwUIID)
        {
            bFind = TRUE;
            break;
        }
    }
    if (bFind == FALSE)
        return;
    m_WindowList.erase(m_WindowListIter);
    m_WindowListSelectIter = m_WindowList.end();
}

BOOL CUIFriendMenu::HandleMessage()
{
    return FALSE;
}

void CUIFriendMenu::DoActionSub(BOOL bMessageOnly)
{
    if (m_bHotKey == TRUE && PressKey(VK_RETURN))
    {
        if (m_WindowListSelectIter != m_WindowList.end() &&
            g_pWindowMgr->GetWindow(*m_WindowListSelectIter) != NULL)
        {
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, *m_WindowListSelectIter, 0);
        }
        HideMenu();
        m_bHotKey = FALSE;
    }
}

void CUIFriendMenu::DoMouseActionSub()
{
    int iLineHeight = (m_fLineHeight + 4) * m_WindowList.size();

    if (GetState() == UISTATE_NORMAL)
    {
        if (CheckMouseIn(m_iPos_x, m_iFriendMenuPos_y - iLineHeight, m_iWidth, iLineHeight) == TRUE)
        {
            m_bHotKey = FALSE;
            g_pWindowMgr->SetWindowsEnable(m_dwUIID);

            int iSelectLine = m_WindowList.size() -
                              (MouseY - m_iFriendMenuPos_y + iLineHeight) / (m_fLineHeight + 4);
            m_WindowListIter = m_WindowList.begin();
            for (int i = 0; i < iSelectLine; ++i)
            {
                ++m_WindowListIter;
                if (m_WindowListIter == m_WindowList.end())
                    break;
            }
            m_WindowListSelectIter = m_WindowListIter;
            if (MouseLButtonPop && GetState() == UISTATE_NORMAL)
            {
                SetFocus(g_hWnd);
                PlayBuffer(SOUND_CLICK01);
                MouseLButtonPop = FALSE;
                if (g_pWindowMgr->GetWindow(*m_WindowListSelectIter) == NULL)
                    ;
                else if (g_pWindowMgr->GetWindow(*m_WindowListSelectIter)->GetState() ==
                             UISTATE_HIDE ||
                         g_pWindowMgr->GetTopNotMainWindowUIID() != *m_WindowListSelectIter)
                {
                    g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, *m_WindowListSelectIter, 0);
                    MouseLButton = false;
                }
                else
                {
                    g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, *m_WindowListSelectIter, 0);
                }
                HideMenu();
            }
            MouseOnWindow = true;
        }
        else if (m_bHotKey == FALSE)
            m_WindowListSelectIter = m_WindowList.end();
    }
}

int CUIFriendMenu::GetBlinkTemp()
{
    return m_iBlinkTemp;
}

void CUIFriendMenu::IncreaseBlinkTemp()
{
    m_iBlinkTemp++;

    if (m_iBlinkTemp > 23)
    {
        m_iBlinkTemp = 0;
    }
}

int CUIFriendMenu::GetLetterBlink()
{
    return m_iLetterBlink;
}

void CUIFriendMenu::IncreaseLetterBlink()
{
    m_iLetterBlink++;

    if (m_iLetterBlink > 5)
    {
        m_iLetterBlink = 0;
        m_bNewMailAlert = FALSE;
    }
}

void CUIFriendMenu::ShowMenu(BOOL bHotKey)
{
    if (m_WindowList.empty() == TRUE)
        return;
    m_bHotKey = bHotKey;

    if (GetState() == UISTATE_HIDE)
    {
        SetState(UISTATE_NORMAL);
        m_iHeight += (m_fLineHeight + 4) * m_WindowList.size();
        m_iPos_y -= (m_fLineHeight + 4) * m_WindowList.size();
        m_fMenuAlphaAdd = 0.25f;

        if (bHotKey == TRUE)
        {
            m_WindowListSelectIter = m_WindowList.begin();
            if (m_WindowList.size() > 1)
            {
                while (*m_WindowListSelectIter == g_pWindowMgr->GetTopWindowUIID())
                {
                    ++m_WindowListSelectIter;
                    if (m_WindowListSelectIter == m_WindowList.end())
                        break;
                };
            }
        }
    }
    else if (bHotKey == TRUE)
    {
        if (m_WindowListSelectIter == m_WindowList.end())
            m_WindowListSelectIter = m_WindowList.begin();
        else
        {
            ++m_WindowListSelectIter;
            if (m_WindowListSelectIter == m_WindowList.end())
                m_WindowListSelectIter = m_WindowList.begin();
        }
    }
}

void CUIFriendMenu::HideMenu()
{
    if (GetState() == UISTATE_NORMAL)
    {
        SetState(UISTATE_HIDE);
        m_iHeight = 0;
        m_iPos_y = m_iFriendMenuPos_y;
        m_fMenuAlphaAdd = -0.25f;
        m_bHotKey = FALSE;
    }
}

void CUIFriendMenu::SetNewChatAlert(DWORD dwAlertWindowID)
{
    BOOL bFind = FALSE;
    for (m_WindowListIter = m_NewChatWindowList.begin();
         m_WindowListIter != m_NewChatWindowList.end(); ++m_WindowListIter)
    {
        if (*m_WindowListIter == dwAlertWindowID)
        {
            bFind = TRUE;
            break;
        }
    }
    PlayBuffer(SOUND_FRIEND_CHAT_ALERT);

    if (bFind == FALSE)
        m_NewChatWindowList.push_back(dwAlertWindowID);
}

void CUIFriendMenu::SetNewChatAlertOff(DWORD dwAlertWindowID)
{
    if (m_NewChatWindowList.empty() == TRUE)
        return;

    BOOL bFind = FALSE;
    for (m_WindowListIter = m_NewChatWindowList.begin();
         m_WindowListIter != m_NewChatWindowList.end(); ++m_WindowListIter)
    {
        if (*m_WindowListIter == dwAlertWindowID)
        {
            bFind = TRUE;
            break;
        }
    }
    if (bFind == TRUE)
        m_NewChatWindowList.erase(m_WindowListIter);
}

BOOL CUIFriendMenu::IsNewChatAlert()
{
    if (m_NewChatWindowList.empty() == FALSE)
        return TRUE;
    else
        return FALSE;
}

void CUIFriendMenu::SetNewMailAlert(BOOL bAlert)
{
    m_bNewMailAlert = bAlert;
}

DWORD CUIFriendMenu::CheckChatRoomDuplication(const wchar_t *pszTargetName)
{
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        int iResult;
        const wchar_t *pName =
            ((CUIChatWindow *)g_pWindowMgr->GetWindow(*m_WindowListIter))->GetChatFriend(&iResult);
        if (iResult == 2 || iResult == 0)
        {
            continue;
        }
        else if (pName == NULL || pName[0] == '\0')
        {
            return MCI_SEQ_MAPPER;
        }
        else if (wcsncmp(pName, pszTargetName, MAX_USERNAME_SIZE) == 0)
        {
            return *m_WindowListIter;
        }
    }
    return 0;
}

void CUIFriendMenu::SendChatRoomConnectCheck()
{
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        auto *pChatWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(*m_WindowListIter);
        if (pChatWindow != nullptr)
        {
            Connection *pSocket = pChatWindow->GetCurrentSocket();
            if (pSocket != nullptr && pSocket->ToChatServer() != nullptr)
            {
                pSocket->ToChatServer()->SendKeepAlive();
            }
        }
    }
}

void CUIFriendMenu::UpdateAllChatWindowInviteList()
{
    CUIChatWindow *pChatWindow = NULL;
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        pChatWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(*m_WindowListIter);
        if (pChatWindow != NULL)
        {
            if (pChatWindow->GetShowType() == 2)
                pChatWindow->UpdateInvitePalList();
        }
    }
}

void CUIFriendMenu::AddRequestWindow(const wchar_t *szTargetName)
{
    if (szTargetName == NULL)
        return;
    if (wcslen(szTargetName) > MAX_USERNAME_SIZE)
        return;
    wchar_t *pszName = new wchar_t[MAX_USERNAME_SIZE + 1];
    wcsncpy(pszName, szTargetName, MAX_USERNAME_SIZE);
    pszName[MAX_USERNAME_SIZE] = '\0';
    m_RequestChatWindowList.push_back(pszName);
}

BOOL CUIFriendMenu::IsRequestWindow(const wchar_t *szTargetName)
{
    for (m_RequestChatWindowListIter = m_RequestChatWindowList.begin();
         m_RequestChatWindowListIter != m_RequestChatWindowList.end();
         ++m_RequestChatWindowListIter)
    {
        if (wcsncmp(*m_RequestChatWindowListIter, szTargetName, MAX_USERNAME_SIZE) == 0)
            return TRUE;
    }
    return FALSE;
}

void CUIFriendMenu::RemoveRequestWindow(const wchar_t *szTargetName)
{
    BOOL bFind = FALSE;
    for (m_RequestChatWindowListIter = m_RequestChatWindowList.begin();
         m_RequestChatWindowListIter != m_RequestChatWindowList.end();
         ++m_RequestChatWindowListIter)
    {
        if (wcsncmp(*m_RequestChatWindowListIter, szTargetName, MAX_USERNAME_SIZE) == 0)
        {
            bFind = TRUE;
            break;
        }
    }
    if (bFind == TRUE)
    {
        if (*m_RequestChatWindowListIter != NULL)
        {
            delete[] *m_RequestChatWindowListIter;
            *m_RequestChatWindowListIter = NULL;
        }
        m_RequestChatWindowList.erase(m_RequestChatWindowListIter);
    }
}

void CUIFriendMenu::RemoveAllRequestWindow()
{
    for (m_RequestChatWindowListIter = m_RequestChatWindowList.begin();
         m_RequestChatWindowListIter != m_RequestChatWindowList.end();
         ++m_RequestChatWindowListIter)
    {
        if (*m_RequestChatWindowListIter != NULL)
        {
            delete[] *m_RequestChatWindowListIter;
            *m_RequestChatWindowListIter = NULL;
        }
    }
    m_RequestChatWindowList.clear();
}

void CUIFriendMenu::CloseAllChatWindow()
{
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, *m_WindowListIter, 0);
    }
    m_NewChatWindowList.clear();
    RemoveAllRequestWindow();
}

void CUIFriendMenu::LockAllChatWindow()
{
    CUIChatWindow *pWindow = NULL;
    for (m_WindowListIter = m_WindowList.begin(); m_WindowListIter != m_WindowList.end();
         ++m_WindowListIter)
    {
        pWindow = (CUIChatWindow *)g_pWindowMgr->GetWindow(*m_WindowListIter);
        if (pWindow != NULL)
        {
            pWindow->AddChatText(255, I18N::Game::YouAreDisconnectedFromTheServer, 1, 0);
            pWindow->Lock(TRUE);
        }
    }
}

namespace ChatLogDetail
{
const UI::Modern::RmlUiDesign &ChatCompatibility()
{
    static const UI::Modern::RmlUiDesign design(
        "Data/UI/PC/Chat/chat.rml", {"ChatCompatibility-Width", "ChatCompatibility-RowHeight",
                                     "ChatCompatibility-VerticalPadding",
                                     "ChatCompatibility-TextWidth", "ChatCompatibility-InitialRows",
                                     "ChatCompatibility-RowStep", "ChatCompatibility-MaximumRows"});
    return design;
}

UI::Modern::PC::Chat::RmlChatMessageType ToModernChatType(SEASON3B::MESSAGE_TYPE type) noexcept
{
    using ModernType = UI::Modern::PC::Chat::RmlChatMessageType;
    switch (type)
    {
    case SEASON3B::TYPE_WHISPER_MESSAGE:
        return ModernType::Whisper;
    case SEASON3B::TYPE_SYSTEM_MESSAGE:
        return ModernType::System;
    case SEASON3B::TYPE_ERROR_MESSAGE:
        return ModernType::Error;
    case SEASON3B::TYPE_PARTY_MESSAGE:
        return ModernType::Party;
    case SEASON3B::TYPE_GUILD_MESSAGE:
        return ModernType::Guild;
    case SEASON3B::TYPE_UNION_MESSAGE:
        return ModernType::Union;
    case SEASON3B::TYPE_GM_MESSAGE:
        return ModernType::GameMaster;
    case SEASON3B::TYPE_GENS_MESSAGE:
        return ModernType::Gens;
    default:
        return ModernType::Normal;
    }
}
} // namespace ChatLogDetail

SEASON3B::CNewUIChatLogWindow::CNewUIChatLogWindow(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), m_modernPanel(keeper), g_RenderText(keeper.SessionText()),
      MouseWheel(keeper.MouseWheelState())
{
    Init();
}

SEASON3B::CNewUIChatLogWindow::~CNewUIChatLogWindow()
{
    Release();
}

void SEASON3B::CNewUIChatLogWindow::Init()
{
    m_pNewUIMng = nullptr;
    m_blockChatOpen = false;
    m_WndPos.x = m_WndPos.y = 0;
    m_ScrollBtnPos.x = m_ScrollBtnPos.y = 0;
    m_WndSize.cx = ChatLogDetail::ChatCompatibility().Number<int>(0);
    m_WndSize.cy = 0;
    m_nShowingLines = ChatLogDetail::ChatCompatibility().Number<int>(4);
    m_iCurrentRenderEndLine = -1;
    m_fBackAlpha = UI::Modern::PC::Chat::DefaultRmlChatAlpha() / 100.0F;

    m_EventState = EVENT_NONE;

    m_bShowFrame = false;

    m_CurrentRenderMsgType = TYPE_ALL_MESSAGE;
    m_bShowChatLog = true;

    m_bPointedMessage = false;
    m_iPointedMessageIndex = 0;
}

bool SEASON3B::CNewUIChatLogWindow::Create(CNewUIManager *pNewUIMng, int x, int y,
                                           int nShowingLines /* = 6 */)
{
    Release();

    if (nullptr == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CHATLOGWINDOW, this);
    m_WndPos.x = x;
    m_WndPos.y = y;
    SetNumberOfShowingLines(nShowingLines);
    return true;
}

void SEASON3B::CNewUIChatLogWindow::Release()
{
    ResetFilter();
    ClearAll();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }

    Init();
}

void SEASON3B::CNewUIChatLogWindow::AddText(const type_string &strID, const type_string &strText,
                                            MESSAGE_TYPE MsgType,
                                            MESSAGE_TYPE ErrMsgType /*= TYPE_ALL_MESSAGE*/)
{
    if (strID.empty() && strText.empty())
    {
        return;
    }

    if (GetNumberOfLines(MsgType) >= MAX_NUMBER_OF_LINES)
    {
        RemoveFrontLine(MsgType);
    }

    if (GetNumberOfLines(TYPE_ALL_MESSAGE) >= MAX_NUMBER_OF_LINES)
    {
        RemoveFrontLine(TYPE_ALL_MESSAGE);
    }

    // The S16 retained log owns every message. Visibility filters rebuild the
    // view and must never delete or reject history entries.
    m_modernPanel.AddMessage(strID, strText, ChatLogDetail::ToModernChatType(MsgType));

    bool accepted = m_vecFilters.empty() || MsgType != TYPE_CHAT_MESSAGE;
    if (!accepted)
    {
        accepted = CheckFilterText(strID) || CheckFilterText(strText);
        if (accepted && g_pOption->IsWhisperSound())
        {
            PlayBuffer(SOUND_WHISPER);
        }
    }
    if (!accepted)
        return;

    ProcessAddText(strID, strText, MsgType, ErrMsgType);
}

void SEASON3B::CNewUIChatLogWindow::ProcessAddText(const type_string &strID,
                                                   const type_string &strText, MESSAGE_TYPE MsgType,
                                                   MESSAGE_TYPE ErrMsgType)
{
    type_vector_msgs *pvecMsgs = GetMsgs(MsgType);
    if (pvecMsgs == nullptr)
    {
        assert(!"Empty Message");
        return;
    }

    int nScrollLines = 0;
    if (strText.size() >= 20)
    {
        type_string strText1, strText2;
        SeparateText(strID, strText, strText1, strText2);
        if (!strText1.empty())
        {
            const auto pMsgText = new CMessageText;
            if (!pMsgText->Create(strID, strText, MsgType))
                delete pMsgText;
            else
            {
                pvecMsgs->push_back(pMsgText);
            }

            const auto pAllMsgText = new CMessageText;
            if (!pAllMsgText->Create(strID, strText1, MsgType))
            {
                delete pAllMsgText;
            }
            else
            {
                m_vecAllMsgs.push_back(pAllMsgText);
            }

            if ((MsgType == TYPE_ERROR_MESSAGE) &&
                (ErrMsgType != TYPE_ERROR_MESSAGE && ErrMsgType != TYPE_ALL_MESSAGE))
            {
                type_vector_msgs *pErrvecMsgs = GetMsgs(ErrMsgType);
                if (pErrvecMsgs == nullptr)
                {
                    assert(!"Error Chat");
                    return;
                }

                const auto pErrMsgText = new CMessageText;
                if (!pErrMsgText->Create(strID, strText1, MsgType))
                    delete pErrMsgText;
                else
                {
                    pErrvecMsgs->push_back(pErrMsgText);
                }
            }

            if (GetCurrentMsgType() == TYPE_ALL_MESSAGE || GetCurrentMsgType() == MsgType)
            {
                nScrollLines++;
            }
        }
        if (!strText2.empty())
        {
            const auto pMsgText = new CMessageText;
            if (!pMsgText->Create(L"", strText2, MsgType))
                delete pMsgText;
            else
            {
                pvecMsgs->push_back(pMsgText);
            }

            const auto pAllMsgText = new CMessageText;
            if (!pAllMsgText->Create(L"", strText2, MsgType))
                delete pAllMsgText;
            else
            {
                m_vecAllMsgs.push_back(pAllMsgText);
            }

            if ((MsgType == TYPE_ERROR_MESSAGE) &&
                (ErrMsgType != TYPE_ERROR_MESSAGE && ErrMsgType != TYPE_ALL_MESSAGE))
            {
                type_vector_msgs *pErrvecMsgs = GetMsgs(ErrMsgType);
                if (pErrvecMsgs == nullptr)
                {
                    assert(!"Error chat 2");
                    return;
                }

                const auto pErrMsgText = new CMessageText;
                if (!pErrMsgText->Create(L"", strText2, MsgType))
                    delete pErrMsgText;
                else
                {
                    pErrvecMsgs->push_back(pErrMsgText);
                }
            }

            if (GetCurrentMsgType() == TYPE_ALL_MESSAGE || GetCurrentMsgType() == MsgType)
            {
                nScrollLines++;
            }
        }
    }
    else
    {
        const auto pMsgText = new CMessageText;
        if (!pMsgText->Create(strID, strText, MsgType))
            delete pMsgText;
        else
        {
            pvecMsgs->push_back(pMsgText);
        }

        const auto pAllMsgText = new CMessageText;
        if (!pAllMsgText->Create(strID, strText, MsgType))
            delete pAllMsgText;
        else
        {
            m_vecAllMsgs.push_back(pAllMsgText);
        }

        if ((MsgType == TYPE_ERROR_MESSAGE) &&
            (ErrMsgType != TYPE_ERROR_MESSAGE && ErrMsgType != TYPE_ALL_MESSAGE))
        {
            type_vector_msgs *pErrvecMsgs = GetMsgs(ErrMsgType);
            if (pErrvecMsgs == nullptr)
            {
                assert(!"Error chat 3");
                return;
            }

            const auto pErrMsgText = new CMessageText;
            if (!pErrMsgText->Create(strID, strText, MsgType))
                delete pErrMsgText;
            else
            {
                pErrvecMsgs->push_back(pErrMsgText);
            }
        }

        if (GetCurrentMsgType() == TYPE_ALL_MESSAGE || GetCurrentMsgType() == MsgType)
        {
            nScrollLines++;
        }
    }

    pvecMsgs = GetMsgs(GetCurrentMsgType());
    if (pvecMsgs == nullptr)
    {
        assert(!"Error chat 4");
        return;
    }

    //. Auto Scrolling
    if (nScrollLines > 0 && ((pvecMsgs->size() - (m_iCurrentRenderEndLine + 1) - nScrollLines) < 3))
        m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
    else if (!m_bShowFrame)
        m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
}

void SEASON3B::CNewUIChatLogWindow::RemoveFrontLine(MESSAGE_TYPE MsgType)
{
    type_vector_msgs *pvecMsgs = GetMsgs(MsgType);

    if (pvecMsgs == nullptr)
    {
        assert(!"Empty Message RemoveFrontLine");
        return;
    }

    auto vi = pvecMsgs->begin();
    if (vi != pvecMsgs->end())
    {
        delete (*vi);
        vi = pvecMsgs->erase(vi);
    }

    if (MsgType == GetCurrentMsgType())
    {
        Scrolling(GetCurrentRenderEndLine());
    }
}

void SEASON3B::CNewUIChatLogWindow::Clear(MESSAGE_TYPE MsgType)
{
    type_vector_msgs *pvecMsgs = GetMsgs(MsgType);
    if (pvecMsgs == nullptr)
    {
        assert(!"Empty Message CNewUIChatLogWindow");
        return;
    }

    auto vi_msg = pvecMsgs->begin();
    for (; vi_msg != pvecMsgs->end(); vi_msg++)
        delete (*vi_msg);
    pvecMsgs->clear();

    if (MsgType == GetCurrentMsgType())
    {
        m_iCurrentRenderEndLine = -1;
    }
}

size_t SEASON3B::CNewUIChatLogWindow::GetNumberOfLines(MESSAGE_TYPE MsgType)
{
    type_vector_msgs *pvecMsgs = GetMsgs(MsgType);
    if (pvecMsgs == nullptr)
    {
        return 0;
    }

    return pvecMsgs->size();
}

int SEASON3B::CNewUIChatLogWindow::GetCurrentRenderEndLine() const
{
    return m_iCurrentRenderEndLine;
}

void SEASON3B::CNewUIChatLogWindow::Scrolling(int nRenderEndLine)
{
    type_vector_msgs *pvecMsgs = GetMsgs(m_CurrentRenderMsgType);
    if (pvecMsgs == nullptr)
    {
        assert(!"Empty message Scrolling");
        return;
    }

    if ((int)pvecMsgs->size() <= m_nShowingLines)
    {
        m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
    }
    else
    {
        if (nRenderEndLine < m_nShowingLines)
            m_iCurrentRenderEndLine = m_nShowingLines - 1;

        else if (nRenderEndLine >= (int)pvecMsgs->size())
            m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
        else
            m_iCurrentRenderEndLine = nRenderEndLine;
    }
}

void SEASON3B::CNewUIChatLogWindow::SetFilterText(const type_string &strFilterText)
{
    bool bPrevFilter = false;

    if (!m_vecFilters.empty())
    {
        bPrevFilter = true;
        ResetFilter();
    }

    wchar_t szTemp[MAX_CHAT_BUFFER_SIZE + 1] = {
        0,
    };
    strFilterText.copy(szTemp, MAX_CHAT_BUFFER_SIZE);
    szTemp[MAX_CHAT_BUFFER_SIZE] = '\0';

    wchar_t *context = nullptr;
    wchar_t *token = wcstok_s(szTemp, L" ", &context);
    token = wcstok_s(nullptr, L" ", &context);

    if (token == nullptr)
    {
        ResetFilter();
        AddText(L"", I18N::Game::FilteringHasBeenCanceled, TYPE_SYSTEM_MESSAGE);
    }
    else
    {
        for (int i = 0; i < 5; i++)
        {
            if (nullptr == token)
            {
                break;
            }
            AddFilterWord(token);
            token = wcstok_s(nullptr, L" ", &context);
        }

        AddText(L"", I18N::Game::FilteringHasBeenActivated, TYPE_SYSTEM_MESSAGE);
    }
}

void SEASON3B::CNewUIChatLogWindow::ResetFilter()
{
    m_vecFilters.clear();
}

void SEASON3B::CNewUIChatLogWindow::SetNumberOfShowingLines(int nShowingLines,
                                                            OUT LPSIZE lpBoxSize /* = nullptr*/)
{
    const int step = ChatLogDetail::ChatCompatibility().Number<int>(5);
    const int maximum = ChatLogDetail::ChatCompatibility().Number<int>(6);
    m_nShowingLines = std::max(step, nShowingLines / step * step);
    if (m_nShowingLines > maximum)
        m_nShowingLines = step;

    if (m_nShowingLines > GetCurrentRenderEndLine())
        Scrolling(m_nShowingLines - 1);

    UpdateWndSize();
    UpdateScrollPos();

    if (lpBoxSize)
    {
        *lpBoxSize = m_WndSize;
    }
}
size_t SEASON3B::CNewUIChatLogWindow::GetNumberOfShowingLines() const
{
    return m_nShowingLines;
}

void SEASON3B::CNewUIChatLogWindow::SetBackAlphaAuto()
{
    m_fBackAlpha =
        UI::Modern::PC::Chat::NextRmlChatAlpha(static_cast<int>(std::lround(m_fBackAlpha * 100))) /
        100.0F;
}

void SEASON3B::CNewUIChatLogWindow::SetBackAlpha(float fAlpha)
{
    if (fAlpha < 0.f)
        m_fBackAlpha = 0.f;
    else if (fAlpha > 1.f)
        m_fBackAlpha = 1.f;
    else
        m_fBackAlpha = fAlpha;
}

float SEASON3B::CNewUIChatLogWindow::GetBackAlpha() const
{
    return m_fBackAlpha;
}

void SEASON3B::CNewUIChatLogWindow::ShowFrame()
{
    m_bShowFrame = true;
}

void SEASON3B::CNewUIChatLogWindow::HideFrame()
{
    m_bShowFrame = false;
}

bool SEASON3B::CNewUIChatLogWindow::IsShowFrame()
{
    return m_bShowFrame;
}

bool SEASON3B::CNewUIChatLogWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUIChatLogWindow::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIChatLogWindow::Update()
{
    UpdateScrollPos();

    return true;
}

float SEASON3B::CNewUIChatLogWindow::GetLayerDepth()
{
    using namespace UI::Modern::MigratedUiRenderLayers;
    return m_blockChatOpen ? BlockChat : Chat;
}

float SEASON3B::CNewUIChatLogWindow::GetKeyEventOrder()
{
    return 8.0f;
}

void SEASON3B::CNewUIChatLogWindow::SeparateText(IN const type_string &strID,
                                                 IN const type_string &strText,
                                                 OUT type_string &strText1,
                                                 OUT type_string &strText2)
{

    SIZE TextSize;

    float max_first_line_size = ChatLogDetail::ChatCompatibility().Number(3) * g_fScreenRate_x;
    if (!strID.empty())
    {
        const type_string strIDPart = strID + L" : ";

        g_RenderText.MeasureText(strIDPart.c_str(), strIDPart.length(), &TextSize);
        max_first_line_size -= (TextSize.cx);
    }

    g_RenderText.MeasureText(strText.c_str(), strText.length(), &TextSize);
    auto required_size = TextSize.cx;

    if (required_size <= max_first_line_size)
    {
        strText1 = strText;
        strText2 = L"";
        return;
    }

    BOOL bSpaceExist = (strText.find_last_of(L" ") != std::wstring::npos) ? TRUE : FALSE;
    int iLocToken = strText.length();

    while ((required_size > max_first_line_size) && (iLocToken > -1))
    {
        iLocToken = (bSpaceExist) ? strText.find_last_of(L" ", iLocToken - 1) : iLocToken - 1;

        g_RenderText.MeasureText((strText.substr(0, iLocToken)).c_str(), iLocToken, &TextSize);
        required_size = TextSize.cx;
    }

    strText1 = strText.substr(0, iLocToken);
    strText2 = strText.substr(iLocToken, strText.length() - iLocToken);
}

bool SEASON3B::CNewUIChatLogWindow::CheckFilterText(const type_string &strTestText)
{
    auto vi_filters = m_vecFilters.begin();
    for (; vi_filters != m_vecFilters.end(); vi_filters++)
    {
        if (vi_filters->find(strTestText))
        {
            return true;
        }
    }

    return false;
}

void SEASON3B::CNewUIChatLogWindow::UpdateWndSize()
{
    m_WndSize.cx = ChatLogDetail::ChatCompatibility().Number<int>(0);
    m_WndSize.cy =
        static_cast<LONG>(ChatLogDetail::ChatCompatibility().Number(1) * GetNumberOfShowingLines() +
                          ChatLogDetail::ChatCompatibility().Number(2));
}

void SEASON3B::CNewUIChatLogWindow::UpdateScrollPos()
{
}

void SEASON3B::CNewUIChatLogWindow::AddFilterWord(const type_string &strWord)
{
    if (m_vecFilters.size() > 5)
        return;

    auto vi_filters = m_vecFilters.begin();
    for (; vi_filters != m_vecFilters.end(); vi_filters++)
    {
        if (0 == (*vi_filters).compare(strWord))
        {
            return;
        }
    }

    m_vecFilters.push_back(strWord);
}

void SEASON3B::CNewUIChatLogWindow::ClearAll()
{
    for (int i = TYPE_ALL_MESSAGE; i < NUMBER_OF_TYPES; i++)
    {
        Clear((MESSAGE_TYPE)i);
    }

    m_iCurrentRenderEndLine = -1;
    m_modernPanel.ClearMessages();
}

SEASON3B::CNewUIChatLogWindow::type_vector_msgs *SEASON3B::CNewUIChatLogWindow::GetMsgs(
    MESSAGE_TYPE MsgType)
{
    switch (MsgType)
    {
    case TYPE_ALL_MESSAGE:
        return &m_vecAllMsgs;
    case TYPE_CHAT_MESSAGE:
        return &m_VecChatMsgs;
    case TYPE_WHISPER_MESSAGE:
        return &m_vecWhisperMsgs;
    case TYPE_SYSTEM_MESSAGE:
        return &m_VecSystemMsgs;
    case TYPE_ERROR_MESSAGE:
        return &m_vecErrorMsgs;
    case TYPE_PARTY_MESSAGE:
        return &m_vecPartyMsgs;
    case TYPE_GUILD_MESSAGE:
        return &m_vecGuildMsgs;
    case TYPE_UNION_MESSAGE:
        return &m_vecUnionMsgs;
    case TYPE_GENS_MESSAGE:
        return &m_vecGensMsgs;
    case TYPE_GM_MESSAGE:
        return &m_vecGMMsgs;
    }

    return nullptr;
}

void SEASON3B::CNewUIChatLogWindow::ChangeMessage(MESSAGE_TYPE MsgType)
{
    m_CurrentRenderMsgType = MsgType;

    type_vector_msgs *pvecMsgs = GetMsgs(GetCurrentMsgType());
    if (pvecMsgs == nullptr)
    {
        return;
    }

    m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
}

SEASON3B::MESSAGE_TYPE SEASON3B::CNewUIChatLogWindow::GetCurrentMsgType() const
{
    return m_CurrentRenderMsgType;
}

void SEASON3B::CNewUIChatLogWindow::ShowChatLog()
{
    m_bShowChatLog = true;

    type_vector_msgs *pvecMsgs = GetMsgs(GetCurrentMsgType());
    if (pvecMsgs == nullptr)
    {
        return;
    }

    m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
}

void SEASON3B::CNewUIChatLogWindow::HideChatLog()
{
    m_bShowChatLog = false;
}

SEASON3B::CNewUISystemLogWindow::CNewUISystemLogWindow(SessionKeeper &keeper)
    : SessionLegacyCalls(keeper), g_RenderText(keeper.SessionText())
{
    Init();
}

SEASON3B::CNewUISystemLogWindow::~CNewUISystemLogWindow()
{
    Release();
}

void SEASON3B::CNewUISystemLogWindow::Init()
{
    m_pNewUIMng = nullptr;
    m_pChatLogWindow = nullptr;
    m_WndPos.x = m_WndPos.y = 0;
    m_WndSize.cx = WND_WIDTH;
    m_WndSize.cy = 0;
    m_nShowingLines = 6;
    m_iCurrentRenderEndLine = -1;
    m_fBackAlpha = 0.6f;
    m_bShowMessages = true;
}

bool SEASON3B::CNewUISystemLogWindow::Create(CNewUIManager *pNewUIMng,
                                             CNewUIChatLogWindow *pChatLogWindow, int x, int y)
{
    Release();

    if (nullptr == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pChatLogWindow = pChatLogWindow;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_SYSTEMLOGWINDOW, this);
    m_WndPos.x = x;
    m_WndPos.y = y;
    return true;
}

void SEASON3B::CNewUISystemLogWindow::Release()
{
    ClearAll();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }

    Init();
}

void SEASON3B::CNewUISystemLogWindow::AddText(const type_string &strText, MESSAGE_TYPE MsgType)
{
    if (strText.empty())
    {
        return;
    }

    if (m_pChatLogWindow != nullptr)
    {
        m_pChatLogWindow->AddText(L"", strText, MsgType);
    }

    type_vector_msgs *pvecMsgs = &m_vecAllMsgs;
    if (pvecMsgs == nullptr)
    {
        assert(!"Empty Message");
        return;
    }

    auto pMsgText = new CMessageText;
    if (!pMsgText->Create(L"", strText, MsgType))
    {
        delete pMsgText;
        return;
    }

    if (pvecMsgs->size() >= MAX_NUMBER_OF_LINES)
    {
        RemoveFrontLine();
    }

    pvecMsgs->push_back(pMsgText);

    m_iCurrentRenderEndLine = pvecMsgs->size() - 1;
}

void SEASON3B::CNewUISystemLogWindow::RemoveFrontLine()
{
    auto vi = m_vecAllMsgs.begin();
    if (vi != m_vecAllMsgs.end())
    {
        delete (*vi);
        vi = m_vecAllMsgs.erase(vi);
    }
}

int SEASON3B::CNewUISystemLogWindow::GetCurrentRenderEndLine() const
{
    return m_iCurrentRenderEndLine;
}

bool SEASON3B::CNewUISystemLogWindow::UpdateMouseEvent()
{
    return true;
}

bool SEASON3B::CNewUISystemLogWindow::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUISystemLogWindow::Update()
{
    return true;
}

float SEASON3B::CNewUISystemLogWindow::GetLayerDepth()
{
    return 6.05f;
}

float SEASON3B::CNewUISystemLogWindow::GetKeyEventOrder()
{
    return 8.0f;
}

void SEASON3B::CNewUISystemLogWindow::ClearAll()
{
    auto vi_msg = m_vecAllMsgs.begin();
    for (; vi_msg != m_vecAllMsgs.end(); vi_msg++)
        delete (*vi_msg);
    m_vecAllMsgs.clear();

    m_iCurrentRenderEndLine = -1;
}

bool SEASON3B::CNewUISystemLogWindow::CheckChatRedundancy(const type_string &strText,
                                                          int iSearchLine /* = 1*/)
{
    if (m_vecAllMsgs.empty())
        return false;
    auto vri_msgs = m_vecAllMsgs.rbegin();
    for (int i = 0; (i < iSearchLine) || (vri_msgs != m_vecAllMsgs.rend()); vri_msgs++, i++)
        if (0 == (*vri_msgs)->GetText().compare(strText))
            return true;
    return false;
}

namespace PartyListDetail
{
using UI::Modern::PartyFrameDesign;
using UI::Modern::PartyFrameMetric;

int PartyFrameLogicalSize(float gfxPixels, float screenRate, float maximumScale)
{
    return static_cast<int>(std::ceil(gfxPixels * maximumScale / screenRate));
}

int PartyFrameLogicalOffset(float gfxPixels, float screenRate, float maximumScale)
{
    return static_cast<int>(std::floor(gfxPixels * maximumScale / screenRate));
}

int PartyFrameLogicalSpan(float gfxOffset, float gfxSize, float screenRate, float maximumScale)
{
    const int start = PartyFrameLogicalOffset(gfxOffset, screenRate, maximumScale);
    const int end = PartyFrameLogicalSize(gfxOffset + gfxSize, screenRate, maximumScale);
    return end - start;
}

ButtonVisualState ToRmlButtonState(BUTTON_STATE state) noexcept
{
    switch (state)
    {
    case BUTTON_STATE_DOWN:
        return ButtonVisualState::Down;
    case BUTTON_STATE_OVER:
        return ButtonVisualState::Over;
    default:
        return ButtonVisualState::Up;
    }
}
} // namespace PartyListDetail

CNewUIPartyListWindow::CNewUIPartyListWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cameraProjection_(keeper.CameraProjectionObject()),
      renderer_(RendererForConstruction()), m_pNewUIMng(nullptr), m_BtnPartyExit(keeper),
      m_bActive(false), m_iSelectedCharacter(-1)
{
}

CNewUIPartyListWindow::~CNewUIPartyListWindow()
{
    Release();
}

bool CNewUIPartyListWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (pNewUIMng == nullptr)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_PARTY_INFO_WINDOW, this);

    (void)x;
    (void)y;
    m_partyFrameY = PartyListDetail::PartyFrameDesign().Number<int>(
        PartyListDetail::PartyFrameMetric::InitialY);
    SetPos(GetScreenWidth());
    SyncLeaveButtons();
    Show(true);
    return true;
}

void CNewUIPartyListWindow::Release()
{
    if (m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

int CNewUIPartyListWindow::GetSelectedCharacter()
{
    if (m_iSelectedCharacter == -1)
    {
        return -1;
    }
    return Party[m_iSelectedCharacter].index;
}

void CNewUIPartyListWindow::SetListBGColor()
{
    StagePartyFrame();
}

void CNewUIPartyListWindow::LeaveParty(int index)
{
    if (gMapManager.IsCursedTemple())
        return;
    PlayBuffer(SOUND_CLICK01);
    SocketClient->ToGameServer()->SendPartyPlayerKickRequest(Party[index].Number);
}

bool CNewUIPartyListWindow::BtnProcess()
{
    m_iSelectedCharacter = -1;
    if (m_partyFrameMinimized)
    {
        return false;
    }

    for (int i = 0; i < PartyNumber; ++i)
    {
        const bool canLeave = Hero != nullptr && (std::wcscmp(Party[0].Name, Hero->ID) == 0 ||
                                                  std::wcscmp(Party[i].Name, Hero->ID) == 0);
        if (canLeave && m_BtnPartyExit[i].UpdateMouseEvent())
        {
            LeaveParty(i);
            return true;
        }

        const float rowX = m_partyFrameX + PartyListDetail::PartyFrameLogicalOffset(
                                               PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::MemberX),
                                               ModernUiScreenRateX(), ModernUiScale());
        const float rowY =
            m_partyFrameY + PartyListDetail::PartyFrameLogicalOffset(
                                PartyListDetail::PartyFrameDesign().Number(
                                    PartyListDetail::PartyFrameMetric::FirstRowY) +
                                    i * PartyListDetail::PartyFrameDesign().Number(
                                            PartyListDetail::PartyFrameMetric::RowStep),
                                ModernUiScreenRateY(), ModernUiScale());
        if (!IsMouseInPartyBox(rowX, rowY,
                               PartyListDetail::PartyFrameLogicalSpan(
                                   PartyListDetail::PartyFrameDesign().Number(
                                       PartyListDetail::PartyFrameMetric::MemberX),
                                   PartyListDetail::PartyFrameDesign().Number(
                                       PartyListDetail::PartyFrameMetric::MemberWidth),
                                   ModernUiScreenRateX(), ModernUiScale()),
                               PartyListDetail::PartyFrameLogicalSpan(
                                   PartyListDetail::PartyFrameDesign().Number(
                                       PartyListDetail::PartyFrameMetric::FirstRowY) +
                                       i * PartyListDetail::PartyFrameDesign().Number(
                                               PartyListDetail::PartyFrameMetric::RowStep),
                                   PartyListDetail::PartyFrameDesign().Number(
                                       PartyListDetail::PartyFrameMetric::BackgroundHeight),
                                   ModernUiScreenRateY(), ModernUiScale())))
        {
            continue;
        }

        m_iSelectedCharacter = i;
        if (SelectedCharacter == -1 && CharactersClient.IsValidIndex(Party[i].index))
        {
            CHARACTER *const character = &CharactersClient[Party[i].index];
            if (character != Hero)
            {
                CreateChat(character->ID, L"", character);
            }
        }
        if (SelectCharacterInPartyList(&Party[i]))
        {
            return true;
        }
    }
    return false;
}

bool CNewUIPartyListWindow::UpdateMouseEvent()
{
    if (!m_bActive)
    {
        return true;
    }

    m_iSelectedCharacter = -1;
    bool passThrough = UpdatePartyFrameMouse();
    if (passThrough && BtnProcess())
    {
        passThrough = false;
    }

    if (passThrough && !m_partyFrameMinimized)
    {
        const float height = PartyListDetail::PartyFrameLogicalSize(
            PartyListDetail::PartyFrameDesign().Number(
                PartyListDetail::PartyFrameMetric::FirstRowY) +
                m_partyFrameRowCount * PartyListDetail::PartyFrameDesign().Number(
                                           PartyListDetail::PartyFrameMetric::RowStep),
            ModernUiScreenRateY(), ModernUiScale());
        if (IsMouseInPartyBox(m_partyFrameX, m_partyFrameY,
                              PartyListDetail::PartyFrameLogicalSize(
                                  PartyListDetail::PartyFrameDesign().Number(
                                      PartyListDetail::PartyFrameMetric::MemberWidth),
                                  ModernUiScreenRateX(), ModernUiScale()),
                              height))
        {
            passThrough = false;
        }
    }

    StagePartyFrame();
    return passThrough;
}

bool CNewUIPartyListWindow::UpdateKeyEvent()
{
    return true;
}

bool CNewUIPartyListWindow::Update()
{
    if (PartyNumber <= 0)
    {
        m_bActive = false;
        m_partyFrameRowCount = 0;
        StagePartyFrame();
        return true;
    }

    m_bActive = true;
    m_partyFrameRowCount = std::clamp(PartyNumber, 0, MAX_PARTYS);
    SyncLeaveButtons();
    for (int i = 0; i < PartyNumber; ++i)
    {
        Party[i].index = -2;
    }
    StagePartyFrame();
    return true;
}

bool CNewUIPartyListWindow::UpdatePartyFrameMouse()
{
    const float frameLocalWidth = m_partyFrameMinimized
                                      ? PartyListDetail::PartyFrameDesign().Number(
                                            PartyListDetail::PartyFrameMetric::HeaderWidth)
                                      : PartyListDetail::PartyFrameDesign().Number(
                                            PartyListDetail::PartyFrameMetric::MemberWidth);
    const float frameLocalHeight =
        m_partyFrameMinimized
            ? PartyListDetail::PartyFrameDesign().Number(
                  PartyListDetail::PartyFrameMetric::HeaderHeight)
            : PartyListDetail::PartyFrameDesign().Number(
                  PartyListDetail::PartyFrameMetric::FirstRowY) +
                  m_partyFrameRowCount * PartyListDetail::PartyFrameDesign().Number(
                                             PartyListDetail::PartyFrameMetric::RowStep);
    const int frameWidth = PartyListDetail::PartyFrameLogicalSize(
        frameLocalWidth, ModernUiScreenRateX(), ModernUiScale());
    const int frameHeight = PartyListDetail::PartyFrameLogicalSize(
        frameLocalHeight, ModernUiScreenRateY(), ModernUiScale());
    const int logicalHeight = static_cast<int>(ModernUiViewportHeight() / ModernUiScreenRateY());
    const bool minimizeHovered = IsMouseInPartyBox(
        m_partyFrameX + PartyListDetail::PartyFrameLogicalOffset(
                            PartyListDetail::PartyFrameDesign().Number(
                                PartyListDetail::PartyFrameMetric::MinimizeX),
                            ModernUiScreenRateX(), ModernUiScale()),
        m_partyFrameY + PartyListDetail::PartyFrameLogicalOffset(
                            PartyListDetail::PartyFrameDesign().Number(
                                PartyListDetail::PartyFrameMetric::MinimizeY),
                            ModernUiScreenRateY(), ModernUiScale()),
        PartyListDetail::PartyFrameLogicalSpan(PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::MinimizeX),
                                               PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::MinimizeSize),
                                               ModernUiScreenRateX(), ModernUiScale()),
        PartyListDetail::PartyFrameLogicalSpan(PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::MinimizeY),
                                               PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::MinimizeSize),
                                               ModernUiScreenRateY(), ModernUiScale()));

    if (m_partyFrameDragging)
    {
        if (MouseLButtonPush || IsRepeat(VK_LBUTTON))
        {
            m_partyFrameX =
                std::clamp(MouseX - m_partyFrameDragOffsetX, 0.0F,
                           static_cast<float>(std::max(0, GetScreenWidth() - frameWidth)));
            m_partyFrameY =
                std::clamp(MouseY - m_partyFrameDragOffsetY, 0.0F,
                           static_cast<float>(std::max(0, logicalHeight - frameHeight)));
            SyncLeaveButtons();
            return false;
        }
        m_partyFrameDragging = false;
        return false;
    }

    if (minimizeHovered)
    {
        if (IsPress(VK_LBUTTON))
        {
            m_partyFrameMinimizePressed = true;
        }
        m_partyFrameMinimizeState =
            m_partyFrameMinimizePressed && (MouseLButtonPush || IsRepeat(VK_LBUTTON))
                ? ButtonVisualState::Down
                : ButtonVisualState::Over;
        if (IsRelease(VK_LBUTTON))
        {
            if (m_partyFrameMinimizePressed)
            {
                m_partyFrameMinimized = !m_partyFrameMinimized;
                const float nextLocalWidth =
                    m_partyFrameMinimized ? PartyListDetail::PartyFrameDesign().Number(
                                                PartyListDetail::PartyFrameMetric::HeaderWidth)
                                          : PartyListDetail::PartyFrameDesign().Number(
                                                PartyListDetail::PartyFrameMetric::MemberWidth);
                const float nextLocalHeight =
                    m_partyFrameMinimized ? PartyListDetail::PartyFrameDesign().Number(
                                                PartyListDetail::PartyFrameMetric::HeaderHeight)
                                          : PartyListDetail::PartyFrameDesign().Number(
                                                PartyListDetail::PartyFrameMetric::FirstRowY) +
                                                m_partyFrameRowCount *
                                                    PartyListDetail::PartyFrameDesign().Number(
                                                        PartyListDetail::PartyFrameMetric::RowStep);
                const int nextWidth = PartyListDetail::PartyFrameLogicalSize(
                    nextLocalWidth, ModernUiScreenRateX(), ModernUiScale());
                const int nextHeight = PartyListDetail::PartyFrameLogicalSize(
                    nextLocalHeight, ModernUiScreenRateY(), ModernUiScale());
                m_partyFrameX =
                    std::clamp(m_partyFrameX, 0.0F,
                               static_cast<float>(std::max(0, GetScreenWidth() - nextWidth)));
                m_partyFrameY =
                    std::clamp(m_partyFrameY, 0.0F,
                               static_cast<float>(std::max(0, logicalHeight - nextHeight)));
            }
            m_partyFrameMinimizePressed = false;
        }
        return false;
    }

    if (IsRelease(VK_LBUTTON))
    {
        m_partyFrameMinimizePressed = false;
    }
    m_partyFrameMinimizeState = ButtonVisualState::Up;

    const bool overHeader = IsMouseInPartyBox(
        m_partyFrameX, m_partyFrameY,
        PartyListDetail::PartyFrameLogicalSize(PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::HeaderWidth),
                                               ModernUiScreenRateX(), ModernUiScale()),
        PartyListDetail::PartyFrameLogicalSize(PartyListDetail::PartyFrameDesign().Number(
                                                   PartyListDetail::PartyFrameMetric::HeaderHeight),
                                               ModernUiScreenRateY(), ModernUiScale()));
    if (IsPress(VK_LBUTTON) && overHeader)
    {
        m_partyFrameDragging = true;
        m_partyFrameDragOffsetX = MouseX - m_partyFrameX;
        m_partyFrameDragOffsetY = MouseY - m_partyFrameY;
        return false;
    }
    return !overHeader;
}

bool CNewUIPartyListWindow::IsMouseInPartyBox(float x, float y, float width, float height) const
{
    return MouseX >= x && MouseX < x + width && MouseY >= y && MouseY < y + height;
}

void CNewUIPartyListWindow::SyncLeaveButtons()
{
    for (int i = 0; i < MAX_PARTYS; ++i)
    {
        const int left = static_cast<int>(m_partyFrameX) +
                         PartyListDetail::PartyFrameLogicalOffset(
                             PartyListDetail::PartyFrameDesign().Number(
                                 PartyListDetail::PartyFrameMetric::MemberX) +
                                 PartyListDetail::PartyFrameDesign().Number(
                                     PartyListDetail::PartyFrameMetric::LeaveX),
                             ModernUiScreenRateX(), ModernUiScale());
        const int top = static_cast<int>(m_partyFrameY) +
                        PartyListDetail::PartyFrameLogicalOffset(
                            PartyListDetail::PartyFrameDesign().Number(
                                PartyListDetail::PartyFrameMetric::FirstRowY) +
                                i * PartyListDetail::PartyFrameDesign().Number(
                                        PartyListDetail::PartyFrameMetric::RowStep) +
                                PartyListDetail::PartyFrameDesign().Number(
                                    PartyListDetail::PartyFrameMetric::LeaveY),
                            ModernUiScreenRateY(), ModernUiScale());
        const int width = PartyListDetail::PartyFrameLogicalSpan(
            PartyListDetail::PartyFrameDesign().Number(PartyListDetail::PartyFrameMetric::MemberX) +
                PartyListDetail::PartyFrameDesign().Number(
                    PartyListDetail::PartyFrameMetric::LeaveX),
            PartyListDetail::PartyFrameDesign().Number(
                PartyListDetail::PartyFrameMetric::LeaveSize),
            ModernUiScreenRateX(), ModernUiScale());
        const int height = PartyListDetail::PartyFrameLogicalSpan(
            PartyListDetail::PartyFrameDesign().Number(
                PartyListDetail::PartyFrameMetric::FirstRowY) +
                i * PartyListDetail::PartyFrameDesign().Number(
                        PartyListDetail::PartyFrameMetric::RowStep) +
                PartyListDetail::PartyFrameDesign().Number(
                    PartyListDetail::PartyFrameMetric::LeaveY),
            PartyListDetail::PartyFrameDesign().Number(
                PartyListDetail::PartyFrameMetric::LeaveSize),
            ModernUiScreenRateY(), ModernUiScale());
        m_BtnPartyExit[i].ChangeButtonInfo(left, top, width, height);
    }
}

float CNewUIPartyListWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::PartyHud;
}

void CNewUIPartyListWindow::OpenningProcess()
{
}

void CNewUIPartyListWindow::ClosingProcess()
{
}

bool CNewUIPartyListWindow::SelectCharacterInPartyList(PARTY_t *pMember)
{
    const auto heroClass = gCharacterManager.GetBaseClass(Hero->Class);
    if (heroClass != CLASS_ELF && heroClass != CLASS_WIZARD && heroClass != CLASS_SUMMONER)
    {
        return false;
    }

    const auto skill = CharacterAttribute->Skill[Hero->CurrentSkill];
    if (skill == AT_SKILL_HEALING || skill == AT_SKILL_HEALING_STR || skill == AT_SKILL_DEFENSE ||
        skill == AT_SKILL_DEFENSE_STR || skill == AT_SKILL_DEFENSE_MASTERY ||
        skill == AT_SKILL_ATTACK || skill == AT_SKILL_ATTACK_STR ||
        skill == AT_SKILL_ATTACK_MASTERY || skill == AT_SKILL_TELEPORT_ALLY ||
        skill == AT_SKILL_SOUL_BARRIER || skill == AT_SKILL_SOUL_BARRIER_STR ||
        skill == AT_SKILL_SOUL_BARRIER_PROFICIENCY || skill == AT_SKILL_ALICE_THORNS ||
        skill == AT_SKILL_RECOVER)
    {
        SelectedCharacter = pMember->index;
        return true;
    }
    return false;
}

bool CNewUIPartyListWindow::OwnsModernPointer(const SessionInputEvent &event) const
{
    if (event.kind != SessionInputEventKind::Pointer || !IsVisible())
        return false;
    const float width = m_partyFrameMinimized ? PartyListDetail::PartyFrameDesign().Number(
                                                    PartyListDetail::PartyFrameMetric::HeaderWidth)
                                              : PartyListDetail::PartyFrameDesign().Number(
                                                    PartyListDetail::PartyFrameMetric::MemberWidth);
    const float height =
        m_partyFrameMinimized
            ? PartyListDetail::PartyFrameDesign().Number(
                  PartyListDetail::PartyFrameMetric::HeaderHeight)
            : PartyListDetail::PartyFrameDesign().Number(
                  PartyListDetail::PartyFrameMetric::FirstRowY) +
                  m_partyFrameRowCount * PartyListDetail::PartyFrameDesign().Number(
                                             PartyListDetail::PartyFrameMetric::RowStep);
    const float x = event.x / ModernUiScreenRateX(), y = event.y / ModernUiScreenRateY();
    return m_partyFrameDragging ||
           (x >= m_partyFrameX && y >= m_partyFrameY &&
            x < m_partyFrameX + width * ModernUiScale() / ModernUiScreenRateX() &&
            y < m_partyFrameY + height * ModernUiScale() / ModernUiScreenRateY());
}

CUIGuildListBox::CUIGuildListBox(SessionKeeper &keeper) : CUITextListBox<GUILDLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 24;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bIsGuildMaster = FALSE;
    m_bNewTypeScrollBar = FALSE;

    m_iNumRenderLine = 18;
    SetPosition(460, 340);
    SetSize(170, 250);
}

void CUIGuildListBox::AddText(const wchar_t *pszID, BYTE Number, BYTE Server)
{
    if (pszID == nullptr || pszID[0] == '\0')
        return;

    if (GetLineNum() == 0)
    {
        if (wcscmp(pszID, Hero->ID) == 0)
            m_bIsGuildMaster = TRUE;
        else
            m_bIsGuildMaster = FALSE;
    }

    GUILDLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE + 1);
    //memcpy(text.m_szID, pszID, wcslen(pszID) + 1);
    text.m_Number = Number;
    text.m_Server = Server;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;
}

BOOL CUIGuildListBox::DoSubMouseAction()
{
    m_TextListIter = m_TextList.begin();
    for (int i = 0; i < m_iCurrentRenderEndLine; ++i, ++m_TextListIter)
        ;

    for (int i = 0; i < m_iNumRenderLine; ++i)
    {
        if (m_TextListIter == m_TextList.end())
            break;

        if (m_bIsGuildMaster == TRUE || wcscmp(m_TextListIter->m_szID, Hero->ID) == 0)
        {
            int iPos_y;
            if (GetLineNum() > m_iNumRenderLine)
                iPos_y = m_iPos_y - 16 - i * 13;
            else
                iPos_y = m_iPos_y - 16 - (i - GetLineNum() + m_iNumRenderLine) * 13;

            float fWidth = 12;
            float fHeight = 10;
            float x = (float)m_iPos_x + m_iWidth - 20 - fWidth;
            auto y = (float)iPos_y;
            if (MouseX >= x && MouseX < x + fWidth && MouseY >= y && MouseY < y + fHeight)
            {
                if (MouseLButtonPush)
                {
                    MouseLButtonPush = false;
                    PlayBuffer(SOUND_CLICK01);
                    DeleteGuildIndex = GetLineNum() - 1 - i - m_iCurrentRenderEndLine;
                    ErrorMessage = MESSAGE_DELETE_GUILD;
                    ClearInput(FALSE);
                    InputEnable = false;
                    InputNumber = 1;
                    InputTextMax[0] = g_iLengthAuthorityCode;
                    InputTextHide[0] = 1;
                }
            }
        }

        ++m_TextListIter;
    }

    return TRUE;
}

CUISimpleChatListBox::CUISimpleChatListBox(SessionKeeper &keeper)
    : CUITextListBox<WHISPER_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseMultiline = TRUE;
}

void CUISimpleChatListBox::AddText(const wchar_t *pszID, const wchar_t *pszText, int iType,
                                   int iColor)
{
    if (pszID[0] == '\0' && pszText[0] == '\0')
    {
        return;
    }

    WHISPER_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE + 1);
    //memcpy(text.m_szID, pszID, wcslen(pszID) + 1);
    text.m_iType = iType;
    text.m_iColor = iColor;
    text.m_uiEmptyLines = 0;

    wcsncpy(text.m_szText, pszText, MAX_TEXT_LENGTH + 1);
    //memcpy(text.m_szText, pszText, wcslen(pszText) + 1);
    m_TextList.push_front(text);
    CalcLineNum();
}

void CUISimpleChatListBox::CalcLineNum()
{
    if (m_dwParentUIID != 0)
    {
        CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(m_dwParentUIID);
        if (pWindow != nullptr)
        {
            switch (m_iResizeType)
            {
            case 0:
                if (m_iRelativeWidth == 0 && m_iRelativeHeight == 0)
                    break;
                else
                    SetSize(m_iRelativeWidth, m_iRelativeHeight);
                break;
            case 1:
                SetSize(pWindow->RWidth() + m_iRelativeWidth, m_iRelativeHeight);
                break;
            case 2:
                SetSize(m_iRelativeWidth, pWindow->RHeight() + m_iRelativeHeight);
                break;
            case 3:
                SetSize(pWindow->RWidth() + m_iRelativeWidth,
                        pWindow->RHeight() + m_iRelativeHeight);
                break;
            default:
                break;
            }
        }
    }

    m_RenderTextList.clear();
    std::deque<WHISPER_TEXT>::reverse_iterator iter;
    for (iter = m_TextList.rbegin(); iter != m_TextList.rend(); ++iter)
    {
        AddTextToRenderList(iter->m_szID, iter->m_szText, iter->m_iType, iter->m_iColor);
    }
}

void CUISimpleChatListBox::AddTextToRenderList(const wchar_t *pszID, const wchar_t *pszText,
                                               int iType, int iColor)
{
    if (pszID == nullptr || pszText == nullptr)
        return;

    WHISPER_TEXT text{};
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE + 1);
    //memcpy(text.m_szID, pszID, wcslen(pszID) + 1);
    text.m_iType = iType;
    text.m_iColor = iColor;
    text.m_uiEmptyLines = 0;

    if (wcslen(pszID) + wcslen(pszText) >= 20)
    {
        SIZE nameSize;
        g_RenderText.MeasureText(pszID, lstrlen(pszID), &nameSize);

        wchar_t Text1[10][MAX_TEXT_LENGTH + 1] = {{0}, {0}, {0}, {0}, {0}};
        int iLine = CutText3(pszText, Text1[0], m_iWidth - 30, 10, MAX_TEXT_LENGTH + 1,
                             (nameSize.cx + 5) / g_fScreenRate_x);

        if (Text1[0][0] != '\0')
        {
            wcsncpy(text.m_szText, Text1[0], MAX_TEXT_LENGTH + 1);
            //memcpy(text.m_szText, Text1[0], wcslen(Text1[0]) + 2);
            m_RenderTextList.push_front(text);
        }

        for (int i = 1; i < iLine; ++i)
        {
            if (Text1[i][0] != '\0')
            {
                text.m_szID[0] = '\0';
                //memcpy(text.m_szID, L"", 1);
                wcsncpy(text.m_szText, Text1[i], MAX_TEXT_LENGTH + 1);
                //memcpy(text.m_szText, Text1[i], wcslen(Text1[i]) + 2);
                m_RenderTextList.push_front(text);
            }
        }
    }
    else
    {
        wcsncpy(text.m_szText, pszText, MAX_TEXT_LENGTH + 1);
        //memcpy(text.m_szText, pszText, wcslen(pszText) + 1);
        m_RenderTextList.push_front(text);
    }
}

CUIChatPalListBox::CUIChatPalListBox(SessionKeeper &keeper) : CUITextListBox<GUILDLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_iLayoutType = 0;

    m_bUseSelectLine = TRUE;

    m_iColumnWidth[0] = m_iColumnWidth[1] = m_iColumnWidth[2] = m_iColumnWidth[3] = 0;
    SIZE TextSize;
    g_RenderText.MeasureText(L"ZZZZZZZZZZZZZ", lstrlen(L"ZZZZZZZZZZZZZ"), &TextSize);
    SetColumnWidth(0, TextSize.cx / g_fScreenRate_x + 8);
    g_RenderText.MeasureText(I18N::Game::Server, wcslen(I18N::Game::Server), &TextSize);
    SetColumnWidth(1, TextSize.cx / g_fScreenRate_x + 8);

    m_bForceEditList = FALSE;
}

void CUIChatPalListBox::AddText(const wchar_t *pszID, BYTE Number, BYTE Server)
{
    if (wcscmp(pszID, L"") == 0)
        return;

    GUILDLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE);
    text.m_szID[MAX_USERNAME_SIZE] = '\0';
    //memcpy(text.m_szID, pszID, wcslen(pszID) + 1);
    text.m_Number = Number;
    text.m_Server = Server;
    m_TextList.push_front(text);

    RemoveText();
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(1);
        m_iCurrentRenderEndLine = 0;
    }
    m_bForceEditList = FALSE;
}

void CUIChatPalListBox::DeleteText(const wchar_t *pszID)
{
    if (pszID == nullptr || wcslen(pszID) == 0)
        return;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (wcsncmp(m_TextListIter->m_szID, pszID, MAX_USERNAME_SIZE) == 0)
            break;
    }
    if (m_TextListIter == m_TextList.end())
        return;

    if (m_bForceEditList == TRUE || m_TextList.size() == 1)
    {
        m_TextList.erase(m_TextListIter);
        SLSetSelectLine(0);
        return;
    }
    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();
    m_TextList.erase(m_TextListIter);
}

const wchar_t *CUIChatPalListBox::GetNameByNumber(BYTE byNumber)
{
    if (byNumber == 255)
        return nullptr;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (m_TextListIter->m_Number == byNumber)
            return m_TextListIter->m_szID;
    }
    return nullptr;
}

void CUIChatPalListBox::MakeTitleText(wchar_t *pszTitleText)
{
    if (pszTitleText == nullptr || m_TextList.empty() == TRUE)
        return;
    int iNameNum = 0;
    std::deque<GUILDLIST_TEXT>::reverse_iterator riter;
    for (riter = m_TextList.rbegin(); riter != m_TextList.rend(); ++riter)
    {
        if (wcsncmp(riter->m_szID, Hero->ID, MAX_USERNAME_SIZE) != 0)
        {
            if (iNameNum > 0)
            {
                wcscat(pszTitleText, L", L");
            }

            wcsncat(pszTitleText, riter->m_szID, MAX_USERNAME_SIZE);
            ++iNameNum;

            if (iNameNum >= 3)
            {
                wcscat(pszTitleText, L"...");
                break;
            }
        }
    }
}

void CUIChatPalListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIChatPalListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIChatPalListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
        if (MouseLButtonDBClick)
        {
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_LISTDBLCLICK, GetUIID(),
                                                0);
            MouseLButtonDBClick = false;
        }
    }
    return TRUE;
}

CUIWindowListBox::CUIWindowListBox(SessionKeeper &keeper) : CUITextListBox<WINDOWLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;
}

void CUIWindowListBox::AddText(DWORD dwUIID, const wchar_t *pszTitle, int iStatus)
{
    WINDOWLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    text.m_dwUIID = dwUIID;
    memset(text.m_szTitle, 0, sizeof(text.m_szTitle));
    wcsncpy(text.m_szTitle, pszTitle, 64);
    text.m_iStatus = iStatus;

    m_TextList.push_front(text);
    SLSetSelectLine(0);

    if (m_iCurrentRenderEndLine != 0)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(1);
        m_iCurrentRenderEndLine = 0;
    }
}

void CUIWindowListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIWindowListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIWindowListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_LISTDBLCLICK, GetUIID(),
                                                0);
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIWindowListBox::DeleteText(DWORD dwUIID)
{
    BOOL bFind = FALSE;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (m_TextListIter->m_dwUIID == dwUIID)
        {
            bFind = TRUE;
            break;
        }
    }
    if (bFind == FALSE)
        return;

    if (m_TextList.size() == 1)
    {
        m_TextList.erase(m_TextListIter);
        SLSetSelectLine(0);
        return;
    }
    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();
    m_TextList.erase(m_TextListIter);
}

CUILetterListBox::CUILetterListBox(SessionKeeper &keeper) : CUITextListBox<LETTERLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    m_iColumnWidth[0] = m_iColumnWidth[1] = m_iColumnWidth[2] = m_iColumnWidth[3] = 0;
    SIZE TextSize;

    SetColumnWidth(0, 15 + 10);
    g_RenderText.MeasureText(I18N::Game::Sender, wcslen(I18N::Game::Sender), &TextSize);
    SetColumnWidth(1, TextSize.cx / g_fScreenRate_x + 8);
    g_RenderText.MeasureText(I18N::Game::DateRcvd, wcslen(I18N::Game::DateRcvd), &TextSize);
    SetColumnWidth(2, TextSize.cx / g_fScreenRate_x + 8);
    g_RenderText.MeasureText(I18N::Game::Title1030, wcslen(I18N::Game::Title1030), &TextSize);
    SetColumnWidth(3, TextSize.cx / g_fScreenRate_x + 8);

    m_bForceEditList = FALSE;
}

void CUILetterListBox::AddText(const wchar_t *pszID, const wchar_t *pszText, const wchar_t *pszDate,
                               const wchar_t *pszTime, BOOL bIsRead)
{
    LETTERLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE + 1);
    wcsncpy(text.m_szText, pszText, MAX_TEXT_LENGTH + 1);
    wcsncpy(text.m_szDate, pszDate, 16);
    wcsncpy(text.m_szTime, pszTime, 16);
    text.m_bIsRead = bIsRead;
    text.m_dwLetterID = ++g_dwLastLetterID;

    m_TextList.push_front(text);
    SLSetSelectLine(0);

    if (m_iCurrentRenderEndLine != 0)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(1);
        m_iCurrentRenderEndLine = 0;
    }
    m_bForceEditList = FALSE;
}

void CUILetterListBox::DeleteText(DWORD dwLetterID)
{
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (m_TextListIter->m_dwLetterID == dwLetterID)
            break;
    }

    if (m_bForceEditList == TRUE || m_TextList.size() == 1)
    {
        m_TextList.erase(m_TextListIter);
        SLSetSelectLine(0);
        return;
    }
    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();
    m_TextList.erase(m_TextListIter);
}

void CUILetterListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUILetterListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUILetterListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3, 12, 13))
            {
                m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2;
                LETTERLIST_TEXT *lt = g_pLetterList->GetLetter(m_TextListIter->m_dwLetterID);
                lt->m_bIsSelected = m_TextListIter->m_bIsSelected;
            }
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            MouseLButtonPush = false;
        }
        if (MouseLButtonDBClick && MouseX > m_iPos_x + 12)
        {
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_LISTDBLCLICK, GetUIID(),
                                                0);
            MouseLButtonDBClick = false;
        }
    }
    return TRUE;
}

CUILetterTextListBox::CUILetterTextListBox(SessionKeeper &keeper)
    : CUITextListBox<LETTER_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseMultiline = TRUE;
    m_iScrollType = UILISTBOX_SCROLL_UPDOWN;
}

void CUILetterTextListBox::AddText(const wchar_t *pszText)
{
    if (pszText == nullptr || pszText[0] == '\0')
    {
        return;
    }

    LETTER_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szText, pszText, MAX_LETTERTEXT_LENGTH + 1);
    m_TextList.push_front(text);
    SLSetSelectLine(0);
    CalcLineNum();
}

void CUILetterTextListBox::CalcLineNum()
{
    if (m_dwParentUIID != 0)
    {
        CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(m_dwParentUIID);
        if (pWindow != nullptr)
        {
            switch (m_iResizeType)
            {
            case 0:
                if (m_iRelativeWidth == 0 && m_iRelativeHeight == 0)
                    break;
                else
                    SetSize(m_iRelativeWidth, m_iRelativeHeight);
                break;
            case 1:
                SetSize(pWindow->RWidth() + m_iRelativeWidth, m_iRelativeHeight);
                break;
            case 2:
                SetSize(m_iRelativeWidth, pWindow->RHeight() + m_iRelativeHeight);
                break;
            case 3:
                SetSize(pWindow->RWidth() + m_iRelativeWidth,
                        pWindow->RHeight() + m_iRelativeHeight);
                break;
            default:
                break;
            }
        }
    }

    m_RenderTextList.clear();
    std::deque<LETTER_TEXT>::reverse_iterator iter;
    for (iter = m_TextList.rbegin(); iter != m_TextList.rend(); ++iter)
    {
        AddTextToRenderList(iter->m_szText);
    }
}

void CUILetterTextListBox::AddTextToRenderList(const wchar_t *pszText)
{
    if (pszText == nullptr)
        return;

    LETTER_TEXT text{};
    if (wcslen(pszText) >= 20)
    {
        wchar_t Text1[80][MAX_TEXT_LENGTH + 1]{};
        int iLine = CutText3(pszText, Text1[0], m_iWidth - 30, 80, MAX_TEXT_LENGTH + 1);

        for (int i = 0; i < iLine; ++i)
        {
            if (Text1[i][0] != '\0')
            {
                wcsncpy(text.m_szText, Text1[i], MAX_TEXT_LENGTH + 1);
                m_RenderTextList.push_front(text);
            }
        }
    }
    else
    {
        wcsncpy(text.m_szText, pszText, MAX_TEXT_LENGTH + 1);
        m_RenderTextList.push_front(text);
    }
}

int CUILetterTextListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

CUISocketListBox::CUISocketListBox(SessionKeeper &keeper) : CUITextListBox<SOCKETLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;
    m_bUseNewUIScrollBar = TRUE;
    m_iScrollType = UILISTBOX_SCROLL_UPDOWN;

    SetPosition(275, 360);
    SetSize(160, 65);
}

void CUISocketListBox::AddText(int iSocketIndex, const wchar_t *pszText)
{
    SOCKETLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    text.m_iSocketIndex = iSocketIndex;
    wcsncpy(text.m_szText, pszText, 64);

    m_TextList.push_front(text);
    SLSetSelectLine(0);

    if (m_iCurrentRenderEndLine != 0)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(1);
        m_iCurrentRenderEndLine = 0;
    }
}

void CUISocketListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUISocketListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUISocketListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            g_pWindowMgr->SendUIMessageToWindow(GetParentUIID(), UI_MESSAGE_LISTDBLCLICK, GetUIID(),
                                                0);
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUISocketListBox::DeleteText(int iSocketIndex)
{
    BOOL bFind = FALSE;
    for (m_TextListIter = m_TextList.begin(); m_TextListIter != m_TextList.end(); ++m_TextListIter)
    {
        if (m_TextListIter->m_iSocketIndex == iSocketIndex)
        {
            bFind = TRUE;
            break;
        }
    }
    if (bFind == FALSE)
        return;

    if (m_TextList.size() == 1)
    {
        m_TextList.erase(m_TextListIter);
        SLSetSelectLine(0);
        return;
    }
    if (SLGetSelectLineNum() != 1)
        SLSelectNextLine();
    m_TextList.erase(m_TextListIter);
}

// Symmetric counterpart to GiveFocus(): drops keyboard focus from the focused
// portable text field without hiding or destroying it. GiveFocus() sets both
// s_pFocusedPortable and g_dwKeyFocusUIID, so release both here (clearing the
// key-focus id only while it still points at this field, to avoid stomping
// another widget), letting the field hand focus back to the game window while
// staying visible.
// and wherever a paragraph exceeds the box width (wrapped at the last space, or
// mid-word when a single word is too long). Each span is [start, end) in buffer
// indices; end excludes the wrapped space or newline.

CUIGuildNoticeListBox::CUIGuildNoticeListBox(SessionKeeper &keeper)
    : CUITextListBox<GUILDLOG_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;
}

void CUIGuildNoticeListBox::AddText(const wchar_t *szContent)
{
    if (szContent == nullptr || szContent[0] == '\0')
        return;

    GUILDLOG_TEXT text{};
    wcscpy(text.m_szContent, szContent);
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        Scrolling(-10000);
    }
}

void CUIGuildNoticeListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIGuildNoticeListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIGuildNoticeListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIGuildNoticeListBox::DeleteText(DWORD dwGuildIndex)
{
}

CUINewGuildMemberListBox::CUINewGuildMemberListBox(SessionKeeper &keeper)
    : CUITextListBox<GUILDLIST_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 18;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    SetPosition(466, 360);
    SetSize(157, 235);
}

void CUINewGuildMemberListBox::AddText(const wchar_t *pszID, BYTE Number, BYTE Server,
                                       BYTE GuildStatus)
{
    if (pszID == nullptr || pszID[0] == '\0')
        return;

    if (GetLineNum() == 0)
    {
        if (wcscmp(pszID, Hero->ID) == 0)
            m_bIsGuildMaster = TRUE;
        else
            m_bIsGuildMaster = FALSE;
    }

    GUILDLIST_TEXT text{};
    text.m_bIsSelected = FALSE;
    wcsncpy(text.m_szID, pszID, MAX_USERNAME_SIZE + 1);
    //memcpy(text.m_szID, pszID, wcslen(pszID) + 1);
    text.m_Number = Number;
    text.m_Server = Server;
    text.m_GuildStatus = GuildStatus;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    //	if (m_iCurrentRenderEndLine != 0) ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        //		m_iCurrentRenderEndLine = 0;
        Scrolling(-10000);
    }
}

void CUINewGuildMemberListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUINewGuildMemberListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUINewGuildMemberListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUINewGuildMemberListBox::DeleteText(DWORD dwUIID)
{
}

CUIUnionGuildListBox::CUIUnionGuildListBox(SessionKeeper &keeper)
    : CUITextListBox<UNIONGUILD_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = 6;

    m_fScrollBarRange_top = 0;
    m_fScrollBarRange_bottom = 0;

    m_fScrollBarPos_y = 0;
    m_fScrollBarWidth = 13;
    m_fScrollBarHeight = 0;

    m_fScrollBarClickPos_y = 0;
    m_bScrollBtnClick = FALSE;
    m_bScrollBarClick = FALSE;

    m_bUseSelectLine = TRUE;

    SetPosition(466, 360);
    SetSize(157, 235);
}

void CUIUnionGuildListBox::AddText(BYTE *pGuildMark, const wchar_t *szGuildName, int nMemberCount)
{
    if (szGuildName == nullptr || szGuildName[0] == '\0')
        return;

    UNIONGUILD_TEXT text{};
    text.m_bIsSelected = FALSE;
    memcpy(text.GuildMark, pGuildMark, sizeof(BYTE) * 64);
    wcsncpy(text.szName, szGuildName, MAX_GUILDNAME);
    text.szName[MAX_GUILDNAME] = 0;
    text.nMemberCount = nMemberCount;
    m_TextList.push_front(text);

    RemoveText();
    SLSetSelectLine(0);
    if (GetLineNum() > m_iNumRenderLine)
        ++m_iCurrentRenderEndLine;

    //	if (m_iCurrentRenderEndLine != 0) ++m_iCurrentRenderEndLine;

    if (GetLineNum() < m_iNumRenderLine)
        ;
    else if (GetLineNum() - m_iCurrentRenderEndLine < m_iNumRenderLine)
        m_iCurrentRenderEndLine = GetLineNum() - m_iNumRenderLine;

    if (m_TextList.size() == 1)
        SLSetSelectLine(1);

    if (m_TextList.empty() == FALSE)
    {
        SLSetSelectLine(GetLineNum());
        //		m_iCurrentRenderEndLine = 0;
        Scrolling(-10000);
    }
}

void CUIUnionGuildListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;
    m_iNumRenderLine = iLine;
}

int CUIUnionGuildListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIUnionGuildListBox::DoLineMouseAction(int iLineNumber)
{
    if (CheckMouseIn(m_iPos_x, GetRenderLinePos_y(iLineNumber) - 3,
                     m_iWidth - m_fScrollBarWidth + 1, 13))
    {
        if (MouseLButtonPush)
        {
            SLSetSelectLine(m_iCurrentRenderEndLine + iLineNumber + 1);
            m_TextListIter->m_bIsSelected = (m_TextListIter->m_bIsSelected + 1) % 2; // T/F Reverse
            MouseLButtonPush = false;
        }
    }
    return TRUE;
}

void CUIUnionGuildListBox::DeleteText(DWORD dwGuildIndex)
{
}

int CUIUnionGuildListBox::GetTextCount()
{
    return m_TextList.size();
}

// Feature controller methods consolidated from shared UI buckets.
#pragma pack(push)
#pragma pack()
SEASON3B::CNewUIChatInputBox::CNewUIChatInputBox(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay_(GameplayForConstruction()),
      m_blockedChatList(L"Data\\block_chat.txt")
{
    Init();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
SEASON3B::CNewUIChatInputBox::~CNewUIChatInputBox()
{
    Release();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::Init()
{
    m_pNewUIMng = nullptr;
    m_pNewUIChatLogWnd = nullptr;
    m_pNewUISystemLogWnd = nullptr;
    m_pChatInputBox = nullptr;
    m_pWhsprIDInputBox = nullptr;
    m_WndPos = {};
    m_WndPos.x = m_WndPos.y = 0;
    m_iCurChatHistory = 0;
    m_iCurWhisperIDHistory = 0;

    m_iTooltipType = INPUT_TOOLTIP_NOTHING;
    m_iInputMsgType = INPUT_CHAT_MESSAGE;
    m_bBlockWhisper = false;
    m_bShowSystemMessages = true;
    m_bShowChatLog = true;
    m_bWhisperSend = true;

    m_bShowMessageElseNormal = false;
    m_modernState = {};
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::Create(CNewUIManager *pNewUIMng,
                                          CNewUIChatLogWindow *pNewUIChatLogWnd,
                                          CNewUISystemLogWindow *pNewUISystemLogWnd, int x, int y)
{
    Release();

    if (nullptr == pNewUIChatLogWnd || nullptr == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CHATINPUTBOX, this);

    m_pNewUIChatLogWnd = pNewUIChatLogWnd;
    m_pNewUISystemLogWnd = pNewUISystemLogWnd;
    SetWndPos(x, y);

    m_pChatInputBox = new CUITextInputBox(SessionOrigin());
    m_pChatInputBox->Init(0, 0, MAX_CHAT_SIZE - 1);
    m_pChatInputBox->SetFont(LegacyFontRole::Normal);
    m_pChatInputBox->SetState(UISTATE_HIDE);

    m_pWhsprIDInputBox = new CUITextInputBox(SessionOrigin());
    m_pWhsprIDInputBox->Init(0, 0, MAX_USERNAME_SIZE);
    m_pWhsprIDInputBox->SetFont(LegacyFontRole::Normal);
    m_pWhsprIDInputBox->SetState(UISTATE_HIDE);

    m_pChatInputBox->SetTabTarget(m_pWhsprIDInputBox);
    m_pWhsprIDInputBox->SetTabTarget(m_pChatInputBox);

    SetInputMsgType(INPUT_CHAT_MESSAGE);

    m_blockedChatList.Load();
    m_pNewUIChatLogWnd->ModernPanel().SetBlockedUsers(m_blockedChatList.Names());

    Show(false);

    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::Release()
{
    RemoveAllChatHIstory();
    RemoveAllWhsprIDHIstory();

    SAFE_DELETE(m_pChatInputBox);
    SAFE_DELETE(m_pWhsprIDInputBox);

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }

    Init();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetWndPos(int x, int y)
{
    m_WndPos.x = x;
    m_WndPos.y = y;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetInputMsgType(int iInputMsgType)
{
    m_iInputMsgType = iInputMsgType;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
int SEASON3B::CNewUIChatInputBox::GetInputMsgType() const
{
    return m_iInputMsgType;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetFont(LegacyFontRole role)
{
    m_pChatInputBox->SetFont(role);
    m_pWhsprIDInputBox->SetFont(role);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::HaveFocus()
{
    if (m_pNewUIChatLogWnd != nullptr)
    {
        return m_pNewUIChatLogWnd->ModernPanel().HasTextInputFocus();
    }
    return (m_pChatInputBox != nullptr && m_pChatInputBox->HaveFocus()) ||
           (m_pWhsprIDInputBox != nullptr && m_pWhsprIDInputBox->HaveFocus());
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::AddChatHistory(const type_string &strText)
{
    if (!m_vecChatHistory.empty() && m_vecChatHistory.back() == strText)
        return;
    if (m_vecChatHistory.size() >= 12)
        m_vecChatHistory.erase(m_vecChatHistory.begin());
    m_vecChatHistory.push_back(strText);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::RemoveChatHistory(int index)
{
    if (index >= 0 && index < (int)m_vecChatHistory.size())
        m_vecChatHistory.erase(m_vecChatHistory.begin() + index);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::RemoveAllChatHIstory()
{
    m_vecChatHistory.clear();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::AddWhsprIDHistory(const type_string &strWhsprID)
{
    if (!m_vecWhsprIDHistory.empty() && m_vecWhsprIDHistory.back() == strWhsprID)
        return;
    if (m_vecWhsprIDHistory.size() >= 5)
        m_vecWhsprIDHistory.erase(m_vecWhsprIDHistory.begin());
    m_vecWhsprIDHistory.push_back(strWhsprID);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::RemoveWhsprIDHistory(int index)
{
    if (index >= 0 && index < (int)m_vecWhsprIDHistory.size())
    {
        m_vecWhsprIDHistory.erase(m_vecWhsprIDHistory.begin() + index);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::RemoveAllWhsprIDHIstory()
{
    m_vecWhsprIDHistory.clear();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::IsBlockWhisper()
{
    return m_bBlockWhisper;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetBlockWhisper(bool bBlockWhisper)
{
    m_bBlockWhisper = bBlockWhisper;
    m_modernState.blockWindowOpen = bBlockWhisper;
    if (m_pNewUIChatLogWnd != nullptr)
    {
        m_pNewUIChatLogWnd->SetBlockChatOpen(bBlockWhisper);
        if (bBlockWhisper && m_pNewUIMng != nullptr)
            m_pNewUIMng->BringToFront(m_pNewUIChatLogWnd);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::IsBlockedUser(const wchar_t *name) const noexcept
{
    return name != nullptr && m_blockedChatList.Contains(name);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::ProcessModernUiInput(const SessionInputEvent &event)
{
    if (m_pNewUIChatLogWnd == nullptr)
        return false;
    const bool handled = m_pNewUIChatLogWnd->ModernPanel().ProcessInput(event);
    if (handled && event.action == SessionInputAction::PointerButton && event.pressed &&
        m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->BringToFront(m_pNewUIChatLogWnd);
    }
    return handled;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
std::optional<UI::Modern::RmlTextInputArea> SEASON3B::CNewUIChatInputBox::ModernTextInputArea()
    const
{
    return m_pNewUIChatLogWnd != nullptr ? m_pNewUIChatLogWnd->ModernPanel().TextInputArea()
                                         : std::nullopt;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::ApplyHistoryDelta(int delta, bool whisper)
{
    auto &history = whisper ? m_vecWhsprIDHistory : m_vecChatHistory;
    int &index = whisper ? m_iCurWhisperIDHistory : m_iCurChatHistory;
    if (history.empty() || delta == 0)
        return;
    index += delta;
    if (index < 0)
        index = static_cast<int>(history.size()) - 1;
    if (index >= static_cast<int>(history.size()))
        index = 0;
    if (whisper)
        m_pNewUIChatLogWnd->ModernPanel().SetWhisperTarget(history[index]);
    else
        m_pNewUIChatLogWnd->ModernPanel().SetMainText(history[index]);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::UpdateModernActions()
{
    if (m_pNewUIChatLogWnd == nullptr)
        return true;
    auto actions = m_pNewUIChatLogWnd->ModernPanel().TakeActions();
    bool clicked = false;
    if (actions.toggleMenu)
    {
        m_modernState.menuExpanded = !m_modernState.menuExpanded;
        clicked = true;
    }
    if (actions.toggleBlock)
    {
        m_modernState.blockWindowOpen = !m_modernState.blockWindowOpen;
        clicked = true;
    }
    if (actions.closeBlock)
    {
        m_modernState.blockWindowOpen = false;
        clicked = true;
    }
    const auto applyFilter = [this, &clicked](bool toggle,
                                              UI::Modern::PC::Chat::RmlChatMessageType type) {
        if (!toggle)
            return;
        UI::Modern::PC::Chat::ToggleRmlChatMessageFilter(type, m_modernState);
        clicked = true;
    };
    using UI::Modern::PC::Chat::RmlChatMessageType;
    applyFilter(actions.toggleWhisper, RmlChatMessageType::Whisper);
    applyFilter(actions.toggleSystem, RmlChatMessageType::System);
    applyFilter(actions.toggleNormal, RmlChatMessageType::Normal);
    applyFilter(actions.toggleParty, RmlChatMessageType::Party);
    applyFilter(actions.toggleGuild, RmlChatMessageType::Guild);
    applyFilter(actions.toggleGens, RmlChatMessageType::Gens);
    m_iInputMsgType = INPUT_CHAT_MESSAGE + static_cast<int>(m_modernState.inputType);
    if (actions.toggleParty && m_modernState.showParty && PartyNumber <= 0)
    {
        g_pSystemLogBox->AddText(I18N::Game::CanOnlyBeUsedDuringParty,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
    if (actions.toggleGuild && m_modernState.showGuild && Hero->GuildStatus == G_NONE)
    {
        g_pSystemLogBox->AddText(I18N::Game::DoNotBelongToTheGuild, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
    if (actions.toggleGens && m_modernState.showGens && Hero->m_byGensInfluence == 0)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouHaveNotJoinedAGens, SEASON3B::TYPE_SYSTEM_MESSAGE);
    }
    if (actions.registerBlockedUser.has_value() &&
        m_blockedChatList.Add(*actions.registerBlockedUser))
    {
        m_pNewUIChatLogWnd->ModernPanel().SetBlockedUsers(m_blockedChatList.Names());
        clicked = true;
    }
    if (actions.deleteBlockedUser.has_value() &&
        m_blockedChatList.Remove(*actions.deleteBlockedUser))
    {
        m_pNewUIChatLogWnd->ModernPanel().SetBlockedUsers(m_blockedChatList.Names());
        clicked = true;
    }
    m_bBlockWhisper = m_modernState.blockWindowOpen;
    m_pNewUIChatLogWnd->SetBlockChatOpen(m_modernState.blockWindowOpen);
    if (actions.toggleBlock && m_modernState.blockWindowOpen && m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->BringToFront(m_pNewUIChatLogWnd);
    }
    m_bShowSystemMessages = m_modernState.showSystem;
    if (actions.cycleViewMode)
    {
        m_modernState.viewMode = UI::Modern::PC::Chat::NextRmlChatViewMode(m_modernState.viewMode);
        clicked = true;
    }
    if (actions.cycleSize)
    {
        m_modernState.sizeIndex =
            UI::Modern::PC::Chat::NextRmlChatSizeIndex(m_modernState.sizeIndex);
        clicked = true;
    }
    if (actions.cycleAlpha)
    {
        m_modernState.alpha = UI::Modern::PC::Chat::NextRmlChatAlpha(m_modernState.alpha);
        clicked = true;
    }
    if (actions.whisperTarget.has_value())
    {
        SetWhsprID(actions.whisperTarget->c_str());
    }
    ApplyHistoryDelta(actions.chatHistoryDelta, false);
    ApplyHistoryDelta(actions.whisperHistoryDelta, true);

    if (actions.open)
    {
        const bool canOpen = !gMapManager.InChaosCastle() ||
                             !g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHAOSCASTLE_TIME);
        if (canOpen)
            g_pNewUISystem->Show(SEASON3B::INTERFACE_CHATINPUTBOX);
    }
    if (actions.close)
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHATINPUTBOX);
        clicked = true;
    }
    if (actions.submit.has_value())
        SubmitModernChat(*actions.submit);
    if (clicked)
        PlayBuffer(SOUND_CLICK01);
    return !(actions.open || actions.close || actions.submit.has_value() || clicked ||
             actions.chatHistoryDelta != 0 || actions.whisperHistoryDelta != 0 ||
             actions.closeBlock || actions.registerBlockedUser.has_value() ||
             actions.deleteBlockedUser.has_value());
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SubmitModernChat(
    const UI::Modern::PC::Chat::RmlChatSubmit &submit)
{
    if (submit.text.empty())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHATINPUTBOX);
        return;
    }
    const std::uint64_t now = GetTickCount64();
    if (m_lastChatTime >= now - ChatCooldownMs)
        return;
    m_lastChatTime = now;

    std::wstring text = submit.text;
    if (text.size() > UI::Modern::PC::Chat::RmlChatInputLimit)
        text.resize(UI::Modern::PC::Chat::RmlChatInputLimit);
    if (text.front() != L'/' && submit.whisper.empty())
    {
        using namespace UI::Modern::PC::Chat;
        const RmlChatSendBlockReason blocked =
            RmlChatSendBlockReasonFor(m_modernState.inputType, PartyNumber > 0,
                                      Hero->GuildStatus != G_NONE, Hero->m_byGensInfluence != 0);
        const wchar_t *warning = nullptr;
        switch (blocked)
        {
        case RmlChatSendBlockReason::PartyRequired:
            warning = I18N::Game::CanOnlyBeUsedDuringParty;
            break;
        case RmlChatSendBlockReason::GuildRequired:
            warning = I18N::Game::DoNotBelongToTheGuild;
            break;
        case RmlChatSendBlockReason::GensRequired:
            warning = I18N::Game::YouHaveNotJoinedAGens;
            break;
        default:
            break;
        }
        if (warning != nullptr)
        {
            AddChatHistory(text);
            g_pSystemLogBox->AddText(warning, SEASON3B::TYPE_SYSTEM_MESSAGE);
            m_pNewUIChatLogWnd->ModernPanel().SetMainText(L"");
            m_iCurChatHistory = static_cast<int>(m_vecChatHistory.size());
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHATINPUTBOX);
            return;
        }
    }

    std::wstring wireText;
    if (text.front() != L'/')
    {
        switch (m_modernState.inputType)
        {
        case UI::Modern::PC::Chat::RmlChatInputType::Party:
            wireText = L"~";
            break;
        case UI::Modern::PC::Chat::RmlChatInputType::Guild:
            wireText = L"@";
            break;
        case UI::Modern::PC::Chat::RmlChatInputType::Gens:
            wireText = L"$";
            break;
        default:
            break;
        }
    }
    wireText += text;
    AddChatHistory(text);
    if (!submit.whisper.empty())
        AddWhsprIDHistory(submit.whisper);

    if (!CheckCommand(text.data()))
    {
        if (!submit.whisper.empty())
        {
            SocketClient->ToGameServer()->SendWhisperMessage(submit.whisper.c_str(),
                                                             wireText.c_str());
            g_pChatListBox->AddText(Hero->ID, text, SEASON3B::TYPE_WHISPER_MESSAGE);
        }
        else if (text.starts_with(I18N::Game::Warp))
        {
            wchar_t *mapName = text.data() + wcslen(I18N::Game::Warp) + 1;
            const int map = g_pMoveCommandWindow->GetMapIndexFromMovereq(mapName);
            if (g_pMoveCommandWindow->IsTheMapInDifferentServer(gMapManager.ContextMap(), map))
            {
                SaveOptions();
            }
            SocketClient->ToGameServer()->SendWarpCommandRequest(
                g_pMoveCommandWindow->GetMoveCommandKey(), map);
        }
        else
        {
            if (Hero->SafeZone || (Hero->Helper.Type != MODEL_HORN_OF_UNIRIA &&
                                   Hero->Helper.Type != MODEL_HORN_OF_DINORANT &&
                                   Hero->Helper.Type != MODEL_DARK_HORSE_ITEM &&
                                   Hero->Helper.Type != MODEL_HORN_OF_FENRIR))
            {
                gameplay_.CheckChatText(text.data());
            }
            SocketClient->ToGameServer()->SendPublicChatMessage(Hero->ID, wireText.c_str());
            if (m_modernState.inputType == UI::Modern::PC::Chat::RmlChatInputType::Normal)
            {
                g_pChatListBox->AddText(Hero->ID, text, SEASON3B::TYPE_CHAT_MESSAGE);
            }
        }
    }
    m_pNewUIChatLogWnd->ModernPanel().SetMainText(L"");
    m_iCurChatHistory = static_cast<int>(m_vecChatHistory.size());
    m_iCurWhisperIDHistory = static_cast<int>(m_vecWhsprIDHistory.size());
    g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHATINPUTBOX);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::UpdateMouseEvent()
{
    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::UpdateKeyEvent()
{
    return m_pNewUIChatLogWnd == nullptr || UpdateModernActions();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool SEASON3B::CNewUIChatInputBox::Update()
{
    UpdateWhisperTargetFromRightClick();
    UpdateModernActions();
    return true;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
float SEASON3B::CNewUIChatInputBox::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::ChatInput;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
float SEASON3B::CNewUIChatInputBox::GetKeyEventOrder()
{
    return 9.0f;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::OpenningProcess()
{
    if (m_pNewUIChatLogWnd != nullptr)
    {
        m_modernState.editing = true;
        RestoreIMEStatus();
        return;
    }

    // Set the state before focusing: a portable field ignores GiveFocus() while
    // still hidden, so focusing after showing lets Enter-to-open type right away.
    m_pChatInputBox->SetState(UISTATE_NORMAL);
    m_pChatInputBox->GiveFocus();
    m_pChatInputBox->SetText(L"");

    if (m_bWhisperSend == true)
    {
        m_pWhsprIDInputBox->SetState(UISTATE_NORMAL);
    }
    else
    {
        m_pWhsprIDInputBox->SetState(UISTATE_HIDE);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::ClosingProcess()
{
    if (m_pNewUIChatLogWnd != nullptr)
    {
        m_modernState.editing = false;
        SaveIMEStatus();
        return;
    }

    m_pNewUIChatLogWnd->HideFrame();

    m_pChatInputBox->SetState(UISTATE_HIDE);
    m_pWhsprIDInputBox->SetState(UISTATE_HIDE);

    SetFocus(g_hWnd);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::GetChatText(type_string &strText)
{
    wchar_t szChatText[256];
    m_pChatInputBox->GetText(szChatText, 256);
    strText = szChatText;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::GetWhsprID(type_string &strWhsprID)
{
    wchar_t szWhisperID[32];
    m_pWhsprIDInputBox->GetText(szWhisperID, 32);
    strWhsprID = szWhisperID;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetWhsprID(const wchar_t *strWhsprID)
{
    if (m_pNewUIChatLogWnd != nullptr)
    {
        m_pNewUIChatLogWnd->ModernPanel().SetWhisperTarget(strWhsprID);
        return;
    }
    m_pWhsprIDInputBox->SetText(strWhsprID);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::UpdateWhisperTargetFromRightClick()
{
    if (SelectedCharacter < 0 || !IsRelease(VK_RBUTTON))
    {
        return;
    }

    auto const character = &CharactersClient[SelectedCharacter];
    if (character->Object.Kind != KIND_PLAYER)
    {
        return;
    }

    if (gMapManager.InChaosCastle())
    {
        return;
    }

    auto const blockedByGensRivalry = IsStrifeMap(gMapManager.ContextMap()) &&
                                      Hero->m_byGensInfluence != character->m_byGensInfluence;
    if (blockedByGensRivalry)
    {
        return;
    }

    SetWhsprID(character->ID);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetTextPosition(int x, int y)
{
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void SEASON3B::CNewUIChatInputBox::SetBuddyPosition(int x, int y)
{
}
#pragma pack(pop)

// Native feature window methods.
#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::InitControls()
{
    SIZE size;
    g_RenderText.MeasureText(I18N::Game::Receiver, wcslen(I18N::Game::Receiver), &size);

    size.cx = (size.cx / g_fScreenRate_x) + 0.5f;

    m_MailtoInputBox.Init(238, 14, 50);
    m_MailtoInputBox.SetParentUIID(m_dwUIID);
    m_MailtoInputBox.SetFont(LegacyFontRole::Normal);
    m_MailtoInputBox.SetOption(UIOPTION_NULL);
    m_MailtoInputBox.SetBackColor(0, 0, 0, 0);
    m_MailtoInputBox.SetTextLimit(MAX_USERNAME_SIZE);
    m_MailtoInputBox.SetParentUIID(GetUIID());
    m_MailtoInputBox.SetArrangeType(0, size.cx, 3);
    m_MailtoInputBox.SetState(UISTATE_NORMAL);

    m_TitleInputBox.Init(238, 14, 50);
    m_TitleInputBox.SetParentUIID(m_dwUIID);
    m_TitleInputBox.SetFont(LegacyFontRole::Normal);
    m_TitleInputBox.SetOption(UIOPTION_NULL);
    m_TitleInputBox.SetBackColor(0, 0, 0, 0);
    m_TitleInputBox.SetTextLimit(MAX_LETTER_TITLE_LENGTH - 1);
    m_TitleInputBox.SetParentUIID(GetUIID());
    m_TitleInputBox.SetArrangeType(0, size.cx, 18);
    m_TitleInputBox.SetState(UISTATE_NORMAL);

    m_TextInputBox.SetMultiline(TRUE);
    m_TextInputBox.Init(238, 135, 50);
    m_TextInputBox.SetTextLimit(MAX_LETTERTEXT_LENGTH - 1);
    m_TextInputBox.SetParentUIID(m_dwUIID);
    m_TextInputBox.SetFont(LegacyFontRole::Normal);
    m_TextInputBox.SetOption(UIOPTION_NULL);
    m_TextInputBox.SetBackColor(0, 0, 0, 0);
    m_TextInputBox.SetParentUIID(GetUIID());
    m_TextInputBox.SetArrangeType(0, 5, 33);
    m_TextInputBox.SetState(UISTATE_NORMAL);

    m_MailtoInputBox.SetTabTarget(&m_TitleInputBox);
    m_TitleInputBox.SetTabTarget(&m_TextInputBox);
    m_TextInputBox.SetTabTarget(&m_MailtoInputBox);

    m_SendButton.Init(1, I18N::Game::Send);
    m_SendButton.SetParentUIID(GetUIID());
    m_SendButton.SetArrangeType(2, 12, 16);
    m_SendButton.SetSize(50, 14);

    m_CloseButton.Init(2, I18N::Game::Close388);
    m_CloseButton.SetParentUIID(GetUIID());
    m_CloseButton.SetArrangeType(2, 63, 16);
    m_CloseButton.SetSize(50, 14);

    //	m_PhotoShowButton.Init(3, I18N::Game::EitherTheReceiverDoesNotExistOrThereIsNoMailBox);
    //	m_PhotoShowButton.SetParentUIID(GetUIID());
    //	m_PhotoShowButton.SetArrangeType(2, 114, 16);
    //	m_PhotoShowButton.SetSize(50, 14);

    m_PrevPoseButton.Init(4, I18N::Game::PrevAction);
    m_PrevPoseButton.SetParentUIID(GetUIID());
    m_PrevPoseButton.SetArrangeType(2, 250, 16);
    m_PrevPoseButton.SetSize(50, 14);

    m_NextPoseButton.Init(5, I18N::Game::NextAction);
    m_NextPoseButton.SetParentUIID(GetUIID());
    m_NextPoseButton.SetArrangeType(2, 301, 16);
    m_NextPoseButton.SetSize(50, 14);

    m_MailtoInputBox.GiveFocus();
    m_iLastTabIndex = 0;
    m_bHaveTextBox = TRUE;

    m_Photo.Init(0);
    m_Photo.SetOption(UIPHOTOVIEWER_CANCONTROL);
    m_Photo.SetParentUIID(GetUIID());
    m_Photo.SetArrangeType(1, 119, 30);
    m_Photo.SetSize(119, RHeight() - 49);
    m_Photo.CopyPlayer();
    m_Photo.SetAutoupdatePlayer(TRUE);
    m_Photo.SetAnimation(AT_STAND1);
    m_Photo.SetAngle(90);
    Refresh();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);

    SetPosition(UI::Modern::PC::Friend::RmlFriendInitialX(),
                UI::Modern::PC::Friend::RmlFriendInitialY());
    SetSize(250, 216);
    SetLimitSize(250, 150);
    SetOption(UIWINDOWSTYLE_TITLEBAR | UIWINDOWSTYLE_FRAME | UIWINDOWSTYLE_MOVEABLE |
              UIWINDOWSTYLE_MINBUTTON);

    m_iShowType = 1;
    SetSize(GetWidth() + 120, GetHeight());
    SetLimitSize(250 + 120, 150);
    m_ModernPanel.Create();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CUILetterWriteWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
std::optional<UI::Modern::RmlTextInputArea> CUILetterWriteWindow::ModernTextInputArea() const
{
    return m_ModernPanel.TextInputArea();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::Refresh()
{
    m_MailtoInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_TitleInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_TextInputBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_SendButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_CloseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    //m_PhotoShowButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_PrevPoseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_NextPoseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_Photo.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::SetMailtoText(const wchar_t *pszText)
{
    m_MailtoInputBox.SetText(pszText);
    m_TitleInputBox.GiveFocus();
    m_iLastTabIndex = 1;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::SetMainTitleText(const wchar_t *pszText)
{
    m_TitleInputBox.SetText(pszText);
    m_TextInputBox.GiveFocus();
    m_iLastTabIndex = 2;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::SetMailContextText(const wchar_t *pszText)
{
    m_TextInputBox.SetText(pszText);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
BOOL CUILetterWriteWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        switch (m_iLastTabIndex)
        {
        case 0:
            m_MailtoInputBox.GiveFocus();
            break;
        case 1:
            m_TitleInputBox.GiveFocus();
            m_iLastTabIndex = 0;
            break;
        case 2:
            m_TextInputBox.GiveFocus();
            m_iLastTabIndex = 0;
            break;
        default:
            break;
        }
        break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;

        DWORD dwUIID = 0;

        switch (m_WorkMessage.m_iParam1)
        {
        case 1:
            if (m_bIsSend == FALSE)
            {
                wchar_t szMailto[MAX_USERNAME_SIZE + 1] = {'\0'};
                wchar_t szTitle[MAX_LETTER_TITLE_LENGTH] = {'\0'};
                wchar_t szTempText[MAX_LETTERTEXT_LENGTH] = {'\0'};

                //std::wstring	strTitle = "", strText = "";
                std::wstring wstrTitle = L"", wstrText = L"";
                int k = 0;

                m_MailtoInputBox.GetText(szMailto, MAX_USERNAME_SIZE + 1);
                m_TitleInputBox.GetText(szTitle, MAX_LETTER_TITLE_LENGTH);
                m_TextInputBox.GetText(szTempText, MAX_LETTERTEXT_LENGTH);

                //for (k = 0; k < MAX_LETTER_TITLE_LENGTH_UTF16 + 1; k++)
                //    szTitleUTF16[k] = g_pMultiLanguage->ConvertFulltoHalfWidthChar(szTitleUTF16[k]);
                //for (k = 0; k < MAX_LETTER_TEXT_LENGTH_UTF16 + 1; k++)
                //    szTextUTF16[k] = g_pMultiLanguage->ConvertFulltoHalfWidthChar(szTextUTF16[k]);

                //wstrTitle = szTitleUTF16;
                //wstrText = szTextUTF16;

                // delete memory
                //delete[] szTitleUTF16;	delete[] szTextUTF16;

                //g_pMultiLanguage->ConvertWideCharToStr(strTitle, wstrTitle.c_str(), g_pMultiLanguage->GetCodePage());
                //g_pMultiLanguage->ConvertWideCharToStr(strText, wstrText.c_str(), g_pMultiLanguage->GetCodePage());
                //wcsncpy(szTitle, strTitle.c_str(), sizeof szTitle);
                //wcsncpy(szTempText, strText.c_str(), sizeof szTempText);

                //if (CheckAbuseFilter(wstrTitle, false))
                //    g_pMultiLanguage->ConvertCharToWideStr(wstrTitle, I18N::Game::PwnedByTheFilter);
                //if (CheckAbuseFilter(wstrText, false))
                //    g_pMultiLanguage->ConvertCharToWideStr(wstrText, I18N::Game::PwnedByTheFilter);

                //g_pMultiLanguage->ConvertWideCharToStr(strTitle, wstrTitle.c_str(), CP_UTF8);
                //g_pMultiLanguage->ConvertWideCharToStr(strText, wstrText.c_str(), CP_UTF8);
                /*wcsncpy(szTitle, strTitle.c_str(), sizeof szTitle);
                wcsncpy(szTempText, strText.c_str(), sizeof szTempText);*/

                if (szMailto[0] == '\0' || wcslen(szMailto) == 0)
                {
                    g_pWindowMgr->AddWindow(UIWNDTYPE_OK, UIWND_DEFAULT, UIWND_DEFAULT,
                                            I18N::Game::EnterTheNameOfTheReceiver);
                    m_MailtoInputBox.GiveFocus();
                    m_iLastTabIndex = 0;
                    break;
                }

                if (szTitle[0] == '\0' || wcslen(szTitle) == 0)
                {
                    g_pWindowMgr->AddWindow(UIWNDTYPE_OK, UIWND_DEFAULT, UIWND_DEFAULT,
                                            I18N::Game::EnterTheTitle);
                    m_TitleInputBox.GiveFocus();
                    m_iLastTabIndex = 1;
                    break;
                }

                if (szTempText[0] == '\0' || wcslen(szTempText) == 0)
                {
                    g_pWindowMgr->AddWindow(UIWNDTYPE_OK, UIWND_DEFAULT, UIWND_DEFAULT,
                                            I18N::Game::EnterYourMessage);
                    m_TextInputBox.GiveFocus();
                    m_iLastTabIndex = 2;
                    break;
                }

                wchar_t szText[1024] = {0};

                for (int i = 0, j = 0; i <= (int)wcslen(szTempText); ++i, ++j)
                {
                    if (j > MAX_LETTERTEXT_LENGTH || i > MAX_LETTERTEXT_LENGTH)
                        break;

                    if (szTempText[i] == '\r')
                    {
                        szText[j] = '\n';
                        if (szTempText[i + 1] == '\n' && szTempText[i + 2] == '\r' &&
                            szTempText[i + 3] == '\n')
                        {
                            szText[++j] = ' ';
                        }
                        ++i;
                    }
                    else
                    {
                        szText[j] = szTempText[i];
                    }
                }

                szText[MAX_LETTERTEXT_LENGTH] = '\0';
                WORD len = std::min<int>(MAX_LETTERTEXT_LENGTH, wcslen(szText));
                m_bIsSend = TRUE;
                int iAngle = m_Photo.GetCurrentAngle() / 6;
                int iZoom = (m_Photo.GetCurrentZoom() * 100.0f - 80 + 5) / 10;
                BYTE Data1 = (iZoom << 6) & 0xC0 | iAngle & 0x3F;
                BYTE Data2 = m_Photo.GetCurrentAction() - AT_ATTACK1;
                SocketClient->ToGameServer()->SendLetterSendRequest(GetUIID(), szMailto, szTitle,
                                                                    Data1, Data2, len, szText);
            }
            break;
        case 2:
            if (CloseCheck() == TRUE)
                g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
            break;
        case 3:
            break;
        case 4:
            m_Photo.ChangeAnimation(-1);
            break;
        case 5:
            m_Photo.ChangeAnimation(1);
            break;
        default:
            break;
        }
    }
    break;
    case UI_MESSAGE_YNRETURN:
        if (m_WorkMessage.m_iParam2 == 1)
        {
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
        }
        break;
    default:
        break;
    }
    return FALSE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::DoActionSub(BOOL bMessageOnly)
{
    m_MailtoInputBox.DoAction(bMessageOnly);
    m_TitleInputBox.DoAction(bMessageOnly);
    m_TextInputBox.DoAction(bMessageOnly);
    m_SendButton.DoAction(bMessageOnly);
    m_CloseButton.DoAction(bMessageOnly);
    //m_PhotoShowButton.DoAction(bMessageOnly);
    if (m_iShowType == 1)
    {
        m_PrevPoseButton.DoAction(bMessageOnly);
        m_NextPoseButton.DoAction(bMessageOnly);
        m_Photo.SetShowType(m_iShowType);
        m_Photo.DoAction(bMessageOnly);
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::DoMouseActionSub()
{
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
BOOL CUILetterWriteWindow::CloseCheck()
{
    wchar_t szTest[16] = {0};
    m_TitleInputBox.GetText(szTest, 15);
    int iTextSize = (szTest[0] == '\0' ? 0 : wcslen(szTest));
    m_TextInputBox.GetText(szTest, 15);
    iTextSize += (szTest[0] == '\0' ? 0 : wcslen(szTest));
    if (iTextSize == 0)
    {
        return TRUE;
    }
    else
    {
        g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                I18N::Game::DoYouWishToQuitWritingThisLetter, GetUIID());
        return FALSE;
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
CUILetterReadWindow::~CUILetterReadWindow()
{
    g_pWindowMgr->CloseLetterRead(m_LetterHead.m_dwLetterID);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);

    SetPosition(UI::Modern::PC::Friend::RmlFriendInitialX(),
                UI::Modern::PC::Friend::RmlFriendInitialY());
    //SetSize(213, 170);
    SetSize(250, 182);
    SetLimitSize(250, 182);
    SetOption(UIWINDOWSTYLE_TITLEBAR | UIWINDOWSTYLE_FRAME | UIWINDOWSTYLE_MOVEABLE |
              UIWINDOWSTYLE_MINBUTTON);

    m_LetterTextBox.SetParentUIID(GetUIID());
    m_LetterTextBox.SetArrangeType(2, 0, 20);
    m_LetterTextBox.SetResizeType(3, 0, -36);

    m_ReplyButton.Init(1, I18N::Game::Reply);
    m_ReplyButton.SetParentUIID(GetUIID());
    m_ReplyButton.SetArrangeType(2, 2, 16);
    m_ReplyButton.SetSize(50, 14);

    m_DeleteButton.Init(2, I18N::Game::Delete);
    m_DeleteButton.SetParentUIID(GetUIID());
    m_DeleteButton.SetArrangeType(2, 53, 16);
    m_DeleteButton.SetSize(50, 14);

    m_CloseButton.Init(3, I18N::Game::Close388);
    m_CloseButton.SetParentUIID(GetUIID());
    m_CloseButton.SetArrangeType(2, 186, 16);
    m_CloseButton.SetSize(50, 14);

    //	m_PhotoButton.Init(4, L">>");
    //	m_PhotoButton.SetParentUIID(GetUIID());
    //	m_PhotoButton.SetArrangeType(2, 186, 16);
    //	m_PhotoButton.SetSize(50, 14);

    m_PrevButton.Init(5, I18N::Game::Previous);
    m_PrevButton.SetParentUIID(GetUIID());
    m_PrevButton.SetArrangeType(2, 104, 16);
    m_PrevButton.SetSize(40, 14);

    m_NextButton.Init(6, I18N::Game::Next);
    m_NextButton.SetParentUIID(GetUIID());
    m_NextButton.SetArrangeType(2, 145, 16);
    m_NextButton.SetSize(40, 14);

    m_Photo.Init(0);
    m_Photo.SetOption(UIPHOTOVIEWER_CANCONTROL);
    m_Photo.SetParentUIID(GetUIID());
    m_Photo.SetArrangeType(1, 119, 15);
    m_Photo.SetResizeType(2, 119, -15);

    m_LetterTextBox.SetResizeType(3, 0 - 120, -36);
    SetSize(GetWidth() + 120, GetHeight());
    SetLimitSize(250 + 120, 182);
    m_ModernPanel.Create();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
bool CUILetterReadWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_ModernPanel.ProcessInput(event);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::Refresh()
{
    m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_ReplyButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_DeleteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_CloseButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    //m_PhotoButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_PrevButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_NextButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_Photo.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);

    m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_Photo.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::SetLetter(LETTERLIST_TEXT *pLetterHead, const wchar_t *pLetterText)
{
    memcpy(&m_LetterHead, pLetterHead, sizeof(LETTERLIST_TEXT));

    wchar_t *temp = new wchar_t[wcslen(pLetterText) + 20];
    wcsncpy(temp, pLetterText, wcslen(pLetterText) + 1);
    wchar_t *context = nullptr;
    wchar_t *token = wcstok_s(temp, L"\n", &context);
    while (token != NULL)
    {
        m_LetterTextBox.AddText(token);
        token = wcstok_s(NULL, L"\n", &context);
    }
    m_LetterTextBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
    delete[] temp;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
BOOL CUILetterReadWindow::HandleMessage()
{
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;
        DWORD dwUIID = 0;
        switch (m_WorkMessage.m_iParam1)
        {
        case 1: {
            wchar_t temp[MAX_TEXT_LENGTH + 1];
            mu_swprintf(temp, I18N::Game::WriteLetterCostDZen, g_cdwLetterCost);

            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_WRITELETTER, 100, 100, temp);
            if (dwUIID == 0)
                break;
            ((CUILetterWriteWindow *)g_pWindowMgr->GetWindow(dwUIID))
                ->SetMailtoText(m_LetterHead.m_szID);
            wchar_t szMailTitle[MAX_TEXT_LENGTH + 1] = {0};
            mu_swprintf(szMailTitle, I18N::Game::ReS, m_LetterHead.m_szText);
            wchar_t szMailTitleResult[32 + 1] = {0};
            CutText4(szMailTitle, szMailTitleResult, NULL, 32);
            ((CUILetterWriteWindow *)g_pWindowMgr->GetWindow(dwUIID))
                ->SetMainTitleText(szMailTitleResult);
        }
        break;
        case 2: {
            wchar_t tempTxt[MAX_TEXT_LENGTH + 1] = {0};
            wcscat(tempTxt, I18N::Game::AreYouSureYouWantToDeleteTheLetter);
            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                             tempTxt, GetUIID());
        }
        break;
        case 3:
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
            break;
        case 4:
            break;
        case 5: {
            DWORD dwPrevID = g_pLetterList->GetPrevLetterID(m_LetterHead.m_dwLetterID);
            if (dwPrevID != 0)
            {
                if (g_pWindowMgr->GetFriendMainWindow() != NULL)
                {
                    g_pWindowMgr->GetFriendMainWindow()->PrevNextCursorMove(
                        g_pLetterList->GetLineNum(dwPrevID));
                }
                g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
                g_pWindowMgr->CloseLetterRead(m_LetterHead.m_dwLetterID);
                g_iLetterReadNextPos_x = GetPosition_x();
                g_iLetterReadNextPos_y = GetPosition_y();
                if (g_pWindowMgr->LetterReadCheck(dwPrevID) == FALSE)
                {
                    if (g_pLetterList->GetLetterText(dwPrevID) == NULL)
                    {
                        SocketClient->ToGameServer()->SendLetterReadRequest(dwPrevID);
                    }
                    else
                    {
                        auto data =
                            reinterpret_cast<const BYTE *>(g_pLetterList->GetLetterText(dwPrevID));
                        ReceiveLetterText(std::span(data, sizeof(FS_LETTER_TEXT)), true);
                    }
                }
                else
                {
                    DWORD dwFindUIID = g_pWindowMgr->GetLetterReadWindow(dwPrevID);
                    if (dwFindUIID != 0)
                        g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, dwFindUIID, 0);
                }
            }
            else
            {
                if (g_pWindowMgr->GetFriendMainWindow() != NULL)
                {
                    g_pWindowMgr->GetFriendMainWindow()->PrevNextCursorMove(
                        g_pLetterList->GetLineNum(m_LetterHead.m_dwLetterID));
                }
            }
        }
        break;
        case 6: {
            DWORD dwNextID = g_pLetterList->GetNextLetterID(m_LetterHead.m_dwLetterID);
            if (dwNextID != 0)
            {
                if (g_pWindowMgr->GetFriendMainWindow() != NULL)
                {
                    g_pWindowMgr->GetFriendMainWindow()->PrevNextCursorMove(
                        g_pLetterList->GetLineNum(dwNextID));
                }
                g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
                g_pWindowMgr->CloseLetterRead(m_LetterHead.m_dwLetterID);
                g_iLetterReadNextPos_x = GetPosition_x();
                g_iLetterReadNextPos_y = GetPosition_y();
                if (g_pWindowMgr->LetterReadCheck(dwNextID) == FALSE)
                {
                    if (g_pLetterList->GetLetterText(dwNextID) == NULL)
                    {
                        SocketClient->ToGameServer()->SendLetterReadRequest(dwNextID);
                    }
                    else
                    {
                        auto data =
                            reinterpret_cast<const BYTE *>(g_pLetterList->GetLetterText(dwNextID));
                        ReceiveLetterText(std::span(data, sizeof(FS_LETTER_TEXT)), true);
                    }
                }
                else
                {
                    DWORD dwFindUIID = g_pWindowMgr->GetLetterReadWindow(dwNextID);
                    if (dwFindUIID != 0)
                        g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, dwFindUIID, 0);
                }
            }
            else
            {
                if (g_pWindowMgr->GetFriendMainWindow() != NULL)
                {
                    g_pWindowMgr->GetFriendMainWindow()->PrevNextCursorMove(
                        g_pLetterList->GetLineNum(m_LetterHead.m_dwLetterID));
                }
            }
        }
        break;
        default:
            break;
        }
    }
    break;
    case UI_MESSAGE_YNRETURN:
        if (m_WorkMessage.m_iParam2 == 1)
        {
            SocketClient->ToGameServer()->SendLetterDeleteRequest(m_LetterHead.m_dwLetterID);
            g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
        }
        break;
    default:
        break;
    }
    return FALSE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::DoActionSub(BOOL bMessageOnly)
{
    m_LetterTextBox.DoAction(bMessageOnly);
    m_ReplyButton.DoAction(bMessageOnly);
    m_DeleteButton.DoAction(bMessageOnly);
    m_CloseButton.DoAction(bMessageOnly);
    //m_PhotoButton.DoAction(bMessageOnly);
    m_PrevButton.DoAction(bMessageOnly);
    m_NextButton.DoAction(bMessageOnly);
    m_Photo.SetShowType(m_iShowType);
    m_Photo.DoAction(bMessageOnly);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::DoMouseActionSub()
{
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::Init(const wchar_t *pszTitle, DWORD dwParentID)
{
    SetTitle(pszTitle);
    SetParentUIID(dwParentID);
    SetOption(UIWINDOWSTYLE_NULL);

    SetPosition(50, 50);
    //SetSize(213, 170);
    SetSize(250, 170);
    SetLimitSize(250, 150);

    RefreshLetterList();

    m_LetterListBox.SetParentUIID(GetUIID());
    m_LetterListBox.SetArrangeType(2, 0, 22);
    m_LetterListBox.SetResizeType(3, 0, -39);

    m_WriteButton.Init(1, I18N::Game::Write);
    m_WriteButton.SetParentUIID(GetUIID());
    m_WriteButton.SetArrangeType(2, 2, 17);
    m_WriteButton.SetSize(50, 14);

    m_ReadButton.Init(2, I18N::Game::Read);
    m_ReadButton.SetParentUIID(GetUIID());
    m_ReadButton.SetArrangeType(2, 53, 17);
    m_ReadButton.SetSize(50, 14);

    m_ReplyButton.Init(3, I18N::Game::Reply);
    m_ReplyButton.SetParentUIID(GetUIID());
    m_ReplyButton.SetArrangeType(2, 104, 17);
    m_ReplyButton.SetSize(50, 14);

    m_DeleteButton.Init(4, I18N::Game::Delete);
    m_DeleteButton.SetParentUIID(GetUIID());
    m_DeleteButton.SetArrangeType(2, 155, 17);
    m_DeleteButton.SetSize(50, 14);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::Refresh()
{
    m_WriteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_ReadButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_ReplyButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_DeleteButton.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
    m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
    m_LetterListBox.SendUIMessageDirect(UI_MESSAGE_LISTSCRLTOP, 0, 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
LETTERLIST_TEXT *CUILetterBoxTabWindow::GetCurrentSelectedLetter()
{
    if (m_LetterListBox.GetSelectedText() == NULL)
        return NULL;
    else
        return m_LetterListBox.GetSelectedText();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::RefreshLetterList()
{
    int iSelectNum = g_pLetterList->UpdateLetterList(
        m_LetterListBox.GetLetterList(),
        (m_LetterListBox.SLGetSelectLineNum() > 0 ? m_LetterListBox.SLGetSelectLine()->m_dwLetterID
                                                  : -1));
    m_LetterListBox.SLSetSelectLine(iSelectNum);
    m_LetterListBox.Scrolling(0);
    CheckAll(FALSE);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::CheckAll(BOOL bCheck)
{
    m_LetterListBox.ResetCheckedLine(bCheck);
    g_pLetterList->ResetLetterSelect(bCheck);
    m_bCheckAllState = bCheck;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::PrevNextCursorMove(int iMove)
{
    m_LetterListBox.SLSetSelectLine(iMove);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::ToggleModernRow(std::size_t index)
{
    std::deque<LETTERLIST_TEXT> &rows = m_LetterListBox.GetLetterList();
    if (index >= rows.size())
        return;
    LETTERLIST_TEXT &row = rows[index];
    row.m_bIsSelected = row.m_bIsSelected == TRUE ? FALSE : TRUE;
    if (LETTERLIST_TEXT *letter = g_pLetterList->GetLetter(row.m_dwLetterID))
        letter->m_bIsSelected = row.m_bIsSelected;
    m_bCheckAllState =
        !rows.empty() && std::all_of(rows.begin(), rows.end(), [](const LETTERLIST_TEXT &letter) {
            return letter.m_bIsSelected == TRUE;
        });
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::SortModern(int column)
{
    if (column < 0 || column > 3 || g_pLetterList->GetCurrentSortType() == column)
    {
        return;
    }
    PlayBuffer(SOUND_CLICK01);
    g_pLetterList->Sort(column);
    RefreshLetterList();
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
BOOL CUILetterBoxTabWindow::HandleMessage()
{
    if (m_WorkMessage.m_iMessage == UI_MESSAGE_LISTDBLCLICK)
    {
        PlayBuffer(SOUND_CLICK01);
        m_WorkMessage.m_iMessage = UI_MESSAGE_BTNLCLICK;
        m_WorkMessage.m_iParam1 = 2;
    }
    switch (m_WorkMessage.m_iMessage)
    {
    case UI_MESSAGE_SELECTED:
        break;
    case UI_MESSAGE_BTNLCLICK: {
        if (g_dwTopWindow != 0)
            break;
        DWORD dwUIID = 0;
        switch (m_WorkMessage.m_iParam1)
        {
        case 1: {
            wchar_t temp[MAX_TEXT_LENGTH + 1];
            mu_swprintf(temp, I18N::Game::WriteLetterCostDZen, g_cdwLetterCost);
            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_WRITELETTER, 100, 100, temp);
        }
        break;
        case 2: {
            if (GetCurrentSelectedLetter() == NULL)
                break;
            DWORD dwLetterID = GetCurrentSelectedLetter()->m_dwLetterID;
            if (g_pWindowMgr->LetterReadCheck(dwLetterID) == FALSE)
            {
                // 캐시
                if (g_pLetterList->GetLetterText(dwLetterID) == NULL)
                {
                    SocketClient->ToGameServer()->SendLetterReadRequest(dwLetterID);
                }
                else
                {
                    auto data =
                        reinterpret_cast<const BYTE *>(g_pLetterList->GetLetterText(dwLetterID));
                    ReceiveLetterText(std::span(data, sizeof(FS_LETTER_TEXT)), true);
                }
            }
            else
            {
                DWORD dwFindUIID = g_pWindowMgr->GetLetterReadWindow(dwLetterID);
                if (dwFindUIID != 0)
                    g_pWindowMgr->SendUIMessage(UI_MESSAGE_SELECT, dwFindUIID, 0);
            }
        }
        break;
        case 3: {
            if (GetCurrentSelectedLetter() == NULL)
                break;
            wchar_t temp[MAX_TEXT_LENGTH + 1];
            mu_swprintf(temp, I18N::Game::WriteLetterCostDZen, g_cdwLetterCost);
            dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_WRITELETTER, 100, 100, temp);
            if (dwUIID == 0)
                break;
            ((CUILetterWriteWindow *)g_pWindowMgr->GetWindow(dwUIID))
                ->SetMailtoText(GetCurrentSelectedLetter()->m_szID);
            wchar_t szMailTitle[MAX_TEXT_LENGTH + 1] = {0};
            mu_swprintf(szMailTitle, I18N::Game::ReS, GetCurrentSelectedLetter()->m_szText);
            wchar_t szMailTitleResult[32 + 1] = {0};
            CutText4(szMailTitle, szMailTitleResult, NULL, 32);
            ((CUILetterWriteWindow *)g_pWindowMgr->GetWindow(dwUIID))
                ->SetMainTitleText(szMailTitleResult);
        }
        break;
        case 4: {
            if (m_LetterListBox.HaveCheckedLine() == FALSE)
            {
                dwUIID = g_pWindowMgr->AddWindow(UIWNDTYPE_OK, UIWND_DEFAULT, UIWND_DEFAULT,
                                                 I18N::Game::SelectTheLetterYouDLikeToDelete);
                break;
            }
            dwUIID =
                g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                        I18N::Game::AreYouSureYouWantToDeleteTheLetter, GetUIID());
        }
        break;
        case 5: {
            SocketClient->ToGameServer()->SendLetterListRequest();
        }
        break;
        default:
            break;
        }
        if (dwUIID != 0)
        {
            CUIBaseWindow *pWindow = g_pWindowMgr->GetWindow(dwUIID);
            if (pWindow != NULL)
            {
                pWindow->SendUIMessageDirect(UI_MESSAGE_P_MOVE, 0, 0);
                pWindow->SendUIMessageDirect(UI_MESSAGE_P_RESIZE, 0, 0);
            }
        }
    }
    break;
    case UI_MESSAGE_YNRETURN:
        if (m_WorkMessage.m_iParam2 == 1)
        {
            if (m_LetterListBox.HaveCheckedLine() == TRUE)
            {
                std::deque<LETTERLIST_TEXT *> letterlist;
                if (m_LetterListBox.GetCheckedLines(&letterlist) == 0)
                    break;
                for (std::deque<LETTERLIST_TEXT *>::iterator iter = letterlist.begin();
                     iter != letterlist.end(); ++iter)
                {
                    SocketClient->ToGameServer()->SendLetterDeleteRequest((*iter)->m_dwLetterID);
                }
                //m_LetterListBox.Scrolling(0);
            }
        }
        break;
    default:
        break;
    }
    return FALSE;
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::DoActionSub(BOOL bMessageOnly)
{
    m_LetterListBox.DoAction(bMessageOnly);

    m_WriteButton.DoAction(bMessageOnly);
    m_ReadButton.DoAction(bMessageOnly);
    m_ReplyButton.DoAction(bMessageOnly);
    m_DeleteButton.DoAction(bMessageOnly);
    //	m_DeliveryButton.DoAction(bMessageOnly);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterBoxTabWindow::DoMouseActionSub()
{
    if (MouseLButton)
    {
        if (CheckMouseIn(RPos_x(0), RPos_y(0), 10, 19) == TRUE)
        {
            if (g_dwTopWindow != 0)
                ;
            else if (m_bCheckAllState == FALSE)
            {
                PlayBuffer(SOUND_CLICK01);
                CheckAll(TRUE);
            }
            else
            {
                PlayBuffer(SOUND_CLICK01);
                CheckAll(FALSE);
            }
            MouseLButton = FALSE;
        }
        else if (CheckMouseIn(RPos_x(0) + 10, RPos_y(0), m_LetterListBox.GetColumnWidth(0) - 10,
                              19) == TRUE)
        {
            if (g_pLetterList->GetCurrentSortType() != 0)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pLetterList->Sort(0);
                RefreshLetterList();
            }
            MouseLButton = FALSE;
        }
        else if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(1), RPos_y(0),
                              m_LetterListBox.GetColumnWidth(1), 19) == TRUE)
        {
            if (g_pLetterList->GetCurrentSortType() != 1)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pLetterList->Sort(1);
                RefreshLetterList();
            }
            MouseLButton = FALSE;
        }
        else if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(2), RPos_y(0),
                              m_LetterListBox.GetColumnWidth(2), 19) == TRUE)
        {
            if (g_pLetterList->GetCurrentSortType() != 2)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pLetterList->Sort(2);
                RefreshLetterList();
            }
            MouseLButton = FALSE;
        }
        else if (CheckMouseIn(RPos_x(0) + m_LetterListBox.GetColumnPos_x(3), RPos_y(0),
                              m_LetterListBox.GetColumnWidth(3), 19) == TRUE)
        {
            if (g_pLetterList->GetCurrentSortType() != 3)
            {
                PlayBuffer(SOUND_CLICK01);
                g_pLetterList->Sort(3);
                RefreshLetterList();
            }
            MouseLButton = FALSE;
        }
    }
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterWriteWindow::ApplyModernUiChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.x || changes.y)
        SetPosition(changes.x.value_or(GetPosition_x()), changes.y.value_or(GetPosition_y()));
    if (changes.receiver)
        m_MailtoInputBox.SetText(changes.receiver->c_str());
    if (changes.subject)
        m_TitleInputBox.SetText(changes.subject->c_str());
    if (changes.body)
        m_TextInputBox.SetText(changes.body->c_str());
    if (changes.action == 0)
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 1, 0);
    if (changes.minimize)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, GetUIID(), 0);
    if (changes.close)
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 2, 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUILetterReadWindow::ApplyModernUiChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.x || changes.y)
        SetPosition(changes.x.value_or(GetPosition_x()), changes.y.value_or(GetPosition_y()));
    if (changes.action)
    {
        constexpr std::array<int, 5> ButtonIds{1, 2, 5, 6, 3};
        const int index = *changes.action;
        if (index >= 0 && index < static_cast<int>(ButtonIds.size()))
            SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, ButtonIds[index], 0);
    }
    if (changes.minimize)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, GetUIID(), 0);
    if (changes.close)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIChatWindow::ApplyModernUiChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.x)
        SetPosition(*changes.x, changes.y.value_or(GetPosition_y()));
    if (changes.y)
        SetPosition(changes.x.value_or(GetPosition_x()), *changes.y);
    if (changes.body)
        m_TextInputBox.SetText(changes.body->c_str());
    if (changes.selectedInvitee)
    {
        m_ModernInviteSelection = *changes.selectedInvitee;
        m_InvitePalListBox.SLSetSelectLine(static_cast<int>(m_ModernInviteSelection + 1));
    }
    if (changes.action == 0)
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 1, 0);
    else if (changes.action == 1)
    {
        m_InvitePalListBox.SLSetSelectLine(static_cast<int>(m_ModernInviteSelection + 1));
        SendUIMessageDirect(UI_MESSAGE_BTNLCLICK, 2, 0);
    }
    if (changes.sendChat)
        SendUIMessageDirect(UI_MESSAGE_TEXTINPUT, 0, 0);
    if (changes.minimize)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_HIDE, GetUIID(), 0);
    if (changes.close)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}
#pragma pack(pop)

#pragma pack(push)
#pragma pack()
void CUIFriendWindow::ApplyModernUiChanges()
{
    const auto changes = m_ModernPanel.TakeChanges();
    if (changes.x || changes.y)
        SetPosition(changes.x.value_or(GetPosition_x()), changes.y.value_or(GetPosition_y()));
    if (changes.tab)
    {
        SetTabIndex(std::clamp(*changes.tab, 0, 2));
        m_ModernScroll = 0;
        Refresh();
    }
    if (changes.scroll)
        m_ModernScroll = *changes.scroll;

    if (changes.selectedRow)
    {
        if (m_iTabIndex == 0)
            m_FriendListWnd.SelectModernRow(*changes.selectedRow);
        else if (m_iTabIndex == 1)
            m_LetterBoxWnd.SelectModernRow(*changes.selectedRow);
        else
            m_ChatRoomListWnd.SelectModernRow(*changes.selectedRow);
    }
    if (changes.checkedRow)
        m_LetterBoxWnd.ToggleModernRow(*changes.checkedRow);
    if (changes.toggleCheckAll)
        m_LetterBoxWnd.CheckAll(m_LetterBoxWnd.ModernCheckAll() ? FALSE : TRUE);
    if (changes.sortColumn)
    {
        if (m_iTabIndex == 0)
            m_FriendListWnd.SortModern(*changes.sortColumn);
        else if (m_iTabIndex == 1)
            m_LetterBoxWnd.SortModern(*changes.sortColumn);
    }
    if (changes.action)
    {
        if (m_iTabIndex == 0 && *changes.action < 4)
            m_FriendListWnd.InvokeModernAction(*changes.action);
        else if (m_iTabIndex == 1 && *changes.action < 4)
            m_LetterBoxWnd.InvokeModernAction(*changes.action);
        else if (m_iTabIndex == 2 && *changes.action == 3)
            m_ChatRoomListWnd.HideAllModern();
        else if (m_iTabIndex == 2 && *changes.action == 4)
            m_ChatRoomListWnd.ToggleModernRow();
    }
    if (changes.toggleRefuseChat)
    {
        PlayBuffer(SOUND_CLICK01);
        if (g_pWindowMgr->GetChatReject() == TRUE)
        {
            SocketClient->ToGameServer()->SendSetFriendOnlineState(1);
            g_pWindowMgr->SetChatReject(FALSE);
        }
        else
        {
            g_pWindowMgr->AddWindow(UIWNDTYPE_QUESTION, UIWND_DEFAULT, UIWND_DEFAULT,
                                    I18N::Game::IfYouRefuseChatAllChatWindowsWillClose, GetUIID());
        }
    }
    if (changes.close)
        g_pWindowMgr->SendUIMessage(UI_MESSAGE_CLOSE, GetUIID(), 0);
}
#pragma pack(pop)
