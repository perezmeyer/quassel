/***************************************************************************
 *   Copyright (C) 2005-09 by the Quassel Project                          *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "inputwidget.h"

#include "action.h"
#include "actioncollection.h"
#include "client.h"
#include "iconloader.h"
#include "ircuser.h"
#include "jumpkeyhandler.h"
#include "networkmodel.h"
#include "qtui.h"
#include "qtuisettings.h"

InputWidget::InputWidget(QWidget *parent)
  : AbstractItemView(parent),
    _networkId(0)
{
  ui.setupUi(this);
  connect(ui.inputEdit, SIGNAL(sendText(QString)), this, SLOT(sendText(QString)));
  connect(ui.ownNick, SIGNAL(activated(QString)), this, SLOT(changeNick(QString)));
  connect(this, SIGNAL(userInput(BufferInfo, QString)), Client::instance(), SIGNAL(sendInput(BufferInfo, QString)));
  setFocusProxy(ui.inputEdit);

  ui.ownNick->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  ui.ownNick->installEventFilter(new MouseWheelFilter(this));
  ui.inputEdit->installEventFilter(new JumpKeyHandler(this));

  QtUiStyleSettings s("Fonts");
  s.notify("InputLine", this, SLOT(setFont(QVariant)));
  QFont font = s.value("InputLine", QFont()).value<QFont>();
  if(font.family().isEmpty())
    font = QApplication::font();
  setFont(font);

  ActionCollection *coll = QtUi::actionCollection();

  Action *activateInputline = coll->add<Action>("FocusInputLine");
  connect(activateInputline, SIGNAL(triggered()), SLOT(setFocus()));
  activateInputline->setText(tr("Focus Input Line"));
  activateInputline->setShortcut(tr("Ctrl+L"));
}

InputWidget::~InputWidget() {
}

void InputWidget::setCustomFont(const QVariant &v) {
  QFont font = v.value<QFont>();
  if(font.family().isEmpty())
    font = QApplication::font();
  ui.inputEdit->setFont(font);
}

void InputWidget::currentChanged(const QModelIndex &current, const QModelIndex &previous) {
  Q_UNUSED(previous)
  NetworkId networkId = current.data(NetworkModel::NetworkIdRole).value<NetworkId>();
  if(networkId == _networkId)
    return;

  setNetwork(networkId);
  updateNickSelector();
  updateEnabledState();
}

void InputWidget::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight) {
  QItemSelectionRange changedArea(topLeft, bottomRight);
  if(changedArea.contains(selectionModel()->currentIndex())) {
    updateEnabledState();
  }
};

void InputWidget::rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end) {
  NetworkId networkId;
  QModelIndex child;
  for(int row = start; row <= end; row++) {
    child = model()->index(row, 0, parent);
    if(NetworkModel::NetworkItemType != child.data(NetworkModel::ItemTypeRole).toInt())
      continue;
    networkId = child.data(NetworkModel::NetworkIdRole).value<NetworkId>();
    if(networkId == _networkId) {
      setNetwork(0);
      updateNickSelector();
      return;
    }
  }
}

void InputWidget::updateEnabledState() {
  QModelIndex currentIndex = selectionModel()->currentIndex();
  
  const Network *net = Client::networkModel()->networkByIndex(currentIndex);
  bool enabled = false;
  if(net) {
    // disable inputline if it's a channelbuffer we parted from or...
    enabled = (currentIndex.data(NetworkModel::ItemActiveRole).value<bool>() || (currentIndex.data(NetworkModel::BufferTypeRole).toInt() != BufferInfo::ChannelBuffer));
    // ... if we're not connected to the network at all
    enabled &= net->isConnected();
  }
  ui.inputEdit->setEnabled(enabled);
}

const Network *InputWidget::currentNetwork() const {
  return Client::network(_networkId);
}

BufferInfo InputWidget::currentBufferInfo() const {
  return selectionModel()->currentIndex().data(NetworkModel::BufferInfoRole).value<BufferInfo>();
};

void InputWidget::setNetwork(NetworkId networkId) {
  if(_networkId == networkId)
    return;

  const Network *previousNet = Client::network(_networkId);
  if(previousNet) {
    disconnect(previousNet, 0, this, 0);
    if(previousNet->me())
      disconnect(previousNet->me(), 0, this, 0);
  }

  _networkId = networkId;

  const Network *network = Client::network(networkId);
  if(network) {
    connect(network, SIGNAL(identitySet(IdentityId)), this, SLOT(setIdentity(IdentityId)));
    connectMyIrcUser();
    setIdentity(network->identity());
  } else {
    setIdentity(0);
    _networkId = 0;
  }
}

void InputWidget::connectMyIrcUser() {
  const Network *network = currentNetwork();
  if(network->me()) {
    connect(network->me(), SIGNAL(nickSet(const QString &)), this, SLOT(updateNickSelector()));
    connect(network->me(), SIGNAL(userModesSet(QString)), this, SLOT(updateNickSelector()));
    connect(network->me(), SIGNAL(userModesAdded(QString)), this, SLOT(updateNickSelector()));
    connect(network->me(), SIGNAL(userModesRemoved(QString)), this, SLOT(updateNickSelector()));
    connect(network->me(), SIGNAL(awaySet(bool)), this, SLOT(updateNickSelector()));
    disconnect(network, SIGNAL(myNickSet(const QString &)), this, SLOT(connectMyIrcUser()));
  } else {
    connect(network, SIGNAL(myNickSet(const QString &)), this, SLOT(connectMyIrcUser()));
  }
}

void InputWidget::setIdentity(IdentityId identityId) {
  if(_identityId == identityId)
    return;

  const Identity *previousIdentity = Client::identity(_identityId);
  if(previousIdentity)
    disconnect(previousIdentity, 0, this, 0);

  _identityId = identityId;

  const Identity *identity = Client::identity(identityId);
  if(identity) {
    connect(identity, SIGNAL(nicksSet(QStringList)),
	    this, SLOT(updateNickSelector()));
  }
  updateNickSelector();
}

void InputWidget::updateNickSelector() const {
  ui.ownNick->clear();

  const Network *net = currentNetwork();
  if(!net)
    return;

  const Identity *identity = Client::identity(net->identity());
  if(!identity) {
    qWarning() << "InputWidget::updateNickSelector(): can't find Identity for Network" << net->networkId();
    return;
  }

  int nickIdx;
  QStringList nicks = identity->nicks();
  if((nickIdx = nicks.indexOf(net->myNick())) == -1) {
    nicks.prepend(net->myNick());
    nickIdx = 0;
  }

  if(nicks.isEmpty())
    return;

  IrcUser *me = net->me();
  if(me)
    nicks[nickIdx] = net->myNick() + QString(" (+%1)").arg(me->userModes());
      
  ui.ownNick->addItems(nicks);

  if(me && me->isAway())
    ui.ownNick->setItemData(nickIdx, SmallIcon("user-away"), Qt::DecorationRole);

  ui.ownNick->setCurrentIndex(nickIdx);
}

void InputWidget::changeNick(const QString &newNick) const {
  const Network *net = currentNetwork();
  if(!net || net->isMyNick(newNick))
    return;
  emit userInput(currentBufferInfo(), QString("/nick %1").arg(newNick));
}

void InputWidget::sendText(QString text) {
  emit userInput(currentBufferInfo(), text);
}


// MOUSE WHEEL FILTER
MouseWheelFilter::MouseWheelFilter(QObject *parent)
  : QObject(parent)
{
}

bool MouseWheelFilter::eventFilter(QObject *obj, QEvent *event) {
  if(event->type() != QEvent::Wheel)
    return QObject::eventFilter(obj, event);
  else
    return true;
}
