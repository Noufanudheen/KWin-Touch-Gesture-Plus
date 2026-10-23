/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QJsonObject>
#include <QMutex>

namespace KWin
{

class DiagnosticsLogger
{
public:
    static DiagnosticsLogger &instance();

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setLogPath(const QString &path);
    QString logPath() const;

    void log(const QString &component, const QString &level, const QString &event, const QJsonObject &details = QJsonObject());
    void log(const QString &component, const QString &level, const QString &event, const QString &message);

    static QString sanitizeForLog(const QString &text);
    static QString redactCommand(const QString &rawCommand);

private:
    DiagnosticsLogger();
    ~DiagnosticsLogger() = default;

    DiagnosticsLogger(const DiagnosticsLogger &) = delete;
    DiagnosticsLogger &operator=(const DiagnosticsLogger &) = delete;

    void checkAndRotate(const QString &resolvedPath);

    bool m_enabled = true;
    QString m_customLogPath;
    mutable QMutex m_mutex;
};

} // namespace KWin
