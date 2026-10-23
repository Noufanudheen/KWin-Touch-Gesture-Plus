/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "zone_manager.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <KConfigGroup>
#include <algorithm>

namespace KWin
{

ElectricBorder GestureZone::edgeFromString(const QString &str)
{
    QString s = str.toLower().trimmed();
    if (s == QStringLiteral("top")) return ElectricTop;
    if (s == QStringLiteral("left")) return ElectricLeft;
    if (s == QStringLiteral("right")) return ElectricRight;
    return ElectricBottom;
}

QString GestureZone::edgeToString(ElectricBorder edge)
{
    switch (edge) {
    case ElectricTop: return QStringLiteral("top");
    case ElectricLeft: return QStringLiteral("left");
    case ElectricRight: return QStringLiteral("right");
    case ElectricBottom:
    default:
        return QStringLiteral("bottom");
    }
}

QJsonObject GestureZone::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("edge")] = edgeToString(edge);
    obj[QStringLiteral("startFraction")] = startFraction;
    obj[QStringLiteral("endFraction")] = endFraction;
    obj[QStringLiteral("gestureType")] = gestureType;
    obj[QStringLiteral("activationThreshold")] = activationThreshold;
    obj[QStringLiteral("enabled")] = enabled;
    obj[QStringLiteral("action")] = action;
    obj[QStringLiteral("oskPolicy")] = oskPolicy;
    obj[QStringLiteral("safeMarginStart")] = safeMarginStart;
    obj[QStringLiteral("safeMarginEnd")] = safeMarginEnd;
    obj[QStringLiteral("priority")] = priority;

    if (!actionParams.isEmpty()) {
        obj[QStringLiteral("actionParams")] = QJsonObject::fromVariantMap(actionParams);
    }
    return obj;
}

std::optional<GestureZone> GestureZone::fromJson(const QJsonObject &obj, QString &errorOut)
{
    GestureZone z;
    z.id = obj.value(QStringLiteral("id")).toString().trimmed();
    if (z.id.isEmpty()) {
        errorOut = QStringLiteral("Zone 'id' is required and cannot be empty.");
        return std::nullopt;
    }

    z.name = obj.value(QStringLiteral("name")).toString(z.id);
    z.edge = edgeFromString(obj.value(QStringLiteral("edge")).toString(QStringLiteral("bottom")));
    z.startFraction = obj.value(QStringLiteral("startFraction")).toDouble(0.0);
    z.endFraction = obj.value(QStringLiteral("endFraction")).toDouble(1.0);
    z.gestureType = obj.value(QStringLiteral("gestureType")).toString(QStringLiteral("inward_swipe"));
    z.activationThreshold = obj.value(QStringLiteral("activationThreshold")).toInt(45);
    z.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    z.action = obj.value(QStringLiteral("action")).toString().trimmed();
    z.oskPolicy = obj.value(QStringLiteral("oskPolicy")).toString(QStringLiteral("Always"));
    z.safeMarginStart = obj.value(QStringLiteral("safeMarginStart")).toInt(0);
    z.safeMarginEnd = obj.value(QStringLiteral("safeMarginEnd")).toInt(0);
    z.priority = obj.value(QStringLiteral("priority")).toInt(0);

    if (obj.contains(QStringLiteral("actionParams"))) {
        z.actionParams = obj.value(QStringLiteral("actionParams")).toObject().toVariantMap();
    }

    return z;
}

ZoneManager::ZoneManager()
{
    initDefaults();
}

void ZoneManager::initDefaults()
{
    m_zones.clear();

    // 1. Top Edge
    GestureZone topZone;
    topZone.id = QStringLiteral("top_fullscreen_expose");
    topZone.name = QStringLiteral("Top Edge: Fullscreen / Expose");
    topZone.edge = ElectricTop;
    topZone.startFraction = 0.0;
    topZone.endFraction = 1.0;
    topZone.action = QStringLiteral("fullscreen_or_expose");
    topZone.safeMarginStart = 100; // Top-left safe zone
    topZone.safeMarginEnd = 150;   // Top-right safe zone
    topZone.priority = 0;
    m_zones.push_back(topZone);

    // 2. Left Edge
    GestureZone leftZone;
    leftZone.id = QStringLiteral("left_activities");
    leftZone.name = QStringLiteral("Left Edge: Activities / Task Switcher");
    leftZone.edge = ElectricLeft;
    leftZone.startFraction = 0.0;
    leftZone.endFraction = 1.0;
    leftZone.action = QStringLiteral("shortcut:plasmashell:manage activities");
    leftZone.priority = 0;
    m_zones.push_back(leftZone);

    // 3. Right Edge
    GestureZone rightZone;
    rightZone.id = QStringLiteral("right_notifications");
    rightZone.name = QStringLiteral("Right Edge: System Tray / Notifications");
    rightZone.edge = ElectricRight;
    rightZone.startFraction = 0.0;
    rightZone.endFraction = 1.0;
    rightZone.action = QStringLiteral("shortcut:plasmashell:activate widget 157");
    rightZone.priority = 0;
    m_zones.push_back(rightZone);

    // 4. Bottom-Left Third
    GestureZone botLeftZone;
    botLeftZone.id = QStringLiteral("bottom_launcher");
    botLeftZone.name = QStringLiteral("Bottom Left: Application Launcher");
    botLeftZone.edge = ElectricBottom;
    botLeftZone.startFraction = 0.0;
    botLeftZone.endFraction = 0.333;
    botLeftZone.action = QStringLiteral("launcher");
    botLeftZone.oskPolicy = QStringLiteral("Disabled");
    botLeftZone.priority = 0;
    m_zones.push_back(botLeftZone);

    // 5. Bottom-Center Third
    GestureZone botCenterZone;
    botCenterZone.id = QStringLiteral("bottom_dock");
    botCenterZone.name = QStringLiteral("Bottom Center: Dock / Taskbar");
    botCenterZone.edge = ElectricBottom;
    botCenterZone.startFraction = 0.333;
    botCenterZone.endFraction = 0.666;
    botCenterZone.action = QStringLiteral("shortcut:plasmashell:activate widget 111");
    botCenterZone.oskPolicy = QStringLiteral("Disabled");
    botCenterZone.priority = 0;
    m_zones.push_back(botCenterZone);

    // 6. Bottom-Right Third
    GestureZone botRightZone;
    botRightZone.id = QStringLiteral("bottom_systemtray");
    botRightZone.name = QStringLiteral("Bottom Right: System Tray Popup Menu");
    botRightZone.edge = ElectricBottom;
    botRightZone.startFraction = 0.666;
    botRightZone.endFraction = 1.0;
    botRightZone.action = QStringLiteral("shortcut:plasmashell:activate widget 145");
    botRightZone.oskPolicy = QStringLiteral("Disabled");
    botRightZone.priority = 0;
    m_zones.push_back(botRightZone);
}

bool ZoneManager::validateZone(const GestureZone &zone, QString &errorOut) const
{
    if (zone.id.trimmed().isEmpty()) {
        errorOut = QStringLiteral("Zone ID cannot be empty.");
        return false;
    }
    if (zone.startFraction < 0.0 || zone.startFraction > 1.0) {
        errorOut = QStringLiteral("Zone '%1' startFraction (%2) must be between 0.0 and 1.0").arg(zone.id).arg(zone.startFraction);
        return false;
    }
    if (zone.endFraction < 0.0 || zone.endFraction > 1.0) {
        errorOut = QStringLiteral("Zone '%1' endFraction (%2) must be between 0.0 and 1.0").arg(zone.id).arg(zone.endFraction);
        return false;
    }
    if (zone.startFraction >= zone.endFraction) {
        errorOut = QStringLiteral("Zone '%1' startFraction (%2) must be strictly less than endFraction (%3)").arg(zone.id).arg(zone.startFraction).arg(zone.endFraction);
        return false;
    }
    if (zone.action.trimmed().isEmpty()) {
        errorOut = QStringLiteral("Zone '%1' has no action configured.").arg(zone.id);
        return false;
    }
    if (zone.safeMarginStart < 0 || zone.safeMarginEnd < 0) {
        errorOut = QStringLiteral("Zone '%1' safe margins cannot be negative.").arg(zone.id);
        return false;
    }
    return true;
}

QStringList ZoneManager::validateAll() const
{
    QStringList warnings;
    QList<QString> seenIds;

    for (const auto &z : m_zones) {
        QString err;
        if (!validateZone(z, err)) {
            warnings.append(err);
        }
        if (seenIds.contains(z.id)) {
            warnings.append(QStringLiteral("Duplicate zone ID detected: '%1'").arg(z.id));
        } else {
            seenIds.append(z.id);
        }
    }

    // Check for overlapping zones on same edge with identical priority
    for (size_t i = 0; i < m_zones.size(); ++i) {
        for (size_t j = i + 1; j < m_zones.size(); ++j) {
            const auto &z1 = m_zones[i];
            const auto &z2 = m_zones[j];
            if (z1.edge == z2.edge && z1.enabled && z2.enabled && z1.priority == z2.priority) {
                // Check range overlap: max(start1, start2) < min(end1, end2)
                qreal overlapStart = std::max(z1.startFraction, z2.startFraction);
                qreal overlapEnd = std::min(z1.endFraction, z2.endFraction);
                if (overlapStart < overlapEnd - 0.001) {
                    warnings.append(QStringLiteral("Notice: Zones '%1' and '%2' overlap on edge '%3' with equal priority (%4). Zone '%1' takes precedence.")
                        .arg(z1.id, z2.id, GestureZone::edgeToString(z1.edge)).arg(z1.priority));
                }
            }
        }
    }

    return warnings;
}

bool ZoneManager::addZone(const GestureZone &zone, QString &errorOut)
{
    if (!validateZone(zone, errorOut)) {
        return false;
    }
    if (findZoneById(zone.id) != nullptr) {
        errorOut = QStringLiteral("A zone with ID '%1' already exists.").arg(zone.id);
        return false;
    }
    m_zones.push_back(zone);
    return true;
}

bool ZoneManager::updateZone(const QString &id, const GestureZone &newDef, QString &errorOut)
{
    if (!validateZone(newDef, errorOut)) {
        return false;
    }
    for (auto &z : m_zones) {
        if (z.id == id) {
            z = newDef;
            return true;
        }
    }
    errorOut = QStringLiteral("Zone with ID '%1' not found.").arg(id);
    return false;
}

bool ZoneManager::removeZone(const QString &id)
{
    auto it = std::remove_if(m_zones.begin(), m_zones.end(), [&id](const GestureZone &z) {
        return z.id == id;
    });
    if (it != m_zones.end()) {
        m_zones.erase(it, m_zones.end());
        return true;
    }
    return false;
}

void ZoneManager::clearZones()
{
    m_zones.clear();
}

const GestureZone* ZoneManager::findZoneById(const QString &id) const
{
    for (const auto &z : m_zones) {
        if (z.id == id) {
            return &z;
        }
    }
    return nullptr;
}

const GestureZone* ZoneManager::matchZone(
    ElectricBorder edge,
    qreal normalizedPosition,
    qreal localCoord,
    qreal edgeLength,
    bool oskActive,
    qreal splitGapMin,
    qreal splitGapMax) const
{
    const GestureZone *bestMatch = nullptr;
    int highestPriority = -999999;

    for (const auto &zone : m_zones) {
        if (!zone.enabled || zone.edge != edge) {
            continue;
        }

        // Check fraction boundary
        if (normalizedPosition < zone.startFraction || normalizedPosition > zone.endFraction) {
            continue;
        }

        // Check safe margin exclusion
        if (zone.safeMarginStart > 0 && localCoord < zone.safeMarginStart) {
            continue;
        }
        if (zone.safeMarginEnd > 0 && edgeLength > 0 && localCoord > (edgeLength - zone.safeMarginEnd)) {
            continue;
        }

        // Check OSK Policy
        if (oskActive) {
            if (zone.oskPolicy == QStringLiteral("Disabled")) {
                continue;
            }
            if (zone.oskPolicy == QStringLiteral("SplitGapOnly")) {
                if (normalizedPosition < splitGapMin || normalizedPosition > splitGapMax) {
                    continue;
                }
            }
        }

        // Priority resolution
        if (zone.priority > highestPriority) {
            highestPriority = zone.priority;
            bestMatch = &zone;
        }
    }

    return bestMatch;
}

bool ZoneManager::loadFromJsonFile(const QString &filePath, QString &errorOut)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        errorOut = QStringLiteral("Unable to open file '%1' for reading: %2").arg(filePath, file.errorString());
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        errorOut = QStringLiteral("JSON parse error in '%1': %2").arg(filePath, parseErr.errorString());
        return false;
    }

    if (!doc.isObject()) {
        errorOut = QStringLiteral("Expected JSON root object in '%1'").arg(filePath);
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray zonesArray = root.value(QStringLiteral("zones")).toArray();
    if (zonesArray.isEmpty()) {
        errorOut = QStringLiteral("No 'zones' array found or array is empty in '%1'").arg(filePath);
        return false;
    }

    std::vector<GestureZone> parsed;
    for (int i = 0; i < zonesArray.size(); ++i) {
        QJsonObject zObj = zonesArray[i].toObject();
        QString zErr;
        auto optZ = GestureZone::fromJson(zObj, zErr);
        if (!optZ) {
            errorOut = QStringLiteral("Failed parsing zone at index %1: %2").arg(i).arg(zErr);
            return false;
        }
        QString vErr;
        if (!validateZone(*optZ, vErr)) {
            errorOut = QStringLiteral("Invalid zone at index %1: %2").arg(i).arg(vErr);
            return false;
        }
        parsed.push_back(*optZ);
    }

    m_zones = std::move(parsed);
    return true;
}

bool ZoneManager::saveToJsonFile(const QString &filePath, QString &errorOut) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorOut = QStringLiteral("Unable to open file '%1' for writing: %2").arg(filePath, file.errorString());
        return false;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("1.0");
    QJsonArray arr;
    for (const auto &z : m_zones) {
        arr.append(z.toJson());
    }
    root[QStringLiteral("zones")] = arr;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ZoneManager::loadFromConfigGroup(const KConfigGroup &group)
{
    // If CustomZonesJson exists in config group, load from it
    if (group.hasKey("CustomZonesJson")) {
        QString jsonStr = group.readEntry("CustomZonesJson", QString());
        if (!jsonStr.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isObject()) {
                QJsonArray arr = doc.object().value(QStringLiteral("zones")).toArray();
                if (!arr.isEmpty()) {
                    std::vector<GestureZone> parsed;
                    for (const auto &val : arr) {
                        QString err;
                        auto optZ = GestureZone::fromJson(val.toObject(), err);
                        if (optZ && validateZone(*optZ, err)) {
                            parsed.push_back(*optZ);
                        }
                    }
                    if (!parsed.empty()) {
                        m_zones = std::move(parsed);
                        return true;
                    }
                }
            }
        }
    }

    // Fallback: populate from legacy kwinrc keys
    initDefaults();

    // Top
    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("top_fullscreen_expose")))) {
        z->enabled = group.readEntry("EnableTopEdge", true);
        z->action = group.readEntry("TopAction", QStringLiteral("fullscreen_or_expose"));
        z->safeMarginStart = group.readEntry("TopLeftSafeWidth", 100);
        z->safeMarginEnd = group.readEntry("TopRightSafeWidth", 150);
    }
    // Left
    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("left_activities")))) {
        z->enabled = group.readEntry("EnableLeftEdge", true);
        z->action = group.readEntry("LeftAction", QStringLiteral("shortcut:plasmashell:manage activities"));
    }
    // Right
    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("right_notifications")))) {
        z->enabled = group.readEntry("EnableRightEdge", true);
        z->action = group.readEntry("RightAction", QStringLiteral("shortcut:plasmashell:activate widget 157"));
    }
    // Bottom
    bool botEnabled = group.readEntry("EnableBottomEdge", true);
    QString botOsk = group.readEntry("BottomGesturesWithOsk", QStringLiteral("Disabled"));

    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("bottom_launcher")))) {
        z->enabled = botEnabled;
        z->action = group.readEntry("BottomLeftAction", QStringLiteral("launcher"));
        z->oskPolicy = botOsk;
    }
    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("bottom_dock")))) {
        z->enabled = botEnabled;
        z->action = group.readEntry("BottomCenterAction", QStringLiteral("shortcut:plasmashell:activate widget 111"));
        z->oskPolicy = botOsk;
    }
    if (auto *z = const_cast<GestureZone*>(findZoneById(QStringLiteral("bottom_systemtray")))) {
        z->enabled = botEnabled;
        z->action = group.readEntry("BottomRightAction", QStringLiteral("shortcut:plasmashell:activate widget 145"));
        z->oskPolicy = botOsk;
    }

    return true;
}

void ZoneManager::saveToConfigGroup(KConfigGroup &group) const
{
    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("1.0");
    QJsonArray arr;
    for (const auto &z : m_zones) {
        arr.append(z.toJson());
    }
    root[QStringLiteral("zones")] = arr;

    QJsonDocument doc(root);
    group.writeEntry("CustomZonesJson", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

} // namespace KWin
