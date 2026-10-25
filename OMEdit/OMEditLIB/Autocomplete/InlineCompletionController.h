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

#ifndef INLINECOMPLETIONCONTROLLER_H
#define INLINECOMPLETIONCONTROLLER_H

#include "LocalCompletionSettings.h"
#include <QObject>
#include <QTimer>

class QPlainTextEdit;
class QCompleter;
class QShortcut;
class QKeyEvent;
class InlineCompletionPreview;
class LocalCompletionClient;

class InlineCompletionController : public QObject
{
  Q_OBJECT
public:
  explicit InlineCompletionController(QPlainTextEdit *pEditor, QCompleter *pCompleter, QObject *pParent = nullptr);
  ~InlineCompletionController() override;
  void setSettings(const LocalCompletionSettings &settings);
  InlineCompletionPreview *preview() const { return mpPreview; }
public slots:
  void requestCompletion();
  void dismiss();
protected:
  bool eventFilter(QObject *pObject, QEvent *pEvent) override;
private:
  bool eligible() const;
  bool automaticEligible() const;
  bool isAcceptanceKey(const QKeyEvent *pEvent) const;
  bool snapshotValid() const;
  void accept();
  QPlainTextEdit *mpEditor;
  QCompleter *mpCompleter;
  InlineCompletionPreview *mpPreview;
  LocalCompletionClient *mpClient;
  QShortcut *mpShortcut;
  LocalCompletionSettings mSettings;
  QTimer mDebounce;
  quint64 mRequestId = 0;
  int mRevision = -1;
  int mPosition = -1;
  bool mTyping = false;
  bool mTypingChanged = false;
  bool mAccepting = false;
};

#endif
