# Window Sweaters for Windows

Knitted borders for your windows, now on Windows 10 and 11. 🧶
The same sweaters as the Mac app: 37 hand-picked colourways, patterns, and
colours borrowed from each app's icon for everything else.

## Get it

Pick the zip for your PC:

- **`windows-x64`** for most Intel and AMD PCs.
- **`windows-arm64`** for Arm PCs, such as Snapdragon laptops.

Not sure? Open **Settings → System → About** and look at **System type**.

Unzip it anywhere you like (for example `C:\Users\<you>\Apps\Window Sweaters`)
and open **WindowSweaters.exe**. A yarn icon appears in the notification area
next to the clock. If you can't see it, click the **^** arrow to show hidden
icons, and drag the yarn onto the taskbar to keep it there.

The app isn't code-signed yet, so Windows SmartScreen may warn you the first
time. Choose **More info → Run anyway**.

Window Sweaters needs Windows 10 version 1903 or later. It runs without
installing anything else and doesn't need administrator rights.

## Make yourself cosy

Click the yarn icon to change the pattern, border width, stitch size and how
background windows look, or to pause the sweaters. Tick **Start with Windows**
to have them back every time you sign in.

**Apps** lists every app with a window that could wear a sweater. Untick one to
leave it bare; it stays that way, even after a restart. **Turn Off for All
Apps** and then ticking a few gives sweaters only to the apps you choose.

**By App** gives each app its own sweater. **Zigzag** gives them all a matching
cream zigzag in colours taken from their icons.

## What's different on Windows

- Maximised and full-screen windows don't wear a sweater: there's no room
  around them. Restore the window and its sweater comes back.
- Sweaters hide while you drag a window's edge to resize it, and return when
  you let go. Moving a window keeps its sweater on.
- Windows 11 windows have round corners and their sweaters follow them. Snapped
  windows and all Windows 10 windows are square, and so are their sweaters.
- Apps are recognised by their program's own name (for example "Google
  Chrome", "Microsoft Word" or "Visual Studio Code"), which is also the name
  shown in the **Apps** menu.
- The optional startup script from the Mac app (`sweatersrc`) isn't used.

## Your own colourways

Open **Your Own Colourways → Edit Colourways** from the yarn menu. That opens
this file in Notepad:

```text
%APPDATA%\Window Sweaters\apps.conf
```

Add a line per app, using the name shown in the **Apps** menu:

```text
Claude = #D58561 atelier-claude
Microsoft Edge = #2F7BBF picnic
```

Names match the start of an app's name, ignoring capitals; the longest match
wins. Put your own PNG charts in **Open Patterns Folder**
(`%APPDATA%\Window Sweaters\charts`), then choose **Reload Colourways and
Patterns**.

You can also change settings from a command prompt while the app is running:

```bat
WindowSweaters.exe width=18 chart=zigzag inactive=dim
```

## Taking it off

Quit from the yarn menu, untick **Start with Windows** first if you ticked it,
then delete the folder you unzipped. To remove your settings and colourways
too, delete `%APPDATA%\Window Sweaters`.

## Build it yourself

You'll need [CMake](https://cmake.org) and Visual Studio 2022 or its Build
Tools, with the **Desktop development with C++** workload (and the ARM64 build
tools for Arm builds). From the repository folder:

```powershell
powershell -ExecutionPolicy Bypass -File windows\build.ps1
```

This builds both versions, runs the renderer tests, and puts the zips in
`outputs\`. Add `-Arch x64` or `-Arch ARM64` to build only one.

## Credits and license

Built on [JankyBorders](https://github.com/FelixKratz/JankyBorders) by Felix
Kratz, with thanks. Released under GPL-3.0; see `LICENSE` and `NOTICE.md`.
