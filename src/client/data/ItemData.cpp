#include "data/ItemData.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationDiagnostics.h"
#include "app/ApplicationNetwork.h"
#include "data/CharacterData.h"
#include "data/Localization.h"
#include "data/ResourceData.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/ChatSocial.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/Guild.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldPresentation.h"
#include "domain/WorldSimulation.h"
#include "render/ModelGeometry.h"
#include "render/ModelResources.h"
#include "render/Sprites.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "support/Camera.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Activities/ActivitiesLogic.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudLogic.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/QuestNpc/QuestNpcLogic.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/session/UiSessionLogic.h"

bool ItemDataLoader::Load(wchar_t *fileName, CErrorReport &errorReport, HWND window)
{
    FILE *fp = _wfopen(fileName, L"rb");
    if (fp == NULL)
    {
        std::wstringstream ss;
        ss << fileName << L" - File not exist.";
        DataFileIO::ShowErrorAndExit(errorReport, window, ss.str().c_str());
        return false;
    }

    // Get file size to determine structure version
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const int LegacySize = sizeof(ITEM_ATTRIBUTE_FILE_LEGACY);
    const int NewSize = sizeof(ITEM_ATTRIBUTE_FILE);
    const long expectedLegacySize = LegacySize * MAX_ITEM + sizeof(DWORD);
    const long expectedNewSize = NewSize * MAX_ITEM + sizeof(DWORD);

    bool isLegacyFormat = (fileSize == expectedLegacySize);
    bool success = false;

    if (isLegacyFormat)
    {
        success = LoadLegacyFormat(fp, fileSize, errorReport, window);
    }
    else
    {
        success = LoadNewFormat(fp, fileSize, errorReport, window);
    }

    fclose(fp);

    return success;
}

template <typename TFileFormat>
bool ItemDataLoader::LoadFormat(FILE *fp, const wchar_t *formatName, CErrorReport &errorReport,
                                HWND window)
{
    const int Size = sizeof(TFileFormat);

    // Configure I/O
    DataFileIO::IOConfig config;
    config.itemSize = Size;
    config.itemCount = MAX_ITEM;
    config.checksumKey = 0xE2F1;
    config.decryptRecord = [](BYTE *data, int size) { BuxConvert(data, size); };

    // Read buffer and checksum
    DWORD dwCheckSum;
    auto buffer = DataFileIO::ReadBuffer(fp, config, errorReport, window, &dwCheckSum);
    if (!buffer)
    {
        std::wstringstream ss;
        ss << L"Failed to read item file (" << formatName << L").";
        DataFileIO::ShowErrorAndExit(errorReport, window, ss.str().c_str());
        return false;
    }

    // Verify checksum
    if (!DataFileIO::VerifyChecksum(buffer.get(), config, dwCheckSum))
    {
        std::wstringstream ss;
        ss << L"Item file corrupted (" << formatName << L").";
        DataFileIO::ShowErrorAndExit(errorReport, window, ss.str().c_str());
        return false;
    }

    // Decrypt buffer
    DataFileIO::DecryptBuffer(buffer.get(), config);

    // Copy items
    BYTE *pSeek = buffer.get();
    for (int i = 0; i < MAX_ITEM; i++)
    {
        TFileFormat source;
        memcpy(&source, pSeek, sizeof(source));
        CopyItemAttributeFromSource(ItemAttribute[i], source);
        pSeek += Size;
    }

    return true;
}

bool ItemDataLoader::LoadLegacyFormat(FILE *fp, long fileSize, CErrorReport &errorReport,
                                      HWND window)
{
    return LoadFormat<ITEM_ATTRIBUTE_FILE_LEGACY>(fp, L"legacy format", errorReport, window);
}

bool ItemDataLoader::LoadNewFormat(FILE *fp, long fileSize, CErrorReport &errorReport, HWND window)
{
    return LoadFormat<ITEM_ATTRIBUTE_FILE>(fp, L"new format", errorReport, window);
}

namespace UI::Items
{
namespace
{
void ReadTransforms(const char *path, bool s9, std::unordered_map<int, ItemSlotTrs> &result)
{
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    if (!input)
        throw std::runtime_error("Cannot load item slot TRS data");
    int id;
    while (input >> id)
    {
        ItemSlotTrs row;
        input >> row.position[0] >> row.position[1];
        if (s9)
        {
            float sourceDistance;
            input >> sourceDistance;
            // All S9 rows use the existing item-view distance. Preserve the
            // source column and reject an unsupported projection at load time.
            if (!input || sourceDistance != RENDER_ITEMVIEW_FAR)
                throw std::runtime_error("Unsupported item TRS view distance");
        }
        if (!(input >> row.rotation[0] >> row.rotation[1] >> row.rotation[2] >> row.scale))
            throw std::runtime_error("Incomplete item slot TRS row");
        result.try_emplace(id, row); // S9 owns matching IDs; S13 fills missing IDs.
    }
    if (!input.eof())
        throw std::runtime_error("Invalid item slot TRS data");
}

const std::unordered_map<int, ItemSlotTrs> &Transforms()
{
    static const auto transforms = [] {
        std::unordered_map<int, ItemSlotTrs> result;
        ReadTransforms("Data/UI/PC/Inventory/ItemTRSDataS9.txt", true, result);
        ReadTransforms("Data/UI/PC/Inventory/ItemTRSDataS13.txt", false, result);
        return result;
    }();
    return transforms;
}
} // namespace
void ItemSlotTrs::EnsureLoaded()
{
    (void)Transforms();
}
const ItemSlotTrs *ItemSlotTrs::Find(int itemId)
{
    const auto &entries = Transforms();
    const auto found = entries.find(itemId);
    return found == entries.end() ? nullptr : &found->second;
}
} // namespace UI::Items

#ifdef _PVP_ADD_MOVE_SCROLL
extern CMurdererMove g_MurdererMove;
#endif // _PVP_ADD_MOVE_SCROLL

void SessionGameDataUnit::GetItemName(int iType, int iLevel, wchar_t *Text) const
{
    ITEM_ATTRIBUTE *p = &ItemAttribute[iType];

    if (iType >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR && iType <= ITEM_SOUL_SHARD_OF_WIZARD)
    {
        if (iType == ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR)
        {
            switch (iLevel)
            {
            case 0:
                mu_swprintf(Text, L"%ls", p->Name);
                break;
            case 1:
                mu_swprintf(Text, L"%ls", I18N::Game::RingOfHonor);
                break;
            }
        }
        else if (iType == ITEM_BROKEN_SWORD_DARK_STONE)
        {
            switch (iLevel)
            {
            case 0:
                mu_swprintf(Text, L"%ls", p->Name);
                break;
            case 1:
                mu_swprintf(Text, L"%ls", I18N::Game::DarkStone);
                break;
            }
        }
        else
        {
            mu_swprintf(Text, L"%ls", p->Name);
        }
    }
    else if (iType == ITEM_POTION + 12)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", I18N::Game::Zen);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::Heart);
            break;
        case 2:
            mu_swprintf(Text, L"%ls", ItemRulesDetail::ChaosEventName[p->Durability]);
            break;
        }
    }
    else if (iType == ITEM_BOX_OF_LUCK)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::StarOfSacredBirth);
            break;
        case 2:
            mu_swprintf(Text, L"%ls", I18N::Game::Firecracker);
            break;
        case 3:
            mu_swprintf(Text, L"%ls", I18N::Game::HeartOfLove);
            break;
        case 5:
            mu_swprintf(Text, L"%ls", I18N::Game::SilverMedal);
            break;
        case 6:
            mu_swprintf(Text, L"%ls", I18N::Game::GoldMedal);
            break;
        case 7:
            mu_swprintf(Text, L"%ls", I18N::Game::BoxOfHeaven);
            break;
            break;
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
            mu_swprintf(Text, L"%ls +%d", I18N::Game::BoxOfKundun, iLevel - 7);
            break;
        case 13:
            mu_swprintf(Text, I18N::Game::HeartOfDarkLord);
            break;
        case 14:
            mu_swprintf(Text, I18N::Game::BlueLuckyPouch);
            break;
            break;

        case 15:
            mu_swprintf(Text, I18N::Game::RedLuckyPouch);
            break;
            break;
        }
    }
    else if (iType == ITEM_FRUITS)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::ENG, p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::STA, p->Name);
            break;
        case 2:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::AGI, p->Name);
            break;
        case 3:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::STR, p->Name);
            break;
        case 4:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::Command, p->Name);
            break;
        }
    }
    else if (iType == ITEM_LOCHS_FEATHER)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::CrestOfMonarch);
            break;
        }
    }
    else if (iType == ITEM_SPIRIT)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::DarkHorse, p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls %ls", I18N::Game::DarkRaven, p->Name);
            break;
        }
    }
    else if (iType == ITEM_POTION + 21)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::Stone);
            break;
        case 2:
            mu_swprintf(Text, L"%ls", I18N::Game::StoneOfFriendship);
            break;
        case 3:
            mu_swprintf(Text, L"%ls", I18N::Game::SignOfLord);
            break;
        }
    }
    else if (iType == ITEM_WEAPON_OF_ARCHANGEL)
    {
        mu_swprintf(Text, L"%ls", I18N::Game::AbsoluteWeaponOfArchangel);
    }
    else if (iType == ITEM_WIZARDS_RING)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::RingOfWarrior);
            break;
        case 2:
            mu_swprintf(Text, L"%ls", I18N::Game::RingOfWarrior);
            break;
        case 3:
            mu_swprintf(Text, L"%ls", I18N::Game::RingOfGlory);
            break;
        }
    }
    else if (iType == ITEM_ALE)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::OliveOfLove);
            break;
        }
    }
    else if (iType == ITEM_ORB_OF_SUMMONING)
    {
        mu_swprintf(Text, L"%ls %ls", SkillAttribute[30 + iLevel].Name, I18N::Game::Jewel);
    }
    else if (iType == ITEM_RED_RIBBON_BOX)
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType == ITEM_GREEN_RIBBON_BOX)
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType == ITEM_BLUE_RIBBON_BOX)
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType == ITEM_SCROLL_OF_FIRE_SCREAM)
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType >= ITEM_PUMPKIN_OF_LUCK && iType <= ITEM_JACK_OLANTERN_DRINK)
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType == ITEM_PINK_CHOCOLATE_BOX)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::LilacCandyBox);
            break;
        }
    }
    else if (iType == ITEM_RED_CHOCOLATE_BOX)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::OrangeCandyBox);
            break;
        }
    }
    else if (iType == ITEM_BLUE_CHOCOLATE_BOX)
    {
        switch (iLevel)
        {
        case 0:
            mu_swprintf(Text, L"%ls", p->Name);
            break;
        case 1:
            mu_swprintf(Text, L"%ls", I18N::Game::NavyCandyBox);
            break;
        }
    }
    else if (iType == ITEM_TRANSFORMATION_RING)
    {
        for (int i = 0; i < MAX_MONSTER; i++)
        {
            if (ItemRulesDetail::SommonTable[iLevel] == MonsterScript[i].Type)
            {
                mu_swprintf(Text, L"%ls %ls", MonsterScript[i].Name,
                            I18N::Game::TransformationRing);
            }
        }
    }
    else if (iType >= ITEM_WINGS_OF_SPIRITS && iType <= ITEM_WINGS_OF_DARKNESS)
    {
        if (iLevel == 0)
            mu_swprintf(Text, L"%ls", p->Name);
        else
            mu_swprintf(Text, L"%ls +%d", p->Name, iLevel);
    }
    else if ((iType >= ITEM_WING_OF_STORM && iType <= ITEM_CAPE_OF_EMPEROR) ||
             (iType >= ITEM_WINGS_OF_DESPAIR && iType <= ITEM_WING_OF_DIMENSION) ||
             (iType == ITEM_CAPE_OF_OVERRULE))
    {
        if (iLevel == 0)
            mu_swprintf(Text, L"%ls", p->Name);
        else
            mu_swprintf(Text, L"%ls +%d", p->Name, iLevel);
    }
    else if (ItemRulesDetail::IsDivineArchangelWeaponItem(iType))
    {
        if (iLevel == 0)
            mu_swprintf(Text, L"%ls", p->Name);
        else
            mu_swprintf(Text, L"%ls +%d", p->Name, iLevel);
    }
    else if (COMGEM::NOGEM != Check_Jewel_Com(iType))
    {
        mu_swprintf(Text, L"%ls +%d", p->Name, iLevel + 1);
    }
    else if (iType == INDEX_COMPILED_CELE)
    {
        mu_swprintf(Text, L"%ls +%d", I18N::Game::JewelOfBless, iLevel + 1);
    }
    else if (iType == INDEX_COMPILED_SOUL)
    {
        mu_swprintf(Text, L"%ls +%d", I18N::Game::JewelOfSoul, iLevel + 1);
    }
    else if ((iType >= ITEM_SEED_FIRE && iType <= ITEM_SEED_EARTH) ||
             (iType >= ITEM_SPHERE_MONO && iType <= ITEM_SPHERE_5) ||
             (iType >= ITEM_SEED_SPHERE_FIRE_1 && iType <= ITEM_SEED_SPHERE_EARTH_5))
    {
        mu_swprintf(Text, L"%ls", p->Name);
    }
    else if (iType == ITEM_SIEGE_POTION)
    {
        int iTextIndex = 0;
        iTextIndex = (iLevel == 0) ? 1413 : 1414;
        mu_swprintf(Text, L"%ls", I18N::Game::Lookup(iTextIndex));
    }
    else
    {
        if (iLevel == 0)
            mu_swprintf(Text, L"%ls", p->Name);
        else
            mu_swprintf(Text, L"%ls +%d", p->Name, iLevel);
    }
}

bool SessionGameDataUnit::IsHighValueItem(ITEM *pItem) const
{
    int iLevel = pItem->Level;

    if (pItem->Type == ITEM_HORN_OF_DINORANT || pItem->Type == ITEM_JEWEL_OF_BLESS ||
        pItem->Type == ITEM_JEWEL_OF_SOUL || pItem->Type == ITEM_JEWEL_OF_LIFE ||
        pItem->Type == ITEM_JEWEL_OF_CREATION || pItem->Type == ITEM_JEWEL_OF_CHAOS ||
        pItem->Type == ITEM_JEWEL_OF_GUARDIAN || pItem->Type == ITEM_PACKED_JEWEL_OF_BLESS ||
        pItem->Type == ITEM_PACKED_JEWEL_OF_SOUL ||
        (pItem->Type >= ITEM_WING && pItem->Type <= ITEM_WINGS_OF_DARKNESS) ||
        pItem->Type == ITEM_DARK_HORSE_ITEM || pItem->Type == ITEM_DARK_RAVEN_ITEM ||
        pItem->Type == ITEM_CAPE_OF_LORD ||
        (pItem->Type >= ITEM_WING_OF_STORM && pItem->Type <= ITEM_WING_OF_DIMENSION) ||
        pItem->AncientDiscriminator > 0 ||
        ItemRulesDetail::IsDivineArchangelWeaponItem(pItem->Type) ||
        pItem->Type == ITEM_LOCHS_FEATHER || pItem->Type == ITEM_FRUITS ||
        pItem->Type == ITEM_WEAPON_OF_ARCHANGEL || pItem->Type == ITEM_SPIRIT ||
        (pItem->Type >= ITEM_GEMSTONE && pItem->Type <= ITEM_HIGHER_REFINE_STONE) ||
        (iLevel > 6 && pItem->Type < ITEM_WING) || pItem->ExcellentFlags > 0 ||
        (pItem->Type >= ITEM_CLAW_OF_BEAST && pItem->Type <= ITEM_HORN_OF_FENRIR) ||
        pItem->Type == ITEM_FLAME_OF_CONDOR || pItem->Type == ITEM_FEATHER_OF_CONDOR ||
        pItem->Type == ITEM_POTION + 121 || pItem->Type == ITEM_POTION + 122 ||
        pItem->Type == ITEM_WING + 130 || pItem->Type == ITEM_WING + 131 ||
        pItem->Type == ITEM_WING + 132 || pItem->Type == ITEM_WING + 133 ||
        pItem->Type == ITEM_WING + 134 || pItem->Type == ITEM_WING + 135 ||
        pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON ||
        pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN ||
        pItem->Type == ITEM_HELPER + 109 || pItem->Type == ITEM_HELPER + 110 ||
        pItem->Type == ITEM_HELPER + 111 || pItem->Type == ITEM_HELPER + 112 ||
        pItem->Type == ITEM_HELPER + 113 || pItem->Type == ITEM_HELPER + 114 ||
        pItem->Type == ITEM_HELPER + 115 || pItem->Type == ITEM_POTION + 112 ||
        pItem->Type == ITEM_POTION + 113 || (pItem->Type == ITEM_WIZARDS_RING && iLevel == 0)
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (g_pMyInventory->IsInvenItem(pItem->Type) && pItem->Durability == 255)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || (pItem->Type >= ITEM_CAPE_OF_FIGHTER && pItem->Type <= ITEM_CAPE_OF_OVERRULE)
#ifdef KJH_FIX_SELL_LUCKYITEM
        || (Check_ItemAction(pItem, eITEM_SELL) && pItem->Durability > 0)
#endif // KJH_FIX_SELL_LUCKYITEM
        || (isCompiledGem(pItem)))
    {
        if (true == pItem->bPeriodItem && false == pItem->bExpiredPeriod)
        {
            return false;
        }
        else if (pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
                 pItem->Type == ITEM_DEMON || pItem->Type == ITEM_SPIRIT_OF_GUARDIAN ||
                 pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING ||
                 pItem->Type == ITEM_PET_SKELETON)
        {
            if (true == pItem->bPeriodItem && true == pItem->bExpiredPeriod)
            {
                return true;
            }
            else
                return false;
        }
        return true;
    }

    return false;
}

bool SessionGameDataUnit::IsTradeBan(ITEM *pItem)
{
    if (pItem->Type == ITEM_MOONSTONE_PENDANT || pItem->Type == ITEM_ELITE_TRANSFER_SKELETON_RING ||
        (pItem->Type == ITEM_POTION + 21 && pItem->Level != 3) ||
        (pItem->Type >= ITEM_SCROLL_OF_EMPEROR_RING_OF_HONOR &&
         pItem->Type <= ITEM_SOUL_SHARD_OF_WIZARD) ||
        pItem->Type == ITEM_WEAPON_OF_ARCHANGEL ||
        (pItem->Type == ITEM_BOX_OF_LUCK && pItem->Level == 13) ||
        (pItem->Type >= ITEM_HELPER + 43 && pItem->Type <= ITEM_HELPER + 45) ||
        (pItem->Type == ITEM_WIZARDS_RING && pItem->Level != 0) ||
        pItem->Type == ITEM_POTION + 64 || pItem->Type == ITEM_FLAME_OF_DEATH_BEAM_KNIGHT ||
        pItem->Type == ITEM_HORN_OF_HELL_MAINE || pItem->Type == ITEM_FEATHER_OF_DARK_PHOENIX ||
        pItem->Type == ITEM_EYE_OF_ABYSSAL || IsPartChargeItem(pItem) ||
        pItem->Type == ITEM_HELPER + 97 || pItem->Type == ITEM_HELPER + 98 ||
        pItem->Type == ITEM_POTION + 91 || pItem->Type == ITEM_HELPER + 99 ||
        pItem->Type == ITEM_PET_PANDA || pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON
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

    if (pItem->Type == ITEM_GM_GIFT)
    {
        if (g_isCharacterBuff((&Hero->Object), eBuff_GMEffect) ||
            (Hero->CtlCode == CTLCODE_20OPERATOR) || (Hero->CtlCode == CTLCODE_08OPERATOR))
            return false;
        else
            return true;
    }
    if (Check_ItemAction(pItem, eITEM_TRADE))
        return true;

    return false;
}

bool SessionGameDataUnit::IsRepairBan(ITEM *pItem) const
{
    if (g_ChangeRingMgr->CheckRepair(pItem->Type) == true)
    {
        return true;
    }
    if (IsPartChargeItem(pItem) == true || ((pItem->Type >= ITEM_TYPE_CHARM_MIXWING + EWS_BEGIN) &&
                                            (pItem->Type <= ITEM_TYPE_CHARM_MIXWING + EWS_END)))
    {
        return true;
    }

    if ((pItem->Type >= ITEM_POTION + 55 && pItem->Type <= ITEM_POTION + 57) ||
        pItem->Type == ITEM_HELPER + 43 || pItem->Type == ITEM_HELPER + 44 ||
        pItem->Type == ITEM_HELPER + 45 ||
        (pItem->Type >= ITEM_HELPER && pItem->Type <= ITEM_HORN_OF_DINORANT) ||
        pItem->Type == ITEM_BOLT || pItem->Type == ITEM_ARROWS || pItem->Type >= ITEM_POTION ||
        (pItem->Type >= ITEM_ORB_OF_TWISTING_SLASH && pItem->Type <= ITEM_ORB_OF_DEATH_STAB) ||
        (pItem->Type >= ITEM_LOCHS_FEATHER && pItem->Type <= ITEM_WEAPON_OF_ARCHANGEL) ||
        pItem->Type == ITEM_POTION + 21 || pItem->Type == ITEM_DARK_HORSE_ITEM ||
        pItem->Type == ITEM_DARK_RAVEN_ITEM || pItem->Type == ITEM_MOONSTONE_PENDANT ||
        pItem->Type == ITEM_PET_RUDOLF || pItem->Type == ITEM_PET_PANDA ||
        pItem->Type == ITEM_PANDA_TRANSFORMATION_RING ||
        pItem->Type == ITEM_SKELETON_TRANSFORMATION_RING || pItem->Type == ITEM_PET_SKELETON ||
        pItem->Type == ITEM_PET_UNICORN || pItem->Type == ITEM_CHERRY_BLOSSOM_PLAYBOX ||
        pItem->Type == ITEM_CHERRY_BLOSSOM_WINE || pItem->Type == ITEM_CHERRY_BLOSSOM_RICE_CAKE ||
        pItem->Type == ITEM_CHERRY_BLOSSOM_FLOWER_PETAL || pItem->Type == ITEM_POTION + 88 ||
        pItem->Type == ITEM_POTION + 89 || pItem->Type == ITEM_GOLDEN_CHERRY_BLOSSOM_BRANCH
#ifdef LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || g_pMyInventory->IsInvenItem(pItem->Type)
#endif //LJH_ADD_SYSTEM_OF_EQUIPPING_ITEM_FROM_INVENTORY
        || pItem->Type == ITEM_HELPER + 7 || pItem->Type == ITEM_TRANSFORMATION_RING ||
        pItem->Type == ITEM_LIFE_STONE_ITEM || pItem->Type == ITEM_WIZARDS_RING ||
        pItem->Type == ITEM_ARMOR_OF_GUARDSMAN || pItem->Type == ITEM_SPLINTER_OF_ARMOR ||
        pItem->Type == ITEM_BLESS_OF_GUARDIAN || pItem->Type == ITEM_CLAW_OF_BEAST ||
        pItem->Type == ITEM_FRAGMENT_OF_HORN || pItem->Type == ITEM_BROKEN_HORN ||
        pItem->Type == ITEM_HORN_OF_FENRIR || pItem->Type == ITEM_OLD_SCROLL ||
        pItem->Type == ITEM_ILLUSION_SORCERER_COVENANT || pItem->Type == ITEM_SCROLL_OF_BLOOD ||
        pItem->Type == ITEM_HELPER + 66 || pItem->Type == ITEM_HELPER + 71 ||
        pItem->Type == ITEM_HELPER + 72 || pItem->Type == ITEM_HELPER + 73 ||
        pItem->Type == ITEM_HELPER + 74 || pItem->Type == ITEM_HELPER + 75)
    {
        return true;
    }

    if (Check_ItemAction(pItem, eITEM_REPAIR))
        return true;

    return false;
}

std::wstring SessionGameDataUnit::GetItemDisplayName(ITEM *pItem)
{
    // NOTE:
    // There may be already another function for this (the one being used for displaying dropped items).
    // This version currently only applies to ascii names of items

    ITEM_ATTRIBUTE *pAttr = &ItemAttribute[pItem->Type];

    auto nNameLen = wcsnlen(pAttr->Name, MAX_ITEM_NAME);
    std::wstring strDisplayName(pAttr->Name, nNameLen);
    std::wstring strOptions;

    if (pItem->Level)
    {
        strOptions += L"+" + std::to_wstring(pItem->Level);
    }
    if (pItem->HasSkill)
    {
        strOptions += L"+Skill"; // TODO: Use I18N::Game::Skill
    }
    if (pItem->OptionLevel)
    {
        strOptions += L"+Option"; // TODO: Use I18N::Game::Option
    }
    if (pItem->HasLuck)
    {
        strOptions += L"+Luck"; // TODO: Use I18N::Game::Luck
    }

    return strDisplayName + (strOptions.empty() ? L"" : L" " + strOptions);
}
