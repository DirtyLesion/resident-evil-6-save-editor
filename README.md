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