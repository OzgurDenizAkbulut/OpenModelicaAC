/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-2026, Open Source Modelica Consortium (OSMC),
 * c/o Linköpings universitet, Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF AGPL VERSION 3 LICENSE OR
 * THIS OSMC PUBLIC LICENSE (OSMC-PL) VERSION 1.8.
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES
 * RECIPIENT'S ACCEPTANCE OF THE OSMC PUBLIC LICENSE OR THE GNU AGPL
 * VERSION 3, ACCORDING TO RECIPIENTS CHOICE.
 *
 * The OpenModelica software and the OSMC (Open Source Modelica Consortium)
 * Public License (OSMC-PL) are obtained from OSMC, either from the above
 * address, from the URLs:
 * http://www.openmodelica.org or
 * https://github.com/OpenModelica/ or
 * http://www.ida.liu.se/projects/OpenModelica,
 * and in the OpenModelica distribution.
 *
 * GNU AGPL version 3 is obtained from:
 * https://www.gnu.org/licenses/licenses.html#GPL
 *
 * This program is distributed WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE, EXCEPT AS EXPRESSLY SET FORTH
 * IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE CONDITIONS OF OSMC-PL.
 *
 * See the full OSMC Public License conditions for more details.
 *
 */

#include "InlineCompletionController.h"
#include "InlineCompletionPreview.h"
#include "LocalCompletionClient.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QScrollBar>
#include <QShortcut>

InlineCompletionController::InlineCompletionController(QPlainTextEdit *pEditor, QCompleter *pCompleter, QObject *pParent)
  : QObject(pParent), mpEditor(pEditor), mpCompleter(pCompleter),
    mpPreview(new InlineCompletionPreview(pEditor)), mpClient(new LocalCompletionClient(this)),
    mpShortcut(new QShortcut(pEditor))
{
  mDebounce.setSingleShot(true);
  mpShortcut->setContext(Qt::WidgetShortcut);
  connect(mpShortcut, &QShortcut::activated, this, &InlineCompletionController::requestCompletion);
  connect(&mDebounce, &QTimer::timeout, this, [this]() {
    if (automaticEligible()) {
      requestCompletion();
    }
  });
  pEditor->installEventFilter(this);
  pEditor->viewport()->installEventFilter(this);
  pEditor->verticalScrollBar()->installEventFilter(this);
  pEditor->horizontalScrollBar()->installEventFilter(this);
  for (QWidget *pChild : pEditor->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
    if (pChild != mpPreview) {
      pChild->installEventFilter(this);
    }
  }
  connect(pEditor, &QObject::destroyed, this, [this]() {
    mDebounce.stop();
    mpClient->cancel();
    mpEditor = nullptr;
    mpPreview = nullptr;
    mpShortcut = nullptr;
  });
  if (pCompleter) {
    pCompleter->popup()->installEventFilter(this);
  }
  connect(pEditor->verticalScrollBar(), &QScrollBar::valueChanged, this, &InlineCompletionController::dismiss);
  connect(pEditor->horizontalScrollBar(), &QScrollBar::valueChanged, this, &InlineCompletionController::dismiss);
  connect(pEditor, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
    if (!mTyping && !mAccepting) {
      dismiss();
    }
  });
  connect(pEditor, &QPlainTextEdit::selectionChanged, this, [this]() {
    if (mpEditor->textCursor().hasSelection()) {
      dismiss();
    }
  });
  connect(pEditor->document(), &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
    dismiss();
    if (mTyping && !mAccepting && (added > 0 || removed > 0)) {
      mTypingChanged = true;
    }
  });
  connect(mpClient, &LocalCompletionClient::completionReady, this, [this](quint64 id, const QString &text) {
    if (id == mRequestId && snapshotValid()) {
      mpPreview->showCompletion(mPosition, text);
    }
  });
  connect(mpClient, &LocalCompletionClient::requestFailed, this, [this](quint64 id, const QString &) {
    if (id == mRequestId) {
      dismiss();
    }
  });
}

InlineCompletionController::~InlineCompletionController()
{
  mpClient->cancel();
  delete mpShortcut;
  delete mpPreview;
}

void InlineCompletionController::setSettings(const LocalCompletionSettings &settings)
{
  dismiss();
  mSettings = settings.normalized();
  if (mpPreview) {
    mpPreview->setAppearance(mSettings.ghostOpacity, mSettings.ghostItalic);
  }
  if (mpShortcut) {
    mpShortcut->setKey(QKeySequence(mSettings.shortcut));
    mpShortcut->setEnabled(mSettings.enabled);
  }
}

bool InlineCompletionController::eligible() const
{
  return mpEditor && mSettings.enabled && mpEditor->hasFocus() && !mpEditor->isReadOnly()
      && !mpEditor->textCursor().hasSelection()
      && (!mpCompleter || !mpCompleter->popup()->isVisible());
}

bool InlineCompletionController::snapshotValid() const
{
  return eligible() && mpEditor->document()->revision() == mRevision
      && mpEditor->textCursor().position() == mPosition;
}

bool InlineCompletionController::automaticEligible() const
{
  if (!mSettings.automatic || !eligible()) {
    return false;
  }
  const int position = mpEditor->textCursor().position();
  for (int offset = 1; offset <= mSettings.minimumPrefixLength; ++offset) {
    if (position < offset) {
      return false;
    }
    const QChar character = mpEditor->document()->characterAt(position - offset);
    if (!character.isLetterOrNumber() && character != QLatin1Char('_')) {
      return false;
    }
  }
  return true;
}

bool InlineCompletionController::isAcceptanceKey(const QKeyEvent *pEvent) const
{
  if (mSettings.acceptShortcut == QStringLiteral("Ctrl+Right")) {
    return pEvent->key() == Qt::Key_Right && pEvent->modifiers() == Qt::ControlModifier;
  }
  if (mSettings.acceptShortcut == QStringLiteral("Ctrl+Return")) {
    return pEvent->key() == Qt::Key_Return && pEvent->modifiers() == Qt::ControlModifier;
  }
  return pEvent->key() == Qt::Key_Tab && pEvent->modifiers() == Qt::NoModifier;
}

void InlineCompletionController::dismiss()
{
  mDebounce.stop();
  mRequestId = 0;
  mpClient->cancel();
  if (mpPreview) {
    mpPreview->dismiss();
  }
}

void InlineCompletionController::requestCompletion()
{
  dismiss();
  if (!eligible()) {
    return;
  }
  mPosition = mpEditor->textCursor().position();
  mRevision = mpEditor->document()->revision();
  const QString text = mpEditor->toPlainText();
  mRequestId = mpClient->requestCompletion(text.left(mPosition).right(mSettings.prefixCharacters),
                                         text.mid(mPosition, mSettings.suffixCharacters), mSettings);
}

void InlineCompletionController::accept()
{
  if (!snapshotValid() || !mpPreview->isVisible()) {
    dismiss();
    return;
  }
  const QString text = mpPreview->completion();
  dismiss();
  mAccepting = true;
  QTextCursor cursor = mpEditor->textCursor();
  cursor.beginEditBlock();
  cursor.insertText(text);
  cursor.endEditBlock();
  mpEditor->setTextCursor(cursor);
  mAccepting = false;
}

bool InlineCompletionController::eventFilter(QObject *pObject, QEvent *pEvent)
{
  if (!mSettings.enabled || !mpEditor) {
    return QObject::eventFilter(pObject, pEvent);
  }
  if ((pEvent->type() == QEvent::MouseButtonPress || pEvent->type() == QEvent::MouseButtonDblClick)
      && mpPreview->isVisible()) {
    dismiss();
    return true;
  }
  if (mpCompleter && pObject == mpCompleter->popup() && pEvent->type() == QEvent::Show) {
    dismiss();
  }
  if (pObject == mpEditor && pEvent->type() == QEvent::ShortcutOverride && mpPreview->isVisible()
      && snapshotValid() && isAcceptanceKey(static_cast<QKeyEvent *>(pEvent))) {
    pEvent->accept();
    return true;
  }
  if (pObject == mpEditor && pEvent->type() == QEvent::KeyPress) {
    QKeyEvent *pKey = static_cast<QKeyEvent *>(pEvent);
    if (mpCompleter && mpCompleter->popup()->isVisible()) {
      dismiss();
      return false;
    }
    if (pKey->key() == Qt::Key_Control || pKey->key() == Qt::Key_Shift || pKey->key() == Qt::Key_Alt
        || pKey->key() == Qt::Key_Meta || pKey->key() == Qt::Key_AltGr) {
      return false;
    }
    if (pKey->key() == Qt::Key_Escape) {
      const bool visible = mpPreview->isVisible();
      dismiss();
      return visible;
    }
    if (isAcceptanceKey(pKey) && mpPreview->isVisible()) {
      accept();
      return true;
    }
    dismiss();
    const bool deletion = pKey->key() == Qt::Key_Backspace || pKey->key() == Qt::Key_Delete;
    const bool typing = (deletion ? mSettings.triggerAfterDeletion : !pKey->text().isEmpty())
        && !(pKey->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && pKey->key() != Qt::Key_Tab;
    // Deliver synchronously so only edits made by this physical key can schedule work.
    if (typing && !mTyping) {
      mTyping = true;
      mTypingChanged = false;
      mpEditor->removeEventFilter(this);
      QCoreApplication::sendEvent(mpEditor, pEvent);
      mpEditor->installEventFilter(this);
      mTyping = false;
      if (mTypingChanged && automaticEligible()) {
        mDebounce.start(mSettings.debounceMs);
      }
      return true;
    }
  }
  switch (pEvent->type()) {
    case QEvent::FocusOut:
    case QEvent::Hide:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::ToolTip:
    case QEvent::Wheel:
    case QEvent::Resize:
    case QEvent::FontChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ReadOnlyChange:
    case QEvent::InputMethod:
      dismiss();
      break;
    default:
      break;
  }
  return QObject::eventFilter(pObject, pEvent);
}
