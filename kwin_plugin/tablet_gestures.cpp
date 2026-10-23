/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tablet_gestures.h"
#include "action_registry.h"
#include "zone_manager.h"
#include "diagnostics_logger.h"
#include "custom_command_store.h"
#include <dlfcn.h>

#include <kwin/input.h>
#include <kwin/input_event.h>
#include <kwin/workspace.h>
#include <kwin/window.h>
#include <kwin/screenedge.h>
#include <kwin/main.h>
#include <kwin/tabletmodemanager.h>
#include <kwin/inputmethod.h>
#include <kwin/inputpanelv1window.h>
#include <kwin/core/output.h>
#include <kwin/effect/effecthandler.h>
#include <kwin/touch_input.h>

#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QFile>
#include <KSharedConfig>
#include <KConfigGroup>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <dirent.h>

namespace KWin
{

TabletGesturesPlugin::TabletGesturesPlugin()
{
    Dl_info dlinfo;
    QString loadedModulePath = QStringLiteral("unknown");
    if (dladdr(reinterpret_cast<const void*>(&TabletGesturesPlugin::staticMetaObject), &dlinfo) && dlinfo.dli_fname) {
        loadedModulePath = QString::fromUtf8(dlinfo.dli_fname);
    }

    QJsonObject initDetails;
    initDetails.insert(QStringLiteral("build_version"), QStringLiteral("v1.3.2-clean-workflow"));
    initDetails.insert(QStringLiteral("build_fingerprint"), QString::fromUtf8(__DATE__ " " __TIME__));
    initDetails.insert(QStringLiteral("module_path"), loadedModulePath);
    DiagnosticsLogger::instance().log(QStringLiteral("runtime"), QStringLiteral("INFO"), QStringLiteral("plugin_init"), initDetails);

    m_actionRegistry = std::make_unique<ActionRegistry>(this);
    m_zoneManager = std::make_unique<ZoneManager>();

    loadConfig();

    if (Workspace::self()) {
        connect(Workspace::self(), &Workspace::configChanged, this, [this]() {
            loadConfig();
            syncCurrentSystemStateToActiveProfile();
            registerNativeEdges();
        });
        connect(Workspace::self(), &Workspace::outputsChanged, this, [this]() {
            syncCurrentSystemStateToActiveProfile();
            registerNativeEdges();
        });
        connect(Workspace::self(), &Workspace::geometryChanged, this, [this]() {
            syncCurrentSystemStateToActiveProfile();
            registerNativeEdges();
        });
    }

    registerNativeEdges();
    ensureInputMethod();

    initSwitchMonitoring();
    initSensorMonitoring();
    initHingeMonitoring();

    bool initialMode = evaluateTabletMode();

    if (kwinApp() && kwinApp()->tabletModeManager()) {
        connect(kwinApp()->tabletModeManager(), &TabletModeManager::tabletModeChanged, this, &TabletGesturesPlugin::onTabletModeChanged);
        kwinApp()->tabletModeManager()->setIsTablet(initialMode);
        onTabletModeChanged(kwinApp()->tabletModeManager()->effectiveTabletMode());
    } else {
        onTabletModeChanged(initialMode);
    }

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/TabletGestures"), this, QDBusConnection::ExportScriptableSlots);
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.TabletGestures"));
}

TabletGesturesPlugin::~TabletGesturesPlugin()
{
    unregisterNativeEdges();

    if (m_hingeTimer) {
        m_hingeTimer->stop();
        delete m_hingeTimer;
        m_hingeTimer = nullptr;
    }
    for (auto &dev : m_switches) {
        if (dev.notifier) {
            delete dev.notifier;
            dev.notifier = nullptr;
        }
        if (dev.fd >= 0) {
            close(dev.fd);
            dev.fd = -1;
        }
    }
    m_switches.clear();
}

bool TabletGesturesPlugin::isTabletMode() const
{
    if (kwinApp() && kwinApp()->tabletModeManager()) {
        return kwinApp()->tabletModeManager()->effectiveTabletMode();
    }
    return m_isTablet;
}

void TabletGesturesPlugin::setTabletMode(bool tablet)
{
    if (kwinApp() && kwinApp()->tabletModeManager()) {
        kwinApp()->tabletModeManager()->setIsTablet(tablet);
    } else {
        onTabletModeChanged(tablet);
    }
}

void TabletGesturesPlugin::toggleTabletMode()
{
    setTabletMode(!isTabletMode());
}

void TabletGesturesPlugin::toggleVirtualKeyboard()
{
    if (m_actionRegistry) {
        m_actionRegistry->runAction(QStringLiteral("virtual_keyboard"), false);
    }
}

void TabletGesturesPlugin::showVirtualKeyboard()
{
    if (kwinApp() && kwinApp()->inputMethod()) {
        kwinApp()->inputMethod()->forceActivate();
    }
}

void TabletGesturesPlugin::hideVirtualKeyboard()
{
    if (kwinApp() && kwinApp()->inputMethod()) {
        kwinApp()->inputMethod()->hide();
    }
}

void TabletGesturesPlugin::reloadConfig()
{
    DiagnosticsLogger::instance().log(QStringLiteral("runtime"), QStringLiteral("INFO"), QStringLiteral("runtime_reload_seen"));
    loadConfig();
    ensureInputMethod();
    registerNativeEdges();
    onTabletModeChanged(isTabletMode());
}

QString TabletGesturesPlugin::listZones() const
{
    if (!m_zoneManager) {
        return QStringLiteral("[]");
    }
    QJsonArray arr;
    for (const auto &z : m_zoneManager->zones()) {
        arr.append(z.toJson());
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString TabletGesturesPlugin::getZone(const QString &id) const
{
    if (!m_zoneManager) {
        return QStringLiteral("{}");
    }
    const auto *z = m_zoneManager->findZoneById(id);
    if (!z) {
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(QJsonDocument(z->toJson()).toJson(QJsonDocument::Compact));
}

bool TabletGesturesPlugin::addOrUpdateZone(const QString &zoneJson, QString &errorOut)
{
    if (!m_zoneManager) {
        errorOut = QStringLiteral("Zone manager unavailable");
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(zoneJson.toUtf8());
    if (!doc.isObject()) {
        errorOut = QStringLiteral("Invalid JSON object");
        return false;
    }
    auto optZ = GestureZone::fromJson(doc.object(), errorOut);
    if (!optZ) {
        return false;
    }

    if (m_zoneManager->findZoneById(optZ->id) != nullptr) {
        if (!m_zoneManager->updateZone(optZ->id, *optZ, errorOut)) {
            return false;
        }
    } else {
        if (!m_zoneManager->addZone(*optZ, errorOut)) {
            return false;
        }
    }

    auto cfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    auto group = cfg->group(QStringLiteral("Effect-tablet_gestures"));
    m_zoneManager->saveToConfigGroup(group);
    cfg->sync();

    registerNativeEdges();
    return true;
}

bool TabletGesturesPlugin::removeZone(const QString &id)
{
    if (!m_zoneManager) {
        return false;
    }
    bool removed = m_zoneManager->removeZone(id);
    if (removed) {
        auto cfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
        auto group = cfg->group(QStringLiteral("Effect-tablet_gestures"));
        m_zoneManager->saveToConfigGroup(group);
        cfg->sync();
        registerNativeEdges();
    }
    return removed;
}

QStringList TabletGesturesPlugin::validateZones() const
{
    if (!m_zoneManager) {
        return {QStringLiteral("Zone manager not initialized")};
    }
    return m_zoneManager->validateAll();
}

bool TabletGesturesPlugin::executeAction(const QString &actionStr)
{
    if (!m_actionRegistry) {
        return false;
    }
    return m_actionRegistry->runAction(actionStr, m_hideKeyboardOnGesture);
}

QString TabletGesturesPlugin::getCustomCommand(int index) const
{
    if (!m_actionRegistry) {
        return QString();
    }
    return m_actionRegistry->resolveCustomCommand(QStringLiteral("custom_%1").arg(index));
}

bool TabletGesturesPlugin::setCustomCommand(int index, const QString &command)
{
    const int hotspotLimit = m_zoneManager ? static_cast<int>(m_zoneManager->zones().size()) : 0;
    if (index < 1 || index > hotspotLimit) {
        return false;
    }

    auto cfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    auto group = cfg->group(QStringLiteral("Effect-tablet_gestures"));

    const QString storePath = group.readEntry("CustomCommandsStorePath", QString());
    CustomCommandStore store(storePath);
    store.initialize();
    store.migrateFromConfigGroup(group);

    if (command.trimmed().isEmpty()) {
        store.deleteCommand(index);
    } else {
        CustomCommandStore::CommandEntry entry;
        entry.slot = index;
        entry.title = m_actionRegistry ? m_actionRegistry->customCommandTitle(index) : QString();
        entry.command = command.trimmed();
        store.saveCommand(entry);
    }

    auto storeEntries = store.loadCommands();
    QJsonObject commandRoot;
    commandRoot.insert(QStringLiteral("version"), 1);
    QJsonArray commandArray;
    QMap<QString, QString> pack;
    QMap<int, QString> titles;
    for (auto it = storeEntries.cbegin(); it != storeEntries.cend(); ++it) {
        QJsonObject obj;
        obj.insert(QStringLiteral("slot"), it.key());
        obj.insert(QStringLiteral("title"), it.value().title);
        obj.insert(QStringLiteral("command"), it.value().command);
        commandArray.append(obj);

        const QString key = QStringLiteral("custom_%1").arg(it.key());
        pack.insert(key, it.value().command);
        pack.insert(QStringLiteral("custom%1").arg(it.key()), it.value().command);
        if (!it.value().title.isEmpty()) {
            titles.insert(it.key(), it.value().title);
        }
    }
    commandRoot.insert(QStringLiteral("commands"), commandArray);
    group.writeEntry("CustomCommandsJson", QString::fromUtf8(QJsonDocument(commandRoot).toJson(QJsonDocument::Compact)));
    if (index <= 6) {
        group.writeEntry(QStringLiteral("CustomCommand%1").arg(index), command.trimmed());
    }
    cfg->sync();

    if (m_actionRegistry) {
        pack.insert(QStringLiteral("custom"), pack.value(QStringLiteral("custom_1")));
        m_actionRegistry->setCustomCommandPack(pack);
        m_actionRegistry->setCustomCommandTitles(titles);
    }

    QJsonObject details;
    details.insert(QStringLiteral("slot"), index);
    details.insert(QStringLiteral("command"), DiagnosticsLogger::redactCommand(command));
    DiagnosticsLogger::instance().log(QStringLiteral("runtime"), QStringLiteral("INFO"), QStringLiteral("set_custom_command"), details);

    return true;
}

void TabletGesturesPlugin::onTabletModeChanged(bool tabletMode)
{
    if (m_isTablet == tabletMode && m_applyingProfile) {
        return;
    }
    m_isTablet = tabletMode;
    m_applyingProfile = true;

    if (m_enableButtonScaling) {
        QString targetBtn = tabletMode ? m_tabletButtonSize : m_laptopButtonSize;
        applyButtonSizeToSystem(targetBtn);
    }

    if (m_enableDisplayScaling) {
        QString targetScale = normalizeDisplayScale(tabletMode ? m_tabletScale : m_laptopScale);
        QString outputName = QStringLiteral("eDP-1");
        if (Workspace::self() && Workspace::self()->activeOutput()) {
            outputName = Workspace::self()->activeOutput()->name();
        }
        QProcess::startDetached(QStringLiteral("kscreen-doctor"), {QStringLiteral("output.%1.scale.%2").arg(outputName, targetScale)});
    }

    if (kwinApp() && kwinApp()->inputMethod()) {
        kwinApp()->inputMethod()->setMode(tabletMode ? InputMethod::VirtualKeyboardVisibility::AnyInput : InputMethod::VirtualKeyboardVisibility::NonMouseInput);
    }

    QTimer::singleShot(600, this, [this]() {
        m_applyingProfile = false;
    });
}

void TabletGesturesPlugin::handleSensorPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != QStringLiteral("net.hadess.SensorProxy")) {
        return;
    }
    if (changedProps.contains(QStringLiteral("AccelerometerOrientation"))) {
        bool tablet = evaluateTabletMode();
        if (kwinApp() && kwinApp()->tabletModeManager()) {
            if (kwinApp()->tabletModeManager()->isTablet() != tablet) {
                kwinApp()->tabletModeManager()->setIsTablet(tablet);
            }
        } else if (tablet != m_isTablet) {
            onTabletModeChanged(tablet);
        }
    }
}

void TabletGesturesPlugin::handleSwitchEvent(int fd)
{
    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) > 0) {
        if (ev.type == EV_SW && ev.code == SW_TABLET_MODE) {
            bool tablet = evaluateTabletMode();
            if (kwinApp() && kwinApp()->tabletModeManager()) {
                if (kwinApp()->tabletModeManager()->isTablet() != tablet) {
                    kwinApp()->tabletModeManager()->setIsTablet(tablet);
                }
            } else if (tablet != m_isTablet) {
                onTabletModeChanged(tablet);
            }
        }
    }
}

void TabletGesturesPlugin::checkHingeSensor()
{
    bool tablet = evaluateTabletMode();
    if (kwinApp() && kwinApp()->tabletModeManager()) {
        if (kwinApp()->tabletModeManager()->isTablet() != tablet) {
            kwinApp()->tabletModeManager()->setIsTablet(tablet);
        }
    } else if (tablet != m_isTablet) {
        onTabletModeChanged(tablet);
    }
}

bool TabletGesturesPlugin::isDockedKeyboardAtBottom(LogicalOutput *output) const
{
    if (!kwinApp() || !kwinApp()->inputMethod()) {
        return false;
    }
    if (!kwinApp()->inputMethod()->isVisible()) {
        return false;
    }
    InputPanelV1Window *panel = kwinApp()->inputMethod()->panel();
    if (!panel) {
        return true;
    }
    if (panel->mode() != InputPanelV1Window::Mode::VirtualKeyboard) {
        return false;
    }
    RectF panelGeo = panel->frameGeometry();
    Rect outGeo = output ? output->geometry() : Workspace::self()->geometry();
    if (panelGeo.bottom() >= outGeo.bottom() - 15) {
        return true;
    }
    return false;
}

void TabletGesturesPlugin::registerNativeEdges()
{
    unregisterNativeEdges();

    if (!m_gesturesEnabled || !Workspace::self() || !Workspace::self()->screenEdges() || !m_zoneManager) {
        return;
    }

    // Check which edges have at least one enabled zone
    bool hasTop = false, hasBottom = false, hasLeft = false, hasRight = false;
    for (const auto &z : m_zoneManager->zones()) {
        if (!z.enabled) continue;
        if (z.edge == ElectricTop) hasTop = true;
        else if (z.edge == ElectricBottom) hasBottom = true;
        else if (z.edge == ElectricLeft) hasLeft = true;
        else if (z.edge == ElectricRight) hasRight = true;
    }

    // TOP EDGE
    if (hasTop) {
        m_nativeTopAction = new QAction(this);
        connect(m_nativeTopAction, &QAction::triggered, this, [this]() {
            LogicalOutput *output = Workspace::self()->activeOutput();
            Rect outGeo = output ? output->geometry() : Workspace::self()->geometry();
            qreal outW = outGeo.width() > 0 ? outGeo.width() : 1920.0;
            qreal localX = m_lastTopStartX - outGeo.x();
            qreal normX = qBound(0.0, localX / outW, 1.0);
            bool oskActive = isDockedKeyboardAtBottom(output);

            const auto *zone = m_zoneManager->matchZone(ElectricTop, normX, localX, outW, oskActive);
            if (zone && m_actionRegistry) {
                m_actionRegistry->runAction(zone->action, m_hideKeyboardOnGesture);
            }
        });
        Workspace::self()->screenEdges()->reserveTouch(ElectricTop, m_nativeTopAction, [this](ElectricBorder b, const QPointF &delta, LogicalOutput *out) {
            if (input() && input()->touch()) {
                m_lastTopStartX = input()->touch()->position().x() - delta.x();
            }
            Rect outGeo = out ? out->geometry() : (Workspace::self() ? Workspace::self()->geometry() : Rect());
            qreal factor = qBound(0.0, delta.y() / m_pullThreshold, 1.0);
            Q_EMIT Workspace::self()->screenEdges()->approaching(b, factor, outGeo);
        });
    }

    // LEFT EDGE
    if (hasLeft) {
        m_nativeLeftAction = new QAction(this);
        connect(m_nativeLeftAction, &QAction::triggered, this, [this]() {
            LogicalOutput *output = Workspace::self()->activeOutput();
            Rect outGeo = output ? output->geometry() : Workspace::self()->geometry();
            qreal outH = outGeo.height() > 0 ? outGeo.height() : 1080.0;
            qreal localY = m_lastLeftStartY - outGeo.y();
            qreal normY = qBound(0.0, localY / outH, 1.0);
            bool oskActive = isDockedKeyboardAtBottom(output);

            const auto *zone = m_zoneManager->matchZone(ElectricLeft, normY, localY, outH, oskActive);
            if (zone && m_actionRegistry) {
                m_actionRegistry->runAction(zone->action, m_hideKeyboardOnGesture);
            }
        });
        Workspace::self()->screenEdges()->reserveTouch(ElectricLeft, m_nativeLeftAction, [this](ElectricBorder b, const QPointF &delta, LogicalOutput *out) {
            if (input() && input()->touch()) {
                m_lastLeftStartY = input()->touch()->position().y() - delta.y();
            }
            Rect outGeo = out ? out->geometry() : (Workspace::self() ? Workspace::self()->geometry() : Rect());
            qreal factor = qBound(0.0, delta.x() / m_pullThreshold, 1.0);
            Q_EMIT Workspace::self()->screenEdges()->approaching(b, factor, outGeo);
        });
    }

    // RIGHT EDGE
    if (hasRight) {
        m_nativeRightAction = new QAction(this);
        connect(m_nativeRightAction, &QAction::triggered, this, [this]() {
            LogicalOutput *output = Workspace::self()->activeOutput();
            Rect outGeo = output ? output->geometry() : Workspace::self()->geometry();
            qreal outH = outGeo.height() > 0 ? outGeo.height() : 1080.0;
            qreal localY = m_lastRightStartY - outGeo.y();
            qreal normY = qBound(0.0, localY / outH, 1.0);
            bool oskActive = isDockedKeyboardAtBottom(output);

            const auto *zone = m_zoneManager->matchZone(ElectricRight, normY, localY, outH, oskActive);
            if (zone && m_actionRegistry) {
                m_actionRegistry->runAction(zone->action, m_hideKeyboardOnGesture);
            }
        });
        Workspace::self()->screenEdges()->reserveTouch(ElectricRight, m_nativeRightAction, [this](ElectricBorder b, const QPointF &delta, LogicalOutput *out) {
            if (input() && input()->touch()) {
                m_lastRightStartY = input()->touch()->position().y() - delta.y();
            }
            Rect outGeo = out ? out->geometry() : (Workspace::self() ? Workspace::self()->geometry() : Rect());
            qreal factor = qBound(0.0, -delta.x() / m_pullThreshold, 1.0);
            Q_EMIT Workspace::self()->screenEdges()->approaching(b, factor, outGeo);
        });
    }

    // BOTTOM EDGE
    if (hasBottom) {
        m_nativeBottomAction = new QAction(this);
        connect(m_nativeBottomAction, &QAction::triggered, this, [this]() {
            LogicalOutput *output = Workspace::self()->activeOutput();
            bool oskActive = isDockedKeyboardAtBottom(output);
            Rect outGeo = output ? output->geometry() : Workspace::self()->geometry();
            qreal outW = outGeo.width() > 0 ? outGeo.width() : 1920.0;
            qreal localX = m_lastBottomStartX - outGeo.x();
            qreal normX = qBound(0.0, localX / outW, 1.0);

            const auto *zone = m_zoneManager->matchZone(ElectricBottom, normX, localX, outW, oskActive, m_splitGapMinX, m_splitGapMaxX);
            if (zone && m_actionRegistry) {
                m_actionRegistry->runAction(zone->action, m_hideKeyboardOnGesture);
            }
        });
        Workspace::self()->screenEdges()->reserveTouch(ElectricBottom, m_nativeBottomAction, [this](ElectricBorder b, const QPointF &delta, LogicalOutput *out) {
            if (input() && input()->touch()) {
                m_lastBottomStartX = input()->touch()->position().x() - delta.x();
            }
            Rect outGeo = out ? out->geometry() : (Workspace::self() ? Workspace::self()->geometry() : Rect());
            qreal factor = qBound(0.0, -delta.y() / m_pullThreshold, 1.0);
            Q_EMIT Workspace::self()->screenEdges()->approaching(b, factor, outGeo);
        });
    }
}

void TabletGesturesPlugin::unregisterNativeEdges()
{
    if (!Workspace::self() || !Workspace::self()->screenEdges()) {
        return;
    }
    if (m_nativeTopAction) {
        Workspace::self()->screenEdges()->unreserveTouch(ElectricTop, m_nativeTopAction);
        delete m_nativeTopAction;
        m_nativeTopAction = nullptr;
    }
    if (m_nativeLeftAction) {
        Workspace::self()->screenEdges()->unreserveTouch(ElectricLeft, m_nativeLeftAction);
        delete m_nativeLeftAction;
        m_nativeLeftAction = nullptr;
    }
    if (m_nativeRightAction) {
        Workspace::self()->screenEdges()->unreserveTouch(ElectricRight, m_nativeRightAction);
        delete m_nativeRightAction;
        m_nativeRightAction = nullptr;
    }
    if (m_nativeBottomAction) {
        Workspace::self()->screenEdges()->unreserveTouch(ElectricBottom, m_nativeBottomAction);
        delete m_nativeBottomAction;
        m_nativeBottomAction = nullptr;
    }
}

QString TabletGesturesPlugin::normalizeButtonSize(const QString &val)
{
    QString s = val.trimmed();
    if (s == QStringLiteral("ButtonDefault") || s == QStringLiteral("Medium") || s == QStringLiteral("2") || s == QStringLiteral("default")) {
        return QStringLiteral("ButtonDefault");
    }
    if (s == QStringLiteral("ButtonVeryLarge") || s == QStringLiteral("Very Large") || s == QStringLiteral("4")) {
        return QStringLiteral("ButtonVeryLarge");
    }
    if (s == QStringLiteral("ButtonLarge") || s == QStringLiteral("Large") || s == QStringLiteral("3")) {
        return QStringLiteral("ButtonLarge");
    }
    if (s == QStringLiteral("ButtonSmall") || s == QStringLiteral("Small") || s == QStringLiteral("1")) {
        return QStringLiteral("ButtonSmall");
    }
    if (s == QStringLiteral("ButtonTiny") || s == QStringLiteral("Tiny")) {
        return QStringLiteral("ButtonTiny");
    }
    if (s == QStringLiteral("ButtonHuge") || s == QStringLiteral("Huge") || s == QStringLiteral("5")) {
        return QStringLiteral("ButtonHuge");
    }
    return QStringLiteral("ButtonDefault");
}

QString TabletGesturesPlugin::buttonSizeToLabel(const QString &val)
{
    QString s = normalizeButtonSize(val);
    if (s == QStringLiteral("ButtonVeryLarge")) return QStringLiteral("Very Large");
    if (s == QStringLiteral("ButtonHuge")) return QStringLiteral("Huge");
    if (s == QStringLiteral("ButtonLarge")) return QStringLiteral("Large");
    if (s == QStringLiteral("ButtonSmall")) return QStringLiteral("Small");
    if (s == QStringLiteral("ButtonTiny")) return QStringLiteral("Tiny");
    return QStringLiteral("Medium");
}

QString TabletGesturesPlugin::normalizeDisplayScale(const QString &val)
{
    QString s = val.trimmed();
    if (s.endsWith(QLatin1Char('%'))) {
        s.chop(1);
        bool ok = false;
        double pct = s.trimmed().toDouble(&ok);
        if (ok && pct > 0) {
            return QString::number(pct / 100.0, 'f', 2);
        }
    }
    if (s == QStringLiteral("100") || s == QStringLiteral("1.0") || s == QStringLiteral("1.00") || s == QStringLiteral("1")) {
        return QStringLiteral("1");
    }
    if (s == QStringLiteral("105") || s == QStringLiteral("1.05")) return QStringLiteral("1.05");
    if (s == QStringLiteral("110") || s == QStringLiteral("1.1") || s == QStringLiteral("1.10")) return QStringLiteral("1.1");
    if (s == QStringLiteral("115") || s == QStringLiteral("1.15")) return QStringLiteral("1.15");
    if (s == QStringLiteral("120") || s == QStringLiteral("1.2") || s == QStringLiteral("1.20")) return QStringLiteral("1.2");
    if (s == QStringLiteral("125") || s == QStringLiteral("1.25")) return QStringLiteral("1.25");
    if (s == QStringLiteral("130") || s == QStringLiteral("1.3") || s == QStringLiteral("1.30")) return QStringLiteral("1.3");
    if (s == QStringLiteral("150") || s == QStringLiteral("1.5") || s == QStringLiteral("1.50")) return QStringLiteral("1.5");
    return s.isEmpty() ? QStringLiteral("1") : s;
}

QString TabletGesturesPlugin::scaleToLabel(const QString &val)
{
    QString s = normalizeDisplayScale(val);
    if (s == QStringLiteral("1.05")) return QStringLiteral("105%");
    if (s == QStringLiteral("1.1") || s == QStringLiteral("1.10")) return QStringLiteral("110%");
    if (s == QStringLiteral("1.15")) return QStringLiteral("115%");
    if (s == QStringLiteral("1.2") || s == QStringLiteral("1.20")) return QStringLiteral("120%");
    if (s == QStringLiteral("1.25")) return QStringLiteral("125%");
    if (s == QStringLiteral("1.3") || s == QStringLiteral("1.30")) return QStringLiteral("130%");
    if (s == QStringLiteral("1.5") || s == QStringLiteral("1.50")) return QStringLiteral("150%");
    return QStringLiteral("100%");
}

void TabletGesturesPlugin::applyButtonSizeToSystem(const QString &size)
{
    QString normalized = normalizeButtonSize(size);
    QStringList configs = {
        QStringLiteral("breezerc"),
        QStringLiteral("klassy/klassyrc"),
        QStringLiteral("sierrabreezeenhancedrc"),
        QStringLiteral("lightlyrc"),
        QStringLiteral("oxygenrc")
    };
    for (const auto &cfgName : configs) {
        auto cfg = KSharedConfig::openConfig(cfgName);
        auto common = cfg->group(QStringLiteral("Common"));
        auto windeco = cfg->group(QStringLiteral("Windeco"));
        common.writeEntry(QStringLiteral("ButtonSize"), normalized);
        windeco.writeEntry(QStringLiteral("ButtonSize"), normalized);
        cfg->sync();
    }
    auto kwinCfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    auto decoGroup = kwinCfg->group(QStringLiteral("org.kde.kdecoration2"));
    decoGroup.writeEntry(QStringLiteral("ButtonSize"), normalized);
    kwinCfg->sync();

    Workspace::self()->reconfigure();
}

QString TabletGesturesPlugin::readCurrentButtonSizeFromSystem()
{
    auto kwinCfg = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    kwinCfg->reparseConfiguration();
    QString activeTheme = kwinCfg->group(QStringLiteral("org.kde.kdecoration2")).readEntry(QStringLiteral("theme"), QStringLiteral("Breeze"));

    QString cfgName = QStringLiteral("breezerc");
    if (activeTheme.compare(QStringLiteral("klassy"), Qt::CaseInsensitive) == 0) {
        cfgName = QStringLiteral("klassy/klassyrc");
    } else if (activeTheme.compare(QStringLiteral("sierrabreezeenhanced"), Qt::CaseInsensitive) == 0) {
        cfgName = QStringLiteral("sierrabreezeenhancedrc");
    } else if (activeTheme.compare(QStringLiteral("oxygen"), Qt::CaseInsensitive) == 0) {
        cfgName = QStringLiteral("oxygenrc");
    }

    auto cfg = KSharedConfig::openConfig(cfgName);
    cfg->reparseConfiguration();
    QString size = cfg->group(QStringLiteral("Common")).readEntry(QStringLiteral("ButtonSize"), QString());
    if (size.isEmpty()) {
        size = cfg->group(QStringLiteral("Windeco")).readEntry(QStringLiteral("ButtonSize"), QString());
    }
    if (size.isEmpty()) {
        auto bCfg = KSharedConfig::openConfig(QStringLiteral("breezerc"));
        bCfg->reparseConfiguration();
        size = bCfg->group(QStringLiteral("Common")).readEntry(QStringLiteral("ButtonSize"), QString());
    }
    return normalizeButtonSize(size);
}

void TabletGesturesPlugin::saveProfiles()
{
    QString tabBtn = buttonSizeToLabel(m_tabletButtonSize);
    QString lapBtn = buttonSizeToLabel(m_laptopButtonSize);
    QString tabScale = scaleToLabel(m_tabletScale);
    QString lapScale = scaleToLabel(m_laptopScale);

    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    auto group = config->group(QStringLiteral("Effect-tablet_gestures"));
    group.writeEntry(QStringLiteral("TabletButtonSize"), tabBtn);
    group.writeEntry(QStringLiteral("LaptopButtonSize"), lapBtn);
    group.writeEntry(QStringLiteral("TabletScale"), tabScale);
    group.writeEntry(QStringLiteral("LaptopScale"), lapScale);
    config->sync();
}

void TabletGesturesPlugin::syncCurrentSystemStateToActiveProfile()
{
    if (m_applyingProfile) {
        return;
    }

    bool changed = false;

    if (m_enableButtonScaling) {
        QString currentBtn = readCurrentButtonSizeFromSystem();
        if (m_isTablet) {
            if (m_tabletButtonSize != currentBtn) {
                m_tabletButtonSize = currentBtn;
                changed = true;
            }
        } else {
            if (m_laptopButtonSize != currentBtn) {
                m_laptopButtonSize = currentBtn;
                changed = true;
            }
        }
    }

    if (m_enableDisplayScaling && Workspace::self() && Workspace::self()->activeOutput()) {
        qreal curScale = Workspace::self()->activeOutput()->scale();
        QString scaleStr = normalizeDisplayScale(QString::number(curScale, 'g', 3));
        if (m_isTablet) {
            if (m_tabletScale != scaleStr) {
                m_tabletScale = scaleStr;
                changed = true;
            }
        } else {
            if (m_laptopScale != scaleStr) {
                m_laptopScale = scaleStr;
                changed = true;
            }
        }
    }

    if (changed) {
        saveProfiles();
    }
}

void TabletGesturesPlugin::loadConfig()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->reparseConfiguration();

    auto pluginsGroup = config->group(QStringLiteral("Plugins"));
    m_gesturesEnabled = pluginsGroup.readEntry(QStringLiteral("tablet_gesturesEnabled"), true);

    auto group = config->group(QStringLiteral("Effect-tablet_gestures"));

    m_hingeAngleThreshold = group.readEntry(QStringLiteral("HingeAngleThreshold"), 185);
    double rad = (m_hingeAngleThreshold * 0.5) * (M_PI / 180.0);
    m_hingeThresholdW = std::cos(rad);
    m_hingePollInterval = group.readEntry(QStringLiteral("HingePollInterval"), 350);
    if (m_hingeTimer && m_hingeTimer->isActive()) {
        m_hingeTimer->setInterval(m_hingePollInterval);
    }
    m_enableButtonScaling = group.readEntry(QStringLiteral("EnableButtonScaling"), true);
    m_tabletButtonSize = normalizeButtonSize(group.readEntry(QStringLiteral("TabletButtonSize"), QStringLiteral("Very Large")));
    m_laptopButtonSize = normalizeButtonSize(group.readEntry(QStringLiteral("LaptopButtonSize"), QStringLiteral("Medium")));
    m_enableDisplayScaling = group.readEntry(QStringLiteral("EnableDisplayScaling"), true);
    m_tabletScale = normalizeDisplayScale(group.readEntry(QStringLiteral("TabletScale"), QStringLiteral("100%")));
    m_laptopScale = normalizeDisplayScale(group.readEntry(QStringLiteral("LaptopScale"), QStringLiteral("100%")));

    qreal readThresh = group.readEntry(QStringLiteral("SwipeThreshold"), 45.0);
    m_pullThreshold = (readThresh < 15.0) ? 45.0 : qBound(15.0, readThresh, 120.0);
    m_hideKeyboardOnGesture = group.readEntry(QStringLiteral("HideKeyboardOnGesture"), true);

    if (m_zoneManager) {
        m_zoneManager->loadFromConfigGroup(group);
    }

    // Load extensible custom-command data from user-owned external store
    DiagnosticsLogger::instance().setEnabled(group.readEntry("DiagnosticsEnabled", true));

    const QString storePath = group.readEntry("CustomCommandsStorePath", QString());
    CustomCommandStore store(storePath);
    store.initialize();
    store.migrateFromConfigGroup(group);

    auto storeEntries = store.loadCommands();
    QMap<int, QString> customTitles;
    QMap<QString, QString> cmdPack;
    for (auto it = storeEntries.cbegin(); it != storeEntries.cend(); ++it) {
        const int slot = it.key();
        const QString command = it.value().command.trimmed();
        const QString title = it.value().title.trimmed();
        if (slot > 0 && !command.isEmpty()) {
            cmdPack.insert(QStringLiteral("custom_%1").arg(slot), command);
            cmdPack.insert(QStringLiteral("custom%1").arg(slot), command);
            if (!title.isEmpty()) customTitles.insert(slot, title);
        }
    }
    cmdPack.insert(QStringLiteral("custom"), cmdPack.value(QStringLiteral("custom_1")));

    if (m_actionRegistry) {
        m_actionRegistry->setCustomCommandTitles(customTitles);
        m_actionRegistry->setCustomCommandPack(cmdPack);
    }

    QJsonObject reloadDetails;
    reloadDetails.insert(QStringLiteral("store_path"), store.storePath());
    reloadDetails.insert(QStringLiteral("command_count"), storeEntries.size());
    reloadDetails.insert(QStringLiteral("hotspot_count"), m_zoneManager ? static_cast<int>(m_zoneManager->zones().size()) : 0);
    DiagnosticsLogger::instance().log(QStringLiteral("runtime"), QStringLiteral("INFO"), QStringLiteral("config_reloaded"), reloadDetails);
}

bool TabletGesturesPlugin::evaluateTabletMode()
{
    if (!m_hingePath.isEmpty()) {
        QFile file(m_hingePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QList<QByteArray> parts = data.split(' ');
            if (parts.size() >= 4) {
                bool ok = false;
                double wRaw = parts[3].trimmed().toDouble(&ok);
                if (ok) {
                    double w = wRaw * 0.0001;
                    if (w < m_hingeThresholdW) {
                        return true;
                    }
                }
            }
        }
    }

    for (const auto &dev : m_switches) {
        if (readSwitchState(dev.fd)) {
            return true;
        }
    }

    QString orient = readSensorOrientation();
    if (orient != QStringLiteral("normal") && !orient.isEmpty()) {
        return true;
    }

    return false;
}

void TabletGesturesPlugin::ensureInputMethod()
{
    if (kwinApp() && kwinApp()->inputMethod()) {
        if (!kwinApp()->inputMethod()->isAvailable()) {
            kwinApp()->inputMethod()->setInputMethodCommand(QStringLiteral("plasma-keyboard"));
        }
        kwinApp()->inputMethod()->setMode(InputMethod::VirtualKeyboardVisibility::AnyInput);
    }
}

void TabletGesturesPlugin::initHingeMonitoring()
{
    m_hingePath.clear();
    DIR *dir = opendir("/sys/bus/iio/devices");
    if (!dir) {
        return;
    }
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "iio:device", 10) != 0) {
            continue;
        }
        QString namePath = QStringLiteral("/sys/bus/iio/devices/") + QString::fromLatin1(ent->d_name) + QStringLiteral("/name");
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
            if (name == QStringLiteral("relative_orientation")) {
                m_hingePath = QStringLiteral("/sys/bus/iio/devices/") + QString::fromLatin1(ent->d_name) + QStringLiteral("/in_rot_quaternion_raw");
                break;
            }
        }
    }
    closedir(dir);

    if (!m_hingePath.isEmpty()) {
        if (!m_hingeTimer) {
            m_hingeTimer = new QTimer(this);
            connect(m_hingeTimer, &QTimer::timeout, this, &TabletGesturesPlugin::checkHingeSensor);
            m_hingeTimer->start(m_hingePollInterval);
        }
    }
}

void TabletGesturesPlugin::initSensorMonitoring()
{
    QDBusConnection::systemBus().connect(
        QStringLiteral("net.hadess.SensorProxy"),
        QStringLiteral("/net/hadess/SensorProxy"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(handleSensorPropertiesChanged(QString,QVariantMap,QStringList))
    );
}

QString TabletGesturesPlugin::readSensorOrientation()
{
    QDBusInterface iface(QStringLiteral("net.hadess.SensorProxy"),
                        QStringLiteral("/net/hadess/SensorProxy"),
                        QStringLiteral("net.hadess.SensorProxy"),
                        QDBusConnection::systemBus());
    if (iface.isValid()) {
        return iface.property("AccelerometerOrientation").toString();
    }
    return QStringLiteral("normal");
}

void TabletGesturesPlugin::initSwitchMonitoring()
{
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        return;
    }
    struct dirent *ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }
        QString devPath = QStringLiteral("/dev/input/") + QString::fromLatin1(ent->d_name);
        int fd = open(devPath.toUtf8().constData(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            unsigned long swBitmask[(SW_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
            if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(swBitmask)), swBitmask) >= 0) {
                if (swBitmask[SW_TABLET_MODE / (8 * sizeof(unsigned long))] & (1UL << (SW_TABLET_MODE % (8 * sizeof(unsigned long))))) {
                    SwitchDev dev;
                    dev.fd = fd;
                    dev.notifier = new QSocketNotifier(dev.fd, QSocketNotifier::Read, this);
                    connect(dev.notifier, &QSocketNotifier::activated, this, [this, fd]() {
                        handleSwitchEvent(fd);
                    });
                    m_switches.push_back(dev);
                    continue;
                }
            }
            close(fd);
        }
    }
    closedir(dir);
}

bool TabletGesturesPlugin::readSwitchState(int fd)
{
    if (fd < 0) {
        return false;
    }
    unsigned long swState[(SW_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
    if (ioctl(fd, EVIOCGSW(sizeof(swState)), swState) >= 0) {
        return (swState[SW_TABLET_MODE / (8 * sizeof(unsigned long))] & (1UL << (SW_TABLET_MODE % (8 * sizeof(unsigned long))))) != 0;
    }
    return false;
}

} // namespace KWin
