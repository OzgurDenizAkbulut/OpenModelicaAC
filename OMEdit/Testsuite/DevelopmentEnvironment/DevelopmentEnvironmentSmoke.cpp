#include "OMEditApplication.h"
#include "MainWindow.h"
#include "OMC/OMCProxy.h"
#include "Modeling/MessagesWidget.h"
#include "Autocomplete/LocalCompletionClient.h"
#include "Options/OptionsDialog.h"
#include "Util/Helper.h"
#include "omedit_config.h"
#include "EditorCompletionSmoke.h"
#include "PlotResultSmoke.h"
#include "DiagramSmoke.h"
#include <QDir>
#include <QFileInfo>
#include <QKeySequenceEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <stdexcept>

static void require(bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
  qInfo().noquote() << "PASS:" << message;
}


template<class T> static T *control(AutocompletePage *page, const char *name)
{
  T *widget = page->findChild<T *>(name);
  require(widget != nullptr, name);
  return widget;
}

static void activate(QComboBox *combo, int index)
{
  combo->setCurrentIndex(index);
  QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, index));
}

static void checkDiscovery(AutocompletePage *page)
{
  int requests = 0;
  bool delay = false;
  bool modelsOnly = true;
  QTcpServer server;
  require(server.listen(QHostAddress::LocalHost), "fake model discovery server listens on loopback");
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
    QTcpSocket *socket = server.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
      QByteArray request = socket->property("request").toByteArray() + socket->readAll();
      socket->setProperty("request", request);
      if (socket->property("handled").toBool() || !request.contains("\r\n\r\n")) {
        return;
      }
      socket->setProperty("handled", true);
      ++requests;
      modelsOnly = modelsOnly && request.startsWith("GET /v1/models ") && !request.contains("prompt");
      const QByteArray body = "{\"data\":[{\"id\":\"first-local-model\"},{\"id\":\"second-local-model\"}]}";
      const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
          + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      QTimer::singleShot(delay ? 250 : 0, socket, [socket, response]() {
        if (socket->state() == QAbstractSocket::ConnectedState) {
          socket->write(response);
          socket->disconnectFromHost();
        }
      });
    });
  });
  LocalCompletionSettings discovery;
  discovery.endpoint = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
  discovery.model.clear();
  page->setSettings(discovery);
  auto *refresh = control<QPushButton>(page, "autocompleteRefreshModels");
  auto *models = control<QComboBox>(page, "autocompleteModel");
  auto *client = page->findChild<LocalCompletionClient *>();
  require(client != nullptr, "options owns its own real completion client");
  QSignalSpy discovered(client, &LocalCompletionClient::modelsDiscovered);
  refresh->click();
  require(discovered.wait(3000) && models->currentText() == "first-local-model",
          "blank model selects first advertised ID after real HTTP refresh");
  require(models->count() == 2, "model dropdown contains advertised IDs");
  models->setEditText("manual-alias");
  refresh->click();
  require(discovered.wait(3000) && models->currentText() == "manual-alias",
          "refresh preserves a nonempty manually entered model alias");
  delay = true;
  refresh->click();
  QElapsedTimer wait;
  wait.start();
  while (requests < 3 && wait.elapsed() < 3000) {
    QTest::qWait(10);
  }
  require(requests == 3, "delayed discovery request reached fake server");
  control<QLineEdit>(page, "autocompleteEndpoint")->setText("http://127.0.0.1:1234/v1");
  QTest::qWait(350);
  require(discovered.count() == 2 && refresh->isEnabled() && models->currentText() == "manual-alias",
          "endpoint edit cancels discovery and rejects stale advertised models");
  require(models->count() == 0, "endpoint edit clears old server model choices while retaining the editable model ID");
  require(modelsOnly, "model refresh uses only GET models and sends no editor source");
}

int main(int argc, char **argv)
{
  MMC_INIT();
  MMC_TRY_TOP()
  const QString output = qEnvironmentVariable("OMEDIT_SMOKE_OUTPUT", QDir::currentPath() + "/omedit-smoke");
  QDir().mkpath(output);
  QTemporaryDir isolatedSettings(output + "/settings-XXXXXX");
  if (!isolatedSettings.isValid()) {
    return 2;
  }
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedSettings.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolatedSettings.path());
  Q_INIT_RESOURCE(resource_omedit);
  OMEditApplication app(argc, argv, threadData, true);
  app.setQuitOnLastWindowClosed(false);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, [&app]() {
    qCritical("FAIL: full-app development smoke exceeded 60 seconds");
    app.exit(3);
  });
  deadline.start(60000);
  QTimer::singleShot(0, &app, [&app, output, &isolatedSettings]() {
    try {
      require(MainWindow::instance()->isTestsuiteRunning(), "real OMEdit application initialized in testsuite mode");
      OMCProxy *compiler = MainWindow::instance()->getOMCProxy();
      auto hasUnknownFlag = []() {
        return MessagesWidget::instance()->getAllMessageWidget()->getMessagesTextBrowser()->toPlainText()
            .contains("Unknown debug flag", Qt::CaseInsensitive);
      };
      require(!hasUnknownFlag(), "startup has no unknown debug flag error");
      require(compiler->clearCommandLineOptions() && !hasUnknownFlag(), "clearing options does not reintroduce unsupported GUI flag");
      if (qEnvironmentVariableIsSet("OMEDIT_SMOKE_LMSTUDIO")) {
        LocalCompletionClient client;
        QSignalSpy connected(&client, &LocalCompletionClient::connectionChecked);
        LocalCompletionSettings live;
        live.endpoint = "http://127.0.0.1:1234/v1";
        live.model = "qwen2.5-coder-1.5b";
        client.testConnection(live);
        require(connected.wait(5500) && connected.at(0).at(0).toBool(), "LM Studio connection and exact model ID pass the real Qt client test");
      }
      compiler->sendCommand("loadString(\"model AutocompleteBuildSmoke Real x(start=1); equation der(x)=-x; end AutocompleteBuildSmoke;\")");
      require(compiler->getResult().trimmed() == "true", "installed compiler loads a synthetic Modelica model");
      compiler->sendCommand("checkModel(AutocompleteBuildSmoke)");
      require(compiler->getResult().contains("completed successfully"), "installed compiler checks the synthetic model");
      require(!compiler->getModelInstance("AutocompleteBuildSmoke").isEmpty(),
              "matching generated Qt API retrieves a real model instance");
      compiler->sendCommand("loadString(\"model ConditionalBuildSmoke parameter Boolean useX=true; Real x if useX; end ConditionalBuildSmoke;\")");
      require(compiler->getResult().trimmed() == "true", "compiler loads conditional component fixture");
      require(compiler->getComponentCondition("ConditionalBuildSmoke", "x") == "useX",
              "matching generated Qt API retrieves component condition");
      OptionsDialog *options = OptionsDialog::instance();
      AutocompletePage *page = options->getAutocompletePage();
      require(page != nullptr, "Autocomplete page exists");
      LocalCompletionSettings defaults;
      defaults.enabled = OM_OMEDIT_LOCAL_AUTOCOMPLETE_DEFAULT_ENABLED;
      require(Helper::organization == QStringLiteral(OMEDIT_SETTINGS_ORGANIZATION)
              && Helper::application == QStringLiteral(OMEDIT_SETTINGS_APPLICATION),
              "application uses configured settings identity");
#if OM_OMEDIT_DEVELOPMENT_PROFILE
      require(defaults.enabled && Helper::organization != "openmodelica"
              && Helper::application != "omedit", "development profile enables completion and isolates official settings");
      require(QCoreApplication::applicationName() == Helper::application
              && QCoreApplication::organizationName() == Helper::organization,
              "Qt application identity matches development settings");
      require(QDir::cleanPath(Utilities::tempDirectory()).endsWith(Helper::application),
              "development temporary logs are isolated from official OMEdit");
#endif
      require(options->localCompletionSettings() == defaults && page->settings() == defaults,
              "all legacy and advanced defaults are retained");
      const bool builtIn = options->getTextEditorPage()->getAutoCompleteCheckBox()->isChecked();
      QSignalSpy changed(options, &OptionsDialog::localCompletionSettingsChanged);
      QSettings *stored = Utilities::getApplicationSettings();
      stored->setValue("localAutocomplete/model", "legacy-custom-model");
      stored->setValue("localAutocomplete/endpoint", "http://127.0.0.1:7234/v1");
      options->readAutocompleteSettings();
      LocalCompletionSettings legacy = defaults;
      legacy.model = "legacy-custom-model";
      legacy.endpoint = "http://127.0.0.1:7234/v1";
      require(page->settings() == legacy && options->localCompletionSettings() == defaults,
              "legacy keys migrate with all hidden fields defaulted and do not overwrite custom URL/model");
      stored->remove("localAutocomplete");
      options->readAutocompleteSettings();
      stored->setValue("localAutocomplete/enabled", false);
      options->readAutocompleteSettings();
      require(!page->settings().enabled, "explicit saved disabled preference overrides development default");
      stored->remove("localAutocomplete");
      options->readAutocompleteSettings();
      options->show();
      for (QListWidget *list : options->findChildren<QListWidget *>()) {
        for (int row = 0; row < list->count(); ++row) {
          if (list->item(row)->text() == QCoreApplication::translate("OptionsDialog", "Autocomplete")) {
            list->setCurrentRow(row);
          }
        }
      }
      options->resize(1100, 760);
      app.processEvents();
      require(options->grab().save(output + "/autocomplete-options-basic.png"), "default Basic page screenshot saved");
      auto *preset = control<QComboBox>(page, "autocompleteServerPreset");
      activate(preset, 2);
      require(page->settings().endpoint == "http://127.0.0.1:1234/v1" && page->settings().model.isEmpty(),
              "explicit LM Studio preset sets local URL and leaves exact model ID for discovery");
      activate(preset, 1);
      require(page->settings().endpoint == defaults.endpoint && page->settings().model == defaults.model,
              "explicit Ollama preset selects known local URL and model ID");
      checkDiscovery(page);
      LocalCompletionSettings candidate = defaults;
      candidate.enabled = true;
      candidate.automatic = false;
      candidate.shortcut = "Ctrl+Alt+J";
      candidate.endpoint = "http://127.0.0.1:1234/v1";
      candidate.model = "smoke-test-model";
      candidate.debounceMs = 475;
      candidate.timeoutMs = 14000;
      candidate.maxLines = 12;
      candidate.maxTokens = 640;
      candidate.prefixCharacters = 24000;
      candidate.suffixCharacters = 6000;
      candidate.temperature = 0.35;
      candidate.instructions = "Prefer descriptive Modelica component names.";
      candidate.ghostOpacity = 70;
      candidate.ghostItalic = true;
      candidate.acceptShortcut = "Ctrl+Return";
      candidate.triggerAfterDeletion = false;
      candidate.minimumPrefixLength = 3;
      page->setSettings(candidate);
      require(page->settings() == candidate && options->localCompletionSettings() == defaults,
              "all unsaved advanced fields remain separate from applied settings");
      options->getLanguageServerPage()->getLanguageServerGroupBox()->setChecked(false);
      options->saveSettings();
      require(changed.count() == 1 && options->localCompletionSettings() == candidate,
              "Save applies every setting with one notification");
      QSettings persisted(QSettings::IniFormat, QSettings::UserScope, Helper::organization, Helper::application);
      persisted.sync();
      require(QFileInfo(persisted.fileName()).absoluteFilePath().startsWith(isolatedSettings.path()),
              "settings writes remain inside isolated test directory");
      options->readAutocompleteSettings();
      require(page->settings() == candidate, "all nondefault advanced values survive QSettings readback");
      require(options->getTextEditorPage()->getAutoCompleteCheckBox()->isChecked() == builtIn,
              "built-in completion preference remains intact");
      page->setSettings(legacy);
      options->reject();
      require(changed.count() == 1 && page->settings() == candidate && options->localCompletionSettings() == candidate,
              "Cancel restores every saved advanced field without notifying or changing applied values");
      options->show();
      auto *advanced = control<QToolButton>(page, "autocompleteAdvancedToggle");
      advanced->setChecked(true);
      app.processEvents();
      require(control<QLabel>(page, "autocompleteStatus")->text().isEmpty(), "canceled discovery status stays cleared after resizing the page");
      require(options->grab().save(output + "/autocomplete-options-advanced.png"), "expanded Advanced page screenshot saved");
      QScrollArea *scroll = nullptr;
      for (QObject *parent = page->parent(); parent; parent = parent->parent()) {
        if ((scroll = qobject_cast<QScrollArea *>(parent))) {
          break;
        }
      }
      require(scroll != nullptr, "advanced options use existing scroll area");
      scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
      app.processEvents();
      require(options->grab().save(output + "/autocomplete-options-advanced-bottom.png"), "advanced options bottom screenshot saved after scrolling");
      auto *profiles = control<QComboBox>(page, "autocompleteProfiles");
      auto *storeProfile = control<QPushButton>(page, "autocompleteStoreProfile");
      auto *removeProfile = control<QPushButton>(page, "autocompleteRemoveProfile");
      auto profileCount = [&persisted]() {
        persisted.sync();
        const int count = persisted.beginReadArray("localAutocomplete/profiles");
        persisted.endArray();
        return count;
      };
      profiles->setEditText("Pending profile");
      storeProfile->click();
      require(profiles->count() == 1 && profileCount() == 0, "new profile stays pending until Options OK");
      options->reject();
      require(profiles->count() == 0 && profiles->currentText().isEmpty() && changed.count() == 1,
              "Cancel discards pending profile creation and selection");
      profiles->setEditText("Studio");
      storeProfile->click();
      LocalCompletionSettings other = candidate;
      other.endpoint = defaults.endpoint;
      other.model = "second-profile-model";
      page->setSettings(other);
      profiles->setEditText("Ollama");
      storeProfile->click();
      options->saveSettings();
      require(profileCount() == 2 && persisted.value("localAutocomplete/selectedProfile").toString() == "Ollama"
              && changed.count() == 2, "OK commits named profiles and their selected entry");
      activate(profiles, profiles->findText("Studio"));
      require(page->settings() == candidate && options->localCompletionSettings() == other,
              "profile switch changes only URL/model and leaves advanced/applied settings intact until OK");
      LocalCompletionSettings updated = candidate;
      updated.model = "updated-profile-model";
      page->setSettings(updated);
      storeProfile->click();
      options->saveSettings();
      require(profileCount() == 2 && changed.count() == 3 && options->localCompletionSettings() == updated,
              "profile update replaces existing entry without duplicating it");
      removeProfile->click();
      require(profiles->count() == 1 && profileCount() == 2, "profile removal is pending until OK");
      options->reject();
      require(profiles->count() == 2 && profiles->currentText() == "Studio" && page->settings() == updated,
              "Cancel restores removed profile, saved selection and fields");
      removeProfile->click();
      options->saveSettings();
      require(profileCount() == 1 && changed.count() == 3, "OK persists profile removal without a false settings notification");
      LocalCompletionSettings invalid = updated;
      invalid.endpoint = "http://example.com/v1";
      page->setSettings(invalid);
      options->saveSettings();
      require(changed.count() == 3 && options->localCompletionSettings() == updated,
              "remote endpoint is rejected without applying settings");
      invalid = updated;
      invalid.shortcut = "A";
      page->setSettings(invalid);
      options->saveSettings();
      require(changed.count() == 3, "unmodified typing shortcut is rejected");
      invalid = updated;
      invalid.shortcut = invalid.acceptShortcut;
      page->setSettings(invalid);
      options->saveSettings();
      require(changed.count() == 3, "manual and acceptance shortcut collision is rejected");
      page->setSettings(updated);
      control<QPlainTextEdit>(page, "autocompleteInstructions")->setPlainText(QString(4001, QLatin1Char('x')));
      options->saveSettings();
      require(changed.count() == 3, "oversized instructions are rejected without truncating applied notes");
      LocalCompletionSettings disabled = updated;
      disabled.enabled = false;
      disabled.endpoint.clear();
      disabled.model.clear();
      page->setSettings(disabled);
      control<QPlainTextEdit>(page, "autocompleteInstructions")->setPlainText(QString(4001, QLatin1Char('x')));
      options->saveSettings();
      require(changed.count() == 3 && options->localCompletionSettings() == updated,
              "disabled feature still rejects oversized notes instead of silently truncating them");
      page->setSettings(disabled);
      options->saveSettings();
      require(changed.count() == 4 && options->localCompletionSettings() == disabled,
              "disabled feature can save irrelevant blank endpoint and model");
      checkDiagram(output);
      checkEditorCompletion(options, output);
      checkPlotResult(output);
      qInfo().noquote() << "SMOKE PASSED; screenshots:" << output + "/autocomplete-options-basic.png"
                       << output + "/autocomplete-options-advanced.png" << output + "/autocomplete-options-advanced-bottom.png";
      app.exit(0);
    } catch (const std::exception &error) {
      qCritical().noquote() << "SMOKE FAILED:" << error.what();
      app.exit(1);
    }
  });
  return app.exec();
  MMC_CATCH_TOP(return 4);
}
