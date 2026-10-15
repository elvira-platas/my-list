# My List

A small task list for KDE Plasma. Made for use in AR glasses with KWin VR and a head-controlled pointer. The interface is in English.

Tasks stay on your computer and save automatically. The background is transparent by default; appearance settings let you adjust it.

## Build and run

Requires Qt 6 development packages (Widgets and qmake), a C++17 compiler, and Make.

```sh
qmake6 writer-todo.pro
make -j2
./writer-todo
```

The executable is still named `writer-todo` so existing task data and settings continue to work.

## Use

- **+** adds a task; the checkbox marks it done; **×** deletes it.
- **▶** highlights one task to work on now.
- The **eye** button hides or shows completed tasks.
- The **corner arrows** switch compact mode on or off. KDE window decorations stay available.
- The **gear** opens background and whole-list text settings. The bottom controls format the active task or selected text.
- **Restore task** brings back the latest deleted task during the current session, with no timer.

Drag the dotted handle to reorder tasks. Keyboard shortcuts: `Ctrl+N` adds, `Ctrl+S` saves, and `Ctrl+Shift+Delete` deletes the selected task. Hover tooltips are disabled for head-gaze use.

## Known limitations

Window size is remembered, but KWin/VR may limit its height when reopening it on a monitor. A reported freeze during text formatting has not been conclusively diagnosed; regression checks pass after changes to text layout handling.

## Checks

Requires Qt Test. From a separate build directory:

```sh
qmake6 /path/to/my-list/tests/regression.pro
make -j2
QT_QPA_PLATFORM=offscreen ./regression
```

Checks use temporary task data and settings.

## How it was made

The code was generated and revised by OpenAI Codex, an AI coding assistant. Elvira Platas proposed the app, described how it should work, tested it in AR glasses, and requested changes. She did not write the code.

## License

[MIT License](LICENSE).
