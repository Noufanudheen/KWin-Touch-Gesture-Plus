/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tablet_gestures_config.h"
#include "tablet_gestures_config_widget.h"
#include "../diagnostics_logger.h"

#include <KPluginFactory>
#include <KSharedConfig>
#include <QVBoxLayout>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QJsonObject>
#include <dlfcn.h>

#include <QStandardPaths>

namespace KWin
{

K_PLUGIN_CLASS(TabletGesturesConfig)

TabletGesturesConfig::TabletGesturesConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    Dl_info dlinfo;
    QString loadedModulePath = QStringLiteral("unknown");
    if (dladdr(reinterpret_cast<const void*>(&TabletGesturesConfig::staticMetaObject), &dlinfo) && dlinfo.dli_fname) {
        loadedModulePath = QString::fromUtf8(dlinfo.dli_fname);
    }

    const QString uiAssetPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("kwin/effects/tablet_gestures/contents/ui/config.ui"));

    QJsonObject initDetails;
    initDetails.insert(QStringLiteral("build_version"), QStringLiteral("v1.3.2-clean-workflow"));
    initDetails.insert(QStringLiteral("build_fingerprint"), QString::fromUtf8(__DATE__ " " __TIME__));
    initDetails.insert(QStringLiteral("module_path"), loadedModulePath);
    initDetails.insert(QStringLiteral("ui_asset_path"), uiAssetPath.isEmpty() ? QStringLiteral("<not_found>") : uiAssetPath);
    DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("kcm_init"), initDetails);

    QVBoxLayout *layout = new QVBoxLayout(widget());
    layout->setContentsMargins(0, 0, 0, 0);

    m_widget = new TabletGesturesConfigWidget(widget());
    layout->addWidget(m_widget);

    connect(m_widget, &TabletGesturesConfigWidget::changed, this, &TabletGesturesConfig::markAsChanged);

    load();
}

TabletGesturesConfig::~TabletGesturesConfig() = default;

void TabletGesturesConfig::load()
{
    KCModule::load();
    if (m_widget) {
        auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
        config->reparseConfiguration();
        auto group = config->group(QStringLiteral("Effect-tablet_gestures"));
        m_widget->load(group);
    }
}

void TabletGesturesConfig::save()
{
    KCModule::save();
    if (m_widget) {
        auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
        auto group = config->group(QStringLiteral("Effect-tablet_gestures"));
        m_widget->save(group);
        config->sync();

        // Reload effect and plugin via D-Bus
        QDBusMessage msgEffect = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/Effects"),
            QStringLiteral("org.kde.kwin.Effects"),
            QStringLiteral("reconfigureEffect")
        );
        msgEffect << QStringLiteral("tablet_gestures");
        QDBusConnection::sessionBus().send(msgEffect);

        QDBusMessage msgPlugin = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.TabletGestures"),
            QStringLiteral("/TabletGestures"),
            QStringLiteral("org.kde.TabletGestures"),
            QStringLiteral("reloadConfig")
        );
        QDBusConnection::sessionBus().send(msgPlugin);

        QJsonObject saveDetails;
        saveDetails.insert(QStringLiteral("reconfigure_sent"), true);
        DiagnosticsLogger::instance().log(QStringLiteral("kcm"), QStringLiteral("INFO"), QStringLiteral("kcm_saved"), saveDetails);
    }
}

void TabletGesturesConfig::defaults()
{
    KCModule::defaults();
    if (m_widget) {
        m_widget->defaults();
    }
}

} // namespace KWin

#include "tablet_gestures_config.moc"
#include "moc_tablet_gestures_config.cpp"
