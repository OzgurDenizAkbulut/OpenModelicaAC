#include "AnalysisWorkerClient.h"
#include "Util/Helper.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>

using namespace AnalysisFigures;

AnalysisWorkerClient::AnalysisWorkerClient(QObject *parent) : QObject(parent)
{
  mStartupTimer.setSingleShot(true);
  connect(&mStartupTimer, &QTimer::timeout, this, [this] {
    fail(tr("Analysis runtime did not become ready within 20 seconds."));
    mProcess.kill();
  });
  connect(&mProcess, &QProcess::readyReadStandardOutput, this, &AnalysisWorkerClient::readOutput);
  connect(&mProcess, &QProcess::readyReadStandardError, this, [this] {
    mDiagnostics += mProcess.readAllStandardError();
    mDiagnostics = mDiagnostics.right(8192);
  });
  connect(&mProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    if (!mStopping) fail(tr("Cannot run analysis runtime: %1").arg(mProcess.errorString()));
  });
  connect(&mProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus) {
    if (!mStopping) fail(tr("Analysis runtime exited (%1). %2").arg(code).arg(QString::fromUtf8(mDiagnostics)));
  });
  mRequestTimer.setInterval(1000);
  connect(&mRequestTimer, &QTimer::timeout, this, [this] {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int id : mRequests.keys()) {
      if (mRequests.value(id).deadline < now) {
        fail(tr("Analysis request timed out. Reopen the window to restart the runtime."));
        mProcess.kill();
        return;
      }
    }
  });
  mRequestTimer.start();
}

AnalysisWorkerClient::~AnalysisWorkerClient()
{
  mStopping = true;
  if (mProcess.state() != QProcess::NotRunning) {
    mProcess.write("{\"op\":\"shutdown\"}\n");
    mProcess.closeWriteChannel();
    if (!mProcess.waitForFinished(300)) {
      mProcess.terminate();
      if (!mProcess.waitForFinished(300)) { mProcess.kill(); mProcess.waitForFinished(1000); }
    }
  }
}

void AnalysisWorkerClient::start()
{
  if (mProcess.state() != QProcess::NotRunning) return;
  const QDir prefix(Helper::OpenModelicaHome.isEmpty()
      ? QCoreApplication::applicationDirPath() + "/.." : Helper::OpenModelicaHome);
  const QString analysis = prefix.filePath("share/omedit/analysis");
  QString python = qEnvironmentVariable("OMEDIT_ANALYSIS_PYTHON");
  if (python.isEmpty()) {
#ifdef Q_OS_WIN
    python = analysis + "/runtime/python.exe";
#else
    python = analysis + "/runtime/bin/python3";
#endif
  }
  QString worker = analysis + "/analysis_worker.py";
#ifdef OMEDIT_ANALYSIS_WORKER_PATH
  if (!QFileInfo::exists(worker)) worker = QString::fromUtf8(OMEDIT_ANALYSIS_WORKER_PATH);
#endif
  if (!QFileInfo::exists(python) || !QFileInfo::exists(worker)) {
    emit error(tr("Analysis figures require the optional managed plotting runtime. Install the analysis runtime component. Developers may set OMEDIT_ANALYSIS_PYTHON to a Python executable containing Matplotlib and Tornado.\nRuntime: %1\nWorker: %2").arg(python, worker));
    return;
  }
  mToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
  mReady = false;
  mOutput.clear();
  mDiagnostics.clear();
  mProcess.start(python, {worker, "--token", mToken, "--port", "0"});
  mStartupTimer.start(20000);
}

void AnalysisWorkerClient::fail(const QString &message)
{
  mReady = false;
  mStartupTimer.stop();
  const auto pending = mRequests;
  mRequests.clear();
  for (const auto &request : pending) if (request.completion) request.completion(false, message);
  emit error(message);
}

void AnalysisWorkerClient::readOutput()
{
  mOutput += mProcess.readAllStandardOutput();
  if (mOutput.size() > 1024 * 1024) { fail(tr("Invalid analysis runtime response.")); mProcess.kill(); return; }
  int newline;
  while ((newline = mOutput.indexOf('\n')) >= 0) {
    const QByteArray line = mOutput.left(newline);
    mOutput.remove(0, newline + 1);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      fail(tr("Invalid JSON response from analysis runtime.")); continue;
    }
    const auto object = document.object();
    if (object.value("event").toString() == "ready") {
      const QUrl url(object.value("url").toString());
      if (url.scheme() != "http" || url.host() != "127.0.0.1" || url.port() <= 0
          || !url.query().contains("token=" + mToken)) {
        fail(tr("Analysis runtime returned an invalid local address.")); mProcess.kill(); return;
      }
      mStartupTimer.stop(); mReady = true; emit ready(url);
    } else {
      const int id = object.value("id").toInt(-1);
      if (!mRequests.contains(id)) continue;
      const auto request = mRequests.take(id);
      if (request.completion) request.completion(object.value("ok").toBool(), object.value("error").toString());
    }
  }
}

void AnalysisWorkerClient::request(QJsonObject object, std::function<void(bool, QString)> completion)
{
  if (!mReady) { if (completion) completion(false, tr("Analysis runtime is unavailable.")); return; }
  const int id = ++mNextId;
  object.insert("id", id);
  mRequests.insert(id, {completion, QDateTime::currentMSecsSinceEpoch() + 60000});
  mProcess.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}

void AnalysisWorkerClient::render(const QJsonObject &figure, std::function<void(bool, QString)> completion)
{ request({{"op", "render"}, {"figure", figure}}, completion); }

void AnalysisWorkerClient::exportFigure(const QString &format, const QString &path, std::function<void(bool, QString)> completion)
{ request({{"op", "export"}, {"format", format}, {"path", path}}, completion); }
