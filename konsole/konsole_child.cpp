/* ----------------------------------------------------------------------- *
 * [konsole_child.cpp]           Konsole                                   *
 * ----------------------------------------------------------------------- *
 * This file is part of Konsole, an X terminal.                            *
 *                                                                         *
 * Copyright (c) 2002 by Stephan Binner <binner@kde.org>                   *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <kglobalsettings.h>
#include <kinputdialog.h>
#include <qwmatrix.h>

#include "konsole_child.h"
#include "konsole.h"
#include <netwm.h>

extern bool argb_visual; // declared in main.cpp and konsole_part.cpp

KonsoleChild::KonsoleChild(Konsole* parent, TESession* _se, int columns, int lines, int scrollbar_location, int frame_style,
                           ColorSchema* _schema,QFont font,int bellmode,QString wordcharacters,
                           bool blinkingCursor, bool ctrlDrag, bool terminalSizeHint, int lineSpacing,
                           bool cutToBeginningOfLine, bool _allowResize, bool _fixedSize):KMainWindow(parent)
,session_terminated(false)
,wallpaperSource(0)
,se(_se)
,schema(_schema)
,allowResize(_allowResize)
,b_fixedSize(_fixedSize)
,rootxpm(0)
{
  te = new TEWidget(this);
  te->setVTFont(font);

  setCentralWidget(te);

  te->setFocus();

  te->setWordCharacters(wordcharacters);
  te->setBlinkingCursor(blinkingCursor);
  te->setCtrlDrag(ctrlDrag);
  te->setTerminalSizeHint(terminalSizeHint);
  te->setTerminalSizeStartup(false);
  te->setLineSpacing(lineSpacing);
  te->setBellMode(bellmode);
  te->setMinimumSize(150,70);
  te->setCutToBeginningOfLine(cutToBeginningOfLine);
  te->setScrollbarLocation(scrollbar_location);
  te->setFrameStyle(frame_style);

  setColLin(columns, lines);

  setSchema(_schema);

  updateTitle();

  connect( se,SIGNAL(done(TESession*)),
           this,SLOT(doneSession(TESession*)) );
  connect( te, SIGNAL(configureRequest(TEWidget*, int, int, int)),
           this, SLOT(configureRequest(TEWidget*, int, int, int)) );

  connect( se,SIGNAL(updateTitle()), this,SLOT(updateTitle()) );
  connect( se,SIGNAL(renameSession(TESession*,const QString&)), this,SLOT(slotRenameSession(TESession*,const QString&)) );
  connect( se,SIGNAL(enableMasterModeConnections()), this,SLOT(enableMasterModeConnections()) );
  connect( se->getEmulation(),SIGNAL(ImageSizeChanged(int,int)), this,SLOT(notifySize(int,int)));
  connect(se->getEmulation(),SIGNAL(changeColumns(int)), this,SLOT(changeColumns(int)) );

  connect( kapp,SIGNAL(backgroundChanged(int)),this, SLOT(slotBackgroundChanged(int)));

  if (kapp->authorizeKAction("konsole_rmb"))
  {
     m_rightButton = new KPopupMenu(this);

     KActionCollection* actions = new KActionCollection(this);

     KAction* selectionEnd = new KAction(i18n("Set Selection End"), 0, te,
                                  SLOT(setSelectionEnd()), actions, "selection_end");
     selectionEnd->plug(m_rightButton);

     KAction *copyClipboard = new KAction(i18n("&Copy"), "editcopy", 0,
                                        te, SLOT(copyClipboard()), actions, "edit_copy");

     copyClipboard->plug(m_rightButton);

     KAction *pasteClipboard = new KAction(i18n("&Paste"), "editpaste", 0,
                                        te, SLOT(pasteClipboard()), actions, "edit_paste");
     pasteClipboard->plug(m_rightButton);

     // Send Signal Menu -------------------------------------------------------------
     if (kapp->authorizeKAction("send_signal"))
     {
        KPopupMenu* m_signals = new KPopupMenu(this);
        m_signals->insertItem( i18n( "&Suspend Task" )   + " (STOP)", SIGSTOP);
        m_signals->insertItem( i18n( "&Continue Task" )  + " (CONT)", SIGCONT);
        m_signals->insertItem( i18n( "&Hangup" )         + " (HUP)", SIGHUP);
        m_signals->insertItem( i18n( "&Interrupt Task" ) + " (INT)", SIGINT);
        m_signals->insertItem( i18n( "&Terminate Task" ) + " (TERM)", SIGTERM);
        m_signals->insertItem( i18n( "&Kill Task" )      + " (KILL)", SIGKILL);
        m_signals->insertItem( i18n( "User Signal &1")   + " (USR1)", SIGUSR1);
        m_signals->insertItem( i18n( "User Signal &2")   + " (USR2)", SIGUSR2);
        connect(m_signals, SIGNAL(activated(int)), SLOT(sendSignal(int)));
        m_rightButton->insertItem(i18n("&Send Signal"), m_signals);
     }

     m_rightButton->insertSeparator();

     KAction *attachSession = new KAction(i18n("&Attach Session"), 0, this,
                                       SLOT(attachSession()), actions, "attach_session");
     attachSession->plug(m_rightButton);
     KAction *renameSession = new KAction(i18n("&Rename Session..."), 0, this,
                                       SLOT(renameSession()), actions, "rename_session");
     renameSession->plug(m_rightButton);

     m_rightButton->insertSeparator();
     KAction *closeSession = new KAction(i18n("C&lose Session"), "fileclose", 0, this,
                                      SLOT(closeSession()), actions, "close_session");
     closeSession->plug(m_rightButton );

     if (KGlobalSettings::insertTearOffHandle())
        m_rightButton->insertTearOffHandle();
  }
}

void KonsoleChild::run() {
   se->changeWidget(te);
   se->setConnect(true);

   kWinModule = new KWinModule();
   connect( kWinModule,SIGNAL(currentDesktopChanged(int)), this,SLOT(currentDesktopChanged(int)) );
}

void KonsoleChild::setSchema(ColorSchema* _schema) {
  schema=_schema;
  if (schema) {
    te->setColorTable(schema->table()); //FIXME: set twice here to work around a bug
    if (schema->useTransparency()) {
      if (!argb_visual) {
        if (!rootxpm)
          rootxpm = new KRootPixmap(te);
        rootxpm->setFadeEffect(schema->tr_x(), QColor(schema->tr_r(), schema->tr_g(), schema->tr_b()));
        rootxpm->start();
      } else {
        te->setBlendColor(qRgba(schema->tr_r(), schema->tr_g(),
                                schema->tr_b(), int(schema->tr_x() * 255)));
        te->setErasePixmap( QPixmap() ); // make sure any background pixmap is unset
      } 
    } else {
      if (rootxpm) {
        rootxpm->stop();
        delete rootxpm;
        rootxpm=0;
      }
      pixmap_menu_activated(schema->alignment(),schema->imagePath());
      te->setBlendColor(qRgba(0, 0, 0, 0xff));
    }
    te->setColorTable(schema->table());
  }
}

void KonsoleChild::updateTitle()
{
  setCaption( se->fullTitle() );
  setIconText( se->IconText() );
}

void KonsoleChild::slotRenameSession(TESession*, const QString &)
{
  updateTitle();
}

void KonsoleChild::enableMasterModeConnections()
{
  se->setListenToKeyPress(true);
}

void KonsoleChild::setColLin(int columns, int lines)
{
  if ((columns==0) || (lines==0))
  {
    columns = 80;
    lines = 24;
  }

  if (b_fixedSize)
    te->setFixedSize(columns, lines);
  else
    te->setSize(columns, lines);
  adjustSize();
  if (b_fixedSize)
    setFixedSize(sizeHint());
  if (schema && schema->alignment() >= 3)
    pixmap_menu_activated(schema->alignment(),schema->imagePath());
}

void KonsoleChild::changeColumns(int columns)
{
  if (allowResize) {
    setColLin(columns,te->Lines());
    te->update();
  }
}

void KonsoleChild::notifySize(int /*lines*/, int /*columns*/)
{
  if (schema && schema->alignment() >= 3)
    pixmap_menu_activated(schema->alignment(),schema->imagePath());
}

KonsoleChild::~KonsoleChild()
{
  disconnect( se->getEmulation(),SIGNAL(ImageSizeChanged(int,int)), this,SLOT(notifySize(int,int)));
  se->setConnect(false);

  if (session_terminated) {
    delete te;
    delete se;
    se=NULL;
    emit doneChild(this,NULL);
  }
  else {
    TEWidget* old_te=te;
    emit doneChild(this,se);
    delete old_te;
  }

  if( kWinModule )
    delete kWinModule;
  kWinModule = 0;
}

void KonsoleChild::configureRequest(TEWidget* _te, int, int x, int y)
{
  if (m_rightButton)
     m_rightButton->popup(_te->mapToGlobal(QPoint(x,y)));
}

void KonsoleChild::doneSession(TESession*)
{
  se->setConnect(false);
  session_terminated=true;
  delete this;
}

void KonsoleChild::sendSignal(int sn)
{
  se->sendSignal(sn);
}

void KonsoleChild::attachSession()
{
  delete this;
}

void KonsoleChild::renameSession() {
  QString name = se->Title();
  bool ok;

  name = KInputDialog::getText( i18n( "Rename Session" ),
      i18n( "Session name:" ), name, &ok, this );

  if (ok) {
    se->setTitle(name);
    updateTitle();
  }
}

void KonsoleChild::closeSession()
{
  se->closeSession();
}

void KonsoleChild::pixmap_menu_activated(int item,QString pmPath)
{
  if (item <= 1) pmPath = "";
  QPixmap pm(pmPath);
  if (pm.isNull()) {
    pmPath = "";
    item = 1;
    te->setBackgroundColor(te->getDefaultBackColor());
    return;
  }
  // FIXME: respect scrollbar (instead of te->size)
  switch (item)
  {
    case 1: // none
    case 2: // tile
            te->setBackgroundPixmap(pm);
    break;
    case 3: // center
            { QPixmap bgPixmap;
              bgPixmap.resize(te->size());
              bgPixmap.fill(te->getDefaultBackColor());
              bitBlt( &bgPixmap, ( te->size().width() - pm.width() ) / 2,
                                ( te->size().height() - pm.height() ) / 2,
                      &pm, 0, 0,
                      pm.width(), pm.height() );

              te->setBackgroundPixmap(bgPixmap);
            }
    break;
    case 4: // full
            {
              float sx = (float)te->size().width() / pm.width();
              float sy = (float)te->size().height() / pm.height();
              QWMatrix matrix;
              matrix.scale( sx, sy );
              te->setBackgroundPixmap(pm.xForm( matrix ));
            }
    break;
  }
}

void KonsoleChild::slotBackgroundChanged(int desk)
{
  // Only update rootxpm if window is visible on current desktop
  NETWinInfo info( qt_xdisplay(), winId(), qt_xrootwin(), NET::WMDesktop );

  if (rootxpm && info.desktop()==desk) {
    //KONSOLEDEBUG << "Wallpaper changed on my desktop, " << desk << ", repainting..." << endl;
    //Check to see if we are on the current desktop. If not, delay the repaint
    //by setting wallpaperSource to 0. Next time our desktop is selected, we will
    //automatically update because we are saying "I don't have the current wallpaper"
    NETRootInfo rootInfo( qt_xdisplay(), NET::CurrentDesktop );
    rootInfo.activate();
    if( rootInfo.currentDesktop() == info.desktop() ) {
       //We are on the current desktop, go ahead and update
       //KONSOLEDEBUG << "My desktop is current, updating..." << endl;
       wallpaperSource = desk;
       rootxpm->repaint(true);
    }
    else {
       //We are not on the current desktop, mark our wallpaper source 'stale'
       //KONSOLEDEBUG << "My desktop is NOT current, delaying update..." << endl;
       wallpaperSource = 0;
    }
  }
}

void KonsoleChild::currentDesktopChanged(int desk) {
   //Get window info
   NETWinInfo info( qt_xdisplay(), winId(), qt_xrootwin(), NET::WMDesktop );
   bool bNeedUpdate = false;

   if( info.desktop()==NETWinInfo::OnAllDesktops ) {
      //This is a sticky window so it will always need updating
      bNeedUpdate = true;
   }
   else if( (info.desktop() == desk) && (wallpaperSource != desk) ) {
      bNeedUpdate = true;
   }
   else {
      //We are not sticky and already have the wallpaper for our desktop
      return;
   }

   //This window is transparent, update the root pixmap
   if( bNeedUpdate && rootxpm ) {
      wallpaperSource = desk;
      rootxpm->repaint(true);
   }
}

#include "konsole_child.moc"
