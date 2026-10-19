# Shihab Styler Mods

متجر إضافات [Shihab Styler](https://shihab.app/). كل إضافة ملف C++ واحد في `mods/<id>.wh.cpp` بصيغة إضافات Windhawk،
ومعها بيانات المتجر بالعربية والإنجليزية في `store/<id>.json`.

The store of [Shihab Styler](https://shihab.app/) mods. Each mod is one C++ file, `mods/<id>.wh.cpp`, in the Windhawk
mod format, with its store entry (Arabic and English texts, section, credits) in `store/<id>.json`.

## How the store is made / كيف يُبنى المتجر

1. A push to `main` runs `.github/workflows/build.yml` on Windows. It builds every mod for each architecture it
   runs on (32, 64, arm64) against the Shihab Styler engine (the version in `ENGINE_VERSION`, downloaded from
   [shihab-styler-engine](https://github.com/asaadassi/shihab-styler-engine) and checked against its SHA-256).
2. The result goes to the **`store`** branch: `catalog.json`, `mods/<id>/<version>.wh.cpp` and
   `mods/<id>/<version>_<arch>.dll`, each with its SHA-256 in the catalog.
3. The publisher signs `catalog.json` on their own computer: `scripts\Sign-Catalog.ps1 -SigningKey <key>`. The app
   uses only a signed catalog, and installs only files that match their SHA-256.

## Adding a mod / إضافة إضافة

- `mods/<id>.wh.cpp` — the source (`@id` = the file name; raise `@version` for every change).
- `store/<id>.json` — `category` (`taskbar`, `start`, `explorer`, `desktop`, `windows`, `other`), `name` and
  `description` (`ar`, `en`), `author`, `license`, `basedOn` (the original, for an adapted mod), optional `preview`.

## Licence

The store tooling is under the MIT licence (`LICENSE`); each mod is under its own licence (see `NOTICE.md`).
