# 09 — Packaging and release

## Never ship the game

Your release contains **your runtime and docs only**. The player supplies the game
(and a System file, if fonts are needed). Make that painless:

* **Find files by content, not by name.** The app = the file whose resource fork has
  your signature type (`HLE_APP_SIGNATURE`; the creator code's resource or `CODE`);
  the System file = the one with `FONT`s. Accept raw forks, AppleDouble and MacBinary.
* Search order: command-line folder → `$<APP>_GAME` → `game/` next to the exe →
  `<user data>/game` → the folder picked last time (remember it in
  `<user data>/game-folder.txt`) → ask with a native folder picker, explaining
  exactly which files are needed and how to get them from a `.sit`/disk image.
* README section "Getting the game files" with the `unar` and `hfsutils` recipes
  (see the ATC port's README for wording that worked).

## Builds

* **Windows**: CMake + MSVC, `WIN32` subsystem for release (no console), `/ENTRY:mainCRTStartup`
  to keep `main()`. A console dev build (`build.bat`) for logs.
* **macOS**: SDL2 built from source via `FetchContent` and linked statically, universal
  `arm64;x86_64`, deployment target 10.13, `.app` bundle with `Info.plist`, ad-hoc
  codesign (`codesign --force --deep -s -`). Unsigned/unnotarized → tell users to
  right-click → Open or remove the quarantine attribute.
* **Linux**: same SDL2 approach on ubuntu-22.04 runners (older glibc = wider
  compatibility); ship a tarball and an AppImage (linuxdeploy); `.desktop` file + icon.
  Native dialogs via `zenity`/`kdialog`, with a clear message if neither exists.
* One **GitHub Actions** workflow building all three on tag push (`v*`), uploading
  artifacts, and creating the release. The ATC port's `.github/workflows/release.yml`
  is a complete working example.
* A `VERSION` file → passed as a define → shown in the window title/About.

## Icons and branding

Draw your own icon (the ATC port generates pixel art with a small script); don't
reuse the game's icon resources in the release. At run time the window icon *may*
come from the user's own copy (`ICN#`/`icl8`), which is fine.

## Licensing

* Port code: pick a licence (MIT used here). Musashi is MIT; SoftFloat has its own
  permissive notice; SDL2 is zlib. Ship a `THIRD_PARTY_NOTICES.md` in every package.
* State clearly what is NOT included (game, fonts, ROM) and who owns it.
* If the game was freeware, quote its own distribution terms; if commercial, point
  to legitimate sources (the user's original disks, archives that host it with
  permission) and don't link to dubious downloads.

## Release lines

If you plan enhancements, split early:
* **v1.x — faithful port**: bug fixes only. Branch `v1`.
* **v2.x — remaster**: new renderer, widescreen, options. Branch `main`.
Fix porting bugs on v1 first, release, then merge v1 into main. CI publishes from
both; mark v1 releases as not-latest.

## Privacy and history hygiene

Before making a repo public, scan history for: game bytes, decompiler output,
extracted assets, copy-protection notes, personal paths/usernames, emails. If any are
in history, rewrite it (`git filter-repo`) and keep a private backup bundle. Use a
consistent public git identity (e.g. a GitHub noreply address).

## Release checklist

- [ ] Fresh machine test: unzip, run, pick the folder, play, quit, relaunch (settings persist)
- [ ] Save folder location documented per OS
- [ ] All three CI builds green; artifacts open
- [ ] README: download table, getting the game, controls (with host modifier mapping),
      what works / known limitations, build-from-source, how it works, credits, licence
- [ ] THIRD_PARTY_NOTICES included in packages
- [ ] Screenshots in `docs/screenshots` (of the port running)
- [ ] No game data anywhere in the repo or packages (`git ls-files` review)
