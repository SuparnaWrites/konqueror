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

#include "konq_treeview.h"
#include "konq_propsview.h"
#include "konq_mainview.h"

#include <kcursor.h>
#include <kdirlister.h>
#include <kdirwatch.h>
#include <kfileitem.h>
#include <kio_error.h>
#include <kio_job.h>
#include <kmimetypes.h>
#include <kpixmapcache.h>
#include <kprotocolmanager.h>
#include <krun.h>

#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <kapp.h>

#include <qmsgbox.h>
#include <qkeycode.h>
#include <qlist.h>
#include <qdragobject.h>
#include <qheader.h>
#include <klocale.h>

#include <opUIUtils.h>

#define Dnd_X_Precision 2
#define Dnd_Y_Precision 2

KonqKfmTreeView::KonqKfmTreeView( KonqMainView *mainView )
{
  kdebug(0, 1202, "+KonqKfmTreeView");
  ADD_INTERFACE( "IDL:Konqueror/KfmTreeView:1.0" );

  setWidget( this );

  QWidget::setFocusPolicy( StrongFocus );
  // viewport()->setFocusPolicy( StrongFocus );
  // setFocusPolicy( ClickFocus );
  setMultiSelection( true );

  m_pMainView          = mainView;
  m_pWorkingDir        = 0L;
  m_bTopLevelComplete  = true;
  m_bSubFolderComplete = true;
  m_iColumns           = -1;
  m_dragOverItem       = 0L;
  m_pressed            = false;
  m_pressedItem        = 0L;
  m_handCursor         = KCursor().handCursor();
  m_stdCursor          = KCursor().arrowCursor();
  m_overItem           = 0L;
  m_dirLister          = 0L;

  // Create a properties instance for this view
  // (copying the default values)
  m_pProps = new KonqPropsView( * KonqPropsView::defaultProps() );

  setRootIsDecorated( true );
  setTreeStepSize( 20 );
  setSorting( 1 );

  initConfig();

  QObject::connect( this, SIGNAL( rightButtonPressed( QListViewItem*,
					     const QPoint&, int ) ),
	   this, SLOT( slotRightButtonPressed( QListViewItem*,
					       const QPoint&, int ) ) );
  QObject::connect( this, SIGNAL( returnPressed( QListViewItem* ) ),
	   this, SLOT( slotReturnPressed( QListViewItem* ) ) );
  QObject::connect( this, SIGNAL( doubleClicked( QListViewItem* ) ),
	   this, SLOT( slotReturnPressed( QListViewItem* ) ) );
  QObject::connect( this, SIGNAL( currentChanged( QListViewItem* ) ),
	   this, SLOT( slotCurrentChanged( QListViewItem* ) ) );

  //  connect( m_pView->gui(), SIGNAL( configChanged() ), SLOT( initConfig() ) );

  // Qt 1.41 hack to get drop events
  viewport()->installEventFilter( this );
  viewport()->setAcceptDrops( true );
  viewport()->setMouseTracking( true );

  m_dragOverItem = 0L;

  setFrameStyle( QFrame::NoFrame | QFrame::Plain );
}

KonqKfmTreeView::~KonqKfmTreeView()
{
  kdebug(0, 1202, "-KonqKfmTreeView");
  // viewport()->removeEventFilter( this );

  if ( m_dirLister ) delete m_dirLister;
  delete m_pProps;

  iterator it = begin();
  for( ; it != end(); ++it )
    it->prepareToDie();

}

bool KonqKfmTreeView::mappingOpenURL( Browser::EventOpenURL eventURL )
{
  KonqBaseView::mappingOpenURL( eventURL );
  openURL( eventURL.url, (int)eventURL.xOffset, (int)eventURL.yOffset );
  return true;
}

bool KonqKfmTreeView::mappingFillMenuView( Browser::View::EventFillMenu_ptr viewMenu )
{
#define MVIEW_SHOWDOT_ID_TREEVIEW 1484 // yes, Simon, I need an id since I want
  // to enable/disable the item. How do I do ? :)
  
  if ( !CORBA::is_nil( viewMenu ) )
  {
    CORBA::WString_var text = Q2C( i18n("Rel&oad Tree") );
    viewMenu->insertItem4( text, this, "slotReloadTree", 0, -1 , -1 );
    text = Q2C( i18n("Show &Dot Files") );
    viewMenu->insertItem4( text, this, "slotShowDot" , 0, 
                                MVIEW_SHOWDOT_ID_TREEVIEW, -1 );
    viewMenu->setItemChecked( MVIEW_SHOWDOT_ID_TREEVIEW, m_pProps->m_bShowDot );
    m_vViewMenu = OpenPartsUI::Menu::_duplicate( viewMenu );
  }
  
  return true;
}

bool KonqKfmTreeView::mappingFillMenuEdit( Browser::View::EventFillMenu_ptr /*editMenu*/ )
{
  // TODO : add select and selectall (taken from Icon View)
  return true;
}

void KonqKfmTreeView::stop()
{
  m_dirLister->stop();
}

char *KonqKfmTreeView::url()
{
  assert( m_dirLister );
  return CORBA::string_dup( m_dirLister->url().ascii() );
}

CORBA::Long KonqKfmTreeView::xOffset()
{
  return (CORBA::Long)contentsY();
}

CORBA::Long KonqKfmTreeView::yOffset()
{
  return (CORBA::Long)contentsX();
}

void KonqKfmTreeView::slotReloadTree()
{
  openURL( url(), contentsX(), contentsY() );
}

void KonqKfmTreeView::slotShowDot()
{
  kdebug(0, 1202, "KonqKfmTreeView::slotShowDot()");
  m_pProps->m_bShowDot = !m_pProps->m_bShowDot;
  m_dirLister->setShowingDotFiles( m_pProps->m_bShowDot );
  m_vViewMenu->setItemChecked( MVIEW_SHOWDOT_ID_TREEVIEW, m_pProps->m_bShowDot );
}

void KonqKfmTreeView::initConfig()
{
  QPalette p          = viewport()->palette();
//  KfmViewSettings *settings  = m_pView->settings();

  KConfig *config = kapp->getConfig();
  config->setGroup("Settings");

  KfmViewSettings *settings = new KfmViewSettings( config );

  m_bgColor           = settings->bgColor();
  m_textColor         = settings->textColor();
  m_linkColor         = settings->linkColor();
  m_vLinkColor        = settings->vLinkColor();
  m_stdFontName       = settings->stdFontName();
  m_fixedFontName     = settings->fixedFontName();
  m_fontSize          = settings->fontSize();

//   m_bgPixmap          = props->bgPixmap();

//   if ( m_bgPixmap.isNull() )
//     viewport()->setBackgroundMode( PaletteBackground );
//   else
//     viewport()->setBackgroundMode( NoBackground );

  m_mouseMode         = settings->mouseMode();

  m_underlineLink     = settings->underlineLink();
  m_changeCursor      = settings->changeCursor();

  QColorGroup c = p.normal();
  QColorGroup n( m_textColor, m_bgColor, c.light(), c.dark(), c.mid(),
		 m_textColor, m_bgColor );
  p.setNormal( n );
  c = p.active();
  QColorGroup a( m_textColor, m_bgColor, c.light(), c.dark(), c.mid(),
		 m_textColor, m_bgColor );
  p.setActive( a );
  c = p.disabled();
  QColorGroup d( m_textColor, m_bgColor, c.light(), c.dark(), c.mid(),
		 m_textColor, m_bgColor );
  p.setDisabled( d );
  viewport()->setPalette( p );
//   viewport()->setBackgroundColor( m_bgColor );

  QFont font( m_stdFontName, m_fontSize );
  setFont( font );

  delete settings;
}

void KonqKfmTreeView::setBgColor( const QColor& _color )
{
  m_bgColor = _color;

  update();
}

void KonqKfmTreeView::setTextColor( const QColor& _color )
{
  m_textColor = _color;

  update();
}

void KonqKfmTreeView::setLinkColor( const QColor& _color )
{
  m_linkColor = _color;

  update();
}

void KonqKfmTreeView::setVLinkColor( const QColor& _color )
{
  m_vLinkColor = _color;

  update();
}

void KonqKfmTreeView::setStdFontName( const char *_name )
{
  m_stdFontName = _name;

  update();
}

void KonqKfmTreeView::setFixedFontName( const char *_name )
{
  m_fixedFontName = _name;

  update();
}

void KonqKfmTreeView::setFontSize( const int _size )
{
  m_fontSize = _size;

  update();
}

void KonqKfmTreeView::setBgPixmap( const QPixmap& _pixmap )
{
  m_bgPixmap = _pixmap;

  if ( m_bgPixmap.isNull() )
    viewport()->setBackgroundMode( PaletteBackground );
  else
    viewport()->setBackgroundMode( NoBackground );

  update();
}

void KonqKfmTreeView::setUnderlineLink( bool _underlineLink )
{
  m_underlineLink = _underlineLink;

  update();
}

bool KonqKfmTreeView::eventFilter( QObject *o, QEvent *e )
{
  if ( o != viewport() )
     return false;

  if ( e->type() == QEvent::Drop )
  {
    dropEvent( (QDropEvent*)e );
    return true;
  }
  else if ( e->type() == QEvent::DragEnter )
  {
    dragEnterEvent( (QDragEnterEvent*)e );
    return true;
  }
  else if ( e->type() == QEvent::DragLeave )
  {
    dragLeaveEvent( (QDragLeaveEvent*)e );
    return true;
  }
  else if ( e->type() == QEvent::DragMove )
  {
    dragMoveEvent( (QDragMoveEvent*)e );
    return true;
  }

  return QListView::eventFilter( o, e );
}

void KonqKfmTreeView::dragMoveEvent( QDragMoveEvent *_ev )
{
  KfmTreeViewItem *item = (KfmTreeViewItem*)itemAt( _ev->pos() );
  if ( !item )
  {
    if ( m_dragOverItem )
      setSelected( m_dragOverItem, false );
    _ev->accept();
    return;
  }

  if ( m_dragOverItem == item )
    return;
  if ( m_dragOverItem != 0L )
    setSelected( m_dragOverItem, false );

  if ( item->item()->acceptsDrops( m_lstDropFormats ) )
  {
    _ev->accept();
    setSelected( item, true );
    m_dragOverItem = item;
  }
  else
  {
    _ev->ignore();
    m_dragOverItem = 0L;
  }

  return;
}

void KonqKfmTreeView::dragEnterEvent( QDragEnterEvent *_ev )
{
  m_dragOverItem = 0L;

  // Save the available formats
  m_lstDropFormats.clear();

  for( int i = 0; _ev->format( i ); i++ )
  {
    if ( *( _ev->format( i ) ) )
      m_lstDropFormats.append( _ev->format( i ) );
  }

  // By default we accept any format
  _ev->accept();
}

void KonqKfmTreeView::dragLeaveEvent( QDragLeaveEvent * )
{
  if ( m_dragOverItem != 0L )
    setSelected( m_dragOverItem, false );
  m_dragOverItem = 0L;

  /** DEBUG CODE */
  // Give the user some feedback...
  /** End DEBUG CODE */
}

void KonqKfmTreeView::dropEvent( QDropEvent *  )
{
  if ( m_dragOverItem != 0L )
    setSelected( m_dragOverItem, false );
  m_dragOverItem = 0L;
}

/*
void KonqKfmTreeView::slotDirectoryDirty( const char *_dir )
{
  QString f = _dir;
  KURL::encode( f );

  QDictIterator<KfmTreeViewDir> it( m_mapSubDirs );
  for( ; it.current(); ++it )
  {
    if ( urlcmp( it.current()->url(0), f, true, true ) )
    {
      updateDirectory( it.current(), it.current()->url(0) );
      return;
    }
  }
}
*/

void KonqKfmTreeView::addSubDir( const KURL & _url, KfmTreeViewDir* _dir )
{
  m_mapSubDirs.insert( _url.url(), _dir );

  if ( _url.isLocalFile() )
    kdirwatch->addDir( _url.path(0) );
}

void KonqKfmTreeView::removeSubDir( const KURL & _url )
{
  m_mapSubDirs.remove( _url.url() );

  if ( _url.isLocalFile() )
    kdirwatch->removeDir( _url.path(0) );
}

void KonqKfmTreeView::keyPressEvent( QKeyEvent *_ev )
{
  // We are only interested in the escape key here
  if ( _ev->key() != Key_Escape )
  {
    QListView::keyPressEvent( _ev );
    return;
  }

  KfmTreeViewItem* item = (KfmTreeViewItem*)currentItem();

  if ( !item->isSelected() )
  {
    iterator it = begin();
    for( ; it != end(); it++ )
      if ( it->isSelected() )
	setSelected( &*it, false );
    setSelected( item, true );
  }

  QPoint p( width() / 2, height() / 2 );
  p = mapToGlobal( p );

  popupMenu( p );
}

void KonqKfmTreeView::mousePressEvent( QMouseEvent *_ev )
{
  m_pressed = true;
  m_pressedPos = _ev->pos();
  m_pressedItem = (KfmTreeViewItem*)itemAt( _ev->pos() );

//  if ( !hasFocus() )
//    setFocus();

  if ( m_pressedItem )
  {
    if ( m_mouseMode == SingleClick )
    {
      if ( _ev->button() == LeftButton && !( _ev->state() & ControlButton ) )
      {
	if ( !m_overItem )
	{
	  //reset to standard cursor
	  setCursor(m_stdCursor);
	  m_overItem = 0L;
	}
	
	iterator it = begin();
	for( ; it != end(); it++ )
	  if ( it->isSelected() )
	    QListView::setSelected( &*it, false );
	
	QListView::setSelected( m_pressedItem, true );
	//      m_pressedItem->returnPressed();
	//	QListView::mousePressEvent( _ev );
	return;
      }
    }
    else if ( m_mouseMode == DoubleClick )
    {
      if ( !m_pressedItem->isSelected() && _ev->button() == LeftButton && !( _ev->state() & ControlButton ) )
      {
	iterator it = begin();
	for( ; it != end(); it++ )
	  if ( it->isSelected() )
	    QListView::setSelected( &*it, false );
	
	QListView::mousePressEvent( _ev );
	return;
      }
      else if ( m_pressedItem->isSelected() && _ev->button() == LeftButton && !( _ev->state() & ControlButton ) )
      {
	QListView::mousePressEvent( _ev );
	// Fix what Qt destroyed :-)
	setSelected( m_pressedItem, true );
	return;
      }
    }

    if ( _ev->button() == RightButton )
    {
      m_pressed = false;
      m_pressedItem = 0L;
    }
  }
  /*
  else
    assert( 0 );
    */

   QListView::mousePressEvent( _ev );
}

void KonqKfmTreeView::mouseReleaseEvent( QMouseEvent *_mouse )
{
  if ( !m_pressed )
    return;

  if ( m_mouseMode == SingleClick &&
       _mouse->button() == LeftButton &&
       !( _mouse->state() & ControlButton ) &&
       isSingleClickArea( _mouse->pos() ) )
  {
    if ( m_pressedItem->isExpandable() )
      m_pressedItem->setOpen( !m_pressedItem->isOpen() );

    slotReturnPressed( m_pressedItem );
  }

  m_pressed = false;
  m_pressedItem = 0L;
}

void KonqKfmTreeView::mouseMoveEvent( QMouseEvent *_mouse )
{
  if ( !m_pressed || !m_pressedItem )
  {
    if ( isSingleClickArea( _mouse->pos() ) )
    {
      KfmTreeViewItem* item = (KfmTreeViewItem*)itemAt( _mouse->pos() );

      if ( m_overItem != item ) // we are on another item than before
      {
	slotOnItem( item );
	m_overItem = item;

	if ( m_mouseMode == SingleClick && m_changeCursor )
	  setCursor( m_handCursor );
      }
    }
    else if ( !m_overItem )
    {
      slotOnItem( 0L );

      setCursor( m_stdCursor );
      m_overItem = 0L;
    }

    return;
  }

  int x = _mouse->pos().x();
  int y = _mouse->pos().y();

  if ( abs( x - m_pressedPos.x() ) > Dnd_X_Precision || abs( y - m_pressedPos.y() ) > Dnd_Y_Precision )
  {
    // Collect all selected items
    QStrList urls;
    iterator it = begin();
    for( ; it != end(); it++ )
      if ( it->isSelected() )
	urls.append( it->item()->url().url() );

    // Multiple URLs ?
    QPixmap pixmap2;
    if ( urls.count() > 1 )
    {
      pixmap2 = *( KPixmapCache::pixmap( "kmultiple.xpm", true ) );
      if ( pixmap2.isNull() )
	warning("KDesktop: Could not find kmultiple pixmap\n");
    }

    // Calculate hotspot
    QPoint hotspot;

    // Do not handle and more mouse move or mouse release events
    m_pressed = false;

    QUriDrag *d = new QUriDrag( urls, viewport() );
    if ( pixmap2.isNull() )
    {
      hotspot.setX( m_pressedItem->pixmap( 0 )->width() / 2 );
      hotspot.setY( m_pressedItem->pixmap( 0 )->height() / 2 );
      d->setPixmap( *(m_pressedItem->pixmap( 0 )), hotspot );
    }
    else
    {
      hotspot.setX( pixmap2.width() / 2 );
      hotspot.setY( pixmap2.height() / 2 );
      d->setPixmap( pixmap2, hotspot );
    }

    d->drag();
  }
}

bool KonqKfmTreeView::isSingleClickArea( const QPoint& _point )
{
  if ( itemAt( _point ) )
  {
    int x = _point.x();
    int pos = header()->mapToActual( 0 );
    int offset = 0;
    int width = columnWidth( pos );

    for ( int index = 0; index < pos; index++ )
      offset += columnWidth( index );

    return ( x > offset && x < ( offset + width ) );
  }

  return false;
}

void KonqKfmTreeView::slotOnItem( KfmTreeViewItem* _item)
{
  QString s;
  if ( _item )
    s = _item->item()->getStatusBarInfo();
  CORBA::WString_var ws = Q2C( s );
  SIGNAL_CALL1( "setStatusBarText", CORBA::Any::from_wstring( ws.out(), 0 ) );
}

void KonqKfmTreeView::selectedItems( QValueList<KfmTreeViewItem*>& _list )
{
  iterator it = begin();
  for( ; it != end(); it++ )
    if ( it->isSelected() )
      _list.append( &*it );
}

void KonqKfmTreeView::slotReturnPressed( QListViewItem *_item )
{
  if ( !_item )
    return;

  KFileItem *item = ((KfmTreeViewItem*)_item)->item();
  mode_t mode = item->mode();

  //execute only if item is a file (or a symlink to a file)
  if ( S_ISREG( mode ) )
    openURLRequest( item->url().url().ascii() );
}

void KonqKfmTreeView::slotRightButtonPressed( QListViewItem *_item, const QPoint &_global, int )
{
  if ( _item && !_item->isSelected() )
  {
    // Deselect all the others
    iterator it = begin();
    for( ; it != end(); ++it )
      if ( it->isSelected() )
	QListView::setSelected( &*it, false );
    QListView::setSelected( _item, true );
  }

  if ( !_item )
  {
    // Popup menu for m_url
    // Torben: I think this is impossible in the treeview, or ?
    //         Perhaps if the list is smaller than the window height.
  }
  else
  {
    popupMenu( _global );
  }
}

void KonqKfmTreeView::popupMenu( const QPoint& _global )
{
  if ( !m_pMainView ) return;
  
  QStringList lstPopupURLs;

  QValueList<KfmTreeViewItem*> items;
  selectedItems( items );
  mode_t mode = 0;
  bool first = true;
  QValueList<KfmTreeViewItem*>::Iterator it = items.begin();
  int i = 0;
  for( ; it != items.end(); ++it ) {
    lstPopupURLs.append( (*it)->item()->url().url() );

    if ( first ) {
      mode = (*it)->item()->mode();
      first = false;
    } else if ( mode != (*it)->item()->mode() ) // modes are different
      mode = 0; // reset to 0
  }

  m_pMainView->popupMenu( _global, lstPopupURLs, mode );
}

void KonqKfmTreeView::openURL( const char *_url, int xOffset, int yOffset )
{
  kdebug(0, 1202, "KonqKfmTreeView::openURL( %s, %d, %d )", _url, xOffset, yOffset );
  bool isNewProtocol = false;

  KURL url( _url );
  if ( url.isMalformed() )
  {
    QString tmp = i18n( "Malformed URL\n%1" ).arg( _url );
    QMessageBox::critical( this, i18n( "Error" ), tmp, i18n( "OK" ) );
    return;
  }

  //test if we are switching to a new protocol
  if ( m_dirLister )
  {
    if ( strcmp( m_dirLister->kurl().protocol(), url.protocol() ) != 0 )
      isNewProtocol = true;
  }

  // The first time or new protocol ? So create the columns first
  if ( m_iColumns == -1 || isNewProtocol )
  {
    QStringList listing = KProtocolManager::self().listing( url.protocol() );
    if ( listing.isEmpty() )
    {
      QString tmp = i18n( "Unknown Protocol %1" ).arg( url.protocol());
      QMessageBox::critical( this, i18n( "Error" ), tmp, i18n( "OK" ) );
      return;
    }

    if ( m_iColumns == -1 )
      m_iColumns = 0;

    int currentColumn = 0;

    QStringList::Iterator it = listing.begin();
    for( ; it != listing.end(); it++ )
    {	
      if ( currentColumn > m_iColumns - 1 )
      {
	addColumn( *it );
	m_iColumns++;
	currentColumn++;
      }
      else
      {
	setColumnText( currentColumn, *it );
	currentColumn++;
      }
    }

    while ( currentColumn < m_iColumns )
      setColumnText( currentColumn++, "" );
  }

  if ( !m_dirLister )
  {
    // Create the directory lister
    m_dirLister = new KDirLister();

    QObject::connect( m_dirLister, SIGNAL( started( const QString & ) ), 
                      this, SLOT( slotStarted( const QString & ) ) );
    QObject::connect( m_dirLister, SIGNAL( completed() ), this, SLOT( slotCompleted() ) );
    QObject::connect( m_dirLister, SIGNAL( canceled() ), this, SLOT( slotCanceled() ) );
    QObject::connect( m_dirLister, SIGNAL( update() ), this, SLOT( slotUpdate() ) );
    QObject::connect( m_dirLister, SIGNAL( clear() ), this, SLOT( slotClear() ) );
    QObject::connect( m_dirLister, SIGNAL( newItem( KFileItem * ) ), 
                      this, SLOT( slotNewItem( KFileItem * ) ) );
    QObject::connect( m_dirLister, SIGNAL( deleteItem( KFileItem * ) ), 
                      this, SLOT( slotDeleteItem( KFileItem * ) ) );
  }

  m_bTopLevelComplete = false;
  m_iXOffset = xOffset;
  m_iYOffset = yOffset;

  // Start the directory lister !
  m_dirLister->openURL( url, m_pProps->m_bShowDot, false /* new url */ );
  // Note : we don't store the url. KDirLister does it for us.
}

void KonqKfmTreeView::setComplete()
{
  if ( m_pWorkingDir )
  {
    m_bSubFolderComplete = true;
    m_pWorkingDir->setComplete( true );
    m_pWorkingDir = 0L;
  }
  else
  {
    m_bTopLevelComplete = true;
    setContentsPos( m_iXOffset, m_iYOffset );
  }
}

void KonqKfmTreeView::slotStarted( const QString & url )
{
  SIGNAL_CALL2( "started", id(), CORBA::Any::from_string( (char*)url.ascii(), 0 ) );
}

void KonqKfmTreeView::slotCompleted()
{
  setComplete();
  SIGNAL_CALL1( "completed", id() );
}

void KonqKfmTreeView::slotCanceled()
{
  setComplete();
  SIGNAL_CALL1( "canceled", id() );
}

void KonqKfmTreeView::slotUpdate()
{
  kdebug( KDEBUG_INFO, 1202, "KonqKfmTreeView::slotUpdate()");
  update();
  //viewport()->update();
}

void KonqKfmTreeView::slotClear()
{
  kdebug( KDEBUG_INFO, 1202, "KonqKfmTreeView::slotClear()");
  if ( !m_pWorkingDir )
    clear();
}
  
void KonqKfmTreeView::slotNewItem( KFileItem * _fileitem )
{
  kdebug( KDEBUG_INFO, 1202, "KonqKfmTreeView::slotNewItem(...)");
  bool isdir = S_ISDIR( _fileitem->mode() );

  if ( m_pWorkingDir ) { // adding under a directory item
    if ( isdir )
      new KfmTreeViewDir( this, m_pWorkingDir, _fileitem );
    else
      new KfmTreeViewItem( this, m_pWorkingDir, _fileitem );
  } else { // adding on the toplevel
    if ( isdir )
      new KfmTreeViewDir( this, _fileitem );
    else
      new KfmTreeViewItem( this, _fileitem );
  }
}

void KonqKfmTreeView::slotDeleteItem( KFileItem * )
{
  //TODO
}

void KonqKfmTreeView::openSubFolder( const KURL &_url, KfmTreeViewDir* _dir )
{
  if ( !m_bTopLevelComplete )
  {
    // TODO: Give a warning
    kdebug(0,1202,"Still waiting for toplevel directory");
    return;
  }

  // If we are opening another sub folder right now, stop it.
  if ( !m_bSubFolderComplete )
  {
    m_dirLister->stop();
  }

  /** Debug code **/
  assert( m_iColumns != -1 && m_dirLister );
  if ( strcmp( m_dirLister->kurl().protocol(), _url.protocol() ) != 0 )
    assert( 0 ); // not same protocol as parent dir -> abort
  /** End Debug code **/

  assert( m_bSubFolderComplete && !m_pWorkingDir );

  m_bSubFolderComplete = false;
  m_pWorkingDir = _dir;
  m_dirLister->openURL( _url, m_pProps->m_bShowDot, true /* keep existing data */ );
}

KonqKfmTreeView::iterator& KonqKfmTreeView::iterator::operator++()
{
  if ( !m_p ) return *this;
  KfmTreeViewItem *i = (KfmTreeViewItem*)m_p->firstChild();
  if ( i )
  {
    m_p = i;
    return *this;
  }
  i = (KfmTreeViewItem*)m_p->nextSibling();
  if ( i )
  {
    m_p = i;
    return *this;
  }
  m_p = (KfmTreeViewItem*)m_p->parent();
  if ( m_p )
    m_p = (KfmTreeViewItem*)m_p->nextSibling();

  return *this;
}

KonqKfmTreeView::iterator KonqKfmTreeView::iterator::operator++(int)
{
  KonqKfmTreeView::iterator it = *this;
  if ( !m_p ) return it;
  KfmTreeViewItem *i = (KfmTreeViewItem*)m_p->firstChild();
  if ( i )
  {
    m_p = i;
    return it;
  }
  i = (KfmTreeViewItem*)m_p->nextSibling();
  if ( i )
  {
    m_p = i;
    return it;
  }
  m_p = (KfmTreeViewItem*)m_p->parent();
  if ( m_p )
    m_p = (KfmTreeViewItem*)m_p->nextSibling();

  return it;
}

/*
void KonqKfmTreeView::updateDirectory()
{
  if ( !m_bTopLevelComplete || !m_bSubFolderComplete )
    return;

  updateDirectory( 0L, m_strURL.data() );
}

void KonqKfmTreeView::updateDirectory( KfmTreeViewDir *_dir, const char *_url )
{
  if ( _dir )
    m_bSubFolderComplete = false;
  else
    m_bTopLevelComplete = false;

  m_pWorkingDir = _dir;

  SIGNAL_CALL2( "started", id(), CORBA::Any::from_string( (char *)0L, 0 ) );
}

void KonqKfmTreeView::slotUpdateFinished( int _id )
{
  if ( m_pWorkingDir )
    m_bSubFolderComplete = true;
  else
    m_bTopLevelComplete = true;

  // Unmark all items
  QListViewItem *item;
  if ( m_pWorkingDir )
    item = m_pWorkingDir->firstChild();
  else
    item = firstChild();
  while( item )
  {
    ((KfmTreeViewItem*)item)->unmark();
    item = item->nextSibling();
  }

  QValueList<UDSEntry>::Iterator it = m_buffer.begin();
  for( ; it != m_buffer.end(); it++ ) {
    int isdir = -1;
    QString name;

    // Find out wether it is a directory
    UDSEntry::Iterator it2 = (*it).begin();
    for( ; it2 != (*it).end(); it2++ )  {
      if ( (*it2).m_uds == UDS_FILE_TYPE ) {
	mode_t mode = (mode_t)((*it2).m_long);
	if ( S_ISDIR( mode ) )
	  isdir = 1;
	else
	  isdir = 0;
      }  else if ( (*it2).m_uds == UDS_NAME ) {
	name = (*it2).m_str;
      }
    }

    assert( isdir != -1 && !name.isEmpty() );

    if ( m_isShowingDotFiles || name[0]!='.' ) {
      // Find this icon
      bool done = false;
      QListViewItem *item;
      if ( m_pWorkingDir )
        item = m_pWorkingDir->firstChild();
      else
        item = firstChild();
      while( item ) {
        if ( name == ((KfmTreeViewItem*)item)->getText().ascii() ) {
          ((KfmTreeViewItem*)item)->mark();
          done = true;
        }
        item = item->nextSibling();
      }

      if ( !done ) {
        kdebug(0,1202,"Inserting %s", name.ascii());
        KfmTreeViewItem *item;
        if ( m_pWorkingDir ) {
          KURL u( m_workingURL );
          u.addPath( name.ascii() );
          kdebug(0,1202,"Final path 1 '%s'", u.path().data());
          if ( isdir )
            item = new KfmTreeViewDir( this, m_pWorkingDir, *it, u );
          else
            item = new KfmTreeViewItem( this, m_pWorkingDir, *it, u );
        } else {
          KURL u( m_url );
          u.addPath( name.ascii() );
          kdebug(0,1202,"Final path 2 '%s'", u.path().data());
          if ( isdir )
            item = new KfmTreeViewDir( this, *it, u );
          else
            item = new KfmTreeViewItem( this, *it, u );
        }
        item->mark();
      }
    }
  }

  // Find all unmarked items and delete them
  QList<QListViewItem> lst;

  if ( m_pWorkingDir )
    item = m_pWorkingDir->firstChild();
  else
    item = firstChild();
  while( item )
  {
    if ( !((KfmTreeViewItem*)item)->isMarked() )
    {
      kdebug(0,1202,"Removing %s", ((KfmTreeViewItem*)item)->getText().data());
      lst.append( item );
    }
    item = item->nextSibling();
  }

  QListViewItem* qlvi;
  for( qlvi = lst.first(); qlvi != 0L; qlvi = lst.next() )
    delete qlvi;

  m_buffer.clear();
  m_pWorkingDir = 0L;

  SIGNAL_CALL1( "completed", id() );
}
*/

void KonqKfmTreeView::drawContentsOffset( QPainter* _painter, int _offsetx, int _offsety,
				    int _clipx, int _clipy, int _clipw, int _cliph )
{
  if ( !_painter )
    return;

  if ( !m_bgPixmap.isNull() )
  {
    int pw = m_bgPixmap.width();
    int ph = m_bgPixmap.height();

    int xOrigin = (_clipx/pw)*pw - _offsetx;
    int yOrigin = (_clipy/ph)*ph - _offsety;

    int rx = _clipx%pw;
    int ry = _clipy%ph;

    for ( int yp = yOrigin; yp - yOrigin < _cliph + ry; yp += ph )
    {
      for ( int xp = xOrigin; xp - xOrigin < _clipw + rx; xp += pw )
	_painter->drawPixmap( xp, yp, m_bgPixmap );
    }
  }

  QListView::drawContentsOffset( _painter, _offsetx, _offsety,
				 _clipx, _clipy, _clipw, _cliph );
}

void KonqKfmTreeView::focusInEvent( QFocusEvent* _event )
{
//  emit gotFocus();

  QListView::focusInEvent( _event );
}

/**************************************************************
 *
 * KfmTreeViewItem
 *
 **************************************************************/

KfmTreeViewItem::KfmTreeViewItem( KonqKfmTreeView *_treeview, KfmTreeViewDir * _parent, KFileItem* _fileitem )
  : QListViewItem( _parent ), m_fileitem( _fileitem )
{
  m_pTreeView = _treeview;
  init();
}

KfmTreeViewItem::KfmTreeViewItem( KonqKfmTreeView *_parent, KFileItem* _fileitem )
  : QListViewItem( _parent ), m_fileitem( _fileitem )
{
  m_pTreeView = _parent;
  init();
}

void KfmTreeViewItem::init()
{
  QPixmap *p = m_fileitem->getPixmap( true /* mini icon */ ); // determine the pixmap (KFileItem)
  if ( p ) setPixmap( 0, *p );
}

QString KfmTreeViewItem::key( int _column, bool ) const
{
  static char buffer[ 12 ];

  assert( _column < (int)m_fileitem->entry().count() );

  unsigned long uds = m_fileitem->entry()[ _column ].m_uds;

  if ( uds & UDS_STRING )
    return m_fileitem->entry()[ _column ].m_str;
  else if ( uds & UDS_LONG )
  {
    sprintf( buffer, "%08d", (int)m_fileitem->entry()[ _column ].m_long );
    return buffer;
  }
  else
    assert( 0 );
}

QString KfmTreeViewItem::text( int _column ) const
{
  //  assert( _column < (int)m_fileitem->entry().count() );

  if ( _column >= (int)m_fileitem->entry().count() )
    return "";

  const UDSAtom & atom = m_fileitem->entry()[ _column ];
  unsigned long uds = atom.m_uds;

  if ( uds == UDS_ACCESS )
    return makeAccessString( atom );
  else if ( uds == UDS_FILE_TYPE )
    return makeTypeString( atom );
  else if ( ( uds & UDS_TIME ) == UDS_TIME )
    return makeTimeString( atom );
  else if ( uds & UDS_STRING )
    return atom.m_str;
  else if ( uds & UDS_LONG )
    return makeNumericString( atom );
  else
    assert( 0 );
}

const char* KfmTreeViewItem::makeNumericString( const UDSAtom &_atom ) const
{
  static char buffer[ 100 ];
  sprintf( buffer, "%i", (int)_atom.m_long );
  return buffer;
}

const char* KfmTreeViewItem::makeTimeString( const UDSAtom &_atom ) const
{
  static char buffer[ 100 ];

  time_t t = (time_t)_atom.m_long;
  struct tm* t2 = localtime( &t );

  strftime( buffer, 100, "%c", t2 );

  return buffer;
}

QString KfmTreeViewItem::makeTypeString( const UDSAtom &_atom ) const
{
  mode_t mode = (mode_t) _atom.m_long;

  if ( S_ISLNK( mode ) )
    return i18n( "Link" );
  else if ( S_ISDIR( mode ) )
    return i18n( "Directory" );
  // TODO check for devices here, too
  else
    return i18n( "File" );
}

const char* KfmTreeViewItem::makeAccessString( const UDSAtom &_atom ) const
{
  static char buffer[ 12 ];

  mode_t mode = (mode_t) _atom.m_long;
	
  char uxbit,gxbit,oxbit;

  if ( (mode & (S_IXUSR|S_ISUID)) == (S_IXUSR|S_ISUID) )
    uxbit = 's';
  else if ( (mode & (S_IXUSR|S_ISUID)) == S_ISUID )
    uxbit = 'S';
  else if ( (mode & (S_IXUSR|S_ISUID)) == S_IXUSR )
    uxbit = 'x';
  else
    uxbit = '-';
	
  if ( (mode & (S_IXGRP|S_ISGID)) == (S_IXGRP|S_ISGID) )
    gxbit = 's';
  else if ( (mode & (S_IXGRP|S_ISGID)) == S_ISGID )
    gxbit = 'S';
  else if ( (mode & (S_IXGRP|S_ISGID)) == S_IXGRP )
    gxbit = 'x';
  else
    gxbit = '-';
	
  if ( (mode & (S_IXOTH|S_ISVTX)) == (S_IXOTH|S_ISVTX) )
    oxbit = 't';
  else if ( (mode & (S_IXOTH|S_ISVTX)) == S_ISVTX )
    oxbit = 'T';
  else if ( (mode & (S_IXOTH|S_ISVTX)) == S_IXOTH )
    oxbit = 'x';
  else
    oxbit = '-';

  buffer[0] = ((( mode & S_IRUSR ) == S_IRUSR ) ? 'r' : '-' );
  buffer[1] = ((( mode & S_IWUSR ) == S_IWUSR ) ? 'w' : '-' );
  buffer[2] = uxbit;
  buffer[3] = ((( mode & S_IRGRP ) == S_IRGRP ) ? 'r' : '-' );
  buffer[4] = ((( mode & S_IWGRP ) == S_IWGRP ) ? 'w' : '-' );
  buffer[5] = gxbit;
  buffer[6] = ((( mode & S_IROTH ) == S_IROTH ) ? 'r' : '-' );
  buffer[7] = ((( mode & S_IWOTH ) == S_IWOTH ) ? 'w' : '-' );
  buffer[8] = oxbit;
  buffer[9] = 0;

  return buffer;
}

void KfmTreeViewItem::paintCell( QPainter *_painter, const QColorGroup & cg, int column, int width, int alignment )
{
  if ( m_pTreeView->m_mouseMode == SingleClick &&
       m_pTreeView->m_underlineLink && column == 0)
  {
    QFont f = _painter->font();
    f.setUnderline( true );
    _painter->setFont( f );
  }
  QListViewItem::paintCell( _painter, cg, column, width, alignment );
}

/**************************************************************
 *
 * KfmTreeViewDir
 *
 **************************************************************/

KfmTreeViewDir::KfmTreeViewDir( KonqKfmTreeView *_parent, KFileItem* _fileitem )
  : KfmTreeViewItem( _parent, _fileitem )
{
  m_pTreeView->addSubDir( _fileitem->url(), this );

  m_bComplete = false;
}

KfmTreeViewDir::KfmTreeViewDir( KonqKfmTreeView *_treeview, KfmTreeViewDir * _parent, KFileItem* _fileitem )
  : KfmTreeViewItem( _treeview, _parent, _fileitem )
{
  m_pTreeView->addSubDir( _fileitem->url(), this );

  m_bComplete = false;
}

KfmTreeViewDir::~KfmTreeViewDir()
{
  if ( m_pTreeView )
    m_pTreeView->removeSubDir( m_fileitem->url() );
}

void KfmTreeViewDir::setup()
{
  setExpandable( true );
  QListViewItem::setup();
}

void KfmTreeViewDir::setOpen( bool _open )
{
  if ( _open && !m_bComplete ) // complete it before opening
    m_pTreeView->openSubFolder( m_fileitem->url(), this );

  QListViewItem::setOpen( _open );
}

QString KfmTreeViewDir::url( int _trailing )
{
  return m_fileitem->url().url( _trailing );
}

#include "konq_treeview.moc"

