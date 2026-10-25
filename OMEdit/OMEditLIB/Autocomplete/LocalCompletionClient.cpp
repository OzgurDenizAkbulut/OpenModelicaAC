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

#include "LocalCompletionClient.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {
const int responseLimit = 65536;

QString completionText(const QString &input, int maxLines)
{
  QString text = input;
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  if (text.trimmed().isEmpty() || text.contains(QStringLiteral("```"))
      || text.contains(QStringLiteral("<|")) || text.contains(QStringLiteral("|>"))) {
    return QString();
  }
  for (const QChar character : text) {
    if ((character.category() == QChar::Other_Control && character != QLatin1Char('\n')
         && character != QLatin1Char('\t')) || character.category() == QChar::Other_Format) {
      return QString();
    }
  }
  static const QRegularExpression prose(QStringLiteral("^\\s*(?:Sure\\b|Certainly\\b|Here(?:'s| is| are)\\b|The following\\b|As an AI\\b)"),
                                        QRegularExpression::CaseInsensitiveOption);
  if (prose.match(text).hasMatch()) {
    return QString();
  }
  int lines = 1;
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i) == QLatin1Char('\n') && ++lines > qBound(1, maxLines, 50)) {
      text.truncate(i);
      break;
    }
  }
  return text.trimmed().isEmpty() ? QString() : text;
}
}

LocalCompletionClient::LocalCompletionClient(QObject *parent)
  : QObject(parent), mNetwork(this)
{
  mNetwork.setProxy(QNetworkProxy::NoProxy);
  mDeadline.setSingleShot(true);
  connect(&mDeadline, &QTimer::timeout, this, [this]() { fail(tr("Local completion request timed out.")); });
}

bool LocalCompletionClient::normalizeEndpoint(const QString &endpoint, QUrl *pEndpoint, QString *pError)
{
  QUrl url(endpoint, QUrl::StrictMode);
  const QString host = url.host();
  const QString authority = endpoint.mid(endpoint.indexOf(QStringLiteral("://")) + 3).section(QLatin1Char('/'), 0, 0);
  QHostAddress address;
  const bool localhost = host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0;
  if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
      || host.isEmpty() || authority.contains(QLatin1Char('@')) || url.hasQuery() || url.hasFragment()
      || (!localhost && (!address.setAddress(host) || !address.isLoopback()))) {
    if (pError) {
      *pError = tr("Use an HTTP or HTTPS loopback URL without credentials, query, or fragment.");
    }
    return false;
  }
  if (localhost) {
    url.setHost(QStringLiteral("127.0.0.1"));
  }
  QString path = url.path();
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  if (path.isEmpty()) {
    path = QStringLiteral("/v1");
  }
  url.setPath(path);
  if (pEndpoint) {
    *pEndpoint = url;
  }
  if (pError) {
    pError->clear();
  }
  return true;
}

QString LocalCompletionClient::fimPrompt(const QString &prefix, const QString &suffix, const LocalCompletionSettings &settings)
{
  const LocalCompletionSettings bounded = settings.normalized();
  QString instructions = bounded.instructions.trimmed();
  if (!instructions.isEmpty()) {
    instructions.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    instructions.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    instructions.replace(QStringLiteral("<|"), QStringLiteral("< |"));
    instructions.replace(QStringLiteral("|>"), QStringLiteral("| >"));
    instructions.replace(QStringLiteral("\n"), QStringLiteral("\n// "));
    instructions = QStringLiteral("// ") + instructions + QLatin1Char('\n');
  }
  return QStringLiteral("<|fim_prefix|>") + instructions + prefix.right(bounded.prefixCharacters)
      + QStringLiteral("<|fim_suffix|>") + suffix.left(bounded.suffixCharacters)
      + QStringLiteral("<|fim_middle|>");
}

quint64 LocalCompletionClient::requestCompletion(const QString &prefix, const QString &suffix,
                                                const LocalCompletionSettings &settings)
{
  cancel();
  const quint64 requestId = mGeneration;
  const LocalCompletionSettings bounded = settings.normalized();
  QJsonObject payload;
  payload.insert(QStringLiteral("model"), settings.model.trimmed());
  payload.insert(QStringLiteral("prompt"), fimPrompt(prefix, suffix, settings));
  payload.insert(QStringLiteral("stream"), false);
  payload.insert(QStringLiteral("temperature"), bounded.temperature);
  payload.insert(QStringLiteral("max_tokens"), bounded.maxTokens);
  payload.insert(QStringLiteral("stop"), QJsonArray{QStringLiteral("<|fim_prefix|>"), QStringLiteral("<|fim_suffix|>"),
                                                QStringLiteral("<|fim_middle|>"), QStringLiteral("<|endoftext|>")});
  start(bounded, QJsonDocument(payload).toJson(QJsonDocument::Compact), Completion);
  return requestId;
}

void LocalCompletionClient::testConnection(const LocalCompletionSettings &settings)
{
  cancel();
  start(settings, QByteArray(), ConnectionTest);
}

void LocalCompletionClient::discoverModels(const LocalCompletionSettings &settings)
{
  cancel();
  start(settings, QByteArray(), ModelDiscovery);
}

void LocalCompletionClient::cancel()
{
  ++mGeneration;
  mDeadline.stop();
  if (mpReply) {
    QNetworkReply *pReply = mpReply;
    mpReply.clear();
    pReply->disconnect(this);
    pReply->abort();
    pReply->deleteLater();
  }
  mResponse.clear();
}

void LocalCompletionClient::start(const LocalCompletionSettings &settings, const QByteArray &payload, RequestKind kind)
{
  mSettings = settings.normalized();
  mRequestKind = kind;
  const bool connectionTest = kind != Completion;
  QUrl url;
  QString error;
  if (!normalizeEndpoint(settings.endpoint, &url, &error)) {
    queueFailure(error);
    return;
  }
  if (kind != ModelDiscovery && settings.model.trimmed().isEmpty()) {
    queueFailure(tr("Specify a local model identifier."));
    return;
  }
  url.setPath(url.path() + (connectionTest ? QStringLiteral("/models") : QStringLiteral("/completions")));
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setRawHeader("Accept", "application/json");
  mpReply = connectionTest ? mNetwork.get(request) : mNetwork.post(request, payload);
  mpReply->setReadBufferSize(responseLimit + 1);
  const quint64 generation = mGeneration;
  connect(mpReply, &QNetworkReply::readyRead, this, [this, generation]() {
    if (generation == mGeneration) {
      readResponse();
    }
  });
  connect(mpReply, &QNetworkReply::metaDataChanged, this, [this, generation]() {
    if (generation == mGeneration && mpReply
        && mpReply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > responseLimit) {
      fail(tr("Local server response exceeds the size limit."));
    }
  });
  connect(mpReply, &QNetworkReply::finished, this, [this, generation]() {
    if (generation == mGeneration) {
      finish();
    }
  });
  mDeadline.start(mSettings.timeoutMs);
}

void LocalCompletionClient::queueFailure(const QString &message)
{
  const quint64 generation = mGeneration;
  QTimer::singleShot(0, this, [this, generation, message]() {
    if (generation == mGeneration) {
      fail(message);
    }
  });
}

void LocalCompletionClient::fail(const QString &message)
{
  const quint64 requestId = mGeneration;
  const RequestKind kind = mRequestKind;
  cancel();
  if (kind == ModelDiscovery) {
    emit modelDiscoveryFailed(message);
  } else if (kind == ConnectionTest) {
    emit connectionChecked(false, message);
  } else {
    emit requestFailed(requestId, message);
  }
}

void LocalCompletionClient::readResponse()
{
  if (!mpReply) {
    return;
  }
  mResponse += mpReply->read(responseLimit + 1 - mResponse.size());
  if (mResponse.size() > responseLimit) {
    fail(tr("Local server response exceeds the size limit."));
  }
}

void LocalCompletionClient::finish()
{
  const quint64 generation = mGeneration;
  readResponse();
  if (generation != mGeneration || !mpReply) {
    return;
  }
  const int status = mpReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status < 200 || status >= 300 || mpReply->error() != QNetworkReply::NoError) {
    fail(tr("Local server request failed. Check the server and configured model."));
    return;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(mResponse, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject() || document.object().contains(QStringLiteral("error"))) {
    fail(tr("Local server returned an invalid completion response."));
    return;
  }
  const QJsonObject object = document.object();
  const RequestKind kind = mRequestKind;
  const quint64 requestId = mGeneration;
  if (kind != Completion) {
    if (!object.value(QStringLiteral("data")).isArray()) {
      fail(tr("Local server returned an invalid model list."));
      return;
    }
    QStringList models;
    for (const QJsonValue &model : object.value(QStringLiteral("data")).toArray()) {
      const QString id = model.toObject().value(QStringLiteral("id")).toString().trimmed();
      if (!id.isEmpty() && id.size() <= 512 && !models.contains(id)) {
        models.append(id);
      }
      if (models.size() == 512) {
        break;
      }
    }
    const bool found = models.contains(mSettings.model.trimmed());
    cancel();
    if (kind == ModelDiscovery) {
      emit modelsDiscovered(models);
    } else {
      emit connectionChecked(found, found ? tr("Local server is reachable and the configured model is available.")
                                        : tr("The configured model is not available on the local server."));
    }
  } else {
    const QJsonValue tokens = object.value(QStringLiteral("usage")).toObject().value(QStringLiteral("completion_tokens"));
    if (tokens.isDouble() && tokens.toDouble() > mSettings.maxTokens) {
      fail(tr("Local server exceeded the requested token limit."));
      return;
    }
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    const QJsonValue text = choices.isEmpty() ? QJsonValue() : choices.first().toObject().value(QStringLiteral("text"));
    const QString completion = text.isString() ? completionText(text.toString(), mSettings.maxLines) : QString();
    if (completion.isEmpty()) {
      fail(tr("Local server returned empty or unsupported completion text."));
      return;
    }
    cancel();
    emit completionReady(requestId, completion);
  }
}
