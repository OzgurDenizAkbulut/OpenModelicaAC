#ifndef PLOTRESULTSMOKE_H
#define PLOTRESULTSMOKE_H

#include "MainWindow.h"
#include "Plotting/VariablesWidget.h"
#include "Plotting/PlotWindowContainer.h"
#include "Simulation/SimulationOptions.h"
#include "OMPlot.h"
#include <QFileInfo>
#include <cmath>
#include <stdexcept>

inline void checkPlotResult(const QString &output)
{
  const QString resultPath = qEnvironmentVariable("OMEDIT_SMOKE_RESULT");
  if (resultPath.isEmpty()) {
    qInfo("SKIP: plotting requires OMEDIT_SMOKE_RESULT from the compiler acceptance run");
    return;
  }
  auto check = [](bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
    qInfo().noquote() << "PASS:" << message;
  };
  const QFileInfo result(resultPath);
  check(result.isFile(), "candidate simulation MAT result exists for GUI plotting");
  MainWindow *main = MainWindow::instance();
  VariablesWidget *variables = main->getVariablesWidget();
  variables->insertVariablesItemsToTree(result.fileName(), result.absolutePath(), QStringList(), SimulationOptions());
  VariablesTreeModel *tree = variables->getVariablesTreeModel();
  VariablesTreeItem *x = tree->findVariablesTreeItem(result.fileName() + ".x", tree->getRootVariablesTreeItem());
  check(x && x->getExistInResultFile(), "real Variables Browser loads x from the candidate MAT result");
  main->switchToPlottingPerspectiveSlot();
  main->show();
  OMPlot::PlotWindow *plot = main->getPlotWindowContainer()->addPlotWindow();
  check(plot != nullptr, "real OMPlot window opens inside OMEdit");
  qApp->processEvents();
  const QModelIndex index = tree->variablesTreeItemIndex(x);
  check(index.isValid() && tree->setData(index, Qt::Checked, Qt::CheckStateRole),
        "checking x in the real Variables Browser triggers plotting");
  const QList<OMPlot::PlotCurve *> curves = plot->getPlot()->getPlotCurvesList();
  check(curves.size() == 1, "real Variables Browser creates one plot curve");
  OMPlot::PlotCurve *curve = curves.first();
  check(QFileInfo(curve->getAbsoluteFilePath()).canonicalFilePath() == result.canonicalFilePath()
        && curve->getYVariable() == "x", "plot curve uses x from the exact candidate result file");
  const auto &times = curve->mXAxisVector;
  const auto &values = curve->mYAxisVector;
  check(times.size() > 1 && times.size() == values.size(), "plot curve contains actual time and result samples");
  check(std::abs(times.at(0)) < 1e-12 && std::abs(times.at(times.size() - 1) - 1.0) < 1e-12
        && std::abs(values.at(0) - 1.0) < 1e-5 && std::abs(values.at(values.size() - 1) - std::exp(-1.0)) < 1e-5,
        "plotted result samples match the candidate simulation decay");
  qApp->processEvents();
  check(plot->grab().save(output + "/simulation-result-plot.png"), "real simulation result plot screenshot saved");
}

#endif
