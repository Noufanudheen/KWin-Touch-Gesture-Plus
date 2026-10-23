/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QMap>
#include <KConfigGroup>

namespace KWin
{

class CustomCommandStore
{
public:
    struct CommandEntry {
        int slot = 0;
        QString title;
        QString command;
        QString createdAt;
        QString updatedAt;
    };

    explicit CustomCommandStore(const QString &configuredPath = QString());
    ~CustomCommandStore() = default;

    QString storePath() const;
    bool isValid() const;

    bool initialize(QString *errorOut = nullptr);

    QMap<int, CommandEntry> loadCommands(QString *errorOut = nullptr);
    bool saveAllCommands(const QMap<int, CommandEntry> &commands, QString *errorOut = nullptr);
    bool saveCommand(const CommandEntry &entry, QString *errorOut = nullptr);
    bool deleteCommand(int slot, QString *errorOut = nullptr);

    // Idempotent migration from kwinrc (CustomCommandsJson or legacy CustomCommand1..6)
    bool migrateFromConfigGroup(const KConfigGroup &group, QString *statusOut = nullptr);

    static QString defaultStorePath();
    static bool isSafeStorePath(const QString &path);

private:
    QString m_storePath;
    bool m_initialized = false;
};

} // namespace KWin
