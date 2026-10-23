/*
    SPDX-FileCopyrightText: 2026 Noufanudheen <developer@local>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KCModule>
#include <KPluginMetaData>

namespace KWin
{

class TabletGesturesConfigWidget;

class TabletGesturesConfig : public KCModule
{
    Q_OBJECT

public:
    explicit TabletGesturesConfig(QObject *parent, const KPluginMetaData &data);
    ~TabletGesturesConfig() override;

    void load() override;
    void save() override;
    void defaults() override;

private:
    TabletGesturesConfigWidget *m_widget = nullptr;
};

} // namespace KWin
