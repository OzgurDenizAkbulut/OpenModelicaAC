#ifndef ANALYSISWORKERCLIENT_H
#define ANALYSISWORKERCLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QHash>
#include <QUrl>
#include <functional>

namespace AnalysisFigures {
class AnalysisWorkerClient : public QObject
{
  Q_OBJECT
public:
  explicit AnalysisWorkerClient(QObject *parent = nullptr);
  ~AnalysisWorkerClient() override;
  void start();
  bool isReady() const { return mReady; }
  void render(const QJsonObject &figure, std::function<void(bool, QString)> completion = {});
  void exportFigure(const QString &format, const QString &path, std::function<void(bool, QString)> completion);
signals:
  void ready(const QUrl &url);
  void error(const QString &message);
private:
  struct Request { std::function<void(bool, QString)> completion; qint64 deadline; };
  void request(QJsonObject object, std::function<void(bool, QString)> completion);
  void readOutput();
  void fail(const QString &message);
  QProcess mProcess;
  QTimer mStartupTimer;
  QTimer mRequestTimer;
  QByteArray mOutput;
  QByteArray mDiagnostics;
  QString mToken;
  QHash<int, Request> mRequests;
  int mNextId = 0;
  bool mReady = false;
  bool mStopping = false;
};
}
#endif
