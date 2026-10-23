/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "custom_command_store.h"
#include "diagnostics_logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace KWin
{

QString CustomCommandStore::defaultStorePath()
{
    QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configDir.trimmed().isEmpty()) {
        configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    }
    if (configDir.trimmed().isEmpty()) {
        configDir = QDir::homePath() + QStringLiteral("/.config");
    }
    return configDir + QStringLiteral("/kwin-touch-gesture-plus/commands");
}

bool CustomCommandStore::isSafeStorePath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return false;
    }
    QFileInfo info(path);
    const QString canonical = info.absoluteFilePath();

    // Reject system paths and unsafe write locations
    if (canonical.startsWith(QStringLiteral("/usr")) ||
        canonical.startsWith(QStringLiteral("/etc")) ||
        canonical.startsWith(QStringLiteral("/opt")) ||
        canonical.startsWith(QStringLiteral("/var")) ||
        canonical.startsWith(QStringLiteral("/root"))) {
        return false;
    }

    // Must be inside user's home directory or dedicated user config location, or temp directory (for tests)
    const QString home = QDir::homePath();
    const QString temp = QDir::tempPath();
    if (!canonical.startsWith(home) && !canonical.startsWith(temp)) {
        return false;
    }

    return true;
}

CustomCommandStore::CustomCommandStore(const QString &configuredPath)
{
    if (!configuredPath.trimmed().isEmpty() && isSafeStorePath(configuredPath.trimmed())) {
        m_storePath = QFileInfo(configuredPath.trimmed()).absoluteFilePath();
    } else {
        m_storePath = defaultStorePath();
    }
}

QString CustomCommandStore::storePath() const
{
    return m_storePath;
}

bool CustomCommandStore::isValid() const
{
    return isSafeStorePath(m_storePath);
}

bool CustomCommandStore::initialize(QString *errorOut)
{
    if (!isValid()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Store path '%1' is not a safe user-writable path.").arg(m_storePath);
        }
        return false;
    }

    QDir dir(m_storePath);
    if (!dir.exists()) {
        if (!dir.mkpath(QStringLiteral("."))) {
            if (errorOut) {
                *errorOut = QStringLiteral("Failed to create directory '%1'.").arg(m_storePath);
            }
            return false;
        }
    }
    m_initialized = true;
    return true;
}

QMap<int, CustomCommandStore::CommandEntry> CustomCommandStore::loadCommands(QString *errorOut)
{
    QMap<int, CommandEntry> result;
    if (!initialize(errorOut)) {
        return result;
    }

    QDir dir(m_storePath);
    const QString manifestPath = dir.filePath(QStringLiteral("manifest.json"));
    QFile manifestFile(manifestPath);

    bool manifestRead = false;
    if (manifestFile.exists() && manifestFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        manifestFile.close();

        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonArray commands = doc.object().value(QStringLiteral("commands")).toArray();
            for (const QJsonValue &val : commands) {
                const QJsonObject item = val.toObject();
                const int slot = item.value(QStringLiteral("slot")).toInt(0);
                const QString fileName = item.value(QStringLiteral("file")).toString(
                    QStringLiteral("custom_%1.json").arg(slot));

                if (slot > 0) {
                    QFile cmdFile(dir.filePath(fileName));
                    if (cmdFile.exists() && cmdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        const QJsonDocument cmdDoc = QJsonDocument::fromJson(cmdFile.readAll());
                        cmdFile.close();
                        if (cmdDoc.isObject()) {
                            const QJsonObject obj = cmdDoc.object();
                            CommandEntry entry;
                            entry.slot = slot;
                            entry.title = obj.value(QStringLiteral("title")).toString().trimmed();
                            entry.command = obj.value(QStringLiteral("command")).toString().trimmed();
                            entry.createdAt = obj.value(QStringLiteral("createdAt")).toString();
                            entry.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
                            if (!entry.command.isEmpty()) {
                                result[slot] = entry;
                            }
                        }
                    }
                }
            }
            manifestRead = true;
        }
    }

    // Fallback: If manifest.json is missing or corrupted, scan for custom_*.json directly
    if (!manifestRead || result.isEmpty()) {
        const QStringList files = dir.entryList(QStringList{QStringLiteral("custom_*.json")}, QDir::Files);
        static const QRegularExpression regex(QStringLiteral("^custom_([0-9]+)\\.json$"));
        for (const QString &file : files) {
            const auto match = regex.match(file);
            if (match.hasMatch()) {
                const int slot = match.captured(1).toInt();
                QFile cmdFile(dir.filePath(file));
                if (cmdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    const QJsonDocument cmdDoc = QJsonDocument::fromJson(cmdFile.readAll());
                    cmdFile.close();
                    if (cmdDoc.isObject()) {
                        const QJsonObject obj = cmdDoc.object();
                        CommandEntry entry;
                        entry.slot = slot;
                        entry.title = obj.value(QStringLiteral("title")).toString().trimmed();
                        entry.command = obj.value(QStringLiteral("command")).toString().trimmed();
                        entry.createdAt = obj.value(QStringLiteral("createdAt")).toString();
                        entry.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
                        if (!entry.command.isEmpty()) {
                            result[slot] = entry;
                        }
                    }
                }
            }
        }
    }

    return result;
}

bool CustomCommandStore::saveAllCommands(const QMap<int, CommandEntry> &commands, QString *errorOut)
{
    if (!initialize(errorOut)) {
        return false;
    }

    QDir dir(m_storePath);
    const QString nowIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    // 1. Write individual custom_<slot>.json files using atomic QSaveFile
    QJsonArray manifestCommands;
    for (auto it = commands.cbegin(); it != commands.cend(); ++it) {
        const int slot = it.key();
        const CommandEntry &entry = it.value();
        if (slot <= 0 || entry.command.trimmed().isEmpty()) {
            continue;
        }

        const QString fileName = QStringLiteral("custom_%1.json").arg(slot);
        QSaveFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorOut) *errorOut = QStringLiteral("Failed to open '%1' for atomic save.").arg(fileName);
            return false;
        }

        QJsonObject obj;
        obj.insert(QStringLiteral("schema_version"), 1);
        obj.insert(QStringLiteral("slot"), slot);
        obj.insert(QStringLiteral("title"), entry.title);
        obj.insert(QStringLiteral("command"), entry.command);
        obj.insert(QStringLiteral("createdAt"), entry.createdAt.isEmpty() ? nowIso : entry.createdAt);
        obj.insert(QStringLiteral("updatedAt"), nowIso);

        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        if (!file.commit()) {
            if (errorOut) *errorOut = QStringLiteral("Atomic commit failed for '%1'.").arg(fileName);
            return false;
        }

        QJsonObject manifestItem;
        manifestItem.insert(QStringLiteral("slot"), slot);
        manifestItem.insert(QStringLiteral("file"), fileName);
        manifestItem.insert(QStringLiteral("title"), entry.title);
        manifestCommands.append(manifestItem);
    }

    // 2. Remove obsolete custom_*.json files
    const QStringList existingFiles = dir.entryList(QStringList{QStringLiteral("custom_*.json")}, QDir::Files);
    static const QRegularExpression regex(QStringLiteral("^custom_([0-9]+)\\.json$"));
    for (const QString &file : existingFiles) {
        const auto match = regex.match(file);
        if (match.hasMatch()) {
            const int slot = match.captured(1).toInt();
            if (!commands.contains(slot)) {
                dir.remove(file);
            }
        }
    }

    // 3. Write manifest.json using atomic QSaveFile
    QSaveFile manifestFile(dir.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = QStringLiteral("Failed to open manifest.json for atomic save.");
        return false;
    }

    QJsonObject manifestRoot;
    manifestRoot.insert(QStringLiteral("version"), 1);
    manifestRoot.insert(QStringLiteral("updatedAt"), nowIso);
    manifestRoot.insert(QStringLiteral("storePath"), m_storePath);
    manifestRoot.insert(QStringLiteral("commands"), manifestCommands);

    manifestFile.write(QJsonDocument(manifestRoot).toJson(QJsonDocument::Indented));
    if (!manifestFile.commit()) {
        if (errorOut) *errorOut = QStringLiteral("Atomic commit failed for manifest.json.");
        return false;
    }

    return true;
}

bool CustomCommandStore::saveCommand(const CommandEntry &entry, QString *errorOut)
{
    auto all = loadCommands(errorOut);
    all[entry.slot] = entry;
    return saveAllCommands(all, errorOut);
}

bool CustomCommandStore::deleteCommand(int slot, QString *errorOut)
{
    auto all = loadCommands(errorOut);
    if (!all.contains(slot)) {
        return true;
    }
    all.remove(slot);
    return saveAllCommands(all, errorOut);
}

bool CustomCommandStore::migrateFromConfigGroup(const KConfigGroup &group, QString *statusOut)
{
    if (!initialize(statusOut)) {
        return false;
    }

    // Idempotent check: if external store already has commands, do not overwrite!
    auto existing = loadCommands();
    if (!existing.isEmpty()) {
        if (statusOut) {
            *statusOut = QStringLiteral("External store already populated with %1 commands; skipping migration.").arg(existing.size());
        }
        return true;
    }

    QMap<int, CommandEntry> migrated;

    // Check canonical CustomCommandsJson
    const QString serialized = group.readEntry("CustomCommandsJson", QString());
    if (!serialized.trimmed().isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(serialized.toUtf8());
        if (doc.isObject() && doc.object().value(QStringLiteral("commands")).isArray()) {
            const QJsonArray commands = doc.object().value(QStringLiteral("commands")).toArray();
            for (const QJsonValue &v : commands) {
                const QJsonObject o = v.toObject();
                const int slot = o.value(QStringLiteral("slot")).toInt(0);
                const QString cmd = o.value(QStringLiteral("command")).toString().trimmed();
                if (slot > 0 && !cmd.isEmpty()) {
                    CommandEntry entry;
                    entry.slot = slot;
                    entry.title = o.value(QStringLiteral("title")).toString().trimmed();
                    entry.command = cmd;
                    migrated[slot] = entry;
                }
            }
        }
    }

    // Fallback: check legacy CustomCommand1..6 keys
    if (migrated.isEmpty()) {
        for (int i = 1; i <= 6; ++i) {
            const QString cmd = group.readEntry(QStringLiteral("CustomCommand%1").arg(i), QString()).trimmed();
            QString title = group.readEntry(QStringLiteral("CustomCommandTitle%1").arg(i), QString()).trimmed();
            if (i == 1 && title.isEmpty()) {
                title = group.readEntry("CustomCommandTitle", QString()).trimmed();
            }
            if (!cmd.isEmpty()) {
                CommandEntry entry;
                entry.slot = i;
                entry.title = title;
                entry.command = cmd;
                migrated[i] = entry;
            }
        }
    }

    if (!migrated.isEmpty()) {
        const bool ok = saveAllCommands(migrated, statusOut);
        if (statusOut) {
            *statusOut = ok ? QStringLiteral("Successfully migrated %1 commands to external store.").arg(migrated.size())
                            : QStringLiteral("Failed to write migrated commands: %1").arg(*statusOut);
        }
        return ok;
    }

    if (statusOut) {
        *statusOut = QStringLiteral("No commands found in config to migrate.");
    }
    return true;
}

} // namespace KWin
