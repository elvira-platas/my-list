#define main todo_application_main
#include "../main.cpp"
#undef main
#include <QtTest/QTest>
#include <QTemporaryDir>
#include <cstdlib>

static void check(bool condition, const char *message)
{
    if (!condition) { qCritical("FAIL: %s", message); std::exit(1); }
}

int main(int argc, char **argv)
{
    QTemporaryDir sandbox;
    qputenv("XDG_DATA_HOME", sandbox.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", sandbox.path().toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("writer-todo-test");
    app.setOrganizationName("Regression");
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/tasks.json";
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QByteArray original = R"([{"html":"First","done":true},{"html":"Second","done":false},{"html":"Third","done":true}])";
    auto write = [&](const QByteArray &data) { QFile f(path); check(f.open(QIODevice::WriteOnly), "open fixture"); f.write(data); };
    auto read = [&] { QFile f(path); check(f.open(QIODevice::ReadOnly), "read fixture"); return f.readAll(); };
    write(original);
    {
        TodoWindow window;
        window.show();
        window.activateWindow();
        QTest::qWait(50);
        auto *list = window.findChild<QListWidget *>("taskList");
        check(list && list->count() == 3, "restore every task");
        check(read() == original, "loading completed tasks must not write file");
        auto *first = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        auto *last = dynamic_cast<TaskRow *>(list->itemWidget(list->item(2)));
        check(first->checkBox->isChecked() && last->checkBox->isChecked(), "restore completion");
        QCheckBox *transparent = nullptr;
        for (auto *box : window.findChildren<QCheckBox *>())
            if (box->text() == "Transparent background") transparent = box;
        check(transparent && transparent->isChecked(), "transparent by default");
        transparent->setChecked(false);
        first->editor->setFocus();
        QTest::qWait(10);
        check(list->currentItem() == list->item(0), "editing selects correct row");
        first->editor->selectAll();
        for (auto *button : window.findChildren<QToolButton *>())
            if (button->text() == "B") button->click();
        check(first->editor->currentCharFormat().fontWeight() == QFont::Bold, "format active editor");
        check(last->editor->currentCharFormat().fontWeight() != QFont::Bold, "other editor unaffected");
        QTest::keyClick(first->editor, Qt::Key_N, Qt::ControlModifier);
        check(list->count() == 4, "new-task shortcut from editor");
        check(QJsonDocument::fromJson(read()).array().size() == 4, "new task saved immediately");
        window.close();
    }
    {
        TodoWindow window;
        for (auto *box : window.findChildren<QCheckBox *>())
            if (box->text() == "Transparent background") check(!box->isChecked(), "remember transparent preference");
        check(window.findChild<QListWidget *>("taskList")->count() == 4, "saved tasks reload");
        window.close();
    }
    write("broken JSON");
    {
        TodoWindow window;
        check(!window.findChild<QListWidget *>("taskList")->isEnabled(), "prevent edits after load failure");
        window.close();
    }
    check(read() == "broken JSON", "preserve unreadable file");
    qInfo("All regression checks passed");
}
