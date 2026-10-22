# RE6 Save Editor

A small dependency free save editor for **Resident Evil 6 (PC/Steam)** `savedata.bin`, written in C++20/Win32.

- Skill points, owned skills, and equipped skill loadouts (Campaign, Mercenaries, Siege, Onslaught, Survivors, Predator)
- Character and costume unlocks, Mercenaries stage unlocks (including No Mercy), Serpent Emblems
- Mercenaries records (solo and duo) and per character inventory
- One click **Unlock Everything**
- Checksum is recalculated automatically, and a `.bak` backup is written before the first save

## Download

Grab `RE6SaveEditor-<version>-win64.zip` from the [Releases](../../releases) page, unzip, and run `RE6SaveEditor.exe`.
It's a single portable exe with no installer and no runtime dependencies (Windows 10 or later).

Or just compile it from source yourself.

## Linux (AppImage)

A native Qt-based Linux build is provided alongside the Windows version. The AppImage build is produced by GitHub Actions and does not require Wine.

- Download the latest successful **RE6SaveEditor-AppImage** artifact from [GitHub Actions](../../actions/workflows/linux-appimage.yml) while the build is in progress.
- Tagged releases automatically attach an `.AppImage` file to the GitHub release.
- Make the downloaded file executable and run it:

```sh
chmod +x RE6SaveEditor-*-x86_64.AppImage
./RE6SaveEditor-*-x86_64.AppImage
```

The Linux version edits unencrypted Resident Evil 6 PC/Steam `savedata.bin` files. It recalculates the checksum when saving and creates a `.bak` backup beside the original save on the first save to that path in the session. Console saves and encrypted saves are not supported.
