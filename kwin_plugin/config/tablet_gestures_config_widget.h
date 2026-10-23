/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QWidget>
#include <QMap>
#include <QString>
#include <memory>
#include <KConfigGroup>
#include "../custom_command_store.h"

#include <QList>

namespace Ui {
class TabletGesturesConfig;
}

class QComboBox;
class QAbstractButton;

namespace KWin
{

class TabletGesturesConfigWidget : public QWidget
{
    Q_OBJECT

public:
    struct CommandItem {
        int slot = 0;
        QString title;
        QString command;
    };

    explicit TabletGesturesConfigWidget(QWidget *parent = nullptr);
    ~TabletGesturesConfigWidget() override;

    void load(const KConfigGroup &group);
    void save(KConfigGroup &group);
    void defaults();

    // Helpers for inspection and unit testing
    Ui::TabletGesturesConfig *ui() const { return m_ui.get(); }
    int currentEditingSlot() const { return m_currentEditingSlot; }
    int commandLimit() const { return m_commandLimit; }
    QMap<int, CommandItem> customCommands() const { return m_commands; }
    CustomCommandStore *store() const { return m_store.get(); }

    void triggerPencil(int edgeIndex);
    void triggerNewCommand();
    void triggerAddCommand(const QString &title, const QString &command);
    void triggerSaveChanges(const QString &newTitle, const QString &newCommand);
    void triggerDeleteCommand();
    void triggerDiscard();

Q_SIGNALS:
    void changed();

private Q_SLOTS:
    void onPencilClicked(int edgeIndex);
    void onCommandListSelectionChanged();
    void onNewCommandClicked();
    void onAddCommandClicked();
    void onSaveChangesClicked();
    void onDeleteCommandClicked();
    void onDiscardCommandClicked();
    void onEditorTextChanged();
    void onWidgetModified();

    bool eventFilter(QObject *watched, QEvent *event) override;

    void enterNewCommandMode(const QString &origin);

private:
    void setupConnections();
    void setupDiagnosticObservers();
    void logRawInput(QWidget *widget, const QString &eventType, Qt::MouseButton button, const QPoint &pos, const QPoint &globalPos);
    void logRawKeyInput(QWidget *widget, const QString &eventType, int key);
    void logHandlerEnter(const QString &actionName);
    void logHandlerExit(const QString &actionName, const QString &result, const QString &reason = QString());

    void populateActionCombos();
    void populateSingleCombo(QComboBox *combo);
    void refreshCustomCommandsList();
    void updateEditorState();
    int findNextAvailableSlot() const;
    void selectActionInCombo(QComboBox *combo, const QString &actionValue);
    QString getActionFromCombo(const QComboBox *combo) const;
    int resolveCustomSlotFromAction(const QString &action) const;

    std::unique_ptr<Ui::TabletGesturesConfig> m_ui;
    std::unique_ptr<CustomCommandStore> m_store;
    QList<QAbstractButton*> m_monitoredButtons;
    QMap<int, CommandItem> m_commands; // Stable numeric command IDs: custom_1, custom_2, ...
    int m_commandLimit = 6; // Number of configured gesture hotspots, read from ZoneManager
    int m_currentEditingSlot = -1; // -1 = New Command mode, positive value = editing that slot
    QString m_savedTitle;
    QString m_savedCommand;
    bool m_loading = false;
};

} // namespace KWin
