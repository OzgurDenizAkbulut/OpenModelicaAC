#ifndef ANALYSISCSVSMOKE_H
#define ANALYSISCSVSMOKE_H

#include <QDialog>
#include <QFileDialog>

static QPushButton *analysisButton(QWidget *window, const QString &text)
{
  for (auto *button : window->findChildren<QPushButton *>()) {
    if (button->text() == text) return button;
  }
  throw std::runtime_error(("Missing analysis button: " + text).toStdString());
}

static void chooseAnalysisResult(AnalysisFigureWindow *window, const QString &path)
{
  const bool nativeDialogs = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  bool selected = false;
  QTimer chooser;
  chooser.setInterval(20);
  QObject::connect(&chooser, &QTimer::timeout, window, [&]() {
    auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
    if (!dialog) return;
    auto *fileName = dialog->findChild<QLineEdit *>("fileNameEdit");
    if (!fileName) return;
    fileName->setText(QDir::toNativeSeparators(path));
    if (dialog->selectedFiles().isEmpty()) return;
    chooser.stop();
    qInfo() << "CSV dialog selection:" << dialog->selectedFiles();
    selected = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
  });
  chooser.start();
  analysisButton(window, QString::fromUtf8("Add result…"))->click();
  chooser.stop();
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogs);
  check(selected, "Add result accepts the selected CSV through its file dialog");
}

static void checkAnalysisCsvLoading(MainWindow *main, AnalysisFigureWindow *window, const QString &result)
{
  auto *tree = main->getVariablesWidget()->getVariablesTreeModel();
  main->openResultFile(result);
  check(tree->findVariablesTreeItem(QFileInfo(result).fileName() + ".x", tree->getRootVariablesTreeItem()),
        "Open Result populates CSV variables without an initialization file");
  auto *sources = control<QComboBox>(window, "analysisSources");
  const int sourceCount = sources->count();
  const QString invalid = QFileInfo(result).absolutePath() + "/invalid-result.csv";
  QFile bad(invalid);
  check(bad.open(QIODevice::WriteOnly), "create invalid CSV result");
  bad.write("time,x\n0,not-a-number\n");
  bad.close();
  chooseAnalysisResult(window, invalid);
  check(sources->count() == sourceCount && !control<QLabel>(window, "analysisErrors")->text().isEmpty()
        && !tree->findVariablesTreeItem("invalid-result.csv", tree->getRootVariablesTreeItem()),
        "invalid CSV reports an error without adding a source or browser entry");
  const QString added = QFileInfo(result).absolutePath() + "/added-result.csv";
  check(QFile::copy(result, added), "copy CSV for Analysis Figure Add result path");
  chooseAnalysisResult(window, added);
  check(sources->currentText() == "added-result.csv", "Add result selects its CSV source");
  check(control<QLabel>(window, "analysisErrors")->text().isEmpty(), "valid CSV import clears the previous loading error");
  check(tree->findVariablesTreeItem("added-result.csv.x", tree->getRootVariablesTreeItem()),
        "Add result populates CSV variables in Variables Browser");
  analysisButton(window, QString::fromUtf8("Variables…"))->click();
  QDialog *variables = nullptr;
  for (auto *dialog : window->findChildren<QDialog *>()) {
    if (dialog->windowTitle() == "Available result variables") variables = dialog;
  }
  check(variables != nullptr, "Variables opens the available CSV variable list");
  auto *names = variables->findChild<QComboBox *>();
  check(names && names->findText("x") >= 0 && names->findText("temperature[1,1]") >= 0,
        "CSV variable list contains scalar and quoted array column names");
  names->setCurrentText("x");
  analysisButton(variables, "Assign to Y / surface Z")->click();
  check(control<QLineEdit>(window, "analysisY")->text() == "x", "CSV variable selection assigns Y");
  assign(window, "analysisY", "");
}

#endif
