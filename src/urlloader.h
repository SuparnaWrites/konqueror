/*
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Stefano Crocco <stefano.crocco@alice.it>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#ifndef URLLLOADER_H
#define URLLLOADER_H

#include "konqopenurlrequest.h"

#include <QObject>
#include <QUrl>

#include <KService>
#include <KParts/BrowserOpenOrSaveQuestion>

namespace KParts {
    class ReadOnlyPart;
};

class KJob;
namespace KIO {
    class OpenUrlJob;
    class ApplicationLauncherJob;
}

class KonqMainWindow;
class KonqView;

/**
 * @brief Class which takes care of finding out what to do with an URL and carries out the chosen action
 *
 * Depending on whether the mimetype of the URL is already known and on whether the URL is a local or remote
 * file, this class can work in a synchronous or asynchronous way. This should be mostly transparent to the user.
 *
 * This class is meant to be used in the following way:
 * - create an instance, passing it the known information about the URL to load
 * - connect to the finished() signal to be notified when the URL has been loaded
 * - call start(): this will attempt to determine the synchronously determine mimetype and, if successful, will
 * decide what to do with it
 * - call viewToUse() to find out where the URL should be opened. If needed, create a new view and call setView()
 * passing the new view
 * - call goOn(): this will asynchronously determine the mimetype and the action to carry out, if not already done,
 * and perform the action itself
 */
class UrlLoader : public QObject
{
    Q_OBJECT

public:
    /**
     * Constructor
     *
     * @param mainWindow the KonqMainWindow which asked to load the URL
     * @param view the view which asked to open the URL. It can be `nullptr`
     * @param url the URL to load
     * @param mimeType the mimetype of the URL or an empty string if not known
     * @param req the object containing information about the URL loading request
     * @param trustedSource whether the source of the URL is trusted
     * @param forceOpen tells never to embed the URL
     */
    UrlLoader(KonqMainWindow *mainWindow, KonqView *view, const QUrl &url, const QString &mimeType, const KonqOpenURLRequest &req, bool trustedSource, bool forceOpen=false);
    ~UrlLoader();

    /** @brief Enum describing the possible actions to be taken*/
    enum class OpenUrlAction{
        UnknwonAction, /**< The action hasn't been decided yet */
        DoNothing, /**< No action should be taken */
        Save, /**< Save the URL */
        Embed, /**< Display the URL in an embedded viewer */
        Open, /**< Display the URL in a separate viewer*/
        Execute /**< Execute the URL */
    };

    /** @brief Enum describing the view to use to embed an URL*/
    enum class ViewToUse{
        View, /**< Use the view passed as argument to the constructor */
        CurrentView, /**< Use the current view */
        NewTab /**< Create a new tab and use its view */
    };

    /**
     * @brief Determines what to do with the URL if its mimetype can be determined without using an `OpenUrlJob`.
     *
     * When the mimetype can be determined without using an `OpenUrlJob`, this function calls decideAction() to
     * determine what should be done with the URL. In this case, subsequent calls to isReady() will return `true`.
     * If the mimetype can't be determined without using an `OpenUrlJob`, calls to isReady() will return `false`,
     * because `OpenUrlJob` works asynchronously.
     *
     * The mimetype can be determined without using an `OpenUrlJob` in the following situations:
     * - a mimetype different from `application/octet-stream` is passed to the constructor
     * - the URL is a local file
     * - the URL scheme is `http` and the URL hasn't yet been processed by the default HTML engine (in this case,
     * a fake `text/html` mimetype will be used and the HTML engine will take care of determining the mimetype)
     *
     * @note This function *doesn't* create or start the `OpenUrlJob`, even if it will be needed.
     */
    void start();

    /**
     * @brief Performs the required action on the URL, using an `OpenUrlJob` to determine its mimetype if needed.
     *
     * If start() had been able to determine the action to carry out, this function simply calls performAction()
     * to perform the chosen action. In all other cases (that is, if the mimetype is still unknown), it launches
     * an `OpenUrlJob` to determine the mimetype. When the job has determined the mimetype, this function will
     * call decideAction() to decide what to do with the URL and then call performAction() to carry out the chosen
     * action.
     */
    void goOn();

    /**
     * @brief Carries out the requested action
     */
    void performAction();

    void abort();

    QString mimeType() const;
    bool isReady() const {return m_ready;}
    ViewToUse viewToUse() const;
    QUrl url() const {return m_url;}
    KonqOpenURLRequest request() const {return m_request;}
    KonqView* view() const {return m_view;}
    void setView(KonqView *view);
    bool isAsync() const {return m_isAsync;}
    void setOldLocationBarUrl(const QString &old);
    QString oldLocationBarUrl() const {return m_oldLocationBarUrl;}
    bool hasError() const {return m_jobHadError;}
    void setNewTab(bool newTab);
    static bool isExecutable(const QString &mimeType);
    QString suggestedFileName() const {return m_request.suggestedFileName;}

    static QString partForLocalFile(const QString &path);

signals:
    void finished(UrlLoader *self);

private slots:

    void mimetypeDeterminedByJob(const QString &mimeType);
    void jobFinished(KJob* job);
    void done(KJob *job=nullptr);

private:

    void embed();
    void open();
    void execute();
    void save();

    bool shouldEmbedThis() const;
    void saveUrlUsingKIO(const QUrl &orig, const QUrl &dest);
    void detectSettingsForLocalFiles();
    void detectSettingsForRemoteFiles();
    void launchOpenUrlJob(bool pauseOnMimeTypeDetermined);
    static bool isTextExecutable(const QString &mimeType);
    void openExternally();
    void killOpenUrlJob();
    static bool serviceIsKonqueror(KService::Ptr service);
    static bool isMimeTypeKnown(const QString &mimeType);
    bool decideEmbedOrSave();
    void decideOpenOrSave();
    bool embedWithoutAskingToSave(const QString &mimeType);
    bool shouldUseDefaultHttpMimeype() const;
    void decideAction();
    bool isViewLocked() const;

    typedef QPair<OpenUrlAction, KService::Ptr> OpenSaveAnswer;

    enum class OpenEmbedMode{Open, Embed};
    OpenSaveAnswer askSaveOrOpen(OpenEmbedMode mode) const;
    OpenUrlAction decideExecute() const;

private:
    QPointer<KonqMainWindow> m_mainWindow;
    QUrl m_url;
    QString m_mimeType;
    KonqOpenURLRequest m_request;
    KonqView *m_view;
    bool m_trustedSource;
    bool m_dontEmbed;
    bool m_ready = false;
    bool m_isAsync = false;
    OpenUrlAction m_action = OpenUrlAction::UnknwonAction;
    KService::Ptr m_service;
    QPointer<KIO::OpenUrlJob> m_openUrlJob;
    QPointer<KIO::ApplicationLauncherJob> m_applicationLauncherJob;
    QString m_oldLocationBarUrl;
    bool m_jobHadError;
    bool m_dontPassToWebEnginePart;
};

QDebug operator<<(QDebug dbg, UrlLoader::OpenUrlAction action);

#endif // URLLLOADER_H
