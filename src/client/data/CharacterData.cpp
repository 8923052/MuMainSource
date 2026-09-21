#include "data/GameData.h"
#include "app/ApplicationKeeper.h"
#include "support/CoreMath.h"

void GameData::OpenMonsterSkillScript(const wchar_t *fileName)
{
    std::memset(storage_.MonsterSkill, -1, sizeof(Script_Skill));
    FILE *file = _wfopen(fileName, L"rb");
    if (file == nullptr)
    {
        ReportLoadError(fileName, GameDataLoadError::MissingFile, true);
        return;
    }

    const int recordSize = sizeof(Script_Skill) + sizeof(int);
    std::vector<BYTE> buffer(recordSize);
    int fileCount = 0;
    if (fread(&fileCount, sizeof(fileCount), 1, file) != 1 || fileCount < 0)
    {
        fclose(file);
        ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
        return;
    }

    for (int index = 0; index < fileCount; ++index)
    {
        if (fread(buffer.data(), buffer.size(), 1, file) != 1)
        {
            ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
            break;
        }
        BuxConvert(buffer.data(), recordSize);
        int monster = -1;
        int offset = 0;
        std::memcpy(&monster, buffer.data() + offset, sizeof(monster));
        offset += sizeof(monster);
        if (monster < 0 || monster >= MODEL_MONSTER_END)
        {
            ReportLoadError(fileName, GameDataLoadError::CorruptFile, true);
            break;
        }
        std::memcpy(storage_.MonsterSkill[monster].Skill_Num, buffer.data() + offset,
                    sizeof(int) * MAX_MONSTERSKILL_NUM);
        offset += sizeof(int) * MAX_MONSTERSKILL_NUM;
        std::memcpy(&storage_.MonsterSkill[monster].Slot, buffer.data() + offset, sizeof(int));
    }
    fclose(file);
}

void GameData::OpenMonsterScript(wchar_t *fileName)
{
    FILE *file = _wfopen(fileName, L"rb");
    if (file == nullptr)
    {
        ReportLoadError(fileName, GameDataLoadError::MissingFile, false);
        return;
    }

    GameDataDecodeDetail::GameDataScriptParser parser(*file);
    while (true)
    {
        GameDataDecodeDetail::GameDataToken token = parser.Next();
        if (token == GameDataDecodeDetail::GameDataToken::End ||
            (token == GameDataDecodeDetail::GameDataToken::Name &&
             std::strcmp("end", parser.String()) == 0))
        {
            break;
        }
        if (token != GameDataDecodeDetail::GameDataToken::Number ||
            storage_.EditMonsterNumber >= MAX_MONSTER)
        {
            ReportLoadError(fileName, GameDataLoadError::CorruptFile, false);
            break;
        }

        MONSTER_SCRIPT *monster = &storage_.MonsterScript[storage_.EditMonsterNumber++];
        monster->Type = static_cast<int>(parser.Number());
        (void)parser.Next();
        token = parser.Next();
        if (token != GameDataDecodeDetail::GameDataToken::Name)
        {
            ReportLoadError(fileName, GameDataLoadError::CorruptFile, false);
            break;
        }
        CMultiLanguage::ConvertFromUtf8(monster->Name, parser.String(), MAX_MONSTER_NAME);
    }
    fclose(file);
}

void GameData::CreateClassAttribute(int characterClass, int strength, int dexterity, int vitality,
                                    int energy, int life, int mana, int levelLife, int levelMana,
                                    int vitalityToLife, int energyToMana)
{
    CLASS_ATTRIBUTE *attribute = &storage_.ClassAttribute[characterClass];
    attribute->Strength = strength;
    attribute->Dexterity = dexterity;
    attribute->Vitality = vitality;
    attribute->Energy = energy;
    attribute->Life = life;
    attribute->Mana = mana;
    attribute->LevelLife = levelLife;
    attribute->LevelMana = levelMana;
    attribute->VitalityToLife = vitalityToLife;
    attribute->EnergyToMana = energyToMana;
    attribute->Shield = 0;
}

void GameData::CreateClassAttributes()
{
    CreateClassAttribute(0, 18, 18, 15, 30, 80, 60, 1, 2, 1, 2);
    CreateClassAttribute(1, 28, 20, 25, 10, 110, 20, 2, 1, 2, 1);
    CreateClassAttribute(2, 50, 50, 50, 30, 110, 30, 110, 30, 6, 3);
    CreateClassAttribute(3, 30, 30, 30, 30, 120, 80, 1, 1, 2, 2);
    CreateClassAttribute(4, 30, 30, 30, 30, 120, 80, 1, 1, 2, 2);
    CreateClassAttribute(5, 50, 50, 50, 30, 110, 30, 110, 30, 6, 3);
    CreateClassAttribute(6, 32, 27, 25, 20, 100, 40, 1, 3, 1, 1);
}

void ApplicationLegacyCalls::OpenMonsterSkillScript(const wchar_t *fileName)
{
    applicationKeeper_.GameDataUnit()->OpenMonsterSkillScript(fileName);
}
void ApplicationLegacyCalls::OpenMonsterScript(wchar_t *fileName)
{
    applicationKeeper_.GameDataUnit()->OpenMonsterScript(fileName);
}
void ApplicationLegacyCalls::CreateClassAttribute(int characterClass, int strength, int dexterity,
                                                  int vitality, int energy, int life, int mana,
                                                  int levelLife, int levelMana, int vitalityToLife,
                                                  int energyToMana)
{
    applicationKeeper_.GameDataUnit()->CreateClassAttribute(
        characterClass, strength, dexterity, vitality, energy, life, mana, levelLife, levelMana,
        vitalityToLife, energyToMana);
}
void ApplicationLegacyCalls::CreateClassAttributes()
{
    applicationKeeper_.GameDataUnit()->CreateClassAttributes();
}
