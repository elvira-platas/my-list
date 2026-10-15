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
        QAction *transparent = nullptr;
        for (auto *box : window.findChildren<QAction *>())
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
        auto *second = dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)));
        auto *fourth = dynamic_cast<TaskRow *>(list->itemWidget(list->item(3)));
        check(!first->focusButton->isEnabled(), "completed tasks cannot be current");
        second->focusButton->click();
        check(second->focusButton->isChecked(), "mark current task");
        fourth->focusButton->click();
        check(!second->focusButton->isChecked() && fourth->focusButton->isChecked(), "only one current task");
        fourth->focusButton->click();
        check(!fourth->focusButton->isChecked(), "clear current task by clicking again");
        second->focusButton->click();
        check(QJsonDocument::fromJson(read()).array().at(1).toObject().value("inProgress").toBool(), "current task saved");
        auto *restore = window.findChild<QToolButton *>("restoreTask");
        const QString firstHtml = first->editor->toHtml();
        first->removeButton->click();
        QTest::qWait(10);
        check(list->count() == 3 && !restore->isHidden(), "deletion offers restore");
        restore->click();
        first = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        check(list->count() == 4 && first->editor->toHtml() == firstHtml && first->checkBox->isChecked(), "restore position formatting and completion");
        second->removeButton->click();
        restore->click();
        second = dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)));
        check(second->focusButton->isChecked(), "restore current-task highlight");
        check(restore->isHidden(), "restore action consumed");
        check(QJsonDocument::fromJson(read()).array().size() == 4, "restoration saved");
        window.close();
    }
    {
        TodoWindow window;
        for (auto *box : window.findChildren<QAction *>())
            if (box->text() == "Transparent background") check(!box->isChecked(), "remember transparent preference");
        check(window.findChild<QListWidget *>("taskList")->count() == 4, "saved tasks reload");
        auto *list = window.findChild<QListWidget *>("taskList");
        auto *second = dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)));
        check(second->focusButton->isChecked(), "current task restored");
        second->checkBox->setChecked(true);
        check(!second->focusButton->isChecked() && !second->focusButton->isEnabled(), "completion clears current task");
        check(!QJsonDocument::fromJson(read()).array().at(1).toObject().value("inProgress").toBool(), "completion saves cleared highlight");
        window.close();
    }
    write(R"([{"html":"Only task","done":false}])");
    {
        TodoWindow window;
        auto *list = window.findChild<QListWidget *>("taskList");
        auto *restore = window.findChild<QToolButton *>("restoreTask");
        auto *compact = window.findChild<QToolButton *>("compactMode");
        compact->setChecked(true);
        auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        row->removeButton->click();
        check(!restore->isHidden(), "restore available in compact mode");
        restore->click();
        row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        check(list->count() == 1 && row->editor->toPlainText() == "Only task", "restore last task without extra blank");
        row->removeButton->click();
        auto *replacement = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        replacement->editor->insertPlainText("New work");
        restore->click();
        check(list->count() == 2, "keep edited replacement when restoring");
        check(dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)))->editor->toPlainText() == "New work", "new text preserved");
        window.close();
    }
    write(R"([{"html":"Done","done":true},{"html":"Working","done":false}])");
    {
        TodoWindow window;
        auto *list = window.findChild<QListWidget *>("taskList");
        auto *filter = window.findChild<QToolButton *>("completedTasks");
        check(filter->toolTip() == "Hide completed (1)", "completed count shown");
        filter->click();
        check(list->item(0)->isHidden() && !list->item(1)->isHidden(), "filter hides only completed tasks");
        auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)));
        row->checkBox->setChecked(true);
        check(list->item(1)->isHidden() && filter->toolTip() == "Show completed (2)", "completion updates filter and count");
        check(list->currentItem() == nullptr, "hidden task is not selected for deletion");
        window.close();
        check(QJsonDocument::fromJson(read()).array().size() == 2, "hidden tasks remain saved");
    }
    {
        TodoWindow window;
        auto *list = window.findChild<QListWidget *>("taskList");
        auto *filter = window.findChild<QToolButton *>("completedTasks");
        check(filter->isChecked() && list->item(0)->isHidden() && list->item(1)->isHidden(), "filter restored after restart");
        filter->click();
        check(!list->item(0)->isHidden() && !list->item(1)->isHidden(), "show completed restores visibility");
        auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(0)));
        row->removeButton->click();
        filter->click();
        window.findChild<QToolButton *>("restoreTask")->click();
        check(!list->item(0)->isHidden() && list->count() == 2, "restored completed task is visible");
        window.close();
    }
    write(R"([{"html":"<b><i>Formatted</i></b>","done":true},{"html":"Plain","done":false}])");
    {
        TodoWindow window;
        auto *list = window.findChild<QListWidget *>("taskList");
        window.findChild<QToolButton *>("completedTasks")->setChecked(true);
        window.findChild<QToolButton *>("allTasksLarger")->click();
        for (int i = 0; i < list->count(); ++i) {
            auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
            QTextCursor cursor(row->editor->document());
            cursor.movePosition(QTextCursor::NextCharacter);
            check(cursor.charFormat().fontPointSize() == 18, "global size includes hidden tasks");
            if (i == 0)
                check(cursor.charFormat().fontWeight() == QFont::Bold && cursor.charFormat().fontItalic(), "global size preserves bold and italic");
        }
        check(list->item(0)->isHidden(), "global size preserves filter");
        window.close();
    }
    {
        TodoWindow window;
        auto *list = window.findChild<QListWidget *>("taskList");
        auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(1)));
        QTextCursor cursor(row->editor->document());
        cursor.movePosition(QTextCursor::NextCharacter);
        check(cursor.charFormat().fontPointSize() == 18, "global size saved in task data");
        window.findChild<QToolButton *>("addTask")->click();
        row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(list->count() - 1)));
        check(row->editor->currentCharFormat().fontPointSize() == 18, "new tasks inherit size after restart");
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
