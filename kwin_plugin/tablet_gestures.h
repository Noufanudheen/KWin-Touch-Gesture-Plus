/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kwin/plugin.h>
#include <kwin/screenedge.h>

#include <QObject>
#include <QAction>
#include <QTimer>
#include <QSocketNotifier>
#include <memory>
#include <vector>

namespace KWin
{

class ActionRegistry;
class ZoneManager;
class LogicalOutput;

class TabletGesturesPlugin : public Plugin
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.TabletGestures")
public:
    TabletGesturesPlugin();
    ~TabletGesturesPlugin() override;

public Q_SLOTS:
    // Core tablet mode and keyboard D-Bus controls
    Q_SCRIPTABLE bool isTabletMode() const;
    Q_SCRIPTABLE void setTabletMode(bool tablet);
    Q_SCRIPTABLE void toggleTabletMode();
    Q_SCRIPTABLE void toggleVirtualKeyboard();
    Q_SCRIPTABLE void showVirtualKeyboard();
    Q_SCRIPTABLE void hideVirtualKeyboard();
    Q_SCRIPTABLE void reloadConfig();

    // Extensible Zone Management D-Bus APIs
    Q_SCRIPTABLE QString listZones() const;
    Q_SCRIPTABLE QString getZone(const QString &id) const;
    Q_SCRIPTABLE bool addOrUpdateZone(const QString &zoneJson, QString &errorOut);
    Q_SCRIPTABLE bool removeZone(const QString &id);
    Q_SCRIPTABLE QStringList validateZones() const;
    Q_SCRIPTABLE bool executeAction(const QString &actionStr);

    // Custom Commands Pack D-Bus APIs
    Q_SCRIPTABLE QString getCustomCommand(int index) const;
    Q_SCRIPTABLE bool setCustomCommand(int index, const QString &command);

private Q_SLOTS:
    void onTabletModeChanged(bool tabletMode);
    void handleSensorPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidated);
    void handleSwitchEvent(int fd);
    void checkHingeSensor();

private:
    void loadConfig();
    void saveProfiles();
    void syncCurrentSystemStateToActiveProfile();

    void registerNativeEdges();
    void unregisterNativeEdges();

    bool isDockedKeyboardAtBottom(LogicalOutput *output) const;
    bool evaluateTabletMode();
    void ensureInputMethod();

    void initHingeMonitoring();
    void initSensorMonitoring();
    void initSwitchMonitoring();
    bool readSwitchState(int fd);
    QString readSensorOrientation();

    void applyButtonSizeToSystem(const QString &size);
    QString readCurrentButtonSizeFromSystem();
    QString normalizeButtonSize(const QString &val);
    QString buttonSizeToLabel(const QString &val);
    QString normalizeDisplayScale(const QString &val);
    QString scaleToLabel(const QString &val);

    struct SwitchDev {
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
    };

    std::unique_ptr<ActionRegistry> m_actionRegistry;
    std::unique_ptr<ZoneManager> m_zoneManager;

    std::vector<SwitchDev> m_switches;
    QTimer *m_hingeTimer = nullptr;
    QString m_hingePath;
    bool m_isTablet = false;
    bool m_applyingProfile = false;
    bool m_gesturesEnabled = true;

    QAction *m_nativeTopAction = nullptr;
    QAction *m_nativeBottomAction = nullptr;
    QAction *m_nativeLeftAction = nullptr;
    QAction *m_nativeRightAction = nullptr;

    qreal m_lastTopStartX = 0.0;
    qreal m_lastBottomStartX = 0.0;
    qreal m_lastLeftStartY = 0.0;
    qreal m_lastRightStartY = 0.0;

    qreal m_pullThreshold = 45.0;
    bool m_hideKeyboardOnGesture = true;
    qreal m_splitGapMinX = 0.44;
    qreal m_splitGapMaxX = 0.56;

    int m_hingeAngleThreshold = 185;
    double m_hingeThresholdW = -0.0436;
    int m_hingePollInterval = 350;
    bool m_enableButtonScaling = true;
    QString m_tabletButtonSize;
    QString m_laptopButtonSize;
    bool m_enableDisplayScaling = true;
    QString m_tabletScale;
    QString m_laptopScale;
};

class TabletGesturesFactory : public PluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginFactory_iid FILE "metadata.json")
    Q_INTERFACES(KWin::PluginFactory)
public:
    TabletGesturesFactory() = default;
    std::unique_ptr<Plugin> create() const override {
        return std::make_unique<TabletGesturesPlugin>();
    }
};

} // namespace KWin
