xmow_setup - shader files for the X-Men Origins: Wolverine Switch port, built from YOUR OWN disc
============================================================================================

What it makes
  <output>\switch\XMOW\game_root\                (default.xex + WGame\, the game files the port reads)
  <output>\switch\XMOW\xmow_shaders.dslib        (270 MB)
  <output>\switch\XMOW\shadercache_dksh.bin      (570 MB)
  <output>\switch\XMOW\shadercache_dksh_fix.bin  (78 KB)
  <output>\switch\XMOW\shadercache_dksh_vs.bin   (7 MB)
Every file is checked against the SHA-256 of the release; the tool stops with an error if one differs.

You need
  - Windows 10 or 11, 64-bit.
  - Your Xbox 360 disc of X-Men Origins: Wolverine (Uncaged Edition) as a plain .iso (XDVDFS), or the extracted disc
    folder (default.xex + WGame\). Compressed images (CCI, GOD, ZAR) must be converted to .iso first.
  - About 10 GB of free disk space (7 GB of it is game_root) and 8 GB of RAM or more. It takes 1 to 2 hours; most of the
    time is shadercache_dksh.bin.

How to use
  Double-click xmow_setup.exe and answer the two questions (the image or folder, and the output folder), or run:
      xmow_setup.exe --iso "D:\path\to\your game.iso" --out "D:\XMOW-SD"
  Options: --disc <folder> instead of --iso, --no-game-root (do not copy the game files), --jobs N (fewer parallel
  processes if the PC runs out of memory), --start <stage> to resume after an error. xmow_setup.exe --help lists all.

Then
  1. Unzip the port release (XMOW.nro + xmow.toml in switch\XMOW).
  2. Copy <output>\switch to the root of the SD card, merging it with the release: switch\XMOW on the SD must hold
     XMOW.nro, xmow.toml, game_root\ and the 4 shader files.
  A log of the run is <output>\xmow_setup.log.

Licenses: licenses\ (NOTICES.txt says which part uses which). The tool contains no game data beyond what
licenses\NOTICES.txt describes; it only reads your disc.
