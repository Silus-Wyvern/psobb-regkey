# Changelog

## Unreleased — first public release

First release of psobb-regkey: an ASI plugin that makes a PSOBB Blue Burst client read
its settings from its own registry key, `HKEY_CURRENT_USER\Software\<vendor>\PSOBB`,
instead of the shared `HKEY_CURRENT_USER\Software\SonicTeam\PSOBB`.

- **One guarded 4-byte write.** No hooks, no code patches, no network access, and
  `psobb.exe` on disk is never modified.
- **Fail closed.** Known builds are recognised by fixed checks; unfamiliar builds by the
  shape of the code that names the registry key, confirmed by the three strings it uses.
  Anything not positively recognised is left completely alone.
- **Report mode.** With no vendor name set, the plugin only checks the client and writes
  a compatibility report to `psobb_regkey.log`.
- **Copy and move** of nine named settings (bindings, graphics, sound, window options)
  from the old key, once. Move asks the player first, writes a `.reg` backup that restores
  on double-click, and removes only what it copied. **Login and server values are never
  read, copied, backed up or removed.**
- **Runs once**, recorded in the registry so an INI that cannot be rewritten does not
  cause a repeat.
- **Operator runbook**, `docs/OPERATORS.md`, including a first-run note: the game never
  creates its settings key, so your launcher (or a first-run copy) must.
- Builds in Visual Studio (`psobb_regkey.slnx`, Release, x86) or with `build.bat`.

See the README's *Status* section for what has and has not been tested.
