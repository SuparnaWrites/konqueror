/* This file is part of the KDE project
    SPDX-FileCopyrightText: 1998, 1999, 2010 David Faure <faure@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "konqmisc.h"
#include <kparts/browserrun.h>
#include "konqsettingsxt.h"
#include "konqmainwindow.h"
#include "konqviewmanager.h"
#include "konqview.h"
#include "konqmainwindowfactory.h"
#include "konqurl.h"

#include "konqdebug.h"
#include <kurifilter.h>
#include <KLocalizedString>

#include <kprotocolmanager.h>
#include <kiconloader.h>
#include <kconfiggroup.h>
#include <QList>
#include <QStandardPaths>

/**********************************************
 *
 * KonqMisc
 *
 **********************************************/
KonqMainWindow *KonqMisc::newWindowFromHistory(KonqView *view, int steps)
{
    int oldPos = view->historyIndex();
    int newPos = oldPos + steps;

    const HistoryEntry *he = view->historyAt(newPos);
    if (!he) {
        return nullptr;
    }

    KonqMainWindow *mainwindow = KonqMainWindowFactory::createEmptyWindow();
    if (!mainwindow) {
        return nullptr;
    }
    KonqView *newView = mainwindow->currentView();

    if (!newView) {
        return nullptr;
    }

    newView->copyHistory(view);
    newView->setHistoryIndex(newPos);
    newView->restoreHistory();
    mainwindow->show();
    return mainwindow;
}

QUrl KonqMisc::konqFilteredURL(KonqMainWindow *parent, const QString &_url, const QUrl &currentDirectory)
{
    Q_UNUSED(parent); // Useful if we want to change the error handling again

    if (!KonqUrl::canBeKonqUrl(_url)) {     // Don't filter "konq:" URLs
        KUriFilterData data(_url);

        if (currentDirectory.isLocalFile()) {
            data.setAbsolutePath(currentDirectory.toLocalFile());
        }

        // We do not want to the filter to check for executables
        // from the location bar.
        data.setCheckForExecutables(false);

        if (KUriFilter::self()->filterUri(data)) {
            if (data.uriType() == KUriFilterData::Error) {
                if (data.errorMsg().isEmpty()) {
                    return KParts::BrowserRun::makeErrorUrl(KIO::ERR_MALFORMED_URL, _url, QUrl(_url));
                } else {
                    return KParts::BrowserRun::makeErrorUrl(KIO::ERR_SLAVE_DEFINED, data.errorMsg(), QUrl(_url));
                }
            } else {
                return data.uri();
            }
        }

        // Show an explicit error for an unknown URL scheme, because the
        // explanation generated by KIO::rawErrorDetail() gives useful
        // information.
        const QString scheme = data.uri().scheme();
        if (!scheme.isEmpty() && !KProtocolInfo::isKnownProtocol(scheme)) {
            return KParts::BrowserRun::makeErrorUrl(KIO::ERR_UNSUPPORTED_PROTOCOL, _url, QUrl(_url));
        }

        // NOTE: a valid URL like http://kde.org always passes the filtering test.
        // As such, this point could only be reached when _url is NOT a valid URL.
        return KParts::BrowserRun::makeErrorUrl(KIO::ERR_MALFORMED_URL, _url, QUrl(_url));
    }

    const bool isKnownAbout = KonqUrl::hasKnownPathRoot(_url);

    return isKnownAbout ? QUrl(_url) : KonqUrl::url(KonqUrl::Type::NoPath);
}

QString KonqMisc::encodeFilename(QString filename)
{
    return filename.replace(':', '_');
}

QString KonqMisc::decodeFilename(QString filename)
{
    return filename.replace('_', ':');
}

