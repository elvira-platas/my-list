# My List

A small task list for KDE Plasma and KWin VR mode.

## Features in the first version

- regular tasks with checkboxes;
- bold and italic text;
- font size from 8 to 72 pt;
- transparent background enabled by default for AR/VR, with your choice remembered;
- hide/show completed tasks with a count, including in compact mode; the filter is remembered;
- Restore task undoes the latest deletion during the current session, without a timer, including in compact mode;
- mark one task as current with ▶; click again to clear, or complete the task (saved across restarts);
- appearance settings in a gear-button menu, including task background opacity and text size for all tasks;
- global text size preserves bold/italic and becomes the default for new tasks;
- larger controls for head-controlled pointers;
- window size remembered after closing; window placement remains controlled by KDE/AR;
- the corner-arrow button switches between compact and full view (your choice is remembered);
- atomic automatic local saving with visible error messages;
- Ctrl+N to add, Ctrl+S to save, Ctrl+Shift+Delete to remove the current task.

## Authorship

This project was created from an idea and request by Elvira Platas to make everyday work easier.

The source code for this version was generated and refined with OpenAI Codex (GPT-5) under the direction of the project author.

## Building

Run the following commands in this directory:

```bash
qmake6 writer-todo.pro
make -j2
./writer-todo
```

The task list is saved in KDE's application data directory.

## License

This project is distributed under the MIT License. See the [LICENSE](LICENSE) file for the full text.

## Regression checks

Run in a separate build directory (requires Qt Test):

```bash
mkdir -p /tmp/writer-todo-regression
cd /tmp/writer-todo-regression
qmake6 /path/to/writer-todo/tests/regression.pro
make -j2
QT_QPA_PLATFORM=offscreen ./regression
```

Tests use temporary data and settings, leaving your task list untouched.
