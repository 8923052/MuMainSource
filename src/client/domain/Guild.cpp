#include "domain/Guild.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "session/SessionKeeper.h"
#include "support/CoreMath.h"
#include "ui/session/UiSessionLogic.h"
#include "render/Textures.h"
#include "app/ApplicationKeeper.h"
#include "render/Sprites.h"
#include "ui/features/Social/SocialLogic.h"
#include "app/ApplicationNetwork.h"
#include "ui/features/Dialogs/DialogsLogic.h"

CGuildCache::CGuildCache(SessionKeeper &keeper) : SessionLegacyCalls(keeper)
{
    Reset();
}

CGuildCache::~CGuildCache()
{
    Reset();
}

void CGuildCache::Reset()
{
    auto &bitmaps = sessionKeeper_.ApplicationKeeperRef().BitmapRegistry();
    for (const auto &[index, variants] : textures_)
        for (const auto &texture : variants)
            bitmaps.RetireLogicalAsset(texture.asset);
    textures_.clear();
    m_dwCurrIndex = 0;
    for (int i = 0; i < MAX_MARKS; ++i)
        GuildMark[i].Key = GuildConstants::INVALID_MARK_INDEX;
}

int CGuildCache::GetGuildMarkIndex(int nGuildKey)
{
    for (int i = 0; i <= (int)m_dwCurrIndex; ++i)
    {
        if (GuildMark[i].Key == nGuildKey)
            return i;
    }
    return GuildConstants::INVALID_MARK_INDEX;
}

BOOL CGuildCache::IsExistGuildMark(int nGuildKey)
{
    if (GetGuildMarkIndex(nGuildKey) == GuildConstants::INVALID_MARK_INDEX)
        return FALSE;
    else
        return TRUE;
}

int CGuildCache::MakeGuildMarkIndex(int nGuildKey)
{
    if (m_dwCurrIndex >= MAX_MARKS)
    {
        assert(!"Guild mark buffer exceeded");
        return GuildConstants::INVALID_MARK_INDEX;
    }

    GuildMark[m_dwCurrIndex].Key = nGuildKey;
    return m_dwCurrIndex++;
}

int CGuildCache::SetGuildMark(int nGuildKey, char *UnionName, char *GuildName, BYTE *Mark)
{
    int nIndex = GetGuildMarkIndex(nGuildKey);
    if (nIndex != GuildConstants::INVALID_MARK_INDEX)
    {
        CMultiLanguage::ConvertFromUtf8(GuildMark[nIndex].UnionName, UnionName,
                                        GuildConstants::GUILD_NAME_LENGTH);
        CMultiLanguage::ConvertFromUtf8(GuildMark[nIndex].GuildName, GuildName,
                                        GuildConstants::GUILD_NAME_LENGTH);
        GuildMark[nIndex].UnionName[GuildConstants::GUILD_NAME_LENGTH] = L'\0';
        GuildMark[nIndex].GuildName[GuildConstants::GUILD_NAME_LENGTH] = L'\0';

        // Unpack compressed mark data: each byte contains two 4-bit values (nibbles)
        for (int i = 0; i < GuildConstants::GUILD_MARK_SIZE; ++i)
        {
            if (i % 2 == 0)
                GuildMark[nIndex].Mark[i] = (Mark[i / 2] >> GuildConstants::MARK_NIBBLE_SHIFT) &
                                            GuildConstants::MARK_NIBBLE_MASK;
            else
                GuildMark[nIndex].Mark[i] = Mark[i / 2] & GuildConstants::MARK_NIBBLE_MASK;
        }
    }
    else
        assert(!"Guild mark not found");

    if (nIndex >= 0)
        PrepareMark(nIndex);
    if (Hero && Hero->GuildMarkIndex == nIndex && g_pNewUISystem && g_pGuildInfoWindow)
        g_pGuildInfoWindow->InvalidateGuildMark();
    return nIndex;
}

// Publish on guild data changes, before worker recording. Each guild owns its pixels.

void CUIGuildMaster::ReceiveGuildRelationShip(GuildRelationshipType relationship,
                                              GuildRequestType request, BYTE high, BYTE low)
{
    if (g_pUIPopup->GetPopupID())
    {
        SocketClient->ToGameServer()->SendGuildRelationshipChangeResponse(
            relationship, request, false, MAKEWORD(low, high));
        return;
    }
    g_pGuildInfoWindow->ReceiveGuildRelationShip(relationship, request, high, low);
}
