# Shihab Alaq Mods — project rules

The public store of Shihab Alaq: mod sources in `mods/<id>.wh.cpp` (Windhawk mod format) and their store entries
in `store/<id>.json` (Arabic and English). Read README.md first.

- CI (`.github/workflows/build.yml`, Windows) builds every mod against the engine in `ENGINE_VERSION` and force-pushes
  the result to the `store` branch. Never edit the `store` branch by hand, except `catalog.json.sig`, which only the
  publisher makes on their computer (`scripts/Sign-Catalog.ps1`). Never put a signing key in this repository or in CI.
- Every change to a mod raises its `@version`; `@id` is the file name.
- Keep each mod's `@author` and `@license`, and `basedOn` / `source` in its store entry for an adapted mod (NOTICE.md).
- Store texts: Arabic and English only, plain words, no emojis.
- Git: no commits unless the user asks.
