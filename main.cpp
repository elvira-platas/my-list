#include <QApplication>
#include <QAbstractTextDocumentLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSettings>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

class TaskList : public QListWidget
{
public:
    using QListWidget::QListWidget;

    void startDragging(QListWidgetItem *item)
    {
        setCurrentItem(item);
        QAbstractItemView::startDrag(Qt::MoveAction);
    }
};

class DragHandle : public QLabel
{
public:
    explicit DragHandle(QWidget *parent = nullptr)
        : QLabel(QStringLiteral("⠿"), parent)
    {
        setObjectName(QStringLiteral("dragHandle"));
        setAlignment(Qt::AlignCenter);
        setFixedSize(38, 38);
        setToolTip(QStringLiteral("Drag to reorder"));
        setCursor(Qt::OpenHandCursor);
    }

    std::function<void()> startDragCallback;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            pressPosition = event->pos();
            dragging = false;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging && (event->buttons() & Qt::LeftButton)
            && (event->pos() - pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            dragging = true;
            if (startDragCallback)
                startDragCallback();
            event->accept();
            return;
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragging = false;
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    QPoint pressPosition;
    bool dragging = false;
};

class TaskRow : public QWidget
{
public:
    explicit TaskRow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        checkBox = new QCheckBox(this);
        checkBox->setToolTip(QStringLiteral("Mark as completed"));
        // Keep the checkbox visually compact, while leaving a little extra
        // room around it for a head-controlled pointer to target.
        checkBox->setMinimumSize(30, 30);

        dragHandle = new DragHandle(this);

        removeButton = new QToolButton(this);
        removeButton->setObjectName(QStringLiteral("taskRemove"));
        removeButton->setText(QStringLiteral("×"));
        removeButton->setToolTip(QStringLiteral("Delete this task"));
        removeButton->setFixedSize(30, 30);
        removeButton->setCursor(Qt::PointingHandCursor);

        editor = new QTextEdit(this);
        editor->setAcceptRichText(true);
        editor->setPlaceholderText(QStringLiteral("Write a task…"));
        editor->setFrameStyle(QFrame::NoFrame);
        editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        editor->setMinimumHeight(28);
        editor->setFontPointSize(16);
        editor->document()->setDocumentMargin(2);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 5, 8, 5);
        layout->setSpacing(8);
        layout->addWidget(checkBox, 0, Qt::AlignVCenter);
        layout->addWidget(dragHandle, 0, Qt::AlignVCenter);
        layout->addWidget(editor, 1, Qt::AlignVCenter);
        layout->addWidget(removeButton, 0, Qt::AlignVCenter);

        connect(editor, &QTextEdit::textChanged, this, [this] {
            adjustHeight();
        });
    }

    void adjustHeight()
    {
        editor->document()->setTextWidth(qMax(1, editor->viewport()->width()));
        const int documentHeight = qRound(editor->document()->documentLayout()->documentSize().height());
        const int editorHeight = qMax(28, documentHeight + 8);
        editor->setFixedHeight(editorHeight);
        setMinimumHeight(qMax(52, editorHeight + 10));
    }

    void requestHeightUpdate()
    {
        if (heightUpdatePending)
            return;
        heightUpdatePending = true;
        QTimer::singleShot(0, this, [this] {
            heightUpdatePending = false;
            adjustHeight();
            if (sizeChangedCallback)
                sizeChangedCallback();
        });
    }

    std::function<void()> sizeChangedCallback;

    QCheckBox *checkBox = nullptr;
    DragHandle *dragHandle = nullptr;
    QTextEdit *editor = nullptr;
    QToolButton *removeButton = nullptr;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        requestHeightUpdate();
    }

private:
    bool heightUpdatePending = false;
};

class TodoWindow : public QMainWindow
{
public:
    TodoWindow()
    {
        setWindowTitle(QStringLiteral("My List"));
        resize(620, 700);
        setMinimumSize(420, 320);
        setAttribute(Qt::WA_TranslucentBackground);

        auto *panel = new QWidget(this);
        panel->setObjectName(QStringLiteral("panel"));
        auto *root = new QVBoxLayout(panel);
        root->setContentsMargins(18, 16, 18, 18);
        root->setSpacing(10);

        auto *heading = new QHBoxLayout;
        auto *title = new QLabel(QStringLiteral("My List"), panel);
        title->setObjectName(QStringLiteral("title"));
        heading->addWidget(title);
        heading->addStretch();

        transparent = new QCheckBox(QStringLiteral("Transparent background"), panel);
        transparent->setToolTip(QStringLiteral("Remove the window background for VR mode"));
        heading->addWidget(transparent);

        systemTheme = new QCheckBox(QStringLiteral("Use system theme"), panel);
        systemTheme->setToolTip(QStringLiteral("Use the current KDE color scheme"));
        systemTheme->setChecked(QSettings().value(QStringLiteral("useSystemTheme"), false).toBool());
        heading->addWidget(systemTheme);
        root->addLayout(heading);

        auto *formatBar = new QHBoxLayout;
        formatBar->setSpacing(5);

        addButton = makeButton(QStringLiteral("+"), QStringLiteral("Add a task"));
        removeButton = makeButton(QStringLiteral("−"), QStringLiteral("Remove the selected task"));
        boldButton = makeButton(QStringLiteral("B"), QStringLiteral("Bold"));
        italicButton = makeButton(QStringLiteral("I"), QStringLiteral("Italic"));
        boldButton->setCheckable(true);
        italicButton->setCheckable(true);

        fontSize = new QSpinBox(panel);
        fontSize->setObjectName(QStringLiteral("fontSize"));
        fontSize->setRange(8, 72);
        fontSize->setValue(16);
        fontSize->setSuffix(QStringLiteral(" pt"));
        fontSize->setToolTip(QStringLiteral("Font size — type a number or use the arrows"));
        fontSize->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        fontSize->setMinimumSize(96, 38);
        fontSize->setAlignment(Qt::AlignCenter);
        fontSize->setKeyboardTracking(false);

        formatBar->addWidget(addButton);
        formatBar->addWidget(removeButton);
        formatBar->addSpacing(12);
        formatBar->addWidget(boldButton);
        formatBar->addWidget(italicButton);
        formatBar->addWidget(fontSize);
        formatBar->addStretch();
        list = new TaskList(panel);
        list->setObjectName(QStringLiteral("taskList"));
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setSpacing(6);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setDragEnabled(true);
        list->setAcceptDrops(true);
        list->setDropIndicatorShown(true);
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        connect(list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
            saveTasks();
        });
        root->addWidget(list, 1);

        auto *hint = new QLabel(QStringLiteral("Ctrl+N — new task    Ctrl+S — save    Delete — remove"), panel);
        hint->setObjectName(QStringLiteral("hint"));
        root->addWidget(hint);
        root->addLayout(formatBar);

        setCentralWidget(panel);
        applyStyle();

        connect(addButton, &QPushButton::clicked, this, [this] { addTask(); });
        connect(removeButton, &QPushButton::clicked, this, [this] { removeCurrentTask(); });
        connect(boldButton, &QPushButton::clicked, this, [this] { toggleBold(); });
        connect(italicButton, &QPushButton::clicked, this, [this] { toggleItalic(); });
        connect(fontSize, qOverload<int>(&QSpinBox::valueChanged), this, [this](int size) {
            applyFormat([size](QTextCharFormat &format) { format.setFontPointSize(size); });
        });
        connect(transparent, &QCheckBox::toggled, this, [this] { applyStyle(); });
        connect(systemTheme, &QCheckBox::toggled, this, [this](bool enabled) {
            QSettings().setValue(QStringLiteral("useSystemTheme"), enabled);
            applyStyle();
        });
        connect(qApp, &QApplication::paletteChanged, this, [this] {
            if (systemTheme->isChecked())
                applyStyle();
        });

        loadTasks();
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        saveTasks();
        QMainWindow::closeEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->matches(QKeySequence::Save)) {
            saveTasks();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::New)) {
            addTask();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Delete && !currentEditor()) {
            removeCurrentTask();
            event->accept();
            return;
        }
        QMainWindow::keyPressEvent(event);
    }

private:
    QToolButton *makeButton(const QString &text, const QString &tip)
    {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setFixedHeight(32);
        return button;
    }

    QTextEdit *currentEditor() const
    {
        if (list->currentItem()) {
            if (auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->currentItem())))
                return row->editor;
        }

        for (int i = 0; i < list->count(); ++i) {
            auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
            if (row && row->editor->hasFocus())
                return row->editor;
        }
        return nullptr;
    }

    TaskRow *currentRowWidget() const
    {
        if (!list->currentItem())
            return nullptr;
        return dynamic_cast<TaskRow *>(list->itemWidget(list->currentItem()));
    }

    void addTask(const QString &html = QString())
    {
        auto *item = new QListWidgetItem(list);
        auto *row = new TaskRow;
        if (!html.isEmpty())
            row->editor->setHtml(html);
        row->adjustHeight();
        item->setSizeHint(QSize(0, row->minimumHeight()));
        list->addItem(item);
        list->setItemWidget(item, row);
        list->setCurrentItem(item);
        row->editor->setFocus();
        row->dragHandle->startDragCallback = [this, item] {
            list->startDragging(item);
        };
        row->sizeChangedCallback = [item, row] {
            item->setSizeHint(QSize(0, row->minimumHeight()));
        };
        connect(row->removeButton, &QToolButton::clicked, this, [this, item] {
            removeTask(item);
        });

        connect(row->editor, &QTextEdit::textChanged, this, [this, item, row] {
            item->setSizeHint(QSize(0, row->minimumHeight()));
            saveTasks();
        });
        connect(row->checkBox, &QCheckBox::toggled, this, [this] { saveTasks(); });

        // Qt finishes laying out restored rich text on the next event loop
        // pass. Recalculate once then so every saved line is visible.
        row->requestHeightUpdate();
    }

    void removeCurrentTask()
    {
        if (!list->currentItem())
            return;
        removeTask(list->currentItem());
    }

    void removeTask(QListWidgetItem *item)
    {
        if (!item)
            return;
        delete list->takeItem(list->row(item));
        if (list->count() == 0)
            addTask();
        saveTasks();
    }

    template <typename Formatter>
    void applyFormat(Formatter formatter)
    {
        auto *editor = currentEditor();
        if (!editor)
            return;
        QTextCursor cursor = editor->textCursor();
        QTextCharFormat format = cursor.charFormat();
        formatter(format);
        cursor.mergeCharFormat(format);
        editor->setCurrentCharFormat(format);
        if (auto *row = currentRowWidget()) {
            row->adjustHeight();
            if (list->currentItem())
                list->currentItem()->setSizeHint(QSize(0, row->minimumHeight()));
        }
        editor->setFocus();
        saveTasks();
    }

    void toggleBold()
    {
        applyFormat([](QTextCharFormat &format) {
            format.setFontWeight(format.fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
        });
    }

    void toggleItalic()
    {
        applyFormat([](QTextCharFormat &format) { format.setFontItalic(!format.fontItalic()); });
    }

    QString dataPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tasks.json");
    }

    void saveTasks()
    {
        QDir().mkpath(QFileInfo(dataPath()).absolutePath());
        QJsonArray tasks;
        for (int i = 0; i < list->count(); ++i) {
            auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
            if (!row)
                continue;
            QJsonObject task;
            task[QStringLiteral("html")] = row->editor->toHtml();
            task[QStringLiteral("done")] = row->checkBox->isChecked();
            tasks.append(task);
        }
        QFile file(dataPath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(tasks).toJson(QJsonDocument::Indented));
    }

    void loadTasks()
    {
        QFile file(dataPath());
        if (!file.open(QIODevice::ReadOnly)) {
            addTask();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        const QJsonArray tasks = document.array();
        for (const QJsonValue &value : tasks) {
            const QJsonObject task = value.toObject();
            addTask(task.value(QStringLiteral("html")).toString());
            if (auto *row = currentRowWidget())
                row->checkBox->setChecked(task.value(QStringLiteral("done")).toBool());
        }
        if (list->count() == 0)
            addTask();
    }

    void applyStyle()
    {
        const bool clear = transparent->isChecked();
        const bool useSystem = systemTheme->isChecked();
        const QPalette palette = QApplication::palette();
        const QColor textColor = useSystem ? palette.color(QPalette::WindowText) : QColor(Qt::white);
        const QColor baseColor = useSystem ? palette.color(QPalette::Base) : QColor(10, 15, 25);

        auto rgba = [](QColor color, int alpha) {
            color.setAlpha(alpha);
            return QStringLiteral("rgba(%1, %2, %3, %4)")
                .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
        };

        const QString panelBackground = clear
            ? QStringLiteral("rgba(0, 0, 0, 0)")
            : (useSystem ? rgba(palette.color(QPalette::Window), 245)
                         : QStringLiteral("rgba(30, 34, 45, 245)"));
        const QString rowBackground = useSystem
            ? rgba(palette.color(QPalette::AlternateBase), clear ? 145 : 220)
            : (clear ? QStringLiteral("rgba(30, 34, 45, 125)")
                     : QStringLiteral("rgba(60, 66, 82, 220)"));
        const QString controlBackground = useSystem
            ? rgba(palette.color(QPalette::Button), 245)
            : panelBackground;
        const QString text = rgba(textColor, 255);
        const QString hint = rgba(textColor, 150);
        const QString border = rgba(textColor, 75);
        const QString indicatorBackground = rgba(baseColor, 180);
        const QString arrowUp = useSystem && textColor.lightness() > 128
            ? QStringLiteral(":/icons/arrow-up-dark.svg") : QStringLiteral(":/icons/arrow-up.svg");
        const QString arrowDown = useSystem && textColor.lightness() > 128
            ? QStringLiteral(":/icons/arrow-down-dark.svg") : QStringLiteral(":/icons/arrow-down.svg");

        QString style = QStringLiteral(
            "QWidget#panel { background: %1; border-radius: 16px; }"
            "QLabel#title { color: %3; font-size: 22px; font-weight: 600; }"
            "QLabel#hint { color: %4; font-size: 11px; }"
            "QListWidget#taskList { background: transparent; }"
            "QListWidget#taskList::item { background: %2; border-radius: 10px; }"
            "QTextEdit { color: %3; background: transparent; font-size: 16px; }"
            "QCheckBox { color: %3; border-radius: 8px; }"
            "QCheckBox:hover { background: rgba(255, 79, 154, 45); }"
            "QToolButton, QSpinBox { color: %3; background: %5; border: 1px solid %6; border-radius: 7px; padding: 4px 8px; }"
            "QToolButton:checked { background: %5; border: 1px solid rgb(255, 79, 154); }"
            "QToolButton:hover, QSpinBox:hover { background: rgba(255, 79, 154, 75); border: 1px solid rgb(255, 79, 154); }"
            "QLabel#dragHandle { color: %4; background: transparent; border-radius: 8px; font-size: 22px; }"
            "QLabel#dragHandle:hover { color: %3; background: rgba(255, 79, 154, 100); }"
            "QToolButton#taskRemove { color: %4; background: transparent; border: 1px solid transparent; border-radius: 8px; font-size: 22px; padding: 0px; }"
            "QToolButton#taskRemove:hover { color: %3; background: rgba(255, 79, 154, 130); border: 1px solid rgb(255, 79, 154); }"
            "QSpinBox#fontSize { padding: 2px 30px 2px 8px; }"
            "QSpinBox#fontSize::up-button, QSpinBox#fontSize::down-button { width: 28px; border-left: 1px solid %6; }"
            "QSpinBox#fontSize::up-button { subcontrol-position: top right; image: url(%8); }"
            "QSpinBox#fontSize::down-button { subcontrol-position: bottom right; image: url(%9); }"
            "QSpinBox#fontSize::up-button:hover, QSpinBox#fontSize::down-button:hover { background: rgba(255, 79, 154, 130); border-left: 1px solid rgb(255, 79, 154); }"
            "QCheckBox::indicator { width: 20px; height: 20px; border: 2px solid %6; border-radius: 5px; background: %7; }"
            "QCheckBox::indicator:hover { border: 2px solid rgb(255, 79, 154); background: rgba(255, 79, 154, 130); }"
            "QCheckBox::indicator:checked { border: 2px solid rgba(255, 79, 154, 220); background: transparent; image: url(:/icons/checkmark.svg); }"
            "QCheckBox::indicator:checked:hover { border: 2px solid rgb(255, 79, 154); background: rgba(255, 79, 154, 130); image: url(:/icons/checkmark.svg); }"
        );
        setStyleSheet(style.arg(panelBackground).arg(rowBackground).arg(text).arg(hint)
                          .arg(controlBackground).arg(border).arg(indicatorBackground)
                          .arg(arrowUp).arg(arrowDown));
    }

    TaskList *list = nullptr;
    QCheckBox *transparent = nullptr;
    QCheckBox *systemTheme = nullptr;
    QToolButton *addButton = nullptr;
    QToolButton *removeButton = nullptr;
    QToolButton *boldButton = nullptr;
    QToolButton *italicButton = nullptr;
    QSpinBox *fontSize = nullptr;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("writer-todo"));
    app.setApplicationDisplayName(QStringLiteral("My List"));
    app.setOrganizationName(QStringLiteral("Elvira"));

    TodoWindow window;
    window.show();
    return app.exec();
}
