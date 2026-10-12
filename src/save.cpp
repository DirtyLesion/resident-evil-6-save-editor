#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "save.h"

#include <windows.h>

#include <cstring>
#include <fstream>
#include <iterator>

namespace {

constexpr uint32_t kHeaderSize = 0x18;
// Last offset any field touches is the end of the 8th loadout block.
constexpr uint32_t kMinSize = 34028 + 8 * 112;

uint16_t ReadU16(const std::vector<uint8_t>& d, uint32_t off) {
    uint16_t v = 0;
    if (off + 2 <= d.size()) std::memcpy(&v, &d[off], 2);
    return v;
}

void WriteU16(std::vector<uint8_t>& d, uint32_t off, uint16_t v) {
    if (off + 2 <= d.size()) std::memcpy(&d[off], &v, 2);
}

}  // namespace

uint32_t SaveFile::ReadU32(uint32_t off) const {
    uint32_t v = 0;
    if (off + 4 <= data.size()) std::memcpy(&v, &data[off], 4);
    return v;
}

void SaveFile::WriteU32(uint32_t off, uint32_t v) {
    if (off + 4 <= data.size()) std::memcpy(&data[off], &v, 4);
}

void SaveFile::WriteU8(uint32_t off, uint8_t v) {
    if (off < data.size()) data[off] = v;
}

uint32_t SaveFile::ComputeChecksum() const {
    uint32_t sum = 0;
    for (uint32_t off = kHeaderSize; off + 4 <= data.size(); off += 4) sum += ReadU32(off);
    return sum;
}

bool SaveFile::Load(const std::wstring& file, std::wstring& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = L"Could not open the file.";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    if (bytes.size() < kMinSize) {
        error = L"The file is too small (" + std::to_wstring(bytes.size()) +
                L" bytes) to be a Resident Evil 6 save.";
        return false;
    }
    uint32_t declared = 0;
    std::memcpy(&declared, &bytes[4], 4);
    if (declared != bytes.size() || bytes.size() % 4 != 0) {
        error = L"The header's size field doesn't match the file size.\n\n"
                L"This doesn't look like an unencrypted Resident Evil 6 PC save. "
                L"If it's encrypted (e.g. a console save), it needs to be decrypted first.";
        return false;
    }

    data = std::move(bytes);
    if (path != file) backedUpPath_.clear();
    path = file;
    dirty = false;
    return true;
}

bool SaveFile::Save(const std::wstring& file, std::wstring& error) {
    WriteU32(8, ComputeChecksum());

    if (backedUpPath_ != file && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::wstring bak = file + L".bak";
        if (!CopyFileW(file.c_str(), bak.c_str(), FALSE)) {
            error = L"Could not create backup file:\n" + bak;
            return false;
        }
    }
    backedUpPath_ = file;

    std::wstring tmp = file + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out) {
            error = L"Could not write:\n" + tmp;
            return false;
        }
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        error = L"Could not replace:\n" + file;
        return false;
    }
    path = file;
    dirty = false;
    return true;
}

int64_t SaveFile::Get(const Field& f) const {
    switch (f.type) {
        case FieldType::U8: return ReadU8(f.offset);
        case FieldType::Flag8: return ReadU8(f.offset) != 0;
        case FieldType::Bits8: return (ReadU8(f.offset) & f.mask) != 0;
        case FieldType::Bits32: return (ReadU32(f.offset) & f.mask) != 0;
        case FieldType::I16: return static_cast<int16_t>(ReadU16(data, f.offset));
        case FieldType::U32:
        case FieldType::Hex32: return ReadU32(f.offset);
        case FieldType::Weapon: return ReadU16(data, f.offset) & 0x0FFF;
    }
    return 0;
}

void SaveFile::Set(const Field& f, int64_t v) {
    if (f.readOnly) return;
    switch (f.type) {
        case FieldType::U8: WriteU8(f.offset, static_cast<uint8_t>(v)); break;
        case FieldType::Flag8: WriteU8(f.offset, v ? 1 : 0); break;
        case FieldType::Bits8: {
            uint8_t b = ReadU8(f.offset);
            WriteU8(f.offset, static_cast<uint8_t>(v ? (b | f.mask) : (b & ~f.mask)));
            break;
        }
        case FieldType::Bits32: {
            uint32_t w = ReadU32(f.offset);
            WriteU32(f.offset, v ? (w | f.mask) : (w & ~f.mask));
            break;
        }
        case FieldType::I16: WriteU16(data, f.offset, static_cast<uint16_t>(static_cast<int16_t>(v))); break;
        case FieldType::U32:
        case FieldType::Hex32: WriteU32(f.offset, static_cast<uint32_t>(v)); break;
        case FieldType::Weapon: {
            uint16_t flags = ReadU16(data, f.offset) & 0xF000;
            WriteU16(data, f.offset, static_cast<uint16_t>(flags | (v & 0x0FFF)));
            break;
        }
    }
    dirty = true;
}

void FieldRange(const Field& f, int64_t& lo, int64_t& hi) {
    switch (f.type) {
        case FieldType::U8: lo = 0; hi = 0xFF; break;
        case FieldType::Flag8:
        case FieldType::Bits8:
        case FieldType::Bits32: lo = 0; hi = 1; break;
        case FieldType::I16: lo = -32768; hi = 32767; break;
        case FieldType::U32:
        case FieldType::Hex32: lo = 0; hi = 0xFFFFFFFFll; break;
        case FieldType::Weapon: lo = 0; hi = 0x0FFF; break;
    }
}
