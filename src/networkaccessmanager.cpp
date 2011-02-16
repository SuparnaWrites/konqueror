/*
 * This file is part of the KDE project.
 *
 * Copyright (C) 2009 Dawit Alemayehu <adawit @ kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "networkaccessmanager.h"
#include "settings/webkitsettings.h"

#include <KDE/KDebug>
#include <KDE/KLocalizedString>

#include <QtCore/QTimer>
#include <QtNetwork/QNetworkReply>
#include <QtWebKit/QWebFrame>
#include <QtWebKit/QWebElementCollection>


#define QL1S(x) QLatin1String(x)
#define HIDABLE_ELEMENTS   QL1S("audio,img,embed,object,iframe,frame,video")

/* Null network reply */
class NullNetworkReply : public QNetworkReply
{
public:
    NullNetworkReply(const QNetworkRequest &req, QObject* parent = 0)
        :QNetworkReply(parent)
    {
        setRequest(req);
        setUrl(req.url());
        setHeader(QNetworkRequest::ContentLengthHeader, 0);
        setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
        setError(QNetworkReply::ContentAccessDenied, i18n("Blocked by ad filter"));
        setAttribute(QNetworkRequest::User, QNetworkReply::ContentAccessDenied);
        QTimer::singleShot(0, this, SIGNAL(finished()));
    }

    virtual void abort() {}
    virtual qint64 bytesAvailable() const { return 0; }

protected:
    virtual qint64 readData(char*, qint64) {return -1;}
};

namespace KDEPrivate {

MyNetworkAccessManager::MyNetworkAccessManager(QObject *parent)
                       : KIO::AccessManager(parent)
{
}

static bool allowRequest(QNetworkAccessManager::Operation op, const QUrl& requestUrl)
{
   if (op == QNetworkAccessManager::GetOperation)
       return true;

   if (!WebKitSettings::self()->isAdFilterEnabled())
       return true;

   if (!WebKitSettings::self()->isAdFiltered(requestUrl.toString()))
       return true;

   //kDebug() << "*** REQUEST BLOCKED BY FILTER:" << WebKitSettings::self()->adFilteredBy(requestUrl.toString());
   return false;
}

QNetworkReply *MyNetworkAccessManager::createRequest(Operation op, const QNetworkRequest &req, QIODevice *outgoingData)
{
    if (allowRequest(op, req.url()))
        return KIO::AccessManager::createRequest(op, req, outgoingData);

    QWebFrame* frame = qobject_cast<QWebFrame*>(req.originatingObject());
    if (frame) {
        if (!m_blockedRequests.contains(frame))
            connect(frame, SIGNAL(loadFinished(bool)), this, SLOT(slotFinished(bool)));
        m_blockedRequests.insert(frame, req.url());
    }

    //kDebug() << "*** BLOCKED UNAUTHORIZED REQUEST => " << req.url() << frame;
    return new NullNetworkReply(req, this);
}

static void hideBlockedElements(const QUrl& url, QWebElementCollection& collection)
{
    for (QWebElementCollection::iterator it = collection.begin(); it != collection.end(); ++it) {
        const QUrl baseUrl ((*it).webFrame()->baseUrl());
        QString src = (*it).attribute(QL1S("src"));
        if (src.isEmpty())
            src = (*it).evaluateJavaScript(QL1S("this.src")).toString();
        if (src.isEmpty())
            continue;
        const QUrl resolvedUrl (baseUrl.resolved(src));
        if (url == resolvedUrl) {
            //kDebug() << "*** HIDING ELEMENT: " << (*it).tagName() << resolvedUrl;
            (*it).removeFromDocument();
        }
    }
}

void MyNetworkAccessManager::slotFinished(bool ok)
{
    if (!ok)
        return;

    if(!WebKitSettings::self()->isAdFilterEnabled())
        return;

    if(!WebKitSettings::self()->isHideAdsEnabled())
        return;

    QWebFrame* frame = qobject_cast<QWebFrame*>(sender());
    if (!frame)
        return;

    QList<QUrl> urls = m_blockedRequests.values(frame);
    if (urls.isEmpty())
        return;

   QWebElementCollection collection = frame->findAllElements(HIDABLE_ELEMENTS);
   if (frame->parentFrame())
        collection += frame->parentFrame()->findAllElements(HIDABLE_ELEMENTS);

    Q_FOREACH(const QUrl& url, urls)
        hideBlockedElements(url, collection);
}

}

#include "networkaccessmanager.moc"
