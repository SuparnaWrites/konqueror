/* This file is part of the KDE project

   Copyright (C) 2002 Patrick Charbonnier <pch@valleeurpe.net>
   Copyright (C) 2002 Carsten Pfeiffer <pfeiffer@kde.org>
   Copyright (C) 2007 Urs Wolfer <uwolfer @ kde.org>
   Copyright (C) 2010 Dawit Alemayehu <adawit@kde.org>
   Copyright (C) 2010 Matthias Fuchs <mat69@gmx.net>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

#include "kget_plugin.h"

#include "kget_interface.h"
#include "konq_kpart_plugin.h"

#include <QDBusConnection>
#include <QIcon>
#include <QMenu>

#include <KActionCollection>
#include <KActionMenu>
#include <KDialogJobUiDelegate>
#include <KFileItem>
#include <KIO/CommandLauncherJob>
#include <KIconLoader>
#include <KLocalizedString>
#include <KMessageBox>
#include <KParts/FileInfoExtension>
#include <KParts/HtmlExtension>
#include <KParts/Part>
#include <KParts/PartManager>
#include <KParts/ReadOnlyPart>
#include <KParts/SelectorInterface>
#include <KPluginFactory>
#include <KProtocolInfo>
#include <KToggleAction>
#include <KToolInvocation>
#include <konq_kpart_plugin.h>

#define QL1S(x) QLatin1String(x)

K_PLUGIN_CLASS_WITH_JSON(KGetPlugin, "kget_plugin.json")

static QWidget *partWidget(QObject *obj)
{
    auto *part = qobject_cast<KParts::ReadOnlyPart *>(obj);
    return part ? part->widget() : nullptr;
}

KGetPlugin::KGetPlugin(QObject *parent, const QVariantList &)
    : KonqParts::Plugin(parent)
{
    auto *menu = new KActionMenu(QIcon::fromTheme("kget"), i18n("Download Manager"), actionCollection());
    actionCollection()->addAction("kget_menu", menu);

    menu->setDelayed(false);
    connect(menu->menu(), &QMenu::aboutToShow, this, &KGetPlugin::showPopup);

    m_dropTargetAction = new KToggleAction(i18n("Show Drop Target"), actionCollection());

    connect(m_dropTargetAction, &QAction::triggered, this, &KGetPlugin::slotShowDrop);
    actionCollection()->addAction(QL1S("show_drop"), m_dropTargetAction);
    menu->addAction(m_dropTargetAction);

    QAction *showLinksAction = actionCollection()->addAction(QL1S("show_links"));
    showLinksAction->setText(i18n("List All Links"));
    connect(showLinksAction, &QAction::triggered, this, &KGetPlugin::slotShowLinks);
    menu->addAction(showLinksAction);

    QAction *showSelectedLinksAction = actionCollection()->addAction(QL1S("show_selected_links"));
    showSelectedLinksAction->setText(i18n("List Selected Links"));
    connect(showSelectedLinksAction, &QAction::triggered, this, &KGetPlugin::slotShowSelectedLinks);
    menu->addAction(showSelectedLinksAction);

    // Hide this plugin if the parent part does not support either
    // The FileInfo or Html extensions...
    if (!KParts::HtmlExtension::childObject(parent) && !KParts::FileInfoExtension::childObject(parent))
        menu->setVisible(false);
}

KGetPlugin::~KGetPlugin()
{
}

static bool hasDropTarget()
{
    bool found = false;

    if (QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.kget")) {
        OrgKdeKgetMainInterface kgetInterface("org.kde.kget", "/KGet", QDBusConnection::sessionBus());
        QDBusReply<bool> reply = kgetInterface.dropTargetVisible();
        if (reply.isValid()) {
            found = reply.value();
        }
    }

    return found;
}

void KGetPlugin::showPopup()
{
    // Check for HtmlExtension support...
    KParts::HtmlExtension *htmlExtn = KParts::HtmlExtension::childObject(parent());
    if (htmlExtn) {
        KParts::SelectorInterface *selector = qobject_cast<KParts::SelectorInterface *>(htmlExtn);
        if (selector) {
            m_dropTargetAction->setChecked(hasDropTarget());
            const KParts::SelectorInterface::QueryMethods methods = selector->supportedQueryMethods();
            bool enable = (methods & KParts::SelectorInterface::EntireContent);
            actionCollection()->action(QL1S("show_links"))->setEnabled(enable);
            enable = (htmlExtn->hasSelection() && (methods & KParts::SelectorInterface::SelectedContent));
            actionCollection()->action(QL1S("show_selected_links"))->setEnabled(enable);
            enable = (actionCollection()->action(QL1S("show_links"))->isEnabled() || actionCollection()->action(QL1S("show_selected_links"))->isEnabled());
            actionCollection()->action(QL1S("show_drop"))->setEnabled(enable);
            return;
        }
    }

    // Check for FileInfoExtension support...
    KParts::FileInfoExtension *fileinfoExtn = KParts::FileInfoExtension::childObject(parent());
    if (fileinfoExtn) {
        m_dropTargetAction->setChecked(hasDropTarget());
        const KParts::FileInfoExtension::QueryModes modes = fileinfoExtn->supportedQueryModes();
        bool enable = (modes & KParts::FileInfoExtension::AllItems);
        actionCollection()->action(QL1S("show_links"))->setEnabled(enable);
        enable = (fileinfoExtn->hasSelection() && (modes & KParts::FileInfoExtension::SelectedItems));
        actionCollection()->action(QL1S("show_selected_links"))->setEnabled(enable);
        enable = (actionCollection()->action(QL1S("show_links"))->isEnabled() || actionCollection()->action(QL1S("show_selected_links"))->isEnabled());
        actionCollection()->action(QL1S("show_drop"))->setEnabled(enable);
        return;
    }

    actionCollection()->action(QL1S("show_selected_links"))->setEnabled(false);
    actionCollection()->action(QL1S("show_links"))->setEnabled(false);
    actionCollection()->action(QL1S("show_drop"))->setEnabled(false);
    if (m_dropTargetAction->isChecked())
        m_dropTargetAction->setChecked(false);
}

void KGetPlugin::slotShowDrop()
{
    if (!QDBusConnection::sessionBus().interface()->isServiceRegistered("org.kde.kget")) {
        const QString command = QStringLiteral("kget --showDropTarget --hideMainWindow");
        auto *job = new KIO::CommandLauncherJob(command);
        job->setDesktopName(QStringLiteral("org.kde.kget"));
        job->setUiDelegate(new KDialogJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, partWidget(parent())));
        job->start();
    } else {
        OrgKdeKgetMainInterface kgetInterface("org.kde.kget", "/KGet", QDBusConnection::sessionBus());
        kgetInterface.setDropTargetVisible(m_dropTargetAction->isChecked());
    }
}

void KGetPlugin::slotShowLinks()
{
    getLinks(false);
}

void KGetPlugin::slotShowSelectedLinks()
{
    getLinks(true);
}

void KGetPlugin::slotImportLinks()
{
    if (m_linkList.isEmpty()) {
        KMessageBox::sorry(partWidget(parent()), i18n("No downloadable links were found."), i18n("No Links"));
        return;
    }

    // Remove any duplicates links from the list...
    m_linkList.removeDuplicates();

    OrgKdeKgetMainInterface kgetInterface("org.kde.kget", "/KGet", QDBusConnection::sessionBus());
    kgetInterface.importLinks(m_linkList);
}

void KGetPlugin::getLinks(bool selectedOnly)
{
    KParts::HtmlExtension *htmlExtn = KParts::HtmlExtension::childObject(parent());
    if (htmlExtn) {
        KParts::SelectorInterface *selector = qobject_cast<KParts::SelectorInterface *>(htmlExtn);
        if (selector) {
            m_linkList.clear();
            const QUrl baseUrl = htmlExtn->baseUrl();
            const QString query = QL1S("a[href], img[src], audio[src], video[src], embed[src], object[data]");
            const KParts::SelectorInterface::QueryMethod method =
                (selectedOnly ? KParts::SelectorInterface::SelectedContent : KParts::SelectorInterface::EntireContent);
            const QList<KParts::SelectorInterface::Element> elements = selector->querySelectorAll(query, method);
            QString attr;
            Q_FOREACH (const KParts::SelectorInterface::Element &element, elements) {
                if (element.hasAttribute(QL1S("href")))
                    attr = QL1S("href");
                else if (element.hasAttribute(QL1S("src")))
                    attr = QL1S("src");
                else if (element.hasAttribute(QL1S("data")))
                    attr = QL1S("data");
                const QUrl resolvedUrl(baseUrl.resolved(QUrl(element.attribute(attr))));
                // Only select valid and non-local links for download...
                if (resolvedUrl.isValid() && !resolvedUrl.isLocalFile() && !resolvedUrl.host().isEmpty()) {
                    if (element.hasAttribute(QL1S("type")))
                        m_linkList << QString(QL1S("url ") + resolvedUrl.url() + QL1S(" type ") + element.attribute(QL1S("type")));
                    else
                        m_linkList << resolvedUrl.url();
                }
            }
        }
        slotImportLinks();
    }

    KParts::FileInfoExtension *fileinfoExtn = KParts::FileInfoExtension::childObject(parent());
    if (fileinfoExtn) {
        m_linkList.clear();
        const KParts::FileInfoExtension::QueryMode mode = (selectedOnly ? KParts::FileInfoExtension::SelectedItems : KParts::FileInfoExtension::AllItems);
        const KFileItemList items = fileinfoExtn->queryFor(mode);
        Q_FOREACH (const KFileItem &item, items) {
            const QUrl url = item.url();
            // Only select valid and non local links for download...
            if (item.isReadable() && item.isFile() && !item.isLocalFile() && !url.host().isEmpty()) {
                if (item.mimetype().isEmpty())
                    m_linkList << url.url();
                else
                    m_linkList << QString(QL1S("url ") + url.url() + QL1S(" type ") + item.mimetype());
            }
        }
        slotImportLinks();
    }
}

#include "kget_plugin.moc"
