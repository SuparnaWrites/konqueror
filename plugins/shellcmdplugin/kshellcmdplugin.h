/* This file is part of the KDE project
    SPDX-FileCopyrightText: 2000 David Faure <faure@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#ifndef kshellcmdplugin_h
#define kshellcmdplugin_h

#include <kparts/plugin.h>

class KShellCmdPlugin : public KParts::Plugin
{
    Q_OBJECT
public:
    KShellCmdPlugin(QObject *parent, const QVariantList &);
    ~KShellCmdPlugin() override {}

public Q_SLOTS:
    void slotExecuteShellCommand();
};

#endif
