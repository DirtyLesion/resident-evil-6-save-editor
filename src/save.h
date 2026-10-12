#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "fields.h"

// Resident Evil 6 (PC) savedata.bin.
//
// Layout of the header:
//   0x00 u32  version/magic (left untouched)
//   0x04 u32  total file size
//   0x08 u32  checksum = sum of all u32 words from 0x18 to end of file (mod 2^32)
//   0x10 u64  profile id
//   0x18 ...  game data
struct SaveFile {
    std::vector<uint8_t> data;
    std::wstring path;
    bool dirty = false;

    bool Loaded() const { return !data.empty(); }

    bool Load(const std::wstring& file, std::wstring& error);
    // Recomputes the checksum and writes atomically. The first save to a given
    // path in a session copies the existing file to "<path>.bak" first.
    bool Save(const std::wstring& file, std::wstring& error);

    uint32_t StoredChecksum() const { return ReadU32(8); }
    uint32_t ComputeChecksum() const;

    int64_t Get(const Field& f) const;
    void Set(const Field& f, int64_t value);

    uint32_t ReadU32(uint32_t off) const;
    void WriteU32(uint32_t off, uint32_t v);
    uint8_t ReadU8(uint32_t off) const { return off < data.size() ? data[off] : 0; }
    void WriteU8(uint32_t off, uint8_t v);

private:
    std::wstring backedUpPath_;
};

// Min/max value a field accepts when typed in by hand.
void FieldRange(const Field& f, int64_t& lo, int64_t& hi);
