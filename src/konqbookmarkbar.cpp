//  -*- c-basic-offset:4; indent-tabs-mode:nil -*-
/* This file is part of the KDE project
   SPDX-FileCopyrightText: 1999 Kurt Granroth <granroth@kde.org>
   SPDX-FileCopyrightText: 1998, 1999 Torben Weis <weis@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "konqbookmarkbar.h"

#include <QApplication>
#include <QDropEvent>
#include <QEvent>
#include <QMenu>

#include <kwidgetsaddons_version.h>
#include <ktoolbar.h>
#include <kactionmenu.h>
#include <kconfig.h>
#include "konqdebug.h"
#include <kconfiggroup.h>
#include <kio/global.h>
#include <kbookmarkmanager.h>

#include "konqbookmarkmenu.h"
#include "kbookmarkimporter.h"
#include "kbookmarkaction.h"
#include "kbookmarkdombuilder.h"
#include "konqpixmapprovider.h"

class KBookmarkBarPrivate
{
public:
    QList<QAction *> m_actions;
    int m_sepIndex;
    QList<int> widgetPositions; //right edge, bottom edge
    QString tempLabel;
    bool m_filteredToolbar;
    bool m_contextMenu;

    KBookmarkBarPrivate() :
        m_sepIndex(-1)
    {
        // see KBookmarkSettings::readSettings in kio
        KConfig config(QStringLiteral("kbookmarkrc"), KConfig::NoGlobals);
        KConfigGroup cg(&config, "Bookmarks");
        m_filteredToolbar = cg.readEntry("FilteredToolbar", false);
        m_contextMenu = cg.readEntry("ContextMenuActions", true);
    }
};

KBookmarkBar::KBookmarkBar(KBookmarkManager *mgr,
                           KBookmarkOwner *_owner, KToolBar *_toolBar,
                           QObject *parent)
    : QObject(parent), m_pOwner(_owner), m_toolBar(_toolBar),
      m_pManager(mgr), d(new KBookmarkBarPrivate)
{
    m_toolBar->setAcceptDrops(true);
    m_toolBar->installEventFilter(this);   // for drops

    if (d->m_contextMenu) {
        m_toolBar->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_toolBar, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextMenu(QPoint)));
    }

    connect(mgr, SIGNAL(changed(QString,QString)),
            SLOT(slotBookmarksChanged(QString)));
    connect(mgr, SIGNAL(configChanged()),
            SLOT(slotConfigChanged()));

    KBookmarkGroup toolbar = getToolbar();
    fillBookmarkBar(toolbar);
    m_toolBarSeparator = new QAction(this);
}

QString KBookmarkBar::parentAddress()
{
    if (d->m_filteredToolbar) {
        return QLatin1String("");
    } else {
        return m_pManager->toolbar().address();
    }
}

KBookmarkGroup KBookmarkBar::getToolbar()
{
    if (d->m_filteredToolbar) {
        return m_pManager->root();
    } else {
        return m_pManager->toolbar();
    }
}

KBookmarkBar::~KBookmarkBar()
{
    //clear();
    qDeleteAll(d->m_actions);
    qDeleteAll(m_lstSubMenus);
    delete d;
}

void KBookmarkBar::clear()
{
    if (m_toolBar) {
        m_toolBar->clear();
    }
    qDeleteAll(d->m_actions);
    d->m_actions.clear();
    qDeleteAll(m_lstSubMenus);
    m_lstSubMenus.clear();
}

void KBookmarkBar::slotBookmarksChanged(const QString &group)
{
    KBookmarkGroup tb = getToolbar(); // heavy for non cached toolbar version
    qCDebug(KONQUEROR_LOG) << "KBookmarkBar::slotBookmarksChanged( " << group << " )";

    if (tb.isNull()) {
        return;
    }

    if (d->m_filteredToolbar) {
        clear();
        fillBookmarkBar(tb);
    } else if (KBookmark::commonParent(group, tb.address()) == group) { // Is group a parent of tb.address?
        clear();
        fillBookmarkBar(tb);
    } else {
        // Iterate recursively into child menus
        for (QList<KBookmarkMenu *>::ConstIterator smit = m_lstSubMenus.constBegin(), smend = m_lstSubMenus.constEnd();
                smit != smend; ++smit) {
            (*smit)->slotBookmarksChanged(group);
        }
    }
}

void KBookmarkBar::slotConfigChanged()
{
    KConfig config(QStringLiteral("kbookmarkrc"), KConfig::NoGlobals);
    KConfigGroup cg(&config, "Bookmarks");
    d->m_filteredToolbar = cg.readEntry("FilteredToolbar", false);
    d->m_contextMenu = cg.readEntry("ContextMenuActions", true);
    clear();
    fillBookmarkBar(getToolbar());
}

void KBookmarkBar::fillBookmarkBar(const KBookmarkGroup &parent)
{
    if (parent.isNull()) {
        return;
    }

    for (KBookmark bm = parent.first(); !bm.isNull(); bm = parent.next(bm)) {
        // Filtered special cases
        if (d->m_filteredToolbar) {
            if (bm.isGroup() && !bm.showInToolbar()) {
                fillBookmarkBar(bm.toGroup());
            }

            if (!bm.showInToolbar()) {
                continue;
            }
        }

        if (!bm.isGroup()) {
            if (bm.isSeparator()) {
                if (m_toolBar) {
                    m_toolBar->addSeparator();
                }
            } else {
                auto host = bm.url().adjusted(QUrl::RemovePath | QUrl::RemoveQuery);
                bm.setIcon(KonqPixmapProvider::self()->iconNameFor(host));
                QAction *action = new KBookmarkAction(bm, m_pOwner, nullptr);
                if (m_toolBar) {
                    m_toolBar->addAction(action);
                }
                d->m_actions.append(action);
                    connect(KonqPixmapProvider::self(), &KonqPixmapProvider::changed, action, [host, action]() {
                        action->setIcon(KonqPixmapProvider::self()->iconForUrl(host));
                    });
                KonqPixmapProvider::self()->downloadHostIcon(host);
            }
        } else {
            KBookmarkActionMenu *action = new KBookmarkActionMenu(bm, nullptr);
#if KWIDGETSADDONS_VERSION >= QT_VERSION_CHECK(5, 77, 0)
            action->setPopupMode(QToolButton::InstantPopup);
#else
            action->setDelayed(false);
#endif
            if (m_toolBar) {
                m_toolBar->addAction(action);
            }
            d->m_actions.append(action);
            KBookmarkMenu *menu = new Konqueror::KonqBookmarkMenu(m_pManager, m_pOwner, action, bm.address());
            m_lstSubMenus.append(menu);
        }
    }
}

void KBookmarkBar::removeTempSep()
{
    if (m_toolBarSeparator) {
        m_toolBar->removeAction(m_toolBarSeparator);
    }

}

/**
 * Handle a QDragMoveEvent event on a toolbar drop
 * @return true if the event should be accepted, false if the event should be ignored
 * @param pos the current QDragMoveEvent position
 * @param the toolbar
 * @param actions the list of actions plugged into the bar
 *        returned action was dropped on
 */
bool KBookmarkBar::handleToolbarDragMoveEvent(const QPoint &p, const QList<QAction *> &actions, const QString &text)
{
    if (d->m_filteredToolbar) {
        return false;
    }
    int pos = m_toolBar->orientation() == Qt::Horizontal ? p.x() : p.y();
    Q_ASSERT(actions.isEmpty() || (m_toolBar == qobject_cast<KToolBar *>(actions.first()->associatedWidgets().value(0))));
    m_toolBar->setUpdatesEnabled(false);
    removeTempSep();

    bool foundWidget = false;
    // Right To Left
    // only relevant if the toolbar is Horizontal!
    bool rtl = QApplication::isRightToLeft() && m_toolBar->orientation() == Qt::Horizontal;
    m_toolBarSeparator->setText(text);

    // Empty toolbar
    if (actions.isEmpty()) {
        d->m_sepIndex = 0;
        m_toolBar->addAction(m_toolBarSeparator);
        m_toolBar->setUpdatesEnabled(true);
        return true;
    }

    // else find the toolbar button
    for (int i = 0; i < d->widgetPositions.count(); ++i) {
        if (rtl ^ (pos <= d->widgetPositions[i])) {
            foundWidget = true;
            d->m_sepIndex = i;
            break;
        }
    }

    QString address;

    if (foundWidget) { // found the containing button
        int leftOrTop = d->m_sepIndex == 0 ? 0 : d->widgetPositions[d->m_sepIndex - 1];
        int rightOrBottom = d->widgetPositions[d->m_sepIndex];
        if (rtl ^ (pos >= (leftOrTop + rightOrBottom) / 2)) {
            // if in second half of button then
            // we jump to next index
            d->m_sepIndex++;
        }
        if (d->m_sepIndex != actions.count()) {
            QAction *before = m_toolBar->actions()[d->m_sepIndex];
            m_toolBar->insertAction(before, m_toolBarSeparator);
        } else {
            m_toolBar->addAction(m_toolBarSeparator);
        }
        m_toolBar->setUpdatesEnabled(true);
        return true;
    } else { // (!foundWidget)
        // if !b and not past last button, we didn't find button
        if (rtl ^ (pos <= d->widgetPositions[d->widgetPositions.count() - 1])) {
            m_toolBar->setUpdatesEnabled(true);
            return false;
        } else { // location is beyond last action, assuming we want to add in the end
            d->m_sepIndex = actions.count();
            m_toolBar->addAction(m_toolBarSeparator);
            m_toolBar->setUpdatesEnabled(true);
            return true;
        }
    }
}

void KBookmarkBar::contextMenu(const QPoint &pos)
{
    KBookmarkActionInterface *action = dynamic_cast<KBookmarkActionInterface *>(m_toolBar->actionAt(pos));
    if (!action) {
        //Show default (ktoolbar) menu
        m_toolBar->setContextMenuPolicy(Qt::DefaultContextMenu);
        //Recreate event with the same position
        QContextMenuEvent evt(QContextMenuEvent::Other, pos);
        QCoreApplication::sendEvent(m_toolBar, &evt);
        //Reassign custom context menu
        m_toolBar->setContextMenuPolicy(Qt::CustomContextMenu);
    } else {
        QMenu *menu = new Konqueror::KonqBookmarkContextMenu(action->bookmark(), m_pManager, m_pOwner);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_toolBar->mapToGlobal(pos));
    }
}

// TODO    *** drop improvements ***
// open submenus on drop interactions
bool KBookmarkBar::eventFilter(QObject *, QEvent *e)
{
    if (d->m_filteredToolbar) {
        return false;    // todo: make this limit the actions
    }

    if (e->type() == QEvent::DragLeave) {
        removeTempSep();
    } else if (e->type() == QEvent::Drop) {
        removeTempSep();

        QDropEvent *dev = static_cast<QDropEvent *>(e);
        QDomDocument doc;
        QList<KBookmark> list = KBookmark::List::fromMimeData(dev->mimeData(), doc);
        if (list.isEmpty()) {
            return false;
        }
        if (list.count() > 1)
            qCWarning(KONQUEROR_LOG) << "Sorry, currently you can only drop one address "
                           "onto the bookmark bar!";
        KBookmark toInsert = list.first();

        KBookmarkGroup parentBookmark = getToolbar();

        if (d->m_sepIndex == 0) {
            KBookmark newBookmark = parentBookmark.addBookmark(toInsert.fullText(), toInsert.url(), KIO::iconNameForUrl(toInsert.url()));

            parentBookmark.moveBookmark(newBookmark, KBookmark());
            m_pManager->emitChanged(parentBookmark);
            return true;
        } else {
            KBookmark after = parentBookmark.first();

            for (int i = 0; i < d->m_sepIndex - 1; ++i) {
                after = parentBookmark.next(after);
            }
            KBookmark newBookmark = parentBookmark.addBookmark(toInsert.fullText(), toInsert.url(), KIO::iconNameForUrl(toInsert.url()));

            parentBookmark.moveBookmark(newBookmark, after);
            m_pManager->emitChanged(parentBookmark);
            return true;
        }
    } else if (e->type() == QEvent::DragMove || e->type() == QEvent::DragEnter) {
        QDragMoveEvent *dme = static_cast<QDragMoveEvent *>(e);
        if (!KBookmark::List::canDecode(dme->mimeData())) {
            return false;
        }

        //cache text, save positions (inserting the temporary widget changes the positions)
        if (e->type() == QEvent::DragEnter) {
            QDomDocument doc;
            const QList<KBookmark> list = KBookmark::List::fromMimeData(dme->mimeData(), doc);
            if (list.isEmpty()) {
                return false;
            }
            d->tempLabel  = list.first().url().toDisplayString(QUrl::PreferLocalFile);

            d->widgetPositions.clear();

            for (int i = 0; i < m_toolBar->actions().count(); ++i)
                if (QWidget *button = m_toolBar->widgetForAction(m_toolBar->actions()[i])) {
                    if (m_toolBar->orientation() == Qt::Horizontal) {
                        if (QApplication::isLeftToRight()) {
                            d->widgetPositions.push_back(button->geometry().right());
                        } else {
                            d->widgetPositions.push_back(button->geometry().left());
                        }
                    } else {
                        d->widgetPositions.push_back(button->geometry().bottom());
                    }
                }
        }

        bool accept = handleToolbarDragMoveEvent(dme->pos(), d->m_actions, d->tempLabel);
        if (accept) {
            dme->accept();
            return true; //Really?
        }
    }
    return false;
}

