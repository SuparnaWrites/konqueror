// kcookiesmain.cpp - Cookies configuration
//
// First version of cookies configuration by Waldo Bastian <bastian@kde.org>
// This dialog box created by David Faure <faure@kde.org>

#include <qlayout.h>
#include <qtabwidget.h>

#include <klocale.h>
#include <kapp.h>
#include <kbuttonbox.h>
#include <kmessagebox.h>
#include <kprocess.h>
#include <kdebug.h>
#include <ksimpleconfig.h>
#include <dcopclient.h>

#include "kcookiesmain.h"
#include "kcookiespolicies.h"
#include "kcookiesmanagement.h"

KCookiesMain::KCookiesMain(QWidget *parent, const char *name)
  : KCModule(parent, name)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    tab = new QTabWidget(this);
    layout->addWidget(tab);
    
    policies = new KCookiesPolicies(this);
    tab->addTab(policies, i18n("&Policies"));
    connect(policies, SIGNAL(changed(bool)), this, SLOT(moduleChanged()));

    management = new KCookiesManagement(this, "Management");
    tab->addTab(management, i18n("&Management"));
    connect(management, SIGNAL(changed(bool)), this, SLOT(moduleChanged()));
}

KCookiesMain::~KCookiesMain()
{
}

void KCookiesMain::load()
{
  policies->load();
  management->load();
}

void KCookiesMain::save()
{
  policies->save();
  management->save();
}

void KCookiesMain::defaults()
{
  policies->defaults();
  management->defaults();
}

void KCookiesMain::moduleChanged()
{
  emit KCModule::changed(true);
}

QString KCookiesMain::quickHelp()
{
  return i18n("<h1>Cookies</h1> Cookies contain information that Konqueror "
    " (or other KDE applications using the http protocol) stores on your "
    " computer, initiiated by a remote internet server. This means, that "
    " a web server can store information about you and your browsing activities "
    " on your machine for later use. You might consider this an attack on your "
    " privacy. <p> However, cookies are useful in certain situations. For example, they "
    " are often used by internet shops, so you can 'put things into a shopping basket'. "
    " Some sites require you have a browser that supports cookies. <p>"
    " Because most people want a compromise between privacy and the benefits cookies offer,"
    " KDE offers you to customize the way it handles cookies. So you might want "
    " to set KDE's default policy to ask you when a server wants to set a cookie,"
    " so you can decide. For your favourite shopping web sites you trust in you might want to"
    " set the policy to accept, so you can use the web sites without being asked"
    " everytime KDE receives a cookie." );
}

#include "kcookiesmain.moc"
