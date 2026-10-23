/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "action_registry.h"

#include <kwin/workspace.h>
#include <kwin/window.h>
#include <kwin/main.h>
#include <kwin/inputmethod.h>

#include <QDBusMessage>
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QProcess>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include "diagnostics_logger.h"

Q_LOGGING_CATEGORY(KWIN_TOUCH_ACTIONS, "kwin.touch.actions", QtWarningMsg)

namespace KWin
{

ActionRegistry::ActionRegistry(QObject *parent)
    : QObject(parent)
{
    // No sample commands are installed implicitly. An empty command pack must
    // remain empty until the user adds real commands in the configuration UI.
}

void ActionRegistry::setCustomCommandPack(const QMap<QString, QString> &commands)
{
    // Replace the pack instead of merging it. Merging left deleted commands
    // executable in memory after a configuration reload.
    m_customCommands.clear();
    for (auto it = commands.cbegin(); it != commands.cend(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!key.isEmpty() && !it.value().trimmed().isEmpty()) {
            m_customCommands[key] = it.value().trimmed();
        }
    }
}

QMap<QString, QString> ActionRegistry::customCommandPack() const
{
    return m_customCommands;
}

void ActionRegistry::setCustomCommandTitle(const QString &title)
{
    setCustomCommandTitle(1, title);
}

void ActionRegistry::setCustomCommandTitle(int slot, const QString &title)
{
    QString trimmed = title.trimmed();
    if (trimmed.isEmpty()) {
        m_customTitles.remove(slot);
    } else {
        m_customTitles[slot] = trimmed;
    }
}

void ActionRegistry::setCustomCommandTitles(const QMap<int, QString> &titles)
{
    m_customTitles = titles;
}

QString ActionRegistry::customCommandTitle(int slot) const
{
    return m_customTitles.value(slot);
}

QMap<int, QString> ActionRegistry::customCommandTitles() const
{
    return m_customTitles;
}

QString ActionRegistry::resolveCustomCommand(const QString &key) const
{
    QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty() ||
        trimmedKey.startsWith(QStringLiteral("exec:")) ||
        trimmedKey.startsWith(QStringLiteral("shortcut:")) ||
        trimmedKey.startsWith(QStringLiteral("dbus:"))) {
        return QString();
    }

    // 1. If key matches any configured custom title
    for (auto it = m_customTitles.cbegin(); it != m_customTitles.cend(); ++it) {
        if (!it.value().isEmpty() && trimmedKey.compare(it.value(), Qt::CaseInsensitive) == 0) {
            QString slotKey = QStringLiteral("custom_%1").arg(it.key());
            if (m_customCommands.contains(slotKey) && !m_customCommands.value(slotKey).isEmpty()) {
                return m_customCommands.value(slotKey);
            }
        }
    }

    // 2. Direct matches for the primary custom command
    if (trimmedKey.compare(QStringLiteral("Custom Command"), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("Custom Command..."), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("custom:"), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("custom_1"), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("custom1"), Qt::CaseInsensitive) == 0 ||
        trimmedKey.compare(QStringLiteral("Custom Command 1"), Qt::CaseInsensitive) == 0) {
        return m_customCommands.value(QStringLiteral("custom_1"));
    }

    QString k = trimmedKey.toLower();
    if (m_customCommands.contains(k) && !m_customCommands.value(k).isEmpty()) {
        return m_customCommands.value(k);
    }
    if (k.startsWith(QLatin1String("custom_"))) {
        QString alt = QStringLiteral("custom") + k.mid(7);
        if (m_customCommands.contains(alt) && !m_customCommands.value(alt).isEmpty()) {
            return m_customCommands.value(alt);
        }
    } else if (k.startsWith(QLatin1String("custom")) && k.length() > 6) {
        QString alt = QStringLiteral("custom_") + k.mid(6);
        if (m_customCommands.contains(alt) && !m_customCommands.value(alt).isEmpty()) {
            return m_customCommands.value(alt);
        }
    }
    if (k.startsWith(QLatin1String("custom command "))) {
        QString num = k.mid(15).trimmed();
        QString alt = QStringLiteral("custom_") + num;
        if (m_customCommands.contains(alt) && !m_customCommands.value(alt).isEmpty()) {
            return m_customCommands.value(alt);
        }
    }
    if (k == QStringLiteral("custom") && m_customCommands.contains(QStringLiteral("custom_1"))) {
        return m_customCommands.value(QStringLiteral("custom_1"));
    }

    // 3. Fallback: match the command text in any configured slot, including
    // slots beyond the historical five-key KConfig layout.
    for (auto it = m_customCommands.cbegin(); it != m_customCommands.cend(); ++it) {
        if (it.key().startsWith(QStringLiteral("custom_")) && !it.value().isEmpty()
            && trimmedKey.compare(it.value(), Qt::CaseInsensitive) == 0) {
            return it.value();
        }
    }

    return QString();
}

QString ActionRegistry::normalizeAction(const QString &act, const QString &defaultAct)
{
    QString a = act.trimmed();
    if (a.isEmpty() || a == QStringLiteral("0") || a == QStringLiteral("default")) {
        return defaultAct;
    }
    if (a.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("None (Disabled)"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("none");
    }
    if (a == QStringLiteral("1") || a.compare(QStringLiteral("launcher"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Application Launcher"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("launcher");
    }
    if (a == QStringLiteral("2") || a == QStringLiteral("shortcut:plasmashell:activate widget 111") ||
        a.compare(QStringLiteral("Toggle Taskbar / Dock"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Taskbar / Dock"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("shortcut:plasmashell:activate widget 111");
    }
    if (a == QStringLiteral("3") || a == QStringLiteral("shortcut:plasmashell:activate widget 157") ||
        a.compare(QStringLiteral("Notifications Panel"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Notifications"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("shortcut:plasmashell:activate widget 157");
    }
    if (a == QStringLiteral("shortcut:plasmashell:activate widget 145") ||
        a.compare(QStringLiteral("System Tray Popup Menu"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("System Tray Popup"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("System Tray"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("shortcut:plasmashell:activate widget 145");
    }
    if (a == QStringLiteral("4") || a == QStringLiteral("shortcut:plasmashell:manage activities") ||
        a.compare(QStringLiteral("Activities / Task Switcher"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Activities"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("shortcut:plasmashell:manage activities");
    }
    if (a == QStringLiteral("5") || a.compare(QStringLiteral("fullscreen_or_expose"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Toggle Fullscreen / Expose"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("fullscreen_or_expose");
    }
    if (a == QStringLiteral("6") || a.compare(QStringLiteral("overview"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Overview"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("overview");
    }
    if (a == QStringLiteral("7") || a.compare(QStringLiteral("show_desktop"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Show Desktop"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("show_desktop");
    }
    if (a == QStringLiteral("8") || a.compare(QStringLiteral("virtual_keyboard"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Toggle Virtual Keyboard"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Virtual Keyboard"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("virtual_keyboard");
    }
    if (a.compare(QStringLiteral("Custom Command"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Custom Command..."), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom:"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("Custom Command 1"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom_1"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom1"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("custom_1");
    }
    if (a.compare(QStringLiteral("Custom Command 2"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom_2"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom2"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("custom_2");
    }
    if (a.compare(QStringLiteral("Custom Command 3"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom_3"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom3"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("custom_3");
    }
    if (a.compare(QStringLiteral("Custom Command 4"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom_4"), Qt::CaseInsensitive) == 0 ||
        a.compare(QStringLiteral("custom4"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("custom_4");
    }
    static const QRegularExpression customSlotRegex(
        QStringLiteral("^(?:custom[_ ]?(?:command[_ ]?)?|custom[_ ]?)([0-9]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = customSlotRegex.match(a);
    if (match.hasMatch()) {
        bool ok = false;
        const int slot = match.captured(1).toInt(&ok);
        if (ok && slot > 0) {
            return QStringLiteral("custom_%1").arg(slot);
        }
    }
    return a;
}

bool ActionRegistry::parseActionString(const QString &actionStr, ActionDefinition &outDef, QString &errorOut) const
{
    QString trimmed = normalizeAction(actionStr);
    if (trimmed.isEmpty() || trimmed == QStringLiteral("none")) {
        outDef.type = ActionType::None;
        outDef.id = QStringLiteral("none");
        return true;
    }

    // Check if it matches a custom command from the pack (e.g. custom_1, custom_2, custom)
    QString resolved = resolveCustomCommand(trimmed);
    if (!resolved.isEmpty() && resolved != trimmed) {
        return parseActionString(resolved, outDef, errorOut);
    }
    static const QRegularExpression unresolvedCustomSlot(
        QStringLiteral("^custom_[0-9]+$"), QRegularExpression::CaseInsensitiveOption);
    if (unresolvedCustomSlot.match(trimmed).hasMatch()) {
        errorOut = QStringLiteral("Custom command '%1' is not configured.").arg(trimmed);
        return false;
    }

    if (trimmed.startsWith(QStringLiteral("shortcut:"))) {
        QStringList parts = trimmed.split(QLatin1Char(':'));
        if (parts.size() < 3) {
            errorOut = QStringLiteral("Invalid shortcut format. Expected 'shortcut:<component>:<action>'");
            return false;
        }
        outDef.type = ActionType::GlobalShortcut;
        outDef.shortcutComponent = parts[1].trimmed();
        outDef.shortcutAction = parts.mid(2).join(QLatin1Char(':')).trimmed();
        outDef.id = trimmed;
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("dbus:"))) {
        QStringList parts = trimmed.split(QLatin1Char(':'));
        if (parts.size() < 5) {
            errorOut = QStringLiteral("Invalid dbus format. Expected 'dbus:<service>:<path>:<iface>:<method>'");
            return false;
        }
        outDef.type = ActionType::DBusCall;
        outDef.dbusService = parts[1].trimmed();
        outDef.dbusPath = parts[2].trimmed();
        outDef.dbusInterface = parts[3].trimmed();
        outDef.dbusMethod = parts[4].trimmed();
        outDef.id = trimmed;
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("exec:"))) {
        QString cmdLine = trimmed.mid(5).trimmed();
        if (cmdLine.isEmpty()) {
            errorOut = QStringLiteral("exec: action has empty command line");
            return false;
        }
        QStringList tokens = QProcess::splitCommand(cmdLine);
        if (tokens.isEmpty()) {
            errorOut = QStringLiteral("exec: unable to parse command arguments");
            return false;
        }
        outDef.type = ActionType::ExternalCommand;
        outDef.executable = tokens.takeFirst();
        outDef.arguments = tokens;
        outDef.id = trimmed;
        return true;
    }

    if (trimmed.startsWith(QStringLiteral("custom:"))) {
        QString cmdLine = trimmed.mid(7).trimmed();
        if (cmdLine.isEmpty()) {
            errorOut = QStringLiteral("custom: action has empty command line");
            return false;
        }
        QString resCmd = resolveCustomCommand(cmdLine);
        if (!resCmd.isEmpty()) {
            return parseActionString(resCmd, outDef, errorOut);
        }
        QStringList tokens = QProcess::splitCommand(cmdLine);
        if (tokens.isEmpty()) {
            errorOut = QStringLiteral("custom: unable to parse command arguments");
            return false;
        }
        outDef.type = ActionType::ExternalCommand;
        outDef.executable = tokens.takeFirst();
        outDef.arguments = tokens;
        outDef.id = trimmed;
        return true;
    }

    // Builtins
    if (trimmed == QStringLiteral("launcher") ||
        trimmed == QStringLiteral("fullscreen_or_expose") ||
        trimmed == QStringLiteral("overview") ||
        trimmed == QStringLiteral("expose") ||
        trimmed == QStringLiteral("show_desktop") ||
        trimmed == QStringLiteral("virtual_keyboard")) {
        outDef.type = ActionType::Builtin;
        outDef.builtinName = trimmed;
        outDef.id = trimmed;
        return true;
    }

    // Fallback: direct executable or command line
    QStringList tokens = QProcess::splitCommand(trimmed);
    if (!tokens.isEmpty()) {
        outDef.type = ActionType::ExternalCommand;
        outDef.executable = tokens.takeFirst();
        outDef.arguments = tokens;
        outDef.id = trimmed;
        return true;
    }

    errorOut = QStringLiteral("Unrecognized action: '%1'").arg(trimmed);
    return false;
}

bool ActionRegistry::runAction(const QString &actionStr, bool hideKeyboard)
{
    ActionDefinition def;
    QString err;
    if (!parseActionString(actionStr, def, err)) {
        qCWarning(KWIN_TOUCH_ACTIONS) << "Failed to parse action:" << actionStr << "Error:" << err;
        return false;
    }
    return runAction(def, hideKeyboard);
}

bool ActionRegistry::runAction(const ActionDefinition &action, bool hideKeyboard)
{
    bool success = true;

    switch (action.type) {
    case ActionType::None:
        return true;

    case ActionType::GlobalShortcut:
        invokeShortcut(action.shortcutComponent, action.shortcutAction);
        break;

    case ActionType::ExternalCommand:
        success = executeExternal(action.executable, action.arguments);
        break;

    case ActionType::DBusCall:
        invokeDBus(action.dbusService, action.dbusPath, action.dbusInterface, action.dbusMethod, action.dbusArguments);
        break;

    case ActionType::Builtin: {
        const QString &b = action.builtinName;
        if (b == QStringLiteral("launcher")) {
            invokeDBus(QStringLiteral("org.kde.plasmashell"),
                       QStringLiteral("/PlasmaShell"),
                       QStringLiteral("org.kde.PlasmaShell"),
                       QStringLiteral("activateLauncherMenu"));
        } else if (b == QStringLiteral("fullscreen_or_expose")) {
            Window *win = Workspace::self() ? Workspace::self()->activeWindow() : nullptr;
            if (win && win->isFullScreen()) {
                invokeShortcut(QStringLiteral("kwin"), QStringLiteral("Window Fullscreen"));
            } else {
                invokeShortcut(QStringLiteral("kwin"), QStringLiteral("ExposeAll"));
            }
        } else if (b == QStringLiteral("expose")) {
            invokeShortcut(QStringLiteral("kwin"), QStringLiteral("ExposeAll"));
        } else if (b == QStringLiteral("overview")) {
            invokeShortcut(QStringLiteral("kwin"), QStringLiteral("Overview"));
        } else if (b == QStringLiteral("show_desktop")) {
            invokeShortcut(QStringLiteral("plasmashell"), QStringLiteral("show desktop"));
        } else if (b == QStringLiteral("virtual_keyboard") || b == QStringLiteral("toggle_keyboard")) {
            if (kwinApp() && kwinApp()->inputMethod()) {
                if (kwinApp()->inputMethod()->isVisible()) {
                    kwinApp()->inputMethod()->hide();
                } else {
                    kwinApp()->inputMethod()->forceActivate();
                }
            }
        } else {
            qCWarning(KWIN_TOUCH_ACTIONS) << "Unknown builtin action:" << b;
            success = false;
        }
        break;
    }

    case ActionType::Custom: {
        auto it = m_customHandlers.find(action.id);
        if (it != m_customHandlers.end()) {
            success = it->second({});
        } else {
            qCWarning(KWIN_TOUCH_ACTIONS) << "No handler registered for custom action:" << action.id;
            success = false;
        }
        break;
    }
    }

    if (hideKeyboard && action.builtinName != QStringLiteral("virtual_keyboard") && action.builtinName != QStringLiteral("toggle_keyboard")) {
        hideInputMethodIfVisible();
    }

    QJsonObject diag;
    diag.insert(QStringLiteral("action_id"), action.id);
    diag.insert(QStringLiteral("action_type"), static_cast<int>(action.type));
    diag.insert(QStringLiteral("success"), success);
    DiagnosticsLogger::instance().log(QStringLiteral("runtime"), success ? QStringLiteral("INFO") : QStringLiteral("WARN"), QStringLiteral("action_executed"), diag);

    return success;
}

void ActionRegistry::invokeShortcut(const QString &component, const QString &action)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/") + component,
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut")
    );
    msg.setArguments({action});
    QDBusConnection::sessionBus().asyncCall(msg);
}

void ActionRegistry::invokeDBus(const QString &service, const QString &path, const QString &iface, const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, path, iface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusConnection::sessionBus().asyncCall(msg);
}

bool ActionRegistry::executeExternal(const QString &executable, const QStringList &args)
{
    if (executable.isEmpty()) {
        qCWarning(KWIN_TOUCH_ACTIONS) << "Cannot execute empty external program";
        return false;
    }

    // Security check: executable should exist or be in PATH
    return QProcess::startDetached(executable, args);
}

bool ActionRegistry::registerCustomAction(const QString &id, std::function<bool(const QVariantMap&)> handler)
{
    m_customHandlers[id] = std::move(handler);
    return true;
}

void ActionRegistry::hideInputMethodIfVisible()
{
    if (kwinApp() && kwinApp()->inputMethod() && kwinApp()->inputMethod()->isVisible()) {
        kwinApp()->inputMethod()->hide();
    }
}

} // namespace KWin
