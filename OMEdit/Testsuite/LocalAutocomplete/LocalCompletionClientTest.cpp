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

#include "Autocomplete/LocalCompletionClient.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

class CompletionServer : public QTcpServer
{
public:
  struct Request {
    QPointer<QTcpSocket> socket;
    QByteArray header;
    QByteArray body;
  };
  QList<Request> requests;

  CompletionServer()
  {
    connect(this, &QTcpServer::newConnection, this, [this]() {
      while (hasPendingConnections()) {
        QTcpSocket *pSocket = nextPendingConnection();
        connect(pSocket, &QTcpSocket::disconnected, pSocket, &QObject::deleteLater);
        connect(pSocket, &QTcpSocket::readyRead, this, [this, pSocket]() {
          QByteArray data = pSocket->property("request").toByteArray() + pSocket->readAll();
          pSocket->setProperty("request", data);
          const int headerEnd = data.indexOf("\r\n\r\n");
          if (headerEnd < 0 || pSocket->property("recorded").toBool()) {
            return;
          }
          const QByteArray header = data.left(headerEnd);
          int length = 0;
          for (const QByteArray &line : header.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
              length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
            }
          }
          if (data.size() - headerEnd - 4 < length) {
            return;
          }
          pSocket->setProperty("recorded", true);
          requests.append({pSocket, header, data.mid(headerEnd + 4, length)});
        });
      }
    });
  }

  LocalCompletionSettings settings() const
  {
    LocalCompletionSettings settings;
    settings.enabled = true;
    settings.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort());
    return settings;
  }

  void respond(int index, const QByteArray &body, const QByteArray &status = "200 OK", const QByteArray &extra = QByteArray())
  {
    if (requests[index].socket) {
      requests[index].socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body);
      requests[index].socket->disconnectFromHost();
    }
  }

  static QByteArray completion(const QString &text)
  {
    return QJsonDocument(QJsonObject{{QStringLiteral("choices"), QJsonArray{
                                      QJsonObject{{QStringLiteral("text"), text}}}}}).toJson(QJsonDocument::Compact);
  }
};

class LocalCompletionClientTest : public QObject
{
  Q_OBJECT
private slots:
  void endpointValidation_data()
  {
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("localhost") << "http://localhost:11434/v1/" << "http://127.0.0.1:11434/v1";
    QTest::newRow("ipv4") << "https://127.12.34.56/v1" << "https://127.12.34.56/v1";
    QTest::newRow("ipv6") << "http://[::1]:1234" << "http://[::1]:1234/v1";
    QTest::newRow("remote") << "http://example.com/v1" << "";
    QTest::newRow("lan") << "http://192.168.1.2/v1" << "";
    QTest::newRow("suffix") << "http://localhost.example.com/v1" << "";
    QTest::newRow("credentials") << "http://user:pass@127.0.0.1/v1" << "";
    QTest::newRow("empty-credentials") << "http://@127.0.0.1/v1" << "";
    QTest::newRow("query") << "http://127.0.0.1/v1?x=1" << "";
    QTest::newRow("empty-query") << "http://127.0.0.1/v1?" << "";
    QTest::newRow("fragment") << "http://127.0.0.1/v1#x" << "";
    QTest::newRow("protocol") << "file://127.0.0.1/v1" << "";
  }

  void endpointValidation()
  {
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QUrl endpoint;
    QString error;
    QCOMPARE(LocalCompletionClient::normalizeEndpoint(input, &endpoint, &error), !expected.isEmpty());
    if (!expected.isEmpty()) {
      QCOMPARE(endpoint.toString(), expected);
      QVERIFY(error.isEmpty());
    } else {
      QVERIFY(!error.isEmpty());
    }
  }

  void payloadAndCompletion()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    LocalCompletionSettings settings = server.settings();
    settings.prefixCharacters = 3;
    settings.suffixCharacters = 2;
    settings.maxTokens = 999;
    const quint64 id = client.requestCompletion(QStringLiteral("abcdef"), QStringLiteral("xyz"), settings);
    QTRY_COMPARE(server.requests.size(), 1);
    QVERIFY(server.requests.first().header.startsWith("POST /v1/completions HTTP/1.1"));
    const QJsonObject payload = QJsonDocument::fromJson(server.requests.first().body).object();
    QCOMPARE(payload.value("prompt").toString(), QStringLiteral("<|fim_prefix|>def<|fim_suffix|>xy<|fim_middle|>"));
    QCOMPARE(payload.value("model").toString(), settings.model);
    QCOMPARE(payload.value("max_tokens").toInt(), 999);
    QCOMPARE(payload.value("temperature").toInt(-1), 0);
    QCOMPARE(payload.value("stream").toBool(true), false);
    QCOMPARE(payload.value("stop").toArray().size(), 4);
    server.respond(0, CompletionServer::completion(QStringLiteral("  Real x;\r\n\tx = 1;\rnext")));
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.first().at(0).toULongLong(), id);
    QCOMPARE(ready.first().at(1).toString(), QStringLiteral("  Real x;\n\tx = 1;\nnext"));
    QCOMPARE(failed.size(), 0);
  }

  void immediateErrorsAreQueuedAndCancelable()
  {
    LocalCompletionClient client;
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    LocalCompletionSettings settings;
    settings.endpoint = QStringLiteral("http://remote.invalid/v1");
    const quint64 id = client.requestCompletion("source", "suffix", settings);
    QCOMPARE(failed.size(), 0);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(0).toULongLong(), id);
    client.requestCompletion("source", "suffix", settings);
    client.cancel();
    QTest::qWait(20);
    QCOMPARE(failed.size(), 1);
  }

  void canceledAndSupersededRequests()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    client.requestCompletion("old", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 1);
    const quint64 latest = client.requestCompletion("new", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 2);
    server.respond(1, CompletionServer::completion("new result"));
    server.respond(0, CompletionServer::completion("obsolete result"));
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.first().at(0).toULongLong(), latest);
    QCOMPARE(ready.first().at(1).toString(), QStringLiteral("new result"));
    client.requestCompletion("cancel", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 3);
    client.cancel();
    server.respond(2, CompletionServer::completion("canceled result"));
    QTest::qWait(30);
    QCOMPARE(ready.size(), 1);
    QCOMPARE(failed.size(), 0);
  }

  void timeout()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    LocalCompletionSettings settings = server.settings();
    settings.timeoutMs = 30;
    const quint64 id = client.requestCompletion("prefix", "suffix", settings);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.first().at(0).toULongLong(), id);
    QVERIFY(failed.first().at(1).toString().contains("timed out"));
  }

  void invalidResponses_data()
  {
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("status");
    QTest::newRow("malformed") << QByteArray("{broken") << QByteArray("200 OK");
    QTest::newRow("missing-text") << QByteArray("{\"choices\":[{\"message\":{\"content\":\"chat\"}}]}") << QByteArray("200 OK");
    QTest::newRow("empty") << CompletionServer::completion("") << QByteArray("200 OK");
    QTest::newRow("whitespace") << CompletionServer::completion(" \n\t") << QByteArray("200 OK");
    QTest::newRow("fence") << CompletionServer::completion("```modelica\nReal x;\n```") << QByteArray("200 OK");
    QTest::newRow("fim") << CompletionServer::completion("Real x;<|fim_suffix|>") << QByteArray("200 OK");
    QTest::newRow("control") << CompletionServer::completion(QString("Real x;") + QChar(0)) << QByteArray("200 OK");
    QTest::newRow("chat") << CompletionServer::completion("Here is the code:\nReal x;") << QByteArray("200 OK");
    QTest::newRow("oversized") << QByteArray(65537, 'x') << QByteArray("200 OK");
    QTest::newRow("http") << QByteArray("{\"error\":\"private server diagnostic\"}") << QByteArray("404 Not Found");
    QTest::newRow("api-error") << QByteArray("{\"error\":\"private server diagnostic\"}") << QByteArray("200 OK");
    QTest::newRow("token-limit") << QByteArray("{\"choices\":[{\"text\":\"Real x;\"}],\"usage\":{\"completion_tokens\":129}}") << QByteArray("200 OK");
  }

  void invalidResponses()
  {
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, status);
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    client.requestCompletion("private source", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 1);
    server.respond(0, body, status);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!failed.first().at(1).toString().contains("private"));
  }

  void boundsLinesAndRefusesRedirect()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    client.requestCompletion("", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 1);
    server.respond(0, CompletionServer::completion("1\n2\n3\n4\n5\n6\n7"));
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready.first().at(1).toString(), QStringLiteral("1\n2\n3\n4\n5\n6"));
    client.requestCompletion("", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 2);
    server.respond(1, "", "302 Found", "Location: http://127.0.0.1:" + QByteArray::number(server.serverPort()) + "/escape\r\n");
    QTRY_COMPARE(failed.size(), 1);
    QTest::qWait(30);
    QCOMPARE(server.requests.size(), 2);
  }

  void abortsOversizedHeadersAndChunkedBodyEarly()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    client.requestCompletion("", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 1);
    server.requests[0].socket->write("HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n");
    QTRY_COMPARE(failed.size(), 1);
    client.requestCompletion("", "", server.settings());
    QTRY_COMPARE(server.requests.size(), 2);
    server.requests[1].socket->write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10001\r\n"
                                    + QByteArray(65537, 'x') + "\r\n");
    QTRY_COMPARE(failed.size(), 2);
  }

  void connectionTest()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy checked(&client, &LocalCompletionClient::connectionChecked);
    client.testConnection(server.settings());
    QTRY_COMPARE(server.requests.size(), 1);
    QVERIFY(server.requests.first().header.startsWith("GET /v1/models HTTP/1.1"));
    QVERIFY(server.requests.first().body.isEmpty());
    server.respond(0, QJsonDocument(QJsonObject{{"data", QJsonArray{QJsonObject{{"id", server.settings().model}}}}}).toJson());
    QTRY_COMPARE(checked.size(), 1);
    QCOMPARE(checked.first().at(0).toBool(), true);
    client.testConnection(server.settings());
    QTRY_COMPARE(server.requests.size(), 2);
    server.respond(1, "{\"data\":[{\"id\":\"another-model\"}]}");
    QTRY_COMPARE(checked.size(), 2);
    QCOMPARE(checked.at(1).at(0).toBool(), false);
  }

  void bypassesApplicationProxy()
  {
    CompletionServer server;
    CompletionServer proxy;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QVERIFY(proxy.listen(QHostAddress::LocalHost));
    const QNetworkProxy previous = QNetworkProxy::applicationProxy();
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort()));
    LocalCompletionClient client;
    client.requestCompletion("", "", server.settings());
    QNetworkProxy::setApplicationProxy(previous);
    QTRY_COMPARE(server.requests.size(), 1);
    QCOMPARE(proxy.requests.size(), 0);
    client.cancel();
  }

  void configuredGenerationAndInstructions()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    LocalCompletionSettings settings = server.settings();
    settings.prefixCharacters = 13000;
    settings.suffixCharacters = 5000;
    settings.maxTokens = 256;
    settings.maxLines = 8;
    settings.temperature = 0.65;
    settings.instructions = "Use Modelica.\r\nPrefer der(x). <|fim_middle|>";
    client.requestCompletion(QString(14000, 'p'), QString(6000, 's'), settings);
    QTRY_COMPARE(server.requests.size(), 1);
    const QJsonObject payload = QJsonDocument::fromJson(server.requests[0].body).object();
    QCOMPARE(payload.value("temperature").toDouble(), 0.65);
    QCOMPARE(payload.value("max_tokens").toInt(), 256);
    QCOMPARE(payload.value("prompt").toString(), QString("<|fim_prefix|>// Use Modelica.\n// Prefer der(x). < |fim_middle| >\n")
             + QString(13000, 'p') + "<|fim_suffix|>" + QString(5000, 's') + "<|fim_middle|>");
    QJsonObject response = QJsonDocument::fromJson(CompletionServer::completion("1\n2\n3\n4\n5\n6\n7\n8\n9")).object();
    response.insert("usage", QJsonObject{{"completion_tokens", 200}});
    server.respond(0, QJsonDocument(response).toJson());
    QTRY_COMPARE(ready.size(), 1);
    QCOMPARE(ready[0][1].toString(), QString("1\n2\n3\n4\n5\n6\n7\n8"));
  }

  void configuredTimeoutBeyondFiveSeconds()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy ready(&client, &LocalCompletionClient::completionReady);
    QSignalSpy failed(&client, &LocalCompletionClient::requestFailed);
    LocalCompletionSettings settings = server.settings();
    settings.timeoutMs = 8000;
    client.requestCompletion("Real ", "", settings);
    QTRY_COMPARE(server.requests.size(), 1);
    QTest::qWait(5200);
    QCOMPARE(failed.size(), 0);
    server.respond(0, CompletionServer::completion("x;"));
    QTRY_COMPARE(ready.size(), 1);
  }

  void discoveryDoesNotRequireModelOrSendSource()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    QSignalSpy found(&client, &LocalCompletionClient::modelsDiscovered);
    QSignalSpy failed(&client, &LocalCompletionClient::modelDiscoveryFailed);
    LocalCompletionSettings settings = server.settings();
    settings.model.clear();
    settings.instructions = "private instructions";
    client.discoverModels(settings);
    QTRY_COMPARE(server.requests.size(), 1);
    QVERIFY(server.requests[0].header.startsWith("GET /v1/models HTTP/1.1"));
    QVERIFY(server.requests[0].body.isEmpty());
    server.respond(0, R"({"data":[{"id":"qwen-local"},{"id":"qwen-local"},{"id":""},{"id":3},{"id":"second"}]})");
    QTRY_COMPARE(found.size(), 1);
    QCOMPARE(found[0][0].toStringList(), QStringList({"qwen-local", "second"}));
    QCOMPARE(failed.size(), 0);
    client.discoverModels(settings);
    QTRY_COMPARE(server.requests.size(), 2);
    client.cancel();
    server.respond(1, R"({"data":[{"id":"stale"}]})");
    QTest::qWait(30);
    QCOMPARE(found.size(), 1);
    client.discoverModels(settings);
    QTRY_COMPARE(server.requests.size(), 3);
    server.respond(2, R"({"data":"malformed"})");
    QTRY_COMPARE(failed.size(), 1);
    settings.endpoint = "http://remote.invalid/v1";
    client.discoverModels(settings);
    QTRY_COMPARE(failed.size(), 2);
    QCOMPARE(server.requests.size(), 3);
  }

  void excessiveSettingsStayBounded()
  {
    CompletionServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    LocalCompletionClient client;
    LocalCompletionSettings settings = server.settings();
    settings.maxTokens = 100000;
    settings.temperature = 10;
    settings.instructions = QString(5000, 'i');
    settings.prefixCharacters = 100000;
    settings.suffixCharacters = 100000;
    client.requestCompletion(QString(100000, 'p'), QString(100000, 's'), settings);
    QTRY_COMPARE(server.requests.size(), 1);
    const QJsonObject payload = QJsonDocument::fromJson(server.requests[0].body).object();
    QCOMPARE(payload.value("max_tokens").toInt(), 2048);
    QCOMPARE(payload.value("temperature").toDouble(), 2.0);
    const QString prompt = payload.value("prompt").toString();
    QCOMPARE(prompt, QString("<|fim_prefix|>// ") + QString(4000, 'i') + '\n'
             + QString(64000, 'p') + "<|fim_suffix|>" + QString(16000, 's') + "<|fim_middle|>");
    client.cancel();
  }
};

QTEST_GUILESS_MAIN(LocalCompletionClientTest)
#include "LocalCompletionClientTest.moc"
