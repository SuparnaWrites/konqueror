/* This file is part of the KDE project
   Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <kdebug.h>

#include "konq_run.h"
#include "konq_view.h"
#include <kfiledialog.h>
#include <kuserprofile.h>
#include <kio/job.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <kstringhandler.h>
#include <ktempfile.h>

#include <assert.h>
#include <iostream.h>

KonqRun::KonqRun( KonqMainWindow* mainWindow, KonqView *_childView,
                  const KURL & _url, const KonqOpenURLRequest & req, bool trustedSource )
    : KParts::BrowserRun( _url, req.args, _childView ? _childView->part() : 0L, mainWindow,
                          //remove referrer if request was typed in manually.
                          // ### TODO: turn this off optionally.
                          !req.typedURL.isEmpty(), trustedSource ),
    m_pMainWindow( mainWindow ), m_pView( _childView ), m_bFoundMimeType( false ), m_req( req )
{
  assert( !m_pMainWindow.isNull() );
  if (m_pView)
    m_pView->setLoading(true);
}

KonqRun::~KonqRun()
{
  kdDebug(1202) << "KonqRun::~KonqRun()" << endl;
}

void KonqRun::foundMimeType( const QString & _type )
{
  kdDebug(1202) << "KonqRun::foundMimeType " << _type << endl;

  QString mimeType = _type; // this ref comes from the job, we lose it when using KIO again

  m_bFoundMimeType = true;

  if (m_pView)
    m_pView->setLoading(false); // first phase finished, don't confuse KonqView

  //kdDebug(1202) << "m_req.nameFilter= " << m_req.nameFilter << endl;
  //kdDebug(1202) << "m_req.typedURL= " << m_req.typedURL << endl;

  // Check if the main window wasn't deleted meanwhile
  if( !m_pMainWindow )
  {
      m_bFinished = true;
      m_bFault = true;
      m_timer.start( 0, true );
      return;
  }

  // Grab the args back from BrowserRun
  m_req.args = m_args;

  m_bFinished = m_pMainWindow->openView( mimeType, m_strURL, m_pView, m_req );
  if ( m_bFinished ) {
      m_pMainWindow = 0L;
      m_timer.start( 0, true );
      return;
  }

  // If we were following another view, do nothing if opening didn't work.
  if ( m_req.followMode )
      m_bFinished = true;

  if ( !m_bFinished ) {
      // If we couldn't embed the mimetype, call BrowserRun::handleNonEmbeddable()
      KParts::BrowserRun::NonEmbeddableResult res = handleNonEmbeddable( mimeType );
      if ( res == KParts::BrowserRun::Delayed )
          return;
      m_bFinished = ( res == KParts::BrowserRun::Handled );
  }

  if ( !m_bFinished ) // only if we're going to open
  {
      // Prevention against user stupidity : if the associated app for this mimetype
      // is konqueror/kfmclient, then we'll loop forever. So we have to check what KRun
      // is going to do before calling it.
      KService::Ptr offer = KServiceTypeProfile::preferredService( mimeType, "Application" );
      if ( offer && ( offer->desktopEntryName() == "konqueror" || offer->desktopEntryName().startsWith("kfmclient") ) )
      {
          KMessageBox::error( m_pMainWindow, i18n("There appears to be a configuration error. You have associated Konqueror with %1, but it can't handle this file type.").arg(mimeType));
          m_bFinished = true;
      }
  }

  if ( m_bFinished ) {
      // make Konqueror think there was an error, in order to stop the spinning wheel
      // (we are starting another app, so the current view should stop loading).
      m_bFault = true;

      m_pMainWindow = 0L;
      m_timer.start( 0, true );
      return;
  }

  kdDebug(1202) << "Nothing special to do in KonqRun, falling back to KRun" << endl;
  KRun::foundMimeType( mimeType );
}

void KonqRun::handleError( KIO::Job *job )
{
    kdDebug(1202) << "KonqRun::handleError error:" << job->errorString() << endl;
    // Override BrowserRun's default behaviour on error messages
    // KHTMLPart will show an error message
    m_job = 0;
    foundMimeType( "text/html" );
}

#include "konq_run.moc"
