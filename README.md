# psobb-regkey

Every stock PSOBB Blue Burst client keeps its settings in the same single place in
the Windows registry. This ASI plugin lets a client keep its settings in its own
place instead, so it is not sharing with — or in the way of — anything else on the
machine.

## Status — read this first

**Pre-release (0.1.0). This code has not yet been compiled with MSVC or run against a
client.** The addresses and byte patterns it relies on were confirmed by static
analysis of the client executable (see `docs/ADDRESSES.md`), and the source was
compile-checked with another compiler, but nothing here has yet been observed working
in a running game. Specifically, not yet shown:

- that the redirect takes effect in a running client;
- the copy and move paths, the one-time prompt, and restoring from the backup file;
- that the unfamiliar-build path recognises a build whose addresses really differ.

Until those are tested, use it on a copy of your client, not a live install.

## The shared location

```
HKEY_CURRENT_USER\Software\SonicTeam\PSOBB
```

Not one location per install — one location, full stop. Every stock Blue Burst client
reads and writes it: the same value names, the same settings, the same saved account
slot. Open `regedit` and you can see it for yourself. A client that stays there is in
a position to get in the way of other installs on the same machine.

## What this does

It changes the middle part of that path, so the client uses

```
HKEY_CURRENT_USER\Software\<your name>\PSOBB
```

That is the whole change. It works on an **unmodified** `psobb.exe`: the file on disk
is never touched, so its hash stays stock.

## What this does NOT do

- **No gameplay changes.** No rates, stats, items, rendering or timing. The only
  memory it writes is one 4-byte pointer.
- **No network access.** It links nothing from the Windows socket libraries.
- **No hooks.** No API hooking, no code patching, no trampolines.
- **No other registry keys.** Only the client's own settings key moves; Direct3D and
  other keys the client uses are untouched.
- **Never touches your saved login.** In every mode, the login and server values are
  never read, copied, backed up or removed.
- **Does nothing on a client it does not recognise.** Recognised, or completely inert.
- **Never writes to the shared location unless you set `mode = 1`** (below). With the
  default settings it never even opens it.

## Install

1. Build it (below), or take `psobb_regkey.asi` from a release.
2. Put `psobb_regkey.asi` in the client's `patches\` folder — the folder its ASI
   loader reads (the client needs the ASI loader, `patch.dll`, for any plugin to run).
3. Copy `dist\psobb_regkey.ini` into the same folder.
4. Launch once with `vendor` left blank and read `psobb_regkey.log` in the game folder
   (next to `psobb.exe`). See *Check your client first*.
5. Set `vendor` to your name and launch again.

## Check your client first

With `vendor` blank the plugin checks whether it recognises your `psobb.exe`, writes a
report to `psobb_regkey.log`, and changes nothing. The report says:

- whether the check **PASSED** or was **REFUSED**, and why;
- which path recognised it (`fast` for a known build, `slow` for an unfamiliar one);
- the addresses used and the strings found;
- the longest name you can use, and why;
- the client's build identity string.

**REFUSED means stop.** The plugin will do nothing on that client — which is safe, but
it also means it will not work there, and no setting changes that.

## Configuration — `psobb_regkey.ini`

| key | meaning | if it is wrong |
|---|---|---|
| `[registry] vendor` | your key name | blank → check-only (report) mode. Too long → refused, and the log names the limit (63 on a known build). Never shortened: a shortened name is a different key. Contains `\` or `/` → refused |
| `[migration] mode` | `0` nothing, `2` copy, `1` move | anything else (including words like `move`) → treated as `0`, logged, and left as you wrote it |
| `[migration] source` | where existing settings come from | defaults to `SonicTeam` |

No INI at all → the plugin does nothing and logs `no config, inactive`.

## Your existing settings

**Nothing is deleted by default.** With `mode = 0`, your old settings stay exactly
where they were. The client just stops reading them, so on first launch your key
bindings, controller layout, graphics and sound are back to defaults, and you sign in
once.

**To bring them over by hand:** in `regedit`, export
`HKEY_CURRENT_USER\Software\SonicTeam\PSOBB`, open the `.reg` file in Notepad, change
`SonicTeam` to your name in the one `[...]` line, delete the `ACCOUNT` and `PASSWORD`
lines, save, and double-click the file.

**Or let the plugin do it, once:**

- **`mode = 2` — copy.** These settings are copied to the new key: `CTRLBUF`,
  `GRAPHICCTRL`, `SOUNDCTRL`, `WINDOW_MODE`, `VSync`, `BitDepth`, `WORD_WRAP`,
  `FONT_JPN`, `FOCUS_SOUND`. The shared location is opened read-only and left exactly
  as it was.
- **`mode = 1` — move. Only for a client that has already been shipped to players
  using the shared location** and is now relocating. A new client never needs it. It
  **removes** those nine settings from the shared location after copying them. Before
  removing anything it:
  1. asks the player (below) — **No** means copy instead, and nothing is removed;
  2. writes a backup, `psobb-settings-backup-YYYY-MM-DD.reg`, in the game folder.
     **Double-clicking it puts the settings back.** It holds only the nine settings
     being moved — never the login. If the backup cannot be written, nothing is
     removed and the plugin copies instead.
  3. copies the settings and checks each one arrived intact;
  4. records that it has run;
  5. only then removes each copied setting, and only if it still matches what was
     copied. Every removed name is written to the log.

**It runs once.** Afterwards the plugin sets `mode` back to `0`. The real record of
"already done" is in the registry, at `HKEY_CURRENT_USER\Software\<your name>\psobb-regkey`
(`Migrated` = 1 moved, 2 copied), so putting `mode` back to 1 or 2 does **not** re-run
it — even if the INI could not be written. It also refuses to run if the new key
already holds any of the nine settings, so a player's live settings are never
overwritten. If you rename your key later, the new name starts fresh.

**The prompt players see with `mode = 1`** (shown once, default button No):

```
<your name> — one-time settings move

This copy of the game keeps its settings in its own place in the Windows
registry, rather than in the shared location that stock PSOBB clients use.

Your existing settings are still in that shared location: your controls and
key bindings, and your graphics, sound and window options. This will move
them across so they carry over.

Your saved login is not touched. It stays where it is, and you'll be asked
to sign in once.

Before anything is removed, a backup is saved to:

    <game folder>\psobb-settings-backup-YYYY-MM-DD.reg

Double-clicking that file puts the old settings back.

One thing worth knowing: if you also play on another PSOBB server from a
different folder, that client may be using some of these same shared
settings, and moving them could reset its controls. If you're not sure,
choose No — your settings will be copied instead, nothing will be removed,
and both will keep working.

Move the settings now?

    Yes  -  move them (a backup is saved first)
    No   -  copy them instead, and leave the originals alone

You'll only be asked this once.
```

**If you are an operator planning `mode = 1` for your players:** try `mode = 2` on a
few players first; tell players before it happens; and run a move yourself on a copy
of a real install, then double-click the backup and check the settings come back.

**Known limit:** a client that switches to a different product name at start-up (the
client has a built-in way to do this) keeps its settings under that product name, and
migration only looks under the product name found at load.

## How it works

The client builds its key name at start-up by copying three strings — a vendor
(`SONICTEAM`), a product (`PSOBB`) and a data folder — into fixed buffers, from three
pointers. The plugin's `load()` runs before that copy (the ASI loader calls it from the
executable's entry point), checks the client is one it recognises, and points the
vendor pointer at your name. The game then builds `Software\<your name>\PSOBB` itself.
One 4-byte write, at `0x009FF8CC` on known builds. Details and every address:
`docs/ADDRESSES.md`.

## Building

32-bit (Win32) only — the client is 32-bit and will not load a 64-bit plugin. Needs
Visual Studio with the **Desktop development with C++** workload. The project uses the
v145 toolset (Visual Studio 18); on an older Visual Studio, change *Platform Toolset*
in the project properties to the one you have.

**In Visual Studio:**

1. Open `psobb_regkey.slnx` (or `psobb_regkey.vcxproj` directly).
2. Set the configuration to **Release** and the platform to **x86** (Win32).
3. *Build → Build Solution*.
4. The plugin is `Release\psobb_regkey.asi`.

**From the command line** (no solution needed):

1. Open a normal Command Prompt in the repo folder.
2. Run `build.bat`.
3. The plugin is `build\psobb_regkey.asi`.

`build.bat` finds Visual Studio itself. If it cannot, set `VCVARS` to your
`vcvars32.bat` first, for example:

```
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
build.bat
```

Both builds use the static runtime (`/MT`), so players need no Visual C++
redistributable.

## Licence and provenance

MIT — see `LICENSE`. Written from scratch against a static disassembly of the client;
no code from other plugins. The addresses are facts about a binary, and the plugin
ships no game code.
