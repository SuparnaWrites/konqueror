/*  This file is part of the KDE project
    Copyright (C) 1999 Simon Hausmann <hausmann@kde.org>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 
*/ 

#include "enginecfg.h"
#include "main.h"

#include <kconfig.h>
#include <klibglobal.h>

EngineCfg *EngineCfg::s_pSelf = 0L;

EngineCfg::EngineCfg()
{
  KConfig *config = KonqSearcherFactory::global()->config();
  config->setGroup( "General" );
  
  QStringList engines = config->readListEntry( "SearchEngines" );
  
  QStringList::ConstIterator gIt = engines.begin();
  QStringList::ConstIterator gEnd = engines.end();
  for (; gIt != gEnd; ++gIt )
  {
    config->setGroup( *gIt );
    
    Entry e;
    e.m_strName = *gIt;
    e.m_lstKeys = config->readListEntry( "Keys" );
    e.m_strQuery = config->readEntry( "Query" );
    
    m_lstSearchEngines.append( e );
  }
}

void EngineCfg::saveEngine( Entry e )
{
  QValueList<Entry>::Iterator it = m_lstSearchEngines.begin();
  QValueList<Entry>::Iterator end = m_lstSearchEngines.end();
  for (; it != end; ++it )
    if ( (*it).m_strName == e.m_strName )
    {
      m_lstSearchEngines.remove( it );
      break;
    }

  m_lstSearchEngines.append( e );
  
  saveConfig();
}

void EngineCfg::removeEngine( const QString &name )
{
  QValueList<Entry>::Iterator it = m_lstSearchEngines.begin();
  QValueList<Entry>::Iterator end = m_lstSearchEngines.end();
  for (; it != end; ++it )
    if ( (*it).m_strName == name )
    {
      m_lstSearchEngines.remove( it );
      break;
    }

  saveConfig();
}  

QString EngineCfg::query( const QString &key )
{
  QValueList<Entry>::ConstIterator it = m_lstSearchEngines.begin();
  QValueList<Entry>::ConstIterator end = m_lstSearchEngines.end();
  for (; it != end; ++it )
    if ( (*it).m_lstKeys.contains( key ) )
      return (*it).m_strQuery;

  return QString::null;
}

EngineCfg::Entry EngineCfg::entryByName( const QString &name )
{
  QValueList<Entry>::ConstIterator it = m_lstSearchEngines.begin();
  QValueList<Entry>::ConstIterator end = m_lstSearchEngines.end();
  for (; it != end; ++it )
    if ( (*it).m_strName == name )
      return *it;

  return Entry();
}

EngineCfg* EngineCfg::self()
{
  if ( !s_pSelf )
    s_pSelf = new EngineCfg;
    
  return s_pSelf;
}

void EngineCfg::saveConfig()
{
  KConfig *config = KonqSearcherFactory::global()->config();

  QStringList engines;

  QValueList<Entry>::ConstIterator it = m_lstSearchEngines.begin();
  QValueList<Entry>::ConstIterator end = m_lstSearchEngines.end();
  for (; it != end; ++it )
  {
    engines.append( (*it).m_strName );
    config->setGroup( (*it).m_strName );
    config->writeEntry( "Keys", (*it).m_lstKeys );
    config->writeEntry( "Query", (*it).m_strQuery );
  }
  
  config->setGroup( "General" );
  config->writeEntry( "SearchEngines", engines );
  config->sync();
}

