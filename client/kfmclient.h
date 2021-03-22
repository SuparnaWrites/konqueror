/* This file is part of the KDE project
   Copyright (C) 1999-2006 David Faure <faure@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef KFMCLIENT_H
#define KFMCLIENT_H

#include <QObject>
class KJob;
class QUrl;
class QString;
class QCommandLineParser;

class ClientApp : public QObject
{
    Q_OBJECT
public:
    ClientApp();

    /** Parse command-line arguments and "do it" */
    bool doIt(const QCommandLineParser &parser);

    /** Make konqueror open a window for @p url */
    bool createNewWindow(const QUrl &url, bool newTab, bool tempFile, const QString &mimetype = QString());

    /** Make konqueror open a window for @p profile, @p url and @p mimetype, deprecated */
    bool openProfile(const QString &profile, const QUrl &url, const QString &mimetype = QString());

private Q_SLOTS:
    void slotResult(KJob *job);

private:
    void delayedQuit();

private:
    bool m_interactive = true;
};

#endif
