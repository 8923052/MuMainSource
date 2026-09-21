#include "ui/features/Hud/HudLogic.h"
#include "I18N/All.h"
#include "app/AppWindow.h"
#include "app/ApplicationAudio.h"
#include "app/ApplicationConfigScheduling.h"
#include "app/ApplicationKeeper.h"
#include "app/ApplicationLoopFrame.h"
#include "app/ApplicationNetwork.h"
#include "data/CharacterData.h"
#include "data/GameData.h"
#include "data/ItemData.h"
#include "data/Localization.h"
#include "data/WorldData.h"
#include "domain/Automation.h"
#include "domain/CharacterPresentation.h"
#include "domain/CharacterSystem.h"
#include "domain/ChatSocial.h"
#include "domain/EffectsUpdate.h"
#include "domain/Events.h"
#include "domain/ItemsSkills.h"
#include "domain/MapSimulation.h"
#include "domain/MovementAI.h"
#include "domain/Quests.h"
#include "domain/Shop.h"
#include "domain/WorldSimulation.h"
#include "network/generated/PacketFunctions_ClientToServer.h"
#include "render/ModelResources.h"
#include "render/Terrain.h"
#include "render/Text.h"
#include "render/Textures.h"
#include "render/World.h"
#include "session/SessionAudio.h"
#include "session/SessionGameplay.h"
#include "session/SessionKeeper.h"
#include "session/SessionNetwork.h"
#include "session/SessionPresentation.h"
#include "session/SessionRender.h"
#include "session/SessionUi.h"
#include "session/SessionWorkspace.h"
#include "support/CoreMath.h"
#include "support/Scenes.h"
#include "ui/features/Dialogs/DialogsLogic.h"
#include "ui/features/Hud/HudRender.h"
#include "ui/features/Items/ItemsLogic.h"
#include "ui/features/Items/ItemsRender.h"
#include "ui/features/Social/SocialLogic.h"
#include "ui/features/World/WorldLogic.h"
#include "ui/runtime/UiControls.h"
#include "ui/runtime/UiRuntime.h"
#include "ui/session/UiSessionLogic.h"
#include "ui/session/UiSessionRender.h"

namespace UI::Skills::Tooltip
{

namespace
{
constexpr int GLOBAL_TEXT_REQUIRED_LEVEL = 76;
constexpr int GLOBAL_TEXT_REQUIRED_STRENGTH = 73;
constexpr int GLOBAL_TEXT_REQUIRED_DEXTERITY = 75;
constexpr int GLOBAL_TEXT_REQUIRED_ENERGY = 77;
constexpr int GLOBAL_TEXT_REQUIRED_CHARISMA = 698;
constexpr int GLOBAL_TEXT_NEED_MORE_STAT = 74;
constexpr int GLOBAL_TEXT_MASTERY_TYPE_BASE = 1080;

// SkillAttribute.Delay is stored in milliseconds; the tooltip displays it as
// seconds with one decimal. No GlobalText entry exists for this line yet, so
// the format string lives here until a localized one is added.
constexpr wchar_t kCooldownFormat[] = L"Cooldown: %.1f sec";
constexpr float kMillisPerSecond = 1000.0f;

bool IsCastleSiegeOnlySkill(int skillType)
{
    switch (skillType)
    {
    case AT_SKILL_RUSH:
    case AT_SKILL_SPACE_SPLIT:
    case AT_SKILL_DEEPIMPACT:
    case AT_SKILL_JAVELIN:
    case AT_SKILL_SPIRAL_SLASH:
    case AT_SKILL_DEATH_CANNON:
    case AT_SKILL_OCCUPY:
        return true;
    default:
        return false;
    }
}

// ---- Tiny line emitters. All operate on outModel.count; bounds are clamped
//      to MAX_TOOLTIP_LINES to avoid overflowing the fixed buffer.

Line &NextSlot(Model &m)
{
    if (m.count >= MAX_TOOLTIP_LINES)
        return m.lines[MAX_TOOLTIP_LINES - 1];
    return m.lines[m.count++];
}

void AddBlank(Model &m)
{
    Line &l = NextSlot(m);
    l.text[0] = L'\n';
    l.text[1] = L'\0';
    l.color = LineColor::White;
    l.isBold = false;
    l.isBlank = true;
    ++m.skipCount;
}

void AddRaw(Model &m, const wchar_t *text, LineColor color, bool bold = false)
{
    Line &l = NextSlot(m);
    wcsncpy(l.text, text, MAX_TOOLTIP_LINE_TEXT - 1);
    l.text[MAX_TOOLTIP_LINE_TEXT - 1] = L'\0';
    l.color = color;
    l.isBold = bold;
    l.isBlank = false;
}

void AddFormatted(Model &m, int globalTextIdx, LineColor color, int v1)
{
    Line &l = NextSlot(m);
    mu_swprintf(l.text, I18N::Game::Lookup(globalTextIdx), v1);
    l.color = color;
    l.isBold = false;
    l.isBlank = false;
}

void AddFormatted(Model &m, int globalTextIdx, LineColor color, int v1, int v2)
{
    Line &l = NextSlot(m);
    mu_swprintf(l.text, I18N::Game::Lookup(globalTextIdx), v1, v2);
    l.color = color;
    l.isBold = false;
    l.isBlank = false;
}

void AddFormattedWide(Model &m, int globalTextIdx, LineColor color, const wchar_t *arg)
{
    Line &l = NextSlot(m);
    mu_swprintf(l.text, I18N::Game::Lookup(globalTextIdx), arg);
    l.color = color;
    l.isBold = false;
    l.isBlank = false;
}

// Requirement line. In game mode, compares value vs current and colors
// red/white accordingly, optionally emitting a `(lacking N)` deficit line.
// In editor mode (currentValue == -1), always white, no deficit line.
void AddRequirementLine(Model &m, int requiredValue, int currentValue, int reqStringIndex)
{
    if (requiredValue <= 0)
        return;

    const bool editorMode = (currentValue < 0);
    const bool requirementMet = editorMode || (currentValue >= requiredValue);

    {
        Line &l = NextSlot(m);
        mu_swprintf(l.text, I18N::Game::Lookup(reqStringIndex), requiredValue);
        l.color = requirementMet ? LineColor::White : LineColor::Red;
        l.isBold = false;
        l.isBlank = false;
    }

    if (editorMode || requirementMet)
        return;

    Line &deficit = NextSlot(m);
    mu_swprintf(deficit.text, I18N::Game::Lookup(GLOBAL_TEXT_NEED_MORE_STAT),
                requiredValue - currentValue);
    deficit.color = LineColor::Red;
    deficit.isBold = false;
    deficit.isBlank = false;
}

// Section emitters. Tooltip layout, top to bottom:
//   [HEADER]        blank (top padding), name (bold blue), blank
//   [BANNER TOP]    red-bg flavor banners (e.g. Infinity Arrow info)
//   [BODY damage]   character-specific damage / buff values
//   [BODY stats]    range, mana, ability gauge
//   [REQUIREMENTS]  level / str / dex / energy / cha (color-coded in game mode)
//   [BANNER BOTTOM] red-bg warnings, brand info, flavor notes
//   [BLUE TAGS]     blue descriptor tags (mastery type, Expansion of Wizardry)
// Each populated section ends with a blank line, which the renderer draws
// at half text-height — giving ~5px spacing between sections and bottom
// padding. Empty sections add nothing, so absent content doesn't compound
// the gap.
// Each emitter is called unconditionally from BuildModel; the emitter
// itself decides whether the section has anything to add.

// Emit a trailing blank as a section separator, but only if the section
// actually added at least one line.
void EndSection(Model &m, int countBeforeSection)
{
    if (m.count > countBeforeSection)
        AddBlank(m);
}

void EmitHeader(Model &m, const wchar_t *name)
{
    AddBlank(m);
    AddRaw(m, name, LineColor::Blue, /*bold=*/true);
    AddBlank(m);
}

void EmitTopBanners(Model &m, int skillType)
{
    const int before = m.count;
    if (skillType == AT_SKILL_INFINITY_ARROW || skillType == AT_SKILL_INFINITY_ARROW_STR)
    {
        AddRaw(m, I18N::Game::ArrowWillNotDecreaseDuringActivation, LineColor::DarkRed);
    }
    EndSection(m, before);
}

} // namespace

DamageContext Builder::BuildDamageContext(int skillType)
{
    auto *const CharacterAttribute = &characterMachine_.Character;
    DamageContext ctx{};
    ctx.heroClass = gCharacterManager.GetBaseClass(Hero->Class);
    ctx.dexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
    ctx.energy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
    ctx.strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
    ctx.vitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
    ctx.charisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;

    gameplay_.GetMagicSkillDamage(skillType, &ctx.magicMin, &ctx.magicMax);
    gameplay_.GetSkillDamage(skillType, &ctx.skillMin, &ctx.skillMax);

    int attackMin = 0, attackMax = 0;
    GetAttackDamage(&attackMin, &attackMax);
    ctx.skillMin += attackMin;
    ctx.skillMax += attackMax;

    // Jewel of Harmony skill-power bonuses from equipped weapons.
    StrengthenCapability rightinfo{}, leftinfo{};
    ITEM *rightweapon = &characterMachine_.Equipment[EQUIPMENT_WEAPON_RIGHT];
    ITEM *leftweapon = &characterMachine_.Equipment[EQUIPMENT_WEAPON_LEFT];
    if (g_pUIJewelHarmonyinfo && rightweapon->Level >= rightweapon->Jewel_Of_Harmony_OptionLevel)
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&rightinfo, rightweapon, 1);
    if (g_pUIJewelHarmonyinfo && leftweapon->Level >= leftweapon->Jewel_Of_Harmony_OptionLevel)
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&leftinfo, leftweapon, 1);
    if (rightinfo.SI_isSP)
    {
        ctx.skillAttackPowerRate += rightinfo.SI_SP.SI_skillattackpower;
        ctx.skillAttackPowerRate += rightinfo.SI_SP.SI_magicalpower;
    }
    if (leftinfo.SI_isSP)
        ctx.skillAttackPowerRate += leftinfo.SI_SP.SI_skillattackpower;

    return ctx;
}

// Wizard/Summoner: Soul Barrier specifics + regular magical damage.
void Builder::EmitMagicalDamage(Model &m, int skillType, const DamageContext &ctx)
{
    auto *const CharacterAttribute = &characterMachine_.Character;
    if (ctx.heroClass != CLASS_WIZARD && ctx.heroClass != CLASS_SUMMONER)
        return;

    if (skillType == AT_SKILL_SOUL_BARRIER || skillType == AT_SKILL_SOUL_BARRIER_STR ||
        skillType == AT_SKILL_SOUL_BARRIER_PROFICIENCY)
    {
        int damageShield = (int)(10 + (ctx.dexterity / 50.f) + (ctx.energy / 200.f));
        if (skillType == AT_SKILL_SOUL_BARRIER_STR ||
            skillType == AT_SKILL_SOUL_BARRIER_PROFICIENCY)
        {
            auto additionalValue = CharacterAttribute->MasterSkillInfo[skillType].GetSkillValue();
            damageShield += (int)additionalValue;
        }
        int deleteMana = (int)(CharacterAttribute->ManaMax * 0.02f);
        int limitTime = (int)(60 + (ctx.energy / 40.f));

        AddFormatted(m, 578, LineColor::White, damageShield);
        AddFormatted(m, 880, LineColor::White, deleteMana);
        AddFormatted(m, 881, LineColor::White, limitTime);
        return;
    }

    if (skillType == AT_SKILL_EXPANSION_OF_WIZARDRY ||
        skillType == AT_SKILL_EXPANSION_OF_WIZARDRY_STR ||
        skillType == AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY || skillType == AT_SKILL_ALICE_SLEEP ||
        skillType == AT_SKILL_ALICE_SLEEP_STR)
        return;

    const WORD s = skillType;
    if ((AT_SKILL_STUN <= s && s <= AT_SKILL_MANA) ||
        (AT_SKILL_ALICE_THORNS <= s && s <= AT_SKILL_ALICE_ENERVATION) || s == AT_SKILL_TELEPORT ||
        s == AT_SKILL_TELEPORT_ALLY)
        return;

    if (AT_SKILL_SUMMON_EXPLOSION <= s && s <= AT_SKILL_SUMMON_POLLUTION)
    {
        int curseMin = 0, curseMax = 0;
        gameplay_.GetCurseSkillDamage(s, &curseMin, &curseMax);
        AddFormatted(m, 1692, LineColor::White, curseMin, curseMax);
        return;
    }

    AddFormatted(m, 170, LineColor::White, ctx.magicMin + ctx.skillAttackPowerRate,
                 ctx.magicMax + ctx.skillAttackPowerRate);
}

void BuilderLegacyCalls::EmitMagicalDamage(Model &model, int skillType,
                                           const DamageContext &context)
{
    owner_.EmitMagicalDamage(model, skillType, context);
}

namespace
{

// Knight/Dark/Elf/Dark Lord/Rage Fighter: physical skill damage.
void EmitPhysicalDamage(Model &m, int skillType, const DamageContext &ctx)
{
    if (ctx.heroClass != CLASS_KNIGHT && ctx.heroClass != CLASS_DARK &&
        ctx.heroClass != CLASS_ELF && ctx.heroClass != CLASS_DARK_LORD &&
        ctx.heroClass != CLASS_RAGEFIGHTER)
        return;

    switch (skillType)
    {
    case AT_SKILL_TELEPORT:
    case AT_SKILL_TELEPORT_ALLY:
    case AT_SKILL_SOUL_BARRIER:
    case AT_SKILL_SOUL_BARRIER_STR:
    case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
    case AT_SKILL_BLOCKING:
    case AT_SKILL_SWELL_LIFE:
    case AT_SKILL_SWELL_LIFE_STR:
    case AT_SKILL_SWELL_LIFE_PROFICIENCY:
    case AT_SKILL_HEALING:
    case AT_SKILL_HEALING_STR:
    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY:
    case AT_SKILL_ATTACK:
    case AT_SKILL_ATTACK_STR:
    case AT_SKILL_ATTACK_MASTERY:
    case AT_SKILL_SUMMON:
    case AT_SKILL_SUMMON + 1:
    case AT_SKILL_SUMMON + 2:
    case AT_SKILL_SUMMON + 3:
    case AT_SKILL_SUMMON + 4:
    case AT_SKILL_SUMMON + 5:
    case AT_SKILL_SUMMON + 6:
    case AT_SKILL_SUMMON + 7:
    case AT_SKILL_IMPROVE_AG:
    case AT_SKILL_STUN:
    case AT_SKILL_REMOVAL_STUN:
    case AT_SKILL_MANA:
    case AT_SKILL_INVISIBLE:
    case AT_SKILL_REMOVAL_INVISIBLE:
    case AT_SKILL_REMOVAL_BUFF:
    case AT_SKILL_INFINITY_ARROW:
    case AT_SKILL_INFINITY_ARROW_STR:
    case AT_SKILL_PARTY_TELEPORT:
    case AT_SKILL_ADD_CRITICAL:
    case AT_SKILL_ADD_CRITICAL_STR1:
    case AT_SKILL_ADD_CRITICAL_STR2:
    case AT_SKILL_ADD_CRITICAL_STR3:
    case AT_SKILL_BRAND_OF_SKILL:
    case AT_SKILL_PLASMA_STORM_FENRIR:
    case AT_SKILL_RECOVER:
    case AT_SKILL_ATT_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY:
        return;
    case AT_SKILL_EARTHSHAKE:
    case AT_SKILL_EARTHSHAKE_STR:
    case AT_SKILL_EARTHSHAKE_MASTERY:
        AddRaw(m, I18N::Game::CheckTheDetailsInPetInformationWindow, LineColor::DarkRed);
        return;
    default:
        AddFormatted(m, 879, LineColor::White, ctx.skillMin,
                     ctx.skillMax + ctx.skillAttackPowerRate);
        return;
    }
}

// Elf buff-value computations (Healing/Defense/Attack/Recover).
void EmitElfBuffValues(Model &m, int skillType, const DamageContext &ctx,
                       CHARACTER_ATTRIBUTE &CharacterAttribute)
{
    if (ctx.heroClass != CLASS_ELF)
        return;

    switch (skillType)
    {
    case AT_SKILL_HEALING_STR: {
        int value = (ctx.energy / 5) + 5;
        auto boostPercent =
            CharacterAttribute.MasterSkillInfo[AT_SKILL_HEALING_STR].GetSkillValue();
        value += static_cast<int>((value * boostPercent) / 100.0);
        AddFormatted(m, 171, LineColor::White, value);
        return;
    }
    case AT_SKILL_HEALING:
        AddFormatted(m, 171, LineColor::White, ctx.energy / 5 + 5);
        return;
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY: {
        int value = ctx.energy / 8 + 2;
        auto boostPercent =
            CharacterAttribute.MasterSkillInfo[AT_SKILL_DEFENSE_STR].GetSkillValue();
        auto masteryBoostPercent =
            CharacterAttribute.MasterSkillInfo[AT_SKILL_DEFENSE_MASTERY].GetSkillValue();
        value += static_cast<int>((value * (boostPercent + masteryBoostPercent)) / 100.0);
        AddFormatted(m, 172, LineColor::White, value);
        return;
    }
    case AT_SKILL_DEFENSE:
        AddFormatted(m, 172, LineColor::White, ctx.energy / 8 + 2);
        return;
    case AT_SKILL_ATTACK_STR:
    case AT_SKILL_ATTACK_MASTERY: {
        int value = ctx.energy / 7 + 3;
        auto boostPercent = CharacterAttribute.MasterSkillInfo[AT_SKILL_ATTACK_STR].GetSkillValue();
        auto masteryBoostPercent =
            CharacterAttribute.MasterSkillInfo[AT_SKILL_ATTACK_MASTERY].GetSkillValue();
        value += static_cast<int>((value * (boostPercent + masteryBoostPercent)) / 100.0);
        AddFormatted(m, 173, LineColor::White, value);
        return;
    }
    case AT_SKILL_ATTACK:
        AddFormatted(m, 173, LineColor::White, ctx.energy / 7 + 3);
        return;
    case AT_SKILL_RECOVER: {
        int cal = ctx.energy / 4;
        AddFormatted(m, 1782, LineColor::White,
                     (int)((float)cal + (float)CharacterAttribute.Level));
        return;
    }
    default:
        return;
    }
}

void EmitBodyStats(Model &m, int skillType, int iDistance, int iMana, int iSkillMana, int iDelayMs)
{
    const int before = m.count;
    if (skillType != AT_SKILL_EXPANSION_OF_WIZARDRY &&
        skillType != AT_SKILL_EXPANSION_OF_WIZARDRY_STR &&
        skillType != AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY)
    {
        if (iDistance)
            AddFormatted(m, 174, LineColor::White, iDistance);
    }

    AddFormatted(m, 175, LineColor::White, iMana);
    if (iSkillMana > 0)
        AddFormatted(m, 360, LineColor::White, iSkillMana);

    if (iDelayMs > 0)
    {
        wchar_t buf[MAX_TOOLTIP_LINE_TEXT];
        mu_swprintf(buf, kCooldownFormat, iDelayMs / kMillisPerSecond);
        AddRaw(m, buf, LineColor::White);
    }
    EndSection(m, before);
}

} // namespace

void Builder::EmitBottomBanners(Model &m, const BuildOptions &options, int skillType)
{
    auto *const CharacterAttribute = &characterMachine_.Character;
    const int before = m.count;

    // Knight extension info banners. These describe the skill itself (weapon
    // requirement, combo membership), so the editor shows them unconditionally.
    // In game mode, the original gating stays: Knight class + extension + lvl
    // 220, so the player only sees them when relevant.
    const bool gameMode = options.includeCharacterSpecific;
    const bool gameModeKnight =
        gameMode && gCharacterManager.GetBaseClass(Hero->Class) == CLASS_KNIGHT;
    const bool gameModeKnightExt =
        gameModeKnight && Hero->byExtensionSkill == 1 && CharacterAttribute->Level >= 220;

    if ((!gameMode || gameModeKnight) && skillType == AT_SKILL_IMPALE)
    {
        AddRaw(m, I18N::Game::CanOnlyBeUsedInMovingUnit, LineColor::DarkRed);
    }

    if (!gameMode || gameModeKnightExt)
    {
        if ((skillType >= AT_SKILL_FALLING_SLASH && skillType <= AT_SKILL_SLASH) ||
            skillType == AT_SKILL_FALLING_SLASH_STR || skillType == AT_SKILL_LUNGE_STR ||
            skillType == AT_SKILL_CYCLONE_STR || skillType == AT_SKILL_CYCLONE_STR_MG ||
            skillType == AT_SKILL_SLASH_STR || skillType == AT_SKILL_TWISTING_SLASH ||
            skillType == AT_SKILL_TWISTING_SLASH_STR ||
            skillType == AT_SKILL_TWISTING_SLASH_STR_MG ||
            skillType == AT_SKILL_TWISTING_SLASH_MASTERY || skillType == AT_SKILL_RAGEFUL_BLOW ||
            skillType == AT_SKILL_RAGEFUL_BLOW_STR || skillType == AT_SKILL_RAGEFUL_BLOW_MASTERY ||
            skillType == AT_SKILL_DEATHSTAB || skillType == AT_SKILL_DEATHSTAB_STR)
        {
            AddRaw(m, I18N::Game::Combo, LineColor::DarkRed);
        }
        else if (skillType == AT_SKILL_STRIKE_OF_DESTRUCTION ||
                 skillType == AT_SKILL_STRIKE_OF_DESTRUCTION_STR)
        {
            AddRaw(m, I18N::Game::CombinationAvailable2StepOnly, LineColor::DarkRed);
        }
    }

    // Skill brand / master info (both modes - data-only).
    const int BrandType = SkillAttribute[skillType].SkillBrand;
    const int SkillUseType = SkillAttribute[skillType].SkillUseType;
    if (SkillUseType == SKILL_USE_TYPE_BRAND)
    {
        AddFormattedWide(m, 1480, LineColor::DarkRed, SkillAttribute[BrandType].Name);
        AddFormatted(m, 1481, LineColor::DarkRed, SkillAttribute[BrandType].Damage);
    }
    if (SkillUseType == SKILL_USE_TYPE_MASTER)
    {
        AddRaw(m, I18N::Game::ThisIsAMasterSkillInGuildBattleAndCastleSiege, LineColor::DarkRed);
        AddFormatted(m, 1483, LineColor::DarkRed, SkillAttribute[skillType].KillCount);
    }

    // Dark Lord party teleport warning (character-specific).
    if (options.includeCharacterSpecific &&
        gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD &&
        skillType == AT_SKILL_PARTY_TELEPORT && PartyNumber <= 0)
    {
        AddRaw(m, I18N::Game::CanOnlyBeUsedDuringParty, LineColor::DarkRed);
    }

    // Plasma Storm Fenrir extra notes (skill-type-only, both modes).
    if (skillType == AT_SKILL_PLASMA_STORM_FENRIR)
    {
        AddRaw(m, I18N::Game::WhenTheAttackIsSuccessfulItWillDecreaseTheDurabilityOf,
               LineColor::DarkRed);
        AddRaw(m, I18N::Game::OneOfTheCertainWeaponsTo50, LineColor::DarkRed);
    }

    // Castle-siege-only badge.
    if (IsCastleSiegeOnlySkill(skillType))
    {
        AddRaw(m, I18N::Game::OnlyInCastleSiege, LineColor::DarkRed);
    }

    // Stun / Invisible / Buff-removal: usable in CS with kill count.
    if (skillType == AT_SKILL_STUN || skillType == AT_SKILL_REMOVAL_STUN ||
        skillType == AT_SKILL_INVISIBLE || skillType == AT_SKILL_REMOVAL_INVISIBLE ||
        skillType == AT_SKILL_REMOVAL_BUFF)
    {
        AddRaw(m, I18N::Game::CanBeUsedDuringCastleSiegeWithRequiredKillCount, LineColor::DarkRed);
    }

    // Impale flavor note.
    if (skillType == AT_SKILL_IMPALE)
    {
        AddRaw(m, I18N::Game::CanBeUsedFromTheMountItem, LineColor::DarkRed);
    }
    EndSection(m, before);
}

void Builder::EmitBodyDamage(Model &m, int skillType)
{
    const int before = m.count;
    const DamageContext ctx = BuildDamageContext(skillType);
    EmitMagicalDamage(m, skillType, ctx);
    EmitPhysicalDamage(m, skillType, ctx);

    if (skillType == AT_SKILL_PLASMA_STORM_FENRIR)
    {
        int skillDamageBase = 0;
        gSkillManager.GetSkillInformation_Damage(AT_SKILL_PLASMA_STORM_FENRIR, &skillDamageBase);

        int minDamage = 0;
        if (ctx.heroClass == CLASS_KNIGHT || ctx.heroClass == CLASS_DARK)
            minDamage = (ctx.strength / 3) + (ctx.dexterity / 5) + (ctx.vitality / 5) +
                        (ctx.energy / 7) + skillDamageBase;
        else if (ctx.heroClass == CLASS_WIZARD || ctx.heroClass == CLASS_SUMMONER)
            minDamage = (ctx.strength / 5) + (ctx.dexterity / 5) + (ctx.vitality / 7) +
                        (ctx.energy / 3) + skillDamageBase;
        else if (ctx.heroClass == CLASS_ELF)
            minDamage = (ctx.strength / 5) + (ctx.dexterity / 3) + (ctx.vitality / 7) +
                        (ctx.energy / 5) + skillDamageBase;
        else if (ctx.heroClass == CLASS_DARK_LORD)
            minDamage = (ctx.strength / 5) + (ctx.dexterity / 5) + (ctx.vitality / 7) +
                        (ctx.energy / 3) + (ctx.charisma / 3) + skillDamageBase;
        else if (ctx.heroClass == CLASS_RAGEFIGHTER)
            minDamage = (ctx.strength / 5) + (ctx.dexterity / 5) + (ctx.vitality / 3) +
                        (ctx.energy / 7) + skillDamageBase;

        const int maxDamage = minDamage + 30;
        AddFormatted(m, 879, LineColor::White, minDamage, maxDamage + ctx.skillAttackPowerRate);
    }

    EmitElfBuffValues(m, skillType, ctx, characterMachine_.Character);
    EndSection(m, before);
}

void Builder::EmitRequirements(Model &m, const BuildOptions &options, int skillType)
{
    const int before = m.count;

    int reqEnergy = 0;
    gSkillManager.GetSkillInformation_Energy(skillType, &reqEnergy);

    int curLevel = -1, curStr = -1, curDex = -1, curEnergy = -1, curCha = -1;
    if (options.includeCharacterSpecific)
    {
        curLevel = CharacterAttribute->Level;
        curStr = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
        curDex = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        curEnergy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
        curCha = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
    }

    AddRequirementLine(m, SkillAttribute[skillType].Level, curLevel, GLOBAL_TEXT_REQUIRED_LEVEL);
    AddRequirementLine(m, SkillAttribute[skillType].Strength, curStr,
                       GLOBAL_TEXT_REQUIRED_STRENGTH);
    AddRequirementLine(m, SkillAttribute[skillType].Dexterity, curDex,
                       GLOBAL_TEXT_REQUIRED_DEXTERITY);
    AddRequirementLine(m, reqEnergy, curEnergy, GLOBAL_TEXT_REQUIRED_ENERGY);
    AddRequirementLine(m, SkillAttribute[skillType].Charisma, curCha,
                       GLOBAL_TEXT_REQUIRED_CHARISMA);
    EndSection(m, before);
}

void Builder::EmitBlueTags(Model &m, int skillType)
{
    const int before = m.count;

    const BYTE MasteryType =
        gSkillManager.GetSkillMasteryType(static_cast<ActionSkillType>(skillType));
    if (MasteryType != 255)
    {
        AddRaw(m, I18N::Game::Lookup(GLOBAL_TEXT_MASTERY_TYPE_BASE + MasteryType), LineColor::Blue);
    }

    if (skillType == AT_SKILL_EXPANSION_OF_WIZARDRY ||
        skillType == AT_SKILL_EXPANSION_OF_WIZARDRY_STR ||
        skillType == AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY)
    {
        AddRaw(m, I18N::Game::MinimumWizardryIncrement20, LineColor::Blue);
    }
    EndSection(m, before);
}

BuilderLegacyCalls::BuilderLegacyCalls(SessionKeeper &keeper, Builder &owner) noexcept
    : SessionLegacyCalls(keeper), owner_(owner)
{
}

DamageContext BuilderLegacyCalls::BuildDamageContext(int skillType)
{
    return owner_.BuildDamageContext(skillType);
}

void BuilderLegacyCalls::EmitBottomBanners(Model &model, const BuildOptions &options, int skillType)
{
    owner_.EmitBottomBanners(model, options, skillType);
}

Builder::Builder(SessionKeeper &keeper) noexcept
    : BuilderLegacyCalls(keeper, *this), gSkillManager(keeper.SkillManagerObject()),
      gameplay_(keeper.GameplayForConstruction()),
      characterMachine_(keeper.CharacterMachineObject())
{
}

void Builder::Build(const BuildOptions &options, Model &outModel)
{
    outModel.Reset();

    if (options.includeCharacterSpecific && (!Hero || !CharacterAttribute || !CharacterMachine))
        return;

    const int SkillType = options.skillType;
    if (SkillType < 0 || SkillType >= MAX_SKILLS)
        return;
    if (SkillAttribute[SkillType].Name[0] == L'\0')
        return;

    // Resolve display strings + raw fields.
    wchar_t lpszName[256];
    int iMana = 0, iDistance = 0, iSkillMana = 0;
    gSkillManager.GetSkillInformation(SkillType, 1, lpszName, &iMana, &iDistance, &iSkillMana);

    // Force / Force Wave name override (character-specific: depends on the
    // weapon's special options).
    if (options.includeCharacterSpecific && SkillType == AT_SKILL_FORCE &&
        Hero->Weapon[0].Type != -1)
    {
        for (int i = 0; i < CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].SpecialNum; i++)
        {
            if (CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Special[i] ==
                AT_SKILL_FORCE_WAVE)
            {
                mu_swprintf(lpszName, L"%ls", I18N::Game::ForceWave);
                break;
            }
        }
    }

    EmitHeader(outModel, lpszName);
    EmitTopBanners(outModel, SkillType);
    if (options.includeCharacterSpecific)
        EmitBodyDamage(outModel, SkillType);
    EmitBodyStats(outModel, SkillType, iDistance, iMana, iSkillMana,
                  SkillAttribute[SkillType].Delay);
    EmitRequirements(outModel, options, SkillType);
    EmitBottomBanners(outModel, options, SkillType);
    EmitBlueTags(outModel, SkillType);
}

} // namespace UI::Skills::Tooltip

void UI::Skills::Tooltip::BuilderLegacyCalls::EmitBodyDamage(Model &m, int skillType)
{
    return owner_.EmitBodyDamage(m, skillType);
} // OMF-01959
void UI::Skills::Tooltip::BuilderLegacyCalls::EmitRequirements(Model &m,
                                                               const BuildOptions &options,
                                                               int skillType)
{
    return owner_.EmitRequirements(m, options, skillType);
} // OMF-01961
void UI::Skills::Tooltip::BuilderLegacyCalls::EmitBlueTags(Model &m, int skillType)
{
    return owner_.EmitBlueTags(m, skillType);
} // OMF-01963

using namespace SEASON3B;

namespace
{
float ClampDefenseSuccessRateMultiplier(float multiplier)
{
    if (multiplier < 1.0f)
    {
        return 1.0f;
    }

    return multiplier;
}

void AppendDetailLine(std::wstring &text, const wchar_t *line)
{
    if (!text.empty())
        text += L'\n';
    text += line;
}
} // namespace

float SEASON3B::CNewUICharacterInfoWindow::GetMasterSkillValue(ActionSkillType skill) const
{
    return CharacterAttribute->MasterSkillInfo[skill].GetSkillValue();
}

float SEASON3B::CNewUICharacterInfoWindow::GetMasterSkillValue(ActionSkillType firstSkill,
                                                               ActionSkillType secondSkill) const
{
    const auto firstSkillInfo = CharacterAttribute->MasterSkillInfo[firstSkill];
    const auto secondSkillInfo = CharacterAttribute->MasterSkillInfo[secondSkill];

    if (secondSkillInfo.GetSkillLevel() > firstSkillInfo.GetSkillLevel())
    {
        return secondSkillInfo.GetSkillValue();
    }

    return firstSkillInfo.GetSkillValue();
}

int SEASON3B::CNewUICharacterInfoWindow::GetMasterSkillValueAsInt(ActionSkillType skill) const
{
    return static_cast<int>(GetMasterSkillValue(skill));
}

int SEASON3B::CNewUICharacterInfoWindow::GetMasterSkillValueAsInt(ActionSkillType firstSkill,
                                                                  ActionSkillType secondSkill) const
{
    return static_cast<int>(GetMasterSkillValue(firstSkill, secondSkill));
}

SEASON3B::CNewUICharacterInfoWindow::CNewUICharacterInfoWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay_(GameplayForConstruction()),
      renderer_(RendererForConstruction()), m_modernPanel(keeper)
{
}

SEASON3B::CNewUICharacterInfoWindow::~CNewUICharacterInfoWindow()
{
    Release();
}

bool SEASON3B::CNewUICharacterInfoWindow::Create(CNewUIManager *manager, int x, int y)
{
    if (manager == nullptr)
        return false;
    m_pNewUIMng = manager;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_CHARACTER, this);
    SetPos(x, y);
    m_modernPanel.Create();
    Show(false);
    return true;
}

void SEASON3B::CNewUICharacterInfoWindow::Release()
{
    m_modernPanel.Release();
    if (m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool SEASON3B::CNewUICharacterInfoWindow::UpdateMouseEvent()
{
    if (BtnProcess())
        return false;
    const auto rect = m_modernPanel.ReferenceRect(static_cast<int>(ModernUiViewportWidth()),
                                                  static_cast<int>(ModernUiViewportHeight()));
    return MouseX < rect.x || MouseX >= rect.x + rect.width || MouseY < rect.y ||
           MouseY >= rect.y + rect.height;
}

bool SEASON3B::CNewUICharacterInfoWindow::BtnProcess()
{
    if (m_modernPanel.CloseButton().IsClick())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHARACTER);
        PlayBuffer(SOUND_CLICK01);
        return true;
    }
    if (CharacterAttribute->LevelUpPoint > 0)
    {
        const int iCount = gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD ? 5 : 4;
        for (int i = 0; i < iCount; ++i)
        {
            if (m_modernPanel.StatButton(i).IsClick())
            {
                SocketClient->ToGameServer()->SendIncreaseCharacterStatPoint(
                    static_cast<CharacterStatAttribute>(i));
                PlayBuffer(SOUND_CLICK01);
                return true;
            }
        }
    }
    if (m_modernPanel.PetButton().IsClick())
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_PET);
        return true;
    }
    if (m_modernPanel.MasterLevelButton().IsClick())
    {
        if (gCharacterManager.IsMasterLevel(Hero->Class) && Hero->Class != CLASS_TEMPLENIGHT)
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MASTER_LEVEL);
        return true;
    }
    return false;
}

bool SEASON3B::CNewUICharacterInfoWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHARACTER) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_CHARACTER);
            PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUICharacterInfoWindow::Update()
{
    (void)BtnProcess();
    modernVisible_ = IsVisible();
    if (modernVisible_)
        modernContent_ = BuildModernContent();
    return true;
}

UI::Modern::PC::Character::RmlCharacterFrameContent SEASON3B::CNewUICharacterInfoWindow::
    BuildModernContent()
{
    using UI::Modern::PC::Character::RmlCharacterFrameContent;
    RmlCharacterFrameContent content;
    content.title = CharacterAttribute->Name;
    content.levelLabel = I18N::Game::Level;
    content.classLabel = I18N::Game::Class;
    content.serverLabel = I18N::Game::Server;
    content.pointLabel = I18N::Game::Point;
    content.statLabels = {I18N::Game::STR, I18N::Game::AGI, I18N::Game::STA, I18N::Game::ENG,
                          I18N::Game::Command};
    content.pet = I18N::Game::Pet;
    content.masterLevel = I18N::Game::MasterSkillTreeA;

    BuildModernProgress(content);

    const std::array<int, BTN_STAT_COUNT> values{
        CharacterAttribute->Strength + CharacterAttribute->AddStrength,
        CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity,
        CharacterAttribute->Vitality + CharacterAttribute->AddVitality,
        CharacterAttribute->Energy + CharacterAttribute->AddEnergy,
        CharacterAttribute->Charisma + CharacterAttribute->AddCharisma};
    content.darkLord = gCharacterManager.GetBaseClass(Hero->Class) == CLASS_DARK_LORD;
    m_modernPanel.MasterLevelButton().SetEnable(gCharacterManager.IsMasterLevel(Hero->Class) &&
                                                Hero->Class != CLASS_TEMPLENIGHT);
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        content.statValues[index] = std::to_wstring(values[index]);
        content.canIncrease[index] = CharacterAttribute->LevelUpPoint > 0 &&
                                     values[index] < 30000 &&
                                     (index != STAT_CHARISMA || content.darkLord);
    }
    BuildModernAttributes(content);
    return content;
}

void SEASON3B::CNewUICharacterInfoWindow::BuildModernProgress(
    UI::Modern::PC::Character::RmlCharacterFrameContent &content)
{
    wchar_t text[256]{};
    content.level = std::to_wstring(CharacterAttribute->Level);
    content.characterClass = gCharacterManager.GetCharacterClassText(CharacterAttribute->Class);
    const std::array<const wchar_t *, 4> serverFormats{
        I18N::Game::SDServer, I18N::Game::SDNonPvPServer, I18N::Game::SDGoldPvPServer,
        I18N::Game::SDGoldServer};
    mu_swprintf(text, serverFormats[g_ServerListManager.GetNonPVPInfo()],
                g_ServerListManager.GetSelectServerName(),
                g_ServerListManager.GetSelectServerIndex());
    content.server = text;
    mu_swprintf(text, I18N::Game::EXPI64dI64d, CharacterAttribute->Experience,
                CharacterAttribute->NextExperience);
    content.experience = text;
    content.experience += L'\n';
    content.experience += BuildPointAdjustmentText();
    content.points = std::to_wstring(CharacterAttribute->LevelUpPoint);
}

std::wstring SEASON3B::CNewUICharacterInfoWindow::BuildPointAdjustmentText() const
{
    wchar_t text[256]{};
    if (CharacterAttribute->Level <= 9)
    {
        mu_swprintf(text, L"%ls %d/%d | %ls %d/%d", I18N::Game::Create, 0, 0, I18N::Game::Decrease,
                    0, 0);
        return text;
    }

    const int minus = CharacterAttribute->wMinusPoint == 0 ? 0 : -CharacterAttribute->wMinusPoint;
    mu_swprintf(text, L"%ls %d/%d | %ls %d/%d", I18N::Game::Create, CharacterAttribute->AddPoint,
                CharacterAttribute->MaxAddPoint, I18N::Game::Decrease, minus,
                -CharacterAttribute->wMaxMinusPoint);
    return text;
}

void SEASON3B::CNewUICharacterInfoWindow::BuildModernAttributes(
    UI::Modern::PC::Character::RmlCharacterFrameContent &content)
{
    wchar_t strAttakMamage[256];
    int iAttackDamageMin = 0;
    int iAttackDamageMax = 0;

    (void)GetAttackDamage(&iAttackDamageMin, &iAttackDamageMax);

    int add_attack_success_rate_pvm = 0;
    int add_attack_success_rate_pvp = 0;
    float add_defense_success_rate_pvm = 0.0f;
    int add_defense_success_rate_pvp = 0;
    int add_attack_dmg_max = 0;
    int add_attack_dmg_min = 0;
    int add_defense = 0;
    int add_magic_damage_min = 0;
    int add_magic_damage_max = 0;

    add_attack_success_rate_pvm =
        GetMasterSkillValueAsInt(AT_SKILL_AttackSuccRateInc, AT_SKILL_IncreaseAttackSuccessRate);
    add_attack_success_rate_pvp =
        GetMasterSkillValueAsInt(AT_SKILL_PvPAttackRate, AT_SKILL_IncreasePvPAttackRate);
    add_defense_success_rate_pvm =
        GetMasterSkillValue(AT_SKILL_DefenseSuccessRateInc, AT_SKILL_IncreaseDefenseSuccessRate);
    add_defense_success_rate_pvp =
        GetMasterSkillValueAsInt(AT_SKILL_PvPDefenceRateInc, AT_SKILL_IncreasePvPDefenseRate);
    add_defense = GetMasterSkillValueAsInt(AT_SKILL_DefenseIncrease, AT_SKILL_IncreasesDefense);
    add_magic_damage_min = GetMasterSkillValueAsInt(AT_SKILL_MinimumWizardryInc);

    ITEM *pWeaponRight = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT];
    ITEM *pWeaponLeft = &CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT];

    int iAttackRating = CharacterAttribute->AttackRating + add_attack_success_rate_pvm;
    int iAttackRatingPK = CharacterAttribute->AttackRatingPK + add_attack_success_rate_pvp;
    iAttackDamageMax += add_attack_dmg_max;
    iAttackDamageMin += add_attack_dmg_min;

    if (g_isCharacterBuff((&Hero->Object), eBuff_AddAG))
    {
        WORD wDexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        iAttackRating += wDexterity;
        iAttackRatingPK += wDexterity;
        if (PartyNumber >= 3)
        {
            int iPlusRating = (wDexterity * ((PartyNumber - 2) * 0.01f));
            iAttackRating += iPlusRating;
            iAttackRatingPK = iPlusRating;
        }
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_HelpNpc))
    {
        int iTemp = 0;
        if (CharacterAttribute->Level > 180)
        {
            iTemp = (180 / 3) + 45;
        }
        else
        {
            iTemp = (CharacterAttribute->Level / 3) + 45;
        }

        iAttackDamageMin += iTemp;
        iAttackDamageMax += iTemp;
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_Berserker))
    {
        int nTemp = CharacterAttribute->Strength + CharacterAttribute->AddStrength +
                    CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        float fTemp = int(CharacterAttribute->Energy / 30) / 100.f;
        iAttackDamageMin += nTemp / 7 * fTemp;
        iAttackDamageMax += nTemp / 4 * fTemp;
    }

    int iMinIndex = 0, iMaxIndex = 0, iMagicalIndex = 0;

    StrengthenCapability SC_r, SC_l;

    int rlevel = pWeaponRight->Level;

    if (rlevel >= pWeaponRight->Jewel_Of_Harmony_OptionLevel)
    {
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC_r, pWeaponRight, 1);

        if (SC_r.SI_isSP)
        {
            iMinIndex = SC_r.SI_SP.SI_minattackpower;
            iMaxIndex = SC_r.SI_SP.SI_maxattackpower;
            iMagicalIndex = SC_r.SI_SP.SI_magicalpower;
        }
    }

    int llevel = pWeaponLeft->Level;

    if (llevel >= pWeaponLeft->Jewel_Of_Harmony_OptionLevel)
    {
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC_l, pWeaponLeft, 1);

        if (SC_l.SI_isSP)
        {
            iMinIndex += SC_l.SI_SP.SI_minattackpower;
            iMaxIndex += SC_l.SI_SP.SI_maxattackpower;
            iMagicalIndex += SC_l.SI_SP.SI_magicalpower;
        }
    }

    int iDefenseRate = 0, iAttackPowerRate = 0;

    StrengthenCapability rightinfo, leftinfo;

    int iRightLevel = pWeaponRight->Level;

    if (iRightLevel >= pWeaponRight->Jewel_Of_Harmony_OptionLevel)
    {
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&rightinfo, pWeaponRight, 1);
    }

    int iLeftLevel = pWeaponLeft->Level;

    if (iLeftLevel >= pWeaponLeft->Jewel_Of_Harmony_OptionLevel)
    {
        g_pUIJewelHarmonyinfo->GetStrengthenCapability(&leftinfo, pWeaponLeft, 1);
    }

    if (rightinfo.SI_isSP)
    {
        iAttackPowerRate += rightinfo.SI_SP.SI_attackpowerRate;
    }
    if (leftinfo.SI_isSP)
    {
        iAttackPowerRate += leftinfo.SI_SP.SI_attackpowerRate;
    }

    for (int k = EQUIPMENT_WEAPON_LEFT; k < MAX_EQUIPMENT; ++k)
    {
        StrengthenCapability defenseinfo;

        ITEM *pItem = &CharacterMachine->Equipment[k];

        int eqlevel = pItem->Level;

        if (eqlevel >= pItem->Jewel_Of_Harmony_OptionLevel)
        {
            g_pUIJewelHarmonyinfo->GetStrengthenCapability(&defenseinfo, pItem, 2);
        }

        if (defenseinfo.SI_isSD)
        {
            iDefenseRate += defenseinfo.SI_SD.SI_defenseRate;
        }
    }

    int itemoption380Attack = 0;
    int itemoption380Defense = 0;

    for (int j = 0; j < MAX_EQUIPMENT; ++j)
    {
        bool is380item = CharacterMachine->Equipment[j].option_380;
        int i380type = CharacterMachine->Equipment[j].Type;

        if (is380item && i380type > -1 && i380type < MAX_ITEM)
        {
            ITEM_ADD_OPTION item380option;

            item380option =
                g_pItemAddOptioninfo->GetItemAddOtioninfo(CharacterMachine->Equipment[j].Type);

            if (item380option.m_byOption1 == 1)
            {
                itemoption380Attack += item380option.m_byValue1;
            }

            if (item380option.m_byOption2 == 1)
            {
                itemoption380Attack += item380option.m_byValue2;
            }

            if (item380option.m_byOption1 == 3)
            {
                itemoption380Defense += item380option.m_byValue1;
            }

            if (item380option.m_byOption2 == 3)
            {
                itemoption380Defense += item380option.m_byValue2;
            }
        }
    }

    ITEM *pItemRingLeft, *pItemRingRight;

    pItemRingLeft = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
    pItemRingRight = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
    if (pItemRingLeft && pItemRingRight)
    {
        int iNonExpiredLRingType = -1;
        int iNonExpiredRRingType = -1;
        if (!pItemRingLeft->bPeriodItem || !pItemRingLeft->bExpiredPeriod)
        {
            iNonExpiredLRingType = pItemRingLeft->Type;
        }
        if (!pItemRingRight->bPeriodItem || !pItemRingRight->bExpiredPeriod)
        {
            iNonExpiredRRingType = pItemRingRight->Type;
        }

        int maxIAttackDamageMin = 0;
        int maxIAttackDamageMax = 0;
        if (iNonExpiredLRingType == ITEM_CHRISTMAS_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_CHRISTMAS_TRANSFORMATION_RING)
        {
            maxIAttackDamageMin = std::max<int>(maxIAttackDamageMin, 20);
            maxIAttackDamageMax = std::max<int>(maxIAttackDamageMax, 20);
        }
        if (iNonExpiredLRingType == ITEM_PANDA_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_PANDA_TRANSFORMATION_RING)
        {
            maxIAttackDamageMin = std::max<int>(maxIAttackDamageMin, 30);
            maxIAttackDamageMax = std::max<int>(maxIAttackDamageMax, 30);
        }
        if (iNonExpiredLRingType == ITEM_SKELETON_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_SKELETON_TRANSFORMATION_RING)
        {
            maxIAttackDamageMin = std::max<int>(maxIAttackDamageMin, 40);
            maxIAttackDamageMax = std::max<int>(maxIAttackDamageMax, 40);
        }

        iAttackDamageMin += maxIAttackDamageMin;
        iAttackDamageMax += maxIAttackDamageMax;
    }

    ITEM *pItemHelper = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
    if (pItemHelper)
    {
        if (pItemHelper->Type == ITEM_HORN_OF_FENRIR && pItemHelper->ExcellentFlags == 0x04)
        {
            WORD wLevel = CharacterAttribute->Level;
            iAttackDamageMin += (wLevel / 12);
            iAttackDamageMax += (wLevel / 12);
        }
        if (pItemHelper->Type == ITEM_DEMON)
        {
            if (false == pItemHelper->bExpiredPeriod)
            {
                iAttackDamageMin += int(float(iAttackDamageMin) * 0.4f);
                iAttackDamageMax += int(float(iAttackDamageMax) * 0.4f);
            }
        }
        if (pItemHelper->Type == ITEM_PET_SKELETON)
        {
            if (false == pItemHelper->bExpiredPeriod)
            {
                iAttackDamageMin += int(float(iAttackDamageMin) * 0.2f);
                iAttackDamageMax += int(float(iAttackDamageMax) * 0.2f);
            }
        }
        if (pItemHelper->Type == ITEM_IMP)
        {
            iAttackDamageMin += int(float(iAttackDamageMin) * 0.3f);
            iAttackDamageMax += int(float(iAttackDamageMax) * 0.3f);
        }
    }

    if (iAttackRating > 0)
    {
        if (iAttackDamageMin + iMinIndex >= iAttackDamageMax + iMaxIndex)
        {
            mu_swprintf(strAttakMamage, I18N::Game::DmgRateDDD, iAttackDamageMax + iMaxIndex,
                        iAttackDamageMax + iMaxIndex, iAttackRating);
        }
        else
        {
            mu_swprintf(strAttakMamage, I18N::Game::DmgRateDDD, iAttackDamageMin + iMinIndex,
                        iAttackDamageMax + iMaxIndex, iAttackRating);
        }
    }
    else
    {
        if (iAttackDamageMin + iMinIndex >= iAttackDamageMax + iMaxIndex)
        {
            mu_swprintf(strAttakMamage, I18N::Game::DmgDD, iAttackDamageMax + iMaxIndex,
                        iAttackDamageMax + iMaxIndex);
        }
        else
        {
            mu_swprintf(strAttakMamage, I18N::Game::DmgDD, iAttackDamageMin + iMinIndex,
                        iAttackDamageMax + iMaxIndex);
        }
    }

    content.details[STAT_STRENGTH] = strAttakMamage;

    if (iAttackRatingPK > 0)
    {
        if (itemoption380Attack != 0 || iAttackPowerRate != 0)
        {
            mu_swprintf(strAttakMamage, I18N::Game::AttackRateDD, iAttackRatingPK,
                        itemoption380Attack + iAttackPowerRate);
        }
        else
        {
            mu_swprintf(strAttakMamage, I18N::Game::AttackRateD, iAttackRatingPK);
        }

        AppendDetailLine(content.details[STAT_STRENGTH], strAttakMamage);
    }

    WORD wDexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;

    bool bDexSuccess = true;
    int iBaseClass = gCharacterManager.GetBaseClass(Hero->Class);

    for (int i = EQUIPMENT_HELM; i <= EQUIPMENT_BOOTS; ++i)
    {
        if (iBaseClass == CLASS_DARK)
        {
            if ((CharacterMachine->Equipment[i].Type == -1 &&
                 (i != EQUIPMENT_HELM && iBaseClass == CLASS_DARK)) ||
                (CharacterMachine->Equipment[i].Type != -1 &&
                 CharacterMachine->Equipment[i].Durability <= 0))
            {
                bDexSuccess = false;
                break;
            }
        }
        else if (iBaseClass == CLASS_RAGEFIGHTER)
        {
            if ((CharacterMachine->Equipment[i].Type == -1 &&
                 (i != EQUIPMENT_GLOVES && iBaseClass == CLASS_RAGEFIGHTER)) ||
                (CharacterMachine->Equipment[i].Type != -1 &&
                 CharacterMachine->Equipment[i].Durability <= 0))
            {
                bDexSuccess = false;
                break;
            }
        }
        else
        {
            if ((CharacterMachine->Equipment[i].Type == -1) ||
                (CharacterMachine->Equipment[i].Type != -1 &&
                 CharacterMachine->Equipment[i].Durability <= 0))
            {
                bDexSuccess = false;
                break;
            }
        }
    }

    if (bDexSuccess)
    {
        int iType;
        if (iBaseClass == CLASS_DARK)
        {
            iType = CharacterMachine->Equipment[EQUIPMENT_ARMOR].Type;

            if ((iType != ITEM_STORM_CROW_ARMOR) && (iType != ITEM_THUNDER_HAWK_ARMOR) &&
                (iType != ITEM_HURRICANE_ARMOR) && (iType != ITEM_VOLCANO_ARMOR) &&
                (iType != ITEM_VALIANT_ARMOR) && (iType != ITEM_DESTORY_ARMOR) &&
                (iType != ITEM_PHANTOM_ARMOR))
            {
                bDexSuccess = false;
            }

            iType = iType % MAX_ITEM_INDEX;
        }
        else
        {
            iType = CharacterMachine->Equipment[EQUIPMENT_HELM].Type % MAX_ITEM_INDEX;
        }

        if (bDexSuccess)
        {
            for (int i = EQUIPMENT_ARMOR; i <= EQUIPMENT_BOOTS; ++i)
            {
                if (iBaseClass == CLASS_RAGEFIGHTER && i == EQUIPMENT_GLOVES)
                    continue;
                if (iType != CharacterMachine->Equipment[i].Type % MAX_ITEM_INDEX)
                {
                    bDexSuccess = false;
                    break;
                }
            }
        }
    }

    int t_adjdef = CharacterAttribute->Defense + add_defense;

    if (g_isCharacterBuff((&Hero->Object), eBuff_HelpNpc))
    {
        if (CharacterAttribute->Level > 180)
        {
            t_adjdef += 180 / 5 + 50;
        }
        else
        {
            t_adjdef += (CharacterAttribute->Level / 5 + 50);
        }
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_Berserker))
    {
        int nTemp = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        float fTemp = (40 - int(CharacterAttribute->Energy / 60)) / 100.f;
        fTemp = MAX(fTemp, 0.1f);
        t_adjdef -= nTemp / 3 * fTemp;
    }

    int maxdefense = 0;

    for (int j = 0; j < MAX_EQUIPMENT; ++j)
    {
        int TempLevel = CharacterMachine->Equipment[j].Level;
        if (TempLevel >= CharacterMachine->Equipment[j].Jewel_Of_Harmony_OptionLevel)
        {
            StrengthenCapability SC;

            g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, &CharacterMachine->Equipment[j], 2);

            if (SC.SI_isSD)
            {
                maxdefense += SC.SI_SD.SI_defense;
            }
        }
    }

    int iChangeRingAddDefense = 0;

    pItemRingLeft = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
    pItemRingRight = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
    if (pItemRingLeft->Type == ITEM_ELITE_TRANSFER_SKELETON_RING ||
        pItemRingRight->Type == ITEM_ELITE_TRANSFER_SKELETON_RING)
    {
        iChangeRingAddDefense = (t_adjdef + maxdefense) / 10;
    }

    if (Hero->Helper.Type == MODEL_PET_PANDA)
        iChangeRingAddDefense += 50;

    if (Hero->Helper.Type == MODEL_PET_UNICORN)
        iChangeRingAddDefense += 50;

    wchar_t strBlocking[256];

    const float defenseSuccessRateMultiplier =
        ClampDefenseSuccessRateMultiplier(add_defense_success_rate_pvm);
    const int defenseSuccessRate =
        static_cast<int>(CharacterAttribute->SuccessfulBlocking * defenseSuccessRateMultiplier);

    int nAdd_FulBlocking = 0;
    if (g_isCharacterBuff((&Hero->Object), eBuff_Def_up_Ourforces))
    {
        int _AddStat = (10 + (float)(CharacterAttribute->Energy - 80) / 10);
        if (_AddStat > 100)
            _AddStat = 100;

        _AddStat = defenseSuccessRate * _AddStat / 100;
        nAdd_FulBlocking += _AddStat;
    }

    if (bDexSuccess)
    {
        // memorylock
        if (defenseSuccessRate > 0)
        {
            if (nAdd_FulBlocking)
            {
                mu_swprintf(strBlocking, I18N::Game::DefenseRateDDD,
                            t_adjdef + maxdefense + iChangeRingAddDefense, defenseSuccessRate,
                            (defenseSuccessRate) / 10 + nAdd_FulBlocking);
            }
            else
            {
                mu_swprintf(strBlocking, I18N::Game::DefenseRateDDD,
                            t_adjdef + maxdefense + iChangeRingAddDefense, defenseSuccessRate,
                            (defenseSuccessRate) / 10);
            }
        }
        else
        {
            mu_swprintf(strBlocking, I18N::Game::DefenseDD,
                        t_adjdef + maxdefense + iChangeRingAddDefense,
                        (t_adjdef + iChangeRingAddDefense) / 10);
        }
    }
    else
    {
        if (defenseSuccessRate > 0)
        {
            if (nAdd_FulBlocking)
            {
                mu_swprintf(strBlocking, I18N::Game::DefenseRateDDD,
                            t_adjdef + maxdefense + iChangeRingAddDefense, defenseSuccessRate,
                            nAdd_FulBlocking);
            }
            else
            {
                mu_swprintf(strBlocking, I18N::Game::DefenseRateDD,
                            t_adjdef + maxdefense + iChangeRingAddDefense, defenseSuccessRate);
            }
        }
        else
        {
            // 209
            mu_swprintf(strBlocking, I18N::Game::DefenseD,
                        t_adjdef + maxdefense + iChangeRingAddDefense);
        }
    }

    content.details[STAT_DEXTERITY] = strBlocking;

    WORD wAttackSpeed = CLASS_WIZARD == iBaseClass || CLASS_SUMMONER == iBaseClass
                            ? CharacterAttribute->MagicSpeed
                            : CharacterAttribute->AttackSpeed;

    mu_swprintf(strBlocking, I18N::Game::AttackSpeedD, wAttackSpeed);
    AppendDetailLine(content.details[STAT_DEXTERITY], strBlocking);

    if (itemoption380Defense != 0 || iDefenseRate != 0)
    {
        mu_swprintf(strBlocking, I18N::Game::DefenseRateDD2110,
                    CharacterAttribute->SuccessfulBlockingPK + add_defense_success_rate_pvp,
                    itemoption380Defense + iDefenseRate);
    }
    else
    {
        mu_swprintf(strBlocking, I18N::Game::DefenseRateD,
                    CharacterAttribute->SuccessfulBlockingPK + add_defense_success_rate_pvp);
    }

    AppendDetailLine(content.details[STAT_DEXTERITY], strBlocking);

    WORD wVitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;

    if (g_isCharacterBuff((&Hero->Object), eBuff_Hp_up_Ourforces))
    {
        CharacterMachine->CalculateAll();
        wVitality = CharacterAttribute->Vitality + CharacterAttribute->AddVitality;
    }

    wchar_t strVitality[256];
    if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
    {
        mu_swprintf(strVitality, I18N::Game::HPDD, CharacterAttribute->Life,
                    Master_Level_Data.wMaxLife);
    }
    else
    {
        mu_swprintf(strVitality, I18N::Game::HPDD, CharacterAttribute->Life,
                    CharacterAttribute->LifeMax);
    }
    content.details[STAT_VITALITY] = strVitality;

    if (iBaseClass == CLASS_RAGEFIGHTER)
    {
        // 물리공격력
        mu_swprintf(strVitality, I18N::Game::MeleeDamageD, 50 + (wVitality / 10));
        AppendDetailLine(content.details[STAT_VITALITY], strVitality);
    }

    WORD wEnergy = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;

    wchar_t strEnergy[256];
    if (gCharacterManager.IsMasterLevel(Hero->Class) == true)
    {
        mu_swprintf(strEnergy, I18N::Game::ManaDD, CharacterAttribute->Mana,
                    Master_Level_Data.wMaxMana);
    }
    else
        mu_swprintf(strEnergy, I18N::Game::ManaDD, CharacterAttribute->Mana,
                    CharacterAttribute->ManaMax);

    content.details[STAT_ENERGY] = strEnergy;

    if (iBaseClass == CLASS_WIZARD || iBaseClass == CLASS_DARK || iBaseClass == CLASS_SUMMONER)
    {
        int iMagicDamageMin;
        int iMagicDamageMax;

        gameplay_.GetMagicSkillDamage(CharacterAttribute->Skill[Hero->CurrentSkill],
                                      &iMagicDamageMin, &iMagicDamageMax);

        int iMagicDamageMinInitial = iMagicDamageMin;
        int iMagicDamageMaxInitial = iMagicDamageMax;

        iMagicDamageMin += add_magic_damage_min;
        iMagicDamageMax += add_magic_damage_max;

        if (CharacterAttribute->Ability & ABILITY_PLUS_DAMAGE)
        {
            iMagicDamageMin += 10;
            iMagicDamageMax += 10;
        }

        int maxMg = 0;

        for (int j = 0; j < MAX_EQUIPMENT; ++j)
        {
            int TempLevel = CharacterMachine->Equipment[j].Level;

            if (TempLevel >= CharacterMachine->Equipment[j].Jewel_Of_Harmony_OptionLevel)
            {
                StrengthenCapability SC;
                g_pUIJewelHarmonyinfo->GetStrengthenCapability(&SC, &CharacterMachine->Equipment[j],
                                                               1);

                if (SC.SI_isSP)
                {
                    maxMg += SC.SI_SP.SI_magicalpower;
                }
            }
        }

        pItemRingLeft = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
        pItemRingRight = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];

        int iNonExpiredLRingType = -1;
        int iNonExpiredRRingType = -1;

        if (!pItemRingLeft->bPeriodItem || !pItemRingLeft->bExpiredPeriod)
        {
            iNonExpiredLRingType = pItemRingLeft->Type;
        }
        if (!pItemRingRight->bPeriodItem || !pItemRingRight->bExpiredPeriod)
        {
            iNonExpiredRRingType = pItemRingRight->Type;
        }

        int maxIMagicDamageMin = 0;
        int maxIMagicDamageMax = 0;

        if (iNonExpiredLRingType == ITEM_CHRISTMAS_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_CHRISTMAS_TRANSFORMATION_RING)
        {
            maxIMagicDamageMin = std::max<int>(maxIMagicDamageMin, 20);
            maxIMagicDamageMax = std::max<int>(maxIMagicDamageMax, 20);
        }
        if (iNonExpiredLRingType == ITEM_PANDA_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_PANDA_TRANSFORMATION_RING)
        {
            maxIMagicDamageMin = std::max<int>(maxIMagicDamageMin, 30);
            maxIMagicDamageMax = std::max<int>(maxIMagicDamageMax, 30);
        }
        if (iNonExpiredLRingType == ITEM_SKELETON_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_SKELETON_TRANSFORMATION_RING)
        {
            maxIMagicDamageMin = std::max<int>(maxIMagicDamageMin, 40);
            maxIMagicDamageMax = std::max<int>(maxIMagicDamageMax, 40);
        }

        iMagicDamageMin += maxIMagicDamageMin;
        iMagicDamageMax += maxIMagicDamageMax;

        pItemHelper = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
        if (pItemHelper)
        {
            if (pItemHelper->Type == ITEM_HORN_OF_FENRIR && pItemHelper->ExcellentFlags == 0x04)
            {
                WORD wLevel = CharacterAttribute->Level;
                iMagicDamageMin += (wLevel / 25);
                iMagicDamageMax += (wLevel / 25);
            }

            if (pItemHelper->Type == ITEM_DEMON)
            {
                if (false == pItemHelper->bExpiredPeriod)
                {
                    iMagicDamageMin += int(float(iMagicDamageMin) * 0.4f);
                    iMagicDamageMax += int(float(iMagicDamageMax) * 0.4f);
                }
            }
            if (pItemHelper->Type == ITEM_PET_SKELETON)
            {
                if (false == pItemHelper->bExpiredPeriod)
                {
                    iMagicDamageMin += int(float(iMagicDamageMin) * 0.2f);
                    iMagicDamageMax += int(float(iMagicDamageMax) * 0.2f);
                }
            }
            if (pItemHelper->Type == ITEM_IMP)
            {
                iMagicDamageMin += int(float(iMagicDamageMin) * 0.3f);
                iMagicDamageMax += int(float(iMagicDamageMax) * 0.3f);
            }
        }

        if (g_isCharacterBuff((&Hero->Object), eBuff_Berserker))
        {
            int nTemp = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
            float fTemp = int(CharacterAttribute->Energy / 30) / 100.f;
            iMagicDamageMin += nTemp / 9 * fTemp;
            iMagicDamageMax += nTemp / 4 * fTemp;
        }

        if ((pWeaponRight->Type >= MODEL_STAFF - MODEL_ITEM &&
             pWeaponRight->Type < (MODEL_STAFF + MAX_ITEM_INDEX - MODEL_ITEM)) ||
            pWeaponRight->Type == (static_cast<int>(MODEL_RUNE_BLADE) - MODEL_ITEM) ||
            pWeaponRight->Type == (static_cast<int>(MODEL_EXPLOSION_BLADE) - MODEL_ITEM) ||
            pWeaponRight->Type == (static_cast<int>(MODEL_SWORD_DANCER) - MODEL_ITEM) ||
            pWeaponRight->Type == (static_cast<int>(MODEL_DARK_REIGN_BLADE) - MODEL_ITEM) ||
            pWeaponRight->Type == (static_cast<int>(MODEL_IMPERIAL_SWORD) - MODEL_ITEM))
        {
            float magicPercent = (float)(pWeaponRight->MagicPower) / 100;

            ITEM_ATTRIBUTE *p = &ItemAttribute[pWeaponRight->Type];
            float percent = CalcDurabilityPercent(pWeaponRight->Durability, p->MagicDur,
                                                  pWeaponRight->Level, pWeaponRight->ExcellentFlags,
                                                  pWeaponRight->AncientDiscriminator);

            magicPercent = magicPercent - magicPercent * percent;
            mu_swprintf(strEnergy, I18N::Game::WizardryDmgDDD, iMagicDamageMin + maxMg,
                        iMagicDamageMax + maxMg,
                        (int)((iMagicDamageMaxInitial + maxMg) * magicPercent));
        }
        else
        {
            mu_swprintf(strEnergy, I18N::Game::WizardryDmgDD216, iMagicDamageMin + maxMg,
                        iMagicDamageMax + maxMg);
        }

        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }

    if (iBaseClass == CLASS_SUMMONER)
    {
        int iCurseDamageMin = 0;
        int iCurseDamageMax = 0;

        gameplay_.GetCurseSkillDamage(CharacterAttribute->Skill[Hero->CurrentSkill],
                                      &iCurseDamageMin, &iCurseDamageMax);

        if (g_isCharacterBuff((&Hero->Object), eBuff_Berserker))
        {
            int nTemp = CharacterAttribute->Energy + CharacterAttribute->AddEnergy;
            float fTemp = int(CharacterAttribute->Energy / 30) / 100.f;
            iCurseDamageMin += nTemp / 9 * fTemp;
            iCurseDamageMax += nTemp / 4 * fTemp;
        }

        int iNonExpiredLRingType = -1;
        int iNonExpiredRRingType = -1;

        if (!pItemRingLeft->bPeriodItem || !pItemRingLeft->bExpiredPeriod)
        {
            iNonExpiredLRingType = pItemRingLeft->Type;
        }
        if (!pItemRingRight->bPeriodItem || !pItemRingRight->bExpiredPeriod)
        {
            iNonExpiredRRingType = pItemRingRight->Type;
        }

        int maxICurseDamageMin = 0;
        int maxICurseDamageMax = 0;

        if (iNonExpiredLRingType == ITEM_PANDA_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_PANDA_TRANSFORMATION_RING)
        {
            maxICurseDamageMin = std::max<int>(maxICurseDamageMin, 30);
            maxICurseDamageMax = std::max<int>(maxICurseDamageMax, 30);
        }
        if (iNonExpiredLRingType == ITEM_SKELETON_TRANSFORMATION_RING ||
            iNonExpiredRRingType == ITEM_SKELETON_TRANSFORMATION_RING)
        {
            maxICurseDamageMin = std::max<int>(maxICurseDamageMin, 40);
            maxICurseDamageMax = std::max<int>(maxICurseDamageMax, 40);
        }

        iCurseDamageMin += maxICurseDamageMin;
        iCurseDamageMax += maxICurseDamageMax;

        pItemHelper = &CharacterMachine->Equipment[EQUIPMENT_HELPER];
        if (pItemHelper)
        {
            if (pItemHelper->Type == ITEM_DEMON)
            {
                if (false == pItemHelper->bExpiredPeriod)
                {
                    iCurseDamageMin += int(float(iCurseDamageMin) * 0.4f);
                    iCurseDamageMax += int(float(iCurseDamageMax) * 0.4f);
                }
            }
            if (pItemHelper->Type == ITEM_PET_SKELETON)
            {
                if (false == pItemHelper->bExpiredPeriod)
                {
                    iCurseDamageMin += int(float(iCurseDamageMin) * 0.2f);
                    iCurseDamageMax += int(float(iCurseDamageMax) * 0.2f);
                }
            }
        }

        if (ITEM_BOOK_OF_SAHAMUTT <= pWeaponLeft->Type && pWeaponLeft->Type <= ITEM_STAFF + 29)
        {
            float fCursePercent = (float)(pWeaponLeft->MagicPower) / 100;

            ITEM_ATTRIBUTE *p = &ItemAttribute[pWeaponLeft->Type];
            float fPercent = ::CalcDurabilityPercent(
                pWeaponLeft->Durability, p->MagicDur, pWeaponLeft->Level,
                pWeaponLeft->ExcellentFlags, pWeaponLeft->AncientDiscriminator);

            fCursePercent -= fCursePercent * fPercent;
            mu_swprintf(strEnergy, I18N::Game::CurseSpellDDD, iCurseDamageMin, iCurseDamageMax,
                        (int)((iCurseDamageMax)*fCursePercent));
        }
        else
        {
            mu_swprintf(strEnergy, I18N::Game::CurseSpellDD1694, iCurseDamageMin, iCurseDamageMax);
        }

        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }

    if (iBaseClass == CLASS_KNIGHT)
    {
        mu_swprintf(strEnergy, I18N::Game::SkillDamageD, 200 + (wEnergy / 10));
        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }
    if (iBaseClass == CLASS_DARK)
    {
        mu_swprintf(strEnergy, I18N::Game::SkillDamageD, 200);
        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }
    if (iBaseClass == CLASS_DARK_LORD)
    {
        mu_swprintf(strEnergy, I18N::Game::SkillDamageD, 200 + (wEnergy / 20));
        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }

    if (iBaseClass == CLASS_RAGEFIGHTER)
    {
        // 마법공격력
        mu_swprintf(strEnergy, I18N::Game::DivineDamageRoarSlasherD, 50 + (wEnergy / 10));
        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
        // 범위공격력
        mu_swprintf(strEnergy, I18N::Game::AOEDamageDarkSideD,
                    100 + (wDexterity / 8 + wEnergy / 10));
        AppendDetailLine(content.details[STAT_ENERGY], strEnergy);
    }

    if (iBaseClass == CLASS_DARK_LORD)
    {
        wchar_t strCharisma[256];
        mu_swprintf(strCharisma, L"%d",
                    CharacterAttribute->Charisma + CharacterAttribute->AddCharisma);
        content.statValues[STAT_CHARISMA] = strCharisma;
    }
}

float SEASON3B::CNewUICharacterInfoWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Character;
}

void SEASON3B::CNewUICharacterInfoWindow::OpenningProcess()
{
    ResetEquipmentLevel();
    for (std::size_t index = 0; index < BTN_STAT_COUNT; ++index)
        m_modernPanel.StatButton(index).Reset();
    m_modernPanel.PetButton().Reset();
    m_modernPanel.MasterLevelButton().Reset();
    m_modernPanel.CloseButton().Reset();
    m_modernPanel.MasterLevelButton().SetEnable(gCharacterManager.IsMasterLevel(Hero->Class) &&
                                                Hero->Class != CLASS_TEMPLENIGHT);

    g_csItemOption.init();

    if (CharacterMachine->IsZeroDurability())
    {
        CharacterMachine->CalculateAll();
    }

    if (g_QuestMng.IsIndexInCurQuestIndexList(0x10009))
    {
        if (g_QuestMng.IsEPRequestRewardState(0x10009))
        {
            g_pMyQuestInfoWindow->UnselectQuestList();
            SocketClient->ToGameServer()->SendQuestClientActionRequest(1, 9);
            g_QuestMng.SetEPRequestRewardState(0x10009, false);
        }
    }
}

void SEASON3B::CNewUICharacterInfoWindow::ClosingProcess()
{
}

bool SEASON3B::CNewUICharacterInfoWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return m_modernPanel.ProcessInput(event);
}

void SEASON3B::CNewUICharacterInfoWindow::ResetEquipmentLevel()
{
    ITEM *pItem = CharacterMachine->Equipment;
    Hero->Weapon[0].Level = pItem[EQUIPMENT_WEAPON_RIGHT].Level;
    Hero->Weapon[1].Level = pItem[EQUIPMENT_WEAPON_LEFT].Level;
    Hero->BodyPart[BODYPART_HELM].Level = pItem[EQUIPMENT_HELM].Level;
    Hero->BodyPart[BODYPART_ARMOR].Level = pItem[EQUIPMENT_ARMOR].Level;
    Hero->BodyPart[BODYPART_PANTS].Level = pItem[EQUIPMENT_PANTS].Level;
    Hero->BodyPart[BODYPART_GLOVES].Level = pItem[EQUIPMENT_GLOVES].Level;
    Hero->BodyPart[BODYPART_BOOTS].Level = pItem[EQUIPMENT_BOOTS].Level;
    Hero->MarkAppearanceChanged();

    CheckFullSet(Hero);
}

namespace
{
std::wstring FormatRatio(std::uint64_t value, std::uint64_t maximum)
{
    return std::to_wstring(value) + L" / " + std::to_wstring(maximum);
}
} // namespace

namespace SEASON3B
{
CNewUIPetInfoWindow::CNewUIPetInfoWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay_(GameplayForConstruction()),
      renderer_(RendererForConstruction()), modernPanel_(keeper)
{
    CalcDamage(selectedTab_);
}

CNewUIPetInfoWindow::~CNewUIPetInfoWindow()
{
    Release();
}

bool CNewUIPetInfoWindow::Create(CNewUIManager *manager, int x, int y)
{
    if (manager == nullptr)
        return false;
    manager_ = manager;
    manager_->AddUIObj(INTERFACE_PET, this);
    SetPos(x, y);
    modernPanel_.Create();
    Show(false);
    return true;
}

void CNewUIPetInfoWindow::Release()
{
    modernPanel_.Release();
    if (manager_ != nullptr)
    {
        manager_->RemoveUIObj(this);
        manager_ = nullptr;
    }
}

bool CNewUIPetInfoWindow::UpdateMouseEvent()
{
    if (BtnProcess())
        return false;
    const UI::Modern::PC::Character::RmlPetInfoRect rect = modernPanel_.ReferenceRect(
        static_cast<int>(ModernUiViewportWidth()), static_cast<int>(ModernUiViewportHeight()));
    return MouseX < rect.x || MouseX >= rect.x + rect.width || MouseY < rect.y ||
           MouseY >= rect.y + rect.height;
}

bool CNewUIPetInfoWindow::UpdateKeyEvent()
{
    if (IsVisible() && IsPress(VK_ESCAPE))
    {
        g_pNewUISystem->Hide(INTERFACE_PET);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

bool CNewUIPetInfoWindow::Update()
{
    (void)BtnProcess();
    modernVisible_ = IsVisible();
    if (modernVisible_)
        modernContent_ = BuildModernContent();
    return true;
}

float CNewUIPetInfoWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::PetInfo;
}

bool CNewUIPetInfoWindow::BtnProcess()
{
    if (modernPanel_.CloseButton().IsClick())
    {
        g_pNewUISystem->Hide(INTERFACE_PET);
        PlayBuffer(SOUND_CLICK01);
        return true;
    }
    for (std::size_t index = 0; index < UI::Modern::PC::Character::RmlPetInfoTabCount; ++index)
    {
        if (!modernPanel_.TabButton(index).IsClick())
            continue;
        const int selected = static_cast<int>(index);
        if (selectedTab_ != selected)
        {
            selectedTab_ = selected;
            CalcDamage(selectedTab_);
            PlayBuffer(SOUND_CLICK01);
        }
        return true;
    }
    return false;
}

void CNewUIPetInfoWindow::CalcDamage(int tab)
{
    if (tab == DarkHorseTab)
    {
        int skillDamage[2]{};
        gameplay_.GetSkillDamage(AT_SKILL_EARTHSHAKE, &skillDamage[0], &skillDamage[1]);
        int masterBoost = 0;
        if (g_pNewUISystem->GetUI_NewMasterLevelInterface() != nullptr)
        {
            masterBoost = static_cast<int>(
                CharacterAttribute->MasterSkillInfo[AT_SKILL_EARTHSHAKE_STR].GetSkillValue());
        }
        const PET_INFO *const pet = Hero->GetEquipedPetInfo(PET_TYPE_DARK_HORSE);
        damage_[0] = pet->m_wDamageMin + skillDamage[0] + masterBoost;
        damage_[1] = pet->m_wDamageMax + skillDamage[1] + masterBoost;
        return;
    }

    const PET_INFO *const pet = Hero->GetEquipedPetInfo(PET_TYPE_DARK_SPIRIT);
    const float weaponBonus =
        CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type == -1
            ? 0.0F
            : CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].MagicPower / 100.0F;
    damage_[0] = pet->m_wDamageMin + static_cast<int>(pet->m_wDamageMin * weaponBonus);
    damage_[1] = pet->m_wDamageMax + static_cast<int>(pet->m_wDamageMax * weaponBonus);
}

UI::Modern::PC::Character::RmlPetInfoContent CNewUIPetInfoWindow::BuildModernContent() const
{
    using UI::Modern::PC::Character::RmlPetInfoContent;
    RmlPetInfoContent content;
    content.title = I18N::Game::Pet;
    content.tabLabels = {I18N::Game::DarkHorse, I18N::Game::DarkRaven};
    content.labels = {I18N::Game::Level, I18N::Game::Life, I18N::Game::EXP,
                      I18N::Game::AttackPowerRate, I18N::Game::AttackSpeed};
    content.commandLabel = I18N::Game::Commands;
    content.leadershipLabel = I18N::Game::LeadershipAvailable;
    content.skills = {I18N::Game::BasicAction, I18N::Game::RandomAutomaticAttack,
                      I18N::Game::AttackWithOwner, I18N::Game::AttackTarget};
    content.selectedTab = selectedTab_;

    const int petType = selectedTab_ == DarkHorseTab ? PET_TYPE_DARK_HORSE : PET_TYPE_DARK_SPIRIT;
    const PET_INFO *const pet = Hero->GetEquipedPetInfo(petType);
    if (pet->m_dwPetType == PET_TYPE_NONE)
    {
        if (selectedTab_ == DarkHorseTab)
        {
            wchar_t missing[256]{};
            mu_swprintf(missing, I18N::Game::NoS, I18N::Game::DarkHorse);
            content.missingPet = missing;
        }
        else
        {
            content.missingPet = I18N::Game::NoPet;
        }
        return content;
    }

    content.petPresent = true;
    content.values[0] = std::to_wstring(pet->m_wLevel);
    content.values[1] = FormatRatio(pet->m_wLife, 255);
    content.values[2] = FormatRatio(pet->m_dwExp1, pet->m_dwExp2);
    content.values[3] = std::to_wstring(damage_[0]) + L" ~ " + std::to_wstring(damage_[1]) + L" (" +
                        std::to_wstring(pet->m_wAttackSuccess) + L")";
    content.values[4] = std::to_wstring(pet->m_wAttackSpeed);
    content.experience = pet->m_dwExp1;
    content.nextExperience = pet->m_dwExp2;
    if (selectedTab_ == DarkSpiritTab)
        content.leadership = std::to_wstring(185 + pet->m_wLevel * 15);
    return content;
}

void CNewUIPetInfoWindow::OpenningProcess()
{
    for (std::size_t index = 0; index < UI::Modern::PC::Character::RmlPetInfoTabCount; ++index)
    {
        modernPanel_.TabButton(index).Reset();
    }
    modernPanel_.CloseButton().Reset();
    CalcDamage(selectedTab_);
}

void CNewUIPetInfoWindow::ClosingProcess()
{
}

bool CNewUIPetInfoWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return modernPanel_.ProcessInput(event);
}
} // namespace SEASON3B

SEASON3B::CNewUIBuffWindow::CNewUIBuffWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), buffs_(keeper)
{
    m_pNewUIMng = NULL;
    m_Pos.x = m_Pos.y = 0;
}

SEASON3B::CNewUIBuffWindow::~CNewUIBuffWindow()
{
    Release();
}

bool SEASON3B::CNewUIBuffWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_BUFF_WINDOW, this);

    SetPos(x, y);

    Show(true);

    return true;
}

void SEASON3B::CNewUIBuffWindow::Release()
{
    buffs_.Release();
    entries_.clear();
    visible_ = false;

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

static eBuffState NormalizeBuffState(eBuffState raw)
{
    switch (raw)
    {
    case EFFECT_GREATER_LIFE_ENHANCED:
    case EFFECT_GREATER_LIFE_MASTERED:
        return eBuff_Life;
    case EFFECT_MAGIC_CIRCLE_IMPROVED:
    case EFFECT_MAGIC_CIRCLE_ENHANCED:
        return eBuff_SwellOfMagicPower;
    case EFFECT_GREATER_CRITICAL_DAMAGE_MASTERED:
    case EFFECT_GREATER_CRITICAL_DAMAGE_EXTENDED:
        return eBuff_AddCriticalDamage;
    case EFFECT_INFINITY_ARROW_IMPROVED:
        return eBuff_InfinityArrow;
    case EFFECT_BLIND_IMPROVED:
        return eDeBuff_Blind;
    case EFFECT_POISON_ARROW_IMPROVED:
        return EFFECT_POISON_ARROW;
    case EFFECT_BLESS_IMPROVED:
        return EFFECT_BLESS;
    case EFFECT_IRON_DEFENSE_IMPROVED:
        return EFFECT_IRON_DEFENSE;
    case EFFECT_BLOOD_HOWLING_IMPROVED:
        return EFFECT_BLOOD_HOWLING;
    default:
        return raw;
    }
}

static int BuffTier(eBuffState buf)
{
    switch (buf)
    {
    case EFFECT_GREATER_LIFE_ENHANCED:
    case EFFECT_MAGIC_CIRCLE_IMPROVED:
    case EFFECT_GREATER_CRITICAL_DAMAGE_EXTENDED:
    case EFFECT_INFINITY_ARROW_IMPROVED:
    case EFFECT_BLIND_IMPROVED:
    case EFFECT_POISON_ARROW_IMPROVED:
    case EFFECT_BLESS_IMPROVED:
    case EFFECT_IRON_DEFENSE_IMPROVED:
    case EFFECT_BLOOD_HOWLING_IMPROVED:
        return 1;
    case EFFECT_GREATER_LIFE_MASTERED:
    case EFFECT_MAGIC_CIRCLE_ENHANCED:
    case EFFECT_GREATER_CRITICAL_DAMAGE_MASTERED:
        return 2;
    default:
        return 0;
    }
}

void SEASON3B::CNewUIBuffWindow::BuffSort(std::list<eBuffState> &buffstate)
{
    OBJECT *pHeroObject = &Hero->Object;
    int iBuffSize = g_CharacterBuffSize(pHeroObject);

    eBuffState top[eBuff_Count] = {};

    for (int i = 0; i < iBuffSize; ++i)
    {
        eBuffState buf = g_CharacterBuff(pHeroObject, i);
        if (buf == eBuffNone)
        {
            continue;
        }
        if (SetDisableRenderBuff(buf))
        {
            continue;
        }

        eBuffState base = NormalizeBuffState(buf);
        if (top[base] == eBuffNone || BuffTier(buf) > BuffTier(top[base]))
        {
            top[base] = buf;
        }
    }

    for (int i = 0; i < iBuffSize; ++i)
    {
        eBuffState buf = g_CharacterBuff(pHeroObject, i);
        if (buf == eBuffNone)
        {
            continue;
        }
        if (SetDisableRenderBuff(buf))
        {
            continue;
        }

        eBuffState base = NormalizeBuffState(buf);
        if (buf != top[base])
        {
            continue;
        }

        eBuffClass eBuffClassType = g_IsBuffClass(buf);
        if (eBuffClassType == eBuffClass_Buff)
        {
            buffstate.push_front(buf);
        }
        else if (eBuffClassType == eBuffClass_DeBuff)
        {
            buffstate.push_back(buf);
        }
        else
        {
            continue;
        }
    }
}

bool SEASON3B::CNewUIBuffWindow::SetDisableRenderBuff(const eBuffState &_BuffState)
{
    switch (_BuffState)
    {
#ifdef PBG_ADD_PKSYSTEM_INGAMESHOP
    case eDeBuff_MoveCommandWin:
#endif //PBG_ADD_PKSYSTEM_INGAMESHOP
    case eDeBuff_FlameStrikeDamage:
    case eDeBuff_GiganticStormDamage:
    case eDeBuff_LightningShockDamage:
    case eDeBuff_Discharge_Stamina:
        return true;
    default:
        return false;
    }
    return false;
}

bool SEASON3B::CNewUIBuffWindow::UpdateMouseEvent()
{
    return buffs_.HoveredBuff() == 0;
}

void SEASON3B::CNewUIBuffWindow::ProcessBuffCancel()
{
    const auto buff = static_cast<eBuffState>(buffs_.TakeCancel());
    if (buff == eBuff_InfinityArrow)
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CInfinityArrowCancelMsgBoxLayout, SessionOrigin()));
    else if (buff == eBuff_SwellOfMagicPower)
        SEASON3B::CreateMessageBox(
            MSGBOX_LAYOUT_CLASS(SEASON3B::CBuffSwellOfMPCancelMsgBoxLayOut, SessionOrigin()));
}

bool SEASON3B::CNewUIBuffWindow::UpdateKeyEvent()
{
    return true;
}

bool SEASON3B::CNewUIBuffWindow::Update()
{
    visible_ = IsVisible();
    if (!visible_)
        return true;
    ProcessBuffCancel();
    std::list<eBuffState> states;
    BuffSort(states);
    entries_.clear();
    for (const auto buff : states)
    {
        if (!buffs_.HasIcon(static_cast<int>(buff)))
            continue;
        const bool debuff = g_IsBuffClass(buff) == eBuffClass_DeBuff;
        entries_.push_back({static_cast<int>(buff), debuff ? 1 : 0, debuff});
    }
    return true;
}

bool SEASON3B::CNewUIBuffWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return visible_ && buffs_.ProcessInput(event);
}

float SEASON3B::CNewUIBuffWindow::GetLayerDepth() //. 5.3f
{
    return 0.95f;
}

void SEASON3B::CNewUIBuffWindow::OpenningProcess()
{
}

void SEASON3B::CNewUIBuffWindow::ClosingProcess()
{
    visible_ = false;
}

SEASON3B::CNewUICommandWindow::CNewUICommandWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), cursedTemple_(keeper.CursedTempleObject()),
      m_modernPanel(keeper)
{
}

SEASON3B::CNewUICommandWindow::~CNewUICommandWindow()
{
    Release();
}

bool SEASON3B::CNewUICommandWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_COMMAND, this);

    SetPos(x, y);

    LoadImages();
    m_modernPanel.Create();

    Show(false);

    return true;
}

void SEASON3B::CNewUICommandWindow::Release()
{
    m_modernPanel.Release();
    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUICommandWindow::OpenningProcess()
{
    m_iCurSelectCommand = COMMAND_NONE;
    m_iCurMouseCursor = CURSOR_NORMAL;
    for (int command = COMMAND_TRADE; command < COMMAND_END; ++command)
        m_modernPanel.CommandButton(command).Reset();
    m_modernPanel.CloseButton().Reset();
    UpdateButtonAvailability();
}

void SEASON3B::CNewUICommandWindow::ClosingProcess()
{
    m_iCurSelectCommand = COMMAND_NONE;
    m_iCurMouseCursor = CURSOR_NORMAL;
}

void SEASON3B::CNewUICommandWindow::BeginTargetCommand(int command)
{
    m_iCurSelectCommand =
        cursedTemple_.GetInterfaceState(static_cast<int>(SEASON3B::INTERFACE_COMMAND), command)
            ? command
            : COMMAND_NONE;
}

bool SEASON3B::CNewUICommandWindow::BtnProcess()
{
    if (m_modernPanel.CloseButton().IsClick())
    {
        g_pNewUISystem->Hide(SEASON3B::INTERFACE_COMMAND);
        PlayBuffer(SOUND_CLICK01);
        return true;
    }

    for (int command = COMMAND_TRADE; command < COMMAND_END; ++command)
    {
        if (m_modernPanel.CommandButton(command).IsClick())
        {
            BeginTargetCommand(command);
            return true;
        }
    }

    return false;
}

bool SEASON3B::CNewUICommandWindow::UpdateMouseEvent()
{
    (void)BtnProcess();
    const auto rect = m_modernPanel.ReferenceRect(static_cast<int>(ModernUiViewportWidth()),
                                                  static_cast<int>(ModernUiViewportHeight()));
    const bool inside = MouseX >= rect.x && MouseX < rect.x + rect.width && MouseY >= rect.y &&
                        MouseY < rect.y + rect.height;
    if (inside)
    {
        SetMouseCursor(CURSOR_NORMAL);
        return false;
    }
    else
    {
        if (m_iCurSelectCommand != COMMAND_NONE)
            SetMouseCursor(CURSOR_IDSELECT);
    }

    return true;
}

bool SEASON3B::CNewUICommandWindow::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_COMMAND) == true)
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_COMMAND);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    return true;
}

bool SEASON3B::CNewUICommandWindow::Update()
{
    if (IsVisible())
    {
        UpdateButtonAvailability();
        SelectCommand();
        RunCommand();
    }

    return true;
}

float SEASON3B::CNewUICommandWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Command;
}

void SEASON3B::CNewUICommandWindow::RunCommand()
{
    if (MouseLButtonPush && m_iCurMouseCursor != CURSOR_IDSELECT)
        SetMouseCursor(CURSOR_PUSH);

    if (m_iCurSelectCommand == COMMAND_NONE)
        return;

    int Selectindex = -1;
    CHARACTER *pSelectedCha = NULL;
    int distX, distY;
    m_bSelectedChar = false;
    m_bCanCommand = false;

    if (SelectedCharacter >= 0)
    {
        pSelectedCha = &CharactersClient[SelectedCharacter];
        m_bSelectedChar = true;
        if (pSelectedCha != NULL)
        {
            distX = abs((pSelectedCha->PositionX) - (Hero->PositionX));
            distY = abs((pSelectedCha->PositionY) - (Hero->PositionY));
            if (pSelectedCha->Object.Kind == KIND_PLAYER && pSelectedCha != Hero &&
                (pSelectedCha->Object.Type == MODEL_PLAYER || pSelectedCha->Change) &&
                (distX <= MAX_DISTANCE_TILE && distY <= MAX_DISTANCE_TILE))
            {
                if ((pSelectedCha->Object.SubType != MODEL_XMAS_EVENT_CHA_DEER) &&
                    (pSelectedCha->Object.SubType != MODEL_XMAS_EVENT_CHA_SNOWMAN) &&
                    (pSelectedCha->Object.SubType != MODEL_XMAS_EVENT_CHA_SSANTA))
                {
                    Selectindex = SelectedCharacter;
                    m_bCanCommand = true;
                }
            }
        }
    }

    if (MouseRButtonPush)
    {
        MouseRButtonPush = false;
        MouseRButton = false;

        SetMouseCursor(CURSOR_NORMAL);

        if (Selectindex >= 0)
        {
            switch (m_iCurSelectCommand)
            {
            case COMMAND_TRADE: {
                CommandTrade(pSelectedCha);
            }
            break;

            case COMMAND_PURCHASE: {
                CommandPurchase(pSelectedCha);
            }
            break;

            case COMMAND_PARTY: {
                CommandParty(pSelectedCha->Key);
            }
            break;

            case COMMAND_WHISPER: {
                CommandWhisper(pSelectedCha);
            }
            break;

            case COMMAND_GUILD: {
                CommandGuild(pSelectedCha);
            }
            break;

            case COMMAND_GUILDUNION: {
                CommandGuildUnion(pSelectedCha);
            }
            break;

            case COMMAND_RIVAL: {
                CommandGuildRival(pSelectedCha);
            }
            break;

            case COMMAND_RIVALOFF: {
                CommandCancelGuildRival(pSelectedCha);
            }
            break;

            case COMMAND_ADD_FRIEND: {
                CommandAddFriend(pSelectedCha);
            }
            break;

            case COMMAND_FOLLOW: {
                CommandFollow(Selectindex);
            }
            break;

            case COMMAND_BATTLE: {
                CommandDual(pSelectedCha);
            }
            break;
            }
        }
        m_iCurSelectCommand = COMMAND_NONE;
    }
}

void SEASON3B::CNewUICommandWindow::SelectCommand()
{
}

int SEASON3B::CNewUICommandWindow::GetCurCommandType()
{
    return m_iCurSelectCommand;
}

void SEASON3B::CNewUICommandWindow::SetMouseCursor(int iCursorType)
{
    m_iCurMouseCursor = iCursorType;
}

int SEASON3B::CNewUICommandWindow::GetMouseCursor()
{
    return m_iCurMouseCursor;
}

void SEASON3B::CNewUICommandWindow::UpdateButtonAvailability()
{
    for (int command = COMMAND_TRADE; command < COMMAND_END; ++command)
    {
        m_modernPanel.CommandButton(command).SetEnable(cursedTemple_.GetInterfaceState(
            static_cast<int>(SEASON3B::INTERFACE_COMMAND), command));
    }
}

bool SEASON3B::CNewUICommandWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && m_modernPanel.ProcessInput(event);
}

bool SEASON3B::CNewUICommandWindow::CommandTrade(CHARACTER *pSelectedCha)
{
    if (pSelectedCha == NULL)
        return false;

    int level = CharacterAttribute->Level;

    if (level < TRADELIMITLEVEL)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCanUseTheTradeCommandAtCharacterLevel6,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if (IsShopInViewport(pSelectedCha))
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotTradeRightNow, SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    SocketClient->ToGameServer()->SendTradeRequest(pSelectedCha->Key);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandPurchase(CHARACTER *pSelectedCha)
{
    if (pSelectedCha == nullptr)
        return false;

    SocketClient->ToGameServer()->SendPlayerShopItemListRequest(pSelectedCha->Key,
                                                                pSelectedCha->ID);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandParty(SHORT iChaKey)
{
    if (PartyNumber > 0 && wcscmp(Party[0].Name, Hero->ID) != 0)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouAreAlreadyInAParty, SEASON3B::TYPE_ERROR_MESSAGE);
        return false;
    }

    SocketClient->ToGameServer()->SendPartyInviteRequest(iChaKey);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandWhisper(CHARACTER *pSelectedCha)
{
    g_pChatInputBox->SetWhsprID(pSelectedCha->ID);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandGuild(CHARACTER *pSelectedChar)
{
    if (Hero->GuildStatus != G_NONE)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouAreAlreadyInAGuild, SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if ((pSelectedChar->GuildMarkIndex < 0) || (pSelectedChar->GuildStatus != G_MASTER))
    {
        g_pSystemLogBox->AddText(I18N::Game::TheUserIsNotAGuildMaster,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }

    SocketClient->ToGameServer()->SendGuildJoinRequest(pSelectedChar->Key);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandGuildUnion(CHARACTER *pSelectedCha)
{
    if (Hero->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::NotAGuildMaster, SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if (pSelectedCha->GuildStatus == G_NONE)
    {
        g_pSystemLogBox->AddText(I18N::Game::ThisDoesNotBelongToTheGuild,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if (pSelectedCha->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::TheUserIsNotAGuildMaster,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if (pSelectedCha->GuildStatus == G_MASTER)
    {
        SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
            GuildRelationshipType::Alliance, GuildRequestType::Join, pSelectedCha->Key);
        return true;
    }

    return false;
}

bool SEASON3B::CNewUICommandWindow::CommandGuildRival(CHARACTER *pSelectedCha)
{
    if (Hero->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::NotAGuildMaster, SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }

    if (pSelectedCha->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::TheUserIsNotAGuildMaster,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }

    SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
        GuildRelationshipType::Hostility, GuildRequestType::Join, pSelectedCha->Key);

    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandCancelGuildRival(CHARACTER *pSelectedCha)
{
    if (Hero->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::NotAGuildMaster, SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    if (pSelectedCha->GuildStatus != G_MASTER)
    {
        g_pSystemLogBox->AddText(I18N::Game::TheUserIsNotAGuildMaster,
                                 SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }

    SetAction(&Hero->Object, PLAYER_RESPECT1);
    SendRequestAction(Hero->Object, AT_RESPECT1);
    SocketClient->ToGameServer()->SendGuildRelationshipChangeRequest(
        GuildRelationshipType::Hostility, GuildRequestType::Leave, pSelectedCha->Key);
    return true;
}

bool SEASON3B::CNewUICommandWindow::CommandAddFriend(CHARACTER *pSelectedCha)
{
    if (g_pWindowMgr->IsServerEnable() == TRUE && pSelectedCha != nullptr)
    {
        SocketClient->ToGameServer()->SendFriendAddRequest(pSelectedCha->ID);
        return true;
    }

    return false;
}

bool SEASON3B::CNewUICommandWindow::CommandFollow(int iSelectedChaIndex)
{
    if (iSelectedChaIndex < 0)
    {
        return false;
    }

    g_iFollowCharacter = iSelectedChaIndex;

    return true;
}

int SEASON3B::CNewUICommandWindow::CommandDual(CHARACTER *pSelectedCha)
{
    int iLevel = CharacterAttribute->Level;
    if (iLevel < 30)
    {
        wchar_t szError[48] = L"";
        mu_swprintf(szError, I18N::Game::OpenOnlyForLevelDOrHigher, 30);
        g_pSystemLogBox->AddText(szError, SEASON3B::TYPE_ERROR_MESSAGE);
        return 3;
    }
    else if (gMapManager.ContextMap() >= WD_65DOPPLEGANGER1 &&
             gMapManager.ContextMap() <= WD_68DOPPLEGANGER4)
    {
        g_pSystemLogBox->AddText(I18N::Game::DuelingIsNotPossibleInThisArea,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return 3;
    }
    else if (gMapManager.ContextMap() == WD_79UNITEDMARKETPLACE)
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotEngageInDuelsWhileInLorenMarket,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return 3;
    }
    else if (!g_DuelMgr.IsDuelEnabled())
    {
        SocketClient->ToGameServer()->SendDuelStartRequest(pSelectedCha->Key, pSelectedCha->ID);
        return 1;
    }
    else if (g_DuelMgr.IsDuelEnabled() && g_DuelMgr.IsDuelPlayer(pSelectedCha, DUEL_ENEMY))
    {
        SocketClient->ToGameServer()->SendDuelStopRequest();
        return 2;
    }
    else
    {
        g_pSystemLogBox->AddText(I18N::Game::YouCannotChallengePlayerIsAlreadyInADuel,
                                 SEASON3B::TYPE_ERROR_MESSAGE);
        return 3;
    }
    return 0;
}

#ifdef PBG_ADD_GENSRANKING

using namespace SEASON3B;

#define TEMP_MAX_TEXT_LENGTH 1024

CNewUIGensRanking::CNewUIGensRanking(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_fBooleanSize(0.8f), renderer_(RendererForConstruction()),
      panel_(keeper)
{
    Init();
}

CNewUIGensRanking::~CNewUIGensRanking()
{
    panel_.Release();
    Destroy();
}

void CNewUIGensRanking::Init()
{
    m_nContribution = 0;
    memset(m_szRanking, 0, sizeof(m_szRanking));

    m_Pos.x = 0;
    m_Pos.y = 0;

    memset(m_szGensTeam, 0, sizeof(m_szGensTeam));

    m_byGensInfluence = GENSTYPE_NONE;
    m_ptRenderMarkPos.x = 0;
    m_ptRenderMarkPos.y = 0;

    m_nNextContribution = 0;

    memset(m_szTitleName, 0, sizeof(m_szTitleName));
    SetTitleName();
}

void CNewUIGensRanking::Destroy()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIGensRanking::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (pNewUIMng == NULL)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_GENSRANKING, this);

    SetPos(x, y);

    Show(false);

    return true;
}

bool CNewUIGensRanking::Update()
{
    const auto changes = panel_.TakeChanges();
    if (changes.focus && m_pNewUIMng)
        m_pNewUIMng->BringToFront(this);
    if (IsVisible() && changes.close)
        g_pNewUISystem->Hide(INTERFACE_GENSRANKING);
    if (IsVisible())
        StageContent();
    visible_ = IsVisible();
    return true;
}

bool CNewUIGensRanking::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && panel_.ProcessInput(event);
}
bool CNewUIGensRanking::UpdateMouseEvent()
{
    return !panel_.ContainsReferencePointer(MouseX, MouseY);
}
float CNewUIGensRanking::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

bool CNewUIGensRanking::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(INTERFACE_GENSRANKING))
    {
        if (IsPress(VK_ESCAPE))
        {
            g_pNewUISystem->Hide(INTERFACE_GENSRANKING);
            return false;
        }
    }
    return true;
}

void CNewUIGensRanking::OpenningProcess()
{
    visible_ = contentDirty_ = true;
    StageContent();
    SocketClient->ToGameServer()->SendGensRankingRequest();
}

void CNewUIGensRanking::ClosingProcess()
{
    visible_ = false;
}

void CNewUIGensRanking::SetContribution(int _Contribution)
{
    contentDirty_ = true;
    if (_Contribution < 0)
    {
        m_nContribution = 0;
        return;
    }

    m_nContribution = _Contribution;
}

int CNewUIGensRanking::GetContribution()
{
    if (m_nContribution <= 0)
        return 0;

    return m_nContribution;
}

bool CNewUIGensRanking::SetRanking(int _Ranking)
{
    contentDirty_ = true;
    if (_Ranking <= 0)
    {
        mu_swprintf(m_szRanking, L"-");
        return false;
    }

    _itow(_Ranking, m_szRanking, 10);
    return true;
}

wchar_t *CNewUIGensRanking::GetRanking()
{
    return m_szRanking;
}

void CNewUIGensRanking::SetNextContribution(int _NextContribution)
{
    contentDirty_ = true;
    if (_NextContribution < 0)
    {
        m_nNextContribution = -1;
        return;
    }
    m_nNextContribution = _NextContribution;
}

int CNewUIGensRanking::GetNextContribution()
{
    return m_nNextContribution;
}

bool CNewUIGensRanking::SetGensInfo()
{
    contentDirty_ = true;
    m_byGensInfluence = (GENS_TYPE)Hero->m_byGensInfluence;

    if ((m_byGensInfluence & GENSTYPE_DUPRIAN) == GENSTYPE_DUPRIAN)
    {
        SetGensTeamName(I18N::Game::Duprian);
        return true;
    }
    else if ((m_byGensInfluence & GENSTYPE_BARNERT) == GENSTYPE_BARNERT)
    {
        SetGensTeamName(I18N::Game::Vanert);
        return true;
    }
    else
    {
        g_pSystemLogBox->AddText(I18N::Game::YouHaveNotJoinedAGens, SEASON3B::TYPE_SYSTEM_MESSAGE);
        return false;
    }
    return false;
}

bool CNewUIGensRanking::SetGensTeamName(const wchar_t *_pTeamName)
{
    contentDirty_ = true;
    if (_pTeamName)
    {
        wcscpy(m_szGensTeam, _pTeamName);
        return true;
    }
    return false;
}

wchar_t *CNewUIGensRanking::GetGensTeamName()
{
    return m_szGensTeam;
}

wchar_t *CNewUIGensRanking::GetTitleName(BYTE _index)
{
    if (TITLENAME_START <= _index && TITLENAME_END >= _index)
        return m_szTitleName[_index - 1];
    else
        return m_szTitleName[TITLENAME_END - 1];
}

int CNewUIGensRanking::GetImageIndex(BYTE rankIndex)
{
    if (rankIndex < TITLENAME_START || rankIndex > TITLENAME_END)
    {
        rankIndex = TITLENAME_END;
    }

    if (rankIndex > TITLENAME_END || rankIndex <= TITLENAME_NONE)
    {
        return -1;
    }

    return TITLENAME_END - rankIndex;
}
#endif //PBG_ADD_GENSRANKING

SEASON3B::CNewUIHotKey::CNewUIHotKey(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), gameplay(GameplayForConstruction()),
      muHelper(MuHelperForConstruction()), g_InGameShopSystem(keeper.InGameShopSystemObject()),
      Items(keeper.ItemsStorage()), m_pNewUIMng(NULL), m_bStateGameOver(false)
{
}

SEASON3B::CNewUIHotKey::~CNewUIHotKey()
{
    Release();
}

bool SEASON3B::CNewUIHotKey::Create(CNewUIManager *pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_HOTKEY, this);
    Show(true);
    return true;
}

void SEASON3B::CNewUIHotKey::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIHotKey::UpdateMouseEvent()
{
    if (g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
    {
        return true;
    }

    if (SelectedCharacter >= 0)
    {
        if (IsRepeat(VK_MENU) && IsRelease(VK_RBUTTON) && gMapManager.InChaosCastle() == false &&
            gMapManager.IsCursedTemple() == false)
        {
            CHARACTER *pCha = &CharactersClient[SelectedCharacter];

            if (pCha->Object.Kind != KIND_PLAYER)
            {
                return false;
            }

            if ((pCha->Object.SubType == MODEL_XMAS_EVENT_CHA_DEER) ||
                (pCha->Object.SubType == MODEL_XMAS_EVENT_CHA_SNOWMAN) ||
                (pCha->Object.SubType == MODEL_XMAS_EVENT_CHA_SSANTA))
            {
                return false;
            }

            if (IsStrifeMap(gMapManager.ContextMap()) &&
                Hero->m_byGensInfluence != pCha->m_byGensInfluence)
                return false;

            float fPos_x = pCha->Object.Position[0] - Hero->Object.Position[0];
            float fPos_y = pCha->Object.Position[1] - Hero->Object.Position[1];
            float fDistance = sqrtf((fPos_x * fPos_x) + (fPos_y * fPos_y));

            if (fDistance < 300.f)
            {
                int x, y;
                x = MouseX + 10;
                y = MouseY - 50;
                if (y < 0)
                {
                    y = 0;
                }
                g_pQuickCommand->OpenQuickCommand(pCha->ID, SelectedCharacter, x, y);
            }
            else
            {
                g_pSystemLogBox->AddText(I18N::Game::ItCannotBeUsedDueToTheDistance,
                                         SEASON3B::TYPE_ERROR_MESSAGE);
                g_pQuickCommand->CloseQuickCommand();
            }

            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUIHotKey::OpenSystemMenuOnEscape()
{
    if (!IsPress(VK_ESCAPE) || !g_MessageBox.IsEmpty())
        return false;
    SEASON3B::CreateSystemMenuMessageBox(SessionOrigin());
    PlayBuffer(SOUND_CLICK01);
    return true;
}

bool SEASON3B::CNewUIHotKey::UpdateKeyEvent()
{
    if (OpenSystemMenuOnEscape())
        return false;

    if (m_bStateGameOver == true)
    {
        return false;
    }

    if (g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
    {
        if (IsPress('M') == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MOVEMAP);
            PlayBuffer(SOUND_CLICK01);
        }
        return false;
    }

    if (AutoGetItem() == true)
    {
        return false;
    }

    if (CanUpdateKeyEventRelatedMyInventory() == true)
    {
        if (IsPress('I') || IsPress('V'))
        {
            if (g_pNPCShop->IsSellingItem() == false)
            {
                g_pNewUISystem->Toggle(SEASON3B::INTERFACE_INVENTORY);
                PlayBuffer(SOUND_CLICK01);
                return false;
            }
        }

        return true;
    }
    else if (CanUpdateKeyEvent() == false)
    {
        return true;
    }

    if (IsPress('F'))
    {
        if (gMapManager.InChaosCastle() == true)
        {
            return true;
        }

        int iLevel = CharacterAttribute->Level;

        if (iLevel < 6)
        {
            if (g_pSystemLogBox->CheckChatRedundancy(
                    I18N::Game::YouMustBeAtLeastLevel6ToUseTheMyFriendFunction) == FALSE)
            {
                g_pSystemLogBox->AddText(I18N::Game::YouMustBeAtLeastLevel6ToUseTheMyFriendFunction,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
            }
        }
        else
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_FRIEND);
        }

        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('I') || IsPress('V'))
    {
        if (g_pNPCShop->IsSellingItem() == false)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }
    else if (IsPress('C'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_CHARACTER);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('T'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MYQUEST);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('G'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_GUILDINFO);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('A'))
    {
        if (gCharacterManager.IsMasterLevel(Hero->Class) == true &&
            Hero->Class != CLASS_TEMPLENIGHT)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MASTER_LEVEL);
        }

        PlayBuffer(SOUND_CLICK01);

        return false;
    }
    else if (IsPress('U'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_WINDOW_MENU);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('O'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_OPTION);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (gMapManager.InChaosCastle() == false && IsPress('D'))
    {
        if (IsStrifeMap(gMapManager.ContextMap()))
        {
            if (g_pSystemLogBox->CheckChatRedundancy(
                    I18N::Game::TheCommandWindowCannotBeActivatedInBattleZone) == FALSE)
                g_pSystemLogBox->AddText(I18N::Game::TheCommandWindowCannotBeActivatedInBattleZone,
                                         SEASON3B::TYPE_SYSTEM_MESSAGE);
        }
        else
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_COMMAND);
            PlayBuffer(SOUND_CLICK01);
        }

        return false;
    }
    else if (IsPress(VK_F1) == true)
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_HELP);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('M') == true)
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MOVEMAP);
        PlayBuffer(SOUND_CLICK01);

        return false;
    }
    else if (IsPress(VK_TAB) == true && gMapManager.InBattleCastle() == true)
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_SIEGEWARFARE);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress(VK_TAB) == true)
    {
        if (g_pNewUIMiniMap->m_bSuccess == false)
        {
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_MINI_MAP);
        }
        else
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MINI_MAP);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (IsPress('X') == true)
    {
        g_ConsoleDebug.Write(MCD_NORMAL,
                             L"InGameShopStatue.Txt CallStack - CNewUIHotKey.UpdateKeyEvent()");
        if (g_pInGameShop->IsInGameShopOpen() == false)
            return false;

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
        if (g_InGameShopSystem.IsScriptDownload() == true)
        {
            if (g_InGameShopSystem.ScriptDownload() == false)
                return false;
        }
        if (g_InGameShopSystem.IsBannerDownload() == true)
        {
            if (g_InGameShopSystem.BannerDownload() == true)
            {
                g_pInGameShop->InitBanner(g_InGameShopSystem.GetBannerFileName(),
                                          g_InGameShopSystem.GetBannerURL());
            }
        }
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

        if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP) == false)
        {
            if (g_InGameShopSystem.GetIsRequestShopOpenning() == false)
            {
                SocketClient->ToGameServer()->SendCashShopOpenState(0);
                g_InGameShopSystem.SetIsRequestShopOpenning(true);
#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
                g_pMainFrame->SetBtnState(MAINFRAME_BTN_PARTCHARGE, true);
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD
            }
        }
        else
        {
            SocketClient->ToGameServer()->SendCashShopOpenState(1);
            g_pNewUISystem->Hide(SEASON3B::INTERFACE_INGAMESHOP);
        }

        return false;
    }
#endif // PBG_ADD_INGAMESHOP_UI_MAINFRAME
    else if (IsPress('B'))
    {
        if (!g_pNewUIGensRanking->SetGensInfo())
            return false;

        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_GENSRANKING);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress(VK_HOME) && !g_pChatInputBox->HaveFocus())
    {
        muHelper.Toggle();
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    else if (IsPress('Z'))
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MUHELPER);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

bool SEASON3B::CNewUIHotKey::Update()
{
    return true;
}

bool SEASON3B::CNewUIHotKey::CanUpdateKeyEventRelatedMyInventory()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MIXINVENTORY) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_TRADE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_STORAGE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCSHOP) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MYSHOP_INVENTORY) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_PURCHASESHOP_INVENTORY) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYITEMWND))
    {
        return true;
    }
    return false;
}

bool SEASON3B::CNewUIHotKey::CanUpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_KANTURU2ND_ENTERNPC) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CATAPULT) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCQUEST) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_SENATUS) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GATEKEEPER) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GUARDSMAN) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GATESWITCH) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPCGUILDMASTER) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_BLOODCASTLE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DEVILSQUARE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CURSEDTEMPLE_NPC) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MASTER_LEVEL) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DUELWATCH) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_DOPPELGANGER_NPC) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_NPC_DIALOGUE) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_QUEST_PROGRESS_ETC) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GOLD_BOWMAN) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_GOLD_BOWMAN_LENA) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_LUCKYCOIN_REGISTRATION) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_EXCHANGE_LUCKYCOIN) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_EMPIREGUARDIAN_NPC) ||
        g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_UNITEDMARKETPLACE_NPC_JULIA))
    {
        return false;
    }

    return true;
}

float SEASON3B::CNewUIHotKey::GetLayerDepth()
{
    return 1.0f;
}

float SEASON3B::CNewUIHotKey::GetKeyEventOrder()
{
    return 1.0f;
}

bool SEASON3B::CNewUIHotKey::IsStateGameOver()
{
    return m_bStateGameOver;
}

bool SEASON3B::CNewUIHotKey::AutoGetItem()
{
    if (g_pMyInventory->GetInventoryCtrl()->GetPickedItem() == NULL && IsPress(VK_SPACE) &&
        g_pChatInputBox->HaveFocus() == false && CheckMouseIn(0, 0, GetScreenWidth(), 429))
    {
        for (int i = 0; i < MAX_ITEMS; ++i)
        {
            OBJECT *pObj = &Items[i].Object;
            if (pObj->Live && pObj->Visible)
            {
                vec3_t vDir;
                VectorSubtract(pObj->Position, Hero->Object.Position, vDir);
                if (VectorLength(vDir) < 300)
                {
                    Hero->MovementType = MOVEMENT_GET;
                    ItemKey = i;
                    g_bAutoGetItem = true;
                    Action(Hero, pObj, true);
                    Hero->MovementType = MOVEMENT_MOVE;
                    g_bAutoGetItem = false;

                    return true;
                }
            }
        }
    }

    return false;
}

SEASON3B::CNewUIMasterLevel::CNewUIMasterLevel(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), modernPanel_(keeper)
{
    m_pNewUIMng = nullptr;
    this->ConsumePoint = 0;
    this->CurSkillID = 0;
    this->classCode = MASTER_SKILL_TREE_CLASS_NONE;
    this->CategoryTextIndex = 0;
    this->InitMasterSkillPoint();
    this->ClearSkillTreeData();
    this->ClearSkillTooltipData();
}

SEASON3B::CNewUIMasterLevel::~CNewUIMasterLevel()
{
    this->Release();
}

BYTE SEASON3B::CNewUIMasterLevel::GetConsumePoint() const
{
    return this->ConsumePoint;
}

int SEASON3B::CNewUIMasterLevel::GetCurSkillID() const
{
    return this->CurSkillID;
}

bool SEASON3B::CNewUIMasterLevel::Create(CNewUIManager *pNewUIMng)
{
    if (nullptr == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MASTER_LEVEL, this);

    return true;
}

void SEASON3B::CNewUIMasterLevel::Release()
{
    modernPanel_.Release();
    this->ClearSkillTreeData();
    this->ClearSkillTooltipData();
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

void SEASON3B::CNewUIMasterLevel::OpenMasterSkillTreeData(const wchar_t *path)
{
    memset(m_stMasterSkillTreeData, 0, sizeof(m_stMasterSkillTreeData));

    FILE *fp = _wfopen(path, L"rb");

    wchar_t Text[256];

    if (fp == nullptr)
    {
        mu_swprintf(Text, L"%ls - File not exist.", path);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, nullptr, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        return;
    }

    constexpr int Size = sizeof(_MASTER_SKILLTREE_DATA);

    auto Buffer = new BYTE[Size * MAX_MASTER_SKILL_DATA];

    fread(Buffer, Size * MAX_MASTER_SKILL_DATA, 1, fp);

    DWORD dwCheckSum;

    fread(&dwCheckSum, sizeof(DWORD), 1u, fp);

    fclose(fp);

    if (dwCheckSum != GenerateCheckSum2(Buffer, 12288, 0x2BC1))
    {
        mu_swprintf(Text, L"%ls - File corrupted.", path);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, nullptr, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        return;
    }

    BYTE *pSeek = Buffer;

    for (int i = 0; i < MAX_MASTER_SKILL_DATA; i++)
    {
        BuxConvert(pSeek, Size);

        memcpy(&m_stMasterSkillTreeData[i], pSeek, Size);

        pSeek += Size;

        if (pSeek == nullptr)
        {
            break;
        }
    }

    delete[] Buffer;
}

void SEASON3B::CNewUIMasterLevel::OpenMasterSkillTooltip(const wchar_t *path)
{
    memset(m_stMasterSkillTooltip, 0, sizeof(m_stMasterSkillTooltip));

    FILE *fp = _wfopen(path, L"rb");

    if (fp == nullptr)
    {
        wchar_t Text[256];
        mu_swprintf(Text, L"%ls - File not exist.", path);
        g_ErrorReport.Write(Text);
        MessageBox(g_hWnd, Text, nullptr, MB_OK);
        SendMessage(g_hWnd, WM_DESTROY, 0, 0);
        return;
    }

    constexpr int record_size = sizeof(_MASTER_SKILL_TOOLTIP_FILE);
    auto file_buffer = new BYTE[record_size * MAX_MASTER_SKILL_DATA];
    fread(file_buffer, record_size * MAX_MASTER_SKILL_DATA, 1, fp);
    DWORD dwCheckSum;
    fread(&dwCheckSum, sizeof(DWORD), 1u, fp);
    fclose(fp);

    BYTE *pSeek = file_buffer;

    for (int i = 0; i < MAX_MASTER_SKILL_DATA; i++)
    {
        BuxConvert(pSeek, record_size);

        _MASTER_SKILL_TOOLTIP_FILE current{};
        memcpy(&current, pSeek, record_size);

        const auto target = &m_stMasterSkillTooltip[i];
        target->SkillNumber = static_cast<ActionSkillType>(current.SkillNumber);
        target->ClassCode = static_cast<MASTER_SKILL_TREE_CLASS>(current.ClassCode);
        CMultiLanguage::ConvertFromUtf8(target->Info1, current.Info1);
        CMultiLanguage::ConvertFromUtf8(target->Info2, current.Info2);
        CMultiLanguage::ConvertFromUtf8(target->Info3, current.Info3);
        CMultiLanguage::ConvertFromUtf8(target->Info4, current.Info4);
        CMultiLanguage::ConvertFromUtf8(target->Info5, current.Info5);
        CMultiLanguage::ConvertFromUtf8(target->Info6, current.Info6);
        CMultiLanguage::ConvertFromUtf8(target->Info7, current.Info7);

        pSeek += record_size;

        if (pSeek == nullptr)
        {
            break;
        }
    }

    delete[] file_buffer;
}

void SEASON3B::CNewUIMasterLevel::InitMasterSkillPoint()
{
    for (int i = 0; i < 3; i++)
    {
        this->CategoryPoint[i] = 0;
        for (int k = 0; k < 10; k++)
        {
            this->skillPoint[i][k] = 0;
        }
    }
}

void SEASON3B::CNewUIMasterLevel::SetMasterType(CLASS_TYPE Class)
{
    switch (Class)
    {
    case CLASS_GRANDMASTER:
        this->classCode = MASTER_SKILL_TREE_CLASS_GRANDMASTER;
        break;
    case CLASS_BLADEMASTER:
        this->classCode = MASTER_SKILL_TREE_CLASS_BLADEMASTER;
        break;
    case CLASS_HIGHELF:
        this->classCode = MASTER_SKILL_TREE_CLASS_HIGHELF;
        break;
    case CLASS_DUELMASTER:
        this->classCode = MASTER_SKILL_TREE_CLASS_DUELMASTER;
        break;
    case CLASS_LORDEMPEROR:
        this->classCode = MASTER_SKILL_TREE_CLASS_LORDEMPEROR;
        break;
    case CLASS_DIMENSIONMASTER:
        this->classCode = MASTER_SKILL_TREE_CLASS_DIMENSIONMASTER;
        break;
    case CLASS_TEMPLENIGHT:
        this->classCode = MASTER_SKILL_TREE_CLASS_TEMPLEKNIGHT;
        break;
    default:
        break;
    }

    this->SetMasterSkillTreeData();

    this->SetMasterSkillToolTipData();

    switch (Class)
    {
    case CLASS_WIZARD:
    case CLASS_SOULMASTER:
    case CLASS_GRANDMASTER:
        this->CategoryTextIndex = 1751;
        this->ClassNameTextIndex = 1669;
        break;
    case CLASS_KNIGHT:
    case CLASS_BLADEKNIGHT:
    case CLASS_BLADEMASTER:
        this->CategoryTextIndex = 1755;
        this->ClassNameTextIndex = 1668;
        break;
    case CLASS_ELF:
    case CLASS_MUSEELF:
    case CLASS_HIGHELF:
        this->CategoryTextIndex = 1759;
        this->ClassNameTextIndex = 1670;
        break;
    case CLASS_DARK:
    case CLASS_DUELMASTER:
        this->CategoryTextIndex = 1763;
        this->ClassNameTextIndex = 1671;
        break;
    case CLASS_DARK_LORD:
    case CLASS_LORDEMPEROR:
        this->CategoryTextIndex = 1767;
        this->ClassNameTextIndex = 1672;
        break;
    case CLASS_SUMMONER:
    case CLASS_BLOODYSUMMONER:
    case CLASS_DIMENSIONMASTER:
        this->CategoryTextIndex = 3136;
        this->ClassNameTextIndex = 1689;
        break;
    case CLASS_RAGEFIGHTER:
    case CLASS_TEMPLENIGHT:
        this->CategoryTextIndex = 3330;
        this->ClassNameTextIndex = 3151;
        break;
    default:
        return;
    }
}

void SEASON3B::CNewUIMasterLevel::SetMasterSkillTreeData()
{
    this->ClearSkillTreeData();

    for (int i = 0; i < MAX_MASTER_SKILL_DATA; i++)
    {
        if (m_stMasterSkillTreeData[i].Index == 0)
        {
            break;
        }

        if ((this->classCode & m_stMasterSkillTreeData[i].ClassCode) == 0)
        {
            continue;
        }

        if (!this->map_masterData
                 .insert(std::pair<BYTE, _MASTER_SKILLTREE_DATA>(m_stMasterSkillTreeData[i].Index,
                                                                 m_stMasterSkillTreeData[i]))
                 .second)
        {
            break;
        }
    }
}

void SEASON3B::CNewUIMasterLevel::SetMasterSkillToolTipData()
{
    this->ClearSkillTooltipData();

    for (int i = 0; i < MAX_MASTER_SKILL_DATA; i++)
    {
        if (m_stMasterSkillTooltip[i].SkillNumber == 0)
        {
            break;
        }

        if ((this->classCode & m_stMasterSkillTooltip[i].ClassCode) == 0)
        {
            continue;
        }

        if (!this->map_masterSkillToolTip
                 .insert(
                     std::pair(m_stMasterSkillTooltip[i].SkillNumber, m_stMasterSkillTooltip[i]))
                 .second)
        {
            break;
        }
    }
}

bool SEASON3B::CNewUIMasterLevel::SetMasterSkillTreeInfo(int index, BYTE skillLevel, float value,
                                                         float nextvalue)
{
    const auto it = this->map_masterData.find(index);

    if (it == this->map_masterData.end())
    {
        return false;
    }

    const CSkillTreeInfo skillInfo = {skillLevel, value, nextvalue};
    CharacterAttribute->MasterSkillInfo[it->second.Skill] = skillInfo;

    this->CategoryPoint[it->second.Group] += skillLevel;

    return true;
}

int SEASON3B::CNewUIMasterLevel::SetDivideString(wchar_t *text, int isItemTollTip, int TextNum,
                                                 int iTextColor, int iTextBold, bool isPercent)
{
    if (text == nullptr)
    {
        return TextNum;
    }

    wchar_t alpszDst[10][256] = {};

    int nLine = 0;

    if (isItemTollTip == 0)
    {
        nLine = DivideStringByPixel((LPTSTR)alpszDst, 10, 256, text, 150, true, 35);
    }
    else if (isItemTollTip == 1)
    {
        nLine = DivideStringByPixel((LPTSTR)alpszDst, 10, 256, text, 200, true, 35);
    }

    for (int i = 0; i < nLine; i++)
    {
        TextListColor[TextNum] = iTextColor;

        TextBold[TextNum] = iTextBold;

        std::wstring cText = alpszDst[i];

        if (isPercent)
        {
            for (int j = cText.find(L"%", 0); j != -1; j = cText.find(L"%", j + 2))
            {
                cText.insert(j, L"%");
            }
        }

        mu_swprintf(TextList[TextNum], cText.c_str());

        TextNum++;
    }

    return TextNum;
}

void SEASON3B::CNewUIMasterLevel::ConsumeModernActions()
{
    if (modernPanel_.TakeClose())
    {
        g_pNewUISystem->Hide(INTERFACE_MASTER_LEVEL);
        PlayBuffer(SOUND_CLICK01);
        return;
    }
    const int slot = modernPanel_.TakeSkillClick();
    if (const auto it = map_masterData.find(slot); slot && it != map_masterData.end())
        TryUpgradeSkill(it->second);
}

bool SEASON3B::CNewUIMasterLevel::Update()
{
    ConsumeModernActions();
    return true;
}

bool SEASON3B::CNewUIMasterLevel::UpdateMouseEvent()
{
    ConsumeModernActions();
    return !modernPanel_.OwnsPointer();
}

bool SEASON3B::CNewUIMasterLevel::ProcessModernUiInput(const SessionInputEvent &event)
{
    return modernPanel_.ProcessInput(event);
}

bool SEASON3B::CNewUIMasterLevel::UpdateKeyEvent()
{
    if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_MASTER_LEVEL) == false ||
        IsPress(VK_ESCAPE) == false && IsPress('A') == false)
    {
        return true;
    }

    g_pNewUISystem->Hide(SEASON3B::INTERFACE_MASTER_LEVEL);

    PlayBuffer(SOUND_CLICK01);

    return false;
}

float SEASON3B::CNewUIMasterLevel::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dynamic;
}

void SEASON3B::CNewUIMasterLevel::UpdateModernLabels()
{
    const std::array<std::uint64_t, 8> key{
        static_cast<std::uint64_t>(Master_Level_Data.nMLevel),
        static_cast<std::uint64_t>(Master_Level_Data.nMLevelUpMPoint),
        static_cast<std::uint64_t>(Master_Level_Data.lMasterLevel_Experince),
        static_cast<std::uint64_t>(Master_Level_Data.lNext_MasterLevel_Experince),
        ClassNameTextIndex,
        static_cast<std::uint64_t>(CategoryPoint[0]),
        static_cast<std::uint64_t>(CategoryPoint[1]),
        static_cast<std::uint64_t>(CategoryPoint[2])};
    if (modernTextSet_ && modernTextKey_ == key)
        return;
    modernTextSet_ = true;
    modernTextKey_ = key;
    auto &labels = modernContent_.labels;
    labels[0] = I18N::Game::Lookup(ClassNameTextIndex);
    wchar_t Buffer[256]{};
    mu_swprintf(Buffer, I18N::Game::MasterLevelD, Master_Level_Data.nMLevel);
    labels[1] = Buffer;
    mu_swprintf(Buffer, I18N::Game::LevelPointD, Master_Level_Data.nMLevelUpMPoint);
    labels[2] = Buffer;
    labels[3].clear();
    if (Master_Level_Data.lNext_MasterLevel_Experince != 0)
    {
        const __int64 iTotalLevel = Master_Level_Data.nMLevel + 400; // ???? - 400?? ???? ??? ????.
        const __int64 iTOverLevel = iTotalLevel - 255;               // 255?? ?? ?? ??
        __int64 iBaseExperience = 0;                                 // ?? ?? ???

        const __int64 iData_Master = // A
            (((__int64)9 + (__int64)iTotalLevel) * (__int64)iTotalLevel * (__int64)iTotalLevel *
             (__int64)10) +
            (((__int64)9 + (__int64)iTOverLevel) * (__int64)iTOverLevel * (__int64)iTOverLevel *
             (__int64)1000);

        iBaseExperience = (iData_Master - (__int64)3892250000) / (__int64)2; // B

        // ??? ???
        const double fNeedExp =
            (double)Master_Level_Data.lNext_MasterLevel_Experince - (double)iBaseExperience;

        // ?? ??? ???
        const double fExp =
            (double)Master_Level_Data.lMasterLevel_Experince - (double)iBaseExperience;

        mu_swprintf(Buffer, I18N::Game::EXP62f, fExp / fNeedExp * 100.0);

        labels[3] = Buffer;
    }
    for (int i = 0; i < MAX_MASTER_SKILL_CATEGORY; ++i)
    {
        mu_swprintf(Buffer, I18N::Game::Lookup(CategoryTextIndex + i), CategoryPoint[i]);
        labels[4 + i] = Buffer;
    }
}

void SEASON3B::CNewUIMasterLevel::UpdateModernContent()
{
    UpdateModernLabels();
    for (const auto &[index, data] : map_masterData)
    {
        const auto &attribute = SkillAttribute[data.Skill];
        const auto level = CharacterAttribute->MasterSkillInfo[data.Skill].GetSkillLevel();
        const bool disabled = !CheckParentSkill(data) ||
                              !CheckRankPoint(data.Group, attribute.SkillRank, level) ||
                              !CheckBeforeSkill(data.Skill, level) ||
                              !g_csItemOption.IsNonWeaponSkillOrIsSkillEquipped(data.Skill);
        auto &slot = modernContent_.slots[index - 1];
        slot.icon =
            UI::Modern::RmlSkillIconState::FromSkill(data.Skill, 3, attribute.Magic_Icon, disabled);
        slot.rank = level;
        slot.arrow = data.ArrowDirection;
    }
}

bool SEASON3B::CNewUIMasterLevel::TryUpgradeSkill(const _MASTER_SKILLTREE_DATA &skillData)
{
    const auto *lpskill = &SkillAttribute[skillData.Skill];
    const auto skillPoint = CharacterAttribute->MasterSkillInfo[skillData.Skill].GetSkillLevel();

    PlayBuffer(SOUND_CLICK01);

    if (!this->CheckSkillPoint(Master_Level_Data.nMLevelUpMPoint, skillData, skillPoint))
    {
        return true;
    }

    if (!g_csItemOption.IsNonWeaponSkillOrIsSkillEquipped(skillData.Skill))
    {
        CreateOkMessageBox(I18N::Game::YouNeedToWearTheRequiredEquipmentToLevelUpThisSkill);
        return true;
    }

    if (!this->CheckParentSkill(skillData) ||
        !this->CheckRankPoint(skillData.Group, lpskill->SkillRank, skillPoint) ||
        !this->CheckBeforeSkill(skillData.Skill, skillPoint))
    {
        CreateOkMessageBox(I18N::Game::YouMustMeetAllSkillRequirements);

        return true;
    }

    this->ConsumePoint = skillData.RequiredPoints;

    this->CurSkillID = skillData.Skill;

    SEASON3B::CreateMessageBox(
        MSGBOX_LAYOUT_CLASS(SEASON3B::CMaster_Level_Interface, SessionOrigin()));

    MouseLButton = false;

    MouseLButtonPop = false;

    MouseLButtonPush = false;

    return true;
}

bool SEASON3B::CNewUIMasterLevel::CheckSkillPoint(WORD mLevelUpPoint,
                                                  const _MASTER_SKILLTREE_DATA &skillData,
                                                  BYTE skillLevel)
{

    if (skillLevel >= skillData.MaxLevel)
    {
        CreateOkMessageBox(I18N::Game::YouCanTRaiseAnyMoreLevels);
        return false;
    }

    if (mLevelUpPoint >= skillData.RequiredPoints)
    {
        return true;
    }

    wchar_t Buffer[358] = {};

    mu_swprintf(Buffer, I18N::Game::YouCanTRaiseAnyMoreLevels,
                skillData.RequiredPoints - mLevelUpPoint);

    CreateOkMessageBox(Buffer);

    return false;
}

bool SEASON3B::CNewUIMasterLevel::CheckParentSkill(const _MASTER_SKILLTREE_DATA &masterSkill)
{
    for (int i = 0; i < MAX_MASTER_SKILL_REQUIRES; i++)
    {
        const auto requiredSkill = masterSkill.RequireSkill[i];
        if (requiredSkill == AT_SKILL_UNDEFINED)
        {
            return true;
        }

        if (requiredSkill < AT_SKILL_MASTER_BEGIN || requiredSkill > AT_SKILL_MASTER_END)
        {
            return true;
        }

        const auto reqSkillLevel =
            CharacterAttribute->MasterSkillInfo[requiredSkill].GetSkillLevel();
        if (reqSkillLevel < MASTER_SKILL_LEVEL_REQ_FOR_NEXT_RANK)
        {
            return false;
        }
    }

    return true;
}

bool SEASON3B::CNewUIMasterLevel::CheckRankPoint(BYTE group, BYTE rank, BYTE skillLevel)
{
    if (this->skillPoint[group][rank] < skillLevel)
    {
        this->skillPoint[group][rank] = skillLevel;
    }

    if (rank == 1)
    {
        return true;
    }

    return this->skillPoint[group][rank - 1] >= 10;
}

bool SEASON3B::CNewUIMasterLevel::CheckBeforeSkill(ActionSkillType skill, BYTE skillLevel)
{
    if (skillLevel != 0)
    {
        return true;
    }

    const auto Index = SkillAttribute[skill].SkillBrand;

    if (Index == 0)
    {
        return true;
    }

    const SKILL_ATTRIBUTE *lpSkill = &SkillAttribute[Index];

    if (lpSkill == nullptr)
    {
        return false;
    }

    if (lpSkill->SkillUseType == 4)
    {
        return true;
    }

    for (int i = 0; i < MAX_MAGIC; i++)
    {
        if (CharacterAttribute->Skill[i] == Index)
        {
            return true;
        }
    }

    return false;
}

void SEASON3B::CNewUIMasterLevel::SkillUpgrade(int index, BYTE skillLevel, float value,
                                               float nextValue)
{
    const auto it = this->map_masterData.find(index);
    if (it == this->map_masterData.end())
    {
        return;
    }

    const auto realSkill = it->second.Skill;
    const int oldLevel = CharacterAttribute->MasterSkillInfo[realSkill].GetSkillLevel();

    const CSkillTreeInfo skillTreeInfo = {skillLevel, value, nextValue};
    CharacterAttribute->MasterSkillInfo[realSkill] = skillTreeInfo;

    // And update the category points
    const int addedPoints = skillLevel - oldLevel;
    this->CategoryPoint[it->second.Group] += addedPoints;
}

void SEASON3B::CNewUIMasterLevel::ClearSkillTreeData()
{
    modernContent_.slots = {};
    map_masterData.clear();
}

void SEASON3B::CNewUIMasterLevel::ClearSkillTooltipData()
{
    if (!map_masterSkillToolTip.empty())
        this->map_masterSkillToolTip.clear();
}

CNewUIQuickCommandWindow::CNewUIQuickCommandWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_pNewUIMng(nullptr), m_Pos{}, m_strID{},
      m_iSelectedCharacterIndex(-1), renderer_(RendererForConstruction()),
      modernPanel_(keeper, "Command", "quick_command.rml", "quick-command")
{
    modernContent_.labels.resize(5);
}
CNewUIQuickCommandWindow::~CNewUIQuickCommandWindow()
{
    Release();
}

bool CNewUIQuickCommandWindow::Create(CNewUIManager *manager, int x, int y)
{
    if (!manager)
        return false;
    m_pNewUIMng = manager;
    modernContent_.labels.resize(5);
    manager->AddUIObj(INTERFACE_QUICK_COMMAND, this);
    SetPos(x, y);
    Show(false);
    return true;
}

void CNewUIQuickCommandWindow::Release()
{
    modernPanel_.Release();
    modernContent_ = {};
    if (!m_pNewUIMng)
        return;
    m_pNewUIMng->RemoveUIObj(this);
    m_pNewUIMng = nullptr;
}

bool CNewUIQuickCommandWindow::IsTargetAvailable() const
{
    if (m_iSelectedCharacterIndex < 0)
        return false;
    const CHARACTER &target = CharactersClient[m_iSelectedCharacterIndex];
    if (wcscmp(target.ID, m_strID) != 0 || !target.Object.Live || target.Object.Kind != KIND_PLAYER)
        return false;
    constexpr float MaximumCommandDistance = 300.0f;
    const float dx = target.Object.Position[0] - Hero->Object.Position[0];
    const float dy = target.Object.Position[1] - Hero->Object.Position[1];
    return dx * dx + dy * dy <= MaximumCommandDistance * MaximumCommandDistance;
}

void CNewUIQuickCommandWindow::ExecuteCommand(int command)
{
    if (!IsTargetAvailable())
    {
        CloseQuickCommand();
        return;
    }
    CHARACTER *target = &CharactersClient[m_iSelectedCharacterIndex];
    switch (command)
    {
    case 0:
        g_pCommandWindow->CommandTrade(target);
        break;
    case 1:
        g_pCommandWindow->CommandPurchase(target);
        break;
    case 2:
        g_pCommandWindow->CommandParty(target->Key);
        break;
    case 3:
        g_pCommandWindow->CommandFollow(m_iSelectedCharacterIndex);
        break;
    case 4:
        g_pCommandWindow->CommandDual(target);
        break;
    default:
        return;
    }
    CloseQuickCommand();
}

bool CNewUIQuickCommandWindow::UpdateMouseEvent()
{
    return !modernPanel_.ContainsReferencePointer(MouseX, MouseY);
}

bool CNewUIQuickCommandWindow::UpdateKeyEvent()
{
    if (!IsVisible() || !IsPress(VK_ESCAPE))
        return true;
    CloseQuickCommand();
    PlayBuffer(SOUND_CLICK01);
    return false;
}

bool CNewUIQuickCommandWindow::Update()
{
    if (IsVisible() && (!IsTargetAvailable() || modernPanel_.TakeDismiss()))
        CloseQuickCommand();
    const int command = modernPanel_.TakeCommand();
    if (IsVisible() && command >= 0)
        ExecuteCommand(command);
    StageModernContent();
    return true;
}

bool CNewUIQuickCommandWindow::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && modernPanel_.ProcessInput(event);
}
float CNewUIQuickCommandWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Dialog;
}
float CNewUIQuickCommandWindow::GetKeyEventOrder()
{
    return 10.0f;
}
void CNewUIQuickCommandWindow::OpenningProcess()
{
    m_iSelectedCharacterIndex = -1;
}
void CNewUIQuickCommandWindow::ClosingProcess()
{
    m_iSelectedCharacterIndex = -1;
    modernContent_.visible = false;
}

void CNewUIQuickCommandWindow::OpenQuickCommand(const wchar_t *id, int index, int x, int y)
{
    g_pNewUISystem->Show(INTERFACE_QUICK_COMMAND);
    SetID(id);
    SetSelectedCharacterIndex(index);
    SetPos(x, y);
    StageModernContent();
}
void CNewUIQuickCommandWindow::CloseQuickCommand()
{
    if (IsVisible())
        g_pNewUISystem->Hide(INTERFACE_QUICK_COMMAND);
}
void CNewUIQuickCommandWindow::SetID(const wchar_t *id)
{
    wcscpy_s(m_strID, id);
}
void CNewUIQuickCommandWindow::SetSelectedCharacterIndex(int index)
{
    m_iSelectedCharacterIndex = index;
}

UI::NoticeLegacyCalls::NoticeLegacyCalls(SessionKeeper &keeper, NoticeBoard &owner) noexcept
    : SessionUiLegacyBindings(keeper), owner_(owner)
{
}

UI::NoticeBoard::NoticeBoard(SessionKeeper &keeper) noexcept : NoticeLegacyCalls(keeper, *this)
{
}

void UI::NoticeBoard::Scroll()
{
    if (count_ <= MaxNotices - 1)
    {
        return;
    }

    count_ = MaxNotices - 1;
    for (int i = 1; i < MaxNotices; ++i)
    {
        notices_[i - 1].color = notices_[i].color;
        wcscpy(notices_[i - 1].text, notices_[i].text);
    }
}

void UI::NoticeBoard::Clear()
{
    memset(notices_, 0, sizeof(notices_));
    count_ = 0;
    time_ = NoticeLifetime;
    blinkPhase_ = 0.f;
}

void UI::NoticeLegacyCalls::Clear()
{
    owner_.Clear();
}

void UI::NoticeBoard::Create(const wchar_t *text, int color)
{
    SIZE size;
    g_RenderText.SetFont(LegacyFontRole::Bold);
    g_RenderText.MeasureText(text, lstrlen(text), &size);

    Scroll();
    notices_[count_].color = static_cast<BYTE>(color);
    if (size.cx < NoticeTextMax)
    {
        wcscpy(notices_[count_++].text, text);
    }
    else
    {
        wchar_t topText[NoticeTextMax]{};
        wchar_t bottomText[NoticeTextMax]{};
        CutText(text, topText, bottomText, NoticeTextMax);
        wcscpy(notices_[count_++].text, topText);
        Scroll();
        notices_[count_].color = static_cast<BYTE>(color);
        wcscpy(notices_[count_++].text, bottomText);
    }
    time_ = NoticeLifetime;
}

void UI::NoticeBoard::Move(float animationFactor)
{
    if (animationFactor <= 0.f)
        return;
    blinkPhase_ = std::fmod(blinkPhase_ + animationFactor, BlinkCycleFrames);
    while (animationFactor >= time_)
    {
        animationFactor -= time_;
        Create(L"", 0);
    }
    time_ -= animationFactor;
}

void UI::NoticeLegacyCalls::Scroll()
{
    return owner_.Scroll();
} // OMF-01936
void UI::NoticeLegacyCalls::Create(const wchar_t *text, int color)
{
    return owner_.Create(text, color);
} // OMF-01938

namespace
{

const UI::Modern::RmlUiDesign &Design()
{
    static const UI::Modern::RmlUiDesign design("Data/UI/PC/HUD/top_menu.rml",
                                                {"TopMenu-FrameWidth", "TopMenu-FrameHeight",
                                                 "TopMenu-OptionRect", "TopMenu-ActionRect",
                                                 "TopMenu-TooltipOffset"});
    return design;
}
} // namespace

CNewUIHeroPositionInfo::CNewUIHeroPositionInfo(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), muHelper_(MuHelperForConstruction()),
      renderer_(RendererForConstruction()), m_BtnConfig(keeper), m_BtnStart(keeper),
      m_BtnStop(keeper)
{
    m_pNewUIMng = NULL;
    m_CurHeroPosition.x = m_CurHeroPosition.y = 0;
}

CNewUIHeroPositionInfo::~CNewUIHeroPositionInfo()
{
    Release();
}

// Create
bool CNewUIHeroPositionInfo::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_HERO_POSITION_INFO, this);

    (void)x;
    (void)y;
    m_BtnConfig.ChangeToolTipText(&I18N::Game::OfficialMUHelperSetting);
    m_BtnStart.ChangeToolTipText(&I18N::Game::StartOfficialMUHelper);
    m_BtnStop.ChangeToolTipText(&I18N::Game::StopOfficialMUHelper);
    const auto offset = Design().Values(HeroPositionDetail::DesignKey::TooltipOffset);
    for (auto *button : {&m_BtnConfig, &m_BtnStart, &m_BtnStop})
        button->MoveTextTipPos(static_cast<int>(offset[0]), static_cast<int>(offset[1]));
    UpdateButtonGeometry();

    Show(true);
    StageTopMenu();

    return true;
}

void CNewUIHeroPositionInfo::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIHeroPositionInfo::BtnProcess()
{
    if (m_BtnConfig.UpdateMouseEvent())
    {
        g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MUHELPER);
        PlayBuffer(SOUND_CLICK01);
        return true;
    }

    auto &action = muHelper_.IsActive() ? m_BtnStop : m_BtnStart;
    if (action.UpdateMouseEvent())
    {
        muHelper_.Toggle();

        PlayBuffer(SOUND_CLICK01);
        return true;
    }

    return false;
}

bool CNewUIHeroPositionInfo::UpdateMouseEvent()
{
    if (true == BtnProcess())
    {
        return false;
    }

    const UI::Modern::RmlTopMenuTransform transform = UI::Modern::CalculateRmlTopMenuTransform(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale());
    const UI::Modern::RmlTopMenuRect frame = UI::Modern::CalculateRmlTopMenuReferenceRect(
        transform, ModernUiViewportWidth(), ModernUiViewportHeight(), 0.0F, 0.0F,
        Design().Number(HeroPositionDetail::DesignKey::FrameWidth),
        Design().Number(HeroPositionDetail::DesignKey::FrameHeight));
    if (CheckMouseIn(static_cast<int>(std::lround(frame.x)), static_cast<int>(std::lround(frame.y)),
                     static_cast<int>(std::lround(frame.width)),
                     static_cast<int>(std::lround(frame.height))))
    {
        return false;
    }

    return true;
}

bool CNewUIHeroPositionInfo::UpdateKeyEvent()
{
    return true;
}

bool CNewUIHeroPositionInfo::Update()
{
    if ((IsVisible() == true) && (Hero != NULL))
    {
        m_CurHeroPosition.x = (Hero->PositionX);
        m_CurHeroPosition.y = (Hero->PositionY);
    }

    UpdateButtonGeometry();
    StageTopMenu();

    return true;
}

float CNewUIHeroPositionInfo::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::TopMenu;
}

void CNewUIHeroPositionInfo::OpenningProcess()
{
}

void CNewUIHeroPositionInfo::ClosingProcess()
{
}

void CNewUIHeroPositionInfo::SetCurHeroPosition(int x, int y)
{
    m_CurHeroPosition.x = x;
    m_CurHeroPosition.y = y;
}

ButtonVisualState CNewUIHeroPositionInfo::ToTopMenuButtonState(BUTTON_STATE state) noexcept
{
    switch (state)
    {
    case BUTTON_STATE_OVER:
        return ButtonVisualState::Over;
    case BUTTON_STATE_DOWN:
        return ButtonVisualState::Down;
    default:
        return ButtonVisualState::Up;
    }
}

void CNewUIHeroPositionInfo::UpdateButtonGeometry()
{
    const UI::Modern::RmlTopMenuTransform transform = UI::Modern::CalculateRmlTopMenuTransform(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale());
    const auto setGeometry = [&](CNewUIButton &button, HeroPositionDetail::DesignKey key) {
        const auto box = Design().Values(key);
        const UI::Modern::RmlTopMenuRect rect = UI::Modern::CalculateRmlTopMenuReferenceRect(
            transform, ModernUiViewportWidth(), ModernUiViewportHeight(), box[0], box[1], box[2],
            box[3]);
        button.ChangeButtonInfo(static_cast<int>(std::lround(rect.x)),
                                static_cast<int>(std::lround(rect.y)),
                                std::max(1, static_cast<int>(std::lround(rect.width))),
                                std::max(1, static_cast<int>(std::lround(rect.height))));
    };
    setGeometry(m_BtnConfig, HeroPositionDetail::DesignKey::OptionRect);
    setGeometry(m_BtnStart, HeroPositionDetail::DesignKey::ActionRect);
    setGeometry(m_BtnStop, HeroPositionDetail::DesignKey::ActionRect);
}

void SessionUiUnit::MovePetCommand(CHARACTER *c)
{
    if (!ResolvePetSystem(c))
    {
        return;
    }

    constexpr int kCommandIconWidth = 32;
    constexpr int kCommandIconHeight = 36;
    constexpr int kCommandBarY = 330;

    int skillCount = 0;
    for (int command = AT_PET_COMMAND_DEFAULT; command < AT_PET_COMMAND_END; ++command)
    {
        const int commandBarWidth =
            (AT_PET_COMMAND_END - AT_PET_COMMAND_DEFAULT) * kCommandIconWidth;
        const int x = 320 - commandBarWidth / 2 + skillCount * kCommandIconWidth;
        const int y = kCommandBarY;
        ++skillCount;

        if (MouseX >= x && MouseX < x + kCommandIconWidth && MouseY >= y &&
            MouseY < y + kCommandIconHeight)
        {
            CheckSkill = command;
            CheckX = x + kCommandIconWidth / 2;
            CheckY = y;
            MouseOnWindow = true;
            if (MouseLButtonPush)
            {
                MouseLButtonPush = false;
                Hero->CurrentSkill = command;
                SkillEnable = false;
                PlayBuffer(SOUND_CLICK01);
                MouseUpdateTime = 0;
                MouseUpdateTimeMax = 6;
            }
        }
    }
}

bool SessionUiUnit::RequestPetInfo(int sx, int sy, ITEM *pItem, bool periodic)
{
    const auto now = std::chrono::steady_clock::now();
    constexpr auto RefreshInterval = std::chrono::seconds(1);
    if (periodic && petInfoRequestTime_ != std::chrono::steady_clock::time_point{} &&
        now - petInfoRequestTime_ < RefreshInterval)
        return false;
    const auto itemIndex = PetManagerDetail::ComposeItemIndex(sx, sy);
    if (periodic || gs_PetInfo.m_dwPetType == PET_TYPE_NONE ||
        g_renderItemIndexBackup != itemIndex || g_renderItemInfoBackup.Type != pItem->Type ||
        g_renderItemInfoBackup.Level != pItem->Level)
    {
        g_renderItemIndexBackup = itemIndex;
        g_renderItemInfoBackup.Type = pItem->Type;
        g_renderItemInfoBackup.Level = pItem->Level;

        std::uint8_t PetType = PET_TYPE_DARK_SPIRIT;
        if (pItem->Type == ITEM_DARK_HORSE_ITEM)
        {
            PetType = PET_TYPE_DARK_HORSE;
        }

        StorageType iInvenType = StorageType::Inventory;
        int iItemIndex = 0;

        if ((iItemIndex = g_pMyInventory->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::Inventory;
        }
        else if ((iItemIndex = g_pMyShopInventory->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::Inventory;
        }
        else if ((iItemIndex = g_pStorageInventory->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::Vault;
        }
        else if ((iItemIndex = g_pStorageInventoryExt->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::Vault;
        }
        else if ((iItemIndex = g_pTrade->GetPointedItemIndexMyInven()) != -1)
        {
            iInvenType = StorageType::TradeOwn;
        }
        else if ((iItemIndex = g_pTrade->GetPointedItemIndexYourInven()) != -1)
        {
            iInvenType = StorageType::TradeOther;
        }
        else if ((iItemIndex = g_pMixInventory->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::Crafting;
        }
        else if ((iItemIndex = g_pPurchaseShopInventory->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::PersonalShop;
        }
        else if ((iItemIndex = g_pNPCShop->GetPointedItemIndex()) != -1)
        {
            iInvenType = StorageType::NpcShop;
        }

        SocketClient->ToGameServer()->SendPetInfoRequest(static_cast<::PetType>(PetType),
                                                         iInvenType, iItemIndex);
        petInfoRequestTime_ = now;
        return true;
    }
    return false;
}

// OMF-00827
// OMF-00834

void SessionUiUnit::FormatPetSpecialOptions(const ITEM &item, const PET_INFO &pet, int &textNum,
                                            int &skipNum)
{
    const auto appendOption = [&](int option, int value) {
        if (textNum >= PetManagerDetail::kTooltipLineLimit)
            return;
        GetSpecialOptionText(item.Type, TextList[textNum], option, value, 0);
        TextListColor[textNum] = TEXT_COLOR_BLUE;
        TextBold[textNum] = false;
        ++textNum;
        ++skipNum;
    };
    bool hasDefense = false;
    for (int i = 0; i < item.SpecialNum; ++i)
    {
        const bool defense =
            item.Type == ITEM_DARK_HORSE_ITEM && item.Special[i] == AT_SET_OPTION_IMPROVE_DEFENCE;
        hasDefense |= defense;
        appendOption(item.Special[i], defense ? sessionKeeper_.Gameplay()->GetPetDefenseBonus(pet)
                                              : item.SpecialValue[i]);
    }
    if (item.Type == ITEM_DARK_HORSE_ITEM && !hasDefense)
        appendOption(AT_SET_OPTION_IMPROVE_DEFENCE,
                     sessionKeeper_.Gameplay()->GetPetDefenseBonus(pet));
}

namespace MainFrameDetail
{

const UI::Modern::RmlUiDesign &MainFrameDesign()
{
    static const UI::Modern::RmlUiDesign design(
        "Data/UI/PC/HUD/main_frame.rml", {"NewUIMainFrameWindow-MainFrameSkillIconWidth",
                                          "NewUIMainFrameWindow-MainFrameSkillIconHeight",
                                          "NewUIMainFrameWindow-MainFrameCurrentSkillX",
                                          "NewUIMainFrameWindow-MainFrameSkillY",
                                          "NewUIMainFrameWindow-MainFrameHotKeyFirstX",
                                          "NewUIMainFrameWindow-MainFrameHotKeyStep",
                                          "NewUIMainFrameWindow-MainFrameSkillPageButtonX",
                                          "NewUIMainFrameWindow-MainFrameSkillPageButtonY",
                                          "NewUIMainFrameWindow-MainFrameSkillPageButtonWidth",
                                          "NewUIMainFrameWindow-MainFrameSkillPageButtonHeight",
                                          "NewUIMainFrameWindow-MainFrameItemFirstX",
                                          "NewUIMainFrameWindow-MainFrameItemY",
                                          "NewUIMainFrameWindow-MainFrameItemStep",
                                          "NewUIMainFrameWindow-MainFrameItemWidth",
                                          "NewUIMainFrameWindow-MainFrameItemHeight",
                                          "NewUIMainFrameWindow-MainFrameItemCountImageX",
                                          "NewUIMainFrameWindow-MainFrameItemCountImageY",
                                          "NewUIMainFrameWindow-MainFrameItemCountScaleX",
                                          "NewUIMainFrameWindow-MainFrameItemCountScaleY",
                                          "NewUIMainFrameWindow-MainFrameItemCountDigitStep",
                                          "NewUIMainFrameWindow-MainFrameItemCountDigitWidth",
                                          "NewUIMainFrameWindow-MainFrameItemCountDigitHeight",
                                          "NewUIMainFrameWindow-MainFrameItemCountDigitLimit",
                                          "NewUIMainFrameWindow-SkillListSlotWidth",
                                          "NewUIMainFrameWindow-SkillListSlotHeight",
                                          "NewUIMainFrameWindow-SkillIconSourceWidth",
                                          "NewUIMainFrameWindow-SkillIconSourceHeight",
                                          "NewUIMainFrameWindow-MainFrameButtonY",
                                          "NewUIMainFrameWindow-MainFrameButtonWidth",
                                          "NewUIMainFrameWindow-MainFrameButtonHeight",
                                          "NewUIMainFrameWindow-MainFrameButtonX",
                                          "NewUIMainFrameWindow-SkillListIconOffset",
                                          "NewUIMainFrameWindow-ExperienceEffectDuration",
                                          "NewUIMainFrameWindow-NumberAtlasWidth",
                                          "NewUIMainFrameWindow-NumberAtlasHeight",
                                          "NewUIMainFrameWindow-SkillAtlasSize",
                                          "NewUIMainFrameWindow-MasterSkillAtlasSize",
                                          "NewUIMainFrameWindow-HotKeyNumberColor",
                                          "NewUIMainFrameWindow-HotKeyNumberBaseScale",
                                          "NewUIMainFrameWindow-HotKeyNumberWidthScale",
                                          "NewUIMainFrameWindow-HotKeyNumberOffsetY",
                                          "NewUIMainFrameWindow-CooldownColor",
                                          "NewUIMainFrameWindow-SkillListCenteredCount",
                                          "NewUIMainFrameWindow-SkillListFirstRowCount",
                                          "NewUIMainFrameWindow-SkillListLeftRunStart",
                                          "NewUIMainFrameWindow-SkillListSecondRowStart"});
    return design;
}

UI::Modern::RmlMainFrameRect MainFrameReferenceRect(int viewportWidth, int viewportHeight,
                                                    float maximumScale, float x, float y,
                                                    float width, float height) noexcept
{
    const auto transform =
        UI::Modern::CalculateRmlMainFrameTransform(viewportWidth, viewportHeight, maximumScale);
    return UI::Modern::CalculateRmlMainFrameReferenceRect(transform, viewportWidth, viewportHeight,
                                                          x, y, width, height);
}

UI::Modern::RmlMainFrameRect RoundedMainFrameReferenceRect(int viewportWidth, int viewportHeight,
                                                           float maximumScale, float x, float y,
                                                           float width, float height) noexcept
{
    const auto rect =
        MainFrameReferenceRect(viewportWidth, viewportHeight, maximumScale, x, y, width, height);
    const float left = std::round(rect.x);
    const float top = std::round(rect.y);
    return {left, top, std::round(rect.x + rect.width) - left,
            std::round(rect.y + rect.height) - top};
}

UI::Modern::RmlMainFrameRect CalculateSkillListSlot(
    const MainFrameDetail::SkillListGeometry &geometry, int index)
{
    const int centeredCount =
        MainFrameDesign().Number<int>(MainFrameDetail::MainFrameDesignKey::SkillListCenteredCount);
    const int firstRowCount =
        MainFrameDesign().Number<int>(MainFrameDetail::MainFrameDesignKey::SkillListFirstRowCount);
    float column;
    if (index < centeredCount)
        column = index % 2 == 0 ? index / 2 : -(index / 2 + 1);
    else if (index < firstRowCount)
        column =
            MainFrameDesign().Number(MainFrameDetail::MainFrameDesignKey::SkillListLeftRunStart) -
            (index - centeredCount);
    else
        column =
            MainFrameDesign().Number(MainFrameDetail::MainFrameDesignKey::SkillListSecondRowStart) +
            (index - firstRowCount);
    return {geometry.originX + column * geometry.slotWidth,
            geometry.originY - (index >= firstRowCount ? geometry.slotHeight : 0.0F),
            geometry.slotWidth, geometry.slotHeight};
}

MainFrameDetail::SkillListGeometry CalculateSkillListGeometry(int viewportWidth, int viewportHeight,
                                                              float maximumScale) noexcept
{
    const auto frame =
        MainFrameReferenceRect(viewportWidth, viewportHeight, maximumScale, 0.0F, 0.0F, 0.0F, 0.0F);
    const auto currentSkill = MainFrameReferenceRect(
        viewportWidth, viewportHeight, maximumScale,
        MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameCurrentSkillX),
        MainFrameDesign().Number<float>(MainFrameDetail::MainFrameDesignKey::MainFrameSkillY),
        MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconWidth),
        MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconHeight));
    const auto slot = MainFrameReferenceRect(
        viewportWidth, viewportHeight, maximumScale, 0.0F, 0.0F,
        MainFrameDesign().Number<float>(MainFrameDetail::MainFrameDesignKey::SkillListSlotWidth),
        MainFrameDesign().Number<float>(MainFrameDetail::MainFrameDesignKey::SkillListSlotHeight));
    const auto icon = MainFrameReferenceRect(
        viewportWidth, viewportHeight, maximumScale, 0.0F, 0.0F,
        MainFrameDesign().Number<float>(MainFrameDetail::MainFrameDesignKey::SkillIconSourceWidth),
        MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::SkillIconSourceHeight));
    const auto iconOffset = MainFrameReferenceRect(
        viewportWidth, viewportHeight, maximumScale, 0.0F, 0.0F,
        MainFrameDesign().Number(MainFrameDetail::MainFrameDesignKey::SkillListIconOffset),
        MainFrameDesign().Number(MainFrameDetail::MainFrameDesignKey::SkillListIconOffset));
    const float originY = frame.y - slot.height;
    return {currentSkill.x,
            originY,
            currentSkill.x - slot.width,
            originY - slot.height,
            slot.width,
            slot.height,
            iconOffset.width,
            iconOffset.height,
            icon.width,
            icon.height};
}
} // namespace MainFrameDetail

SEASON3B::CNewUIMainFrameWindow::CNewUIMainFrameWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), g_pFriendMenu(keeper.FriendMenuObject()),
      g_InGameShopSystem(keeper.InGameShopSystemObject()), renderer_(RendererForConstruction()),
      m_ItemHotKey(keeper), m_BtnCShop(keeper), m_BtnChaInfo(keeper), m_BtnMyInven(keeper),
      m_BtnQuest(keeper), m_BtnFriend(keeper), m_BtnWindow(keeper)
{
    m_bExpEffect = false;
    m_dwExpEffectTime = 0;
    m_dwPreExp = 0;
    m_dwGetExp = 0;
    m_bButtonBlink = false;
}

SEASON3B::CNewUIMainFrameWindow::~CNewUIMainFrameWindow()
{
    Release();
}

bool SEASON3B::CNewUIMainFrameWindow::Create(CNewUIManager *pNewUIMng,
                                             CNewUI3DRenderMng *pNewUI3DRenderMng)
{
    if (NULL == pNewUIMng || NULL == pNewUI3DRenderMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_MAINFRAME, this);

    m_pNewUI3DRenderMng = pNewUI3DRenderMng;
    m_pNewUI3DRenderMng->Add3DRenderObj(this, ITEMHOTKEYNUMBER_CAMERA_Z_ORDER);

    SetButtonInfo();

    Show(true);

    return true;
}

void SEASON3B::CNewUIMainFrameWindow::UpdateButtonGeometry()
{
    const std::array<CNewUIButton *, UI::Modern::RmlMainFrameRequest::ButtonCount> buttons{
        &m_BtnCShop, &m_BtnChaInfo, &m_BtnMyInven, &m_BtnQuest, &m_BtnFriend, &m_BtnWindow};
    for (std::size_t index = 0; index < buttons.size(); ++index)
    {
        const auto rect = MainFrameDetail::RoundedMainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Values(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonX)[index],
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameButtonHeight));
        buttons[index]->ChangeButtonInfo(static_cast<int>(rect.x), static_cast<int>(rect.y),
                                         static_cast<int>(rect.width),
                                         static_cast<int>(rect.height));
    }
}

void SEASON3B::CNewUIMainFrameWindow::Release()
{
    if (m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->Remove3DRenderObj(this);
        m_pNewUI3DRenderMng = NULL;
    }

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool SEASON3B::CNewUIMainFrameWindow::IsVisible() const
{
    return CNewUIObj::IsVisible();
}

bool SEASON3B::CNewUIMainFrameWindow::UpdateMouseEvent()
{
    UpdateButtonGeometry();
    m_ItemHotKey.UpdateMouseEvent();

    if (g_pNewUIHotKey->IsStateGameOver() == true)
    {
        return true;
    }

    if (BtnProcess() == true)
    {
        return false;
    }

    return true;
}

bool SEASON3B::CNewUIMainFrameWindow::BtnProcess()
{
    if (g_pNewUIHotKey->CanUpdateKeyEventRelatedMyInventory() == true)
    {
        if (m_BtnMyInven.UpdateMouseEvent() == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
            return true;
        }
    }
    else if (g_pNewUIHotKey->CanUpdateKeyEvent() == true)
    {
        if (m_BtnMyInven.UpdateMouseEvent() == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_INVENTORY);
            PlayBuffer(SOUND_CLICK01);
            return true;
        }
        else if (m_BtnChaInfo.UpdateMouseEvent() == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_CHARACTER);

            PlayBuffer(SOUND_CLICK01);

            if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_CHARACTER))
            {
                std::uint16_t questNumber = 0;
                std::uint16_t questGroup = 0;
                if (g_QuestMng.GetQuestIndexByEtcSelection(questNumber, questGroup))
                {
                    SocketClient->ToGameServer()->SendQuestSelectRequest(questNumber, questGroup,
                                                                         0);
                }
            }

            return true;
        }
        else if (m_BtnFriend.UpdateMouseEvent() == true)
        {
            if (gMapManager.InChaosCastle() == true)
            {
                PlayBuffer(SOUND_CLICK01);
                return true;
            }

            int iLevel = CharacterAttribute->Level;

            if (iLevel < 6)
            {
                if (g_pSystemLogBox->CheckChatRedundancy(
                        I18N::Game::YouMustBeAtLeastLevel6ToUseTheMyFriendFunction) == FALSE)
                {
                    g_pSystemLogBox->AddText(
                        I18N::Game::YouMustBeAtLeastLevel6ToUseTheMyFriendFunction,
                        SEASON3B::TYPE_SYSTEM_MESSAGE);
                }
            }
            else
            {
                g_pNewUISystem->Toggle(SEASON3B::INTERFACE_FRIEND);
            }
            PlayBuffer(SOUND_CLICK01);
            return true;
        }
        else if (m_BtnQuest.UpdateMouseEvent() == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_MYQUEST);
            PlayBuffer(SOUND_CLICK01);
            return true;
        }
        else if (m_BtnWindow.UpdateMouseEvent() == true)
        {
            g_pNewUISystem->Toggle(SEASON3B::INTERFACE_WINDOW_MENU);
            PlayBuffer(SOUND_CLICK01);
            return true;
        }

#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
        else if (m_BtnCShop.UpdateMouseEvent() == true)
        {
            if (g_pInGameShop->IsInGameShopOpen() == false)
                return false;

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
            if (g_InGameShopSystem.IsScriptDownload() == true)
            {
                if (g_InGameShopSystem.ScriptDownload() == false)
                    return false;
            }

            if (g_InGameShopSystem.IsBannerDownload() == true)
            {
                g_InGameShopSystem.BannerDownload();
            }
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD

            if (g_pNewUISystem->IsVisible(SEASON3B::INTERFACE_INGAMESHOP) == false)
            {
                if (g_InGameShopSystem.GetIsRequestShopOpenning() == false)
                {
                    SocketClient->ToGameServer()->SendCashShopOpenState(0);
                    g_InGameShopSystem.SetIsRequestShopOpenning(true);

#ifdef KJH_MOD_SHOP_SCRIPT_DOWNLOAD
                    g_pMainFrame->SetBtnState(MAINFRAME_BTN_PARTCHARGE, true);
#endif // KJH_MOD_SHOP_SCRIPT_DOWNLOAD
                }
            }
            else
            {
                SocketClient->ToGameServer()->SendCashShopOpenState(1);
                g_pNewUISystem->Hide(SEASON3B::INTERFACE_INGAMESHOP);
            }

            return true;
        }
#endif //PBG_ADD_INGAMESHOP_UI_MAINFRAME
    }

    return false;
}

bool SEASON3B::CNewUIMainFrameWindow::UpdateKeyEvent()
{
    if (m_ItemHotKey.UpdateKeyEvent() == false)
    {
        return false;
    }
    return true;
}

bool SEASON3B::CNewUIMainFrameWindow::Update()
{
    if (m_bExpEffect == true)
    {
        if (timeGetTime() - m_dwExpEffectTime >
            MainFrameDetail::MainFrameDesign().Number(
                MainFrameDetail::MainFrameDesignKey::ExperienceEffectDuration))
        {
            m_bExpEffect = false;
            m_dwExpEffectTime = 0;
            m_dwGetExp = 0;
        }
    }

    StageMainFrame();

    return true;
}

void SEASON3B::CNewUIMainFrameWindow::FillExperienceRequest(
    UI::Modern::RmlMainFrameRequest &request) const
{
    const bool master = gCharacterManager.IsMasterExperienceActive(CharacterAttribute->Class,
                                                                   CharacterAttribute->Level);
    const __int64 level = master ? static_cast<__int64>(Master_Level_Data.nMLevel)
                                 : static_cast<__int64>(CharacterAttribute->Level);
    request.currentExperience =
        master ? Master_Level_Data.lMasterLevel_Experince : CharacterAttribute->Experience;
    request.nextExperience =
        master ? Master_Level_Data.lNext_MasterLevel_Experince : CharacterAttribute->NextExperience;
    request.experienceStyle = master ? UI::Modern::RmlMainFrameExperienceStyle::Master
                                     : UI::Modern::RmlMainFrameExperienceStyle::Normal;

    __int64 lowerBound = 0;
    if (master)
    {
        const __int64 totalLevel = level + 400;
        const __int64 overLevel = totalLevel - 255;
        const __int64 total = (9 + totalLevel) * totalLevel * totalLevel * 10 +
                              (9 + overLevel) * overLevel * overLevel * 1000;
        lowerBound = (total - 3892250000LL) / 2;
    }
    else if (level > 1)
    {
        const __int64 priorLevel = level - 1;
        lowerBound = (9 + priorLevel) * priorLevel * priorLevel * 10;
        if (priorLevel > 255)
        {
            const __int64 overLevel = priorLevel - 255;
            lowerBound += (9 + overLevel) * overLevel * overLevel * 1000;
        }
    }

    const __int64 upperBound = std::max(lowerBound, request.nextExperience);
    const double range = static_cast<double>(upperBound - lowerBound);
    const double current =
        std::clamp(static_cast<double>(request.currentExperience), static_cast<double>(lowerBound),
                   static_cast<double>(upperBound));
    const double ratio = range > 0.0 ? std::clamp((current - lowerBound) / range, 0.0, 1.0) : 0.0;
    if (ratio >= 1.0)
    {
        request.experiencePage = 9;
        request.experienceRatio = 1.0F;
        return;
    }
    const double segment = ratio * 10.0;
    request.experiencePage = std::clamp(static_cast<int>(segment), 0, 9);
    request.experienceRatio = static_cast<float>(segment - std::floor(segment));
}

float SEASON3B::CNewUIMainFrameWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::BottomHud;
}

float SEASON3B::CNewUIMainFrameWindow::GetKeyEventOrder()
{
    return 2.9f;
}

void SEASON3B::CNewUIMainFrameWindow::SetItemHotKey(int iHotKey, int iItemType, int iItemLevel)
{
    m_ItemHotKey.SetHotKey(iHotKey, iItemType, iItemLevel);
}

int SEASON3B::CNewUIMainFrameWindow::GetItemHotKey(int iHotKey)
{
    return m_ItemHotKey.GetHotKey(iHotKey);
}

int SEASON3B::CNewUIMainFrameWindow::GetItemHotKeyLevel(int iHotKey)
{
    return m_ItemHotKey.GetHotKeyLevel(iHotKey);
}

void SEASON3B::CNewUIMainFrameWindow::UseHotKeyItemRButton()
{
    m_ItemHotKey.UseItemRButton();
}

void SEASON3B::CNewUIMainFrameWindow::UpdateItemHotKey()
{
    m_ItemHotKey.UpdateKeyEvent();
}

void SEASON3B::CNewUIMainFrameWindow::ResetSkillHotKey()
{
    g_pSkillList->Reset();
}

void SEASON3B::CNewUIMainFrameWindow::SetSkillHotKey(int iHotKey, int iSkillType)
{
    g_pSkillList->SetHotKey(iHotKey, iSkillType);
}

int SEASON3B::CNewUIMainFrameWindow::GetSkillHotKey(int iHotKey)
{
    return g_pSkillList->GetHotKey(iHotKey);
}

int SEASON3B::CNewUIMainFrameWindow::GetSkillHotKeyIndex(int iSkillType)
{
    return sessionKeeper_.SkillManagerObject().GetSkillIndex(iSkillType);
}

SEASON3B::CNewUIItemHotKey::CNewUIItemHotKey(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), m_iTooltipHotKey(-1)
{
    for (int i = 0; i < HOTKEY_COUNT; ++i)
    {
        m_iHotKeyItemType[i] = -1;
        m_iHotKeyItemLevel[i] = 0;
    }
}

SEASON3B::CNewUIItemHotKey::~CNewUIItemHotKey()
{
}

bool SEASON3B::CNewUIItemHotKey::UpdateKeyEvent()
{
    int iIndex = -1;

    if (IsPress('Q') == true)
    {
        iIndex = GetHotKeyItemIndex(HOTKEY_Q);
    }
    else if (IsPress('W') == true)
    {
        iIndex = GetHotKeyItemIndex(HOTKEY_W);
    }
    else if (IsPress('E') == true)
    {
        iIndex = GetHotKeyItemIndex(HOTKEY_E);
    }
    else if (IsPress('R') == true)
    {
        iIndex = GetHotKeyItemIndex(HOTKEY_R);
    }

    if (iIndex != -1)
    {
        ITEM *pItem = NULL;
        pItem = sessionKeeper_.GameData()->FindInventoryItemBySlot(iIndex);
        if ((pItem->Type >= ITEM_POTION + 78 && pItem->Type <= ITEM_POTION + 82))
        {
            std::list<eBuffState> secretPotionbufflist;
            secretPotionbufflist.push_back(eBuff_SecretPotion1);
            secretPotionbufflist.push_back(eBuff_SecretPotion2);
            secretPotionbufflist.push_back(eBuff_SecretPotion3);
            secretPotionbufflist.push_back(eBuff_SecretPotion4);
            secretPotionbufflist.push_back(eBuff_SecretPotion5);

            if (g_isCharacterBufflist((&Hero->Object), secretPotionbufflist) != eBuffNone)
            {
                CreateOkMessageBox(
                    I18N::Game::YouCannotUseThisItemWhileThePotionEffectsRemainActive,
                    RGBA(255, 30, 0, 255));
            }
            else
            {
                SendRequestUse(iIndex, 0);
            }
        }
        else

        {
            SendRequestUse(iIndex, 0);
        }
        return false;
    }

    return true;
}

void SEASON3B::CNewUIItemHotKey::UpdateMouseEvent()
{
    m_iTooltipHotKey = -1;
    for (int i = 0; i < HOTKEY_COUNT; ++i)
    {
        const auto rect = MainFrameDetail::MainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemFirstX) +
                i * MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemStep),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemHeight));
        if (CheckMouseIn(rect.x, rect.y, rect.width, rect.height) && GetHotKeyItemIndex(i) != -1)
        {
            m_iTooltipHotKey = i;
            return;
        }
    }
}

int SEASON3B::CNewUIItemHotKey::GetHotKeyItemIndex(int iType, bool bItemCount)
{
    int iStartItemType = 0, iEndItemType = 0;

    switch (iType)
    {
    case HOTKEY_Q:
        if (GetHotKeyCommonItem(iType, iStartItemType, iEndItemType) == false)
        {
            if (m_iHotKeyItemType[iType] >= ITEM_SMALL_MANA_POTION &&
                m_iHotKeyItemType[iType] <= ITEM_LARGE_MANA_POTION)
            {
                iStartItemType = ITEM_LARGE_MANA_POTION;
                iEndItemType = ITEM_SMALL_MANA_POTION;
            }
            else
            {
                iStartItemType = ITEM_LARGE_HEALING_POTION;
                iEndItemType = ITEM_APPLE;
            }
        }
        break;
    case HOTKEY_W:
        if (GetHotKeyCommonItem(iType, iStartItemType, iEndItemType) == false)
        {
            if (m_iHotKeyItemType[iType] >= ITEM_APPLE &&
                m_iHotKeyItemType[iType] <= ITEM_LARGE_HEALING_POTION)
            {
                iStartItemType = ITEM_LARGE_HEALING_POTION;
                iEndItemType = ITEM_APPLE;
            }
            else
            {
                iStartItemType = ITEM_LARGE_MANA_POTION;
                iEndItemType = ITEM_SMALL_MANA_POTION;
            }
        }
        break;
    case HOTKEY_E:
        if (GetHotKeyCommonItem(iType, iStartItemType, iEndItemType) == false)
        {
            if (m_iHotKeyItemType[iType] >= ITEM_APPLE &&
                m_iHotKeyItemType[iType] <= ITEM_LARGE_HEALING_POTION)
            {
                iStartItemType = ITEM_LARGE_HEALING_POTION;
                iEndItemType = ITEM_APPLE;
            }
            else if (m_iHotKeyItemType[iType] >= ITEM_SMALL_MANA_POTION &&
                     m_iHotKeyItemType[iType] <= ITEM_LARGE_MANA_POTION)
            {
                iStartItemType = ITEM_LARGE_MANA_POTION;
                iEndItemType = ITEM_SMALL_MANA_POTION;
            }
            else
            {
                iStartItemType = ITEM_ANTIDOTE;
                iEndItemType = ITEM_ANTIDOTE;
            }
        }
        break;
    case HOTKEY_R:
        if (GetHotKeyCommonItem(iType, iStartItemType, iEndItemType) == false)
        {
            if (m_iHotKeyItemType[iType] >= ITEM_APPLE &&
                m_iHotKeyItemType[iType] <= ITEM_LARGE_HEALING_POTION)
            {
                iStartItemType = ITEM_LARGE_HEALING_POTION;
                iEndItemType = ITEM_APPLE;
            }
            else if (m_iHotKeyItemType[iType] >= ITEM_SMALL_MANA_POTION &&
                     m_iHotKeyItemType[iType] <= ITEM_LARGE_MANA_POTION)
            {
                iStartItemType = ITEM_LARGE_MANA_POTION;
                iEndItemType = ITEM_SMALL_MANA_POTION;
            }
            else
            {
                iStartItemType = ITEM_LARGE_SHIELD_POTION;
                iEndItemType = ITEM_SMALL_SHIELD_POTION;
            }
        }
        break;
    }

    CNewUIInventoryCtrl *const inventory = g_pMyInventory->GetInventoryCtrl();
    const int itemCount = static_cast<int>(inventory->GetNumberOfItems());
    int total = 0;
    ITEM *selected = nullptr;
    for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex)
    {
        ITEM *const item = inventory->GetItem(itemIndex);
        if (item == nullptr || item->Type < iEndItemType || item->Type > iStartItemType)
        {
            continue;
        }
        const bool ignoresLevel =
            item->Type >= ITEM_APPLE && item->Type <= ITEM_LARGE_HEALING_POTION;
        if (!ignoresLevel && item->Level != m_iHotKeyItemLevel[iType])
        {
            continue;
        }

        if (bItemCount)
        {
            total += item->Type == ITEM_ALE || item->Type == ITEM_TOWN_PORTAL_SCROLL ||
                             item->Type == ITEM_POTION + 20
                         ? 1
                         : item->Durability;
            continue;
        }

        if (selected == nullptr || item->Type > selected->Type ||
            (item->Type == selected->Type &&
             (item->x > selected->x || (item->x == selected->x && item->y > selected->y))))
        {
            selected = item;
        }
    }

    if (bItemCount)
    {
        return total;
    }
    return inventory->GetIndexByItem(selected);
}

bool SEASON3B::CNewUIItemHotKey::GetHotKeyCommonItem(IN int iHotKey, OUT int &iStart, OUT int &iEnd)
{
    switch (m_iHotKeyItemType[iHotKey])
    {
    case ITEM_SIEGE_POTION:
    case ITEM_ANTIDOTE:
    case ITEM_ALE:
    case ITEM_TOWN_PORTAL_SCROLL:
    case ITEM_POTION + 20:
    case ITEM_JACK_OLANTERN_BLESSINGS:
    case ITEM_JACK_OLANTERN_WRATH:
    case ITEM_JACK_OLANTERN_CRY:
    case ITEM_JACK_OLANTERN_FOOD:
    case ITEM_JACK_OLANTERN_DRINK:
    case ITEM_POTION + 70:
    case ITEM_POTION + 71:
    case ITEM_POTION + 78:
    case ITEM_POTION + 79:
    case ITEM_POTION + 80:
    case ITEM_POTION + 81:
    case ITEM_POTION + 82:
    case ITEM_POTION + 94:
    case ITEM_CHERRY_BLOSSOM_WINE:
    case ITEM_CHERRY_BLOSSOM_RICE_CAKE:
    case ITEM_CHERRY_BLOSSOM_FLOWER_PETAL:
    case ITEM_POTION + 133:
        if (m_iHotKeyItemType[iHotKey] != ITEM_POTION + 20 || m_iHotKeyItemLevel[iHotKey] == 0)
        {
            iStart = iEnd = m_iHotKeyItemType[iHotKey];
            return true;
        }
        break;
    default:
        if (m_iHotKeyItemType[iHotKey] >= ITEM_SMALL_SHIELD_POTION &&
            m_iHotKeyItemType[iHotKey] <= ITEM_LARGE_SHIELD_POTION)
        {
            iStart = ITEM_LARGE_SHIELD_POTION;
            iEnd = ITEM_SMALL_SHIELD_POTION;
            return true;
        }
        else if (m_iHotKeyItemType[iHotKey] >= ITEM_SMALL_COMPLEX_POTION &&
                 m_iHotKeyItemType[iHotKey] <= ITEM_LARGE_COMPLEX_POTION)
        {
            iStart = ITEM_LARGE_COMPLEX_POTION;
            iEnd = ITEM_SMALL_COMPLEX_POTION;
            return true;
        }
        break;
    }
    return false;
}

int SEASON3B::CNewUIItemHotKey::GetHotKeyItemCount(int iType)
{
    return 0;
}

void SEASON3B::CNewUIItemHotKey::SetHotKey(int iHotKey, int iItemType, int iItemLevel)
{
    if (iHotKey != -1 && CNewUIMyInventory::CanRegisterItemHotKey(iItemType) == true)
    {
        m_iHotKeyItemType[iHotKey] = iItemType;
        m_iHotKeyItemLevel[iHotKey] = iItemLevel;
    }
    else
    {
        m_iHotKeyItemType[iHotKey] = -1;
        m_iHotKeyItemLevel[iHotKey] = 0;
    }
}

int SEASON3B::CNewUIItemHotKey::GetHotKey(int iHotKey)
{
    if (iHotKey != -1)
    {
        return m_iHotKeyItemType[iHotKey];
    }

    return -1;
}

int SEASON3B::CNewUIItemHotKey::GetHotKeyLevel(int iHotKey)
{
    if (iHotKey != -1)
    {
        return m_iHotKeyItemLevel[iHotKey];
    }

    return 0;
}

void SEASON3B::CNewUIItemHotKey::UseItemRButton()
{
    for (int i = 0; i < HOTKEY_COUNT; ++i)
    {
        const auto rect = MainFrameDetail::MainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemFirstX) +
                i * MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameItemStep),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameItemHeight));
        if (CheckMouseIn(rect.x, rect.y, rect.width, rect.height) == true)
        {
            if (MouseRButtonPush)
            {
                MouseRButtonPush = false;
                int iIndex = GetHotKeyItemIndex(i);
                if (iIndex != -1)
                {
                    SendRequestUse(iIndex, 0);
                    break;
                }
            }
        }
    }
}

SEASON3B::CNewUISkillList::CNewUISkillList(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), skillImages_(keeper),
      skillRenderer_(RendererForConstruction()), gSkillManager(keeper.SkillManagerObject()),
      gameplay_(GameplayForConstruction()), g_CMonkSystem(keeper.MonkSystemObject()),
      m_SkillTooltip(keeper)
{
    m_pNewUIMng = NULL;
    skillEntries_.reserve(MAX_MAGIC + AT_PET_COMMAND_END - AT_PET_COMMAND_DEFAULT);
    Reset();
}

SEASON3B::CNewUISkillList::~CNewUISkillList()
{
    Release();
}

bool SEASON3B::CNewUISkillList::Create(CNewUIManager *pNewUIMng,
                                       CNewUI3DRenderMng *pNewUI3DRenderMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_SKILL_LIST, this);

    m_pNewUI3DRenderMng = pNewUI3DRenderMng;

    LoadImages();

    Show(true);

    return true;
}

void SEASON3B::CNewUISkillList::Release()
{
    skillImages_.Release();
    skillEntries_.clear();
    if (m_pNewUI3DRenderMng)
    {
        m_pNewUI3DRenderMng->DeleteUI2DEffectObject(UI2DEffectCallback);
    }

    UnloadImages();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

void SEASON3B::CNewUISkillList::Reset()
{
    m_bSkillList = false;
    m_bHotKeySkillListUp = false;

    m_bRenderSkillInfo = false;
    m_iRenderSkillInfoType = 0;
    m_iRenderSkillInfoPosX = 0;
    m_iRenderSkillInfoPosY = 0;

    for (int i = 0; i < SKILLHOTKEY_COUNT; ++i)
    {
        m_iHotKeySkillType[i] = -1;
    }

    m_EventState = EVENT_NONE;
    m_iLastPageSkill = -1;
}

bool SEASON3B::CNewUISkillList::UpdateMouseEvent()
{
#ifdef MOD_SKILLLIST_UPDATEMOUSE_BLOCK
    if (GFxProcess::GetInstancePtr()->GetUISelect() == 1)
    {
        return true;
    }
#endif //MOD_SKILLLIST_UPDATEMOUSE_BLOCK

    if (g_isCharacterBuff((&Hero->Object), eBuff_DuelWatch))
    {
        m_bSkillList = false;
        return true;
    }

    BYTE bySkillNumber = CharacterAttribute->SkillNumber;
    BYTE bySkillMasterNumber = CharacterAttribute->SkillMasterNumber;

    float x, y, width, height;

    m_bRenderSkillInfo = false;

    if (bySkillNumber <= 0)
    {
        return true;
    }

    const auto pageButtonRect = MainFrameDetail::MainFrameReferenceRect(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonX),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonY),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonWidth),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillPageButtonHeight));
    const bool pageButtonHovered = CheckMouseIn(pageButtonRect.x, pageButtonRect.y,
                                                pageButtonRect.width, pageButtonRect.height);
    if (pageButtonHovered)
    {
        if (m_EventState == EVENT_NONE)
        {
            m_EventState = MouseLButtonPush ? EVENT_BTN_DOWN_SKILLPAGE : EVENT_BTN_HOVER_SKILLPAGE;
            return MouseLButtonPush == false;
        }
        if (m_EventState == EVENT_BTN_HOVER_SKILLPAGE && MouseLButtonPush)
        {
            m_EventState = EVENT_BTN_DOWN_SKILLPAGE;
            return false;
        }
        if (m_EventState == EVENT_BTN_DOWN_SKILLPAGE && MouseLButtonPush == false)
        {
            ToggleSkillHotKeyPage();
            m_EventState = EVENT_NONE;
            PlayBuffer(SOUND_CLICK01);
            return false;
        }
        if (m_EventState == EVENT_BTN_HOVER_SKILLPAGE || m_EventState == EVENT_BTN_DOWN_SKILLPAGE)
        {
            return false;
        }
    }
    else if (m_EventState == EVENT_BTN_HOVER_SKILLPAGE ||
             (m_EventState == EVENT_BTN_DOWN_SKILLPAGE && MouseLButtonPush == false))
    {
        m_EventState = EVENT_NONE;
        return true;
    }

    const auto currentSkillRect = MainFrameDetail::MainFrameReferenceRect(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameCurrentSkillX),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillY),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconWidth),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconHeight));
    x = currentSkillRect.x;
    y = currentSkillRect.y;
    width = currentSkillRect.width;
    height = currentSkillRect.height;
    if (m_EventState == EVENT_NONE && MouseLButtonPush == false &&
        CheckMouseIn(x, y, width, height) == true)
    {
        m_EventState = EVENT_BTN_HOVER_CURRENTSKILL;
        return true;
    }
    if (m_EventState == EVENT_BTN_HOVER_CURRENTSKILL && MouseLButtonPush == false &&
        CheckMouseIn(x, y, width, height) == false)
    {
        m_EventState = EVENT_NONE;
        return true;
    }
    if (m_EventState == EVENT_BTN_HOVER_CURRENTSKILL &&
        (MouseLButtonPush == true || MouseLButtonDBClick == true) &&
        CheckMouseIn(x, y, width, height) == true)
    {
        m_EventState = EVENT_BTN_DOWN_CURRENTSKILL;
        return false;
    }
    if (m_EventState == EVENT_BTN_DOWN_CURRENTSKILL)
    {
        if (MouseLButtonPush == false && MouseLButtonDBClick == false)
        {
            if (CheckMouseIn(x, y, width, height) == true)
            {
                m_bSkillList = !m_bSkillList;
                PlayBuffer(SOUND_CLICK01);
                m_EventState = EVENT_NONE;
                return false;
            }
            m_EventState = EVENT_NONE;
            return true;
        }
    }

    if (m_EventState == EVENT_BTN_HOVER_CURRENTSKILL)
    {
        SetSkillTooltip(Hero->CurrentSkill, x, y, width);

        return false;
    }
    else if (m_EventState == EVENT_BTN_DOWN_CURRENTSKILL)
    {
        return false;
    }

    const auto hotSkillStripRect = MainFrameDetail::MainFrameReferenceRect(
        ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameHotKeyFirstX),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillY),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameHotKeyStep) *
                4.0F +
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconWidth),
        MainFrameDetail::MainFrameDesign().Number<float>(
            MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconHeight));
    x = hotSkillStripRect.x;
    y = hotSkillStripRect.y;
    width = hotSkillStripRect.width;
    height = hotSkillStripRect.height;
    if (m_EventState == EVENT_NONE && MouseLButtonPush == false &&
        CheckMouseIn(x, y, width, height) == true)
    {
        m_EventState = EVENT_BTN_HOVER_SKILLHOTKEY;
        return true;
    }
    if (m_EventState == EVENT_BTN_HOVER_SKILLHOTKEY && MouseLButtonPush == false &&
        CheckMouseIn(x, y, width, height) == false)
    {
        m_EventState = EVENT_NONE;
        return true;
    }
    if (m_EventState == EVENT_BTN_HOVER_SKILLHOTKEY && MouseLButtonPush == true &&
        CheckMouseIn(x, y, width, height) == true)
    {
        m_EventState = EVENT_BTN_DOWN_SKILLHOTKEY;
        return false;
    }

    int iStartIndex = (m_bHotKeySkillListUp == true) ? 6 : 1;
    for (int i = 0, iIndex = iStartIndex; i < 5; ++i, iIndex++)
    {
        const auto hotSkillRect = MainFrameDetail::MainFrameReferenceRect(
            ModernUiViewportWidth(), ModernUiViewportHeight(), ModernUiScale(),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameHotKeyFirstX) +
                i * MainFrameDetail::MainFrameDesign().Number<float>(
                        MainFrameDetail::MainFrameDesignKey::MainFrameHotKeyStep),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillY),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconWidth),
            MainFrameDetail::MainFrameDesign().Number<float>(
                MainFrameDetail::MainFrameDesignKey::MainFrameSkillIconHeight));
        x = hotSkillRect.x;
        y = hotSkillRect.y;
        width = hotSkillRect.width;
        height = hotSkillRect.height;
        if (iIndex == 10)
        {
            iIndex = 0;
        }
        if (CheckMouseIn(x, y, width, height) == true)
        {
            if (m_iHotKeySkillType[iIndex] == -1)
            {
                if (m_EventState == EVENT_BTN_HOVER_SKILLHOTKEY)
                {
                    m_bRenderSkillInfo = false;
                    m_iRenderSkillInfoType = -1;
                }
                if (m_EventState == EVENT_BTN_DOWN_SKILLHOTKEY && MouseLButtonPush == false)
                {
                    m_EventState = EVENT_NONE;
                }
                continue;
            }

            WORD bySkillType = CharacterAttribute->Skill[m_iHotKeySkillType[iIndex]];

            if (bySkillType == 0 ||
                (bySkillType >= AT_SKILL_STUN && bySkillType <= AT_SKILL_REMOVAL_BUFF))
                continue;

            BYTE bySkillUseType = SkillAttribute[bySkillType].SkillUseType;

            if (bySkillUseType == SKILL_USE_TYPE_MASTERLEVEL)
            {
                continue;
            }

            if (m_EventState == EVENT_BTN_HOVER_SKILLHOTKEY)
            {
                SetSkillTooltip(m_iHotKeySkillType[iIndex], x, y, width);
                return true;
            }
            if (m_EventState == EVENT_BTN_DOWN_SKILLHOTKEY)
            {
                if (MouseLButtonPush == false)
                {
                    if (m_iRenderSkillInfoType == m_iHotKeySkillType[iIndex])
                    {
                        m_EventState = EVENT_NONE;
                        gSkillManager.SelectHeroSkill(m_iHotKeySkillType[iIndex]);
                        PlayBuffer(SOUND_CLICK01);
                        return false;
                    }
                    else
                    {
                        m_EventState = EVENT_NONE;
                    }
                }
            }
        }
    }

    x = hotSkillStripRect.x;
    y = hotSkillStripRect.y;
    width = hotSkillStripRect.width;
    height = hotSkillStripRect.height;
    if (m_EventState == EVENT_BTN_DOWN_SKILLHOTKEY)
    {
        if (MouseLButtonPush == false && CheckMouseIn(x, y, width, height) == false)
        {
            m_EventState = EVENT_NONE;
            return true;
        }
        return false;
    }

    if (m_bSkillList == false)
        return true;

    WORD bySkillType = 0;

    int iSkillCount = 0;
    bool bMouseOnSkillList = false;

    const MainFrameDetail::SkillListGeometry skillList =
        MainFrameDetail::CalculateSkillListGeometry(ModernUiViewportWidth(),
                                                    ModernUiViewportHeight(), ModernUiScale());
    x = skillList.originX;
    y = skillList.originY;
    width = skillList.slotWidth;
    height = skillList.slotHeight;

    EVENT_STATE PrevEventState = m_EventState;

    for (int i = 0; i < MAX_MAGIC; ++i)
    {
        bySkillType = CharacterAttribute->Skill[i];

        if (bySkillType == 0 ||
            (bySkillType >= AT_SKILL_STUN && bySkillType <= AT_SKILL_REMOVAL_BUFF))
            continue;

        BYTE bySkillUseType = SkillAttribute[bySkillType].SkillUseType;

        if (bySkillUseType == SKILL_USE_TYPE_MASTER || bySkillUseType == SKILL_USE_TYPE_MASTERLEVEL)
        {
            continue;
        }

        const auto slot = MainFrameDetail::CalculateSkillListSlot(skillList, iSkillCount++);
        x = slot.x;
        y = slot.y;

        if (CheckMouseIn(x, y, width, height) == true)
        {
            bMouseOnSkillList = true;
            if (m_EventState == EVENT_NONE && MouseLButtonPush == false)
            {
                m_EventState = EVENT_BTN_HOVER_SKILLLIST;
                break;
            }
        }

        if (m_EventState == EVENT_BTN_HOVER_SKILLLIST && MouseLButtonPush == true &&
            CheckMouseIn(x, y, width, height) == true)
        {
            m_EventState = EVENT_BTN_DOWN_SKILLLIST;
            break;
        }

        if (m_EventState == EVENT_BTN_HOVER_SKILLLIST && MouseLButtonPush == false &&
            CheckMouseIn(x, y, width, height) == true)
        {
            SetSkillTooltip(i, x, y, width);
        }

        if (m_EventState == EVENT_BTN_DOWN_SKILLLIST && MouseLButtonPush == false &&
            m_iRenderSkillInfoType == i && CheckMouseIn(x, y, width, height) == true)
        {
            m_EventState = EVENT_NONE;

            gSkillManager.SelectHeroSkill(i);
            m_bSkillList = false;

            PlayBuffer(SOUND_CLICK01);
            return false;
        }
    }

    if (PrevEventState != m_EventState)
    {
        if (m_EventState == EVENT_NONE || m_EventState == EVENT_BTN_HOVER_SKILLLIST)
            return true;
        return false;
    }

    if (Hero->PetCommands.present)
    {
        x = skillList.petOriginX;
        y = skillList.petOriginY;
        width = skillList.slotWidth;
        height = skillList.slotHeight;
        for (int i = AT_PET_COMMAND_DEFAULT; i < AT_PET_COMMAND_END; ++i)
        {
            if (CheckMouseIn(x, y, width, height) == true)
            {
                bMouseOnSkillList = true;

                if (m_EventState == EVENT_NONE && MouseLButtonPush == false)
                {
                    m_EventState = EVENT_BTN_HOVER_SKILLLIST;
                    return true;
                }
                if (m_EventState == EVENT_BTN_HOVER_SKILLLIST && MouseLButtonPush == true)
                {
                    m_EventState = EVENT_BTN_DOWN_SKILLLIST;
                    return false;
                }

                if (m_EventState == EVENT_BTN_HOVER_SKILLLIST)
                {
                    SetSkillTooltip(i, x, y, width);
                }
                if (m_EventState == EVENT_BTN_DOWN_SKILLLIST && MouseLButtonPush == false &&
                    m_iRenderSkillInfoType == i)
                {
                    m_EventState = EVENT_NONE;

                    gSkillManager.SelectHeroSkill(i);
                    m_bSkillList = false;
                    PlayBuffer(SOUND_CLICK01);
                    return false;
                }
            }
            x += width;
        }
    }

    if (bMouseOnSkillList == false && m_EventState == EVENT_BTN_HOVER_SKILLLIST)
    {
        m_EventState = EVENT_NONE;
        return true;
    }
    if (bMouseOnSkillList == false && MouseLButtonPush == false &&
        m_EventState == EVENT_BTN_DOWN_SKILLLIST)
    {
        m_EventState = EVENT_NONE;
        return false;
    }
    if (m_EventState == EVENT_BTN_DOWN_SKILLLIST)
    {
        if (MouseLButtonPush == false)
        {
            m_EventState = EVENT_NONE;
            return true;
        }
        return false;
    }

    return true;
}

bool SEASON3B::CNewUISkillList::UpdateKeyEvent()
{
    for (int i = 0; i < 9; ++i)
    {
        if (IsPress('1' + i))
        {
            UseHotKey(i + 1);
        }
    }

    if (IsPress('0'))
    {
        UseHotKey(0);
    }

    if (m_EventState == EVENT_BTN_HOVER_SKILLLIST)
    {
        if (IsRepeat(VK_CONTROL))
        {
            for (int i = 0; i < 9; ++i)
            {
                if (IsPress('1' + i))
                {
                    SetHotKey(i + 1, m_iRenderSkillInfoType);

                    return false;
                }
            }

            if (IsPress('0'))
            {
                SetHotKey(0, m_iRenderSkillInfoType);

                return false;
            }
        }
    }

    if (IsRepeat(VK_SHIFT))
    {
        for (int i = 0; i < 4; ++i)
        {
            if (IsPress('1' + i))
            {
                Hero->CurrentSkill = AT_PET_COMMAND_DEFAULT + i;
                return false;
            }
        }
    }

    return true;
}

bool SEASON3B::CNewUISkillList::IsArrayUp(BYTE bySkill)
{
    for (int i = 0; i < SKILLHOTKEY_COUNT; ++i)
    {
        if (m_iHotKeySkillType[i] == bySkill)
        {
            if (i == 0 || i > 5)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
    }

    return false;
}

bool SEASON3B::CNewUISkillList::IsArrayIn(BYTE bySkill)
{
    for (int i = 0; i < SKILLHOTKEY_COUNT; ++i)
    {
        if (m_iHotKeySkillType[i] == bySkill)
        {
            return true;
        }
    }

    return false;
}

void SEASON3B::CNewUISkillList::SetHotKey(int iHotKey, int iSkillType)
{
    for (int i = 0; i < SKILLHOTKEY_COUNT; ++i)
    {
        if (m_iHotKeySkillType[i] == iSkillType)
        {
            m_iHotKeySkillType[i] = -1;
            break;
        }
    }

    m_iHotKeySkillType[iHotKey] = iSkillType;
}

int SEASON3B::CNewUISkillList::GetHotKey(int iHotKey)
{
    return m_iHotKeySkillType[iHotKey];
}

int SEASON3B::CNewUISkillList::GetSelectedMainFrameHotSlot() const
{
    const int firstHotKey = m_bHotKeySkillListUp ? 6 : 1;
    for (int slot = 0; slot < 5; ++slot)
    {
        const int hotKey = (firstHotKey + slot) % SKILLHOTKEY_COUNT;
        if (m_iHotKeySkillType[hotKey] == Hero->CurrentSkill)
        {
            return slot;
        }
    }
    return -1;
}

void SEASON3B::CNewUISkillList::UseHotKey(int iHotKey)
{
    if (m_iHotKeySkillType[iHotKey] != -1)
    {
        if (m_iHotKeySkillType[iHotKey] >= AT_PET_COMMAND_DEFAULT &&
            m_iHotKeySkillType[iHotKey] < AT_PET_COMMAND_END)
        {
            if (!Hero->PetCommands.present)
            {
                return;
            }
        }

        auto wHotKeySkill = CharacterAttribute->Skill[m_iHotKeySkillType[iHotKey]];

        if (wHotKeySkill == 0)
        {
            return;
        }

        gSkillManager.SelectHeroSkill(m_iHotKeySkillType[iHotKey]);

        auto bySkill = CharacterAttribute->Skill[Hero->CurrentSkill];

        if (g_pOption->IsAutoAttack() == true && gMapManager.ContextMap() != WD_6STADIUM &&
            gMapManager.InChaosCastle() == false &&
            (bySkill == AT_SKILL_TELEPORT || bySkill == AT_SKILL_TELEPORT_ALLY))
        {
            SelectedCharacter = -1;
            Attacking = -1;
        }
    }
}

void SEASON3B::CNewUISkillList::ToggleSkillHotKeyPage()
{
    m_bHotKeySkillListUp = !m_bHotKeySkillListUp;
}

bool SEASON3B::CNewUISkillList::Update()
{
    if (m_iLastPageSkill != Hero->CurrentSkill)
    {
        m_iLastPageSkill = Hero->CurrentSkill;
        if (IsArrayIn(Hero->CurrentSkill) == true)
        {
            m_bHotKeySkillListUp = IsArrayUp(Hero->CurrentSkill);
        }
    }

    if (!Hero->PetCommands.present)
    {
        if (Hero->CurrentSkill >= AT_PET_COMMAND_DEFAULT && Hero->CurrentSkill < AT_PET_COMMAND_END)
        {
            Hero->CurrentSkill = 0;
        }
    }

    return true;
}

void SEASON3B::CNewUISkillList::FillMainFrameSkills(UI::Modern::RmlMainFrameRequest &request)
{
    if (!CharacterAttribute->SkillNumber)
        return;
    const int first = m_bHotKeySkillListUp ? 6 : 1;
    for (int slot = 0; slot < UI::Modern::RmlMainFrameRequest::CurrentSkillSlot; ++slot)
    {
        const int key = (first + slot) % SKILLHOTKEY_COUNT;
        const int index = m_iHotKeySkillType[key];
        if (index < 0 || (index >= AT_PET_COMMAND_DEFAULT && index < AT_PET_COMMAND_END &&
                          !Hero->PetCommands.present))
            continue;
        FillMainFrameSkill(request, slot, index);
    }
    FillMainFrameSkill(request, UI::Modern::RmlMainFrameRequest::CurrentSkillSlot,
                       Hero->CurrentSkill);
}

void SEASON3B::CNewUISkillList::FillMainFrameSkill(UI::Modern::RmlMainFrameRequest &request,
                                                   int slot, int index)
{
    auto type = CharacterAttribute->Skill[index];
    if (index >= AT_PET_COMMAND_DEFAULT)
        type = static_cast<ActionSkillType>(index);
    if (type == 0)
        return;
    const bool disabled = IsSkillIconDisabled(type);
    const auto &skill = SkillAttribute[type];
    request.skillIcons[slot] = UI::Modern::RmlSkillIconState::FromSkill(type, skill.SkillUseType,
                                                                        skill.Magic_Icon, disabled);
    if ((disabled && (type == AT_SKILL_CHAIN_DRIVE || type == AT_SKILL_CHAIN_DRIVE_STR ||
                      type == AT_SKILL_DRAGON_KICK || type == AT_SKILL_DRAGON_ROAR ||
                      type == AT_SKILL_DRAGON_ROAR_STR)) ||
        type == AT_SKILL_INFINITY_ARROW || type == AT_SKILL_INFINITY_ARROW_STR ||
        type == AT_SKILL_EXPANSION_OF_WIZARDRY || type == AT_SKILL_EXPANSION_OF_WIZARDRY_STR ||
        type == AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY)
        return;
    request.skillCooldowns[slot] = SkillCooldownRatio(index);
}

float SEASON3B::CNewUISkillList::SkillCooldownRatio(int index)
{
    const int delay = CharacterAttribute->SkillDelay[index];
    if (delay <= 0)
        return 0;
    const auto type = CharacterAttribute->Skill[index];
    if (type == AT_SKILL_PLASMA_STORM_FENRIR && !CheckAttack())
        return 0;
    return static_cast<float>(delay) / SkillAttribute[type].Delay;
}

void SEASON3B::CNewUISkillList::SetSkillTooltip(int type, float x, float y, float width)
{
    m_bRenderSkillInfo = true;
    m_iRenderSkillInfoType = type;
    m_iRenderSkillInfoPosX = static_cast<int>(std::lround(x + width * 0.5F));
    m_iRenderSkillInfoPosY = static_cast<int>(std::lround(y));
}

float SEASON3B::CNewUISkillList::GetLayerDepth()
{
    return 5.2f;
}

bool SEASON3B::CNewUISkillList::IsSkillIconDisabled(ActionSkillType bySkillType)
{
    bool bCantSkill = false;

    BYTE bySkillUseType = SkillAttribute[bySkillType].SkillUseType;

    if (!gSkillManager.AreSkillAttributeRequirementsMet(bySkillType))
    {
        bCantSkill = true;
    }

    if (IsCanBCSkill(bySkillType) == false)
    {
        bCantSkill = true;
    }
    if (g_isCharacterBuff((&Hero->Object), eBuff_AddSkill) &&
        bySkillUseType == SKILL_USE_TYPE_BRAND)
    {
        bCantSkill = true;
    }
    auto isSittingOnPet =
        (Hero->Helper.Type == MODEL_HORN_OF_UNIRIA || Hero->Helper.Type == MODEL_HORN_OF_DINORANT ||
         Hero->Helper.Type == MODEL_HORN_OF_FENRIR);
    if (bySkillType == AT_SKILL_IMPALE && !isSittingOnPet)
    {
        bCantSkill = true;
    }

    if (bySkillType == AT_SKILL_IMPALE && isSittingOnPet)
    {
        int iTypeL = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
        int iTypeR = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
        if ((iTypeL < ITEM_SPEAR || iTypeL >= ITEM_BOW) &&
            (iTypeR < ITEM_SPEAR || iTypeR >= ITEM_BOW))
        {
            bCantSkill = true;
        }
    }

    if (isSittingOnPet &&
        ((bySkillType >= AT_SKILL_BLOCKING && bySkillType <= AT_SKILL_SLASH) ||
         bySkillType == AT_SKILL_FALLING_SLASH_STR || bySkillType == AT_SKILL_LUNGE_STR ||
         bySkillType == AT_SKILL_CYCLONE_STR || bySkillType == AT_SKILL_CYCLONE_STR_MG ||
         bySkillType == AT_SKILL_SLASH_STR))
    {
        bCantSkill = true;
    }

    if ((bySkillType == AT_SKILL_POWER_SLASH || bySkillType == AT_SKILL_POWER_SLASH_STR) &&
        isSittingOnPet)
    {
        bCantSkill = true;
    }

    if (bySkillType == AT_SKILL_PARTY_TELEPORT && PartyNumber <= 0)
    {
        bCantSkill = true;
    }

    if (bySkillType == AT_SKILL_PARTY_TELEPORT &&
        (IsDoppelGanger1() || IsDoppelGanger2() || IsDoppelGanger3() || IsDoppelGanger4()))
    {
        bCantSkill = true;
    }

    if (bySkillType == AT_SKILL_EARTHSHAKE || bySkillType == AT_SKILL_EARTHSHAKE_STR ||
        bySkillType == AT_SKILL_EARTHSHAKE_MASTERY)
    {
        BYTE byDarkHorseLife = 0;
        byDarkHorseLife = CharacterMachine->Equipment[EQUIPMENT_HELPER].Durability;
        if (byDarkHorseLife == 0 || Hero->Helper.Type != MODEL_DARK_HORSE_ITEM)
        {
            bCantSkill = true;
        }
    }
#ifdef PJH_FIX_SPRIT
    /*???*/
    if (bySkillType >= AT_PET_COMMAND_DEFAULT && bySkillType < AT_PET_COMMAND_END)
    {
        int iCharisma = CharacterAttribute->Charisma + CharacterAttribute->AddCharisma;
        PET_INFO PetInfo;
        giPetManager::GetPetInfo(PetInfo, 421 - PET_TYPE_DARK_SPIRIT);
        int RequireCharisma = (185 + (PetInfo.m_wLevel * 15));
        if (RequireCharisma > iCharisma)
        {
            bCantSkill = true;
        }
    }
#endif //PJH_FIX_SPRIT
    if ((bySkillType == AT_SKILL_INFINITY_ARROW) || (bySkillType == AT_SKILL_INFINITY_ARROW_STR) ||
        (bySkillType == AT_SKILL_EXPANSION_OF_WIZARDRY) ||
        (bySkillType == AT_SKILL_EXPANSION_OF_WIZARDRY_STR) ||
        (bySkillType == AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY))
    {
        if ((g_isCharacterBuff((&Hero->Object), eBuff_InfinityArrow)) ||
            (g_isCharacterBuff((&Hero->Object), eBuff_SwellOfMagicPower)))
        {
            bCantSkill = true;
        }
    }

    if (bySkillType == AT_SKILL_FIRE_SLASH || bySkillType == AT_SKILL_FIRE_SLASH_STR)
    {
        WORD Strength;
        const WORD wRequireStrength = 596;
        Strength = CharacterAttribute->Strength + CharacterAttribute->AddStrength;
        if (Strength < wRequireStrength)
        {
            bCantSkill = true;
        }
        int iTypeL = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
        int iTypeR = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;

        if (!(iTypeR != -1 && (iTypeR < ITEM_STAFF || iTypeR >= ITEM_STAFF + MAX_ITEM_INDEX) &&
              (iTypeL < ITEM_STAFF || iTypeL >= ITEM_STAFF + MAX_ITEM_INDEX)))
        {
            bCantSkill = true;
        }
    }

    switch (bySkillType)
    {
        //case AT_SKILL_PIERCING:
    case AT_SKILL_ICE_ARROW:
    case AT_SKILL_ICE_ARROW_STR: {
        WORD Dexterity;
        const WORD wRequireDexterity = 646;
        Dexterity = CharacterAttribute->Dexterity + CharacterAttribute->AddDexterity;
        if (Dexterity < wRequireDexterity)
        {
            bCantSkill = true;
        }
    }
    break;
    }

    if (bySkillType == AT_SKILL_TWISTING_SLASH || bySkillType == AT_SKILL_TWISTING_SLASH_STR ||
        bySkillType == AT_SKILL_TWISTING_SLASH_STR_MG ||
        bySkillType == AT_SKILL_TWISTING_SLASH_MASTERY || bySkillType == AT_SKILL_RAGEFUL_BLOW ||
        bySkillType == AT_SKILL_RAGEFUL_BLOW_STR || bySkillType == AT_SKILL_RAGEFUL_BLOW_MASTERY ||
        bySkillType == AT_SKILL_DEATHSTAB || bySkillType == AT_SKILL_DEATHSTAB_STR)
    {
        int iTypeL = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
        int iTypeR = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;

        if (!(iTypeR != -1 && (iTypeR < ITEM_STAFF || iTypeR >= ITEM_STAFF + MAX_ITEM_INDEX) &&
              (iTypeL < ITEM_STAFF || iTypeL >= ITEM_STAFF + MAX_ITEM_INDEX)))
        {
            bCantSkill = true;
        }
    }

    if (gMapManager.InChaosCastle() == true)
    {
        if (bySkillType == AT_SKILL_EARTHSHAKE || bySkillType == AT_SKILL_EARTHSHAKE_STR ||
            bySkillType == AT_SKILL_EARTHSHAKE_MASTERY || bySkillType == AT_SKILL_RIDER ||
            (static_cast<int>(bySkillType) >= static_cast<int>(AT_PET_COMMAND_DEFAULT) &&
             static_cast<int>(bySkillType) <= static_cast<int>(AT_PET_COMMAND_TARGET)))
        {
            bCantSkill = true;
        }
    }
    else
    {
        if (bySkillType == AT_SKILL_EARTHSHAKE || bySkillType == AT_SKILL_EARTHSHAKE_STR ||
            bySkillType == AT_SKILL_EARTHSHAKE_MASTERY)
        {
            BYTE byDarkHorseLife = 0;
            byDarkHorseLife = CharacterMachine->Equipment[EQUIPMENT_HELPER].Durability;
            if (byDarkHorseLife == 0)
            {
                bCantSkill = true;
            }
        }
    }

    if (!g_CMonkSystem.IsSwordformGlovesUseSkill(bySkillType))
    {
        bCantSkill = true;
    }
    if (g_CMonkSystem.IsRideNotUseSkill(bySkillType, Hero->Helper.Type))
    {
        bCantSkill = true;
    }

    ITEM *pLeftRing = &CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
    ITEM *pRightRing = &CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];

    if (g_CMonkSystem.IsChangeringNotUseSkill(pLeftRing->Type, pRightRing->Type, pLeftRing->Level,
                                              pRightRing->Level) &&
        (gCharacterManager.GetBaseClass(Hero->Class) == CLASS_RAGEFIGHTER))
    {
        bCantSkill = true;
    }

    if (!g_csItemOption.IsNonWeaponSkillOrIsSkillEquipped(bySkillType))
    {
        bCantSkill = true;
    }

    if (bySkillType == AT_SKILL_MULTI_SHOT && gameplay_.GetEquipedBowType_Skill() == BOWTYPE_NONE)
        bCantSkill = true;
    if (bySkillType == AT_SKILL_FLAME_STRIKE)
    {
        const int left = CharacterMachine->Equipment[EQUIPMENT_WEAPON_LEFT].Type;
        const int right = CharacterMachine->Equipment[EQUIPMENT_WEAPON_RIGHT].Type;
        if (!(right != -1 && (right < ITEM_STAFF || right >= ITEM_STAFF + MAX_ITEM_INDEX) &&
              (left < ITEM_STAFF || left >= ITEM_STAFF + MAX_ITEM_INDEX)))
            bCantSkill = true;
    }
    return bCantSkill;
}

bool SEASON3B::CNewUISkillList::IsSkillListUp()
{
    return m_bHotKeySkillListUp;
}

void SEASON3B::CNewUISkillList::ResetMouseLButton()
{
    MouseLButton = false;
    MouseLButtonPop = false;
    MouseLButtonPush = false;
}

void SEASON3B::CNewUIMainFrameWindow::SetPreExp_Wide(__int64 dwPreExp)
{
    m_loPreExp = dwPreExp;
}

void SEASON3B::CNewUIMainFrameWindow::SetGetExp_Wide(__int64 dwGetExp)
{
    m_loGetExp = dwGetExp;

    if (m_loGetExp > 0)
    {
        m_bExpEffect = true;
        m_dwExpEffectTime = timeGetTime();
    }
}

void SEASON3B::CNewUIMainFrameWindow::SetPreExp(__int64 dwPreExp)
{
    m_dwPreExp = dwPreExp;
}

void SEASON3B::CNewUIMainFrameWindow::SetGetExp(__int64 dwGetExp)
{
    m_dwGetExp = dwGetExp;

    if (m_dwGetExp > 0)
    {
        m_bExpEffect = true;
        m_dwExpEffectTime = timeGetTime();
    }
}

void SEASON3B::CNewUIMainFrameWindow::SetBtnState(int iBtnType, bool bStateDown)
{
    switch (iBtnType)
    {
#ifdef PBG_ADD_INGAMESHOP_UI_MAINFRAME
    case MAINFRAME_BTN_PARTCHARGE: {
        if (bStateDown)
        {
            m_BtnCShop.UnRegisterButtonState();
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_CSHOP, 2);
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_CSHOP, 3);
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_CSHOP, 2);
            m_BtnCShop.ChangeImgIndex(IMAGE_MENU_BTN_CSHOP, 2);
        }
        else
        {
            m_BtnCShop.UnRegisterButtonState();
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_CSHOP, 0);
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_CSHOP, 1);
            m_BtnCShop.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_CSHOP, 2);
            m_BtnCShop.ChangeImgIndex(IMAGE_MENU_BTN_CSHOP, 0);
        }
    }
    break;
#endif //defined defined PBG_ADD_INGAMESHOP_UI_MAINFRAME
    case MAINFRAME_BTN_CHAINFO: {
        if (bStateDown)
        {
            m_BtnChaInfo.UnRegisterButtonState();
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_CHAINFO, 2);
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_CHAINFO, 3);
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_CHAINFO, 2);
            m_BtnChaInfo.ChangeImgIndex(IMAGE_MENU_BTN_CHAINFO, 2);
        }
        else
        {
            m_BtnChaInfo.UnRegisterButtonState();
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_CHAINFO, 0);
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_CHAINFO, 1);
            m_BtnChaInfo.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_CHAINFO, 2);
            m_BtnChaInfo.ChangeImgIndex(IMAGE_MENU_BTN_CHAINFO, 0);
        }
    }
    break;
    case MAINFRAME_BTN_MYINVEN: {
        if (bStateDown)
        {
            m_BtnMyInven.UnRegisterButtonState();
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_MYINVEN, 2);
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_MYINVEN, 3);
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_MYINVEN, 2);
            m_BtnMyInven.ChangeImgIndex(IMAGE_MENU_BTN_MYINVEN, 2);
        }
        else
        {
            m_BtnMyInven.UnRegisterButtonState();
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_MYINVEN, 0);
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_MYINVEN, 1);
            m_BtnMyInven.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_MYINVEN, 2);
            m_BtnMyInven.ChangeImgIndex(IMAGE_MENU_BTN_MYINVEN, 0);
        }
    }
    break;
    case MAINFRAME_BTN_FRIEND: {
        if (bStateDown)
        {
            m_BtnFriend.UnRegisterButtonState();
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_FRIEND, 2);
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_FRIEND, 3);
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_FRIEND, 2);
            m_BtnFriend.ChangeImgIndex(IMAGE_MENU_BTN_FRIEND, 2);
        }
        else
        {
            m_BtnFriend.UnRegisterButtonState();
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_FRIEND, 0);
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_FRIEND, 1);
            m_BtnFriend.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_FRIEND, 2);
            m_BtnFriend.ChangeImgIndex(IMAGE_MENU_BTN_FRIEND, 0);
        }
    }
    break;
    case MAINFRAME_BTN_WINDOW: {
        if (bStateDown)
        {
            m_BtnWindow.UnRegisterButtonState();
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_WINDOW, 2);
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_WINDOW, 3);
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_WINDOW, 2);
            m_BtnWindow.ChangeImgIndex(IMAGE_MENU_BTN_WINDOW, 2);
        }
        else
        {
            m_BtnWindow.UnRegisterButtonState();
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_UP, IMAGE_MENU_BTN_WINDOW, 0);
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_OVER, IMAGE_MENU_BTN_WINDOW, 1);
            m_BtnWindow.RegisterButtonState(BUTTON_STATE_DOWN, IMAGE_MENU_BTN_WINDOW, 2);
            m_BtnWindow.ChangeImgIndex(IMAGE_MENU_BTN_WINDOW, 0);
        }
    }
    break;
    }
}

namespace MoveCommandDetail
{

const UI::Modern::RmlUiDesign &Design()
{
    static const UI::Modern::RmlUiDesign design(
        "Data/UI/PC/HUD/move_command.rml",
        {"MoveCommand-PanelWidth",      "MoveCommand-PanelHeight",
         "MoveCommand-MainListX",       "MoveCommand-MainListY",
         "MoveCommand-FavoriteListY",   "MoveCommand-ShowMapX",
         "MoveCommand-CloseX",          "MoveCommand-TextButtonY",
         "MoveCommand-TextButtonWidth", "MoveCommand-TextButtonHeight",
         "MoveCommand-ScrollBarX",      "MoveCommand-ScrollBarY",
         "MoveCommand-RowHeight",       "MoveCommand-RowWidth",
         "MoveCommand-CheckBoxX",       "MoveCommand-CheckBoxWidth",
         "MoveCommand-ScrollUpRect",    "MoveCommand-ScrollThumbRect",
         "MoveCommand-ScrollDownY",     "MoveCommand-ScrollTrackHeight"});
    return design;
}

UI::Modern::RmlMoveCommandRect MoveRowRect(float listX, float listY, std::size_t index) noexcept
{
    const float height = Design().Number(MoveCommandDetail::DesignKey::RowHeight);
    return {listX, listY + height * index, Design().Number(MoveCommandDetail::DesignKey::RowWidth),
            height};
}

UI::Modern::RmlMoveCommandRect MoveCheckBoxRect(float listX, float listY,
                                                std::size_t index) noexcept
{
    auto row = MoveRowRect(listX, listY, index);
    const float offset = Design().Number(MoveCommandDetail::DesignKey::CheckBoxX);
    row.x += offset;
    row.width =
        std::min(Design().Number(MoveCommandDetail::DesignKey::CheckBoxWidth), row.width - offset);
    return row;
}

UI::Modern::RmlMoveCommandRect MoveThumbRect(std::size_t position, std::size_t maximum) noexcept
{
    const auto box = Design().Values(MoveCommandDetail::DesignKey::ScrollThumbRect);
    UI::Modern::RmlMuScrollBarState state{};
    state.position = position;
    state.maximum = maximum;
    state.pageSize = UI::Modern::RmlMoveCommandVisibleRows;
    UI::Modern::RmlMuScrollBarMetrics metrics{};
    metrics.thumbHeight = box[3];
    metrics.minimumThumbHeight = box[3];
    metrics.trackHeight = Design().Number(MoveCommandDetail::DesignKey::ScrollTrackHeight);
    return {Design().Number(MoveCommandDetail::DesignKey::ScrollBarX) + box[0],
            Design().Number(MoveCommandDetail::DesignKey::ScrollBarY) + box[1] +
                UI::Modern::RmlMuScrollBar::ThumbOffset(state, metrics),
            box[2], UI::Modern::RmlMuScrollBar::ThumbLength(state, metrics)};
}

UI::Modern::RmlMoveCommandRect MoveScrollButtonRect(bool down) noexcept
{
    const auto box = Design().Values(MoveCommandDetail::DesignKey::ScrollUpRect);
    return {Design().Number(MoveCommandDetail::DesignKey::ScrollBarX) + box[0],
            Design().Number(MoveCommandDetail::DesignKey::ScrollBarY) +
                (down ? Design().Number(MoveCommandDetail::DesignKey::ScrollDownY) : box[1]),
            box[2], box[3]};
}

bool IsLuckySeal(const std::wstring &name)
{
    return std::find(MoveCommandDetail::LuckySealMapNames.begin(),
                     MoveCommandDetail::LuckySealMapNames.end(),
                     name) != MoveCommandDetail::LuckySealMapNames.end();
}

} // namespace MoveCommandDetail

CNewUIMoveCommandWindow::CNewUIMoveCommandWindow(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), renderer_(RendererForConstruction()), showMapButton_(keeper),
      closeButton_(keeper), scrollUpButton_(keeper), scrollDownButton_(keeper)
{
    m_pNewUIMng = nullptr;
    favoriteIndices_.fill(-1);
}

CNewUIMoveCommandWindow::~CNewUIMoveCommandWindow()
{
    Release();
}

bool CNewUIMoveCommandWindow::Create(CNewUIManager *pNewUIMng, int x, int y)
{
    if (pNewUIMng == nullptr)
    {
        return false;
    }

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_MOVEMAP, this);
    SetPos(x, y);
    UpdateButtonGeometry();
    Show(false);
    StageMoveCommand();
    return true;
}

void CNewUIMoveCommandWindow::Release()
{
    UI::Modern::RmlMoveCommandRequest request;
    renderer_.StageMoveCommand(request);

    if (m_pNewUIMng != nullptr)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

bool CNewUIMoveCommandWindow::IsLuckySealBuff()
{
    return g_isCharacterBuff((&Hero->Object), eBuff_Seal1) ||
           g_isCharacterBuff((&Hero->Object), eBuff_Seal2) ||
           g_isCharacterBuff((&Hero->Object), eBuff_Seal3) ||
           g_isCharacterBuff((&Hero->Object), eBuff_Seal4) ||
           g_isCharacterBuff((&Hero->Object), eBuff_Seal_HpRecovery) ||
           g_isCharacterBuff((&Hero->Object), eBuff_Seal_MpRecovery) ||
           g_isCharacterBuff((&Hero->Object), eBuff_AscensionSealMaster) ||
           g_isCharacterBuff((&Hero->Object), eBuff_WealthSealMaster) ||
           g_isCharacterBuff((&Hero->Object), eBuff_NewWealthSeal) ||
           g_isCharacterBuff((&Hero->Object), eBuff_PartyExpBonus);
}

bool CNewUIMoveCommandWindow::IsMapMove(const std::wstring &src)
{
    if ((Hero->Object.Kind == KIND_PLAYER && Hero->Object.Type == MODEL_PLAYER &&
         Hero->Object.SubType == MODEL_GM_CHARACTER) ||
        g_isCharacterBuff((&Hero->Object), eBuff_GMEffect))
    {
        return true;
    }

    if (IsLuckySealBuff())
    {
        return true;
    }

    const wchar_t *command = nullptr;
    bool ignoreCase = false;
    if (src.find(I18N::Game::Warp) != std::wstring::npos)
    {
        command = I18N::Game::Warp;
    }
    else if (src.find(L"/move") != std::wstring::npos)
    {
        command = L"/move";
        ignoreCase = true;
    }
    else
    {
        return MoveCommandDetail::IsLuckySeal(src);
    }

    wchar_t text[1024]{};
    wcsncpy_s(text, src.c_str(), _TRUNCATE);
    std::wstring separators = command;
    separators += L' ';
    wchar_t *context = nullptr;
    const wchar_t *mapName = wcstok_s(text, separators.c_str(), &context);
    if (mapName == nullptr)
    {
        return false;
    }

    SettingCanMoveMap();
    for (const CMoveCommandData::MOVEINFODATA *moveInfo : moveEntries_)
    {
        const int comparison = ignoreCase ? wcsicmp(mapName, moveInfo->_ReqInfo.szMainMapName)
                                          : wcscmp(mapName, moveInfo->_ReqInfo.szMainMapName);
        if (comparison == 0 && moveInfo->_bCanMove)
        {
            return MoveCommandDetail::IsLuckySeal(moveInfo->_ReqInfo.szSubMapName);
        }
    }
    return false;
}

void CNewUIMoveCommandWindow::SetMoveCommandKey(DWORD key)
{
    m_dwMoveCommandKey = key;
}

DWORD CNewUIMoveCommandWindow::GetMoveCommandKey()
{
    m_dwMoveCommandKey = g_KeyGenerator.GenerateKeyValue(m_dwMoveCommandKey);
    return m_dwMoveCommandKey;
}

void CNewUIMoveCommandWindow::SetStrifeMap()
{
    constexpr int StrifeMapIndex = 42;
    const bool enableStrife = !g_ServerListManager.IsNonPvP();
    for (CMoveCommandData::MOVEINFODATA *moveInfo : moveEntries_)
    {
        moveInfo->_bStrife = enableStrife && moveInfo->_ReqInfo.index == StrifeMapIndex;
    }
}

int CNewUIMoveCommandWindow::AdjustedRequiredLevel(
    const CMoveCommandData::MOVEINFODATA &moveInfo) const
{
    int requiredLevel = moveInfo._ReqInfo.iReqLevel;
    const int baseClass = gCharacterManager.GetBaseClass(CharacterAttribute->Class);
    const bool reducedClass =
        baseClass == CLASS_DARK || baseClass == CLASS_DARK_LORD || baseClass == CLASS_RAGEFIGHTER;
    if (reducedClass && requiredLevel != 400)
    {
        requiredLevel = static_cast<int>(static_cast<float>(requiredLevel) * 2.0F / 3.0F);
    }
    return requiredLevel;
}

bool CNewUIMoveCommandWindow::CanMoveToMap(CMoveCommandData::MOVEINFODATA &moveInfo) const
{
    const int requiredLevel = AdjustedRequiredLevel(moveInfo);
    if (CharacterAttribute->Level < requiredLevel ||
        static_cast<int>(CharacterMachine->Gold) < moveInfo._ReqInfo.iReqZen ||
        static_cast<int>(Hero->PK) >= PVP_MURDERER1)
    {
        return false;
    }

    const ITEM &rightRing = CharacterMachine->Equipment[EQUIPMENT_RING_RIGHT];
    const ITEM &leftRing = CharacterMachine->Equipment[EQUIPMENT_RING_LEFT];
    const ITEM &helper = CharacterMachine->Equipment[EQUIPMENT_HELPER];
    const ITEM &wing = CharacterMachine->Equipment[EQUIPMENT_WING];

    if (wcscmp(moveInfo._ReqInfo.szMainMapName, I18N::Game::Icarus) == 0)
    {
        const bool canFly =
            helper.Type == ITEM_HORN_OF_FENRIR || helper.Type == ITEM_HORN_OF_DINORANT ||
            helper.Type == ITEM_DARK_HORSE_ITEM || wing.Type == ITEM_CAPE_OF_LORD ||
            (wing.Type >= ITEM_WING_OF_STORM && wing.Type <= ITEM_WING_OF_DIMENSION) ||
            (wing.Type >= ITEM_WING && wing.Type <= ITEM_WINGS_OF_DARKNESS) ||
            (wing.Type >= ITEM_WING + 130 && wing.Type <= ITEM_WING + 134) ||
            (wing.Type >= ITEM_CAPE_OF_FIGHTER && wing.Type <= ITEM_CAPE_OF_OVERRULE) ||
            wing.Type == ITEM_WING + 135;
        return canFly && helper.Type != ITEM_HORN_OF_UNIRIA &&
               !g_ChangeRingMgr->CheckBanMoveIcarusMap(rightRing.Type, leftRing.Type);
    }

    if (wcsncmp(moveInfo._ReqInfo.szMainMapName, I18N::Game::Atlans, wcslen(I18N::Game::Atlans)) ==
        0)
    {
        return helper.Type != ITEM_HORN_OF_UNIRIA && helper.Type != ITEM_HORN_OF_DINORANT;
    }

    if (g_ServerListManager.IsNonPvP() &&
        wcscmp(moveInfo._ReqInfo.szMainMapName, I18N::Game::Vulcanus) == 0)
    {
        return false;
    }

    return !moveInfo._bStrife || Hero->m_byGensInfluence != 0;
}

void CNewUIMoveCommandWindow::SettingCanMoveMap()
{
    for (CMoveCommandData::MOVEINFODATA *moveInfo : moveEntries_)
    {
        moveInfo->_bCanMove = CanMoveToMap(*moveInfo);
        moveInfo->_bSelected = false;
    }
}

bool CNewUIMoveCommandWindow::BtnProcess()
{
    UpdateButtonGeometry();
    UpdateHoveredRows();

    if (closeButton_.UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(INTERFACE_MOVEMAP);
        return true;
    }

    if (showMapButton_.UpdateMouseEvent())
    {
        g_pNewUISystem->Hide(INTERFACE_MOVEMAP);
        if (g_pNewUIMiniMap->m_bSuccess)
        {
            g_pNewUISystem->Toggle(INTERFACE_MINI_MAP);
        }
        return true;
    }

    if (scrollUpButton_.UpdateMouseEvent())
    {
        ScrollBy(-1);
        return true;
    }

    if (scrollDownButton_.UpdateMouseEvent())
    {
        ScrollBy(1);
        return true;
    }

    const std::size_t maximumOffset = MaximumScrollOffset();
    const auto thumb = MoveCommandDetail::MoveThumbRect(scrollOffset_, maximumOffset);
    const auto thumbRect = ReferenceRect(thumb.x, thumb.y, thumb.width, thumb.height);

    if (!thumbDragging_ && maximumOffset > 0 && MouseIn(thumbRect) && IsPress(VK_LBUTTON))
    {
        thumbDragging_ = true;
        thumbDragMouseY_ = MouseY;
        thumbDragStartOffset_ = scrollOffset_;
    }
    if (thumbDragging_)
    {
        if (IsRelease(VK_LBUTTON))
        {
            thumbDragging_ = false;
        }
        else
        {
            const auto startThumb = MoveCommandDetail::MoveThumbRect(0, maximumOffset);
            const auto endThumb = MoveCommandDetail::MoveThumbRect(maximumOffset, maximumOffset);
            const auto travelRect = ReferenceRect(0.0F, 0.0F, 0.0F, endThumb.y - startThumb.y);
            scrollOffset_ = UI::Modern::RmlMuScrollBar::DragPosition(
                thumbDragStartOffset_, MouseY - thumbDragMouseY_, maximumOffset, travelRect.height);
        }
    }

    const auto panelRect = ReferenceRect(
        0.0F, 0.0F, MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::PanelWidth),
        MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::PanelHeight));
    if (!MouseIn(panelRect))
    {
        return false;
    }

    if (IsPress(VK_LBUTTON))
    {
        g_pMyInventory->GetInventoryCtrl()->BackupPickedItem();
    }

    if (MouseWheel > 0)
    {
        ScrollBy(-1);
    }
    else if (MouseWheel < 0)
    {
        ScrollBy(1);
    }
    MouseWheel = 0;

    if (!IsRelease(VK_LBUTTON))
    {
        return false;
    }

    if (hoveredMainRow_ >= 0)
    {
        const std::size_t mapIndex = scrollOffset_ + static_cast<std::size_t>(hoveredMainRow_);
        const auto box = MoveCommandDetail::MoveCheckBoxRect(
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListX),
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListY),
            static_cast<std::size_t>(hoveredMainRow_));
        const auto checkbox = ReferenceRect(box.x, box.y, box.width, box.height);
        if (MouseIn(checkbox))
        {
            ToggleFavorite(mapIndex);
        }
        else
        {
            WarpTo(mapIndex);
        }
        return true;
    }

    if (hoveredFavoriteRow_ >= 0)
    {
        const std::size_t favoriteIndex = static_cast<std::size_t>(hoveredFavoriteRow_);
        const auto box = MoveCommandDetail::MoveCheckBoxRect(
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListX),
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::FavoriteListY),
            favoriteIndex);
        const auto checkbox = ReferenceRect(box.x, box.y, box.width, box.height);
        if (MouseIn(checkbox))
        {
            RemoveFavorite(favoriteIndex);
        }
        else
        {
            WarpTo(static_cast<std::size_t>(favoriteIndices_[favoriteIndex]));
        }
        return true;
    }

    return false;
}

bool CNewUIMoveCommandWindow::UpdateMouseEvent()
{
    BtnProcess();
    return !MouseIn(ReferenceRect(
        0.0F, 0.0F, MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::PanelWidth),
        MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::PanelHeight)));
}

bool CNewUIMoveCommandWindow::UpdateKeyEvent()
{
    if (IsVisible() && IsPress(VK_ESCAPE))
    {
        g_pNewUISystem->Hide(INTERFACE_MOVEMAP);
        PlayBuffer(SOUND_CLICK01);
        return false;
    }
    return true;
}

bool CNewUIMoveCommandWindow::Update()
{
    if (!IsVisible())
    {
        return true;
    }

    SettingCanMoveMap();
    UpdateHoveredRows();
    StageMoveCommand();
    return true;
}

void CNewUIMoveCommandWindow::OpenningProcess()
{
    SetPos(0, 0);
    SetStrifeMap();
    SettingCanMoveMap();
    scrollOffset_ = 0;
    hoveredMainRow_ = -1;
    hoveredFavoriteRow_ = -1;
    thumbDragging_ = false;
    UpdateButtonGeometry();
    StageMoveCommand();
}

void CNewUIMoveCommandWindow::ClosingProcess()
{
    UI::Modern::RmlMoveCommandRequest request;
    renderer_.StageMoveCommand(request);
}

float CNewUIMoveCommandWindow::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::Warp;
}

void CNewUIMoveCommandWindow::UpdateButtonGeometry()
{
    const auto setGeometry = [this](CNewUIButton &button, float x, float y, float width,
                                    float height) {
        const auto rect = ReferenceRect(x, y, width, height);
        button.ChangeButtonInfo(static_cast<int>(std::lround(rect.x)),
                                static_cast<int>(std::lround(rect.y)),
                                std::max(1, static_cast<int>(std::lround(rect.width))),
                                std::max(1, static_cast<int>(std::lround(rect.height))));
    };

    setGeometry(showMapButton_,
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::ShowMapX),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonY),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonWidth),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonHeight));
    setGeometry(closeButton_,
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::CloseX),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonY),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonWidth),
                MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::TextButtonHeight));
    const auto up = MoveCommandDetail::MoveScrollButtonRect(false);
    const auto down = MoveCommandDetail::MoveScrollButtonRect(true);
    setGeometry(scrollUpButton_, up.x, up.y, up.width, up.height);
    setGeometry(scrollDownButton_, down.x, down.y, down.width, down.height);
}

void CNewUIMoveCommandWindow::UpdateHoveredRows()
{
    hoveredMainRow_ = -1;
    hoveredFavoriteRow_ = -1;

    for (std::size_t index = 0; index < UI::Modern::RmlMoveCommandVisibleRows; ++index)
    {
        if (scrollOffset_ + index >= moveEntries_.size())
        {
            break;
        }
        const auto row = MoveCommandDetail::MoveRowRect(
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListX),
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListY), index);
        if (MouseIn(ReferenceRect(row.x, row.y, row.width, row.height)))
        {
            hoveredMainRow_ = static_cast<int>(index);
            return;
        }
    }

    for (std::size_t index = 0; index < UI::Modern::RmlMoveCommandFavoriteRows; ++index)
    {
        if (favoriteIndices_[index] < 0)
        {
            break;
        }
        const auto row = MoveCommandDetail::MoveRowRect(
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::MainListX),
            MoveCommandDetail::Design().Number(MoveCommandDetail::DesignKey::FavoriteListY), index);
        if (MouseIn(ReferenceRect(row.x, row.y, row.width, row.height)))
        {
            hoveredFavoriteRow_ = static_cast<int>(index);
            return;
        }
    }
}

void CNewUIMoveCommandWindow::ScrollBy(int amount)
{
    const int target = static_cast<int>(scrollOffset_) + amount;
    scrollOffset_ =
        static_cast<std::size_t>(std::clamp(target, 0, static_cast<int>(MaximumScrollOffset())));
}

void CNewUIMoveCommandWindow::ToggleFavorite(std::size_t mapIndex)
{
    CMoveCommandData::MOVEINFODATA *const moveInfo = MoveAt(mapIndex);
    if (moveInfo == nullptr)
    {
        return;
    }

    const auto existing =
        std::find(favoriteIndices_.begin(), favoriteIndices_.end(), static_cast<int>(mapIndex));
    const bool checked = existing != favoriteIndices_.end();
    if (!UI::Modern::RmlMuCheckBoxListRow::CanToggle(checked, !moveInfo->_bCanMove))
    {
        return;
    }
    if (checked)
    {
        RemoveFavorite(static_cast<std::size_t>(std::distance(favoriteIndices_.begin(), existing)));
        return;
    }

    const auto empty = std::find(favoriteIndices_.begin(), favoriteIndices_.end(), -1);
    if (empty != favoriteIndices_.end())
    {
        *empty = static_cast<int>(mapIndex);
    }
}

void CNewUIMoveCommandWindow::RemoveFavorite(std::size_t favoriteIndex)
{
    if (favoriteIndex >= favoriteIndices_.size())
    {
        return;
    }

    for (std::size_t index = favoriteIndex; index + 1 < favoriteIndices_.size(); ++index)
    {
        favoriteIndices_[index] = favoriteIndices_[index + 1];
    }
    favoriteIndices_.back() = -1;
}

void CNewUIMoveCommandWindow::WarpTo(std::size_t mapIndex)
{
    CMoveCommandData::MOVEINFODATA *const moveInfo = MoveAt(mapIndex);
    if (moveInfo == nullptr || !moveInfo->_bCanMove)
    {
        return;
    }

    if (IsTheMapInDifferentServer(gMapManager.ContextMap(), moveInfo->_ReqInfo.index))
    {
        SaveOptions();
    }

    SocketClient->ToGameServer()->SendWarpCommandRequest(GetMoveCommandKey(),
                                                         moveInfo->_ReqInfo.index);
    g_pNewUISystem->Hide(INTERFACE_MOVEMAP);
}

CMoveCommandData::MOVEINFODATA *CNewUIMoveCommandWindow::MoveAt(std::size_t index) const
{
    return index < moveEntries_.size() ? moveEntries_[index] : nullptr;
}

std::size_t CNewUIMoveCommandWindow::MaximumScrollOffset() const noexcept
{
    return moveEntries_.size() > UI::Modern::RmlMoveCommandVisibleRows
               ? moveEntries_.size() - UI::Modern::RmlMoveCommandVisibleRows
               : 0;
}

bool CNewUIMoveCommandWindow::IsFavorite(std::size_t mapIndex) const noexcept
{
    return std::find(favoriteIndices_.begin(), favoriteIndices_.end(),
                     static_cast<int>(mapIndex)) != favoriteIndices_.end();
}

UI::Modern::RmlMoveCommandRect CNewUIMoveCommandWindow::ReferenceRect(float x, float y, float width,
                                                                      float height) const noexcept
{
    return UI::Modern::CalculateRmlMoveCommandReferenceRect(
        UI::Modern::CalculateRmlMoveCommandTransform(ModernUiScale()), ModernUiViewportWidth(),
        ModernUiViewportHeight(), x, y, width, height);
}

bool CNewUIMoveCommandWindow::MouseIn(const UI::Modern::RmlMoveCommandRect &rect) const
{
    return CheckMouseIn(static_cast<int>(std::lround(rect.x)),
                        static_cast<int>(std::lround(rect.y)),
                        std::max(1, static_cast<int>(std::lround(rect.width))),
                        std::max(1, static_cast<int>(std::lround(rect.height))));
}

ButtonVisualState CNewUIMoveCommandWindow::ToButtonState(BUTTON_STATE state) noexcept
{
    switch (state)
    {
    case BUTTON_STATE_OVER:
        return ButtonVisualState::Over;
    case BUTTON_STATE_DOWN:
        return ButtonVisualState::Down;
    default:
        return ButtonVisualState::Up;
    }
}

BOOL CNewUIMoveCommandWindow::IsTheMapInDifferentServer(const int fromMapIndex,
                                                        const int toMapIndex) const
{
    switch (fromMapIndex)
    {
    case WD_30BATTLECASTLE:
    case WD_79UNITEDMARKETPLACE:
        return TRUE;
    default:
        break;
    }

    switch (toMapIndex)
    {
    case 24:
    case 44:
        return TRUE;
    default:
        return FALSE;
    }
}

int CNewUIMoveCommandWindow::GetMapIndexFromMovereq(const wchar_t *mapName)
{
    if (mapName == nullptr)
    {
        return -1;
    }

    for (const CMoveCommandData::MOVEINFODATA *moveInfo : moveEntries_)
    {
        if (wcsicmp(moveInfo->_ReqInfo.szMainMapName, mapName) == 0 ||
            wcsicmp(moveInfo->_ReqInfo.szSubMapName, mapName) == 0)
        {
            return moveInfo->_ReqInfo.index;
        }
    }
    return -1;
}

using namespace MUHelper;

CNewUIMuHelper::CNewUIMuHelper(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper), muHelper_(MuHelperForConstruction()),
      _TempConfig(muHelper_.UiConfiguration()), m_modernPanel(keeper)
{
    m_pNewUIMng = NULL;

    m_iCurrentOpenTab = 0;
    m_iCurrentOpenSubWin = -1;

    m_iSelectedSkillSlot = 0;
    m_aiSelectedSkills.fill(-1);
}

CNewUIMuHelper::~CNewUIMuHelper()
{
    Release();
}

bool CNewUIMuHelper::Create(CNewUIManager *pNewUIMng, int, int)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_MUHELPER, this);

    m_modernPanel.Create();

    Show(false);

    return true;
}

void CNewUIMuHelper::Release()
{
    m_modernPanel.Release();

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIMuHelper::Update()
{
    ApplyModernChanges();
    return true;
}

bool CNewUIMuHelper::UpdateMouseEvent()
{
    ApplyModernChanges();
    return !m_modernPanel.OwnsPointer();
}

bool CNewUIMuHelper::ProcessModernUiInput(const SessionInputEvent &event)
{
    return IsVisible() && m_modernPanel.ProcessInput(event);
}

bool CNewUIMuHelper::HasTextInputFocus() const noexcept
{
    return IsVisible() && m_modernPanel.HasTextInputFocus();
}

std::optional<UI::Modern::RmlTextInputArea> CNewUIMuHelper::ModernTextInputArea() const
{
    return m_modernPanel.TextInputArea();
}

void CNewUIMuHelper::ApplyModernForm(const UI::Modern::PC::MuHelper::RmlMuHelperFormValues &form)
{
    MuHelperPanelDetail::WriteCombatForm(form, _TempConfig);
    const bool comboReady = std::all_of(m_aiSelectedSkills.begin(), m_aiSelectedSkills.begin() + 3,
                                        [](int skill) { return skill > 0; });
    if (form.combo && !comboReady)
    {
        g_pSystemLogBox->AddText(I18N::Game::InOrderToUseComboSkill, SEASON3B::TYPE_ERROR_MESSAGE);
    }
    _TempConfig.bUseCombo = form.combo && comboReady;
    _TempConfig.bBuffDuration = form.buffDuration;
    _TempConfig.bUseDarkRaven = form.useDarkRaven;
    _TempConfig.iDarkRavenMode = form.darkRavenMode;
    _TempConfig.bSupportParty = form.supportParty;
    _TempConfig.bAutoHeal = form.autoHeal;
    _TempConfig.bUseDrainLife = form.drainLife;
    _TempConfig.bRepairItem = form.repairItem;
    _TempConfig.bPickAllItems = form.pickAll;
    _TempConfig.bPickSelectItems = form.pickSelected;
    _TempConfig.bPickJewel = form.pickJewel;
    _TempConfig.bPickZen = form.pickZen;
    _TempConfig.bPickAncient = form.pickAncient;
    _TempConfig.bPickExcellent = form.pickExcellent;
    _TempConfig.bPickExtraItems = form.pickExtra;
    _TempConfig.bAutoAcceptFriend = form.autoAcceptFriend;
    _TempConfig.bAutoAcceptGuild = form.autoAcceptGuild;
    _TempConfig.bUseSelfDefense = form.selfDefense;
    _TempConfig.iPotionThreshold = form.potionThreshold;
    _TempConfig.iHealThreshold = form.healThreshold;
    _TempConfig.bAutoHealParty = form.partyHeal;
    _TempConfig.iHealPartyThreshold = form.partyHealThreshold;
    _TempConfig.bBuffDurationParty = form.partyBuffDuration;
    _TempConfig.iBuffCastInterval = form.partyBuffInterval;
    m_modernItemInput = form.itemInput;

    if (m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL2_CONFIG ||
        m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG)
    {
        const std::size_t skill =
            m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG ? 2U : 1U;
        uint32_t &condition = _TempConfig.aiSkillCondition[skill];
        condition &= MUHELPER_SKILL_PRECON_CLEAR;
        condition &= MUHELPER_SKILL_SUBCON_CLEAR;
        condition |= form.skillPreCondition == 1 ? ON_MOBS_ATTACKING : ON_MOBS_NEARBY;
        constexpr std::array<uint32_t, 4> MobConditions{
            ON_MORE_THAN_TWO_MOBS, ON_MORE_THAN_THREE_MOBS, ON_MORE_THAN_FOUR_MOBS,
            ON_MORE_THAN_FIVE_MOBS};
        condition |= MobConditions[form.skillMobCount];
    }
}

void CNewUIMuHelper::ApplyModernChanges()
{
    auto changes = m_modernPanel.TakeChanges();
    if (changes.form)
        ApplyModernForm(*changes.form);
    if (changes.tab)
    {
        m_iCurrentOpenTab = *changes.tab;
        m_iCurrentOpenSubWin = -1;
        g_pNewUISystem->Hide(INTERFACE_MUHELPER_SKILL_LIST);
    }
    if (changes.huntingRange)
        _TempConfig.iHuntingRange = *changes.huntingRange;
    if (changes.obtainingRange)
        _TempConfig.iObtainingRange = *changes.obtainingRange;
    if (changes.assignedSkillSlot)
    {
        const int slot = *changes.assignedSkillSlot;
        const bool sameOpenSlot =
            slot == m_iSelectedSkillSlot && g_pNewUIMuHelperSkillList->IsVisible();
        m_iSelectedSkillSlot = slot;
        if (slot < 3)
            g_pNewUIMuHelperSkillList->FilterByAttackSkills();
        else
            g_pNewUIMuHelperSkillList->FilterByBuffSkills();
        g_pNewUIMuHelperSkillList->Show(!sameOpenSlot);
    }
    if (changes.clearSkillSlot)
    {
        const int slot = *changes.clearSkillSlot;
        m_aiSelectedSkills[slot] = -1;
        ApplyConfigFromSkillSlot(slot, 0);
        _TempConfig.bUseCombo = false;
    }
    if (changes.availableSkillSlot)
    {
        const auto &skills = g_pNewUIMuHelperSkillList->Skills();
        const std::size_t index = *changes.availableSkillSlot;
        if (index < skills.size())
            AssignSkill(skills[index]);
        g_pNewUIMuHelperSkillList->Show(false);
    }
    if (changes.openSubPage)
        OpenModernSubPage(*changes.openSubPage);
    if (changes.addItem && !m_modernItemInput.empty())
    {
        _TempConfig.aExtraItems.insert(m_modernItemInput);
        m_modernItemInput.clear();
    }
    if (changes.removeItemIndex && *changes.removeItemIndex >= 0 &&
        static_cast<std::size_t>(*changes.removeItemIndex) < _TempConfig.aExtraItems.size())
    {
        auto item = _TempConfig.aExtraItems.begin();
        std::advance(item, *changes.removeItemIndex);
        _TempConfig.aExtraItems.erase(item);
    }
    if (changes.reset)
    {
        Reset();
        _TempConfig.bBuffDurationParty = false;
        m_modernItemInput.clear();
    }
    if (changes.resetSubPage)
        ResetModernSubPage();
    if (changes.closeSubPage)
        CancelModernSubPage();
    if (changes.closeSubPage || changes.saveSubPage)
    {
        m_iCurrentOpenSubWin = -1;
    }
    if (changes.close || changes.save)
    {
        if (changes.save)
            muHelper_.Save(_TempConfig);
        g_pNewUISystem->Hide(INTERFACE_MUHELPER);
        SetFocus(g_hWnd);
        ReleaseTextInputFocus();
    }
}

void CNewUIMuHelper::ApplyConfigFromSkillSlot(int iSlot, int iSkill)
{
    if (iSlot < 3)
    {
        _TempConfig.aiSkill[iSlot] = iSkill;
    }
    else
    {
        _TempConfig.aiBuff[iSlot - MuHelperPanelDetail::SKILL_SLOT_BUFF1] = iSkill;
    }
}

void CNewUIMuHelper::ApplyConfig()
{
    muHelper_.Load(_TempConfig);

    m_aiSelectedSkills[0] = _TempConfig.aiSkill[0] ? _TempConfig.aiSkill[0] : -1;
    m_aiSelectedSkills[1] = _TempConfig.aiSkill[1] ? _TempConfig.aiSkill[1] : -1;
    m_aiSelectedSkills[2] = _TempConfig.aiSkill[2] ? _TempConfig.aiSkill[2] : -1;
    m_aiSelectedSkills[3] = _TempConfig.aiBuff[0] ? _TempConfig.aiBuff[0] : -1;
    m_aiSelectedSkills[4] = _TempConfig.aiBuff[1] ? _TempConfig.aiBuff[1] : -1;
    m_aiSelectedSkills[5] = _TempConfig.aiBuff[2] ? _TempConfig.aiBuff[2] : -1;
}

void CNewUIMuHelper::Show(bool bShow)
{
    CNewUIObj::Show(bShow);

    if (bShow == false)
    {
        m_iCurrentOpenSubWin = -1;
        if (g_pNewUIMuHelperSkillList)
            g_pNewUIMuHelperSkillList->Show(false);
    }

    SetFocus(g_hWnd);
    ReleaseTextInputFocus();
}

namespace MuHelperPanelDetail
{

void ReadCombatForm(const ConfigData &config, MuHelperPanelDetail::RmlMuHelperFormValues &form)
{
    form.fallbackBasicAttack = config.bFallbackBasicAttack;
    form.usePotion = config.bUseHealPotion;
    form.longRangeCounter = config.bLongRangeCounterAttack;
    form.returnPosition = config.bReturnToOriginalPosition;
    form.returnSeconds = config.iMaxSecondsAway;
    for (std::size_t skill = 0; skill < form.skillTimer.size(); ++skill)
    {
        const std::size_t configSkill = skill + 1;
        form.skillTimer[skill] = (config.aiSkillCondition[configSkill] & ON_TIMER) != 0;
        form.skillCondition[skill] = (config.aiSkillCondition[configSkill] & ON_CONDITION) != 0;
        form.skillInterval[skill] = static_cast<int>(config.aiSkillInterval[configSkill]);
    }
    form.combo = config.bUseCombo;
    form.concentratedMonsters = config.bConcentratedMonsters;
    form.useSkillsClosely = config.bUseSkillsClosely;
    form.manualControlYieldSeconds = config.iManualControlYieldSeconds;
}

void WriteCombatForm(const MuHelperPanelDetail::RmlMuHelperFormValues &form, ConfigData &config)
{
    config.bFallbackBasicAttack = form.fallbackBasicAttack;
    config.bUseHealPotion = form.usePotion;
    config.bLongRangeCounterAttack = form.longRangeCounter;
    config.bReturnToOriginalPosition = form.returnPosition;
    config.iMaxSecondsAway = form.returnSeconds;
    for (std::size_t skill = 0; skill < form.skillTimer.size(); ++skill)
    {
        const std::size_t configSkill = skill + 1;
        uint32_t &condition = config.aiSkillCondition[configSkill];
        condition &= ~(static_cast<uint32_t>(ON_TIMER) | static_cast<uint32_t>(ON_CONDITION));
        if (form.skillTimer[skill])
            condition |= ON_TIMER;
        if (form.skillCondition[skill])
            condition |= ON_CONDITION;
        config.aiSkillInterval[configSkill] = form.skillInterval[skill];
    }
    config.bConcentratedMonsters = form.concentratedMonsters;
    config.bUseSkillsClosely = form.useSkillsClosely;
    config.iManualControlYieldSeconds = std::clamp(form.manualControlYieldSeconds, 0,
                                                   ManualControlYieldSecondsMaximum);
}

void ResetCombatConfig(ConfigData &config)
{
    config.bConcentratedMonsters = false;
    config.bUseSkillsClosely = false;
    config.iManualControlYieldSeconds = ManualControlYieldSecondsDefault;
    config.iHuntingRange = MuHelperPanelDetail::MAX_HUNTING_RANGE;

    config.iMaxSecondsAway = 10;
    config.bLongRangeCounterAttack = false;
    config.bReturnToOriginalPosition = true;

    config.aiSkill.fill(0);
    config.bUseCombo = false;

    config.aiSkillInterval.fill(0);

    config.aiSkillCondition.fill(0);
}
} // namespace MuHelperPanelDetail

UI::Modern::PC::MuHelper::RmlMuHelperContent CNewUIMuHelper::ModernContent() const
{
    using UI::Modern::PC::MuHelper::RmlMuHelperContent;
    using UI::Modern::PC::MuHelper::RmlMuHelperTextId;

    RmlMuHelperContent content;
    content[RmlMuHelperTextId::Title] = I18N::Game::OfficialMUHelper;
    content[RmlMuHelperTextId::Hunting] = I18N::Game::MuHelperBattleSetting;
    content[RmlMuHelperTextId::Obtaining] = I18N::Game::Obtaining;
    content[RmlMuHelperTextId::OtherSettings] = I18N::Game::OtherSettings;
    content[RmlMuHelperTextId::Range] = I18N::Game::MuHelperAttackRange;
    content[RmlMuHelperTextId::UseRegularAttack] = I18N::Game::MuHelperUseRegularAttack;
    content[RmlMuHelperTextId::Potion] = I18N::Game::Potion;
    content[RmlMuHelperTextId::LongDistanceCounterAttack] = I18N::Game::LongDistanceCounterAttack;
    content[RmlMuHelperTextId::OriginalPosition] = I18N::Game::OriginalPosition;
    content[RmlMuHelperTextId::Seconds] = L"Sec";
    content[RmlMuHelperTextId::BasicSkill] = I18N::Game::BasicSkill;
    content[RmlMuHelperTextId::ActivationSkill1] = I18N::Game::MuHelperActivation;
    content[RmlMuHelperTextId::ActivationSkill2] = I18N::Game::MuHelperActivation;
    content[RmlMuHelperTextId::Delay] = I18N::Game::MuHelperDelayCondition;
    content[RmlMuHelperTextId::Condition] = I18N::Game::Con;
    content[RmlMuHelperTextId::Setting] = I18N::Game::Setting;
    content[RmlMuHelperTextId::Combo] = I18N::Game::Combo;
    content[RmlMuHelperTextId::UseDarkSpirits] = I18N::Game::UseDarkSpirits;
    content[RmlMuHelperTextId::AutoAttack] = I18N::Game::AutoAttack;
    content[RmlMuHelperTextId::CeaseAttack] = I18N::Game::CeaseAttack;
    content[RmlMuHelperTextId::AttackTogether] = I18N::Game::AttackTogether;
    content[RmlMuHelperTextId::Party] = I18N::Game::Party;
    content[RmlMuHelperTextId::AutoHeal] = I18N::Game::AutoHeal;
    content[RmlMuHelperTextId::DrainLife] = I18N::Game::DrainLife;
    content[RmlMuHelperTextId::BuffDuration] = I18N::Game::BuffDuration;
    content[RmlMuHelperTextId::RepairItem] = I18N::Game::RepairItem;
    content[RmlMuHelperTextId::PickAll] = I18N::Game::MuHelperLootAllItems;
    content[RmlMuHelperTextId::PickSelected] = I18N::Game::MuHelperLootSelectedItem;
    content[RmlMuHelperTextId::Jewel] = I18N::Game::JewelGem;
    content[RmlMuHelperTextId::SetItem] = I18N::Game::SetItem;
    content[RmlMuHelperTextId::Zen] = I18N::Game::Zen;
    content[RmlMuHelperTextId::ExcellentItem] = I18N::Game::ExcellentItem;
    content[RmlMuHelperTextId::AddExtraItem] = I18N::Game::AddExtraItem;
    content[RmlMuHelperTextId::Add] = I18N::Game::Add;
    content[RmlMuHelperTextId::Delete] = I18N::Game::Delete;
    content[RmlMuHelperTextId::AutoAcceptFriend] = I18N::Game::AutoAcceptFriend;
    content[RmlMuHelperTextId::AutoAcceptGuild] = I18N::Game::AutoAcceptGuildMember;
    content[RmlMuHelperTextId::PvpCounterattack] = I18N::Game::PVPCounterattack;
    content[RmlMuHelperTextId::ManualControlYield] = L"Manual Control Pause";
    content[RmlMuHelperTextId::Initialization] = I18N::Game::Initialization;
    content[RmlMuHelperTextId::SaveSetting] = I18N::Game::SaveSetting;
    content[RmlMuHelperTextId::AutoRecovery] = I18N::Game::AutoRecovery;
    content[RmlMuHelperTextId::AutoPotion] = I18N::Game::AutoPotion;
    content[RmlMuHelperTextId::HpStatus] = I18N::Game::HPStatus;
    content[RmlMuHelperTextId::ActivationSkill] = I18N::Game::ActivationSkill;
    content[RmlMuHelperTextId::PreCondition] = I18N::Game::MuHelperBasicCondition;
    content[RmlMuHelperTextId::MonsterWithinRange] = I18N::Game::MonsterWithinHuntingRange;
    content[RmlMuHelperTextId::MonsterAttackingMe] = I18N::Game::MonsterAttackingMe;
    content[RmlMuHelperTextId::SubCondition] = I18N::Game::SubCon;
    content[RmlMuHelperTextId::MoreThanTwo] = I18N::Game::MoreThan2Mobs;
    content[RmlMuHelperTextId::MoreThanThree] = I18N::Game::MoreThan3Mobs;
    content[RmlMuHelperTextId::MoreThanFour] = I18N::Game::MoreThan4Mobs;
    content[RmlMuHelperTextId::MoreThanFive] = I18N::Game::MoreThan5Mobs;
    content[RmlMuHelperTextId::PreferencePartyHeal] = I18N::Game::PreferenceOfPartyHeal;
    content[RmlMuHelperTextId::HpStatusParty] = I18N::Game::HPStatusOfPartyMembers;
    content[RmlMuHelperTextId::BuffSupport] = I18N::Game::BuffSupport;
    content[RmlMuHelperTextId::BuffDurationParty] = I18N::Game::BuffDurationForAllPartyMembers;
    content[RmlMuHelperTextId::TimeCastingBuff] = I18N::Game::TimeSpaceOfCastingBuff;
    content[RmlMuHelperTextId::Close] = I18N::Game::Cancel;
    content[RmlMuHelperTextId::LootRange] = I18N::Game::MuHelperLootSetting;
    content[RmlMuHelperTextId::SaveSetup] = I18N::Game::SaveSetup;
    content[RmlMuHelperTextId::MonsterCondition] = I18N::Game::MuHelperMonsterCondition;
    content[RmlMuHelperTextId::SkillInterval] = I18N::Game::MuHelperSkillInterval;
    content[RmlMuHelperTextId::ConcentratedMonsters] = I18N::Game::MuHelperConcentratedMonsters;
    content[RmlMuHelperTextId::UseSkillsClosely] = I18N::Game::MuHelperUseSkillsClosely;

    auto &form = content.form;
    MuHelperPanelDetail::ReadCombatForm(_TempConfig, form);
    form.buffDuration = _TempConfig.bBuffDuration;
    form.useDarkRaven = _TempConfig.bUseDarkRaven;
    form.darkRavenMode = _TempConfig.iDarkRavenMode;
    form.supportParty = _TempConfig.bSupportParty;
    form.autoHeal = _TempConfig.bAutoHeal;
    form.drainLife = _TempConfig.bUseDrainLife;
    form.repairItem = _TempConfig.bRepairItem;
    form.pickAll = _TempConfig.bPickAllItems;
    form.pickSelected = _TempConfig.bPickSelectItems;
    form.pickJewel = _TempConfig.bPickJewel;
    form.pickZen = _TempConfig.bPickZen;
    form.pickAncient = _TempConfig.bPickAncient;
    form.pickExcellent = _TempConfig.bPickExcellent;
    form.pickExtra = _TempConfig.bPickExtraItems;
    form.autoAcceptFriend = _TempConfig.bAutoAcceptFriend;
    form.autoAcceptGuild = _TempConfig.bAutoAcceptGuild;
    form.selfDefense = _TempConfig.bUseSelfDefense;
    form.potionThreshold = _TempConfig.iPotionThreshold;
    form.healThreshold = _TempConfig.iHealThreshold;
    form.partyHeal = _TempConfig.bAutoHealParty;
    form.partyHealThreshold = _TempConfig.iHealPartyThreshold;
    form.partyBuffDuration = _TempConfig.bBuffDurationParty;
    form.partyBuffInterval = _TempConfig.iBuffCastInterval;
    form.itemInput = m_modernItemInput;

    if (m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL2_CONFIG ||
        m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG)
    {
        const std::size_t skill =
            m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG ? 2U : 1U;
        const uint32_t condition = _TempConfig.aiSkillCondition[skill];
        form.skillPreCondition = (condition & ON_MOBS_ATTACKING) != 0 ? 1 : 0;
        if ((condition & ON_MORE_THAN_FIVE_MOBS) != 0)
            form.skillMobCount = 3;
        else if ((condition & ON_MORE_THAN_FOUR_MOBS) != 0)
            form.skillMobCount = 2;
        else if ((condition & ON_MORE_THAN_THREE_MOBS) != 0)
            form.skillMobCount = 1;
        else
            form.skillMobCount = 0;
    }

    content.extraItems.assign(_TempConfig.aExtraItems.begin(), _TempConfig.aExtraItems.end());
    content.characterClass = gCharacterManager.GetBaseClass(Hero->Class);
    content.tab = m_iCurrentOpenTab;
    content.subPage = m_iCurrentOpenSubWin;
    content.huntingRange = _TempConfig.iHuntingRange;
    content.obtainingRange = _TempConfig.iObtainingRange;
    content.skillPickerVisible =
        g_pNewUIMuHelperSkillList && g_pNewUIMuHelperSkillList->IsVisible();
    FillModernSkills(content);
    return content;
}

void CNewUIMuHelper::FillModernSkills(UI::Modern::PC::MuHelper::RmlMuHelperContent &content) const
{
    content.assignedSkills = m_aiSelectedSkills;
    for (std::size_t i = 0; i < m_aiSelectedSkills.size(); ++i)
    {
        if (m_aiSelectedSkills[i] <= 0)
            continue;
        const auto &skill = SkillAttribute[m_aiSelectedSkills[i]];
        content.assignedIcons[i] = UI::Modern::RmlSkillIconState::FromSkill(
            m_aiSelectedSkills[i], skill.SkillUseType, skill.Magic_Icon);
    }
    if (content.skillPickerVisible)
    {
        content.availableSkillCount =
            std::min(g_pNewUIMuHelperSkillList->Skills().size(),
                     UI::Modern::PC::MuHelper::RmlMuHelperAvailableSkillCount);
        for (std::size_t i = 0; i < content.availableSkillCount; ++i)
        {
            const auto &skill = SkillAttribute[g_pNewUIMuHelperSkillList->Skills()[i]];
            content.availableIcons[i] = UI::Modern::RmlSkillIconState::FromSkill(
                g_pNewUIMuHelperSkillList->Skills()[i], skill.SkillUseType, skill.Magic_Icon);
        }
    }
}

void CNewUIMuHelper::OpenModernSubPage(int request)
{
    m_modernSubBackup = _TempConfig;
    const int characterClass = gCharacterManager.GetBaseClass(Hero->Class);
    if (request == 0)
    {
        m_iCurrentOpenSubWin =
            characterClass == CLASS_ELF        ? MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG_ELF
            : characterClass == CLASS_SUMMONER ? MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG_SUMMY
                                               : MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG;
    }
    else if (request == 1)
    {
        m_iCurrentOpenSubWin = characterClass == CLASS_ELF
                                   ? MuHelperPanelDetail::SUB_PAGE_PARTY_CONFIG_ELF
                                   : MuHelperPanelDetail::SUB_PAGE_PARTY_CONFIG;
    }
    else
    {
        m_iCurrentOpenSubWin = request;
    }
}

void CNewUIMuHelper::CancelModernSubPage()
{
    if (m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL2_CONFIG ||
        m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG)
    {
        const std::size_t skill =
            m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG ? 2U : 1U;
        _TempConfig.aiSkillCondition[skill] = m_modernSubBackup.aiSkillCondition[skill];
        _TempConfig.aiSkillInterval[skill] = m_modernSubBackup.aiSkillInterval[skill];
    }
    else if (m_iCurrentOpenSubWin >= MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG_ELF &&
             m_iCurrentOpenSubWin <= MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG)
    {
        _TempConfig.iPotionThreshold = m_modernSubBackup.iPotionThreshold;
        _TempConfig.iHealThreshold = m_modernSubBackup.iHealThreshold;
    }
    else
    {
        _TempConfig.iHealPartyThreshold = m_modernSubBackup.iHealPartyThreshold;
        _TempConfig.bAutoHealParty = m_modernSubBackup.bAutoHealParty;
        _TempConfig.bBuffDurationParty = m_modernSubBackup.bBuffDurationParty;
        _TempConfig.iBuffCastInterval = m_modernSubBackup.iBuffCastInterval;
    }
}

void CNewUIMuHelper::ResetModernSubPage()
{
    if (m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL2_CONFIG ||
        m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG)
    {
        const std::size_t skill =
            m_iCurrentOpenSubWin == MuHelperPanelDetail::SUB_PAGE_SKILL3_CONFIG ? 2U : 1U;
        _TempConfig.aiSkillCondition[skill] =
            static_cast<uint32_t>(ON_MOBS_NEARBY) | static_cast<uint32_t>(ON_MORE_THAN_TWO_MOBS);
        _TempConfig.aiSkillInterval[skill] = 0;
    }
    else if (m_iCurrentOpenSubWin >= MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG_ELF &&
             m_iCurrentOpenSubWin <= MuHelperPanelDetail::SUB_PAGE_POTION_CONFIG)
    {
        _TempConfig.iPotionThreshold = 50;
        _TempConfig.iHealThreshold = 50;
    }
    else
    {
        _TempConfig.iHealPartyThreshold = 50;
        _TempConfig.bAutoHealParty = false;
        _TempConfig.bBuffDurationParty = false;
        _TempConfig.iBuffCastInterval = 0;
    }
}

bool CNewUIMuHelper::UpdateKeyEvent()
{
    if (IsVisible())
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(INTERFACE_MUHELPER);
            g_pNewUISystem->Hide(INTERFACE_MUHELPER_SKILL_LIST);
            // PlayBuffer(SOUND_CLICK01);
            SetFocus(g_hWnd);
            ReleaseTextInputFocus();

            return false;
        }
    }
    return true;
}

void CNewUIMuHelper::Reset()
{
    MuHelperPanelDetail::ResetCombatConfig(_TempConfig);

    _TempConfig.aiBuff.fill(0);

    _TempConfig.bBuffDuration = true;
    _TempConfig.bBuffDurationParty = true;
    _TempConfig.iBuffCastInterval = 0;

    _TempConfig.bAutoHeal = false;
    _TempConfig.iHealThreshold = 60;
    _TempConfig.bUseDrainLife = false;
    _TempConfig.bUseHealPotion = false;
    _TempConfig.iPotionThreshold = 40;
    _TempConfig.bSupportParty = false;
    _TempConfig.bAutoHealParty = false;
    _TempConfig.iHealPartyThreshold = 60;

    _TempConfig.bUseDarkRaven = false;
    _TempConfig.iDarkRavenMode = PET_ATTACK_CEASE;
    _TempConfig.bRepairItem = false;

    _TempConfig.iObtainingRange = 8;
    _TempConfig.bPickAllItems = false;
    _TempConfig.bPickSelectItems = false;
    _TempConfig.bPickZen = false;
    _TempConfig.bPickJewel = false;
    _TempConfig.bPickExcellent = false;
    _TempConfig.bPickAncient = false;
    _TempConfig.bPickExtraItems = false;
    _TempConfig.aExtraItems.clear();

    ApplyConfig();
}

void CNewUIMuHelper::LoadSavedConfig(const ConfigData &config)
{
    _TempConfig = config;
    ApplyConfig();
}

float CNewUIMuHelper::GetLayerDepth()
{
    return UI::Modern::MigratedUiRenderLayers::MuHelper;
}

float CNewUIMuHelper::GetKeyEventOrder()
{
    return 3.4;
}

void CNewUIMuHelper::AssignSkill(int iSkill)
{
    if (m_iSelectedSkillSlot != -1 && m_iSelectedSkillSlot < MAX_SKILLS_SLOT)
    {
        if (!IsSkillAssigned(iSkill))
        {
            m_aiSelectedSkills[m_iSelectedSkillSlot] = iSkill;
            ApplyConfigFromSkillSlot(m_iSelectedSkillSlot, iSkill);

            g_ConsoleDebug.Write(MCD_NORMAL, L"[MU Helper] Assign m_aiSelectedSkills[%d] = %d",
                                 m_iSelectedSkillSlot, iSkill);
        }
        else
        {
            int iPrevIndex = GetSkillIndex(iSkill);
            m_aiSelectedSkills[iPrevIndex] = -1;
            ApplyConfigFromSkillSlot(iPrevIndex, 0);
            m_aiSelectedSkills[m_iSelectedSkillSlot] = iSkill;
            ApplyConfigFromSkillSlot(m_iSelectedSkillSlot, iSkill);
            _TempConfig.bUseCombo = false;
        }
    }
}

bool CNewUIMuHelper::IsSkillAssigned(int iSkill)
{
    return std::find(m_aiSelectedSkills.begin(), m_aiSelectedSkills.end(), iSkill) !=
           m_aiSelectedSkills.end();
}

int CNewUIMuHelper::GetSkillIndex(int iSkill)
{
    auto it = std::find(m_aiSelectedSkills.begin(), m_aiSelectedSkills.end(), iSkill);

    if (it != m_aiSelectedSkills.end())
    {
        return std::distance(m_aiSelectedSkills.begin(), it);
    }

    return -1;
}

void CNewUIMuHelperSkillList::PrepareSkillsToRender()
{
    m_aiSkillsToRender.clear();

    constexpr int HelperHotbarSlots = 10;
    for (int hotbarSlot = 0; hotbarSlot < HelperHotbarSlots; ++hotbarSlot)
    {
        const int skillIndex = g_pMainFrame->GetSkillHotKey(hotbarSlot);
        if (skillIndex < 0 || skillIndex >= MAX_MAGIC)
            continue;
        const int skillType = CharacterAttribute->Skill[skillIndex];
        if (skillType == 0 || (skillType >= AT_SKILL_STUN && skillType <= AT_SKILL_REMOVAL_BUFF))
        {
            continue;
        }
        const BYTE useType = SkillAttribute[skillType].SkillUseType;
        if (useType == SKILL_USE_TYPE_MASTER || useType == SKILL_USE_TYPE_MASTERLEVEL)
        {
            continue;
        }
        if ((m_bFilterByAttackSkills && IsAttackSkill(skillType)) ||
            (m_bFilterByBuffSkills && IsBuffSkill(skillType)))
        {
            m_aiSkillsToRender.push_back(skillType);
        }
    }
}

CNewUIMuHelperSkillList::CNewUIMuHelperSkillList(SessionKeeper &keeper)
    : SessionUiLegacyBindings(keeper)
{
    m_pNewUIMng = NULL;
}

CNewUIMuHelperSkillList::~CNewUIMuHelperSkillList()
{
    Release();
}

bool CNewUIMuHelperSkillList::Create(CNewUIManager *pNewUIMng)
{
    if (NULL == pNewUIMng)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(INTERFACE_MUHELPER_SKILL_LIST, this);

    Show(false);

    return true;
}

void CNewUIMuHelperSkillList::Release()
{

    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = NULL;
    }
}

bool CNewUIMuHelperSkillList::UpdateMouseEvent()
{
    return true;
}

bool CNewUIMuHelperSkillList::UpdateKeyEvent()
{
    if (IsVisible())
    {
        if (IsPress(VK_ESCAPE) == true)
        {
            g_pNewUISystem->Hide(INTERFACE_MUHELPER_SKILL_LIST);
            SetFocus(g_hWnd);
            ReleaseTextInputFocus();
            // PlayBuffer(SOUND_CLICK01);

            return false;
        }
    }
    return true;
}

bool CNewUIMuHelperSkillList::Update()
{
    return true;
}

float CNewUIMuHelperSkillList::GetLayerDepth()
{
    return 5.2f;
}

const std::vector<int> &CNewUIMuHelperSkillList::Skills() const noexcept
{
    return m_aiSkillsToRender;
}

bool CNewUIMuHelperSkillList::IsAttackSkill(int iSkillType)
{
    if (IsBuffSkill(iSkillType))
    {
        return false;
    }

    if (IsDefenseSkill(iSkillType))
    {
        return false;
    }

    if (IsHealingSkill(iSkillType))
    {
        return false;
    }

    return true;
}

bool CNewUIMuHelperSkillList::IsBuffSkill(int iSkillType)
{
    // To-do: Complete list of buffs

    switch (iSkillType)
    {
    // BK buffs
    case AT_SKILL_SWELL_LIFE:
    case AT_SKILL_SWELL_LIFE_STR:
    case AT_SKILL_SWELL_LIFE_PROFICIENCY:
        return true;
    // Elf buffs
    case AT_SKILL_INFINITY_ARROW:
    case AT_SKILL_INFINITY_ARROW_STR:
    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
    case AT_SKILL_DEFENSE_MASTERY:
    case AT_SKILL_ATTACK:
    case AT_SKILL_ATTACK_STR:
    case AT_SKILL_ATTACK_MASTERY:
        return true;
    // Wiz buffs
    case AT_SKILL_SOUL_BARRIER:
    case AT_SKILL_SOUL_BARRIER_STR:
    case AT_SKILL_SOUL_BARRIER_PROFICIENCY:
    case AT_SKILL_EXPANSION_OF_WIZARDRY:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_STR:
    case AT_SKILL_EXPANSION_OF_WIZARDRY_MASTERY:
        return true;
    // DL buffs
    case AT_SKILL_ADD_CRITICAL:
    case AT_SKILL_ADD_CRITICAL_STR1:
    case AT_SKILL_ADD_CRITICAL_STR2:
    case AT_SKILL_ADD_CRITICAL_STR3:
        return true;
    // Summoner buffs
    case AT_SKILL_ALICE_BERSERKER:
    case AT_SKILL_ALICE_BERSERKER_STR:
    case AT_SKILL_ALICE_THORNS:
        return true;
        // RF Buffs
    case AT_SKILL_ATT_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES:
    case AT_SKILL_DEF_UP_OURFORCES:
    case AT_SKILL_HP_UP_OURFORCES_STR:
    case AT_SKILL_DEF_UP_OURFORCES_MASTERY:
    case AT_SKILL_DEF_UP_OURFORCES_STR:
        return true;
    }

    return false;
}

bool CNewUIMuHelperSkillList::IsHealingSkill(int iSkillType)
{
    // To-do: Complete list of healing skills

    switch (iSkillType)
    {
    case AT_SKILL_HEALING:
    case AT_SKILL_HEALING_STR:
        return true;
    case AT_SKILL_ALICE_DRAINLIFE:
    case AT_SKILL_ALICE_DRAINLIFE_STR:
        return true;
    }

    return false;
}

bool CNewUIMuHelperSkillList::IsDefenseSkill(int iSkillType)
{
    switch (iSkillType)
    {
    case AT_SKILL_DEFENSE:
    case AT_SKILL_DEFENSE_STR:
        return true;
    }

    return false;
}

void CNewUIMuHelperSkillList::FilterByAttackSkills()
{
    m_bFilterByAttackSkills = true;
    m_bFilterByBuffSkills = false;

    PrepareSkillsToRender();
}

void CNewUIMuHelperSkillList::FilterByBuffSkills()
{
    m_bFilterByBuffSkills = true;
    m_bFilterByAttackSkills = false;

    PrepareSkillsToRender();
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

CUIMoveCommandListBox::CUIMoveCommandListBox(SessionKeeper &keeper)
    : CUITextListBox<MOVECOMMAND_TEXT>(keeper)
{
    m_iMaxLineCount = LegacyControlDetail::MaxTextLines;
    m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = 21;

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

    SetPosition(465, 324);
    SetSize(160, 195);
}

void CUIMoveCommandListBox::AddText(int iIndex, const wchar_t *szMapName,
                                    const wchar_t *szSubMapName, int iReqLevel, int iReqZen,
                                    int iGateNum)
{
    if (szMapName == nullptr || szMapName[0] == '\0' || iIndex < 0)
        return;

    MOVECOMMAND_TEXT text{};
    text.m_bIsSelected = FALSE;
    text.m_bCanMove = FALSE;
    wcsncpy(text.szMainMapName, szMapName, 32);
    wcsncpy(text.szSubMapName, szSubMapName, 32);
    text.iReqLevel = iReqLevel;
    text.iReqZen = iReqZen;
    text.iGateNum = iGateNum;

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

void CUIMoveCommandListBox::SetNumRenderLine(int iLine)
{
    if (iLine < m_iNumRenderLine && iLine < GetLineNum())
        ++m_iCurrentRenderEndLine;
    else if (iLine > GetLineNum())
        m_iCurrentRenderEndLine = 0;

    m_iNumRenderLine = iLine;
}

int CUIMoveCommandListBox::GetRenderLinePos_y(int iLineNumber)
{
    if (GetLineNum() > m_iNumRenderLine)
        return (m_iPos_y - m_iHeight + (m_iNumRenderLine - 1) * 13 - iLineNumber * 13 + 3);
    else
        return (m_iPos_y - m_iHeight + (GetLineNum() - 1) * 13 - iLineNumber * 13 + 3);
}

BOOL CUIMoveCommandListBox::DoLineMouseAction(int iLineNumber)
{
    return TRUE;
}
