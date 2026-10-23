/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "diagnostics_logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QStandardPaths>

namespace KWin
{

DiagnosticsLogger &DiagnosticsLogger::instance()
{
    static DiagnosticsLogger s_instance;
    return s_instance;
}

DiagnosticsLogger::DiagnosticsLogger()
{
}

void DiagnosticsLogger::setEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_enabled = enabled;
}

bool DiagnosticsLogger::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

void DiagnosticsLogger::setLogPath(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_customLogPath = path;
}

QString DiagnosticsLogger::logPath() const
{
    QMutexLocker locker(&m_mutex);
    if (!m_customLogPath.isEmpty()) {
        return m_customLogPath;
    }

    QString stateDir = qEnvironmentVariable("XDG_STATE_HOME");
    if (stateDir.trimmed().isEmpty()) {
        stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
    }
    if (stateDir.trimmed().isEmpty()) {
        stateDir = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return stateDir + QStringLiteral("/kwin-touch-gesture-plus/diagnostics.log");
}

void DiagnosticsLogger::checkAndRotate(const QString &resolvedPath)
{
    constexpr qint64 maxSizeBytes = 1024 * 1024; // 1 MiB rotation limit
    QFileInfo info(resolvedPath);
    if (info.exists() && info.size() >= maxSizeBytes) {
        const QString backupPath = resolvedPath + QStringLiteral(".1");
        QFile::remove(backupPath);
        QFile::rename(resolvedPath, backupPath);
    }
}

QString DiagnosticsLogger::sanitizeForLog(const QString &text)
{
    QString clean = text;
    clean.replace(QLatin1Char('\n'), QLatin1String(" \\n "));
    clean.replace(QLatin1Char('\r'), QLatin1String(""));
    clean.replace(QLatin1Char('"'), QLatin1String("\\\""));
    if (clean.length() > 256) {
        clean = clean.left(253) + QStringLiteral("...");
    }
    return clean;
}

QString DiagnosticsLogger::redactCommand(const QString &rawCommand)
{
    if (rawCommand.trimmed().isEmpty()) {
        return QStringLiteral("<empty>");
    }
    // Strict privacy rule: never log raw commands or shell arguments.
    // Only report length and prefix indicator (e.g. exec, shortcut, dbus).
    QString prefix = QStringLiteral("custom");
    if (rawCommand.startsWith(QStringLiteral("exec:"), Qt::CaseInsensitive)) {
        prefix = QStringLiteral("exec");
    } else if (rawCommand.startsWith(QStringLiteral("shortcut:"), Qt::CaseInsensitive)) {
        prefix = QStringLiteral("shortcut");
    } else if (rawCommand.startsWith(QStringLiteral("dbus:"), Qt::CaseInsensitive)) {
        prefix = QStringLiteral("dbus");
    }
    return QStringLiteral("[%1: len=%2]").arg(prefix).arg(rawCommand.trimmed().length());
}

void DiagnosticsLogger::log(const QString &component, const QString &level, const QString &event, const QJsonObject &details)
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled) {
        return;
    }

    const QString path = !m_customLogPath.isEmpty() ? m_customLogPath : [this]() {
        QString stateDir = qEnvironmentVariable("XDG_STATE_HOME");
        if (stateDir.trimmed().isEmpty()) {
            stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
        }
        if (stateDir.trimmed().isEmpty()) {
            stateDir = QDir::homePath() + QStringLiteral("/.local/state");
        }
        return stateDir + QStringLiteral("/kwin-touch-gesture-plus/diagnostics.log");
    }();

    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());

    checkAndRotate(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const qint64 pid = QCoreApplication::instance() ? QCoreApplication::applicationPid() : 0;

    QString line = QStringLiteral("[%1] [pid:%2] [%3] [%4] [%5]")
        .arg(timestamp)
        .arg(pid)
        .arg(component.toLower())
        .arg(level.toUpper())
        .arg(event);

    if (!details.isEmpty()) {
        const QString jsonStr = QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact));
        line.append(QLatin1Char(' '));
        line.append(jsonStr);
    }
    line.append(QLatin1Char('\n'));

    file.write(line.toUtf8());
    file.flush();
    file.close();
}

void DiagnosticsLogger::log(const QString &component, const QString &level, const QString &event, const QString &message)
{
    QJsonObject obj;
    if (!message.isEmpty()) {
        obj.insert(QStringLiteral("message"), sanitizeForLog(message));
    }
    log(component, level, event, obj);
}

} // namespace KWin
