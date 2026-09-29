# psobb-regkey — operator runbook

For a server operator putting this plugin into a client they ship. Follow it in
order. The README says what the plugin is; this says how to deploy it without
surprising your players.

---

## First run: "it says redirect applied, but nothing happened"

**This is the most common first-run confusion, and it usually means the plugin is
working.**

**The game never creates its settings key. It only reads it.** Settings are written
by a launcher or settings tool — yours, or the original launcher and its settings
program — never by the game itself.

So on a fresh setup you will see all three of these, and none of them is a fault:

- the log says
  ```
  redirect applied: settings key is HKCU\Software\<vendor>\PSOBB
  ```
- in `regedit`, **there is no `HKEY_CURRENT_USER\Software\<vendor>` key at all** (or
  it exists and is empty);
- in game, **every setting is at its default**.

The game is correctly reading your new key — there is just nothing in it yet.
**Fix it one of two ways:**

1. **Have your launcher or settings tool write to `HKCU\Software\<vendor>\PSOBB`**,
   with `<vendor>` spelled exactly as in `psobb_regkey.ini`. This is the permanent
   answer: whatever writes your players' settings must write to the key the game now
   reads. See [Point your launcher at the same key](#4-point-your-launcher-at-the-same-key).
2. **Or, on the first run, set `mode = 2` (copy)** with `source` set to the key the
   client used before — `SonicTeam` for a stock client, or whatever name your client
   used. The plugin copies the settings across once, creating the key. See
   [Copy or move](#5-copy-or-move-existing-settings--or-neither).

To confirm the redirect itself works: put a value you can see in the new key (for
example set `WINDOW_MODE` to `0` or `1` under `HKCU\Software\<vendor>\PSOBB`) and
launch. The game should follow it.

---

## 1. Is this for you?

- **You want your client to read its settings from its own registry key** instead of
  the shared `HKCU\Software\SonicTeam\PSOBB` that every stock client uses. That is
  the whole of what the plugin does. See the README, *What this does NOT do*.
- **Your client must load ASI plugins** (it has `patch.dll` and a `patches\` folder).
  Without that the plugin never runs.
- **If your client has never used the shared key, or has no players yet**, you need
  only `mode = 0` and your launcher writing the new key. Skip section 5's move
  material entirely.

## 2. Choose your vendor name

- It becomes the middle of the key: `HKCU\Software\<vendor>\PSOBB`. Use your server's
  own name, or something unmistakably yours.
- **Up to 63 characters on a recognised build.** On a build recognised only through the
  unfamiliar-build path, the limit may be as low as 9. **The log states the limit and
  why**; a name over it is refused, never shortened.
- Letters, digits, space, hyphen, underscore and dot. **No `\` or `/`** — refused.
- **Do not use `SonicTeam`** — that is the shared location; the plugin allows it but
  warns, because it defeats the point.
- **Pick it once.** The run-once record is kept under the vendor name, so renaming later
  makes the plugin treat the install as new (see section 7).

## 3. Check your build first — report mode

1. Put `psobb_regkey.asi` and `psobb_regkey.ini` in a **copy** of your client's
   `patches\` folder.
2. Leave `vendor` **blank**. Launch once, then quit.
3. Open `psobb_regkey.log` in the **game folder** (next to `psobb.exe`).

**Passing** looks like:
```
---- compatibility report ----
build identity : <Brand>Ver%d%d%d
gate           : PASSED
path           : fast
vendor slot    : 0x009FF8CC (holds 0x0097D0D8)
strings found  : vendor "SONICTEAM", product "PSOBB", data "data"
name cap       : 63 characters (destination buffer is 64 bytes on this known build)
------------------------------
report mode: vendor is not set, so nothing was written. Set vendor in the INI to redirect.
```
`path : slow` also passes — the build is unfamiliar but was recognised by its code
shape. **Please report a slow-path build**, with its report, so it can be added.

**Refused** looks like:
```
gate           : REFUSED
path           : none
Refusing to act: fast path: check 3: vendor string at 0x0097D0D8 is "...", not SONICTEAM; slow path: ...
```
**REFUSED means stop.** The plugin will do nothing on that client — safe, but it will
not work there, and no setting changes that. A common cause is a client whose vendor
name was already changed by editing the executable; such a client does not need this
plugin.

## 4. Point your launcher at the same key

**This is the step that decides whether your players see their settings.** The plugin
changes only where the **game reads**. Your launcher or settings tool must **write**
to:

```
HKEY_CURRENT_USER\Software\<vendor>\PSOBB
```

If the two disagree, the game reads a key nothing writes and players get defaults, with
no error anywhere. Check it once on your own machine: change a setting in your launcher,
then look for it under that key in `regedit`.

**Clients that use the built-in alternate product name.** The client has a built-in
mode that switches its product name from `PSOBB` to `PSOBB_Hangame` during start-up. A
client running that way reads `HKCU\Software\<vendor>\PSOBB_Hangame`, so your launcher
must write there — and the plugin's copy and move only look under `PSOBB`, so they do
not cover that mode.

## 5. Copy or move existing settings — or neither

Set in `[migration]`. The nine settings concerned are key and controller bindings,
graphics, sound and window options: `CTRLBUF`, `GRAPHICCTRL`, `SOUNDCTRL`,
`WINDOW_MODE`, `VSync`, `BitDepth`, `WORD_WRAP`, `FONT_JPN`, `FOCUS_SOUND`. **Login and
server values are never read, copied, backed up or removed, in any mode** — players sign
in once.

| mode | use it when | what happens |
|---|---|---|
| `0` | a new client, or your launcher will write fresh settings | nothing is copied or removed; the shared key is never even opened |
| `2` copy | you want existing settings to carry over, and **you are not trying to vacate the shared key** | the nine are copied into your key; the shared key is opened read-only and left exactly as it was |
| `1` move | **only for a client already shipped to players who have their settings in the shared key**, and you are relocating them | the player is asked; on **Yes** a backup is written, the nine are copied, then removed from the shared key |

**Move is a fleet-wide, once-in-a-client's-life operation.** A new deployment never
needs it. Before using it:

1. **Rehearse on your own machine**, on a copy of a real install: run `2`, then run `1`
   on another copy, **answer Yes, and actually restore the backup** (below). Do not push
   what you have not undone yourself.
2. **Dry run:** ship `mode = 2` to a handful of players first. It catches install paths,
   permissions and antivirus that one machine cannot.
3. **Tell players before it happens:** where settings are moving, that controls and
   graphics carry over, that they sign in once, that they will see a one-time prompt,
   and that a backup file is written into the game folder. **Say plainly that anyone
   who also plays on another PSOBB server should choose No.**
4. **Be around on the day** to walk someone through a restore.

**Your packer and updater must not ship `mode = 1` repeatedly** over players' INI files.
The plugin's registry record stops a repeat anyway, but do not lean on that.

## 6. What players see with move, and how to undo it

With `mode = 1` and settings present, the player sees a one-time prompt, default button
**No**. The exact text is in the README, *The prompt players see*.

- **No** (or the prompt cannot be shown): settings are **copied**, nothing is removed.
- **Yes**: a backup is written to the game folder as
  `psobb-settings-backup-YYYY-MM-DD.reg`, holding **only the settings being moved —
  never the login**. If the backup cannot be written, **nothing is removed** and it
  copies instead. Then the settings are copied, checked, recorded, and only then removed
  from the shared key, each one only if it still matches what was copied.

**Restoring from the backup:** double-click `psobb-settings-backup-YYYY-MM-DD.reg`,
accept the Windows prompt. The moved settings go back into the shared key exactly as
they were. That is what to walk a player through when **"my other PSOBB client reset
itself"** — the log lists exactly which values were removed. Restoring does not stop
the redirect; your client keeps reading its own key.

Tell players **not to delete the backup file** for a while.

## 7. Run-once

Copy and move run **once per vendor name**:

- After running, the plugin sets `mode` back to `0` in the INI.
- **The real record is in the registry:** `HKCU\Software\<vendor>\psobb-regkey`,
  value `Migrated` (`1` moved, `2` copied), with `MigratedOn`, `MigratedFrom` and
  `MigratedNote`. Setting `mode` back to `1` or `2` does **not** re-run it — even if the
  INI could not be rewritten (read-only file, protected folder, an updater restoring it).
- It **refuses** if your key already holds any of the nine settings, so a player's live
  settings are never overwritten.
- **To deliberately run it again:** delete the `psobb-regkey` subkey under your vendor
  key in `regedit`, then set `mode` again.
- **If the source key had nothing to copy**, it still records completion (`MigratedNote`
  says so) and stops looking. If you fix the source afterwards, delete the record to
  retry.
- **Renaming your vendor** starts fresh under the new name: there is no record there.

## 8. Troubleshooting from the log

The log is `psobb_regkey.log` in the game folder. It appends; past 1 MB it moves to
`psobb_regkey.log.old`.

| log says | means | do |
|---|---|---|
| *(no log file at all)* | the plugin never loaded | check it is in the folder your ASI loader reads (`patches\`) and the client has `patch.dll` |
| `no config, inactive` | no INI next to the plugin | put `psobb_regkey.ini` beside `psobb_regkey.asi` |
| `report mode: vendor is not set` | working as designed; nothing written | set `vendor` |
| `gate : REFUSED` / `Refusing to act: ...` | build not recognised | stop — see section 3 |
| `redirect applied: settings key is ...` but defaults in game | **the key is empty or missing** | see [First run](#first-run-it-says-redirect-applied-but-nothing-happened) |
| `Refusing: vendor is N characters; the limit on this build is ...` | name too long | shorten it in the INI |
| `Refusing: vendor contains '\'` | would create nested keys | remove `\` or `/` |
| `WARNING: vendor is SonicTeam` | you chose the shared key | choose your own name |
| `mode "..." is not recognised; treating as 0` | `mode` must be `0`, `1` or `2` — no words | fix the value |
| `nothing to migrate: source key absent` | nothing under `source` | check `source`; completion was recorded — see section 7 to retry |
| `migration refused: the destination already holds settings` | your key already has settings | expected on a second pass; nothing is overwritten |
| `mode=N requested but migration already recorded ...; ignoring` | the run-once record stopped a repeat | expected |
| `player chose No (or the prompt was unavailable): completing as a copy` | move became copy | expected when the player declines |
| `BACKUP COULD NOT BE WRITTEN ...` | move became copy; nothing removed | check the game folder is writable |
| `removed from source: NAME` | move removed that setting | the list a restore conversation needs |
| `left in place (changed since it was copied): NAME` | a value changed mid-move; not removed | harmless |
| `could not write mode = 0 back to the INI` | INI not writable | harmless — the registry record prevents a repeat |

## 9. What this does not do

The README's *What this does NOT do* is the list — read it before trusting the plugin
with your players. In short: one 4-byte write, no hooks, no network, no gameplay
changes, never touches saved logins, and nothing at all on a client it does not
recognise.
