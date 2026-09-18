# Contributing

Use the macOS Command Line Tools and run `make test` before submitting changes.
Keep changes focused and include reproduction steps and relevant validation.
Do not run live-window probes unattended: they create visible temporary windows.

## Compatibility contracts

- Keep `CFBundleIdentifier` as **`local.knitborders.app`**. The previous
  `local.knitborders` identity failed menu-bar hosting on the development Mac;
  changing only the identity restored it. Do not revert it as part of a rename.
- Preserve existing preferences and the legacy Application Support paths.
- The default border width is 12 pt. A default change must not overwrite a user's
  saved selection. Keep the native menu and renderer defaults consistent.
- AppKit UI, tracked-window state and geometry updates belong on the main thread.
  Keep slow window-list collection off it. Preserve resize suppression and
  window lifecycle safeguards when changing rendering.
- A visible status-item flag or successful WindowServer transaction does not
  prove that pixels are visible. Document the limits of automated checks.

## Visual changes

Use `make catalogue` to inspect full windows and enlarged corners, plus 1x/2x
renderer checks from `make test`. When adding an app, update its rule, chart, catalogue metadata, collection count
checks, public collection page, and both PDF catalogues. Catalogue exports must
use built-in profiles without loading personal settings or PNG overrides.
Keep the SVG and native status-icon geometry
in sync. Use original artwork; do not add third-party reference images without
appropriate permission.

## Manual checks

Test menu actions, dragging, resizing from every edge, close/minimize/restore,
overlapping apps, and a display transfer in each direction. Include the macOS
version and display arrangement when reporting a tracking problem. Avoid
including window titles, private content or personal configuration in reports.

Optional probes can be built with `make bin/live-display`, `make bin/live-resize`
and `make bin/live-order`. Read each probe's source for its scope before running.
The display probe uses its own temporary window and a separate border connection.

## Before publishing a release

Run the tests and manual checks on the intended macOS versions. Update both the
bundle version in `AppInfo.plist` and command-line version in `src/main.c`.
Release builds must keep `ARCHS` and `DEPLOY` in the Makefile: without an explicit
`-mmacosx-version-min`, clang targets whatever macOS built it and the binary then
refuses to launch below that version, no matter what `LSMinimumSystemVersion` says.
Confirm with `vtool -show-build` that both slices report the intended `minos`.
Distribution signing and notarization are not configured by the local build.
Publish source with the license and attribution; do not upload local logs,
archives, personal settings, build products or developer signing credentials.

## Automatic colourways

`src/autoyarn.m` handles apps without built-in or personal rules. The window
owner PID selects the icon; decoding and colour extraction run on a serial
worker. Cache updates, chart generation and redraw callbacks run on the main
thread. Never capture a border pointer in an icon job: its window may close
before the job completes.

`tests/autoyarn.m` supplies synthetic icons to the same decoder and cache. It
checks contrast after softening, transparency, asynchronous completion, rule
precedence, mode changes, reloads, relaunches and cache/table limits without
requiring running apps. The native menu test checks generated-chart filtering.

Automatic sweaters use two yarns. Pattern shape is a stable hash of the app
name, independent of icon colour. Colourless icons retain the existing name-
based fallback. The cache holds 64 recent processes; a full chart table can
retain the icon colour without a motif until chart space is available.

The shared Zigzag uses icon colours for every app, the built-in collection
included, with `knit_zigzag_contrast()` choosing cream or a deeper shade.
Personal `apps.conf` colours always win, a user's own `zigzag.png` is never
recoloured, and a pale app without room for its own chart knits plain rather
than cream on cream. Owners turned away while every cache slot is decoding are
remembered and asked again, each once, as requests settle; never a redraw of
every window, which with more apps than slots would evict and re-request
forever. Icon sampling ignores faintly tinted greys (colour strength
`max − min` below 0.10) and needs about 1.7% of the icon in colour, so
black-and-white logos keep their fallback and small accents still count; this
applies in every mode. `make styles` regenerates the README comparison image;
its Zigzag colours are fixed in `tests/render.c` so it is the same on any Mac.

## Choosing apps in the menu

`app_allowed()` in `src/windows.c` is the only place a border can be refused,
and every path that creates one goes through it. `src/hidden.m` holds a default
(all apps on, or all off) plus the apps ticked the other way, keyed by bundle
identifier and resolved through `NSRunningApplication`, never from the
executable path. This is kept apart from the startup script's
`blacklist=`/`whitelist=`, so neither overwrites the other. A change calls
`windows_apply_app_filter()`, which removes and adds only the affected windows
rather than rebuilding every border, then settles focus again. The Apps menu
lists Dock apps, any app owning a window that could wear a sweater (worn now
or not), and any running app ticked the other way; that rule is
`knit_menu_apps()` and is tested directly. `tests/app_filter.m` drives the
real `windows.c` and `hidden.m` against a scripted WindowServer.
