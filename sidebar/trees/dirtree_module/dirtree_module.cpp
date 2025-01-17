/* This file is part of the KDE project
    SPDX-FileCopyrightText: 2000 David Faure <faure@kde.org>
    SPDX-FileCopyrightText: 2002 Michael Brade <brade@kde.org>
    SPDX-FileCopyrightText: 2003 Waldo Bastian <bastian@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "dirtree_module.h"
#include "dirtree_item.h"

#include <kconfiggroup.h>
#include <kio_version.h>
#include <kprotocolmanager.h>
#include <kdesktopfile.h>
#include <kmessagebox.h>
#include <kiconloader.h>
#include <kdirlister.h>
#include <KLocalizedString>

KonqSidebarDirTreeModule::KonqSidebarDirTreeModule(KonqSidebarTree *parentTree, bool showHidden)
    : KonqSidebarTreeModule(parentTree, showHidden), m_dirLister(0L), m_topLevelItem(0L)
{
    // SLOW! Get the KConfigGroup from the plugin.
    KConfig config("konqsidebartngrc");
    KConfigGroup generalGroup(&config, "General");
    m_showArchivesAsFolders = generalGroup.readEntry("ShowArchivesAsFolders", true);
}

KonqSidebarDirTreeModule::~KonqSidebarDirTreeModule()
{
    // KDirLister may still emit canceled while being deleted.
    if (m_dirLister) {
#if KIO_VERSION < QT_VERSION_CHECK(5, 79, 0)
        disconnect(m_dirLister, SIGNAL(canceled(QUrl)),
                   this, SLOT(slotListingStopped(QUrl)));
#else
        disconnect(m_dirLister, &KCoreDirLister::listingDirCanceled, this, &KonqSidebarDirTreeModule::slotListingStopped);
#endif
        delete m_dirLister;
    }
}

QList<QUrl> KonqSidebarDirTreeModule::selectedUrls()
{
    QList<QUrl> lst;
    KonqSidebarDirTreeItem *selection = static_cast<KonqSidebarDirTreeItem *>(m_pTree->selectedItem());
    if (!selection) {
        kError() << "no selection!" << endl;
        return lst;
    }
    lst.append(selection->fileItem().url());
    return lst;
}

void KonqSidebarDirTreeModule::addTopLevelItem(KonqSidebarTreeTopLevelItem *item)
{
    if (m_topLevelItem) { // We can handle only one at a time !
        kError() << "Impossible, we can have only one toplevel item !" << endl;
    }

    KDesktopFile cfg(item->path());
    KConfigGroup desktopGroup = cfg.desktopGroup();

    QUrl targetURL;
    targetURL.setPath(item->path());

    if (cfg.hasLinkType()) {
        targetURL = cfg.readUrl();
        // some services might want to make their URL configurable in kcontrol
        QString configured = desktopGroup.readPathEntry("X-KDE-ConfiguredURL", QString());
        if (!configured.isEmpty()) {
            QStringList list = configured.split(':');
            KConfig config(list[0]);
            KConfigGroup urlGroup(&config, list[1] != "noGroup" ? list[1] : "General");
            QString conf_url = urlGroup.readEntry(list[2], QString());
            if (!conf_url.isEmpty()) {
                targetURL = conf_url;
            }
        }
    } else if (cfg.hasDeviceType()) {
        // Determine the mountpoint
        QString mp = desktopGroup.readPathEntry("MountPoint", QString());
        if (mp.isEmpty()) {
            return;
        }

        targetURL.setPath(mp);
    } else {
        return;
    }

    bool bListable = KProtocolManager::supportsListing(targetURL);
    //qCDebug(SIDEBAR_LOG) << targetURL.toDisplayString() << " listable : " << bListable;

    if (!bListable) {
        item->setExpandable(false);
        item->setListable(false);
    }

    item->setExternalURL(targetURL);
    addSubDir(item);

    m_topLevelItem = item;
}

void KonqSidebarDirTreeModule::openTopLevelItem(KonqSidebarTreeTopLevelItem *item)
{
    if (!item->childCount() && item->isListable()) {
        openSubFolder(item);
    }
}

void KonqSidebarDirTreeModule::addSubDir(KonqSidebarTreeItem *item)
{
    QString id = item->externalURL().adjusted(QUrl::StripTrailingSlash).toString();
    qCDebug(SIDEBAR_LOG) << this << id;
    m_dictSubDirs.insert(id, item);

    KonqSidebarDirTreeItem *ditem = dynamic_cast<KonqSidebarDirTreeItem *>(item);
    if (ditem) {
        m_ptrdictSubDirs.insert(ditem->fileItem(), item);
    }
}

// Remove <key, item> from dict, taking into account that there maybe
// other items with the same key.
static void remove(Q3Dict<KonqSidebarTreeItem> &dict, const QString &key, KonqSidebarTreeItem *item)
{
    Q3PtrList<KonqSidebarTreeItem> *otherItems = 0;
    while (true) {
        KonqSidebarTreeItem *takeItem = dict.take(key);
        if (!takeItem || (takeItem == item)) {
            if (!otherItems) {
                return;
            }

            // Insert the otherItems back in
            for (KonqSidebarTreeItem * otherItem; (otherItem = otherItems->take(0));) {
                dict.insert(key, otherItem);
            }
            delete otherItems;
            return;
        }
        // Not the item we are looking for
        if (!otherItems) {
            otherItems = new Q3PtrList<KonqSidebarTreeItem>();
        }

        otherItems->prepend(takeItem);
    }
}

// Looks up key in dict and returns it in item, if there are multiple items
// with the same key, additional items are returned in itemList which should
// be deleted by the caller.
static void lookupItems(Q3Dict<KonqSidebarTreeItem> &dict, const QString &key, KonqSidebarTreeItem *&item, Q3PtrList<KonqSidebarTreeItem> *&itemList)
{
    itemList = 0;
    item = dict.take(key);
    if (!item) {
        return;
    }

    while (true) {
        KonqSidebarTreeItem *takeItem = dict.take(key);
        if (!takeItem) {
            //
            // Insert itemList back in
            if (itemList) {
                for (KonqSidebarTreeItem *otherItem = itemList->first(); otherItem; otherItem = itemList->next()) {
                    dict.insert(key, otherItem);
                }
            }
            dict.insert(key, item);
            return;
        }
        if (!itemList) {
            itemList = new Q3PtrList<KonqSidebarTreeItem>();
        }

        itemList->prepend(takeItem);
    }
}

// Remove <key, item> from dict, taking into account that there maybe
// other items with the same key.
static void remove(QHash<KFileItem, KonqSidebarTreeItem *> &dict, const KFileItem &key, KonqSidebarTreeItem *item)
{
    Q3PtrList<KonqSidebarTreeItem> *otherItems = 0;
    while (true) {
        KonqSidebarTreeItem *takeItem = dict.take(key);
        if (!takeItem || (takeItem == item)) {
            if (!otherItems) {
                return;
            }

            // Insert the otherItems back in
            for (KonqSidebarTreeItem * otherItem; (otherItem = otherItems->take(0));) {
                dict.insert(key, otherItem);
            }
            delete otherItems;
            return;
        }
        // Not the item we are looking for
        if (!otherItems) {
            otherItems = new Q3PtrList<KonqSidebarTreeItem>();
        }

        otherItems->prepend(takeItem);
    }
}

// Looks up key in dict and returns it in item, if there are multiple items
// with the same key, additional items are returned in itemList which should
// be deleted by the caller.
static void lookupItems(QHash<KFileItem, KonqSidebarTreeItem *> &dict, const KFileItem &key, KonqSidebarTreeItem *&item, Q3PtrList<KonqSidebarTreeItem> *&itemList)
{
    itemList = 0;
    item = dict.take(key);
    if (!item) {
        return;
    }

    while (true) {
        KonqSidebarTreeItem *takeItem = dict.take(key);
        if (!takeItem) {
            //
            // Insert itemList back in
            if (itemList) {
                for (KonqSidebarTreeItem *otherItem = itemList->first(); otherItem; otherItem = itemList->next()) {
                    dict.insert(key, otherItem);
                }
            }
            dict.insert(key, item);
            return;
        }
        if (!itemList) {
            itemList = new Q3PtrList<KonqSidebarTreeItem>();
        }

        itemList->prepend(takeItem);
    }
}

void KonqSidebarDirTreeModule::removeSubDir(KonqSidebarTreeItem *item, bool childrenOnly)
{
    qCDebug(SIDEBAR_LOG) << this << "item=" << item;
    if (item->firstChild()) {
        KonqSidebarTreeItem *it = static_cast<KonqSidebarTreeItem *>(item->firstChild());
        KonqSidebarTreeItem *next = 0L;
        while (it) {
            next = static_cast<KonqSidebarTreeItem *>(it->nextSibling());
            removeSubDir(it);
            delete it;
            it = next;
        }
    }

    if (!childrenOnly) {
        QString id = item->externalURL().adjusted(QUrl::StripTrailingSlash).toString();
        remove(m_dictSubDirs, id, item);
        while (!(item->alias.isEmpty())) {
            remove(m_dictSubDirs, item->alias.front(), item);
            item->alias.pop_front();
        }

        KonqSidebarDirTreeItem *ditem = dynamic_cast<KonqSidebarDirTreeItem *>(item);
        if (ditem) {
            remove(m_ptrdictSubDirs, ditem->fileItem(), item);
        }
    }
}

void KonqSidebarDirTreeModule::openSubFolder(KonqSidebarTreeItem *item)
{
    qCDebug(SIDEBAR_LOG) << this << "openSubFolder(" << item->externalURL().prettyUrl() << ")";

    if (!m_dirLister) { // created on demand
        m_dirLister = new KDirLister();
        //m_dirLister->setDelayedMimeTypes( true ); // this was set, but it's wrong, without a KMimeTypeResolver...
        //m_dirLister->setDirOnlyMode( true );
//  QStringList mimetypes;
//  mimetypes<<QString("inode/directory");
//  m_dirLister->setMimeFilter(mimetypes);

        connect(m_dirLister, SIGNAL(newItems(KFileItemList)),
                this, SLOT(slotNewItems(KFileItemList)));
        connect(m_dirLister, SIGNAL(refreshItems(QList<QPair<KFileItem,KFileItem> >)),
                this, SLOT(slotRefreshItems(QList<QPair<KFileItem,KFileItem> >)));
        connect(m_dirLister, SIGNAL(deleteItem(KFileItem)),
                this, SLOT(slotDeleteItem(KFileItem)));

#if KIO_VERSION < QT_VERSION_CHECK(5, 79, 0)
        connect(m_dirLister, SIGNAL(completed(QUrl)),
                this, SLOT(slotListingStopped(QUrl)));
        connect(m_dirLister, SIGNAL(canceled(QUrl)),
                this, SLOT(slotListingStopped(QUrl)));
#else
        connect(m_dirLister, &KCoreDirLister::listingDirCompleted, this, &KonqSidebarDirTreeModule::slotListingStopped);
        connect(m_dirLister, &KCoreDirLister::listingDirCanceled, this, &KonqSidebarDirTreeModule::slotListingStopped);
#endif

        connect(m_dirLister, SIGNAL(redirection(QUrl,QUrl)),
                this, SLOT(slotRedirection(QUrl,QUrl)));
    }

    if (!item->isTopLevelItem() &&
            static_cast<KonqSidebarDirTreeItem *>(item)->hasStandardIcon()) {
        int size = KIconLoader::global()->currentSize(KIconLoader::Small);
        QPixmap pix = DesktopIcon("folder-open", size);
        m_pTree->startAnimation(item, "kde", 6, &pix);
    } else {
        m_pTree->startAnimation(item);
    }

    listDirectory(item);
}

void KonqSidebarDirTreeModule::listDirectory(KonqSidebarTreeItem *item)
{
    // This causes a reparsing, but gets rid of the trailing slash
    QString strUrl = item->externalURL().adjusted(QUrl::StripTrailingSlash).toString();
    QUrl url(strUrl);

    Q3PtrList<KonqSidebarTreeItem> *itemList;
    KonqSidebarTreeItem *openItem;
    lookupItems(m_dictSubDirs, strUrl, openItem, itemList);

    while (openItem) {
        if (openItem->childCount()) {
            break;
        }

        openItem = itemList ? itemList->take(0) : 0;
    }
    delete itemList;

    if (openItem) {
        // We have this directory listed already, just copy the entries as we
        // can't use the dirlister, it would invalidate the old entries
        int size = KIconLoader::global()->currentSize(KIconLoader::Small);
        KonqSidebarTreeItem *parentItem = item;
        KonqSidebarDirTreeItem *oldItem = static_cast<KonqSidebarDirTreeItem *>(openItem->firstChild());
        while (oldItem) {
            const KFileItem fileItem = oldItem->fileItem();
            if (! fileItem.isDir()) {
                if (!fileItem.url().isLocalFile()) {
                    continue;
                }
                KMimeType::Ptr ptr = fileItem.determineMimeType();
                if (ptr && (ptr->is("inode/directory") || m_showArchivesAsFolders)
                        && ((!ptr->property("X-KDE-LocalProtocol").toString().isEmpty()))) {
                    qCDebug(SIDEBAR_LOG) << "Something not really a directory";
                } else {
//                kError() << "Item " << fileItem->url().prettyUrl() << " is not a directory!" << endl;
                    continue;
                }
            }

            KonqSidebarDirTreeItem *dirTreeItem = new KonqSidebarDirTreeItem(parentItem, m_topLevelItem, fileItem);
            dirTreeItem->setPixmap(0, fileItem.pixmap(size));
            dirTreeItem->setText(0, KIO::decodeFileName(fileItem.name()));

            oldItem = static_cast<KonqSidebarDirTreeItem *>(oldItem->nextSibling());
        }
        m_pTree->stopAnimation(item);

        return;
    }

    m_dirLister->setShowingDotFiles(showHidden());

    if (tree()->isOpeningFirstChild()) {
        m_dirLister->setAutoErrorHandlingEnabled(false, 0);
    } else {
        m_dirLister->setAutoErrorHandlingEnabled(true, tree());
    }

    m_dirLister->openUrl(url, KDirLister::Keep);
}

void KonqSidebarDirTreeModule::slotNewItems(const KFileItemList &entries)
{
    qCDebug(SIDEBAR_LOG) << this << entries.count();

    Q_ASSERT(entries.count());
    const KFileItem firstItem = entries.first();

    // Find parent item - it's the same for all the items
    QUrl dir(firstItem.url().adjusted(QUrl::StripTrailingSlash).toString());
    dir = dir.adjusted(QUrl::RemoveFilename);
    dir.setPath(dir.path() + "");
    qCDebug(SIDEBAR_LOG) << this << "dir=" << dir.adjusted(QUrl::StripTrailingSlash).toString();

    Q3PtrList<KonqSidebarTreeItem> *parentItemList;
    KonqSidebarTreeItem *parentItem;
    lookupItems(m_dictSubDirs, dir.adjusted(QUrl::StripTrailingSlash).toString(), parentItem, parentItemList);

    if (!parentItem) {   // hack for dnssd://domain/type/service listed in dnssd:/type/ dir
        dir.setHost(QString());
        lookupItems(m_dictSubDirs, dir.adjusted(QUrl::StripTrailingSlash).toString(), parentItem, parentItemList);
    }

    if (!parentItem) {
        KMessageBox::error(tree(), i18n("Cannot find parent item %1 in the tree. Internal error.", dir.adjusted(QUrl::StripTrailingSlash).toString()));
        return;
    }

    qCDebug(SIDEBAR_LOG) << "number of additional parent items:" << (parentItemList ? parentItemList->count() : 0);
    int size = KIconLoader::global()->currentSize(KIconLoader::Small);
    do {
        qCDebug(SIDEBAR_LOG) << "Parent Item URL:" << parentItem->externalURL();
        KFileItemList::const_iterator kit = entries.begin();
        const KFileItemList::const_iterator kend = entries.end();
        for (; kit != kend; ++kit) {
            const KFileItem fileItem = *kit;

            if (! fileItem.isDir()) {
                if (!fileItem.url().isLocalFile()) {
                    continue;
                }
                KMimeType::Ptr ptr = fileItem.determineMimeType();

                if (ptr && (ptr->is("inode/directory") || m_showArchivesAsFolders)
                        && ((!ptr->property("X-KDE-LocalProtocol").toString().isEmpty()))) {
                    qCDebug(SIDEBAR_LOG) << "Something really a directory";
                } else {
                    //kError() << "Item " << fileItem->url().prettyUrl() << " is not a directory!" << endl;
                    continue;
                }
            }

            KonqSidebarDirTreeItem *dirTreeItem = new KonqSidebarDirTreeItem(parentItem, m_topLevelItem, fileItem);
            dirTreeItem->setPixmap(0, fileItem.pixmap(size));
            dirTreeItem->setText(0, KIO::decodeFileName(fileItem.name()));
        }

    } while ((parentItem = parentItemList ? parentItemList->take(0) : 0));
    delete parentItemList;
}

void KonqSidebarDirTreeModule::slotRefreshItems(const QList<QPair<KFileItem, KFileItem> > &entries)
{
    int size = KIconLoader::global()->currentSize(KIconLoader::Small);

    qCDebug(SIDEBAR_LOG) << "# of items to refresh:" << entries.count();

    for (int i = 0; i < entries.count(); ++i) {
        const KFileItem fileItem(entries.at(i).second);
        const KFileItem oldFileItem(entries.at(i).first);

        Q3PtrList<KonqSidebarTreeItem> *itemList;
        KonqSidebarTreeItem *item;
        lookupItems(m_ptrdictSubDirs, oldFileItem, item, itemList);

        if (!item) {
            kWarning(1201) << "can't find old entry for " << oldFileItem.url().adjusted(QUrl::StripTrailingSlash).toString();
            continue;
        }

        do {
            if (item->isTopLevelItem()) { // we only have dirs and one toplevel item in the dict
                kWarning(1201) << "entry for " << oldFileItem.url().adjusted(QUrl::StripTrailingSlash).toString() << "matches against toplevel.";
                break;
            }

            KonqSidebarDirTreeItem *dirTreeItem = static_cast<KonqSidebarDirTreeItem *>(item);
            // Item renamed ?
            if (dirTreeItem->id != fileItem.url().adjusted(QUrl::StripTrailingSlash).toString()) {
                qCDebug(SIDEBAR_LOG) << "renaming" << oldFileItem << "->" << fileItem;
                // We need to update the URL in m_dictSubDirs, and to get rid of the child items, so remove and add.
                // Then remove + delete
                removeSubDir(dirTreeItem, true /*children only*/);
                remove(m_dictSubDirs, dirTreeItem->id, dirTreeItem);
                remove(m_ptrdictSubDirs, oldFileItem, dirTreeItem);

                dirTreeItem->reset(); // Reset id
                dirTreeItem->setPixmap(0, fileItem.pixmap(size));
                dirTreeItem->setText(0, KIO::decodeFileName(fileItem.name()));

                // Make sure the item doesn't get inserted twice!
                // dirTreeItem->id points to the new name
                remove(m_dictSubDirs, dirTreeItem->id, dirTreeItem);
                remove(m_ptrdictSubDirs, fileItem, dirTreeItem);
                m_dictSubDirs.insert(dirTreeItem->id, dirTreeItem);
                m_ptrdictSubDirs.insert(fileItem, dirTreeItem);
            } else {
                dirTreeItem->setPixmap(0, fileItem.pixmap(size));
                dirTreeItem->setText(0, KIO::decodeFileName(fileItem.name()));
            }

        } while ((item = itemList ? itemList->take(0) : 0));
        delete itemList;
    }
}

void KonqSidebarDirTreeModule::slotDeleteItem(const KFileItem &fileItem)
{
    qCDebug(SIDEBAR_LOG) << fileItem.url().adjusted(QUrl::StripTrailingSlash).toString();

    // All items are in m_ptrdictSubDirs, so look it up fast
    Q3PtrList<KonqSidebarTreeItem> *itemList;
    KonqSidebarTreeItem *item;
    lookupItems(m_dictSubDirs, fileItem.url().adjusted(QUrl::StripTrailingSlash).toString(), item, itemList);
    while (item) {
        removeSubDir(item);
        delete item;

        item = itemList ? itemList->take(0) : 0;
    }
    delete itemList;
}

void KonqSidebarDirTreeModule::slotRedirection(const QUrl &oldUrl, const QUrl &newUrl)
{
    qCDebug(SIDEBAR_LOG) << newUrl;

    QString oldUrlStr = oldUrl.adjusted(QUrl::StripTrailingSlash).toString();
    QString newUrlStr = newUrl.adjusted(QUrl::StripTrailingSlash).toString();

    Q3PtrList<KonqSidebarTreeItem> *itemList;
    KonqSidebarTreeItem *item;
    lookupItems(m_dictSubDirs, oldUrlStr, item, itemList);

    if (!item) {
        kWarning(1201) << "NOT FOUND   oldUrl=" << oldUrlStr;
        return;
    }

    do {
        if (item->alias.contains(newUrlStr)) {
            continue;
        }
        qCDebug(SIDEBAR_LOG) << "Redirectiong element";
        // We need to update the URL in m_dictSubDirs
        m_dictSubDirs.insert(newUrlStr, item);
        item->alias << newUrlStr;

        qCDebug(SIDEBAR_LOG) << "Updating url of " << item << " to " << newUrlStr;

    } while ((item = itemList ? itemList->take(0) : 0));
    delete itemList;
}

void KonqSidebarDirTreeModule::slotListingStopped(const QUrl &url)
{
    //qCDebug(SIDEBAR_LOG) << url;

    Q3PtrList<KonqSidebarTreeItem> *itemList;
    KonqSidebarTreeItem *item;
    lookupItems(m_dictSubDirs, url.adjusted(QUrl::StripTrailingSlash).toString(), item, itemList);

    while (item) {
        if (item->childCount() == 0) {
            item->setExpandable(false);
            item->repaint();
        }
        m_pTree->stopAnimation(item);

        item = itemList ? itemList->take(0) : 0;
    }
    delete itemList;

    //qCDebug(SIDEBAR_LOG) << "m_selectAfterOpening " << m_selectAfterOpening.prettyUrl();
    if (!m_selectAfterOpening.isEmpty() && url.isParentOf(m_selectAfterOpening)) {
        QUrl theURL(m_selectAfterOpening);
        m_selectAfterOpening = QUrl();
        followURL(theURL);
    }
}

void KonqSidebarDirTreeModule::followURL(const QUrl &url)
{
    // Check if we already know this URL
    KonqSidebarTreeItem *item = m_dictSubDirs[ url.adjusted(QUrl::StripTrailingSlash).toString() ];
    if (item) { // found it  -> ensure visible, select, return.
        m_pTree->ensureItemVisible(item);
        m_pTree->setSelected(item, true);
        return;
    }

    QUrl uParent(url);
    KonqSidebarTreeItem *parentItem = 0L;
    // Go up to the first known parent
    do {
        uParent = uParent.upUrl();
        parentItem = m_dictSubDirs[ uParent.adjusted(QUrl::StripTrailingSlash).toString() ];
    } while (!parentItem && !uParent.path().isEmpty() && uParent.path() != "/");

    // Not found !?!
    if (!parentItem) {
        qCDebug(SIDEBAR_LOG) << "No parent found for url " << url.toDisplayString();
        return;
    }
    qCDebug(SIDEBAR_LOG) << "Found parent " << uParent.toDisplayString();

    // That's the parent directory we found. Open if not open...
    if (!parentItem->isOpen()) {
        parentItem->setOpen(true);
        if (parentItem->childCount() && m_dictSubDirs[ url.adjusted(QUrl::StripTrailingSlash).toString() ]) {
            // Immediate opening, if the dir was already listed
            followURL(url);   // equivalent to a goto-beginning-of-method
        } else {
            m_selectAfterOpening = url;
            //qCDebug(SIDEBAR_LOG) << "m_selectAfterOpening=" << m_selectAfterOpening.url();
        }
    }
}

extern "C"
{
    KDE_EXPORT KonqSidebarTreeModule *create_konq_sidebartree_dirtree(KonqSidebarTree *par, const bool showHidden)
    {
        return new KonqSidebarDirTreeModule(par, showHidden);
    }
}

