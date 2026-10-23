/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tablet_gestures_config_widget.h"
#include "ui_config.h"
#include "../action_registry.h"
#include "../zone_manager.h"
#include "../diagnostics_logger.h"
#include "../custom_command_store.h"

#include <QComboBox>
#include <QListWidgetItem>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>

namespace KWin
{

TabletGesturesConfigWidget::TabletGesturesConfigWidget(QWidget *parent)
    : QWidget(parent)
    , m_ui(std::make_unique<Ui::TabletGesturesConfig>())
    , m_store(std::make_unique<CustomCommandStore>())
{
    m_ui->setupUi(this);
    setupConnections();
    setupDiagnosticObservers();
    defaults();
}

TabletGesturesConfigWidget::~TabletGesturesConfigWidget() = default;

void TabletGesturesConfigWidget::setupConnections()
{
    // Command Manager connections
    connect(m_ui->listCustomCommands, &QListWidget::itemSelectionChanged, this, &TabletGesturesConfigWidget::onCommandListSelectionChanged);
    connect(m_ui->btnNewCommand, &QPushButton::clicked, this, &TabletGesturesConfigWidget::onNewCommandClicked);
    connect(m_ui->btnAddCommand, &QPushButton::clicked, this, &TabletGesturesConfigWidget::onAddCommandClicked);
    connect(m_ui->btnSaveChanges, &QPushButton::clicked, this, &TabletGesturesConfigWidget::onSaveChangesClicked);
    connect(m_ui->btnDeleteCommand, &QPushButton::clicked, this, &TabletGesturesConfigWidget::onDeleteCommandClicked);
    connect(m_ui->btnDiscardCommand, &QPushButton::clicked, this, &TabletGesturesConfigWidget::onDiscardCommandClicked);

    connect(m_ui->editCommandTitle, &QLineEdit::textChanged, this, &TabletGesturesConfigWidget::onEditorTextChanged);
    connect(m_ui->editCommandLine, &QLineEdit::textChanged, this, &TabletGesturesConfigWidget::onEditorTextChanged);

    auto onReturn = [this]() {
        if (m_ui->btnAddCommand->isEnabled()) {
            onAddCommandClicked();
        } else if (m_ui->btnSaveChanges->isEnabled()) {
            onSaveChangesClicked();
        }
    };
    connect(m_ui->editCommandTitle, &QLineEdit::returnPressed, this, onReturn);
    connect(m_ui->editCommandLine, &QLineEdit::returnPressed, this, onReturn);

    // Track modifications on settings controls
    const auto edgeCombos = {
        m_ui->kcfg_BottomLeftAction,
        m_ui->kcfg_BottomCenterAction,
        m_ui->kcfg_BottomRightAction,
        m_ui->kcfg_TopAction,
        m_ui->kcfg_RightAction,
        m_ui->kcfg_LeftAction
    };
    for (auto *cb : edgeCombos) {
        connect(cb, &QComboBox::activated, this, [this, cb](int index) {
            if (cb->itemData(index).toString() == QStringLiteral("__open_custom_tab__")) {
                m_ui->tabWidget->setCurrentWidget(m_ui->tabCustomCommands);
                enterNewCommandMode(QStringLiteral("combo_action"));
                m_ui->editCommandLine->setFocus();
                const QString prev = cb->property("previousAction").toString();
                selectActionInCombo(cb, prev.isEmpty() ? QStringLiteral("none") : prev);
            } else {
                cb->setProperty("previousAction", cb->currentData().toString());
            }
        });
    }

    const auto combos = {
        m_ui->kcfg_BottomLeftAction,
        m_ui->kcfg_BottomCenterAction,
        m_ui->kcfg_BottomRightAction,
        m_ui->kcfg_TopAction,
        m_ui->kcfg_RightAction,
        m_ui->kcfg_LeftAction,
        m_ui->kcfg_TabletButtonSize,
        m_ui->kcfg_LaptopButtonSize,
        m_ui->kcfg_TabletScale,
        m_ui->kcfg_LaptopScale,
        m_ui->kcfg_BottomGesturesWithOsk
    };
    for (auto *cb : combos) {
        connect(cb, &QComboBox::currentIndexChanged, this, &TabletGesturesConfigWidget::onWidgetModified);
    }

    const auto sliders = {
        m_ui->kcfg_EdgeMargin,
        m_ui->kcfg_SwipeThreshold,
        m_ui->kcfg_HingeAngleThreshold,
        m_ui->kcfg_HingePollInterval,
        m_ui->kcfg_TopLeftSafeWidth,
        m_ui->kcfg_TopLeftSafeHeight,
        m_ui->kcfg_TopRightSafeWidth,
        m_ui->kcfg_TopRightSafeHeight
    };
    for (auto *s : sliders) {
        connect(s, &QSlider::valueChanged, this, &TabletGesturesConfigWidget::onWidgetModified);
    }

    const auto checkboxes = {
        m_ui->kcfg_EnableButtonScaling,
        m_ui->kcfg_EnableDisplayScaling,
        m_ui->kcfg_EnableTopEdge,
        m_ui->kcfg_EnableBottomEdge,
        m_ui->kcfg_EnableLeftEdge,
        m_ui->kcfg_EnableRightEdge,
        m_ui->kcfg_HideKeyboardOnGesture
    };
    for (auto *c : checkboxes) {
        connect(c, &QAbstractButton::toggled, this, &TabletGesturesConfigWidget::onWidgetModified);
    }
}

void TabletGesturesConfigWidget::onWidgetModified()
{
    if (!m_loading) {
        Q_EMIT changed();
    }
}

void TabletGesturesConfigWidget::setupDiagnosticObservers()
{
    m_monitoredButtons = {
        m_ui->btnNewCommand,
        m_ui->btnAddCommand,
        m_ui->btnSaveChanges,
        m_ui->btnDeleteCommand,
        m_ui->btnDiscardCommand
    };

    for (auto *btn : m_monitoredButtons) {
        if (btn) {
            btn->installEventFilter(this);

            // Independent clicked signal observer
            connect(btn, &QAbstractButton::clicked, this, [this, btn]() {
                QJsonObject details;
                details.insert(QStringLiteral("button"), btn->objectName());
                details.insert(QStringLiteral("enabled"), btn->isEnabled());
                details.insert(QStringLiteral("visible"), btn->isVisible());
                DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("button_clicked_signal"), details);
            });
        }
    }

    // Also install filter on container widgets to observe clicks over disabled controls
    const auto containers = {
        static_cast<QWidget*>(m_ui->groupCustomCommands),
        static_cast<QWidget*>(m_ui->tabCustomCommands),
        static_cast<QWidget*>(m_ui->groupActions),
        static_cast<QWidget*>(m_ui->tabGestures),
        static_cast<QWidget*>(m_ui->tabWidget),
        static_cast<QWidget*>(m_ui->listCustomCommands)
    };
    for (auto *c : containers) {
        if (c) {
            c->installEventFilter(this);
        }
    }
}

bool TabletGesturesConfigWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEv = static_cast<QMouseEvent*>(event);
        const QString evType = (event->type() == QEvent::MouseButtonPress) ? QStringLiteral("mouse_press") : QStringLiteral("mouse_release");

        auto *btn = qobject_cast<QAbstractButton*>(watched);
        if (btn && m_monitoredButtons.contains(btn)) {
            logRawInput(btn, evType, mouseEv->button(), mouseEv->position().toPoint(), mouseEv->globalPosition().toPoint());
        } else {
            // Check if click occurred over one of the disabled monitored buttons
            for (auto *b : m_monitoredButtons) {
                if (b && (!b->isEnabled() || watched != b) && b->isVisible()) {
                    QPoint localToBtn = b->mapFromGlobal(mouseEv->globalPosition().toPoint());
                    if (b->rect().contains(localToBtn)) {
                        logRawInput(b, evType, mouseEv->button(), localToBtn, mouseEv->globalPosition().toPoint());
                        break;
                    }
                }
            }
        }
    } else if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto *keyEv = static_cast<QKeyEvent*>(event);
        if (keyEv->key() == Qt::Key_Space || keyEv->key() == Qt::Key_Return || keyEv->key() == Qt::Key_Enter) {
            auto *btn = qobject_cast<QAbstractButton*>(watched);
            if (btn && m_monitoredButtons.contains(btn)) {
                const QString evType = (event->type() == QEvent::KeyPress) ? QStringLiteral("key_press") : QStringLiteral("key_release");
                logRawKeyInput(btn, evType, keyEv->key());
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void TabletGesturesConfigWidget::logRawInput(QWidget *widget, const QString &eventType, Qt::MouseButton button, const QPoint &pos, const QPoint &globalPos)
{
    QJsonObject details;
    details.insert(QStringLiteral("target_object"), widget ? widget->objectName() : QStringLiteral("unknown"));
    details.insert(QStringLiteral("event_type"), eventType);
    QString btnStr = QStringLiteral("none");
    if (button == Qt::LeftButton) btnStr = QStringLiteral("left");
    else if (button == Qt::RightButton) btnStr = QStringLiteral("right");
    else if (button == Qt::MiddleButton) btnStr = QStringLiteral("middle");
    details.insert(QStringLiteral("button"), btnStr);
    details.insert(QStringLiteral("enabled"), widget ? widget->isEnabled() : false);
    details.insert(QStringLiteral("visible"), widget ? widget->isVisible() : false);
    if (widget) {
        const QRect g = widget->geometry();
        details.insert(QStringLiteral("geometry"), QStringLiteral("%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height()));
    }
    details.insert(QStringLiteral("local_pos"), QStringLiteral("%1,%2").arg(pos.x()).arg(pos.y()));
    details.insert(QStringLiteral("global_pos"), QStringLiteral("%1,%2").arg(globalPos.x()).arg(globalPos.y()));

    if (widget && !widget->isEnabled()) {
        QString reason = QStringLiteral("disabled");
        if (widget == m_ui->btnAddCommand) {
            reason = m_ui->editCommandLine->text().trimmed().isEmpty() ? QStringLiteral("empty_command_field") : QStringLiteral("capacity_limit_reached");
        } else if (widget == m_ui->btnSaveChanges) {
            reason = (m_currentEditingSlot == -1) ? QStringLiteral("no_slot_selected") : QStringLiteral("editor_not_modified_or_empty");
        } else if (widget == m_ui->btnDeleteCommand) {
            reason = (m_currentEditingSlot == -1) ? QStringLiteral("no_slot_selected") : QStringLiteral("disabled");
        } else if (widget == m_ui->btnDiscardCommand) {
            reason = QStringLiteral("no_changes_to_discard");
        } else if (widget == m_ui->btnNewCommand) {
            reason = QStringLiteral("capacity_limit_reached");
        }
        details.insert(QStringLiteral("disabled_reason"), reason);
    }

    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("raw_input"), details);
}

void TabletGesturesConfigWidget::logRawKeyInput(QWidget *widget, const QString &eventType, int key)
{
    QJsonObject details;
    details.insert(QStringLiteral("target_object"), widget ? widget->objectName() : QStringLiteral("unknown"));
    details.insert(QStringLiteral("event_type"), eventType);
    QString keyName = QStringLiteral("Unknown");
    if (key == Qt::Key_Space) keyName = QStringLiteral("Space");
    else if (key == Qt::Key_Return) keyName = QStringLiteral("Return");
    else if (key == Qt::Key_Enter) keyName = QStringLiteral("Enter");
    details.insert(QStringLiteral("key"), keyName);
    details.insert(QStringLiteral("enabled"), widget ? widget->isEnabled() : false);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("raw_input"), details);
}

void TabletGesturesConfigWidget::logHandlerEnter(const QString &actionName)
{
    QJsonObject details;
    details.insert(QStringLiteral("action_name"), actionName);
    details.insert(QStringLiteral("current_slot"), m_currentEditingSlot);
    details.insert(QStringLiteral("command_count"), m_commands.size());
    details.insert(QStringLiteral("command_limit"), m_commandLimit);
    details.insert(QStringLiteral("current_tab"), m_ui->tabWidget->currentIndex());
    details.insert(QStringLiteral("selected_row"), m_ui->listCustomCommands->currentRow());
    details.insert(QStringLiteral("btn_add_enabled"), m_ui->btnAddCommand->isEnabled());
    details.insert(QStringLiteral("btn_save_enabled"), m_ui->btnSaveChanges->isEnabled());
    details.insert(QStringLiteral("btn_delete_enabled"), m_ui->btnDeleteCommand->isEnabled());
    details.insert(QStringLiteral("btn_discard_enabled"), m_ui->btnDiscardCommand->isEnabled());
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("handler_enter"), details);
}

void TabletGesturesConfigWidget::logHandlerExit(const QString &actionName, const QString &result, const QString &reason)
{
    QJsonObject details;
    details.insert(QStringLiteral("action_name"), actionName);
    details.insert(QStringLiteral("result"), result);
    if (!reason.isEmpty()) {
        details.insert(QStringLiteral("reason"), reason);
    }
    details.insert(QStringLiteral("current_slot"), m_currentEditingSlot);
    details.insert(QStringLiteral("command_count"), m_commands.size());
    details.insert(QStringLiteral("command_limit"), m_commandLimit);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("handler_exit"), details);
}

void TabletGesturesConfigWidget::populateActionCombos()
{
    const auto combos = {
        m_ui->kcfg_BottomLeftAction,
        m_ui->kcfg_BottomCenterAction,
        m_ui->kcfg_BottomRightAction,
        m_ui->kcfg_TopAction,
        m_ui->kcfg_RightAction,
        m_ui->kcfg_LeftAction
    };
    for (auto *combo : combos) {
        populateSingleCombo(combo);
    }
}

void TabletGesturesConfigWidget::populateSingleCombo(QComboBox *combo)
{
    const QString currentData = combo->currentData().toString();
    const QString currentText = combo->currentText();

    const bool blocked = combo->blockSignals(true);
    combo->clear();

    // 1. Built-in actions
    combo->addItem(tr("Application Launcher"), QStringLiteral("launcher"));
    combo->addItem(tr("System Tray Popup Menu"), QStringLiteral("shortcut:plasmashell:activate widget 145"));
    combo->addItem(tr("Toggle Taskbar / Dock"), QStringLiteral("shortcut:plasmashell:activate widget 111"));
    combo->addItem(tr("Notifications Panel"), QStringLiteral("shortcut:plasmashell:activate widget 157"));
    combo->addItem(tr("Activities / Task Switcher"), QStringLiteral("shortcut:plasmashell:manage activities"));
    combo->addItem(tr("Toggle Fullscreen / Expose"), QStringLiteral("fullscreen_or_expose"));
    combo->addItem(tr("Overview"), QStringLiteral("overview"));
    combo->addItem(tr("Show Desktop"), QStringLiteral("show_desktop"));
    combo->addItem(tr("Toggle Virtual Keyboard"), QStringLiteral("virtual_keyboard"));

    // 2. Custom commands currently in the pack
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        const int slot = it.key();
        const auto &cmdItem = it.value();
        const QString label = cmdItem.title.isEmpty() ? cmdItem.command : cmdItem.title;
        const QString displayName = tr("Custom: %1").arg(label);
        combo->addItem(displayName, QStringLiteral("custom_%1").arg(slot));
    }

    // 3. Option to configure / add custom command
    combo->addItem(tr("⚙ Custom Command... (Add / Edit)"), QStringLiteral("__open_custom_tab__"));

    // 4. None / Disabled
    combo->addItem(tr("None (Disabled)"), QStringLiteral("none"));

    combo->blockSignals(blocked);

    if (!currentData.isEmpty()) {
        selectActionInCombo(combo, currentData);
    } else if (!currentText.isEmpty()) {
        selectActionInCombo(combo, currentText);
    }
}

void TabletGesturesConfigWidget::refreshCustomCommandsList()
{
    m_ui->label_ListHeading->setText(tr("Configured Custom Commands: %1 / %2 (gesture hotspots: %3)")
        .arg(m_commands.size()).arg(m_commandLimit).arg(m_commandLimit));

    const bool blocked = m_ui->listCustomCommands->blockSignals(true);
    m_ui->listCustomCommands->clear();

    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        const int slot = it.key();
        const auto &cmd = it.value();
        QString itemText;
        if (cmd.title.isEmpty() || cmd.title == cmd.command) {
            itemText = tr("[custom_%1] %2").arg(slot).arg(cmd.command);
        } else {
            itemText = tr("[custom_%1] %2 — %3").arg(slot).arg(cmd.title).arg(cmd.command);
        }

        auto *item = new QListWidgetItem(itemText, m_ui->listCustomCommands);
        item->setData(Qt::UserRole, slot);
    }

    m_ui->listCustomCommands->blockSignals(blocked);
}

int TabletGesturesConfigWidget::resolveCustomSlotFromAction(const QString &action) const
{
    const QString a = action.trimmed();
    if (a.isEmpty() || a.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
        return -1;
    }

    // Direct slot matches: custom_1 .. custom_N
    if (a.startsWith(QStringLiteral("custom_"), Qt::CaseInsensitive)) {
        bool ok = false;
        int s = a.mid(7).toInt(&ok);
        if (ok && m_commands.contains(s)) {
            return s;
        }
    }

    // Match title or command
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        if (!it.value().title.isEmpty() && a.compare(it.value().title, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
        if (!it.value().command.isEmpty() && a.compare(it.value().command, Qt::CaseInsensitive) == 0) {
            return it.key();
        }
    }

    return -1;
}

void TabletGesturesConfigWidget::onPencilClicked(int edgeIndex)
{
    logHandlerEnter(QStringLiteral("pencil_click"));
    m_ui->tabWidget->setCurrentIndex(1);

    QComboBox *combo = nullptr;
    QString edgeName;
    switch (edgeIndex) {
    case 0: combo = m_ui->kcfg_BottomLeftAction; edgeName = QStringLiteral("Bottom Left"); break;
    case 1: combo = m_ui->kcfg_BottomCenterAction; edgeName = QStringLiteral("Bottom Center"); break;
    case 2: combo = m_ui->kcfg_BottomRightAction; edgeName = QStringLiteral("Bottom Right"); break;
    case 3: combo = m_ui->kcfg_TopAction; edgeName = QStringLiteral("Top Edge Swipe Down"); break;
    case 4: combo = m_ui->kcfg_RightAction; edgeName = QStringLiteral("Right Edge Swipe Inward"); break;
    case 5: combo = m_ui->kcfg_LeftAction; edgeName = QStringLiteral("Left Edge Swipe Inward"); break;
    }

    QString currentVal;
    int targetSlot = -1;
    if (combo) {
        currentVal = combo->currentData().toString();
        if (currentVal.isEmpty()) {
            currentVal = combo->currentText();
        }
        targetSlot = resolveCustomSlotFromAction(currentVal);
    }

    QJsonObject details;
    details.insert(QStringLiteral("edge_index"), edgeIndex);
    details.insert(QStringLiteral("edge_name"), edgeName);
    details.insert(QStringLiteral("action_value"), currentVal);
    details.insert(QStringLiteral("target_tab"), 1);

    if (targetSlot > 0 && m_commands.contains(targetSlot)) {
        // Select matching entry in list
        for (int i = 0; i < m_ui->listCustomCommands->count(); ++i) {
            auto *item = m_ui->listCustomCommands->item(i);
            if (item && item->data(Qt::UserRole).toInt() == targetSlot) {
                m_ui->listCustomCommands->setCurrentRow(i);
                break;
            }
        }
        details.insert(QStringLiteral("resolved_slot"), targetSlot);
        details.insert(QStringLiteral("mode"), QStringLiteral("edit"));
        details.insert(QStringLiteral("result"), QStringLiteral("ok"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("pencil_click"), details);
        logHandlerExit(QStringLiteral("pencil_click"), QStringLiteral("ok"));
    } else {
        enterNewCommandMode(QStringLiteral("pencil"));
        details.insert(QStringLiteral("mode"), QStringLiteral("new"));
        details.insert(QStringLiteral("result"), QStringLiteral("ok"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("pencil_click"), details);
        logHandlerExit(QStringLiteral("pencil_click"), QStringLiteral("ok"));
    }
}

void TabletGesturesConfigWidget::onCommandListSelectionChanged()
{
    auto *item = m_ui->listCustomCommands->currentItem();
    if (!item) {
        return;
    }

    int slot = item->data(Qt::UserRole).toInt();
    if (!m_commands.contains(slot)) {
        return;
    }

    m_currentEditingSlot = slot;
    const auto &cmd = m_commands[slot];
    m_savedTitle = cmd.title;
    m_savedCommand = cmd.command;

    m_ui->labelCommandStatus->setText(tr("Editing Command (custom_%1)").arg(slot));

    const bool b1 = m_ui->editCommandTitle->blockSignals(true);
    const bool b2 = m_ui->editCommandLine->blockSignals(true);

    m_ui->editCommandTitle->setText(cmd.title);
    m_ui->editCommandLine->setText(cmd.command);

    m_ui->editCommandTitle->blockSignals(b1);
    m_ui->editCommandLine->blockSignals(b2);

    m_ui->editCommandTitle->setEnabled(true);
    m_ui->editCommandLine->setEnabled(true);

    m_ui->btnNewCommand->setEnabled(true);
    m_ui->btnAddCommand->setEnabled(false);
    m_ui->btnAddCommand->setToolTip(tr("Add is only available in New Command mode."));
    m_ui->btnSaveChanges->setEnabled(false);
    m_ui->btnSaveChanges->setToolTip(tr("Modify Title or Command to enable saving changes."));
    m_ui->btnDeleteCommand->setEnabled(true);
    m_ui->btnDeleteCommand->setToolTip(tr("Delete this custom command from the pack."));
    m_ui->btnDiscardCommand->setEnabled(false);
    m_ui->btnDiscardCommand->setToolTip(tr("No changes to discard."));

    QJsonObject details;
    details.insert(QStringLiteral("selected_slot"), slot);
    details.insert(QStringLiteral("title"), DiagnosticsLogger::sanitizeForLog(cmd.title));
    details.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(cmd.command));
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("select_command"), details);
}

int TabletGesturesConfigWidget::findNextAvailableSlot() const
{
    for (int i = 1; i <= m_commandLimit; ++i) {
        if (!m_commands.contains(i)) {
            return i;
        }
    }
    // If capacity allows beyond configured hotspots, find smallest free positive integer
    int s = 1;
    while (m_commands.contains(s)) {
        s++;
    }
    return s;
}

void TabletGesturesConfigWidget::enterNewCommandMode(const QString &origin)
{
    m_ui->listCustomCommands->clearSelection();
    m_currentEditingSlot = -1;
    m_savedTitle.clear();
    m_savedCommand.clear();

    const bool b1 = m_ui->editCommandTitle->blockSignals(true);
    const bool b2 = m_ui->editCommandLine->blockSignals(true);
    m_ui->editCommandTitle->clear();
    m_ui->editCommandLine->clear();
    m_ui->editCommandTitle->blockSignals(b1);
    m_ui->editCommandLine->blockSignals(b2);

    const bool hasCapacity = m_commands.size() < m_commandLimit && findNextAvailableSlot() > 0;
    int nextSlot = findNextAvailableSlot();

    QJsonObject details;
    details.insert(QStringLiteral("origin"), origin);
    details.insert(QStringLiteral("has_capacity"), hasCapacity);
    details.insert(QStringLiteral("target_slot"), nextSlot);
    details.insert(QStringLiteral("command_count"), m_commands.size());
    details.insert(QStringLiteral("command_limit"), m_commandLimit);

    if (hasCapacity) {
        m_ui->labelCommandStatus->setText(tr("New Command Mode — new command will use custom_%1").arg(nextSlot));
        m_ui->editCommandTitle->setEnabled(true);
        m_ui->editCommandLine->setEnabled(true);
        m_ui->btnNewCommand->setEnabled(true);
        m_ui->btnAddCommand->setEnabled(false);
        m_ui->btnAddCommand->setToolTip(tr("Enter a command in the Command field below to enable adding."));
        m_ui->btnSaveChanges->setEnabled(false);
        m_ui->btnSaveChanges->setToolTip(tr("Save is only available when editing an existing command."));
        m_ui->btnDeleteCommand->setEnabled(false);
        m_ui->btnDeleteCommand->setToolTip(tr("Delete is only available when an existing command is selected."));
        m_ui->btnDiscardCommand->setEnabled(false);
        m_ui->btnDiscardCommand->setToolTip(tr("No text to discard."));
        details.insert(QStringLiteral("result"), QStringLiteral("ok"));
    } else {
        m_ui->labelCommandStatus->setText(tr("All available command slots are in use (%1 configured gesture hotspots). Delete a command before adding another.").arg(m_commandLimit));
        m_ui->editCommandTitle->setEnabled(false);
        m_ui->editCommandLine->setEnabled(false);
        m_ui->btnNewCommand->setEnabled(false);
        m_ui->btnAddCommand->setEnabled(false);
        m_ui->btnAddCommand->setToolTip(tr("Command capacity reached. Delete a command first."));
        m_ui->btnSaveChanges->setEnabled(false);
        m_ui->btnDeleteCommand->setEnabled(false);
        m_ui->btnDiscardCommand->setEnabled(false);
        details.insert(QStringLiteral("result"), QStringLiteral("limit_reached"));
    }

    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("enter_new_mode"), details);
}

void TabletGesturesConfigWidget::onNewCommandClicked()
{
    logHandlerEnter(QStringLiteral("btn_new_click"));
    enterNewCommandMode(QStringLiteral("user_click"));
    logHandlerExit(QStringLiteral("btn_new_click"), QStringLiteral("ok"));
}

void TabletGesturesConfigWidget::onAddCommandClicked()
{
    logHandlerEnter(QStringLiteral("btn_add_click"));
    QString cmd = m_ui->editCommandLine->text().trimmed();
    QString title = m_ui->editCommandTitle->text().trimmed();
    if (title.isEmpty()) {
        title = cmd;
    }

    QJsonObject details;
    details.insert(QStringLiteral("title"), DiagnosticsLogger::sanitizeForLog(title));
    details.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(cmd));

    if (cmd.isEmpty()) {
        m_ui->labelCommandStatus->setText(tr("Enter a command before adding it."));
        details.insert(QStringLiteral("result"), QStringLiteral("error"));
        details.insert(QStringLiteral("reason"), QStringLiteral("empty_command"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("WARN"), QStringLiteral("btn_add_click"), details);
        logHandlerExit(QStringLiteral("btn_add_click"), QStringLiteral("error"), QStringLiteral("empty_command"));
        return;
    }

    if (m_commands.size() >= m_commandLimit) {
        m_ui->labelCommandStatus->setText(tr("The command limit matches the %1 configured gesture hotspots.").arg(m_commandLimit));
        details.insert(QStringLiteral("result"), QStringLiteral("error"));
        details.insert(QStringLiteral("reason"), QStringLiteral("capacity_limit"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("WARN"), QStringLiteral("btn_add_click"), details);
        logHandlerExit(QStringLiteral("btn_add_click"), QStringLiteral("error"), QStringLiteral("capacity_limit"));
        return;
    }

    int slot = findNextAvailableSlot();
    m_commands[slot] = CommandItem { slot, title, cmd };

    QJsonObject memDetails;
    memDetails.insert(QStringLiteral("slot"), slot);
    memDetails.insert(QStringLiteral("title"), DiagnosticsLogger::sanitizeForLog(title));
    memDetails.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(cmd));
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("command_added_in_memory"), memDetails);

    refreshCustomCommandsList();
    populateActionCombos();

    // Select the newly added command in list
    for (int i = 0; i < m_ui->listCustomCommands->count(); ++i) {
        auto *item = m_ui->listCustomCommands->item(i);
        if (item && item->data(Qt::UserRole).toInt() == slot) {
            m_ui->listCustomCommands->setCurrentRow(i);
            break;
        }
    }

    details.insert(QStringLiteral("result"), QStringLiteral("ok"));
    details.insert(QStringLiteral("assigned_slot"), slot);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("btn_add_click"), details);

    Q_EMIT changed();
    logHandlerExit(QStringLiteral("btn_add_click"), QStringLiteral("ok"));
}

void TabletGesturesConfigWidget::onSaveChangesClicked()
{
    logHandlerEnter(QStringLiteral("btn_save_click"));
    QJsonObject details;
    if (m_currentEditingSlot == -1 || !m_commands.contains(m_currentEditingSlot)) {
        details.insert(QStringLiteral("result"), QStringLiteral("error"));
        details.insert(QStringLiteral("reason"), QStringLiteral("no_selection"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("WARN"), QStringLiteral("btn_save_click"), details);
        logHandlerExit(QStringLiteral("btn_save_click"), QStringLiteral("error"), QStringLiteral("no_selection"));
        return;
    }

    QString cmd = m_ui->editCommandLine->text().trimmed();
    QString title = m_ui->editCommandTitle->text().trimmed();
    if (title.isEmpty()) {
        title = cmd;
    }

    details.insert(QStringLiteral("slot"), m_currentEditingSlot);
    details.insert(QStringLiteral("title"), DiagnosticsLogger::sanitizeForLog(title));
    details.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(cmd));

    if (cmd.isEmpty()) {
        m_ui->labelCommandStatus->setText(tr("Command field cannot be empty."));
        details.insert(QStringLiteral("result"), QStringLiteral("error"));
        details.insert(QStringLiteral("reason"), QStringLiteral("empty_command"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("WARN"), QStringLiteral("btn_save_click"), details);
        logHandlerExit(QStringLiteral("btn_save_click"), QStringLiteral("error"), QStringLiteral("empty_command"));
        return;
    }

    m_commands[m_currentEditingSlot] = CommandItem { m_currentEditingSlot, title, cmd };
    m_savedTitle = title;
    m_savedCommand = cmd;

    QJsonObject memDetails;
    memDetails.insert(QStringLiteral("slot"), m_currentEditingSlot);
    memDetails.insert(QStringLiteral("title"), DiagnosticsLogger::sanitizeForLog(title));
    memDetails.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(cmd));
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("command_saved_in_memory"), memDetails);

    int curSlot = m_currentEditingSlot;
    refreshCustomCommandsList();
    populateActionCombos();

    for (int i = 0; i < m_ui->listCustomCommands->count(); ++i) {
        auto *item = m_ui->listCustomCommands->item(i);
        if (item && item->data(Qt::UserRole).toInt() == curSlot) {
            m_ui->listCustomCommands->setCurrentRow(i);
            break;
        }
    }

    m_ui->btnSaveChanges->setEnabled(false);
    m_ui->btnDiscardCommand->setEnabled(false);

    details.insert(QStringLiteral("result"), QStringLiteral("ok"));
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("btn_save_click"), details);

    Q_EMIT changed();
    logHandlerExit(QStringLiteral("btn_save_click"), QStringLiteral("ok"));
}

void TabletGesturesConfigWidget::onDeleteCommandClicked()
{
    logHandlerEnter(QStringLiteral("btn_delete_click"));
    QJsonObject details;
    if (m_currentEditingSlot == -1 || !m_commands.contains(m_currentEditingSlot)) {
        details.insert(QStringLiteral("result"), QStringLiteral("error"));
        details.insert(QStringLiteral("reason"), QStringLiteral("no_selection"));
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("WARN"), QStringLiteral("btn_delete_click"), details);
        logHandlerExit(QStringLiteral("btn_delete_click"), QStringLiteral("error"), QStringLiteral("no_selection"));
        return;
    }

    int slotToDelete = m_currentEditingSlot;
    QString deletedSlotKey = QStringLiteral("custom_%1").arg(slotToDelete);
    m_commands.remove(slotToDelete);

    QJsonObject memDetails;
    memDetails.insert(QStringLiteral("slot"), slotToDelete);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("command_deleted_in_memory"), memDetails);

    // Reset any dropdown referencing this slot to "none" (None Disabled)
    const auto combos = {
        m_ui->kcfg_BottomLeftAction,
        m_ui->kcfg_BottomCenterAction,
        m_ui->kcfg_BottomRightAction,
        m_ui->kcfg_TopAction,
        m_ui->kcfg_RightAction,
        m_ui->kcfg_LeftAction
    };
    int clearedCombos = 0;
    for (auto *cb : combos) {
        if (cb->currentData().toString().compare(deletedSlotKey, Qt::CaseInsensitive) == 0) {
            selectActionInCombo(cb, QStringLiteral("none"));
            clearedCombos++;
        }
    }

    refreshCustomCommandsList();
    populateActionCombos();

    enterNewCommandMode(QStringLiteral("after_delete"));

    details.insert(QStringLiteral("result"), QStringLiteral("ok"));
    details.insert(QStringLiteral("deleted_slot"), slotToDelete);
    details.insert(QStringLiteral("cleared_combos"), clearedCombos);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("btn_delete_click"), details);

    Q_EMIT changed();
    logHandlerExit(QStringLiteral("btn_delete_click"), QStringLiteral("ok"));
}

void TabletGesturesConfigWidget::onDiscardCommandClicked()
{
    logHandlerEnter(QStringLiteral("btn_discard_click"));
    QJsonObject details;
    details.insert(QStringLiteral("slot"), m_currentEditingSlot);

    if (m_currentEditingSlot == -1) {
        m_ui->editCommandTitle->clear();
        m_ui->editCommandLine->clear();
        m_ui->btnAddCommand->setEnabled(false);
        m_ui->btnDiscardCommand->setEnabled(false);
    } else {
        const bool b1 = m_ui->editCommandTitle->blockSignals(true);
        const bool b2 = m_ui->editCommandLine->blockSignals(true);
        m_ui->editCommandTitle->setText(m_savedTitle);
        m_ui->editCommandLine->setText(m_savedCommand);
        m_ui->editCommandTitle->blockSignals(b1);
        m_ui->editCommandLine->blockSignals(b2);

        m_ui->btnSaveChanges->setEnabled(false);
        m_ui->btnDiscardCommand->setEnabled(false);
    }

    details.insert(QStringLiteral("result"), QStringLiteral("ok"));
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("discard_completed"), details);
    logHandlerExit(QStringLiteral("btn_discard_click"), QStringLiteral("ok"));
}

void TabletGesturesConfigWidget::onEditorTextChanged()
{
    QString curTitle = m_ui->editCommandTitle->text().trimmed();
    QString curCmd = m_ui->editCommandLine->text().trimmed();
    bool hasCmd = !curCmd.isEmpty();

    if (m_currentEditingSlot == -1) {
        bool hasSlot = (m_commands.size() < m_commandLimit && findNextAvailableSlot() > 0);
        bool canAdd = hasCmd && hasSlot;
        m_ui->btnAddCommand->setEnabled(canAdd);
        if (!hasCmd) {
            m_ui->btnAddCommand->setToolTip(tr("Enter a command in the Command field below to enable adding."));
        } else if (!hasSlot) {
            m_ui->btnAddCommand->setToolTip(tr("Cannot add command: hotspot limit (%1) reached. Delete a command first.").arg(m_commandLimit));
        } else {
            m_ui->btnAddCommand->setToolTip(tr("Add this custom command to the pack."));
        }

        bool canDiscard = (!curCmd.isEmpty() || !m_ui->editCommandTitle->text().isEmpty());
        m_ui->btnDiscardCommand->setEnabled(canDiscard);
        m_ui->btnDiscardCommand->setToolTip(canDiscard ? tr("Discard entered text and reset fields.") : tr("No text to discard."));
    } else {
        bool dirty = (curTitle != m_savedTitle || curCmd != m_savedCommand);
        bool canSave = dirty && hasCmd;
        m_ui->btnSaveChanges->setEnabled(canSave);
        if (!dirty) {
            m_ui->btnSaveChanges->setToolTip(tr("Modify the Title or Command to enable saving changes."));
        } else if (!hasCmd) {
            m_ui->btnSaveChanges->setToolTip(tr("Command field cannot be empty."));
        } else {
            m_ui->btnSaveChanges->setToolTip(tr("Save changes to this custom command."));
        }

        m_ui->btnDiscardCommand->setEnabled(dirty);
        m_ui->btnDiscardCommand->setToolTip(dirty ? tr("Discard unsaved edits and revert to saved values.") : tr("No changes to discard."));
    }
}

void TabletGesturesConfigWidget::selectActionInCombo(QComboBox *combo, const QString &actionValue)
{
    const QString a = actionValue.trimmed();
    int idx = combo->findData(a);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
        return;
    }

    // Match by itemText
    idx = combo->findText(a);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
        return;
    }

    // Match by "Custom: " + text or vice-versa
    if (!a.startsWith(QStringLiteral("Custom: "), Qt::CaseInsensitive)) {
        idx = combo->findText(QStringLiteral("Custom: %1").arg(a));
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
            return;
        }
    } else {
        QString stripped = a.mid(8).trimmed();
        idx = combo->findText(stripped);
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
            return;
        }
    }

    // Match by custom slot
    int slot = resolveCustomSlotFromAction(a);
    if (slot > 0) {
        idx = combo->findData(QStringLiteral("custom_%1").arg(slot));
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
            return;
        }
    }

    // Direct text fallback
    combo->setEditText(a);
}

QString TabletGesturesConfigWidget::getActionFromCombo(const QComboBox *combo) const
{
    QString data = combo->currentData().toString();
    if (!data.isEmpty()) {
        return data;
    }
    return combo->currentText();
}

void TabletGesturesConfigWidget::load(const KConfigGroup &group)
{
    m_loading = true;

    DiagnosticsLogger::instance().setEnabled(group.readEntry("DiagnosticsEnabled", true));

    m_ui->kcfg_EdgeMargin->setValue(group.readEntry("EdgeMargin", 25));
    m_ui->kcfg_SwipeThreshold->setValue(group.readEntry("SwipeThreshold", 45));

    m_ui->kcfg_EnableTopEdge->setChecked(group.readEntry("EnableTopEdge", true));
    m_ui->kcfg_EnableBottomEdge->setChecked(group.readEntry("EnableBottomEdge", true));
    m_ui->kcfg_EnableLeftEdge->setChecked(group.readEntry("EnableLeftEdge", true));
    m_ui->kcfg_EnableRightEdge->setChecked(group.readEntry("EnableRightEdge", true));

    m_ui->kcfg_EnableButtonScaling->setChecked(group.readEntry("EnableButtonScaling", true));
    m_ui->kcfg_TabletButtonSize->setCurrentText(group.readEntry("TabletButtonSize", QStringLiteral("Very Large")));
    m_ui->kcfg_LaptopButtonSize->setCurrentText(group.readEntry("LaptopButtonSize", QStringLiteral("Medium")));

    m_ui->kcfg_EnableDisplayScaling->setChecked(group.readEntry("EnableDisplayScaling", true));
    m_ui->kcfg_TabletScale->setCurrentText(group.readEntry("TabletScale", QStringLiteral("100%")));
    m_ui->kcfg_LaptopScale->setCurrentText(group.readEntry("LaptopScale", QStringLiteral("100%")));

    m_ui->kcfg_HingeAngleThreshold->setValue(group.readEntry("HingeAngleThreshold", 185));
    m_ui->kcfg_HingePollInterval->setValue(group.readEntry("HingePollInterval", 350));

    m_ui->kcfg_TopLeftSafeWidth->setValue(group.readEntry("TopLeftSafeWidth", 100));
    m_ui->kcfg_TopLeftSafeHeight->setValue(group.readEntry("TopLeftSafeHeight", 60));
    m_ui->kcfg_TopRightSafeWidth->setValue(group.readEntry("TopRightSafeWidth", 150));
    m_ui->kcfg_TopRightSafeHeight->setValue(group.readEntry("TopRightSafeHeight", 60));

    m_ui->kcfg_BottomGesturesWithOsk->setCurrentText(group.readEntry("BottomGesturesWithOsk", QStringLiteral("Disabled")));
    m_ui->kcfg_HideKeyboardOnGesture->setChecked(group.readEntry("HideKeyboardOnGesture", true));

    // Determine hotspot capacity from configured zones
    ZoneManager configuredZones;
    configuredZones.loadFromConfigGroup(group);
    m_commandLimit = static_cast<int>(configuredZones.zones().size());
    if (m_commandLimit <= 0) {
        m_commandLimit = 6;
    }

    // Initialize custom commands store in user-owned directory
    const QString storePathSetting = group.readEntry("CustomCommandsStorePath", QString());
    m_store = std::make_unique<CustomCommandStore>(storePathSetting);
    QString storeInitError;
    if (!m_store->initialize(&storeInitError)) {
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("ERROR"), QStringLiteral("store_init_failed"), storeInitError);
    }

    // Non-destructive migration if external store is empty
    QString migrationStatus;
    m_store->migrateFromConfigGroup(group, &migrationStatus);

    // Load commands from the user-owned external store
    QString loadError;
    auto storeEntries = m_store->loadCommands(&loadError);
    m_commands.clear();
    for (auto it = storeEntries.cbegin(); it != storeEntries.cend(); ++it) {
        m_commands[it.key()] = CommandItem { it.value().slot, it.value().title, it.value().command };
    }

    refreshCustomCommandsList();
    populateActionCombos();

    // Select configured actions
    selectActionInCombo(m_ui->kcfg_BottomLeftAction, group.readEntry("BottomLeftAction", QStringLiteral("launcher")));
    selectActionInCombo(m_ui->kcfg_BottomCenterAction, group.readEntry("BottomCenterAction", QStringLiteral("shortcut:plasmashell:activate widget 111")));
    selectActionInCombo(m_ui->kcfg_BottomRightAction, group.readEntry("BottomRightAction", QStringLiteral("shortcut:plasmashell:activate widget 145")));
    selectActionInCombo(m_ui->kcfg_TopAction, group.readEntry("TopAction", QStringLiteral("Toggle Fullscreen / Expose")));
    selectActionInCombo(m_ui->kcfg_RightAction, group.readEntry("RightAction", QStringLiteral("Notifications Panel")));
    selectActionInCombo(m_ui->kcfg_LeftAction, group.readEntry("LeftAction", QStringLiteral("Activities / Task Switcher")));

    enterNewCommandMode(QStringLiteral("load"));

    // Warn if existing commands exceed current hotspot limit (do NOT delete user data!)
    if (m_commands.size() > m_commandLimit) {
        m_ui->labelCommandStatus->setText(tr("Warning: %1 custom commands exist, exceeding the current hotspot limit (%2). All commands are preserved safely; delete an existing command before adding a new one.").arg(m_commands.size()).arg(m_commandLimit));
    }

    QJsonObject loadDetails;
    loadDetails.insert(QStringLiteral("store_path"), m_store->storePath());
    loadDetails.insert(QStringLiteral("command_count"), m_commands.size());
    loadDetails.insert(QStringLiteral("command_limit"), m_commandLimit);
    loadDetails.insert(QStringLiteral("migration_status"), migrationStatus);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("kcm_loaded"), loadDetails);

    m_loading = false;
}

void TabletGesturesConfigWidget::save(KConfigGroup &group)
{
    QJsonObject applyDetails;
    applyDetails.insert(QStringLiteral("command_count"), m_commands.size());
    applyDetails.insert(QStringLiteral("command_limit"), m_commandLimit);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("config_apply_started"), applyDetails);

    group.writeEntry("EdgeMargin", m_ui->kcfg_EdgeMargin->value());
    group.writeEntry("SwipeThreshold", m_ui->kcfg_SwipeThreshold->value());

    group.writeEntry("EnableTopEdge", m_ui->kcfg_EnableTopEdge->isChecked());
    group.writeEntry("EnableBottomEdge", m_ui->kcfg_EnableBottomEdge->isChecked());
    group.writeEntry("EnableLeftEdge", m_ui->kcfg_EnableLeftEdge->isChecked());
    group.writeEntry("EnableRightEdge", m_ui->kcfg_EnableRightEdge->isChecked());

    group.writeEntry("EnableButtonScaling", m_ui->kcfg_EnableButtonScaling->isChecked());
    group.writeEntry("TabletButtonSize", m_ui->kcfg_TabletButtonSize->currentText());
    group.writeEntry("LaptopButtonSize", m_ui->kcfg_LaptopButtonSize->currentText());

    group.writeEntry("EnableDisplayScaling", m_ui->kcfg_EnableDisplayScaling->isChecked());
    group.writeEntry("TabletScale", m_ui->kcfg_TabletScale->currentText());
    group.writeEntry("LaptopScale", m_ui->kcfg_LaptopScale->currentText());

    group.writeEntry("HingeAngleThreshold", m_ui->kcfg_HingeAngleThreshold->value());
    group.writeEntry("HingePollInterval", m_ui->kcfg_HingePollInterval->value());

    group.writeEntry("TopLeftSafeWidth", m_ui->kcfg_TopLeftSafeWidth->value());
    group.writeEntry("TopLeftSafeHeight", m_ui->kcfg_TopLeftSafeHeight->value());
    group.writeEntry("TopRightSafeWidth", m_ui->kcfg_TopRightSafeWidth->value());
    group.writeEntry("TopRightSafeHeight", m_ui->kcfg_TopRightSafeHeight->value());

    group.writeEntry("BottomGesturesWithOsk", m_ui->kcfg_BottomGesturesWithOsk->currentText());
    group.writeEntry("HideKeyboardOnGesture", m_ui->kcfg_HideKeyboardOnGesture->isChecked());
    group.writeEntry("DiagnosticsEnabled", DiagnosticsLogger::instance().isEnabled());

    // 1. Save canonical commands to user-owned external command store
    if (!m_store) {
        m_store = std::make_unique<CustomCommandStore>();
    }
    m_store->initialize();

    QMap<int, CustomCommandStore::CommandEntry> storeEntries;
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        CustomCommandStore::CommandEntry entry;
        entry.slot = it.key();
        entry.title = it.value().title;
        entry.command = it.value().command;
        storeEntries[it.key()] = entry;
    }

    QString saveError;
    bool storeSaveOk = m_store->saveAllCommands(storeEntries, &saveError);
    if (!storeSaveOk) {
        QJsonObject errDetails;
        errDetails.insert(QStringLiteral("error"), saveError);
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("ERROR"), QStringLiteral("store_write_failed"), errDetails);
    } else {
        QJsonObject okDetails;
        okDetails.insert(QStringLiteral("store_path"), m_store->storePath());
        okDetails.insert(QStringLiteral("command_count"), storeEntries.size());
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("store_write_succeeded"), okDetails);
    }
    group.writeEntry("CustomCommandsStorePath", m_store->storePath());

    // 2. Dual-write CustomCommandsJson and legacy keys for backward compatibility
    QJsonObject commandRoot;
    commandRoot.insert(QStringLiteral("version"), 1);
    QJsonArray commandArray;
    for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
        QJsonObject commandObject;
        commandObject.insert(QStringLiteral("slot"), it.key());
        commandObject.insert(QStringLiteral("title"), it.value().title);
        commandObject.insert(QStringLiteral("command"), it.value().command);
        commandArray.append(commandObject);
    }
    commandRoot.insert(QStringLiteral("commands"), commandArray);
    group.writeEntry("CustomCommandsJson", QString::fromUtf8(QJsonDocument(commandRoot).toJson(QJsonDocument::Compact)));

    for (int i = 1; i <= 6; ++i) {
        const bool exists = m_commands.contains(i);
        group.writeEntry(QStringLiteral("CustomCommand%1").arg(i), exists ? m_commands[i].command : QString());
        group.writeEntry(QStringLiteral("CustomCommandTitle%1").arg(i), exists ? m_commands[i].title : QString());
        if (i == 1) {
            group.writeEntry("CustomCommandTitle", exists ? m_commands[i].title : QString());
        }
    }

    // 3. Save edge gesture mappings
    group.writeEntry("BottomLeftAction", getActionFromCombo(m_ui->kcfg_BottomLeftAction));
    group.writeEntry("BottomCenterAction", getActionFromCombo(m_ui->kcfg_BottomCenterAction));
    group.writeEntry("BottomRightAction", getActionFromCombo(m_ui->kcfg_BottomRightAction));
    group.writeEntry("TopAction", getActionFromCombo(m_ui->kcfg_TopAction));
    group.writeEntry("RightAction", getActionFromCombo(m_ui->kcfg_RightAction));
    group.writeEntry("LeftAction", getActionFromCombo(m_ui->kcfg_LeftAction));

    QJsonObject details;
    details.insert(QStringLiteral("store_path"), m_store->storePath());
    details.insert(QStringLiteral("command_count"), m_commands.size());
    details.insert(QStringLiteral("store_save_ok"), storeSaveOk);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("kcm_save_completed"), details);
}

void TabletGesturesConfigWidget::defaults()
{
    m_loading = true;

    m_ui->kcfg_EdgeMargin->setValue(25);
    m_ui->kcfg_SwipeThreshold->setValue(45);

    m_ui->kcfg_EnableTopEdge->setChecked(true);
    m_ui->kcfg_EnableBottomEdge->setChecked(true);
    m_ui->kcfg_EnableLeftEdge->setChecked(true);
    m_ui->kcfg_EnableRightEdge->setChecked(true);

    m_ui->kcfg_EnableButtonScaling->setChecked(true);
    m_ui->kcfg_TabletButtonSize->setCurrentText(QStringLiteral("Very Large"));
    m_ui->kcfg_LaptopButtonSize->setCurrentText(QStringLiteral("Medium"));

    m_ui->kcfg_EnableDisplayScaling->setChecked(true);
    m_ui->kcfg_TabletScale->setCurrentText(QStringLiteral("100%"));
    m_ui->kcfg_LaptopScale->setCurrentText(QStringLiteral("100%"));

    m_ui->kcfg_HingeAngleThreshold->setValue(185);
    m_ui->kcfg_HingePollInterval->setValue(350);

    m_ui->kcfg_TopLeftSafeWidth->setValue(100);
    m_ui->kcfg_TopLeftSafeHeight->setValue(60);
    m_ui->kcfg_TopRightSafeWidth->setValue(150);
    m_ui->kcfg_TopRightSafeHeight->setValue(60);

    m_ui->kcfg_BottomGesturesWithOsk->setCurrentText(QStringLiteral("Disabled"));
    m_ui->kcfg_HideKeyboardOnGesture->setChecked(true);

    // CRITICAL: Do NOT erase m_commands on Defaults!
    // Custom commands are user data; Defaults resets gesture actions to defaults while preserving commands.
    ZoneManager defaultZones;
    m_commandLimit = static_cast<int>(defaultZones.zones().size());
    if (m_commandLimit <= 0) {
        m_commandLimit = 6;
    }

    refreshCustomCommandsList();
    populateActionCombos();

    selectActionInCombo(m_ui->kcfg_BottomLeftAction, QStringLiteral("launcher"));
    selectActionInCombo(m_ui->kcfg_BottomCenterAction, QStringLiteral("shortcut:plasmashell:activate widget 111"));
    selectActionInCombo(m_ui->kcfg_BottomRightAction, QStringLiteral("shortcut:plasmashell:activate widget 145"));
    selectActionInCombo(m_ui->kcfg_TopAction, QStringLiteral("fullscreen_or_expose"));
    selectActionInCombo(m_ui->kcfg_RightAction, QStringLiteral("shortcut:plasmashell:activate widget 157"));
    selectActionInCombo(m_ui->kcfg_LeftAction, QStringLiteral("shortcut:plasmashell:manage activities"));

    enterNewCommandMode(QStringLiteral("defaults"));

    QJsonObject defDetails;
    defDetails.insert(QStringLiteral("commands_preserved"), m_commands.size());
    defDetails.insert(QStringLiteral("command_limit"), m_commandLimit);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("defaults_applied"), defDetails);

    m_loading = false;
}

// Direct test-invoked helpers for programmatic verification
void TabletGesturesConfigWidget::triggerPencil(int edgeIndex) { onPencilClicked(edgeIndex); }
void TabletGesturesConfigWidget::triggerNewCommand() { onNewCommandClicked(); }
void TabletGesturesConfigWidget::triggerAddCommand(const QString &title, const QString &command)
{
    onNewCommandClicked();
    m_ui->editCommandTitle->setText(title);
    m_ui->editCommandLine->setText(command);
    onAddCommandClicked();
}
void TabletGesturesConfigWidget::triggerSaveChanges(const QString &newTitle, const QString &newCommand)
{
    m_ui->editCommandTitle->setText(newTitle);
    m_ui->editCommandLine->setText(newCommand);
    onSaveChangesClicked();
}
void TabletGesturesConfigWidget::triggerDeleteCommand() { onDeleteCommandClicked(); }
void TabletGesturesConfigWidget::triggerDiscard() { onDiscardCommandClicked(); }

} // namespace KWin
