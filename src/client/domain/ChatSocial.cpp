#include "domain/ChatSocial.h"
#include "support/CoreMath.h"
#include "app/ApplicationAudio.h"
#include "data/ItemData.h"
#include "domain/CharacterSystem.h"
#include "domain/Events.h"
#include "I18N/All.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionPresentation.h"
#include "session/SessionUi.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

void SessionGameplayUnit::CheckChatText(wchar_t *Text)
{
    CHARACTER *c = Hero;
    OBJECT *o = &c->Object;
    if (FindText(Text, I18N::Game::Hello) || FindText(Text, I18N::Game::Hi) ||
        FindText(Text, I18N::Game::Welcome) || FindText(Text, I18N::Game::Welcome) ||
        FindText(Text, I18N::Game::Thanks) || FindText(Text, I18N::Game::Thanks) ||
        FindText(Text, I18N::Game::Thanks) || FindText(Text, I18N::Game::Thanks))
    {
        SetActionClass(c, o, PLAYER_GREETING1, AT_GREETING1);
        SendRequestAction(Hero->Object, AT_GREETING1);
    }
    else if (FindText(Text, I18N::Game::EnjoyTheGame) || FindText(Text, I18N::Game::Bye) ||
             FindText(Text, I18N::Game::Bye280))
    {
        SetActionClass(c, o, PLAYER_GOODBYE1, AT_GOODBYE1);
        SendRequestAction(Hero->Object, AT_GOODBYE1);
    }
    else if (FindText(Text, I18N::Game::Good) || FindText(Text, I18N::Game::Good) ||
             FindText(Text, I18N::Game::Wow) || FindText(Text, I18N::Game::Wow) ||
             FindText(Text, I18N::Game::Nice) || FindText(Text, I18N::Game::Nice))
    {
        SetActionClass(c, o, PLAYER_CLAP1, AT_CLAP1);
        SendRequestAction(Hero->Object, AT_CLAP1);
    }
    else if (FindText(Text, I18N::Game::Here) || FindText(Text, I18N::Game::Here) ||
             FindText(Text, I18N::Game::Come) || FindText(Text, I18N::Game::Come))
    {
        SetActionClass(c, o, PLAYER_GESTURE1, AT_GESTURE1);
        SendRequestAction(Hero->Object, AT_GESTURE1);
    }
    else if (FindText(Text, I18N::Game::There) || FindText(Text, I18N::Game::There) ||
             FindText(Text, I18N::Game::That) || FindText(Text, I18N::Game::That))
    {
        SetActionClass(c, o, PLAYER_DIRECTION1, AT_DIRECTION1);
        SendRequestAction(Hero->Object, AT_DIRECTION1);
    }
    else if (FindText(Text, I18N::Game::Not) || FindText(Text, I18N::Game::Not) ||
             FindText(Text, I18N::Game::Never) || FindText(Text, I18N::Game::Never) ||
             FindText(Text, I18N::Game::DoNot) || FindText(Text, I18N::Game::DoNot) ||
             FindText(Text, I18N::Game::DoNot302))
    {
        SetActionClass(c, o, PLAYER_UNKNOWN1, AT_UNKNOWN1);
        SendRequestAction(Hero->Object, AT_UNKNOWN1);
    }
    else if (FindText(Text, L";") || FindText(Text, I18N::Game::Sorry) ||
             FindText(Text, I18N::Game::Sorry) || FindText(Text, I18N::Game::Sorry))
    {
        SetActionClass(c, o, PLAYER_AWKWARD1, AT_AWKWARD1);
        SendRequestAction(Hero->Object, AT_AWKWARD1);
    }
    else if (FindText(Text, L"ㅠ.ㅠ") || FindText(Text, L"ㅜ.ㅜ") || FindText(Text, L"T_T") ||
             FindText(Text, I18N::Game::Sad) || FindText(Text, I18N::Game::Sad) ||
             FindText(Text, I18N::Game::Cry) || FindText(Text, I18N::Game::Cry))
    {
        SetActionClass(c, o, PLAYER_CRY1, AT_CRY1);
        SendRequestAction(Hero->Object, AT_CRY1);
    }
    else if (FindText(Text, L"ㅡ.ㅡ") || FindText(Text, L"ㅡ.,ㅡ") || FindText(Text, L"ㅡ,.ㅡ") ||
             FindText(Text, L"-.-") || FindText(Text, L"-_-") || FindText(Text, I18N::Game::Huh) ||
             FindText(Text, I18N::Game::Pooh))
    {
        SetActionClass(c, o, PLAYER_SEE1, AT_SEE1);
        SendRequestAction(Hero->Object, AT_SEE1);
    }
    else if (FindText(Text, L"^^") || FindText(Text, L"^.^") || FindText(Text, L"^_^") ||
             FindText(Text, I18N::Game::Haha) || FindText(Text, I18N::Game::Hehe) ||
             FindText(Text, I18N::Game::Hoho) || FindText(Text, I18N::Game::Hoho) ||
             FindText(Text, I18N::Game::Hihi))
    {
        SetActionClass(c, o, PLAYER_SMILE1, AT_SMILE1);
        SendRequestAction(Hero->Object, AT_SMILE1);
    }
    else if (FindText(Text, I18N::Game::Great) || FindText(Text, I18N::Game::OhYeah) ||
             FindText(Text, I18N::Game::OhYeah320) || FindText(Text, I18N::Game::BeatIt))
    {
        SetActionClass(c, o, PLAYER_CHEER1, AT_CHEER1);
        SendRequestAction(Hero->Object, AT_CHEER1);
    }
    else if (FindText(Text, I18N::Game::Win) || FindText(Text, I18N::Game::Win) ||
             FindText(Text, I18N::Game::Victory) || FindText(Text, I18N::Game::Victory))
    {
        SetActionClass(c, o, PLAYER_WIN1, AT_WIN1);
        SendRequestAction(Hero->Object, AT_WIN1);
    }
    else if (FindText(Text, I18N::Game::Sleep) || FindText(Text, I18N::Game::Sleep) ||
             FindText(Text, I18N::Game::Tired) || FindText(Text, I18N::Game::Tired))
    {
        SetActionClass(c, o, PLAYER_SLEEP1, AT_SLEEP1);
        SendRequestAction(Hero->Object, AT_SLEEP1);
    }
    else if (FindText(Text, I18N::Game::Cold) || FindText(Text, I18N::Game::Cold) ||
             FindText(Text, I18N::Game::Hurt) || FindText(Text, I18N::Game::Hurt) ||
             FindText(Text, I18N::Game::Hurt))
    {
        SetActionClass(c, o, PLAYER_COLD1, AT_COLD1);
        SendRequestAction(Hero->Object, AT_COLD1);
    }
    else if (FindText(Text, I18N::Game::Again) || FindText(Text, I18N::Game::Again) ||
             FindText(Text, I18N::Game::OK) || FindText(Text, I18N::Game::Great))
    {
        SetActionClass(c, o, PLAYER_AGAIN1, AT_AGAIN1);
        SendRequestAction(Hero->Object, AT_AGAIN1);
    }
    else if (FindText(Text, I18N::Game::Respect) || FindText(Text, I18N::Game::Respect) ||
             FindText(Text, I18N::Game::Defeated))
    {
        SetActionClass(c, o, PLAYER_RESPECT1, AT_RESPECT1);
        SendRequestAction(Hero->Object, AT_RESPECT1);
    }
    else if (FindText(Text, I18N::Game::Sir) || FindText(Text, I18N::Game::Sir) ||
             FindText(Text, L"/ㅡ") || FindText(Text, L"ㅡ^"))
    {
        SetActionClass(c, o, PLAYER_SALUTE1, AT_SALUTE1);
        SendRequestAction(Hero->Object, AT_SALUTE1);
    }
    else if (FindText(Text, I18N::Game::Rush) || FindText(Text, I18N::Game::Rush) ||
             FindText(Text, I18N::Game::GoGo) || FindText(Text, I18N::Game::GoGo))
    {
        SetActionClass(c, o, PLAYER_RUSH1, AT_RUSH1);
        SendRequestAction(Hero->Object, AT_RUSH1);
    }
    else if (FindText(Text, I18N::Game::Hustle) || FindText(Text, L"hustle"))
    {
        SetActionClass(c, o, PLAYER_HUSTLE, AT_HUSTLE);
        SendRequestAction(Hero->Object, AT_HUSTLE);
    }
    else if (FindText(Text, I18N::Game::ComeOn))
    {
        SetActionClass(c, o, PLAYER_PROVOCATION, AT_PROVOCATION);
        SendRequestAction(Hero->Object, AT_PROVOCATION);
    }
    else if (FindText(Text, I18N::Game::Great))
    {
        SetActionClass(c, o, PLAYER_CHEERS, AT_CHEERS);
        SendRequestAction(Hero->Object, AT_CHEERS);
    }
    else if (FindText(Text, I18N::Game::LookAround))
    {
        SetActionClass(c, o, PLAYER_LOOK_AROUND, AT_LOOK_AROUND);
        SendRequestAction(Hero->Object, AT_LOOK_AROUND);
    }
    else if (FindText(Text, I18N::Game::TheForehead))
    {
        ITEM *pItem_rr = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
        ITEM *pItem_rl = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];

        if (pItem_rr->Type == ITEM_JACK_OLANTERN_TRANSFORMATION_RING ||
            pItem_rl->Type == ITEM_JACK_OLANTERN_TRANSFORMATION_RING)
        {
            if (rand_fps_check(2))
            {
                SetAction(o, PLAYER_JACK_1);
                SendRequestAction(Hero->Object, AT_JACK1);
            }
            else
            {
                SetAction(o, PLAYER_JACK_2);
                SendRequestAction(Hero->Object, AT_JACK2);
            }

            o->m_iAnimation = 0;
        }
    }
    else if (FindText(Text, I18N::Game::Christmas))
    {
        ITEM *pItem_rr = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
        ITEM *pItem_rl = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];

        if (pItem_rr->Type == ITEM_CHRISTMAS_TRANSFORMATION_RING ||
            pItem_rl->Type == ITEM_CHRISTMAS_TRANSFORMATION_RING)
        {
            if (o->CurrentAction != PLAYER_SANTA_1 && o->CurrentAction != PLAYER_SANTA_2)
            {
                int i = rand() % 3;
                if (rand() % 2)
                {
                    SetAction(o, PLAYER_SANTA_1);
                    SendRequestAction(Hero->Object, AT_SANTA1_1 + i);
                    PlayBuffer(static_cast<ESound>(SOUND_XMAS_JUMP_SANTA + i));
                }
                else
                {
                    SetAction(o, PLAYER_SANTA_2);
                    SendRequestAction(Hero->Object, AT_SANTA2_1 + i);
                    PlayBuffer(SOUND_XMAS_TURN);
                }

                xmasEvent_.CreateXmasEventEffect(c, o, i);
            }

            o->m_iAnimation = 0;
        }
    }
}

void SessionLegacyCalls::CheckChatText(wchar_t *text)
{
    sessionKeeper_.Gameplay()->CheckChatText(text);
}

using UI::Chat::WHISPER_ID_SLOTS;

bool SessionLegacyCalls::CheckLevel(int requiredLevel, wchar_t *targetId)
{
    return sessionKeeper_.Gameplay()->CheckLevel(requiredLevel, targetId);
}

void SessionLegacyCalls::Register(int requiredLevel, wchar_t *targetId)
{
    sessionKeeper_.Gameplay()->Register(requiredLevel, targetId);
}

void SessionLegacyCalls::Clear()
{
    sessionKeeper_.Gameplay()->Clear();
}

void SessionVisualUnit::AddChat(ChatTextDetail::CHAT *c, const wchar_t *chat_text, int flag)
{
    float Time = 0;
    int Length = (int)wcslen(chat_text);
    switch (flag)
    {
    case 0:
        Time = Length * 2 + 160;
        break;
    case 1:
        Time = 1000;
        g_pChatListBox->AddText(c->ID, chat_text, SEASON3B::TYPE_CHAT_MESSAGE);
        break;
    }

    if (Length >= 20)
    {
        CutText(chat_text, c->Text[1], c->Text[0], 256);
        c->LifeTime[0] = Time;
        c->LifeTime[1] = Time;
    }
    else
    {
        memset(c->Text[0], 0, 256);
        wcscpy(c->Text[0], chat_text);
        c->LifeTime[0] = Time;
    }
}

void SessionVisualUnit::AddGuildName(ChatTextDetail::CHAT *c, CHARACTER *Owner)
{
    if (IsShopInViewport(Owner))
    {
        std::wstring summary;
        GetShopTitleSummary(Owner, summary);
        wcscpy(c->szShopTitle, summary.c_str());
    }
    else
    {
        c->szShopTitle[0] = '\0';
    }

    if (Owner->GuildMarkIndex >= 0 && GuildMark[Owner->GuildMarkIndex].UnionName[0])
    {
        if (Owner->GuildRelationShip == GR_UNION)
            mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                        I18N::Game::Alliance);
        if (Owner->GuildRelationShip == GR_UNIONMASTER)
        {
            if (Owner->GuildStatus == G_MASTER)
                mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                            I18N::Game::AllianceMaster);
            else
                mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                            I18N::Game::Alliance);
        }
        else if (Owner->GuildRelationShip == GR_RIVAL)
        {
            if (Owner->GuildStatus == G_MASTER)
                mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                            I18N::Game::OpposingMaster);
            else
                mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                            I18N::Game::Oppose);
        }
        else if (Owner->GuildRelationShip == GR_RIVALUNION)
            mu_swprintf(c->Union, L"<%ls> %ls", GuildMark[Owner->GuildMarkIndex].UnionName,
                        I18N::Game::OpposingAllianceMaster);
        else
            mu_swprintf(c->Union, L"<%ls>", GuildMark[Owner->GuildMarkIndex].UnionName);
    }
    else
        c->Union[0] = 0;

    if (Owner->GuildMarkIndex >= 0)
    {
        c->GuildColor = Owner->GuildTeam;

        if (Owner->GuildStatus == G_PERSON)
            mu_swprintf(c->Guild, L"[%ls] %ls", GuildMark[Owner->GuildMarkIndex].GuildName,
                        I18N::Game::Members);
        else if (Owner->GuildStatus == G_MASTER)
            mu_swprintf(c->Guild, L"[%ls] %ls", GuildMark[Owner->GuildMarkIndex].GuildName,
                        I18N::Game::Master);
        else if (Owner->GuildStatus == G_SUB_MASTER)
            mu_swprintf(c->Guild, L"[%ls] %ls", GuildMark[Owner->GuildMarkIndex].GuildName,
                        I18N::Game::AssistM);
        else if (Owner->GuildStatus == G_BATTLE_MASTER)
            mu_swprintf(c->Guild, L"[%ls] %ls", GuildMark[Owner->GuildMarkIndex].GuildName,
                        I18N::Game::BattleM);
        else
            mu_swprintf(c->Guild, L"[%ls]", GuildMark[Owner->GuildMarkIndex].GuildName);
    }
    else
    {
        c->GuildColor = 0;
        c->Guild[0] = 0;
    }
}

void SessionVisualUnit::CreateChat(wchar_t *character_name, const wchar_t *chat_text,
                                   CHARACTER *Owner, int Flag, int SetColor)
{
    OBJECT *o = &Owner->Object;
    if (!o->Live || !o->Visible)
        return;

    int Color;
    if (SetColor != -1)
    {
        Color = SetColor;
    }
    else
    {
        Color = Owner->PK;
        if (o->Kind == KIND_NPC)
            Color = 0;
    }

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *c = &Chat[i];
        if (c->Owner == Owner)
        {
            wcscpy(c->ID, character_name);
            c->Color = Color;
            AddGuildName(c, Owner);
            if (wcslen(chat_text) == 0)
            {
                c->IDLifeTime = 10;
            }
            else
            {
                if (c->LifeTime[0] > 0)
                {
                    wcscpy(c->Text[1], c->Text[0]);
                    c->LifeTime[1] = c->LifeTime[0];
                }
                c->Owner = Owner;
                AddChat(c, chat_text, Flag);
            }
            return;
        }
    }

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *c = &Chat[i];
        if (c->IDLifeTime <= 0 && c->LifeTime[0] <= 0)
        {
            c->Owner = Owner;
            wcscpy(c->ID, character_name);
            c->Color = Color;
            AddGuildName(c, Owner);
            if (wcslen(chat_text) == 0)
            {
                c->IDLifeTime = 100;
            }
            else
            {
                AddChat(c, chat_text, Flag);
            }
            return;
        }
    }
}

void SessionVisualUnit::DetachCharacterChats(CHARACTER *character) noexcept
{
    for (ChatTextDetail::CHAT &chat : Chat)
    {
        if (chat.Owner != character)
            continue;
        chat.Owner = nullptr;
        chat.IDLifeTime = 0.0F;
        chat.LifeTime[0] = 0.0F;
        chat.LifeTime[1] = 0.0F;
    }
}

int SessionVisualUnit::CreateChat(wchar_t *character_name, const wchar_t *chat_text, OBJECT *Owner,
                                  int Flag, int SetColor)
{
    OBJECT *o = Owner;
    if (!o->Live || !o->Visible)
        return 0;

    int Color = 0;

    if (SetColor != -1)
    {
        Color = SetColor;
    }

    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *c = &Chat[i];
        if (c->IDLifeTime <= 0 && c->LifeTime[0] <= 0)
        {
            c->Owner = NULL;
            wcscpy(c->ID, character_name);
            c->Color = Color;
            c->GuildColor = 0;
            c->Guild[0] = 0;
            AddChat(c, chat_text, 0);
            c->LifeTime[0] = Flag;

            Vector(o->Position[0], o->Position[1], o->Position[2] + o->BoundingBoxMax[2] + 60.f,
                   c->Position);
            return c->LifeTime[0];
        }
    }

    return 0;
}

void SessionVisualUnit::AssignChat(wchar_t *character_name, const wchar_t *chat_text, int flag)
{
    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if (o->Live && o->Kind == KIND_PLAYER)
        {
            if (wcscmp(c->ID, character_name) == 0)
            {
                CreateChat(character_name, chat_text, c, flag);
                return;
            }
        }
    }

    for (int i = 0; i < CharactersClient.Size(); i++)
    {
        if (!CharactersClient.IsValidIndex(i))
            continue;
        CHARACTER *c = &CharactersClient[i];
        OBJECT *o = &c->Object;
        if (o->Live && o->Kind == KIND_MONSTER)
        {
            if (wcscmp(c->ID, character_name) == 0)
            {
                CreateChat(character_name, chat_text, c, flag);
                return;
            }
        }
    }
}

void SessionVisualUnit::MoveChat()
{
    for (int i = 0; i < ChatTextDetail::MAX_CHAT; i++)
    {
        ChatTextDetail::CHAT *c = &Chat[i];
        if (c->IDLifeTime > 0)
            c->IDLifeTime -= FPS_ANIMATION_FACTOR;
        if (c->LifeTime[0] > 0)
            c->LifeTime[0] -= FPS_ANIMATION_FACTOR;
        if (c->LifeTime[1] > 0)
            c->LifeTime[1] -= FPS_ANIMATION_FACTOR;
        if (c->Owner != NULL && (!c->Owner->Object.Live || !c->Owner->Object.Visible))
        {
            c->IDLifeTime = 0;
            c->LifeTime[0] = 0;
            c->LifeTime[1] = 0;
        }
    }
}

void SessionLegacyCalls::AddChat(UI::Chat::CHAT *chat, const wchar_t *chatText, int flag)
{
    sessionKeeper_.Visual()->AddChat(chat, chatText, flag);
}

void SessionLegacyCalls::AddGuildName(UI::Chat::CHAT *chat, CHARACTER *owner)
{
    sessionKeeper_.Visual()->AddGuildName(chat, owner);
}

void SessionLegacyCalls::CreateChat(wchar_t *characterName, const wchar_t *chatText,
                                    CHARACTER *owner, int flag, int setColor)
{
    sessionKeeper_.Visual()->CreateChat(characterName, chatText, owner, flag, setColor);
}

int SessionLegacyCalls::CreateChat(wchar_t *characterName, const wchar_t *chatText, OBJECT *owner,
                                   int flag, int setColor)
{
    return sessionKeeper_.Visual()->CreateChat(characterName, chatText, owner, flag, setColor);
}

void SessionLegacyCalls::AssignChat(wchar_t *characterName, const wchar_t *chatText, int flag)
{
    sessionKeeper_.Visual()->AssignChat(characterName, chatText, flag);
}

void SessionLegacyCalls::MoveChat()
{
    sessionKeeper_.Visual()->MoveChat();
}

void CUIChatInputBox::GetTexts(wchar_t *pText, wchar_t *pBuddyText)
{
    m_TextInputBox.GetText(pText);
    m_BuddyInputBox.GetText(pBuddyText);
}

void CUIChatInputBox::ClearTexts()
{
    m_TextInputBox.SetText(nullptr);
    m_BuddyInputBox.SetText(nullptr);
}

void CUIChatInputBox::AddHistory(const wchar_t *pszText)
{
    if (pszText == nullptr || pszText[0] == 0)
        return;

    wchar_t *pszSaveText = new wchar_t[wcslen(pszText) + 1];
    wcsncpy(pszSaveText, pszText, wcslen(pszText) + 1);
    m_HistoryList.push_front(pszSaveText);
    m_CurrentHistoryLine = m_HistoryList.begin();
    m_bHistoryMode = FALSE;

    RemoveHistory(FALSE);
}

void CUIChatInputBox::MoveHistory(int iDegree)
{
    if (m_HistoryList.empty() == TRUE)
        return;

    if (iDegree == 10000)
    {
        m_CurrentHistoryLine = m_HistoryList.begin();
        m_bHistoryMode = FALSE;
    }
    else if (iDegree > 0)
    {
        PlayBuffer(SOUND_CLICK01);
        for (int i = 0; i < iDegree; ++i)
        {
            if (m_CurrentHistoryLine != m_HistoryList.begin())
                --m_CurrentHistoryLine;
            else
            {
                if (m_bHistoryMode == TRUE)
                {
                    SetText(TRUE, m_szTempText, FALSE, nullptr);
                    m_TextInputBox.m_caretTimer.ResetTimer();
                    m_bHistoryMode = FALSE;
                }
                return;
            }
        }
    }
    else if (iDegree < 0)
    {
        PlayBuffer(SOUND_CLICK01);
        for (int i = 0; i > iDegree; --i)
        {
            if (m_CurrentHistoryLine != m_HistoryList.end())
            {
                if (m_bHistoryMode == FALSE)
                {
                    m_TextInputBox.GetText(m_szTempText);
                    m_bHistoryMode = TRUE;
                }
                else
                    ++m_CurrentHistoryLine;
            }
        }
        if (m_CurrentHistoryLine == m_HistoryList.end())
            --m_CurrentHistoryLine;
    }
    SetText(TRUE, *m_CurrentHistoryLine, FALSE, nullptr);
    m_TextInputBox.m_caretTimer.ResetTimer();
}

void CUIChatInputBox::RemoveHistory(BOOL bClear)
{
    if (bClear == TRUE)
    {
        for (m_HistoryListIter = m_HistoryList.begin(); m_HistoryListIter != m_HistoryList.end();
             ++m_HistoryListIter)
        {
            if (*m_HistoryListIter != nullptr)
            {
                delete[] *m_HistoryListIter;
                *m_HistoryListIter = nullptr;
            }
        }
        m_HistoryList.clear();
    }
    else if (m_HistoryList.size() > LegacyControlDetail::MAX_HISTORY_LINES)
    {
        m_HistoryListIter = m_HistoryList.begin();
        for (int i = 0; i < LegacyControlDetail::MAX_HISTORY_LINES; ++i, ++m_HistoryListIter)
            ;
        for (; m_HistoryListIter != m_HistoryList.end(); ++m_HistoryListIter)
        {
            if (*m_HistoryListIter != nullptr)
            {
                delete[] *m_HistoryListIter;
                *m_HistoryListIter = nullptr;
            }
        }
        int nDelCount = m_HistoryList.size() - LegacyControlDetail::MAX_HISTORY_LINES;

        for (int i = 0; i < nDelCount; ++i)
            m_HistoryList.pop_back();
    }
}

bool SessionGameplayUnit::CheckLevel(int requiredLevel, wchar_t *targetId)
{
    auto &WhisperRegistID = sessionKeeper_.ChatStorage().WhisperRegistID;

    int level = CharacterAttribute->Level;

    if (level >= requiredLevel)
        return true;

    for (int i = 0; i < WHISPER_ID_SLOTS; ++i)
    {
        if (wcscmp(targetId, WhisperRegistID[i]) == 0)
        {
            return true;
        }
    }

    g_pSystemLogBox->AddText(I18N::Game::YouCanUseTheWhisperCommandAtCharacterLevel6,
                             SEASON3B::TYPE_SYSTEM_MESSAGE);

    return false;
}

void SessionGameplayUnit::Register(int requiredLevel, wchar_t *targetId)
{
    auto &WhisperRegistID = sessionKeeper_.ChatStorage().WhisperRegistID;
    auto &WhisperID_Num = sessionKeeper_.ChatStorage().WhisperID_Num;

    int level = CharacterAttribute->Level;

    if (level < requiredLevel)
    {
        bool noMatch = true;
        for (int i = 0; i < WHISPER_ID_SLOTS; ++i)
        {
            if (wcscmp(targetId, WhisperRegistID[i]) == 0)
            {
                noMatch = false;
                break;
            }
        }

        if (noMatch)
        {
            wcscpy(WhisperRegistID[WhisperID_Num], targetId);
            WhisperID_Num++;

            if (WhisperID_Num >= WHISPER_ID_SLOTS)
            {
                WhisperID_Num = 0;
            }
        }
    }
}

void SessionGameplayUnit::Clear()
{
    auto &WhisperRegistID = sessionKeeper_.ChatStorage().WhisperRegistID;

    ZeroMemory(WhisperRegistID, sizeof(WhisperRegistID));
}
