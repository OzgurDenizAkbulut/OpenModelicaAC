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

#include "InlineCompletionPreview.h"

#include <QScrollBar>
#include <QPainter>
#include <QTextBlock>
#include <QTextLayout>

InlineCompletionPreview::InlineCompletionPreview(QPlainTextEdit *pEditor)
  : QPlainTextEdit(pEditor->viewport()), mpEditor(pEditor)
{
  setReadOnly(true);
  setUndoRedoEnabled(false);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  hide();
}

void InlineCompletionPreview::setAppearance(int opacity, bool italic)
{
  dismiss();
  mOpacity = qBound(10, opacity, 90);
  mItalic = italic;
}

bool InlineCompletionPreview::showCompletion(int position, const QString &text)
{
  dismiss();
  if (text.isEmpty() || position < 0 || position >= mpEditor->document()->characterCount()
      || !mpEditor->document()->findBlock(position).isVisible()) {
    return false;
  }
  mPosition = position;
  mCompletion = text;
  setGeometry(mpEditor->viewport()->rect());
  setFont(mpEditor->font());
  setPalette(mpEditor->palette());
  setLineWrapMode(mpEditor->lineWrapMode());
  setWordWrapMode(mpEditor->wordWrapMode());
  setTabStopDistance(mpEditor->tabStopDistance());
  setCenterOnScroll(mpEditor->centerOnScroll());
  document()->setDefaultFont(mpEditor->document()->defaultFont());
  document()->setDefaultTextOption(mpEditor->document()->defaultTextOption());
  document()->setDocumentMargin(mpEditor->document()->documentMargin());
  setPlainText(mpEditor->toPlainText());

  // Bake highlighter formats into this independent document before insertion.
  for (QTextBlock block = mpEditor->document()->begin(); block.isValid(); block = block.next()) {
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
      QTextCursor cursor(document());
      cursor.setPosition(block.position() + range.start);
      cursor.setPosition(qMin(block.position() + range.start + range.length,
                              block.position() + block.length() - 1), QTextCursor::KeepAnchor);
      cursor.setCharFormat(range.format);
    }
  }
  QTextCursor cursor(document());
  cursor.setPosition(position);
  QTextCharFormat format;
  QColor ghostColor = mpEditor->palette().color(QPalette::Text);
  ghostColor.setAlphaF(mOpacity / 100.0);
  format.setForeground(ghostColor);
  format.setFontItalic(mItalic);
  cursor.insertText(text, format);
  setTextCursor(cursor);

  for (QTextBlock block = mpEditor->document()->begin(); block.isValid(); block = block.next()) {
    const int mapped = block.position() > position ? block.position() + text.size() : block.position();
    QTextBlock presentationBlock = document()->findBlock(mapped);
    presentationBlock.setVisible(block.isVisible());
    if (!block.isVisible()) {
      presentationBlock.setLineCount(0);
    }
  }
  document()->markContentsDirty(0, document()->characterCount());
  show();
  raise();
  verticalScrollBar()->setValue(mpEditor->verticalScrollBar()->value());
  horizontalScrollBar()->setValue(mpEditor->horizontalScrollBar()->value());
  ensureCursorVisible();
  for (int offset = 0; offset <= text.size(); ++offset) {
    QTextCursor visibleCursor(document());
    visibleCursor.setPosition(position + offset);
    if (!viewport()->rect().contains(cursorRect(visibleCursor))) {
      dismiss();
      return false;
    }
  }
  for (QWidget *pChild : mpEditor->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
    pChild->update();
  }
  return true;
}

void InlineCompletionPreview::dismiss()
{
  hide();
  mCompletion.clear();
  for (QWidget *pChild : mpEditor->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
    pChild->update();
  }
}

QRectF InlineCompletionPreview::sourceBlockRect(const QTextBlock &block) const
{
  const int position = block.position() > mPosition ? block.position() + mCompletion.size() : block.position();
  const QTextBlock presentationBlock = document()->findBlock(position);
  QRectF result = blockBoundingGeometry(presentationBlock).translated(contentOffset());
  if (block.position() <= mPosition && mPosition < block.position() + block.length()) {
    const QTextBlock last = document()->findBlock(block.position() + block.length() - 1 + mCompletion.size());
    result.setBottom(blockBoundingGeometry(last).translated(contentOffset()).bottom());
  }
  return result;
}

void InlineCompletionPreview::paintEvent(QPaintEvent *pEvent)
{
  QPlainTextEdit::paintEvent(pEvent);
  if (mpEditor->hasFocus()) {
    QTextCursor cursor(document());
    cursor.setPosition(mPosition);
    QPainter painter(viewport());
    painter.fillRect(cursorRect(cursor), palette().color(QPalette::Text));
  }
}
