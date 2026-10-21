# Shihab Alaq Mods

متجر إضافات [شهاب ألق](https://shihab.app/). كل إضافة ملف C++ واحد في `mods/<id>.wh.cpp` بصيغة إضافات Windhawk،
ومعها بيانات المتجر بالعربية والإنجليزية في `store/<id>.json`.

The store of [Shihab Alaq](https://shihab.app/) mods. Each mod is one C++ file, `mods/<id>.wh.cpp`, in the Windhawk
mod format, with its store entry (Arabic and English texts, section, credits) in `store/<id>.json`.

## How the store is made / كيف يُبنى المتجر

1. A push to `main` runs `.github/workflows/build.yml` on Windows. It builds every mod for each architecture it
   runs on (32, 64, arm64) against the Shihab Alaq engine (the version in `ENGINE_VERSION`, downloaded from
   [shihab-alaq-engine](https://github.com/asaadassi/shihab-alaq-engine) and checked against its SHA-256).
2. The result goes to the **`store`** branch: `catalog.json`, `mods/<id>/<version>.wh.cpp` and
   `mods/<id>/<version>_<arch>.dll`, each with its SHA-256 in the catalog.
3. The publisher signs `catalog.json` on their own computer: `scripts\Sign-Catalog.ps1 -SigningKey <key>`. The app
   uses only a signed catalog, and installs only files that match their SHA-256.

## Adding a mod / إضافة إضافة

- `mods/<id>.wh.cpp` — the source (`@id` = the file name; raise `@version` for every change).
- `store/<id>.json` — `category` (`taskbar`, `start`, `explorer`, `desktop`, `windows`, `other`), `name` and
  `description` (`ar`, `en`), `author`, `license`, `basedOn` (the original, for an adapted mod), optional `preview`,
  optional `conflicts` (the ids of mods that change the same thing; the app warns before both run together).
  `group` (one of `store/groups.json`) and `order` (its place in the group).
  For the feature page, in plain words a home user understands (no versions, licences or technical terms):
  `summary` (`ar`, `en`: one short line) and `highlights` (`ar`, `en`: 2 to 5 short points on what it does and
  what you can choose).
- `store/groups.json` — the features the app shows: each group has `id`, `category`, `icon` (a Segoe Fluent Icons
  code point), `name`, `tagline` (a few words for its card) and `description` (`ar`, `en`), and `exclusive: true` when only one of its mods may run at a time (its mods then conflict with each other).
  The mods stay separate files: a crash turns off only the part that crashed, and each mod is injected only where it
  needs to be.

## Settings / الإعدادات

Every setting a mod shows has Arabic texts next to the English ones: `$name:ar`, `$description:ar`, and
`$options:ar` with the same values as `$options`. A setting a home user doesn't need carries `$_advanced: true`;
the app shows it under "More options". A setting that needs code or a format string (XAML styles, `%date%`
patterns, window class names, font or program names) carries `$_hidden: true`: the app does not show it but keeps and
saves its value. A text that holds a colour carries `$_color: true`: the app shows a palette instead of a text box.
Changing a mod's settings block raises its `@version`.

Pictures of a mod's choices go in its store entry, `optionImages`: `{"<flat setting name>": {"<value>": "<url>"}}`,
with `"true"` as the value for a switch. Each picture stays in its author's repository at a
`https://raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>` address pinned to a commit; CI fetches it once and
puts its SHA-256 and size in the catalog, and the app shows only a picture that matches.

## Licence

The store tooling is under the MIT licence (`LICENSE`); each mod is under its own licence (see `NOTICE.md`).
