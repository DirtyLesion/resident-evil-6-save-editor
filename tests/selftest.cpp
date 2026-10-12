// Round-trip test: load a save, edit a few fields, save to a temp copy, reload and verify.
// Usage: selftest.exe <path-to-savedata.bin>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "../src/fields.h"
#include "../src/save.h"

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                            \
        }                                                          \
    } while (0)

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: selftest <savedata.bin>\n");
        return 2;
    }
    fs::path tmpDir = fs::temp_directory_path() / L"re6se_selftest";
    fs::create_directories(tmpDir);
    fs::path copy = tmpDir / L"savedata.bin";
    fs::copy_file(argv[1], copy, fs::copy_options::overwrite_existing);
    const std::vector<uint8_t> original = ReadAll(copy);

    SaveFile save;
    std::wstring err;
    CHECK(save.Load(copy.wstring(), err));
    CHECK(save.StoredChecksum() == save.ComputeChecksum());
    std::printf("checksum stored=0x%08X computed=0x%08X\n", save.StoredChecksum(), save.ComputeChecksum());

    // Every field must be in bounds and every page non-empty.
    auto pages = BuildPages();
    size_t fieldCount = 0;
    for (const auto& p : pages) {
        CHECK(!p.fields.empty());
        for (const auto& f : p.fields) {
            CHECK(f.offset + 4 <= save.data.size() || f.offset + 2 <= save.data.size());
            ++fieldCount;
        }
    }
    std::printf("%zu pages, %zu fields\n", pages.size(), fieldCount);

    // Unchanged save must round-trip byte-for-byte.
    CHECK(save.Save(copy.wstring(), err));
    CHECK(ReadAll(copy) == original);
    CHECK(fs::exists(copy.wstring() + L".bak"));

    // Edit: skill points, a bit flag, a weapon id and a signed ammo value.
    const Field& sp = pages[0].fields[0];
    const Field& leon = pages[1].fields[0];
    const Field& ada = pages[1].fields[24];
    Page* inv = nullptr;
    for (auto& p : pages)
        if (p.path == L"Inventory/Leon") inv = &p;
    CHECK(inv != nullptr);
    const Field& weapon = inv->fields[0];
    const Field& ammo = inv->fields[1];

    CHECK(save.Get(leon) == 1);
    save.Set(sp, 1234567);
    save.Set(ada, 1);
    save.Set(weapon, 266);
    save.Set(ammo, -1);
    CHECK(save.dirty);
    CHECK(save.Save(copy.wstring(), err));

    SaveFile reloaded;
    CHECK(reloaded.Load(copy.wstring(), err));
    CHECK(reloaded.StoredChecksum() == reloaded.ComputeChecksum());
    CHECK(reloaded.Get(sp) == 1234567);
    CHECK(reloaded.Get(ada) == 1);
    CHECK(reloaded.Get(leon) == 1);
    CHECK(reloaded.Get(weapon) == 266);
    CHECK(reloaded.Get(ammo) == -1);

    // Only the edited bytes and the checksum may differ.
    const auto& now = reloaded.data;
    size_t diffs = 0;
    for (size_t i = 0; i < now.size(); ++i) {
        if (now[i] == original[i]) continue;
        bool expected = (i >= 8 && i < 12) || (i >= sp.offset && i < sp.offset + 4) || i == ada.offset ||
                        (i >= weapon.offset && i < weapon.offset + 4);
        if (!expected) std::printf("unexpected change at 0x%zX\n", i);
        CHECK(expected);
        ++diffs;
    }
    std::printf("%zu bytes changed\n", diffs);

    // Unlock everything keeps checksum valid after save.
    UnlockEverything(reloaded);
    CHECK(reloaded.Save(copy.wstring(), err));
    SaveFile again;
    CHECK(again.Load(copy.wstring(), err));
    CHECK(again.StoredChecksum() == again.ComputeChecksum());
    for (const auto& f : pages[1].fields) CHECK(again.Get(f) == 1);
    for (const auto& p : pages)
        if (p.path == L"Skills/Owned Skills") {
            CHECK(p.fields.size() == 96);
            for (const auto& f : p.fields) CHECK(again.Get(f) == 3);
        }
    // Skills equipped in the sample must already be owned.
    for (uint32_t off = 1920; off < 1932; off += 4) CHECK(save.ReadU8(1812 + save.ReadU32(off)) == 3);

    // Garbage / encrypted-looking input is rejected.
    fs::path junk = tmpDir / L"junk.bin";
    {
        std::ofstream out(junk, std::ios::binary);
        std::vector<char> bytes(original.size());
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>((i * 2654435761u) >> 13);
        out.write(bytes.data(), bytes.size());
    }
    SaveFile bad;
    CHECK(!bad.Load(junk.wstring(), err));

    fs::remove_all(tmpDir);
    std::printf(failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
