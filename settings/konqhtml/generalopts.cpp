/*
    Add here all general options - those that apply to both web browsing and filemanagement mode

    SPDX-FileCopyrightText: 1998 Sven Radej
    SPDX-FileCopyrightText: 1998 David Faure
    SPDX-FileCopyrightText: 2001 Waldo Bastian <bastian@kde.org>
    SPDX-FileCopyrightText: 2007 Nick Shaforostoff <shafff@ukr.net>
*/

// Own
#include "generalopts.h"

// Qt
#include <QDBusConnection>
#include <QDBusMessage>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QStandardPaths>
#include <QUrl>

// KDE
#include <kbuildsycocaprogressdialog.h>
#include <kmimetypetrader.h>
#include <kservice.h>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KMessageWidget>

// Local
#include "ui_advancedTabOptions.h"

// Keep in sync with konqueror.kcfg
static const char DEFAULT_STARTPAGE[] = "konq:konqueror";
static const char DEFAULT_HOMEPAGE[] = "https://www.kde.org/";
// Keep in sync with the order in the combo
enum StartPage { ShowAboutPage, ShowStartUrlPage, ShowBlankPage, ShowBookmarksPage };

//-----------------------------------------------------------------------------

KKonqGeneralOptions::KKonqGeneralOptions(QWidget *parent, const QVariantList &)
    : KCModule(parent), m_emptyStartUrlWarning(new KMessageWidget(this))
{
    m_pConfig = KSharedConfig::openConfig(QStringLiteral("konquerorrc"), KConfig::NoGlobals);
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    addHomeUrlWidgets(lay);

    QGroupBox *tabsGroup = new QGroupBox(i18n("Tabbed Browsing"));

    tabOptions = new Ui_advancedTabOptions;
    tabOptions->setupUi(tabsGroup);

    connect(tabOptions->m_pShowMMBInTabs, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pDynamicTabbarHide, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pNewTabsInBackground, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pOpenAfterCurrentPage, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pTabConfirm, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pTabCloseActivatePrevious, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pPermanentCloseButton, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pKonquerorTabforExternalURL, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pPopupsWithinTabs, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);
    connect(tabOptions->m_pMiddleClickClose, &QAbstractButton::toggled, this, &KKonqGeneralOptions::slotChanged);

    lay->addWidget(tabsGroup);

    emit changed(false);
}

void KKonqGeneralOptions::addHomeUrlWidgets(QVBoxLayout *lay)
{
    QFormLayout *formLayout = new QFormLayout;
    lay->addLayout(formLayout);

    m_emptyStartUrlWarning->setText(i18nc("The user chose to use a custom start page but left the corresponding field empty", "Please, insert the custom start page"));
    m_emptyStartUrlWarning->setMessageType(KMessageWidget::Warning);
    m_emptyStartUrlWarning->setIcon(QIcon::fromTheme("dialog-warning"));
    m_emptyStartUrlWarning->hide();
    formLayout->addRow(m_emptyStartUrlWarning);

    QLabel *startLabel = new QLabel(i18nc("@label:listbox", "When &Konqueror starts:"), this);

    QWidget *containerWidget = new QWidget(this);
    QHBoxLayout *hboxLayout = new QHBoxLayout(containerWidget);
    hboxLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->addRow(startLabel, containerWidget);

    m_startCombo = new QComboBox(this);
    m_startCombo->setEditable(false);
    m_startCombo->addItem(i18nc("@item:inlistbox", "Show Introduction Page"), ShowAboutPage);
    m_startCombo->addItem(i18nc("@item:inlistbox", "Show My Start Page"), ShowStartUrlPage);
    m_startCombo->addItem(i18nc("@item:inlistbox", "Show Blank Page"), ShowBlankPage);
    m_startCombo->addItem(i18nc("@item:inlistbox", "Show My Bookmarks"), ShowBookmarksPage);
    startLabel->setBuddy(m_startCombo);
    connect(m_startCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KKonqGeneralOptions::slotChanged);
    hboxLayout->addWidget(m_startCombo);

    startURL = new QLineEdit(this);
    startURL->setWindowTitle(i18nc("@title:window", "Select Start Page"));
    hboxLayout->addWidget(startURL);
    connect(startURL, &QLineEdit::textChanged, this, &KKonqGeneralOptions::displayEmpytStartPageWarningIfNeeded);

    QString startstr = i18n("This is the URL of the web page "
                           "Konqueror will show when starting.");
    startURL->setToolTip(startstr);
    connect(m_startCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            startURL->setVisible(idx == ShowStartUrlPage);
            displayEmpytStartPageWarningIfNeeded();
            });
    startURL->hide();

    ////

    QLabel *label = new QLabel(i18n("Home page:"), this);

    homeURL = new QLineEdit(this);
    homeURL->setWindowTitle(i18nc("@title:window", "Select Home Page"));
    formLayout->addRow(label, homeURL);
    connect(homeURL, &QLineEdit::textChanged, this, &KKonqGeneralOptions::slotChanged);
    label->setBuddy(homeURL);

    QString homestr = i18n("This is the URL of the web page where "
                           "Konqueror will jump to when "
                           "the \"Home\" button is pressed.");
    label->setToolTip(homestr);
    homeURL->setToolTip(homestr);

    ////

    QLabel *webLabel = new QLabel(i18n("Default web browser engine:"), this);

    m_webEngineCombo = new QComboBox(this);
    m_webEngineCombo->setEditable(false);
    m_webEngineCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    formLayout->addRow(webLabel, m_webEngineCombo);
    webLabel->setBuddy(m_webEngineCombo);
    connect(m_webEngineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KKonqGeneralOptions::slotChanged);

    QLabel *splitLabel = new QLabel(i18n("When splitting a view"));
    m_splitBehaviour = new QComboBox(this);
    //Keep items order in sync with KonqMainWindow::SplitBehaviour
    m_splitBehaviour->addItems({
        i18n("Always duplicate current view"),
        i18n("Duplicate current view only for local files")
    });
    splitLabel->setBuddy(m_splitBehaviour);
    formLayout->addRow(splitLabel, m_splitBehaviour);
    connect(m_splitBehaviour, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &KKonqGeneralOptions::slotChanged);
}

KKonqGeneralOptions::~KKonqGeneralOptions()
{
    delete tabOptions;
}

void KKonqGeneralOptions::displayEmpytStartPageWarningIfNeeded()
{
    if (startURL->isVisible() && startURL->text().isEmpty()) {
        m_emptyStartUrlWarning->animatedShow();
    } else if (m_emptyStartUrlWarning->isVisible()) {
        m_emptyStartUrlWarning->animatedHide();
    }
}

static StartPage urlToStartPageEnum(const QString &startUrl)
{
    if (startUrl == QLatin1String("konq:blank")) {
        return ShowBlankPage;
    }
    if (startUrl == QLatin1String("konq:") || startUrl == QLatin1String("konq:konqueror")) {
        return ShowAboutPage;
    }
    if (startUrl == QLatin1String("bookmarks:") || startUrl == QLatin1String("bookmarks:/")) {
        return ShowBookmarksPage;
    }
    return ShowStartUrlPage;
}

static QString startPageEnumToUrl(StartPage startPage)
{
    switch (startPage) {
        case ShowBlankPage:
            return QStringLiteral("konq:blank");
        case ShowAboutPage:
            return QStringLiteral("konq:konqueror");
        case ShowBookmarksPage:
            return QStringLiteral("bookmarks:/");
        case ShowStartUrlPage:
            return QString();
    }
    return QString();
}

void KKonqGeneralOptions::load()
{
    KConfigGroup userSettings(m_pConfig, "UserSettings");
    const QUrl homeUrl(QUrl(userSettings.readEntry("HomeURL", DEFAULT_HOMEPAGE)));
    const QUrl startUrl(QUrl(userSettings.readEntry("StartURL", DEFAULT_STARTPAGE)));
    homeURL->setText(homeUrl.toString());
    startURL->setText(startUrl.toString());
    const StartPage startPage = urlToStartPageEnum(startUrl.toString());
    const int startComboIndex = m_startCombo->findData(startPage);
    Q_ASSERT(startComboIndex != -1);
    m_startCombo->setCurrentIndex(startComboIndex);

    const bool alwaysDuplicateWhenSplitting = userSettings.readEntry("AlwaysDuplicatePageWhenSplittingView", true);
    m_splitBehaviour->setCurrentIndex(alwaysDuplicateWhenSplitting ? 0 : 1);

    m_webEngineCombo->clear();
    // ## Well, the problem with using the trader to find the available parts, is that if a user
    // removed a part in keditfiletype text/html, it won't be in the list anymore. Oh well.
    const KService::List partOfferList = KMimeTypeTrader::self()->query(QStringLiteral("text/html"), QStringLiteral("KParts/ReadOnlyPart"), QStringLiteral("not ('KParts/ReadWritePart' in ServiceTypes)"));
    // Sorted list, so the first one is the preferred one, no need for a setCurrentIndex.
    Q_FOREACH (const KService::Ptr partService, partOfferList) {
        // We want only the HTML-capable parts, not any text/plain part (via inheritance)
        // This is a small "private inheritance" hack, pending a more general solution
        if (!partService->hasMimeType(QStringLiteral("text/plain"))) {
            m_webEngineCombo->addItem(QIcon::fromTheme(partService->icon()), partService->name(),
                                      QVariant(partService->storageId()));
        }
    }

    KConfigGroup cg(m_pConfig, "FMSettings"); // ### what a wrong group name for these settings...

    tabOptions->m_pShowMMBInTabs->setChecked(cg.readEntry("MMBOpensTab", true));
    tabOptions->m_pDynamicTabbarHide->setChecked(!(cg.readEntry("AlwaysTabbedMode", false)));

    tabOptions->m_pNewTabsInBackground->setChecked(!(cg.readEntry("NewTabsInFront", false)));
    tabOptions->m_pOpenAfterCurrentPage->setChecked(cg.readEntry("OpenAfterCurrentPage", false));
    tabOptions->m_pPermanentCloseButton->setChecked(cg.readEntry("PermanentCloseButton", true));
    tabOptions->m_pKonquerorTabforExternalURL->setChecked(cg.readEntry("KonquerorTabforExternalURL", false));
    tabOptions->m_pPopupsWithinTabs->setChecked(cg.readEntry("PopupsWithinTabs", false));
    tabOptions->m_pTabCloseActivatePrevious->setChecked(cg.readEntry("TabCloseActivatePrevious", false));
    tabOptions->m_pMiddleClickClose->setChecked(cg.readEntry("MouseMiddleClickClosesTab", false));

    cg = KConfigGroup(m_pConfig, "Notification Messages");
    tabOptions->m_pTabConfirm->setChecked(!cg.hasKey("MultipleTabConfirm"));

}

void KKonqGeneralOptions::defaults()
{
    homeURL->setText(QUrl(DEFAULT_HOMEPAGE).toString());
    startURL->setText(QUrl(DEFAULT_STARTPAGE).toString());
    m_splitBehaviour->setCurrentIndex(0);

    bool old = m_pConfig->readDefaults();
    m_pConfig->setReadDefaults(true);
    load();
    m_pConfig->setReadDefaults(old);
}

void KKonqGeneralOptions::save()
{
    KConfigGroup userSettings(m_pConfig, "UserSettings");
    const int startComboIndex = m_startCombo->currentIndex();
    const StartPage choice = static_cast<StartPage>(m_startCombo->itemData(startComboIndex).toInt());
    QString startUrl(startPageEnumToUrl(static_cast<StartPage>(choice)));
    if (startUrl.isEmpty()) {
        startUrl = startURL->text();
    }
    userSettings.writeEntry("StartURL", startUrl);
    userSettings.writeEntry("HomeURL", homeURL->text());
    userSettings.writeEntry("AlwaysDuplicatePageWhenSplittingView", m_splitBehaviour->currentIndex() == 0);

    if (m_webEngineCombo->currentIndex() > 0) {
        // The user changed the preferred web engine, save into mimeapps.list.
        const QString preferredWebEngine = m_webEngineCombo->itemData(m_webEngineCombo->currentIndex()).toString();
        //qCDebug(KONQUEROR_LOG) << "preferredWebEngine=" << preferredWebEngine;

        KSharedConfig::Ptr profile = KSharedConfig::openConfig(QStringLiteral("mimeapps.list"), KConfig::NoGlobals, QStandardPaths::ConfigLocation);
        KConfigGroup addedServices(profile, "Added KDE Service Associations");
        Q_FOREACH (const QString &mimeType, QStringList() << "text/html" << "application/xhtml+xml" << "application/xml") {
            QStringList services = addedServices.readXdgListEntry(mimeType);
            services.removeAll(preferredWebEngine);
            services.prepend(preferredWebEngine); // make it the preferred one
            addedServices.writeXdgListEntry(mimeType, services);
        }
        profile->sync();

        // kbuildsycoca is the one reading mimeapps.list, so we need to run it now
        KBuildSycocaProgressDialog::rebuildKSycoca(this);
    }

    KConfigGroup cg(m_pConfig, "FMSettings");
    cg.writeEntry("MMBOpensTab", tabOptions->m_pShowMMBInTabs->isChecked());
    cg.writeEntry("AlwaysTabbedMode", !(tabOptions->m_pDynamicTabbarHide->isChecked()));

    cg.writeEntry("NewTabsInFront", !(tabOptions->m_pNewTabsInBackground->isChecked()));
    cg.writeEntry("OpenAfterCurrentPage", tabOptions->m_pOpenAfterCurrentPage->isChecked());
    cg.writeEntry("PermanentCloseButton", tabOptions->m_pPermanentCloseButton->isChecked());
    cg.writeEntry("KonquerorTabforExternalURL", tabOptions->m_pKonquerorTabforExternalURL->isChecked());
    cg.writeEntry("PopupsWithinTabs", tabOptions->m_pPopupsWithinTabs->isChecked());
    cg.writeEntry("TabCloseActivatePrevious", tabOptions->m_pTabCloseActivatePrevious->isChecked());
    cg.writeEntry("MouseMiddleClickClosesTab", tabOptions->m_pMiddleClickClose->isChecked());
    cg.sync();
    // It only matters whether the key is present, its value has no meaning
    cg = KConfigGroup(m_pConfig, "Notification Messages");
    if (tabOptions->m_pTabConfirm->isChecked()) {
        cg.deleteEntry("MultipleTabConfirm");
    } else {
        cg.writeEntry("MultipleTabConfirm", true);
    }
    // Send signal to all konqueror instances
    QDBusMessage message =
        QDBusMessage::createSignal(QStringLiteral("/KonqMain"), QStringLiteral("org.kde.Konqueror.Main"), QStringLiteral("reparseConfiguration"));
    QDBusConnection::sessionBus().send(message);

    emit changed(false);
}

void KKonqGeneralOptions::slotChanged()
{
    emit changed(true);
}

