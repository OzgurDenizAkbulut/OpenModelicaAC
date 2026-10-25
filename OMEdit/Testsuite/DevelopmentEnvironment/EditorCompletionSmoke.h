#ifndef EDITORCOMPLETIONSMOKE_H
#define EDITORCOMPLETIONSMOKE_H

#include "MainWindow.h"
#include "Editors/ModelicaEditor.h"
#include "Modeling/LibraryTreeWidget.h"
#include "Modeling/ModelWidgetContainer.h"
#include "Options/OptionsDialog.h"
#include "Autocomplete/InlineCompletionController.h"
#include "Autocomplete/InlineCompletionPreview.h"
#include <QElapsedTimer>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <stdexcept>

inline void checkEditorCompletion(OptionsDialog *options, const QString &output)
{
  auto check = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
    qInfo().noquote() << "PASS:" << message;
  };
  auto waitUntil = [](auto condition) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 3000) {
      QTest::qWait(10);
    }
    return condition();
  };
  QTemporaryDir workspace(output + "/editor-XXXXXX");
  check(workspace.isValid(), "real editor fixture has an isolated directory");
  const QString name = "InlineEditorAcceptance";
  QFile file(workspace.filePath(name + ".mo"));
  const QByteArray source = "model InlineEditorAcceptance\n  Real x;\nequation\n  der(x) = -x;\nend InlineEditorAcceptance;\n";
  check(file.open(QIODevice::WriteOnly) && file.write(source) == source.size(), "real editor fixture written");
  file.close();

  LocalCompletionSettings settings = options->localCompletionSettings();
  settings.enabled = false;
  options->getAutocompletePage()->setSettings(settings);
  options->saveSettings();
  MainWindow *main = MainWindow::instance();
  main->getLibraryWidget()->openFile(file.fileName());
  LibraryTreeModel *tree = main->getLibraryWidget()->getLibraryTreeModel();
  LibraryTreeItem *item = tree->findLibraryTreeItem(name);
  check(item != nullptr, "compiler-loaded file appears in the real library tree");
  tree->showModelWidget(item);
  ModelWidget *model = item->getModelWidget();
  check(model != nullptr, "real model widget created");
  auto *editor = qobject_cast<ModelicaEditor *>(model->getEditor());
  check(editor != nullptr, "model owns a real ModelicaEditor");
  auto *controller = editor->findChild<InlineCompletionController *>();
  check(controller != nullptr, "real ModelicaEditor owns its inline completion controller");
  PlainTextEdit *text = editor->getPlainTextEdit();
  model->getTextViewToolButton()->setChecked(true);
  main->resize(1200, 800);
  main->show();
  main->activateWindow();
  text->setFocus();
  check(waitUntil([&]() { return text->hasFocus(); }), "real Modelica text view receives focus");
  const QString original = text->toPlainText();
  const int position = original.indexOf("Real x") + 6;
  check(position >= 6, "completion insertion position exists in loaded model");
  QTextCursor cursor(text->document());
  cursor.setPosition(position);
  text->setTextCursor(cursor);

  int requests = 0;
  QByteArray requestBody;
  QTcpServer server;
  check(server.listen(QHostAddress::LocalHost), "real editor completion fixture listens on loopback");
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
    while (server.hasPendingConnections()) {
      QTcpSocket *socket = server.nextPendingConnection();
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
        const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        const int header = request.indexOf("\r\n\r\n");
        if (header < 0 || socket->property("handled").toBool()) {
          return;
        }
        int length = 0;
        for (const QByteArray &line : request.left(header).split('\n')) {
          if (line.toLower().startsWith("content-length:")) {
            length = line.mid(15).trimmed().toInt();
          }
        }
        if (request.size() < header + 4 + length) {
          return;
        }
        socket->setProperty("handled", true);
        ++requests;
        requestBody = request.mid(header + 4, length);
        const QByteArray body = "{\"choices\":[{\"text\":\"(start=1)\"}]}";
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    }
  });
  settings.enabled = true;
  settings.automatic = false;
  settings.endpoint = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
  settings.model = "editor-smoke-model";
  settings.shortcut = "Ctrl+Alt+J";
  settings.acceptShortcut = "Tab";
  settings.instructions.clear();
  options->getAutocompletePage()->setSettings(settings);
  options->saveSettings();
  text->setFocus();
  check(waitUntil([&]() { return text->hasFocus(); }), "text focus restored after applying autocomplete settings");
  qApp->processEvents();
  text->grab();
  QTest::qWait(50);
  check(text->hasFocus() && text->textCursor().position() == position,
        "real editor layout is settled before requesting completion");
  const int revision = text->document()->revision();
  const bool modified = text->document()->isModified();
  QTest::keyClick(text, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
  check(waitUntil([&]() { return controller->preview()->isVisible(); }), "real editor shortcut displays a synthetic HTTP completion");
  check(requests == 1 && requestBody.contains("editor-smoke-model") && requestBody.contains("Real x"),
        "real editor sends its model and source through the configured client");
  check(controller->preview()->completion() == "(start=1)" && text->toPlainText() == original
        && text->document()->revision() == revision && text->document()->isModified() == modified,
        "real editor ghost leaves source revision and modified state unchanged");
  check(controller->preview()->grab().save(output + "/autocomplete-editor-ghost.png"), "real editor ghost screenshot saved");
  check(controller->preview()->isVisible() && controller->preview()->completion() == "(start=1)",
        "real editor ghost remains visible after screenshot capture");
  QTest::keyClick(text, Qt::Key_Tab);
  check(text->toPlainText() == original.left(position) + "(start=1)" + original.mid(position)
        && !controller->preview()->isVisible(), "Tab accepts completion through the real editor event filter");
  text->undo();
  check(text->toPlainText() == original, "one undo restores the real editor source before completion");
  settings.enabled = false;
  options->getAutocompletePage()->setSettings(settings);
  options->saveSettings();
  text->setFocus();
  controller->requestCompletion();
  QTest::qWait(50);
  check(requests == 1 && !controller->preview()->isVisible(), "saved disable preference reaches the existing real editor controller");
}

#endif
