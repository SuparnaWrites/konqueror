/*
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2008 Laurent Montel <montel@kde.org>

    SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "webenginepartfactory.h"
#include "webenginepart_ext.h"
#include "webenginepart.h"

#include <kparts_version.h>
#if KPARTS_VERSION >= QT_VERSION_CHECK(5, 77, 0)
#include <KPluginMetaData>
#endif

#include <QWidget>

WebEngineFactory::~WebEngineFactory()
{
    // qCDebug(WEBENGINEPART_LOG) << this;
}

QObject *WebEngineFactory::create(const char* iface, QWidget *parentWidget, QObject *parent, const QVariantList &args, const QString& keyword)
{
    Q_UNUSED(iface);
    Q_UNUSED(keyword);
    Q_UNUSED(args);

    connect(parentWidget, &QObject::destroyed, this, &WebEngineFactory::slotDestroyed);

    // NOTE: The code below is what makes it possible to properly integrate QtWebEngine's PORTING_TODO
    // history management with any KParts based application.
    QByteArray histData (m_historyBufContainer.value(parentWidget));
    if (!histData.isEmpty()) histData = qUncompress(histData);
#if KPARTS_VERSION >= QT_VERSION_CHECK(5, 77, 0)
    WebEnginePart* part = new WebEnginePart(parentWidget, parent, metaData(), histData);
#else
    WebEnginePart* part = new WebEnginePart(parentWidget, parent, histData);
#endif
    WebEngineBrowserExtension* ext = qobject_cast<WebEngineBrowserExtension*>(part->browserExtension());
    if (ext) {
        connect(ext, QOverload<QObject *, const QByteArray &>::of(&WebEngineBrowserExtension::saveHistory),
                this, &WebEngineFactory::slotSaveHistory);
    }
    return part;
}

void WebEngineFactory::slotSaveHistory(QObject* widget, const QByteArray& buffer)
{
    // qCDebug(WEBENGINEPART_LOG) << "Caching history data from" << widget;
    m_historyBufContainer.insert(widget, buffer);
}

void WebEngineFactory::slotDestroyed(QObject* object)
{
    // qCDebug(WEBENGINEPART_LOG) << "Removing cached history data of" << object;
    m_historyBufContainer.remove(object);
}
