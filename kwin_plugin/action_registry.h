/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <memory>
#include <functional>

namespace KWin
{

enum class ActionType {
    None,
    Builtin,
    GlobalShortcut,
    ExternalCommand,
    DBusCall,
    Custom
};

struct ActionDefinition {
    ActionType type = ActionType::None;
    QString id;
    QString name;
    QString description;

    // For GlobalShortcut
    QString shortcutComponent;
    QString shortcutAction;

    // For ExternalCommand
    QString executable;
    QStringList arguments;

    // For DBusCall
    QString dbusService;
    QString dbusPath;
    QString dbusInterface;
    QString dbusMethod;
    QVariantList dbusArguments;

    // For Builtin
    QString builtinName;
};

class ActionRegistry : public QObject
{
    Q_OBJECT
public:
    explicit ActionRegistry(QObject *parent = nullptr);
    ~ActionRegistry() override = default;

    static QString normalizeAction(const QString &act, const QString &defaultAct = QStringLiteral("none"));

    bool parseActionString(const QString &actionStr, ActionDefinition &outDef, QString &errorOut) const;

    bool runAction(const QString &actionStr, bool hideKeyboard = true);
    bool runAction(const ActionDefinition &action, bool hideKeyboard = true);

    void invokeShortcut(const QString &component, const QString &action);
    void invokeDBus(const QString &service, const QString &path, const QString &iface, const QString &method, const QVariantList &args = {});
    bool executeExternal(const QString &executable, const QStringList &args);

    bool registerCustomAction(const QString &id, std::function<bool(const QVariantMap&)> handler);

    void setCustomCommandPack(const QMap<QString, QString> &commands);
    QMap<QString, QString> customCommandPack() const;
    void setCustomCommandTitle(const QString &title);
    void setCustomCommandTitle(int slot, const QString &title);
    void setCustomCommandTitles(const QMap<int, QString> &titles);
    QString customCommandTitle(int slot = 1) const;
    QMap<int, QString> customCommandTitles() const;
    QString resolveCustomCommand(const QString &key) const;

private:
    void hideInputMethodIfVisible();

    std::map<QString, std::function<bool(const QVariantMap&)>> m_customHandlers;
    QMap<QString, QString> m_customCommands;
    QMap<int, QString> m_customTitles;
};

} // namespace KWin
