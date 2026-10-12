#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct EnumEntry {
    int32_t value;
    const wchar_t* name;
};
using EnumTable = std::vector<EnumEntry>;

enum class FieldType {
    U8,      // unsigned byte
    Flag8,   // byte, 0 = no, non-zero = yes (writes 1)
    Bits8,   // single bit (mask) inside a byte
    Bits32,  // single bit (mask) inside a little-endian u32
    I16,     // signed little-endian 16-bit
    U32,     // unsigned little-endian 32-bit
    Weapon,  // u16 weapon id; top 4 bits are flags and are preserved
    Hex32,   // u32 shown as hex
};

struct Field {
    std::wstring name;
    uint32_t offset;
    FieldType type;
    uint32_t mask = 0;                  // Bits8 / Bits32
    const EnumTable* names = nullptr;   // optional value -> label table
    bool readOnly = false;
};

// A page of fields shown together. `path` is a '/'-separated tree path.
struct Page {
    std::wstring path;
    std::vector<Field> fields;
};

std::vector<Page> BuildPages();

// Bulk action: unlock all characters/costumes, Mercenaries stages and Serpent Emblems.
struct SaveFile;
void UnlockEverything(SaveFile& save);
