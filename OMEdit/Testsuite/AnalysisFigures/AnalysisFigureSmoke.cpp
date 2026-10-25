#include "OMEditApplication.h"
#include "MainWindow.h"
#include "Plotting/PlotWindowContainer.h"
#include "Plotting/VariablesWidget.h"
#include "Plotting/Analysis/AnalysisFigureWindow.h"
#include "Plotting/Analysis/AnalysisFigureData.h"
#include "Plotting/Analysis/AnalysisWorkerClient.h"
#include "Simulation/SimulationOptions.h"
#include <QAction>
#include <QComboBox>
#include <QMessageBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMdiSubWindow>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

using namespace AnalysisFigures;

static void check(bool condition, const char *message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
  qInfo().noquote() << "PASS:" << message;
}

static bool waitFor(const std::function<bool()> &condition, int timeout = 30000)
{
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout) {
    QTest::qWait(20);
  }
  return condition();
}

template<class T> static T *control(QObject *parent, const char *name)
{
  T *widget = parent->findChild<T *>(name);
  check(widget != nullptr, name);
  return widget;
}

static void assign(AnalysisFigureWindow *window, const char *name, const QString &value)
{
  auto *field = control<QLineEdit>(window, name);
  field->setText(value);
  QMetaObject::invokeMethod(field, "editingFinished", Qt::DirectConnection);
}

static void exportImage(AnalysisWorkerClient *worker, const QString &file)
{
  bool finished = false;
  bool success = false;
  QString error;
  worker->exportFigure("png", file, [&](bool ok, const QString &message) {
    success = ok;
    error = message;
    finished = true;
  });
  check(waitFor([&]() {return finished;}), "figure export completes within deadline");
  if (!success) {
    throw std::runtime_error(error.toStdString());
  }
  QImage image(file);
  check(!image.isNull() && image.width() > 100 && image.height() > 100,
        "worker exports a decodable figure image");
}

static void renderFigure(AnalysisFigureWindow *window, QPushButton *button)
{
  QSignalSpy completed(window, &AnalysisFigureWindow::figureRendered);
  button->click();
  check(!completed.isEmpty() || completed.wait(30000), "builder render completes");
  if (!completed.last().at(0).toBool()) {
    throw std::runtime_error(completed.last().at(1).toString().toStdString());
  }
  check(control<QLabel>(window, "analysisErrors")->text().isEmpty(), "figure resolves without panel errors");
}

static void captureFigure(AnalysisFigureWindow *window, QWebEngineView *view, const QString &path)
{
  QElapsedTimer deadline;
  deadline.start();
  QVariantMap geometry;
  bool fitted = false;
  while (!fitted && deadline.elapsed() < 15000) {
    bool inspected = false;
    view->page()->runJavaScript(R"JS((function() {
      const c = document.querySelector('canvas');
      if (!c) return {ready:false};
      const r = c.getBoundingClientRect();
      const pixels = c.getContext('2d').getImageData(0,0,c.width,c.height).data;
      let ink = 0;
      for (let i=0; i<pixels.length; i+=400) {
        if (pixels[i+3] > 0 && Math.min(pixels[i],pixels[i+1],pixels[i+2]) < 220) ink++;
      }
      return {ready:document.documentElement.dataset.analysisReady==='true',
        width:innerWidth,height:innerHeight,left:r.left,top:r.top,
        canvasWidth:r.width,canvasHeight:r.height,ink:ink};
    })())JS", [&](const QVariant &value) { geometry = value.toMap(); inspected = true; });
    check(waitFor([&]() {return inspected;}, 2000), "figure geometry inspection completes");
    fitted = geometry.value("ready").toBool() && geometry.value("ink").toInt() > 10
        && geometry.value("canvasWidth").toDouble() > 100
        && geometry.value("left").toDouble() + geometry.value("canvasWidth").toDouble() <= geometry.value("width").toDouble() + 2
        && geometry.value("top").toDouble() + geometry.value("canvasHeight").toDouble() <= geometry.value("height").toDouble() + 2;
    if (!fitted) QTest::qWait(100);
  }
  qInfo() << "Canvas geometry:" << geometry;
  check(fitted, "painted canvas fits the embedded viewport");
  QTest::qWait(250);
  check(window->grab().save(path), "settled figure builder screenshot saved");
}

#include "AnalysisCsvSmoke.h"

static QVariant javascript(QWebEngineView *view, const QString &source)
{
  auto result = std::make_shared<QVariant>();
  auto complete = std::make_shared<bool>(false);
  view->page()->runJavaScript(source, [result, complete](const QVariant &value) { *result=value; *complete=true; });
  check(waitFor([complete]() {return *complete;}, 3000), "analysis JavaScript completes");
  return *result;
}

static void checkPlaybackFrames(AnalysisFigureWindow *window, QWebEngineView *view)
{
  javascript(view, R"JS(
    window.analysisFrameSizes=[];
    const previousPaint=fig.imageObj.onload;
    fig.imageObj.onload=function() {
      window.analysisFrameSizes.push([this.naturalWidth,this.naturalHeight,fig.canvas.width,fig.canvas.height]);
      previousPaint.apply(this,arguments);
    };
  )JS");
  auto *slider = control<QSlider>(window, "analysisTimeSlider");
  slider->setValue(0);
  auto *play = control<QPushButton>(window, "analysisPlay");
  QSignalSpy completed(window, &AnalysisFigureWindow::figureRendered);
  play->click();
  check(waitFor([&completed]() {return completed.size() >= 5;}, 8000), "playback renders successive frames");
  play->click();
  check(waitFor([view]() {return javascript(view, "window.analysisFrameSizes.length>=5").toBool();}, 5000),
        "successive playback frames reach the embedded canvas");
  check(javascript(view, R"JS(window.analysisFrameSizes.every(function(size) {
    return size[0]===size[2] && size[1]===size[3]
      && size[0]===Math.round(fig.canvas_div.clientWidth*fig.ratio)
      && size[1]===Math.round(fig.canvas_div.clientHeight*fig.ratio);
  }))JS").toBool(), "every playback frame keeps the viewport scale");
}

int main(int argc, char **argv)
{
  MMC_INIT();
  MMC_TRY_TOP()
  const QString output = qEnvironmentVariable("OMEDIT_ANALYSIS_SMOKE_OUTPUT",
                                              QDir::currentPath() + "/analysis-smoke");
  QDir().mkpath(output);
  QTemporaryDir isolated(output + "/settings-XXXXXX");
  if (!isolated.isValid()) {
    return 2;
  }
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, isolated.path());
  Q_INIT_RESOURCE(resource_omedit);
  OMEditApplication app(argc, argv, threadData, true);
  app.setQuitOnLastWindowClosed(false);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, [&]() {
    qCritical("FAIL: analysis GUI smoke deadline exceeded");
    app.exit(3);
  });
  deadline.start(110000);
  QTimer::singleShot(0, &app, [&]() {
    try {
      const QString result = output + "/analysis-fixture.csv";
      QFile data(result);
      check(data.open(QIODevice::WriteOnly | QIODevice::Truncate), "create analysis result fixture");
      data.write("\"time\",\"x\",\"y\",\"z\",\"temperature[1,1]\",\"temperature[1,2]\",\"temperature[2,1]\",\"temperature[2,2]\"\n");
      for (int i = 0; i <= 100; ++i) {
        const double time = i / 10.0;
        const QString line = QString("%1,%2,%3,%4,%5,%6,%7,%8\n")
            .arg(time).arg(std::sin(time)).arg(std::cos(time)).arg(time / 10.0)
            .arg(time).arg(time + 1).arg(time + 2).arg(time + 4);
        data.write(line.toUtf8());
      }
      data.close();
      MainWindow *main = MainWindow::instance();
      main->resize(1440, 1000);
      main->show();
      main->openResultFile(result);
      auto *action = control<QAction>(main, "newAnalysisFigureAction");
      action->trigger();
      auto *container = main->getPlotWindowContainer();
      auto *subwindow = container->currentSubWindow();
      check(subwindow != nullptr, "menu creates an MDI window in Plotting");
      auto *window = qobject_cast<AnalysisFigureWindow *>(subwindow->widget());
      check(window != nullptr, "menu opens the real Analysis Figure builder");
      checkAnalysisCsvLoading(main, window, result);
      window->addVariable(result, "x");
      auto *worker = window->findChild<AnalysisWorkerClient *>();
      check(worker != nullptr, "builder owns the real worker client");
      check(waitFor([&]() {return worker->isReady();}), "managed plotting worker starts");
      auto *view = control<QWebEngineView>(window, "analysisView");
      check(waitFor([&]() {return view->url().host() == "127.0.0.1";}), "figure web view uses local worker");
      QTest::qWait(1000);
      auto *render = control<QPushButton>(window, "analysisRender");
      renderFigure(window, render);
      exportImage(worker, output + "/time-figure.png");
      captureFigure(window, view, output + "/builder-time.png");

      auto *type = control<QComboBox>(window, "analysisPlotType");
      check(type->findData("trajectory3d") >= 0, "3D trajectory is available without code");
      type->setCurrentIndex(type->findData("trajectory3d"));
      assign(window, "analysisX", "x");
      auto *tree = main->getVariablesWidget()->getVariablesTreeModel();
      auto *variable = tree->findVariablesTreeItem(QFileInfo(result).fileName() + ".y", tree->getRootVariablesTreeItem());
      check(variable != nullptr, "Variables Browser contains trajectory Y variable");
      const QModelIndex variableIndex = tree->variablesTreeItemIndex(variable);
      std::unique_ptr<QMimeData> mime(tree->mimeData({variableIndex}));
      check(mime->hasFormat("application/x-omedit-analysis-variable"), "Variables Browser provides analysis drag data");
      auto *yField = control<QLineEdit>(window, "analysisY");
      QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(yField, &enter);
      QDropEvent drop(QPointF(5, 5), Qt::CopyAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(yField, &drop);
      check(drop.isAccepted() && yField->text() == "y", "dropping a real browser variable assigns the Y field");
      assign(window, "analysisZ", "z");
      renderFigure(window, render);
      exportImage(worker, output + "/trajectory-figure.png");
      captureFigure(window, view, output + "/builder-trajectory.png");
      checkPlaybackFrames(window, view);

      type->setCurrentIndex(type->findData("surface"));
      control<QComboBox>(window, "analysisSources")->setCurrentIndex(0);
      assign(window, "analysisX", "");
      assign(window, "analysisY", "");
      assign(window, "analysisZ", "temperature");
      auto *time = control<QSlider>(window, "analysisTimeSlider");
      time->setValue(time->maximum());
      renderFigure(window, render);
      exportImage(worker, output + "/surface-figure.png");
      captureFigure(window, view, output + "/builder-surface.png");

      bool painted = false;
      QElapsedTimer paintDeadline;
      paintDeadline.start();
      while (!painted && paintDeadline.elapsed() < 10000) {
        bool inspected = false;
        view->page()->runJavaScript(
            "document.querySelectorAll('canvas').length > 0 && document.documentElement.dataset.analysisReady === 'true'",
            [&](const QVariant &value) { painted = value.toBool(); inspected = true; });
        check(waitFor([&]() {return inspected;}, 2000), "canvas inspection completes");
        if (!painted) QTest::qWait(50);
      }
      check(painted, "embedded WebAgg canvas has painted a worker image");
      type->setCurrentIndex(type->findData("trajectory3d"));
      check(control<QLineEdit>(window, "analysisX")->text() == "x"
            && control<QLineEdit>(window, "analysisY")->text() == "y"
            && control<QLineEdit>(window, "analysisZ")->text() == "z",
            "switching through a surface preserves trajectory assignments");
      bool savePrompt = false;
      QTimer::singleShot(0, &app, [&]() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
          if (auto *message = qobject_cast<QMessageBox *>(widget)) {
            savePrompt = true;
            message->done(QMessageBox::Cancel);
          }
        }
      });
      check(!window->close() && savePrompt, "Cancel preserves an unsaved analysis window");
      delete subwindow;
      qInfo().noquote() << "ANALYSIS SMOKE PASSED; evidence:" << output;
      app.exit(0);
    } catch (const std::exception &error) {
      qCritical().noquote() << "ANALYSIS SMOKE FAILED:" << error.what();
      for (auto *subwindow : MainWindow::instance()->getPlotWindowContainer()->subWindowList()) {
        if (qobject_cast<AnalysisFigureWindow *>(subwindow->widget())) delete subwindow;
      }
      app.exit(1);
    }
  });
  return app.exec();
  MMC_CATCH_TOP(return 4);
}
