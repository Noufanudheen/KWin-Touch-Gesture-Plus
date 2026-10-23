/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <kwin/screenedge.h>

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <vector>
#include <optional>

class KConfigGroup;

namespace KWin
{

struct GestureZone {
    QString id;
    QString name;
    ElectricBorder edge = ElectricBottom;
    qreal startFraction = 0.0; // 0.0 - 1.0 along the edge
    qreal endFraction = 1.0;   // 0.0 - 1.0 along the edge
    QString gestureType = QStringLiteral("inward_swipe");
    int activationThreshold = 45; // pixels
    bool enabled = true;
    QString action;
    QVariantMap actionParams;
    QString oskPolicy = QStringLiteral("Always"); // "Always", "Disabled", "SplitGapOnly"
    int safeMarginStart = 0; // pixels from the start corner of the edge
    int safeMarginEnd = 0;   // pixels from the end corner of the edge
    int priority = 0;        // Higher priority wins in overlapping regions

    QJsonObject toJson() const;
    static std::optional<GestureZone> fromJson(const QJsonObject &obj, QString &errorOut);
    static ElectricBorder edgeFromString(const QString &str);
    static QString edgeToString(ElectricBorder edge);
};

class ZoneManager
{
public:
    ZoneManager();
    ~ZoneManager() = default;

    void initDefaults();

    const std::vector<GestureZone>& zones() const { return m_zones; }
    std::vector<GestureZone>& zones() { return m_zones; }

    bool addZone(const GestureZone &zone, QString &errorOut);
    bool updateZone(const QString &id, const GestureZone &newDef, QString &errorOut);
    bool removeZone(const QString &id);
    void clearZones();

    const GestureZone* findZoneById(const QString &id) const;

    const GestureZone* matchZone(
        ElectricBorder edge,
        qreal normalizedPosition, // 0.0 - 1.0
        qreal localCoord,         // physical pixels along edge
        qreal edgeLength,         // total physical length of edge
        bool oskActive,
        qreal splitGapMin = 0.40,
        qreal splitGapMax = 0.60
    ) const;

    bool validateZone(const GestureZone &zone, QString &errorOut) const;
    QStringList validateAll() const;

    bool loadFromJsonFile(const QString &filePath, QString &errorOut);
    bool saveToJsonFile(const QString &filePath, QString &errorOut) const;

    bool loadFromConfigGroup(const KConfigGroup &group);
    void saveToConfigGroup(KConfigGroup &group) const;

private:
    std::vector<GestureZone> m_zones;
};

} // namespace KWin
