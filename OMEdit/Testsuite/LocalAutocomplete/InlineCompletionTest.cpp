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

#include "Autocomplete/InlineCompletionController.h"
#include "Autocomplete/InlineCompletionPreview.h"
#include "Autocomplete/LocalCompletionClient.h"

#include <QCompleter>
#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSharedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

class CompletionServer : public QTcpServer
{
public:
  int requests = 0;
  bool respond = true;
  QString text = QStringLiteral(" = 1;\n  Real y");
  CompletionServer()
  {
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this]() {
      while (hasPendingConnections()) {
        QTcpSocket *pSocket = nextPendingConnection();
        auto pBuffer = QSharedPointer<QByteArray>::create();
        connect(pSocket, &QTcpSocket::readyRead, pSocket, [this, pSocket, pBuffer]() {
          pBuffer->append(pSocket->readAll());
          const int header = pBuffer->indexOf("\r\n\r\n");
          if (header < 0) {
            return;
          }
          int length = 0;
          for (const QByteArray &line : pBuffer->left(header).split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
              length = line.mid(15).trimmed().toInt();
            }
          }
          if (pBuffer->size() < header + 4 + length) {
            return;
          }
          ++requests;
          if (respond) {
            const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("choices"),
                QJsonArray{QJsonObject{{QStringLiteral("text"), text}}}}}).toJson(QJsonDocument::Compact);
            pSocket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                           + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            pSocket->disconnectFromHost();
          }
        });
        connect(pSocket, &QTcpSocket::disconnected, pSocket, &QObject::deleteLater);
      }
    });
  }
  LocalCompletionSettings settings() const
  {
    LocalCompletionSettings result;
    result.enabled = true;
    result.endpoint = QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort());
    return result;
  }
};

class InlineCompletionTest : public QObject
{
  Q_OBJECT
private slots:
  void multilineIsolationAndGeometry()
  {
    QPlainTextEdit editor;
    editor.setFont(QFont(QStringLiteral("Consolas"), 12));
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real x;\nequation\n  x = 0;\nend M;"));
    editor.document()->setModified(false);
    editor.show();
    QTest::qWait(20);
    const QString original = editor.toPlainText();
    const int revision = editor.document()->revision();
    const int following = original.indexOf(QStringLiteral("equation"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(following);
    const int oldY = editor.cursorRect(cursor).top();
    QSignalSpy changes(editor.document(), &QTextDocument::contentsChange);
    InlineCompletionPreview preview(&editor);
    const QString ghost = QStringLiteral(" = 1;\nReal y");
    QVERIFY(preview.showCompletion(6, ghost));
    QCOMPARE(preview.toPlainText(), original.left(6) + ghost + original.mid(6));
    QVERIFY(preview.sourceBlockRect(editor.document()->findBlock(following)).top() > oldY);
    QCOMPARE(editor.toPlainText(), original);
    QCOMPARE(editor.document()->revision(), revision);
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());
    QVERIFY(!preview.document()->isUndoAvailable());
    QCOMPARE(changes.count(), 0);
    const QString screenshot = qEnvironmentVariable("OMEDIT_INLINE_PREVIEW_SCREENSHOT");
    if (!screenshot.isEmpty()) {
      QVERIFY(editor.grab().save(screenshot));
    }
    preview.dismiss();
    QCOMPARE(changes.count(), 0);
  }

  void wrapsAndPreservesFoldedBlocks()
  {
    QPlainTextEdit editor;
    editor.resize(220, 400);
    editor.setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor.setPlainText(QStringLiteral("Real x;\nannotation(\n  hidden=true);\nend M;"));
    QTextBlock hidden = editor.document()->findBlockByNumber(2);
    hidden.setVisible(false);
    hidden.setLineCount(0);
    editor.show();
    QTest::qWait(20);
    InlineCompletionPreview preview(&editor);
    const QString ghost = QStringLiteral(" = aVeryLongModelicaName + anotherLongModelicaName;\nReal y");
    QVERIFY(preview.showCompletion(6, ghost));
    QVERIFY(preview.document()->findBlock(hidden.position() + ghost.size()).isVisible() == false);
    QVERIFY(!hidden.isVisible());
    QVERIFY(preview.document()->firstBlock().layout()->lineCount() > 1);
    QTextCursor cursor(editor.document());
    cursor.setPosition(editor.document()->lastBlock().position());
    QVERIFY(preview.sourceBlockRect(editor.document()->lastBlock()).top() > editor.cursorRect(cursor).top());
  }

  void rejectsClippedOutput()
  {
    QPlainTextEdit editor;
    editor.resize(160, 100);
    editor.setLineWrapMode(QPlainTextEdit::NoWrap);
    editor.setPlainText(QStringLiteral("x;"));
    editor.show();
    QTest::qWait(20);
    InlineCompletionPreview preview(&editor);
    QVERIFY(!preview.showCompletion(1, QString(200, QLatin1Char('x'))));
    QVERIFY(!preview.isVisible());
    QVERIFY(preview.completion().isEmpty());
    QCOMPARE(editor.toPlainText(), QStringLiteral("x;"));
  }

  void scrollsPresentationAtBottomWithoutMovingSource()
  {
    QPlainTextEdit editor;
    editor.resize(640, 180);
    editor.setPlainText(QStringLiteral("Real x;\n").repeated(30));
    editor.show();
    editor.moveCursor(QTextCursor::End);
    editor.ensureCursorVisible();
    QTest::qWait(20);
    const int scroll = editor.verticalScrollBar()->value();
    const int revision = editor.document()->revision();
    InlineCompletionPreview preview(&editor);
    QVERIFY(preview.showCompletion(editor.textCursor().position(), QStringLiteral("Real y;\nequation\n  y = 1;")));
    QCOMPARE(editor.verticalScrollBar()->value(), scroll);
    QCOMPARE(editor.document()->revision(), revision);
    const QRectF mapped = preview.sourceBlockRect(editor.document()->lastBlock());
    QVERIFY(mapped.top() >= 0);
    QVERIFY(mapped.bottom() <= preview.viewport()->height());
  }

  void disabledAndConfigurableManualShortcut()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    LocalCompletionSettings settings = server.settings();
    settings.enabled = false;
    controller.setSettings(settings);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    QTest::keyClicks(&editor, "Real x");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Real x"));
    QTest::qWait(200);
    QCOMPARE(server.requests, 0);
    settings.enabled = true;
    settings.automatic = false;
    settings.shortcut = QStringLiteral("Ctrl+Alt+J");
    controller.setSettings(settings);
    QTest::keyClick(&editor, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    QTRY_COMPARE(server.requests, 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    editor.clearFocus();
    QVERIFY(!controller.preview()->isVisible());
  }

  void debounceAcceptAndUndo()
  {
    CompletionServer server;
    QVERIFY(server.isListening());
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real ;\nend M;"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(5);
    editor.setTextCursor(cursor);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    controller.setSettings(server.settings());
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    QTest::keyClicks(&editor, "x");
    QTest::qWait(80);
    QCOMPARE(server.requests, 0);
    QTRY_COMPARE(server.requests, 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    const QString original = editor.toPlainText();
    const QString visible = controller.preview()->completion();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.toPlainText(), original.left(6) + visible + original.mid(6));
    QCOMPARE(editor.textCursor().position(), 6 + visible.size());
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);
    QVERIFY(!controller.preview()->isVisible());
    QTest::qWait(200);
    QCOMPARE(server.requests, 1);
  }

  void deletionSchedulesAndPreviewClickOnlyDismisses()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real xy;\nend M;"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(7);
    editor.setTextCursor(cursor);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    controller.setSettings(server.settings());
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QTRY_COMPARE(server.requests, 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    const int anchor = editor.textCursor().position();
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 120));
    QVERIFY(!controller.preview()->isVisible());
    QCOMPARE(editor.textCursor().position(), anchor);
    QTest::keyClick(&editor, Qt::Key_Delete);
    QTRY_COMPARE(server.requests, 2);
    QTRY_VERIFY(controller.preview()->isVisible());
    editor.undo();
    QVERIFY(!controller.preview()->isVisible());
    QTest::qWait(200);
    QCOMPARE(server.requests, 2);
  }

  void dismissalAndPopupPrecedence()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real x;\nend M;"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(6);
    editor.setTextCursor(cursor);
    QCompleter completer(QStringList{QStringLiteral("xyz"), QStringLiteral("xyzz")});
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    controller.setSettings(server.settings());
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    QTest::keyClick(&editor, Qt::Key_Escape);
    QVERIFY(!controller.preview()->isVisible());
    QTest::qWait(200);
    QCOMPARE(server.requests, 1);
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    completer.complete();
    QTRY_VERIFY(!controller.preview()->isVisible());
    completer.popup()->hide();
    editor.setFocus();
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    editor.setReadOnly(true);
    QVERIFY(!controller.preview()->isVisible());
    const QString original = editor.toPlainText();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.toPlainText(), original);
  }

  void programmaticEditsAndStaleResponses()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    controller.setSettings(server.settings());
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    editor.setPlainText(QStringLiteral("Real x;"));
    QTest::qWait(200);
    QCOMPARE(server.requests, 0);
    LocalCompletionClient *pClient = controller.findChild<LocalCompletionClient *>();
    QVERIFY(pClient);
    QSignalSpy completions(pClient, &LocalCompletionClient::completionReady);
    controller.requestCompletion();
    QTRY_COMPARE(server.requests, 1);
    QTRY_COMPARE(completions.count(), 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    const quint64 staleId = completions.first().at(0).toULongLong();
    editor.moveCursor(QTextCursor::End);
    QVERIFY(!controller.preview()->isVisible());
    QVERIFY(QMetaObject::invokeMethod(pClient, "completionReady", Qt::QueuedConnection,
                                     Q_ARG(quint64, staleId), Q_ARG(QString, server.text)));
    QTest::qWait(200);
    QVERIFY(!controller.preview()->isVisible());
    QCOMPARE(server.requests, 1);
  }

  void configuredAcceptance_data()
  {
    QTest::addColumn<QString>("shortcut");
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("modifiers");
    QTest::newRow("tab") << QStringLiteral("Tab") << int(Qt::Key_Tab) << int(Qt::NoModifier);
    QTest::newRow("ctrl-right") << QStringLiteral("Ctrl+Right") << int(Qt::Key_Right) << int(Qt::ControlModifier);
    QTest::newRow("ctrl-return") << QStringLiteral("Ctrl+Return") << int(Qt::Key_Return) << int(Qt::ControlModifier);
  }

  void configuredAcceptance()
  {
    QFETCH(QString, shortcut);
    QFETCH(int, key);
    QFETCH(int, modifiers);
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real x;\nend M;"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(6);
    editor.setTextCursor(cursor);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    LocalCompletionSettings settings = server.settings();
    settings.automatic = false;
    settings.acceptShortcut = shortcut;
    controller.setSettings(settings);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    const QString original = editor.toPlainText();
    const QString visible = controller.preview()->completion();
    QTest::keyClick(&editor, Qt::Key(key), Qt::KeyboardModifiers(modifiers));
    QCOMPARE(editor.toPlainText(), original.left(6) + visible + original.mid(6));
    QCOMPARE(editor.textCursor().position(), 6 + visible.size());
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);

    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    QTest::keyClick(&editor, Qt::Key(key), Qt::KeyboardModifiers(modifiers) | Qt::ShiftModifier);
    QVERIFY(!controller.preview()->isVisible());
    QVERIFY(!editor.toPlainText().contains(visible));
  }

  void automaticPrefixGateAndManualBypass()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    LocalCompletionSettings settings = server.settings();
    settings.debounceMs = 50;
    settings.minimumPrefixLength = 2;
    controller.setSettings(settings);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    QTest::keyClicks(&editor, "_");
    QTest::qWait(100);
    QCOMPARE(server.requests, 0);
    QTest::keyClicks(&editor, "2");
    QTRY_COMPARE(server.requests, 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    QTest::keyClicks(&editor, ";");
    QTest::qWait(100);
    QCOMPARE(server.requests, 1);
    controller.requestCompletion();
    QTRY_COMPARE(server.requests, 2);
    QTRY_VERIFY(controller.preview()->isVisible());
  }

  void deletionToggleAndDebounceChange()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("abc"));
    editor.moveCursor(QTextCursor::End);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    LocalCompletionSettings settings = server.settings();
    settings.triggerAfterDeletion = false;
    settings.debounceMs = 50;
    controller.setSettings(settings);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    QTest::keyClick(&editor, Qt::Key_Backspace);
    editor.moveCursor(QTextCursor::Start);
    QTest::keyClick(&editor, Qt::Key_Delete);
    QCOMPARE(editor.toPlainText(), QStringLiteral("b"));
    QTest::qWait(100);
    QCOMPARE(server.requests, 0);
    settings.triggerAfterDeletion = true;
    settings.debounceMs = 400;
    controller.setSettings(settings);
    QTest::keyClick(&editor, Qt::Key_Delete);
    QTest::qWait(200);
    QCOMPARE(server.requests, 0);
    QTRY_COMPARE(server.requests, 1);
    QTRY_VERIFY(controller.preview()->isVisible());
    QTest::keyClicks(&editor, "x");
    settings.debounceMs = 50;
    controller.setSettings(settings);
    QTest::qWait(450);
    QCOMPARE(server.requests, 1);
    QTest::keyClicks(&editor, "y");
    QTRY_COMPARE(server.requests, 2);
  }

  void ghostAppearanceAndChangedSettingsInvalidate()
  {
    CompletionServer server;
    QPlainTextEdit editor;
    editor.resize(640, 360);
    editor.setPlainText(QStringLiteral("Real x;"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(6);
    editor.setTextCursor(cursor);
    QCompleter completer;
    completer.setWidget(&editor);
    InlineCompletionController controller(&editor, &completer);
    LocalCompletionSettings settings = server.settings();
    controller.setSettings(settings);
    editor.show();
    editor.activateWindow();
    editor.setFocus();
    QTRY_VERIFY(editor.hasFocus());
    LocalCompletionClient *pClient = controller.findChild<LocalCompletionClient *>();
    QSignalSpy completions(pClient, &LocalCompletionClient::completionReady);
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    const quint64 staleId = completions.first().at(0).toULongLong();
    settings.ghostOpacity = 100;
    settings.ghostItalic = true;
    controller.setSettings(settings);
    QVERIFY(!controller.preview()->isVisible());
    QVERIFY(QMetaObject::invokeMethod(pClient, "completionReady", Qt::QueuedConnection,
                                     Q_ARG(quint64, staleId), Q_ARG(QString, server.text)));
    QTest::qWait(20);
    QVERIFY(!controller.preview()->isVisible());
    controller.requestCompletion();
    QTRY_VERIFY(controller.preview()->isVisible());
    QTextCursor ghostCursor(controller.preview()->document());
    ghostCursor.setPosition(7);
    QVERIFY(ghostCursor.charFormat().fontItalic());
    QVERIFY(qAbs(ghostCursor.charFormat().foreground().color().alphaF() - 0.9) < 0.005);
    QCOMPARE(editor.toPlainText(), QStringLiteral("Real x;"));
    QVERIFY(!editor.textCursor().charFormat().fontItalic());
    QVERIFY(!editor.document()->isUndoAvailable());
  }
};

QTEST_MAIN(InlineCompletionTest)
#include "InlineCompletionTest.moc"
