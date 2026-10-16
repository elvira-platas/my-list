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
#include <QAction>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStyle>
#include <QStandardPaths>
#include <QSettings>
#include <QLockFile>
#include <QMessageBox>
#include <QSaveFile>
#include <QShortcut>
#include <QSignalBlocker>
#include <QJsonParseError>
#include <QTextCharFormat>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <optional>
#include <QPointer>
#include <QScopedValueRollback>
#include <QWindow>
#include <cmath>

// Settings are part of the application surface, not a separate compositor popup.
class SettingsPanel : public QWidget
{
public:
    explicit SettingsPanel(QWidget *parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("settingsPanel"));
        setFocusPolicy(Qt::StrongFocus);
        content = new QVBoxLayout(this);
        content->setContentsMargins(8, 8, 8, 8);
        content->setSpacing(0);
        hide();
    }

    QAction *addToggle(const QString &text)
    {
        auto *action = new QAction(text, this);
        action->setCheckable(true);
        auto *box = new QCheckBox(text, this);
        box->setMinimumHeight(64);
        content->addWidget(box);
        connect(box, &QCheckBox::toggled, action, &QAction::setChecked);
        connect(action, &QAction::toggled, box, &QCheckBox::setChecked);
        connect(action, &QAction::changed, box, [action, box] {
            box->setToolTip(action->toolTip());
        });
        return action;
    }

    void addControl(QWidget *control) { content->addWidget(control); }
    QColor surface = QColor(20, 23, 31);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(surface);
        painter.drawRoundedRect(QRectF(rect()), 12, 12);
    }

private:
    QVBoxLayout *content;
};

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
        setFixedSize(44, 44);
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
        setObjectName(QStringLiteral("taskRow"));
        setAttribute(Qt::WA_StyledBackground);
        setProperty("inProgress", false);
        focusButton = new QToolButton(this);
        focusButton->setObjectName(QStringLiteral("taskFocus"));
        focusButton->setText(QStringLiteral("▶"));
        focusButton->setCheckable(true);
        focusButton->setFixedSize(44, 44);
        focusButton->setToolTip(QStringLiteral("Work on this task now"));
        focusButton->setAccessibleName(QStringLiteral("Current task"));

        checkBox = new QCheckBox(this);
        checkBox->setToolTip(QStringLiteral("Mark as completed"));
        // Keep the checkbox visually compact, while leaving a little extra
        // room around it for a head-controlled pointer to target.
        checkBox->setMinimumSize(44, 44);

        dragHandle = new DragHandle(this);

        removeButton = new QToolButton(this);
        removeButton->setObjectName(QStringLiteral("taskRemove"));
        removeButton->setText(QStringLiteral("×"));
        removeButton->setToolTip(QStringLiteral("Delete this task\nCtrl+Shift+Delete — delete the selected task"));
        removeButton->setFixedSize(44, 44);
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
        layout->addWidget(focusButton, 0, Qt::AlignVCenter);
        layout->addWidget(removeButton, 0, Qt::AlignVCenter);

        connect(editor, &QTextEdit::textChanged, this, [this] {
            requestHeightUpdate();
        });
    }

    void setInProgress(bool active)
    {
        const QSignalBlocker blocker(focusButton);
        focusButton->setChecked(active);
        focusButton->setToolTip(active ? QStringLiteral("Stop highlighting this task")
                                      : QStringLiteral("Work on this task now"));
        setProperty("inProgress", active);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }

    void adjustHeight()
    {
        // QTextEdit owns document width; changing it here can trigger layout loops.
        const int documentHeight = int(std::ceil(editor->document()->documentLayout()->documentSize().height()));
        const int editorHeight = qMax(28, documentHeight + 8);
        if (editor->height() != editorHeight)
            editor->setFixedHeight(editorHeight);
        const int rowHeight = qMax(54, editorHeight + 10);
        if (minimumHeight() != rowHeight)
            setMinimumHeight(rowHeight);
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

    QToolButton *focusButton = nullptr;
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
        addButton = makeButton(QString(), QStringLiteral("Add a task (Ctrl+N)"));
        addButton->setObjectName(QStringLiteral("addTask"));
        addButton->setAccessibleName(QStringLiteral("Add a task"));
        addButton->setFixedSize(56, 56);
        addButton->setIconSize(QSize(28, 28));
        addButton->setFocusPolicy(Qt::StrongFocus);
        heading->addWidget(addButton);
        heading->addStretch();
        compactButton = makeButton(QString(), QStringLiteral("Show only tasks"));
        compactButton->setObjectName(QStringLiteral("compactMode"));
        compactButton->setCheckable(true);
        compactButton->setFixedSize(44, 44);
        compactButton->setIconSize(QSize(24, 24));
        compactButton->setAccessibleName(QStringLiteral("Compact mode"));
        compactButton->setFocusPolicy(Qt::StrongFocus);
        heading->addWidget(compactButton);

        auto *settingsButton = makeButton(QStringLiteral("⚙"), QStringLiteral("Appearance settings"));
        settingsButton->setObjectName(QStringLiteral("appearanceSettings"));
        settingsButton->setAccessibleName(QStringLiteral("Appearance settings"));
        settingsButton->setFocusPolicy(Qt::StrongFocus);
        settingsButton->setFixedSize(44, 44);
        settingsMenu = new SettingsPanel(panel);
        settingsMenu->setMinimumWidth(320);
        connect(settingsButton, &QToolButton::clicked, this, [this, settingsButton, panel] {
            if (settingsMenu->isVisible()) {
                settingsMenu->hide();
                return;
            }
            settingsMenu->ensurePolished();
            settingsMenu->adjustSize();
            const QPoint anchor = settingsButton->mapTo(panel, QPoint(settingsButton->width(), settingsButton->height() + 12));
            const int requiredHeight = anchor.y() + settingsMenu->height() + 8;
            if (panel->height() < requiredHeight)
                resize(width(), height() + requiredHeight - panel->height());
            settingsMenu->move(qMax(8, anchor.x() - settingsMenu->width()), anchor.y());
            settingsMenu->show();
            settingsMenu->raise();
            settingsMenu->setFocus();
        });
        heading->insertWidget(2, settingsButton);

        transparent = settingsMenu->addToggle(QStringLiteral("Transparent background"));
        transparent->setCheckable(true);
        transparent->setToolTip(QStringLiteral("Remove the window background for VR mode"));
        transparent->setChecked(QSettings().value(QStringLiteral("transparentBackground"), true).toBool());

        systemTheme = settingsMenu->addToggle(QStringLiteral("Use system theme"));
        systemTheme->setCheckable(true);
        systemTheme->setToolTip(QStringLiteral("Use the current KDE color scheme"));
        systemTheme->setChecked(QSettings().value(QStringLiteral("useSystemTheme"), false).toBool());
        rowOpacity = qBound(0, QSettings().value(QStringLiteral("rowOpacity"), 50).toInt(), 100);
        auto *opacityPanel = new QWidget(settingsMenu);
        auto *opacityLayout = new QHBoxLayout(opacityPanel);
        opacityLayout->setContentsMargins(8, 12, 8, 12);
        auto *lessOpacity = makeButton(QStringLiteral("−"), QStringLiteral("Lighter task background"));
        auto *moreOpacity = makeButton(QStringLiteral("+"), QStringLiteral("Denser task background"));
        lessOpacity->setFixedSize(56, 56);
        moreOpacity->setFixedSize(56, 56);
        auto *opacityLabel = new QLabel(opacityPanel);
        opacityLabel->setAlignment(Qt::AlignCenter);
        opacityLayout->addWidget(lessOpacity);
        opacityLayout->addWidget(opacityLabel, 1);
        opacityLayout->addWidget(moreOpacity);
        settingsMenu->addControl(opacityPanel);
        auto updateOpacity = [this, opacityLabel, lessOpacity, moreOpacity] {
            opacityLabel->setText(QStringLiteral("Task background\n%1%").arg(rowOpacity));
            lessOpacity->setEnabled(rowOpacity > 0);
            moreOpacity->setEnabled(rowOpacity < 100);
        };
        updateOpacity();
        connect(lessOpacity, &QToolButton::clicked, this, [this, updateOpacity] {
            rowOpacity = qMax(0, rowOpacity - 10);
            QSettings().setValue(QStringLiteral("rowOpacity"), rowOpacity);
            updateOpacity();
            applyStyle();
        });
        connect(moreOpacity, &QToolButton::clicked, this, [this, updateOpacity] {
            rowOpacity = qMin(100, rowOpacity + 10);
            QSettings().setValue(QStringLiteral("rowOpacity"), rowOpacity);
            updateOpacity();
            applyStyle();
        });
        defaultTaskSize = qBound(8, QSettings().value(QStringLiteral("defaultTaskSize"), 16).toInt(), 72);
        auto *sizePanel = new QWidget(settingsMenu);
        auto *sizeLayout = new QHBoxLayout(sizePanel);
        sizeLayout->setContentsMargins(8, 12, 8, 12);
        auto *smaller = makeButton(QStringLiteral("−"), QStringLiteral("Smaller text in all tasks"));
        auto *larger = makeButton(QStringLiteral("+"), QStringLiteral("Larger text in all tasks"));
        smaller->setObjectName(QStringLiteral("allTasksSmaller"));
        larger->setObjectName(QStringLiteral("allTasksLarger"));
        smaller->setFixedSize(56, 56);
        larger->setFixedSize(56, 56);
        auto *sizeLabel = new QLabel(sizePanel);
        sizeLabel->setAlignment(Qt::AlignCenter);
        sizeLayout->addWidget(smaller);
        sizeLayout->addWidget(sizeLabel, 1);
        sizeLayout->addWidget(larger);
        settingsMenu->addControl(sizePanel);
        auto updateSize = [this, sizeLabel, smaller, larger] {
            sizeLabel->setText(QStringLiteral("All task text\n%1 pt").arg(defaultTaskSize));
            smaller->setEnabled(defaultTaskSize > 8);
            larger->setEnabled(defaultTaskSize < 72);
        };
        updateSize();
        connect(smaller, &QToolButton::clicked, this, [this, updateSize] {
            setAllTaskSizes(qMax(8, defaultTaskSize - 2));
            updateSize();
        });
        connect(larger, &QToolButton::clicked, this, [this, updateSize] {
            setAllTaskSizes(qMin(72, defaultTaskSize + 2));
            updateSize();
        });
        root->insertLayout(0, heading);

        auto *formatPanel = new QWidget(panel);
        auto *formatBar = new QHBoxLayout(formatPanel);
        formatBar->setContentsMargins(0, 0, 0, 0);
        formatBar->setSpacing(5);

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
        fontSize->setMinimumHeight(44);
        fontSize->setAlignment(Qt::AlignCenter);
        fontSize->setKeyboardTracking(false);

        formatBar->addWidget(boldButton);
        formatBar->addWidget(italicButton);
        formatBar->addWidget(fontSize);
        formatBar->addStretch();
        list = new TaskList(panel);
        list->setObjectName(QStringLiteral("taskList"));
        list->setToolTip(QStringLiteral("Tasks are saved automatically. Ctrl+S — save now."));
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
        completedButton = makeButton(QString(), QStringLiteral("Hide or show completed tasks"));
        completedButton->setObjectName(QStringLiteral("completedTasks"));
        completedButton->setCheckable(true);
        completedButton->setChecked(QSettings().value(QStringLiteral("hideCompleted"), false).toBool());
        completedButton->setMinimumSize(88, 56);
        completedButton->setIconSize(QSize(28, 28));
        completedButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        completedButton->setFocusPolicy(Qt::StrongFocus);
        completedButton->hide();
        formatBar->addWidget(completedButton);
        connect(completedButton, &QToolButton::toggled, this, [this](bool hide) {
            QSettings().setValue(QStringLiteral("hideCompleted"), hide);
            updateCompletedVisibility();
        });
        restoreButton = makeButton(QStringLiteral("↶ Restore task"), QStringLiteral("Restore the last deleted task"));
        restoreButton->setObjectName(QStringLiteral("restoreTask"));
        restoreButton->setMinimumHeight(56);
        restoreButton->setFocusPolicy(Qt::StrongFocus);
        restoreButton->hide();
        root->addWidget(restoreButton);
        connect(restoreButton, &QToolButton::clicked, this, [this] { restoreTask(); });

        saveError = new QLabel(panel);
        saveError->setWordWrap(true);
        saveError->setStyleSheet(QStringLiteral("color: #ffb7b7; background: #402020; padding: 8px;"));
        saveError->hide();
        root->addWidget(saveError);
        root->addWidget(formatPanel);

        connect(compactButton, &QToolButton::toggled, this,
                [this, formatPanel, formatBar, heading, settingsButton, root](bool compact) {
            settingsMenu->hide();
            settingsButton->setVisible(!compact);
            formatPanel->setVisible(!compact);
            if (compact) {
                formatBar->removeWidget(completedButton);
                heading->insertWidget(heading->indexOf(compactButton), completedButton);
            } else {
                heading->removeWidget(completedButton);
                formatBar->addWidget(completedButton);
            }
            completedButton->setVisible(completedCount > 0);
            root->setContentsMargins(compact ? 8 : 18, compact ? 8 : 16,
                                     compact ? 8 : 18, compact ? 8 : 18);
            setMinimumSize(compact ? QSize(320, 200) : QSize(420, 320));
            compactButton->setAccessibleName(compact ? QStringLiteral("Full view") : QStringLiteral("Compact mode"));
            compactButton->setToolTip(compact ? QStringLiteral("Show settings and formatting")
                                              : QStringLiteral("Show only tasks"));
            QSettings().setValue(QStringLiteral("compactMode"), compact);
            applyStyle();
        });
        compactButton->setChecked(QSettings().value(QStringLiteral("compactMode"), false).toBool());

        setCentralWidget(panel);
        applyStyle();

        connect(addButton, &QToolButton::clicked, this, [this] { addTask(); saveTasks(); });
        connect(boldButton, &QToolButton::clicked, this, [this] { toggleBold(); });
        connect(italicButton, &QToolButton::clicked, this, [this] { toggleItalic(); });
        connect(fontSize, qOverload<int>(&QSpinBox::valueChanged), this, [this](int size) {
            applyFormat([size](QTextCharFormat &format) { format.setFontPointSize(size); });
        });
        connect(transparent, &QAction::toggled, this, [this](bool enabled) {
            QSettings().setValue(QStringLiteral("transparentBackground"), enabled);
            applyStyle();
        });
        connect(systemTheme, &QAction::toggled, this, [this](bool enabled) {
            QSettings().setValue(QStringLiteral("useSystemTheme"), enabled);
            applyStyle();
        });

        auto *newShortcut = new QShortcut(QKeySequence::New, this);
        connect(newShortcut, &QShortcut::activated, this, [this] { addTask(); saveTasks(); });
        auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
        connect(saveShortcut, &QShortcut::activated, this, [this] { saveTasks(); });
        auto *deleteShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Delete")), this);
        connect(deleteShortcut, &QShortcut::activated, this, [this] { removeCurrentTask(); });
        qApp->installEventFilter(this);
        loadTasks();
        const QSize savedSize = QSettings().value(QStringLiteral("windowSize"), QSize(620, 700)).toSize();
        if (savedSize.isValid() && !savedSize.isEmpty()) {
            resize(savedSize.expandedTo(minimumSize()));
        }
    }

    ~TodoWindow() override
    {
        qApp->removeEventFilter(this);
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::ApplicationPaletteChange && systemTheme && systemTheme->isChecked())
            applyStyle();
    }

    void closeEvent(QCloseEvent *event) override
    {
        if (!loadFailed && !saveTasks()) {
            event->ignore();
            return;
        }
        const QSize rememberedSize = (isMaximized() || isFullScreen() || isMinimized())
            ? normalGeometry().size() : size();
        if (rememberedSize.isValid() && !rememberedSize.isEmpty())
            QSettings().setValue(QStringLiteral("windowSize"), rememberedSize);
        QMainWindow::closeEvent(event);
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *widget = qobject_cast<QWidget *>(object);
        const bool ours = widget && widget->window() == this;
        if (ours && settingsMenu->isVisible()) {
            if (event->type() == QEvent::KeyPress
                && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
                settingsMenu->hide();
                return true;
            }
            if (event->type() == QEvent::MouseButtonPress && widget != settingsMenu
                && !settingsMenu->isAncestorOf(widget)
                && widget->objectName() != QStringLiteral("appearanceSettings"))
                settingsMenu->hide();
        }
        // Hover hints distract during head-gaze use; retain descriptions for accessibility.
        if (ours && event->type() == QEvent::ToolTip)
            return true;
        if (event->type() == QEvent::FocusIn && ours) {
            for (int i = 0; i < list->count(); ++i) {
                auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
                if (row && (object == row->editor || object == row->checkBox)) {
                    list->setCurrentItem(list->item(i));
                    syncFormatControls(row->editor);
                    break;
                }
            }
        }
        return QMainWindow::eventFilter(object, event);
    }

private:
    QToolButton *makeButton(const QString &text, const QString &tip)
    {
        auto *button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tip);
        button->setMinimumSize(44, 44);
        button->setFocusPolicy(Qt::NoFocus);
        return button;
    }

    QTextEdit *currentEditor() const
    {
        if (list->currentItem() && !list->currentItem()->isHidden()) {
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

    void addTask(const QString &html = QString(), int position = -1)
    {
        if (loadFailed)
            return;
        auto *item = new QListWidgetItem;
        list->insertItem(position < 0 ? list->count() : qMin(position, list->count()), item);
        auto *row = new TaskRow;
        if (!html.isEmpty())
            row->editor->setHtml(html);
        else
            row->editor->setFontPointSize(defaultTaskSize);
        row->adjustHeight();
        item->setSizeHint(QSize(0, row->minimumHeight()));
        list->setItemWidget(item, row);
        list->setCurrentItem(item);
        row->editor->setFocus();
        connect(row->editor, &QTextEdit::currentCharFormatChanged, this, [this, row] {
            if (currentEditor() == row->editor)
                syncFormatControls(row->editor);
        });
        row->dragHandle->startDragCallback = [this, item] {
            list->startDragging(item);
        };
        row->sizeChangedCallback = [item, row] {
            const QSize size(0, row->minimumHeight());
            if (item->sizeHint() != size)
                item->setSizeHint(size);
        };
        connect(row->removeButton, &QToolButton::clicked, this, [this, item] {
            removeTask(item);
        });

        connect(row->editor, &QTextEdit::textChanged, this, [this, item, row] {
            row->requestHeightUpdate();
            saveTasks();
        });
        connect(row->focusButton, &QToolButton::toggled, this, [this, row](bool active) {
            if (active) {
                for (int i = 0; i < list->count(); ++i) {
                    auto *other = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
                    if (other && other != row)
                        other->setInProgress(false);
                }
            }
            row->setInProgress(active);
            saveTasks();
        });
        connect(row->checkBox, &QCheckBox::toggled, this, [this, row](bool done) {
            if (done)
                row->setInProgress(false);
            row->focusButton->setEnabled(!done);
            saveTasks();
        });

        // Qt finishes laying out restored rich text on the next event loop
        // pass. Recalculate once then so every saved line is visible.
        row->requestHeightUpdate();
    }

    void removeCurrentTask()
    {
        if (!list->currentItem() || list->currentItem()->isHidden())
            return;
        removeTask(list->currentItem());
    }

    void removeTask(QListWidgetItem *item)
    {
        if (!item)
            return;
        auto *row = dynamic_cast<TaskRow *>(list->itemWidget(item));
        if (!row)
            return;
        deletedTask = DeletedTask{row->editor->toHtml(), row->checkBox->isChecked(),
                                  row->focusButton->isChecked(), list->row(item)};
        // A queued height update must not access an item after deletion.
        row->sizeChangedCallback = nullptr;
        delete list->takeItem(list->row(item));
        emptyReplacement = nullptr;
        if (list->count() == 0) {
            addTask();
            emptyReplacement = currentRowWidget();
            emptyReplacement->editor->document()->setModified(false);
        }
        restoreButton->show();
        saveTasks();
    }

    void restoreTask()
    {
        if (!deletedTask || loadFailed)
            return;
        const DeletedTask task = *deletedTask;
        {
            const QScopedValueRollback<bool> guard(loading, true);
            // Remove only the untouched blank row created after deleting the last task.
            if (emptyReplacement && !emptyReplacement->editor->document()->isModified()
                && !emptyReplacement->checkBox->isChecked() && !emptyReplacement->focusButton->isChecked()) {
                for (int i = 0; i < list->count(); ++i) {
                    if (list->itemWidget(list->item(i)) == emptyReplacement) {
                        emptyReplacement->sizeChangedCallback = nullptr;
                        delete list->takeItem(i);
                        break;
                    }
                }
            }
            emptyReplacement = nullptr;
            addTask(task.html, task.position);
            auto *row = currentRowWidget();
            row->checkBox->setChecked(task.done);
            if (task.done)
                completedButton->setChecked(false);
            if (task.inProgress && !task.done)
                row->focusButton->setChecked(true);
            list->scrollToItem(list->currentItem());
        }
        deletedTask.reset();
        restoreButton->hide();
        saveTasks();
    }

    template <typename Formatter>
    void applyFormat(Formatter formatter)
    {
        auto *editor = currentEditor();
        if (!editor)
            return;
        {
            const QScopedValueRollback<bool> guard(loading, true);
            QTextCharFormat format;
            formatter(format);
            editor->mergeCurrentCharFormat(format);
        }
        syncFormatControls(editor);
        if (auto *row = currentRowWidget())
            row->requestHeightUpdate();
        // Keep focus in the spin box while its value is being committed.
        if (!fontSize->hasFocus())
            editor->setFocus();
        saveTasks();
    }

    void setAllTaskSizes(int size)
    {
        if (loadFailed)
            return;
        defaultTaskSize = size;
        QSettings().setValue(QStringLiteral("defaultTaskSize"), size);
        QTextCharFormat format;
        format.setFontPointSize(size);
        auto resizeDocument = [&format, size](QTextDocument *document) {
            QFont font = document->defaultFont();
            font.setPointSize(size);
            document->setDefaultFont(font);
            QTextCursor cursor(document);
            cursor.select(QTextCursor::Document);
            cursor.mergeCharFormat(format);
        };
        {
            const QScopedValueRollback<bool> guard(loading, true);
            for (int i = 0; i < list->count(); ++i) {
                auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
                if (!row)
                    continue;
                resizeDocument(row->editor->document());
                row->editor->mergeCurrentCharFormat(format);
                row->requestHeightUpdate();
            }
            if (deletedTask) {
                QTextDocument document;
                document.setHtml(deletedTask->html);
                resizeDocument(&document);
                deletedTask->html = document.toHtml();
            }
        }
        if (auto *editor = currentEditor())
            syncFormatControls(editor);
        saveTasks();
    }

    void syncFormatControls(QTextEdit *editor)
    {
        const QSignalBlocker blockBold(boldButton), blockItalic(italicButton), blockSize(fontSize);
        const auto format = editor->currentCharFormat();
        boldButton->setChecked(format.fontWeight() >= QFont::Bold);
        italicButton->setChecked(format.fontItalic());
        const qreal size = format.fontPointSize() > 0 ? format.fontPointSize()
            : editor->document()->defaultFont().pointSizeF();
        fontSize->setValue(qRound(size));
    }

    void toggleBold()
    {
        const bool bold = currentEditor() && currentEditor()->fontWeight() >= QFont::Bold;
        applyFormat([bold](QTextCharFormat &format) {
            format.setFontWeight(bold ? QFont::Normal : QFont::Bold);
        });
    }

    void toggleItalic()
    {
        const bool italic = currentEditor() && currentEditor()->fontItalic();
        applyFormat([italic](QTextCharFormat &format) { format.setFontItalic(!italic); });
    }

    QString dataPath() const
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tasks.json");
    }

    void updateCompletedVisibility()
    {
        if (loading)
            return;
        int completed = 0;
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            auto *row = dynamic_cast<TaskRow *>(list->itemWidget(item));
            if (!row)
                continue;
            const bool done = row->checkBox->isChecked();
            completed += done ? 1 : 0;
            const bool hide = done && completedButton->isChecked();
            if (hide && item == list->currentItem()) {
                list->setCurrentItem(nullptr);
                completedButton->setFocus();
            }
            item->setHidden(hide);
        }
        completedCount = completed;
        const QString description = (completedButton->isChecked()
            ? QStringLiteral("Show completed (%1)") : QStringLiteral("Hide completed (%1)")).arg(completed);
        completedButton->setToolTip(description);
        completedButton->setAccessibleName(description);
        updateCompletedIcon();
        completedButton->setVisible(completed > 0);
    }

    bool saveTasks()
    {
        if (loading || loadFailed)
            return false;
        updateCompletedVisibility();
        QDir().mkpath(QFileInfo(dataPath()).absolutePath());
        QJsonArray tasks;
        for (int i = 0; i < list->count(); ++i) {
            auto *row = dynamic_cast<TaskRow *>(list->itemWidget(list->item(i)));
            if (!row)
                continue;
            QJsonObject task;
            task[QStringLiteral("html")] = row->editor->toHtml();
            task[QStringLiteral("done")] = row->checkBox->isChecked();
            task[QStringLiteral("inProgress")] = row->focusButton->isChecked();
            tasks.append(task);
        }
        QSaveFile file(dataPath());
        const QByteArray data = QJsonDocument(tasks).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            saveError->setText(QStringLiteral("Could not save tasks: %1").arg(file.errorString()));
            saveError->show();
            return false;
        }
        saveError->hide();
        return true;
    }

    void loadTasks()
    {
        loading = true;
        QFile file(dataPath());
        if (file.exists()) {
            QJsonParseError error;
            const bool opened = file.open(QIODevice::ReadOnly);
            const QJsonDocument document = QJsonDocument::fromJson(opened ? file.readAll() : QByteArray(), &error);
            bool valid = opened && error.error == QJsonParseError::NoError && document.isArray();
            if (valid) {
                for (const auto &value : document.array()) {
                    const auto task = value.toObject();
                    if (!value.isObject() || !task.value(QStringLiteral("html")).isString()
                        || !task.value(QStringLiteral("done")).isBool()) {
                        valid = false;
                        break;
                    }
                }
            }
            if (!valid) {
                loadFailed = true;
                saveError->setText(QStringLiteral("Could not read %1. The original file is preserved. Restore it and restart the app.").arg(dataPath()));
                saveError->show();
                list->setEnabled(false);
                addButton->setEnabled(false);
            } else {
                for (const auto &value : document.array()) {
                    const auto task = value.toObject();
                    addTask(task.value(QStringLiteral("html")).toString());
                    auto *row = currentRowWidget();
                    row->checkBox->setChecked(task.value(QStringLiteral("done")).toBool());
                    if (!row->checkBox->isChecked())
                        row->focusButton->setChecked(task.value(QStringLiteral("inProgress")).toBool());
                }
            }
        }
        if (list->count() == 0 && !loadFailed)
            addTask();
        loading = false;
        updateCompletedVisibility();
    }

    void updateCompletedIcon()
    {
        const QColor color = systemTheme->isChecked()
            ? QApplication::palette().color(QPalette::WindowText) : QColor(Qt::white);
        const qreal ratio = devicePixelRatioF();
        const QString count = QString::number(completedCount);
        const QFontMetrics metrics(completedButton->font());
        const int width = 36 + metrics.horizontalAdvance(count);
        QPixmap pixmap(qRound(width * ratio), qRound(28 * ratio));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath eye;
        eye.moveTo(2, 14);
        eye.cubicTo(8, 4, 20, 4, 26, 14);
        eye.cubicTo(20, 24, 8, 24, 2, 14);
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(14, 14), 3, 3);
        if (completedButton->isChecked())
            painter.drawLine(QPointF(4, 4), QPointF(24, 24));
        painter.setFont(completedButton->font());
        painter.drawText(QRect(36, 0, width - 36, 28), Qt::AlignCenter, count);
        painter.end();
        completedButton->setIconSize(QSize(width, 28));
        completedButton->setIcon(QIcon(pixmap));
    }

    void updateAddIcon(const QColor &color)
    {
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(qRound(28 * ratio), qRound(28 * ratio));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(14, 4), QPointF(14, 24));
        painter.drawLine(QPointF(4, 14), QPointF(24, 14));
        painter.end();
        addButton->setIcon(QIcon(pixmap));
    }

    void updateCompactIcon(const QColor &color)
    {
        const qreal ratio = devicePixelRatioF();
        QPixmap pixmap(qRound(24 * ratio), qRound(24 * ratio));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        // Four corner arrows point outwards to expand, inwards to compact.
        for (int x : {-1, 1}) {
            for (int y : {-1, 1}) {
                painter.save();
                painter.translate(12, 12);
                painter.scale(x, y);
                const int tip = compactButton->isChecked() ? 9 : 3;
                const int tail = compactButton->isChecked() ? 3 : 9;
                painter.drawLine(QPoint(tail, tail), QPoint(tip, tip));
                painter.drawLine(QPoint(tail, tip), QPoint(tip, tip));
                painter.drawLine(QPoint(tip, tail), QPoint(tip, tip));
                painter.restore();
            }
        }
        painter.end();
        compactButton->setIcon(QIcon(pixmap));
    }

    void applyStyle()
    {
        const bool clear = transparent->isChecked();
        const bool useSystem = systemTheme->isChecked();
        const QPalette palette = QApplication::palette();
        const QColor textColor = useSystem ? palette.color(QPalette::WindowText) : QColor(Qt::white);
        updateAddIcon(textColor);
        updateCompactIcon(textColor);
        updateCompletedIcon();
        const QColor menuBackground = useSystem ? palette.color(QPalette::AlternateBase)
            : (clear ? QColor(20, 21, 23) : QColor(32, 33, 36));
        settingsMenu->surface = menuBackground;
        settingsMenu->update();
        const QColor menuHover = useSystem ? palette.color(QPalette::Highlight) : QColor(87, 43, 67);
        const QColor menuHoverText = useSystem ? palette.color(QPalette::HighlightedText) : QColor(Qt::white);
        settingsMenu->setStyleSheet(QStringLiteral(
            "QWidget#settingsPanel QLabel { color: %2; background: transparent; font-size: 18px; }"
            "QWidget#settingsPanel QToolButton { color: %2; background: transparent; border: 1px solid transparent; border-radius: 7px; font-size: 24px; }"
            "QWidget#settingsPanel QToolButton:hover { background-color: %3; color: %4; }"
            "QWidget#settingsPanel QToolButton:disabled { color: #888888; }"
            "QWidget#settingsPanel QCheckBox { font-size: 18px; padding: 4px 8px; color: %2; background: transparent; border-radius: 8px; spacing: 16px; }"
            "QWidget#settingsPanel QCheckBox:hover { background-color: %3; color: %4; }"
            "QWidget#settingsPanel QCheckBox::indicator { width: 24px; height: 24px; border: 2px solid %2; border-radius: 5px; background-color: %1; }"
            "QWidget#settingsPanel QCheckBox::indicator:checked { border-color: #ff4f9a; image: url(:/icons/checkmark.svg); }"
            "QWidget#settingsPanel QCheckBox::indicator:unchecked { image: none; }"
        ).arg(menuBackground.name(), textColor.name(), menuHover.name(), menuHoverText.name()));
        const QColor baseColor = useSystem ? palette.color(QPalette::Base) : QColor(14, 15, 17);

        auto rgba = [](QColor color, int alpha) {
            color.setAlpha(alpha);
            return QStringLiteral("rgba(%1, %2, %3, %4)")
                .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
        };

        const QString panelBackground = clear
            ? QStringLiteral("rgba(0, 0, 0, 0)")
            : (useSystem ? rgba(palette.color(QPalette::Window), 245)
                         : QStringLiteral("rgba(22, 23, 25, 245)"));
        const QString rowBackground = useSystem
            ? rgba(palette.color(QPalette::AlternateBase), qRound(rowOpacity * 2.55))
            : rgba(clear ? QColor(24, 25, 27) : QColor(42, 43, 46), qRound(rowOpacity * 2.55));
        const QString controlBackground = useSystem
            ? rgba(palette.color(QPalette::Button), 245)
            : panelBackground;
        const QString text = rgba(textColor, 255);
        const QString hint = rgba(textColor, 150);
        const QString border = rgba(textColor, 75);
        const QString indicatorBackground = rgba(baseColor, 180);
        const QString arrowUp = useSystem && textColor.lightness() < 128
            ? QStringLiteral(":/icons/arrow-up-dark.svg") : QStringLiteral(":/icons/arrow-up.svg");
        const QString arrowDown = useSystem && textColor.lightness() < 128
            ? QStringLiteral(":/icons/arrow-down-dark.svg") : QStringLiteral(":/icons/arrow-down.svg");

        QString style = QStringLiteral(
            "QWidget#panel { background: %1; border-radius: 16px; }"
            "QListWidget#taskList { background: transparent; }"
            "QListWidget#taskList::item { background: %2; border-radius: 10px; }"
            "QWidget#taskRow { border: 2px solid transparent; border-radius: 10px; }"
            "QWidget#taskRow[inProgress=\"true\"] { border: 2px solid #ff4f9a; background: rgba(255, 79, 154, 35); }"
            "QToolButton#taskFocus { padding: 0px; font-size: 20px; }"
            "QToolButton#taskFocus:checked { color: #ff4f9a; border-color: transparent; }"
            "QToolButton#taskFocus:disabled { color: %4; border-color: transparent; }"
            "QTextEdit { color: %3; background: transparent; font-size: 16pt; }"
            "QCheckBox { color: %3; border-radius: 8px; }"
            "QCheckBox:hover { background: rgba(255, 79, 154, 45); }"
            "QToolButton, QSpinBox { color: %3; background: %5; border: 1px solid transparent; border-radius: 7px; padding: 4px 8px; }"
            "QToolButton#appearanceSettings { font-size: 24px; padding: 0px; }"
            "QToolButton#appearanceSettings::menu-indicator { image: none; }"
            "QToolButton:checked { background: rgba(255, 79, 154, 55); border: 1px solid transparent; }"
            "QToolButton#compactMode { border: 1px solid transparent; }"
            "QToolButton:hover { background: rgba(255, 79, 154, 75); border: 1px solid transparent; }"
            "QLabel#dragHandle { color: %4; background: transparent; border-radius: 8px; font-size: 22px; }"
            "QLabel#dragHandle:hover { color: %3; background: rgba(255, 79, 154, 100); }"
            "QToolButton#taskRemove { color: %4; background: transparent; border: 1px solid transparent; border-radius: 8px; font-size: 22px; padding: 0px; }"
            "QToolButton#taskRemove:hover { color: %3; background: rgba(255, 79, 154, 130); border: 1px solid transparent; }"
            "QSpinBox#fontSize { padding: 2px 36px 2px 8px; }"
            "QSpinBox#fontSize::up-button, QSpinBox#fontSize::down-button { subcontrol-origin: border; width: 32px; border-left: 1px solid transparent; }"
            "QSpinBox#fontSize::up-button { subcontrol-position: top right; border-top-right-radius: 7px; }"
            "QSpinBox#fontSize::up-arrow { image: url(%8); width: 18px; height: 12px; }"
            "QSpinBox#fontSize::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 7px; }"
            "QSpinBox#fontSize::down-arrow { image: url(%9); width: 18px; height: 12px; }"
            "QSpinBox#fontSize::up-button:hover, QSpinBox#fontSize::down-button:hover { background: rgba(255, 79, 154, 130); border-left: 1px solid transparent; }"
            "QCheckBox::indicator { width: 20px; height: 20px; border: 2px solid %6; border-radius: 5px; background: %7; }"
            "QCheckBox::indicator:hover { border: 2px solid rgb(255, 79, 154); background: rgba(255, 79, 154, 130); }"
            "QCheckBox::indicator:checked { border: 2px solid rgba(255, 79, 154, 220); background: transparent; image: url(:/icons/checkmark.svg); }"
            "QCheckBox::indicator:checked:hover { border: 2px solid rgb(255, 79, 154); background: rgba(255, 79, 154, 130); image: url(:/icons/checkmark.svg); }"
        );
        setStyleSheet(style.arg(panelBackground).arg(rowBackground).arg(text).arg(hint)
                          .arg(controlBackground).arg(border).arg(indicatorBackground)
                          .arg(arrowUp).arg(arrowDown));
        fontSize->ensurePolished();
        // Qt includes the styled buttons, editor margins, and suffix in this hint.
        fontSize->setFixedWidth(fontSize->sizeHint().width());
    }

    struct DeletedTask {
        QString html;
        bool done;
        bool inProgress;
        int position;
    };
    std::optional<DeletedTask> deletedTask;
    QPointer<TaskRow> emptyReplacement;
    int defaultTaskSize = 16;
    int rowOpacity = 50;
    int completedCount = 0;
    QToolButton *completedButton = nullptr;
    QToolButton *restoreButton = nullptr;
    bool loading = false;
    bool loadFailed = false;
    QLabel *saveError = nullptr;
    TaskList *list = nullptr;
    SettingsPanel *settingsMenu = nullptr;
    QAction *transparent = nullptr;
    QAction *systemTheme = nullptr;
    QToolButton *compactButton = nullptr;
    QToolButton *addButton = nullptr;
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

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataDirectory)) {
        QMessageBox::critical(nullptr, QStringLiteral("My List"),
                              QStringLiteral("Could not create the task data folder."));
        return 1;
    }
    QLockFile instanceLock(dataDirectory + QStringLiteral("/instance.lock"));
    // A long-running session is not stale. Qt still detects a process that has exited.
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        if (instanceLock.error() == QLockFile::LockFailedError) {
            qWarning("My List is already running; leaving its tasks unchanged.");
            return 0;
        }
        QMessageBox::critical(nullptr, QStringLiteral("My List"),
                              QStringLiteral("Could not lock the task data folder. Check its permissions."));
        return 1;
    }

    TodoWindow window;
    window.show();
    return app.exec();
}
