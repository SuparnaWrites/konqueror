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

#include "konq_propsview.h"
#include "konq_listviewitems.h"
#include "konq_listviewwidget.h"
#include "konq_listview.h"
#include <konqfileitem.h>
#include <kio/job.h>
#include <kio/global.h>
#include <klocale.h>
#include <assert.h>
#include <stdio.h>

/**************************************************************
 *
 * KonqListViewItem
 *
 **************************************************************/
//KonqListViewItem::KonqListViewItem( KonqBaseListViewWidget *_listViewWidget, KonqListViewDir * _parent, KonqFileItem* _fileitem )
KonqListViewItem::KonqListViewItem( KonqBaseListViewWidget *_listViewWidget, KonqListViewItem * _parent, KonqFileItem* _fileitem )
:KonqBaseListViewItem( _parent,_fileitem )
{
   m_pListViewWidget = _listViewWidget;
   init();
}

KonqListViewItem::KonqListViewItem( KonqBaseListViewWidget *_listViewWidget, KonqFileItem* _fileitem )
:KonqBaseListViewItem(_listViewWidget,_fileitem)
{
   m_pListViewWidget = _listViewWidget;
   init();
}

void KonqListViewItem::init()
{
   setPixmap( 0/*m_PListViewWidget->m_filenameColumn*/, m_fileitem->pixmap( m_pListViewWidget->iconSize(), false /*no image preview*/ ) );
   // Set the text of each column

   if (S_ISDIR(m_fileitem->mode()))
      sortChar='0';

   setText(0,m_fileitem->text());
   //now we have the first column, so let's do the rest

   for (unsigned int i=0; i<KonqBaseListViewWidget::NumberOfAtoms; i++)
   {
      ColumnInfo *tmpColumn=&m_pListViewWidget->columnConfigInfo()[i];
      if (tmpColumn->displayThisOne)
      {
         switch (tmpColumn->udsId)
         {
            // Why did you remove the switch ? It's easier to read
            // and more efficient... (David)
            //so here we go again (Alex)
         case KIO::UDS_USER:
            setText(tmpColumn->displayInColumn,m_fileitem->user());
            break;
         case KIO::UDS_GROUP:
            setText(tmpColumn->displayInColumn,m_fileitem->group());
            break;
         case KIO::UDS_FILE_TYPE:
            setText(tmpColumn->displayInColumn,m_fileitem->mimeComment());
            break;
         case KIO::UDS_MIME_TYPE:
            setText(tmpColumn->displayInColumn,m_fileitem->mimetype());
            break;
         case KIO::UDS_URL:
            setText(tmpColumn->displayInColumn,m_fileitem->url().prettyURL());
            break;
         case KIO::UDS_LINK_DEST:
            setText(tmpColumn->displayInColumn,m_fileitem->linkDest());
            break;
         case KIO::UDS_SIZE:
            setText(tmpColumn->displayInColumn,KGlobal::locale()->formatNumber( m_fileitem->size(), 0 ));
            break;
         case KIO::UDS_ACCESS:
            setText(tmpColumn->displayInColumn,makeAccessString(m_fileitem->permissions()));
            break;
         case KIO::UDS_MODIFICATION_TIME:
         case KIO::UDS_ACCESS_TIME:
         case KIO::UDS_CREATION_TIME:
            {
               QDateTime dt;
               dt.setTime_t( m_fileitem->time( tmpColumn->udsId ) );
               setText(tmpColumn->displayInColumn,KGlobal::locale()->formatDate(dt.date(),true)+" "+KGlobal::locale()->formatTime(dt.time()));
            }
            break;
         default:
            break;
         };
      };
   };
}

QString KonqListViewItem::key( int _column, bool asc) const
{
   QString tmp=sortChar;
   if (!asc && (sortChar=='0')) tmp=QChar('2');
   //check if it is a time column
   if (_column>1)
   {
      for (unsigned int i=0; i<KonqBaseListViewWidget::NumberOfAtoms; i++)
      {
         ColumnInfo *cInfo=&m_pListViewWidget->columnConfigInfo()[i];
         if (_column==cInfo->displayInColumn)
         {
            if ((cInfo->udsId==KIO::UDS_MODIFICATION_TIME)
                || (cInfo->udsId==KIO::UDS_ACCESS_TIME)
                || (cInfo->udsId==KIO::UDS_CREATION_TIME))
            {
               QString tmpDate;
               tmpDate.sprintf("%ld",m_fileitem->time(cInfo->udsId));
               tmp+=tmpDate;
               return tmp;
            }
            else break;

         }
      }
   }
   tmp+=text(_column);
   return tmp;
}

QString KonqListViewItem::makeNumericString( const KIO::UDSAtom &_atom ) const
{
  return KGlobal::locale()->formatNumber( _atom.m_long, 0);
}

QString KonqListViewItem::makeTimeString( const KIO::UDSAtom &_atom ) const
{
   QDateTime dt; dt.setTime_t((time_t) _atom.m_long);
   return KGlobal::locale()->formatDate(dt.date(), true) + " " +
      KGlobal::locale()->formatTime(dt.time());
}

void KonqListViewItem::paintCell( QPainter *_painter, const QColorGroup & _cg, int _column, int _width, int _alignment )
{
  QColorGroup cg( _cg );

  if ( _column == 0 )
  {
     _painter->setFont( m_pListViewWidget->itemFont() );
     cg.setColor( QColorGroup::Text, m_pListViewWidget->itemColor() );
  }
  else
     _painter->setPen( m_pListViewWidget->color() );

  if (!m_pListViewWidget->props()->bgPixmap().isNull())
  {
     _painter->drawTiledPixmap( 0, 0, _width, height(),
                                m_pListViewWidget->props()->bgPixmap(),
                                0, 0 ); // ?
  }

  // Now prevent QListViewItem::paintCell from drawing a white background
  // I hope color0 is transparent :-))
  // Sorry, to me it looks more like black (alex)
  //cg.setColor( QColorGroup::Base, QColor(qRgba(0, 0, 0, 0)));

  QListViewItem::paintCell( _painter, cg, _column, _width, _alignment );
}

