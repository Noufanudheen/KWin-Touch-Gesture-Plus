/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../kwin_plugin/zone_manager.h"
#include "../kwin_plugin/action_registry.h"

#include <QCoreApplication>
#include <QTest>
#include <QTemporaryFile>

using namespace KWin;

class TestZoneManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaultZones() {
        ZoneManager zm;
        const auto &zones = zm.zones();
        QCOMPARE(zones.size(), 6);

        const auto *top = zm.findZoneById(QStringLiteral("top_fullscreen_expose"));
        QVERIFY(top != nullptr);
        QCOMPARE(top->edge, ElectricTop);
        QCOMPARE(top->startFraction, 0.0);
        QCOMPARE(top->endFraction, 1.0);
        QCOMPARE(top->action, QStringLiteral("fullscreen_or_expose"));

        const auto *botLeft = zm.findZoneById(QStringLiteral("bottom_launcher"));
        QVERIFY(botLeft != nullptr);
        QCOMPARE(botLeft->edge, ElectricBottom);
        QCOMPARE(botLeft->startFraction, 0.0);
        QCOMPARE(botLeft->endFraction, 0.333);

        const auto *botMid = zm.findZoneById(QStringLiteral("bottom_dock"));
        QVERIFY(botMid != nullptr);
        QCOMPARE(botMid->startFraction, 0.333);
        QCOMPARE(botMid->endFraction, 0.666);

        const auto *botRight = zm.findZoneById(QStringLiteral("bottom_systemtray"));
        QVERIFY(botRight != nullptr);
        QCOMPARE(botRight->startFraction, 0.666);
        QCOMPARE(botRight->endFraction, 1.0);
    }

    void testZoneValidation() {
        ZoneManager zm;
        GestureZone badZone;
        badZone.id = QStringLiteral(""); // Empty ID
        QString err;
        QVERIFY(!zm.validateZone(badZone, err));

        badZone.id = QStringLiteral("test_bad");
        badZone.startFraction = 0.8;
        badZone.endFraction = 0.2; // inverted
        QVERIFY(!zm.validateZone(badZone, err));

        badZone.startFraction = -0.1;
        badZone.endFraction = 0.5;
        QVERIFY(!zm.validateZone(badZone, err));

        badZone.startFraction = 0.1;
        badZone.endFraction = 1.2; // > 1.0
        QVERIFY(!zm.validateZone(badZone, err));

        badZone.startFraction = 0.0;
        badZone.endFraction = 0.5;
        badZone.action = QStringLiteral("launcher");
        QVERIFY(zm.validateZone(badZone, err));
    }

    void testZoneMatching() {
        ZoneManager zm;
        qreal outW = 1920.0;
        qreal outH = 1080.0;

        // Bottom left third
        const auto *m1 = zm.matchZone(ElectricBottom, 0.15, 0.15 * outW, outW, false);
        QVERIFY(m1 != nullptr);
        QCOMPARE(m1->id, QStringLiteral("bottom_launcher"));

        // Bottom center third
        const auto *m2 = zm.matchZone(ElectricBottom, 0.50, 0.50 * outW, outW, false);
        QVERIFY(m2 != nullptr);
        QCOMPARE(m2->id, QStringLiteral("bottom_dock"));

        // Bottom right third
        const auto *m3 = zm.matchZone(ElectricBottom, 0.85, 0.85 * outW, outW, false);
        QVERIFY(m3 != nullptr);
        QCOMPARE(m3->id, QStringLiteral("bottom_systemtray"));

        // Left edge
        const auto *m4 = zm.matchZone(ElectricLeft, 0.50, 0.50 * outH, outH, false);
        QVERIFY(m4 != nullptr);
        QCOMPARE(m4->id, QStringLiteral("left_activities"));

        // Right edge
        const auto *m5 = zm.matchZone(ElectricRight, 0.50, 0.50 * outH, outH, false);
        QVERIFY(m5 != nullptr);
        QCOMPARE(m5->id, QStringLiteral("right_notifications"));
    }

    void testSafeZoneMargins() {
        ZoneManager zm;
        qreal outW = 1920.0;

        // Top edge: safeMarginStart = 100, safeMarginEnd = 150
        // Near top-left corner (< 100 px)
        const auto *mLeftSafe = zm.matchZone(ElectricTop, 0.03, 50.0, outW, false);
        QVERIFY(mLeftSafe == nullptr);

        // Near top-right corner (> 1920 - 150 = 1770 px)
        const auto *mRightSafe = zm.matchZone(ElectricTop, 0.98, 1850.0, outW, false);
        QVERIFY(mRightSafe == nullptr);

        // Center of top edge (safe from corners)
        const auto *mCenter = zm.matchZone(ElectricTop, 0.50, 960.0, outW, false);
        QVERIFY(mCenter != nullptr);
        QCOMPARE(mCenter->id, QStringLiteral("top_fullscreen_expose"));
    }

    void testOSKPolicy() {
        ZoneManager zm;
        qreal outW = 1920.0;

        // Default bottom zones have oskPolicy = Disabled
        const auto *mWithoutOsk = zm.matchZone(ElectricBottom, 0.50, 960.0, outW, false);
        QVERIFY(mWithoutOsk != nullptr);

        const auto *mWithOsk = zm.matchZone(ElectricBottom, 0.50, 960.0, outW, true);
        QVERIFY(mWithOsk == nullptr); // Should be suppressed when OSK is visible

        // Add a SplitGapOnly zone
        GestureZone gapZone;
        gapZone.id = QStringLiteral("gap_dismiss");
        gapZone.name = QStringLiteral("Split Gap Dismiss");
        gapZone.edge = ElectricBottom;
        gapZone.startFraction = 0.0;
        gapZone.endFraction = 1.0;
        gapZone.action = QStringLiteral("virtual_keyboard");
        gapZone.oskPolicy = QStringLiteral("SplitGapOnly");
        gapZone.priority = 10;
        QString err;
        QVERIFY(zm.addZone(gapZone, err));

        // When OSK is active:
        // Position at 0.50 (between default split gap 0.44 - 0.56) should match
        const auto *mInGap = zm.matchZone(ElectricBottom, 0.50, 960.0, outW, true, 0.44, 0.56);
        QVERIFY(mInGap != nullptr);
        QCOMPARE(mInGap->id, QStringLiteral("gap_dismiss"));

        // Position outside gap (0.20) should NOT match
        const auto *mOutsideGap = zm.matchZone(ElectricBottom, 0.20, 0.20 * outW, outW, true, 0.44, 0.56);
        QVERIFY(mOutsideGap == nullptr);
    }

    void testPriorityResolution() {
        ZoneManager zm;
        qreal outW = 1920.0;

        // Add high priority corner zone on bottom-left (0.0 - 0.10)
        GestureZone cornerZone;
        cornerZone.id = QStringLiteral("corner_quick_action");
        cornerZone.name = QStringLiteral("Corner Quick Action");
        cornerZone.edge = ElectricBottom;
        cornerZone.startFraction = 0.0;
        cornerZone.endFraction = 0.10;
        cornerZone.action = QStringLiteral("show_desktop");
        cornerZone.priority = 100; // Higher than default 0
        QString err;
        QVERIFY(zm.addZone(cornerZone, err));

        // Inside corner zone (0.05) -> cornerZone wins
        const auto *mCorner = zm.matchZone(ElectricBottom, 0.05, 0.05 * outW, outW, false);
        QVERIFY(mCorner != nullptr);
        QCOMPARE(mCorner->id, QStringLiteral("corner_quick_action"));

        // Outside corner zone (0.20) -> default bottom_launcher wins
        const auto *mLauncher = zm.matchZone(ElectricBottom, 0.20, 0.20 * outW, outW, false);
        QVERIFY(mLauncher != nullptr);
        QCOMPARE(mLauncher->id, QStringLiteral("bottom_launcher"));
    }

    void testActionParsing() {
        ActionRegistry ar;
        ActionDefinition def;
        QString err;

        // Global shortcut
        QVERIFY(ar.parseActionString(QStringLiteral("shortcut:plasmashell:activate widget 111"), def, err));
        QCOMPARE(def.type, ActionType::GlobalShortcut);
        QCOMPARE(def.shortcutComponent, QStringLiteral("plasmashell"));
        QCOMPARE(def.shortcutAction, QStringLiteral("activate widget 111"));

        // DBus call
        QVERIFY(ar.parseActionString(QStringLiteral("dbus:org.kde.plasmashell:/PlasmaShell:org.kde.PlasmaShell:activateLauncherMenu"), def, err));
        QCOMPARE(def.type, ActionType::DBusCall);
        QCOMPARE(def.dbusService, QStringLiteral("org.kde.plasmashell"));
        QCOMPARE(def.dbusPath, QStringLiteral("/PlasmaShell"));
        QCOMPARE(def.dbusInterface, QStringLiteral("org.kde.PlasmaShell"));
        QCOMPARE(def.dbusMethod, QStringLiteral("activateLauncherMenu"));

        // External command
        QVERIFY(ar.parseActionString(QStringLiteral("exec:/usr/bin/brightnessctl set +10%"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/brightnessctl"));
        QCOMPARE(def.arguments, QStringList({QStringLiteral("set"), QStringLiteral("+10%")}));

        // Builtin
        QVERIFY(ar.parseActionString(QStringLiteral("fullscreen_or_expose"), def, err));
        QCOMPARE(def.type, ActionType::Builtin);
        QCOMPARE(def.builtinName, QStringLiteral("fullscreen_or_expose"));

        // Custom commands are empty until explicitly configured by the user.
        QVERIFY(!ar.parseActionString(QStringLiteral("custom_1"), def, err));
        QMap<QString, QString> initialPack;
        initialPack.insert(QStringLiteral("custom_1"), QStringLiteral("exec:/usr/bin/konsole"));
        initialPack.insert(QStringLiteral("custom_2"), QStringLiteral("exec:/usr/bin/dolphin"));
        ar.setCustomCommandPack(initialPack);

        QVERIFY(ar.parseActionString(QStringLiteral("custom_1"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/konsole"));

        QVERIFY(ar.parseActionString(QStringLiteral("custom_2"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/dolphin"));

        // Direct command fallback
        QVERIFY(ar.parseActionString(QStringLiteral("/usr/bin/htop"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/htop"));

        // Dynamic pack updating
        QMap<QString, QString> newPack;
        newPack[QStringLiteral("custom_1")] = QStringLiteral("exec:/usr/bin/firefox");
        ar.setCustomCommandPack(newPack);
        QVERIFY(ar.parseActionString(QStringLiteral("custom_1"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/firefox"));

        // "Custom Command" human label
        QVERIFY(ar.parseActionString(QStringLiteral("Custom Command"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/firefox"));

        // Custom command with Title option
        ar.setCustomCommandTitle(QStringLiteral("Web Browser"));
        QVERIFY(ar.parseActionString(QStringLiteral("Web Browser"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/firefox"));

        // If Title is blank, command itself is used
        ar.setCustomCommandTitle(QString());
        QVERIFY(ar.parseActionString(QStringLiteral("Custom Command"), def, err));
        QCOMPARE(def.type, ActionType::ExternalCommand);
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/firefox"));

        // The command registry supports a slot matching a seventh/custom zone,
        // not merely the original five fixed slots.
        QMap<QString, QString> expandedPack;
        expandedPack.insert(QStringLiteral("custom_7"), QStringLiteral("exec:/usr/bin/htop"));
        ar.setCustomCommandPack(expandedPack);
        QVERIFY(ar.parseActionString(QStringLiteral("custom_7"), def, err));
        QCOMPARE(def.executable, QStringLiteral("/usr/bin/htop"));
        QVERIFY(!ar.parseActionString(QStringLiteral("custom_1"), def, err)); // replacement removes stale slots
    }

    void testJsonRoundTrip() {
        ZoneManager zm;
        QTemporaryFile tempFile;
        QVERIFY(tempFile.open());
        QString tempPath = tempFile.fileName();
        tempFile.close();

        QString err;
        QVERIFY(zm.saveToJsonFile(tempPath, err));

        ZoneManager zmLoaded;
        zmLoaded.clearZones();
        QCOMPARE(zmLoaded.zones().size(), 0);

        QVERIFY(zmLoaded.loadFromJsonFile(tempPath, err));
        QCOMPARE(zmLoaded.zones().size(), 6);

        const auto *top = zmLoaded.findZoneById(QStringLiteral("top_fullscreen_expose"));
        QVERIFY(top != nullptr);
        QCOMPARE(top->edge, ElectricTop);
        QCOMPARE(top->action, QStringLiteral("fullscreen_or_expose"));
    }
};

QTEST_MAIN(TestZoneManager)
#include "test_zone_manager.moc"
