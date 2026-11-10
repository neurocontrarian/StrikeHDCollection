Strike Remastered Collection HD - Linux build (Desert Strike)
=============================================================

Unofficial Linux build of https://github.com/Icedomega13/StrikeHDCollection
(v0.8.2), with the changes from https://github.com/neurocontrarian/StrikeHDCollection
(frame pacing synced to the display, diagnostics overlay, CRT filter).
This build contains Desert Strike only.

No game data is included: you need your own Desert Strike (Mega Drive / Genesis)
ROM dump. It is checked by SHA-1:
  d7e7d8c358eb845b84fb08f904cc0b95d0a4053d

Requirements
- Linux x86_64 with glibc 2.35 or newer (Ubuntu 22.04, Debian 12,
  Fedora 36, Batocera 44 and later).
- OpenGL drivers (the software renderer works too, without the CRT filter).
- Everything else (SDL3) is built in.

Run
1. Put your ROM in the roms/ folder next to the program (any file name),
   or start the game and use Add ROM.
2. Run ./StrikeCollectionHD
Settings, saves and a copy of the ROM go to ~/.local/share/DesertStrikeHD/DesertStrikeHD/.

Batocera (x86_64)
1. Copy this folder to /userdata/system/strikehd/ (the program must be
   /userdata/system/strikehd/StrikeCollectionHD) and your ROM to
   /userdata/system/strikehd/roms/.
2. Copy batocera/StrikeHD.sh to /userdata/roms/ports/.
3. Game Settings > Update Gamelists: the game is in Ports.
   The log goes to /userdata/system/logs/strikehd.log.
On NVIDIA cards, Batocera's compositor (picom) tears fullscreen games;
StrikeHD.sh stops it while the game runs and starts it again afterwards.

Licenses
The project's code is under the MIT license (LICENSE.txt). Third-party
components keep their own licenses (licenses/): SDL3 (zlib), Musashi (MIT),
SoftFloat, ymfm (BSD-3), z80 (MIT), stb (MIT / public domain),
Chakra Petch font (SIL OFL 1.1), Lola Retro CRT shaders (MIT).

Desert Strike, Jungle Strike and Urban Strike are trademarks of Electronic Arts.
This is an unofficial fan project, not affiliated with or endorsed by EA or Sega.
