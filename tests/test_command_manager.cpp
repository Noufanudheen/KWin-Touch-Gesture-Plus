/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../kwin_plugin/config/tablet_gestures_config_widget.h"
#include "../kwin_plugin/config/ui_config.h"
#include "../kwin_plugin/zone_manager.h"
#include "../kwin_plugin/diagnostics_logger.h"
#include "../kwin_plugin/custom_command_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <KSharedConfig>

using namespace KWin;

class TestCommandManager : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_testConfigDir;
    std::unique_ptr<QTemporaryDir> m_testStateDir;

    static void addCommand(TabletGesturesConfigWidget &widget, const QString &title, const QString &command)
    {
        widget.triggerNewCommand();
        auto *ui = widget.ui();
        ui->editCommandTitle->setText(title);
        ui->editCommandLine->setText(command);
        Q_ASSERT(ui->btnAddCommand->isEnabled());
        QTest::mouseClick(ui->btnAddCommand, Qt::LeftButton);
    }

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        m_testConfigDir = std::make_unique<QTemporaryDir>();
        qputenv("XDG_CONFIG_HOME", m_testConfigDir->path().toUtf8());
        m_testStateDir = std::make_unique<QTemporaryDir>();
        qputenv("XDG_STATE_HOME", m_testStateDir->path().toUtf8());
    }

    void cleanupTestCase()
    {
        DiagnosticsLogger::instance().setLogPath(QString());
        m_testConfigDir.reset();
        m_testStateDir.reset();
    }

    void testInitialStateIsBlank()
    {
        TabletGesturesConfigWidget widget;
        auto *ui = widget.ui();

        QCOMPARE(widget.customCommands().size(), 0);
        QCOMPARE(ui->listCustomCommands->count(), 0);
        QCOMPARE(widget.commandLimit(), 6); // Six default configured hotspots.
        QCOMPARE(ui->tabWidget->currentIndex(), 0);
        QVERIFY(ui->listCustomCommands->selectedItems().isEmpty());
        QVERIFY(ui->editCommandTitle->text().isEmpty());
        QVERIFY(ui->editCommandLine->text().isEmpty());
        QVERIFY(!ui->btnAddCommand->isEnabled());

        // Built-in actions (9), Custom Command navigation item (1), and None/Disabled (1) = 11 items.
        QCOMPARE(ui->kcfg_BottomLeftAction->count(), 11);
        QCOMPARE(ui->kcfg_BottomCenterAction->count(), 11);
        QCOMPARE(ui->kcfg_BottomRightAction->count(), 11);
        QCOMPARE(ui->kcfg_TopAction->count(), 11);
        QCOMPARE(ui->kcfg_RightAction->count(), 11);
        QCOMPARE(ui->kcfg_LeftAction->count(), 11);
    }

    void testDropdownCustomCommandOptionNavigatesToCustomCommandsTab()
    {
        TabletGesturesConfigWidget widget;
        auto *ui = widget.ui();
        widget.show();
        const QList<QComboBox *> combos = {
            ui->kcfg_BottomLeftAction,
            ui->kcfg_BottomCenterAction,
            ui->kcfg_BottomRightAction,
            ui->kcfg_TopAction,
            ui->kcfg_RightAction,
            ui->kcfg_LeftAction,
        };

        for (QComboBox *combo : combos) {
            ui->tabWidget->setCurrentIndex(0);
            int navIdx = combo->findData(QStringLiteral("__open_custom_tab__"));
            QVERIFY(navIdx >= 0);
            combo->setCurrentIndex(navIdx);
            Q_EMIT combo->activated(navIdx);

            QCOMPARE(ui->tabWidget->currentIndex(), 1);
            QCOMPARE(widget.currentEditingSlot(), -1);
            QVERIFY(ui->listCustomCommands->selectedItems().isEmpty());
            QVERIFY(ui->editCommandTitle->text().isEmpty());
            QVERIFY(ui->editCommandLine->text().isEmpty());
            QVERIFY(!ui->btnAddCommand->isEnabled());
        }
    }

    void testAddCommandWithBlankTitleReplacesWithCommandAndPopulatesCombos()
    {
        TabletGesturesConfigWidget widget;
        auto *ui = widget.ui();
        widget.show();
        widget.triggerNewCommand();
        // Leave title blank - should automatically default to command
        ui->editCommandTitle->clear();
        ui->editCommandLine->setText(QStringLiteral("exec:/usr/bin/firefox"));
        QVERIFY(ui->btnAddCommand->isEnabled());
        QTest::mouseClick(ui->btnAddCommand, Qt::LeftButton);

        QCOMPARE(widget.customCommands().size(), 1);
        QCOMPARE(widget.currentEditingSlot(), 1);
        QCOMPARE(ui->listCustomCommands->count(), 1);
        // Verify title was replaced by command
        QCOMPARE(widget.customCommands()[1].title, QStringLiteral("exec:/usr/bin/firefox"));
        QCOMPARE(ui->listCustomCommands->item(0)->text(), QStringLiteral("[custom_1] exec:/usr/bin/firefox"));

        const int customIndex = ui->kcfg_BottomLeftAction->findData(QStringLiteral("custom_1"));
        QVERIFY(customIndex >= 0);
        QCOMPARE(ui->kcfg_BottomLeftAction->itemText(customIndex), QStringLiteral("Custom: exec:/usr/bin/firefox"));
        ui->kcfg_BottomLeftAction->setCurrentIndex(customIndex);

        // Verify editing in Custom Command tab
        ui->listCustomCommands->setCurrentRow(0);
        QCOMPARE(widget.currentEditingSlot(), 1);
        QCOMPARE(ui->editCommandTitle->text(), QStringLiteral("exec:/usr/bin/firefox"));
        QCOMPARE(ui->editCommandLine->text(), QStringLiteral("exec:/usr/bin/firefox"));
        QVERIFY(!ui->btnSaveChanges->isEnabled());
        QVERIFY(ui->btnDeleteCommand->isEnabled());
    }

    void testEditSaveAndDiscard()
    {
        TabletGesturesConfigWidget widget;
        auto *ui = widget.ui();
        widget.show();
        addCommand(widget, QStringLiteral("Terminal"), QStringLiteral("exec:/usr/bin/konsole"));

        ui->editCommandTitle->setText(QStringLiteral("Unsaved title"));
        ui->editCommandLine->setText(QStringLiteral("exec:/bin/false"));
        QVERIFY(ui->btnSaveChanges->isEnabled());
        QVERIFY(ui->btnDiscardCommand->isEnabled());
        QTest::mouseClick(ui->btnDiscardCommand, Qt::LeftButton);
        QCOMPARE(ui->editCommandTitle->text(), QStringLiteral("Terminal"));
        QCOMPARE(ui->editCommandLine->text(), QStringLiteral("exec:/usr/bin/konsole"));
        QVERIFY(!ui->btnSaveChanges->isEnabled());

        ui->editCommandTitle->setText(QStringLiteral("Terminal Pro"));
        ui->editCommandLine->setText(QStringLiteral("exec:/usr/bin/konsole --separate"));
        QTest::mouseClick(ui->btnSaveChanges, Qt::LeftButton);
        QCOMPARE(widget.currentEditingSlot(), 1); // Identity preserved; no duplicate.
        QCOMPARE(widget.customCommands().size(), 1);
        QCOMPARE(widget.customCommands().value(1).title, QStringLiteral("Terminal Pro"));
        QCOMPARE(widget.customCommands().value(1).command, QStringLiteral("exec:/usr/bin/konsole --separate"));
        const int itemIndex = ui->kcfg_BottomRightAction->findData(QStringLiteral("custom_1"));
        QVERIFY(itemIndex >= 0);
        QCOMPARE(ui->kcfg_BottomRightAction->itemText(itemIndex), QStringLiteral("Custom: Terminal Pro"));
    }

    void testDeleteClearsReferencingAction()
    {
        TabletGesturesConfigWidget widget;
        auto *ui = widget.ui();
        widget.show();
        addCommand(widget, QStringLiteral("App 1"), QStringLiteral("exec:/usr/bin/app1"));
        addCommand(widget, QStringLiteral("App 2"), QStringLiteral("exec:/usr/bin/app2"));
        addCommand(widget, QStringLiteral("App 3"), QStringLiteral("exec:/usr/bin/app3"));

        const int index = ui->kcfg_BottomRightAction->findData(QStringLiteral("custom_3"));
        QVERIFY(index >= 0);
        ui->kcfg_BottomRightAction->setCurrentIndex(index);
        ui->listCustomCommands->setCurrentRow(2);
        QCOMPARE(widget.currentEditingSlot(), 3);
        QTest::mouseClick(ui->btnDeleteCommand, Qt::LeftButton);

        QCOMPARE(widget.customCommands().size(), 2);
        QVERIFY(!widget.customCommands().contains(3));
        QCOMPARE(ui->kcfg_BottomRightAction->currentData().toString(), QStringLiteral("none"));
        QCOMPARE(ui->kcfg_BottomRightAction->findData(QStringLiteral("custom_3")), -1);
        QCOMPARE(widget.currentEditingSlot(), -1);
        QVERIFY(ui->editCommandLine->text().isEmpty());
    }

    void testLimitTracksConfiguredHotspotCount()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        const QString path = file.fileName();
        file.close();
        auto config = KSharedConfig::openConfig(path);
        auto group = config->group(QStringLiteral("Effect-tablet_gestures"));

        // A user-defined configuration with two hotspots allows two commands.
        QJsonObject z1;
        z1.insert(QStringLiteral("id"), QStringLiteral("zone-one"));
        z1.insert(QStringLiteral("name"), QStringLiteral("Zone One"));
        z1.insert(QStringLiteral("edge"), QStringLiteral("bottom"));
        z1.insert(QStringLiteral("startFraction"), 0.0);
        z1.insert(QStringLiteral("endFraction"), 0.5);
        z1.insert(QStringLiteral("gestureType"), QStringLiteral("inward_swipe"));
        z1.insert(QStringLiteral("activationThreshold"), 45);
        z1.insert(QStringLiteral("enabled"), true);
        z1.insert(QStringLiteral("action"), QStringLiteral("none"));
        z1.insert(QStringLiteral("oskPolicy"), QStringLiteral("Always"));
        z1.insert(QStringLiteral("safeMarginStart"), 0);
        z1.insert(QStringLiteral("safeMarginEnd"), 0);
        z1.insert(QStringLiteral("priority"), 0);
        QJsonObject z2 = z1;
        z2.insert(QStringLiteral("id"), QStringLiteral("zone-two"));
        z2.insert(QStringLiteral("name"), QStringLiteral("Zone Two"));
        z2.insert(QStringLiteral("startFraction"), 0.5);
        z2.insert(QStringLiteral("endFraction"), 1.0);
        QJsonObject root;
        root.insert(QStringLiteral("version"), QStringLiteral("1.0"));
        root.insert(QStringLiteral("zones"), QJsonArray{z1, z2});
        group.writeEntry("CustomZonesJson", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
        config->sync();

        TabletGesturesConfigWidget widget;
        widget.load(group);
        widget.show();
        auto *ui = widget.ui();
        QCOMPARE(widget.commandLimit(), 2);
        addCommand(widget, QStringLiteral("One"), QStringLiteral("exec:/usr/bin/true"));
        addCommand(widget, QStringLiteral("Two"), QStringLiteral("exec:/usr/bin/false"));
        QCOMPARE(widget.customCommands().size(), 2);

        widget.triggerNewCommand();
        QVERIFY(!ui->editCommandLine->isEnabled());
        QVERIFY(!ui->btnAddCommand->isEnabled());
        QVERIFY(ui->labelCommandStatus->text().contains(QStringLiteral("2 configured gesture hotspots")));
        QVERIFY(ui->label_ListHeading->text().contains(QStringLiteral("2 / 2")));
    }

    void testUserOwnedExternalStorePersistence()
    {
        QTemporaryDir storeDir;
        QVERIFY(storeDir.isValid());
        const QString storePath = storeDir.path() + QStringLiteral("/commands");

        QTemporaryFile configFile;
        QVERIFY(configFile.open());
        const QString configPath = configFile.fileName();
        configFile.close();

        auto config = KSharedConfig::openConfig(configPath);
        auto group = config->group(QStringLiteral("Effect-tablet_gestures"));
        group.writeEntry("CustomCommandsStorePath", storePath);
        config->sync();

        // 1. Save commands via widget
        {
            TabletGesturesConfigWidget widget;
            widget.load(group);
            addCommand(widget, QStringLiteral("Editor"), QStringLiteral("exec:/usr/bin/kate"));
            addCommand(widget, QStringLiteral("Browser"), QStringLiteral("exec:/usr/bin/firefox"));
            widget.save(group);
            config->sync();
        }

        // Verify physical files on disk
        QVERIFY(QFile::exists(storePath + QStringLiteral("/manifest.json")));
        QVERIFY(QFile::exists(storePath + QStringLiteral("/custom_1.json")));
        QVERIFY(QFile::exists(storePath + QStringLiteral("/custom_2.json")));

        // 2. Load into fresh widget
        {
            TabletGesturesConfigWidget loadedWidget;
            auto reloadConfig = KSharedConfig::openConfig(configPath);
            reloadConfig->reparseConfiguration();
            loadedWidget.load(reloadConfig->group(QStringLiteral("Effect-tablet_gestures")));

            QCOMPARE(loadedWidget.customCommands().size(), 2);
            QCOMPARE(loadedWidget.customCommands().value(1).title, QStringLiteral("Editor"));
            QCOMPARE(loadedWidget.customCommands().value(1).command, QStringLiteral("exec:/usr/bin/kate"));
            QCOMPARE(loadedWidget.customCommands().value(2).title, QStringLiteral("Browser"));
            QCOMPARE(loadedWidget.customCommands().value(2).command, QStringLiteral("exec:/usr/bin/firefox"));
        }
    }

    void testDefaultsPreservesUserCommands()
    {
        TabletGesturesConfigWidget widget;
        addCommand(widget, QStringLiteral("Cmd1"), QStringLiteral("exec:/usr/bin/first"));
        addCommand(widget, QStringLiteral("Cmd2"), QStringLiteral("exec:/usr/bin/second"));
        QCOMPARE(widget.customCommands().size(), 2);

        // Calling defaults must NOT erase user custom commands
        widget.defaults();
        QCOMPARE(widget.customCommands().size(), 2);
        QCOMPARE(widget.customCommands().value(1).title, QStringLiteral("Cmd1"));
        QCOMPARE(widget.customCommands().value(2).title, QStringLiteral("Cmd2"));
    }

    void testDiagnosticsLoggingAndPrivacy()
    {
        QTemporaryFile logFile;
        QVERIFY(logFile.open());
        const QString logPath = logFile.fileName();
        logFile.close();

        DiagnosticsLogger::instance().setLogPath(logPath);
        DiagnosticsLogger::instance().setEnabled(true);

        TabletGesturesConfigWidget widget;
        widget.show();
        addCommand(widget, QStringLiteral("SecretApp"), QStringLiteral("exec:/usr/bin/super_secret_script.sh --token=XYZ123"));

        // Read log file
        QFile file(logPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString logContent = QString::fromUtf8(file.readAll());
        file.close();

        // Must record structured events from raw input, clicked observer, handler, and state transitions
        QVERIFY(logContent.contains(QStringLiteral("raw_input")));
        QVERIFY(logContent.contains(QStringLiteral("button_clicked_signal")));
        QVERIFY(logContent.contains(QStringLiteral("handler_enter")));
        QVERIFY(logContent.contains(QStringLiteral("handler_exit")));
        QVERIFY(logContent.contains(QStringLiteral("btn_add_click")));
        QVERIFY(logContent.contains(QStringLiteral("command_added_in_memory")));
        QVERIFY(logContent.contains(QStringLiteral("SecretApp")));

        // False-positive prevention: enter_new_mode on init must not be logged as btn_new_click
        QVERIFY(logContent.contains(QStringLiteral("enter_new_mode")));

        // Privacy assertion: MUST NOT leak raw command or argument contents!
        QVERIFY(!logContent.contains(QStringLiteral("super_secret_script.sh")));
        QVERIFY(!logContent.contains(QStringLiteral("XYZ123")));

        // Verify redacted length representation
        QVERIFY(logContent.contains(QStringLiteral("[exec: len=")));

        // Reset log path
        DiagnosticsLogger::instance().setLogPath(QString());
    }
};

QTEST_MAIN(TestCommandManager)
#include "test_command_manager.moc"
