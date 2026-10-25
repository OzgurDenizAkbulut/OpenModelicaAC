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

#ifndef LOCALCOMPLETIONCLIENT_H
#define LOCALCOMPLETIONCLIENT_H

#include "LocalCompletionSettings.h"

#include <QObject>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QStringList>

class QNetworkReply;

class LocalCompletionClient : public QObject
{
  Q_OBJECT
public:
  explicit LocalCompletionClient(QObject *parent = nullptr);
  quint64 requestCompletion(const QString &prefix, const QString &suffix, const LocalCompletionSettings &settings);
  void cancel();
  void testConnection(const LocalCompletionSettings &settings);
  void discoverModels(const LocalCompletionSettings &settings);
  static bool normalizeEndpoint(const QString &endpoint, QUrl *pEndpoint, QString *pError = nullptr);
  static QString fimPrompt(const QString &prefix, const QString &suffix, const LocalCompletionSettings &settings);

signals:
  void completionReady(quint64 requestId, const QString &text);
  void requestFailed(quint64 requestId, const QString &error);
  void connectionChecked(bool success, const QString &message);
  void modelsDiscovered(const QStringList &models);
  void modelDiscoveryFailed(const QString &message);

private:
  enum RequestKind { Completion, ConnectionTest, ModelDiscovery };
  void start(const LocalCompletionSettings &settings, const QByteArray &payload, RequestKind kind);
  void readResponse();
  void finish();
  void fail(const QString &message);
  void queueFailure(const QString &message);
  QNetworkAccessManager mNetwork;
  QPointer<QNetworkReply> mpReply;
  QTimer mDeadline;
  QByteArray mResponse;
  LocalCompletionSettings mSettings;
  quint64 mGeneration = 0;
  RequestKind mRequestKind = Completion;
};

#endif
