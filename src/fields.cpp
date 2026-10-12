#include "fields.h"

#include "save.h"
#include "tables.h"

namespace {

// Offsets into savedata.bin (PC). All values are little-endian.
constexpr uint32_t kProfileId = 16;
constexpr uint32_t kSiegeSkills = 1752;        // 8 x u32
constexpr uint32_t kSkillPoints = 1804;        // u32
constexpr uint32_t kSkillStates = 1812;        // 1 byte per skill id (0 = locked, 1 = buyable, 3 = owned)
constexpr uint32_t kCampaignSkills = 1920;     // 3 x u32
constexpr uint32_t kSerpentEmblems = 2016;     // 4 campaigns x 20 bytes
constexpr uint32_t kMercSkills = 2344;         // 8 sets x 3 x u32
constexpr uint32_t kMercStageUnlocks = 2440;   // u32 bitfield
constexpr uint32_t kCharacterUnlocks = 2448;   // 4 bitfield bytes
constexpr uint32_t kMercSolo = 9448;           // 10 stages x 432 bytes
constexpr uint32_t kMercDuo = 13768;           // 10 stages x 432 bytes
constexpr uint32_t kOnslaughtSkills = 26732;   // 8 x u32
constexpr uint32_t kSurvivorsSkills = 26768;   // 8 x u32
constexpr uint32_t kPredatorSkills = 26804;    // 8 x u32
constexpr uint32_t kLastCampaign = 32524;
constexpr uint32_t kLastPlayerSlot = 32528;
constexpr uint32_t kLastDifficulty = 32532;
constexpr uint32_t kLastStage = 32544;
constexpr uint32_t kLoadouts = 34028;          // 8 characters x 112 bytes

constexpr uint32_t kMercStageStride = 16 * 12 + 240;
constexpr uint32_t kMercRecordSize = 12;
constexpr uint32_t kLoadoutStride = 16 + 24 * 4;
constexpr int kEmblemsPerCampaign = 20;
constexpr int kMercStageCount = 10;

const EnumTable kCampaigns = {{0, L"Leon"}, {1, L"Chris"}, {2, L"Jake"}, {3, L"Ada"}, {4, L"Prelude"}};
const EnumTable kPlayerSlots = {{0, L"Player 1 (main character)"}, {1, L"Player 2 (partner)"}};
const EnumTable kDifficulties = {
    {0, L"Amateur"}, {1, L"Normal"}, {2, L"Veteran"}, {3, L"Professional"}, {4, L"No Hope"}};
const EnumTable kSkillState = {{0, L"Locked"}, {1, L"Available to buy"}, {3, L"Owned"}};
constexpr uint8_t kSkillOwned = 3;
const EnumTable kRanks ={{0, L"SS"}, {1, L"S"}, {2, L"A"}, {3, L"B"}, {4, L"C"}, {5, L"D"}, {6, L"E"}};

const wchar_t* const kCampaignNames[] = {L"Leon", L"Chris", L"Jake", L"Ada"};
const wchar_t* const kMercStages[kMercStageCount] = {
    L"Urban Chaos",        L"Steel Beast",   L"Mining the Depths", L"Rail Yard",       L"High Seas Fortress",
    L"The Catacombs",      L"Requiem for War", L"Liquid Fire",     L"Rooftop Mission", L"Creature Workshop"};
const wchar_t* const kMercCharacters[16] = {
    L"Leon",  L"Leon (Alt)",  L"Helena", L"Helena (Alt)", L"Chris", L"Chris (Alt)", L"Piers", L"Piers (Alt)",
    L"Jake",  L"Jake (Alt)",  L"Sherry", L"Sherry (Alt)", L"Ada",   L"Ada (Alt)",   L"Carla", L"Agent"};
const wchar_t* const kLoadoutCharacters[8] = {L"Leon", L"Helena", L"Chris", L"Piers",
                                              L"Jake", L"Sherry", L"Ada",   L"Agent"};

// Character/costume unlock bits: {byte index, mask, label}.
struct UnlockBit {
    uint32_t byte;
    uint8_t mask;
    const wchar_t* name;
};
const UnlockBit kCharacterBits[] = {
    {0, 0x01, L"Leon"},   {0, 0x02, L"Leon - Costume 1"},   {0, 0x04, L"Leon - Costume 2"},   {0, 0x08, L"Leon - Costume 3"},
    {0, 0x10, L"Helena"}, {0, 0x20, L"Helena - Costume 1"}, {0, 0x40, L"Helena - Costume 2"}, {0, 0x80, L"Helena - Costume 3"},
    {1, 0x01, L"Chris"},  {1, 0x02, L"Chris - Costume 1"},  {1, 0x04, L"Chris - Costume 2"},  {1, 0x08, L"Chris - Costume 3"},
    {1, 0x10, L"Piers"},  {1, 0x20, L"Piers - Costume 1"},  {1, 0x40, L"Piers - Costume 2"},  {1, 0x80, L"Piers - Costume 3"},
    {2, 0x01, L"Jake"},   {2, 0x02, L"Jake - Costume 1"},   {2, 0x04, L"Jake - Costume 2"},   {2, 0x08, L"Jake - Costume 3"},
    {2, 0x10, L"Sherry"}, {2, 0x20, L"Sherry - Costume 1"}, {2, 0x40, L"Sherry - Costume 2"}, {2, 0x80, L"Sherry - Costume 3"},
    {3, 0x01, L"Ada"},    {3, 0x02, L"Ada - Costume 1"},    {3, 0x04, L"Ada - Costume 2"},    {3, 0x08, L"Ada - Costume 3"},
    {3, 0x10, L"Carla"},  {3, 0x20, L"Agent"},
};

Field Num(std::wstring name, uint32_t off, FieldType t, const EnumTable* names = nullptr) {
    return Field{std::move(name), off, t, 0, names, false};
}

Field Bit(std::wstring name, uint32_t off, FieldType t, uint32_t mask) {
    return Field{std::move(name), off, t, mask, nullptr, false};
}

Page SkillSlots(std::wstring path, uint32_t base) {
    Page p{std::move(path), {}};
    for (int i = 0; i < 8; ++i)
        p.fields.push_back(Num(L"Slot " + std::to_wstring(i + 1), base + i * 4, FieldType::U32, &kSkills));
    return p;
}

Page MercRecords(std::wstring path, uint32_t base, int stage) {
    Page p{std::move(path), {}};
    for (int c = 0; c < 16; ++c) {
        uint32_t o = base + stage * kMercStageStride + c * kMercRecordSize;
        std::wstring who = std::wstring(kMercCharacters[c]) + L": ";
        p.fields.push_back(Num(who + L"Cleared", o + 0, FieldType::Flag8));
        p.fields.push_back(Num(who + L"Rank", o + 1, FieldType::U8, &kRanks));
        p.fields.push_back(Num(who + L"Score", o + 4, FieldType::U32));
        p.fields.push_back(Num(who + L"Enemies defeated", o + 8, FieldType::U8));
        p.fields.push_back(Num(who + L"Max combo", o + 9, FieldType::U8));
    }
    return p;
}

}  // namespace

std::vector<Page> BuildPages() {
    std::vector<Page> pages;

    {
        Page p{L"General", {}};
        p.fields.push_back(Num(L"Skill points", kSkillPoints, FieldType::U32));
        p.fields.push_back(Num(L"Last campaign", kLastCampaign, FieldType::U32, &kCampaigns));
        p.fields.push_back(Num(L"Last character", kLastPlayerSlot, FieldType::U32, &kPlayerSlots));
        p.fields.push_back(Num(L"Last difficulty", kLastDifficulty, FieldType::U32, &kDifficulties));
        p.fields.push_back(Num(L"Last stage", kLastStage, FieldType::U32, &kStages));
        Field profile = Num(L"Profile ID", kProfileId, FieldType::Hex32);
        profile.readOnly = true;
        p.fields.push_back(profile);
        pages.push_back(std::move(p));
    }

    {
        Page p{L"Unlocks/Characters & Costumes", {}};
        for (const auto& b : kCharacterBits)
            p.fields.push_back(Bit(b.name, kCharacterUnlocks + b.byte, FieldType::Bits8, b.mask));
        pages.push_back(std::move(p));
    }
    {
        Page p{L"Unlocks/Mercenaries Stages", {}};
        for (int i = 0; i < kMercStageCount; ++i)
            p.fields.push_back(Bit(kMercStages[i], kMercStageUnlocks, FieldType::Bits32, 1u << i));
        for (int i = 0; i < kMercStageCount; ++i)
            p.fields.push_back(Bit(std::wstring(kMercStages[i]) + L" (No Mercy)", kMercStageUnlocks,
                                   FieldType::Bits32, 1u << (i + kMercStageCount)));
        pages.push_back(std::move(p));
    }
    for (int c = 0; c < 4; ++c) {
        Page p{std::wstring(L"Unlocks/Serpent Emblems/") + kCampaignNames[c], {}};
        for (int i = 0; i < kEmblemsPerCampaign; ++i)
            p.fields.push_back(Num(L"Emblem " + std::to_wstring(i + 1),
                                   kSerpentEmblems + c * kEmblemsPerCampaign + i, FieldType::Flag8));
        pages.push_back(std::move(p));
    }

    {
        Page p{L"Skills/Owned Skills", {}};
        for (const auto& s : kSkills)
            if (s.value != 0) p.fields.push_back(Num(s.name, kSkillStates + s.value, FieldType::U8, &kSkillState));
        pages.push_back(std::move(p));
    }
    {
        Page p{L"Skills/Campaign", {}};
        for (int i = 0; i < 3; ++i)
            p.fields.push_back(Num(L"Skill " + std::to_wstring(i + 1), kCampaignSkills + i * 4, FieldType::U32, &kSkills));
        pages.push_back(std::move(p));
    }
    {
        Page p{L"Skills/Mercenaries", {}};
        for (int set = 0; set < 8; ++set)
            for (int i = 0; i < 3; ++i)
                p.fields.push_back(Num(L"Set " + std::to_wstring(set + 1) + L" - Skill " + std::to_wstring(i + 1),
                                       kMercSkills + (set * 3 + i) * 4, FieldType::U32, &kSkills));
        pages.push_back(std::move(p));
    }
    pages.push_back(SkillSlots(L"Skills/Siege", kSiegeSkills));
    pages.push_back(SkillSlots(L"Skills/Onslaught", kOnslaughtSkills));
    pages.push_back(SkillSlots(L"Skills/Survivors", kSurvivorsSkills));
    pages.push_back(SkillSlots(L"Skills/Predator", kPredatorSkills));

    for (int s = 0; s < kMercStageCount; ++s)
        pages.push_back(MercRecords(std::wstring(L"Mercenaries Records/Solo/") + kMercStages[s], kMercSolo, s));
    for (int s = 0; s < kMercStageCount; ++s)
        pages.push_back(MercRecords(std::wstring(L"Mercenaries Records/Duo/") + kMercStages[s], kMercDuo, s));

    for (int c = 0; c < 8; ++c) {
        Page p{std::wstring(L"Inventory/") + kLoadoutCharacters[c], {}};
        uint32_t base = kLoadouts + c * kLoadoutStride + 16;
        for (int s = 0; s < 24; ++s) {
            std::wstring slot = L"Slot " + std::to_wstring(s + 1);
            p.fields.push_back(Num(slot + L" - Item", base + s * 4, FieldType::Weapon, &kWeapons));
            p.fields.push_back(Num(slot + L" - Amount", base + s * 4 + 2, FieldType::I16));
        }
        pages.push_back(std::move(p));
    }

    return pages;
}

void UnlockEverything(SaveFile& save) {
    for (const auto& b : kCharacterBits) {
        uint32_t off = kCharacterUnlocks + b.byte;
        save.WriteU8(off, static_cast<uint8_t>(save.ReadU8(off) | b.mask));
    }
    save.WriteU32(kMercStageUnlocks, save.ReadU32(kMercStageUnlocks) | ((1u << (2 * kMercStageCount)) - 1));
    for (int i = 0; i < 4 * kEmblemsPerCampaign; ++i)
        if (save.ReadU8(kSerpentEmblems + i) == 0) save.WriteU8(kSerpentEmblems + i, 1);
    for (const auto& s : kSkills)
        if (s.value != 0) save.WriteU8(kSkillStates + s.value, kSkillOwned);
    save.dirty = true;
}
