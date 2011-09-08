/***************************************************************************
 *   Copyright (C) 2009 by Peter Penz <peter.penz19@gmail.com>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "versioncontrolobserver.h"

#include "dolphin_versioncontrolsettings.h"

#include <KDirLister>
#include <KLocale>
#include <KService>
#include <KServiceTypeTrader>
#include <kitemviews/kfileitemmodel.h>
#include <kversioncontrolplugin.h>

#include "pendingthreadsmaintainer.h"
#include "updateitemstatesthread.h"

#include <QMutexLocker>
#include <QTimer>

VersionControlObserver::VersionControlObserver(QObject* parent) :
    QObject(parent),
    m_pendingItemStatesUpdate(false),
    m_versionedDirectory(false),
    m_silentUpdate(false),
    m_model(0),
    m_dirVerificationTimer(0),
    m_plugin(0),
    m_updateItemStatesThread(0)
{
    // The verification timer specifies the timeout until the shown directory
    // is checked whether it is versioned. Per default it is assumed that users
    // don't iterate through versioned directories and a high timeout is used
    // The timeout will be decreased as soon as a versioned directory has been
    // found (see verifyDirectory()).
    m_dirVerificationTimer = new QTimer(this);
    m_dirVerificationTimer->setSingleShot(true);
    m_dirVerificationTimer->setInterval(500);
    connect(m_dirVerificationTimer, SIGNAL(timeout()),
            this, SLOT(verifyDirectory()));
}

VersionControlObserver::~VersionControlObserver()
{
    if (m_updateItemStatesThread) {
        if (m_updateItemStatesThread->isFinished()) {
            delete m_updateItemStatesThread;
            m_updateItemStatesThread = 0;
        } else {
            // The version controller gets deleted, while a thread still
            // is working to get the version information. To avoid a blocking
            // user interface, the thread will be forwarded to the
            // PendingThreadsMaintainer, which will delete the thread later.
            disconnect(m_updateItemStatesThread, SIGNAL(finished()),
                       this, SLOT(slotThreadFinished()));
            PendingThreadsMaintainer::instance().append(m_updateItemStatesThread);
            m_updateItemStatesThread = 0;
        }
    }

    if (m_plugin) {
        m_plugin->disconnect();
        m_plugin = 0;
    }
}

void VersionControlObserver::setModel(KFileItemModel* model)
{
    if (m_model) {
        disconnect(m_model, SIGNAL(itemsInserted(KItemRangeList)),
                   this, SLOT(delayedDirectoryVerification()));
    }

    m_model = model;

    if (model) {
        connect(m_model, SIGNAL(itemsInserted(KItemRangeList)),
                this, SLOT(delayedDirectoryVerification()));
    }
}

KFileItemModel* VersionControlObserver::model() const
{
    return m_model;
}

QList<QAction*> VersionControlObserver::contextMenuActions(const KFileItemList& items) const
{
    QList<QAction*> actions;
    if (isVersioned() && m_updateItemStatesThread->lockPlugin()) {
        actions = m_plugin->contextMenuActions(items);
        m_updateItemStatesThread->unlockPlugin();
    }
    return actions;
}

QList<QAction*> VersionControlObserver::contextMenuActions(const QString& directory) const
{
    QList<QAction*> actions;
    if (isVersioned() && m_updateItemStatesThread->lockPlugin()) {
        actions = m_plugin->contextMenuActions(directory);
        m_updateItemStatesThread->unlockPlugin();
    }

    return actions;
}

void VersionControlObserver::delayedDirectoryVerification()
{
    m_silentUpdate = false;
    m_dirVerificationTimer->start();
}

void VersionControlObserver::silentDirectoryVerification()
{
    m_silentUpdate = true;
    m_dirVerificationTimer->start();
}

void VersionControlObserver::verifyDirectory()
{
    if (!m_model) {
        return;
    }

    const KUrl versionControlUrl = m_model->rootDirectory();
    if (!versionControlUrl.isLocalFile()) {
        return;
    }

    if (m_plugin) {
        m_plugin->disconnect();
    }

    m_plugin = searchPlugin(versionControlUrl);
    if (m_plugin) {
        connect(m_plugin, SIGNAL(versionStatesChanged()),
                this, SLOT(silentDirectoryVerification()));
        connect(m_plugin, SIGNAL(infoMessage(QString)),
                this, SIGNAL(infoMessage(QString)));
        connect(m_plugin, SIGNAL(errorMessage(QString)),
                this, SIGNAL(errorMessage(QString)));
        connect(m_plugin, SIGNAL(operationCompletedMessage(QString)),
                this, SIGNAL(operationCompletedMessage(QString)));

        if (!m_versionedDirectory) {
            m_versionedDirectory = true;

            // The directory is versioned. Assume that the user will further browse through
            // versioned directories and decrease the verification timer.
            m_dirVerificationTimer->setInterval(100);
            connect(m_model, SIGNAL(itemsInserted(KItemRangeList)),
                   this, SLOT(delayedDirectoryVerification()));
        }
        updateItemStates();
    } else if (m_versionedDirectory) {
        m_versionedDirectory = false;

        // The directory is not versioned. Reset the verification timer to a higher
        // value, so that browsing through non-versioned directories is not slown down
        // by an immediate verification.
        m_dirVerificationTimer->setInterval(500);
        disconnect(m_model, SIGNAL(itemsInserted(KItemRangeList)),
                   this, SLOT(delayedDirectoryVerification()));
    }
}

void VersionControlObserver::slotThreadFinished()
{
    if (!m_plugin) {
        return;
    }

    if (!m_updateItemStatesThread->retrievedItems()) {
        // Ignore m_silentUpdate for an error message
        emit errorMessage(i18nc("@info:status", "Update of version information failed."));
        return;
    }

    const QList<ItemState> itemStates = m_updateItemStatesThread->itemStates();
    foreach (const ItemState& itemState, itemStates) {
        QHash<QByteArray, QVariant> values;
        values.insert("version", QVariant(static_cast<int>(itemState.version)));
        m_model->setData(itemState.index, values);
    }

    if (!m_silentUpdate) {
        // Using an empty message results in clearing the previously shown information message and showing
        // the default status bar information. This is useful as the user already gets feedback that the
        // operation has been completed because of the icon emblems.
        emit operationCompletedMessage(QString());
    }

    if (m_pendingItemStatesUpdate) {
        m_pendingItemStatesUpdate = false;
        updateItemStates();
    }
}

void VersionControlObserver::updateItemStates()
{
    Q_ASSERT(m_plugin);
    if (!m_updateItemStatesThread) {
        m_updateItemStatesThread = new UpdateItemStatesThread();
        connect(m_updateItemStatesThread, SIGNAL(finished()),
                this, SLOT(slotThreadFinished()));
    }
    if (m_updateItemStatesThread->isRunning()) {
        // An update is currently ongoing. Wait until the thread has finished
        // the update (see slotThreadFinished()).
        m_pendingItemStatesUpdate = true;
        return;
    }

    QList<ItemState> itemStates;
    //addDirectory(QModelIndex(), itemStates);
    if (!itemStates.isEmpty()) {
        if (!m_silentUpdate) {
            emit infoMessage(i18nc("@info:status", "Updating version information..."));
        }
        m_updateItemStatesThread->setData(m_plugin, itemStates);
        m_updateItemStatesThread->start(); // slotThreadFinished() is called when finished
    }
}

/*void VersionControlObserver::addDirectory(const QModelIndex& parentIndex, QList<ItemState>& itemStates)
{
    Q_UNUSED(parentIndex);
    Q_UNUSED(itemStates);
    const int rowCount = m_dolphinModel->rowCount(parentIndex);
    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex index = m_dolphinModel->index(row, DolphinModel::Version, parentIndex);
        addDirectory(index, itemStates);

        ItemState itemState;
        itemState.index = index;
        itemState.item = m_dolphinModel->itemForIndex(index);
        itemState.version = KVersionControlPlugin::UnversionedVersion;

        itemStates.append(itemState);
    }
}*/

KVersionControlPlugin* VersionControlObserver::searchPlugin(const KUrl& directory) const
{
    static bool pluginsAvailable = true;
    static QList<KVersionControlPlugin*> plugins;

    if (!pluginsAvailable) {
        // A searching for plugins has already been done, but no
        // plugins are installed
        return 0;
    }

    if (plugins.isEmpty()) {
        // No searching for plugins has been done yet. Query the KServiceTypeTrader for
        // all fileview version control plugins and remember them in 'plugins'.
        const QStringList enabledPlugins = VersionControlSettings::enabledPlugins();

        const KService::List pluginServices = KServiceTypeTrader::self()->query("FileViewVersionControlPlugin");
        for (KService::List::ConstIterator it = pluginServices.constBegin(); it != pluginServices.constEnd(); ++it) {
            if (enabledPlugins.contains((*it)->name())) {
                KVersionControlPlugin* plugin = (*it)->createInstance<KVersionControlPlugin>();
                if (plugin) {
                    plugins.append(plugin);
                }
            }
        }
        if (plugins.isEmpty()) {
            pluginsAvailable = false;
            return 0;
        }
    }

    // Verify whether the current directory contains revision information
    // like .svn, .git, ...
    Q_UNUSED(directory);
    foreach (KVersionControlPlugin* plugin, plugins) {
        // Use the KDirLister cache to check for .svn, .git, ... files
        KUrl dirUrl(directory);
        KUrl fileUrl = dirUrl;
        fileUrl.addPath(plugin->fileName());
        const KFileItem item; // = m_dirLister->findByUrl(fileUrl);
        if (!item.isNull()) {
            return plugin;
        }

        // Version control systems like Git provide the version information
        // file only in the root directory. Check whether the version information file can
        // be found in one of the parent directories. For performance reasons this
        // step is only done, if the previous directory was marked as versioned by
        // m_versionedDirectory. Drawback: Until e. g. Git is recognized, the root directory
        // must be shown at least once.
        if (m_versionedDirectory) {
            KUrl upUrl = dirUrl.upUrl();
            while (upUrl != dirUrl) {
                const QString filePath = dirUrl.pathOrUrl(KUrl::AddTrailingSlash) + plugin->fileName();
                QFileInfo file(filePath);
                if (file.exists()) {
                    return plugin;
                }
                dirUrl = upUrl;
                upUrl = dirUrl.upUrl();
            }
        }
    }

    return 0;
}

bool VersionControlObserver::isVersioned() const
{
    return false; //m_dolphinModel->hasVersionData() && m_plugin;
}

#include "versioncontrolobserver.moc"