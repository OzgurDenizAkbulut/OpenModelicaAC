#include "AnalysisFigureWindow.h"
#include "AnalysisFigureData.h"
#include "AnalysisWorkerClient.h"
#include "MainWindow.h"
#include "OMC/OMCProxy.h"
#include "Plotting/VariablesWidget.h"
#include "Simulation/SimulationOptions.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDialog>
#include <QPixmap>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <memory>
#if !defined(__EMSCRIPTEN__) && !defined(OM_OMEDIT_NO_WEBENGINE)
#include <QWebEngineView>
#endif

using namespace AnalysisFigures;

namespace {
class VariableField : public QLineEdit
{
public:
  explicit VariableField(QWidget *parent = nullptr) : QLineEdit(parent) { setAcceptDrops(true); }
  std::function<void(QJsonObject)> dropped;
protected:
  void dragEnterEvent(QDragEnterEvent *event) override {
    if (event->mimeData()->hasFormat("application/x-omedit-analysis-variable")) event->acceptProposedAction();
    else QLineEdit::dragEnterEvent(event);
  }
  void dropEvent(QDropEvent *event) override {
    if (!event->mimeData()->hasFormat("application/x-omedit-analysis-variable")) { QLineEdit::dropEvent(event); return; }
    const auto object = QJsonDocument::fromJson(event->mimeData()->data("application/x-omedit-analysis-variable")).object();
    if (!object.value("file").toString().isEmpty() && !object.value("variable").toString().isEmpty()) {
      if (dropped) dropped(object);
      event->acceptProposedAction();
    }
  }
};
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QJsonObject newSeries() {
  return {{"x", "time"}, {"y", ""}, {"z", ""}, {"color", ""}, {"xUnit", "s"},
          {"xOriginalUnit", "s"}, {"xScale", 1}, {"yScale", 1}, {"zScale", 1}};
}
QJsonObject newPanel() {
  return {{"id", newId()}, {"type", "time"}, {"legend", true}, {"grid", true},
          {"series", QJsonArray{newSeries()}}, {"surface", QJsonObject{}}};
}
}

AnalysisFigureWindow::AnalysisFigureWindow(QWidget *parent) : QWidget(parent),
  mDefinition(AnalysisFigureData::defaultDefinition()), mpWorker(new AnalysisWorkerClient(this))
{
  setObjectName("analysisFigureWindow");
  setWindowTitle(tr("Analysis Figure[*]"));
  setWindowIcon(QIcon(":/Resources/icons/analysis-figure.svg"));
  if (mDefinition.value("panels").toArray().isEmpty()) mDefinition.insert("panels", QJsonArray{newPanel()});
  auto *layout = new QVBoxLayout(this);
  auto *toolbar = new QHBoxLayout;
  auto button = [this](QHBoxLayout *row, const QString &text, std::function<void()> action) {
    auto *control = new QPushButton(text, this); row->addWidget(control);
    connect(control, &QPushButton::clicked, this, action); return control;
  };
  button(toolbar, tr("Open…"), [this] { openFigure(); });
  button(toolbar, tr("Save…"), [this] { saveFigure(); });
  button(toolbar, tr("Pin run…"), [this] { pinSource(); });
  button(toolbar, tr("Package…"), [this] {
    readControls(); const QString path = QFileDialog::getSaveFileName(this, tr("New portable figure directory"));
    if (path.isEmpty()) return;
    QString error; if (!AnalysisFigureData::exportPackage(mDefinition, path, &error)) showError(error);
  });
  button(toolbar, tr("CSV…"), [this] {
    readControls(); const QString path = QFileDialog::getSaveFileName(this, tr("Export plotted data"), {}, tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QString error; if (!AnalysisFigureData::exportCsv(mDefinition, path, &error)) showError(error);
  });
  button(toolbar, tr("Preview…"), [this] { exportPreview(); });
  button(toolbar, tr("Export…"), [this] { exportFigure(); });
  button(toolbar, tr("Refresh"), [this] { readControls(); scheduleRender(); })->setObjectName("analysisRender");
  toolbar->addStretch(); layout->addLayout(toolbar);
  auto *splitter = new QSplitter(this); layout->addWidget(splitter, 1);
  auto *scroll = new QScrollArea(splitter); scroll->setWidgetResizable(true); scroll->setMinimumWidth(330);
  auto *controls = new QWidget; auto *form = new QFormLayout(controls); scroll->setWidget(controls);
  mpTitle = new QLineEdit; form->addRow(tr("Figure title"), mpTitle);
  mpSources = new QComboBox; mpSources->setObjectName("analysisSources"); form->addRow(tr("Result source"), mpSources);
  auto *sourceButtons = new QHBoxLayout;
  button(sourceButtons, tr("Add result…"), [this] {
    const QString path = QFileDialog::getOpenFileName(this, tr("Simulation result"), {}, tr("Results (*.mat *.csv *.arrow);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    const auto names = AnalysisFigureData::variables(path, &error);
    if (!error.isEmpty() || names.isEmpty()) {
      showError(error.isEmpty() ? tr("The result contains no available variables.") : error);
      return;
    }
    readControls();
    const QFileInfo result(path);
    MainWindow::instance()->getVariablesWidget()->insertVariablesItemsToTree(
        result.fileName(), result.absolutePath(), names, SimulationOptions());
    const QString id = ensureSource(path); auto p=panel();
    if (p.value("type")=="surface" || p.value("type")=="heatmap") { auto s=p.value("surface").toObject(); s.insert("source", id); p.insert("surface", s); putPanel(p); }
    else { auto s=series(); s.insert("source", id); putSeries(s); }
    showError({}); reloadControls(); scheduleRender();
  });
  button(sourceButtons, tr("Variables…"), [this] {
    const auto sources = mDefinition.value("sources").toArray();
    if (mpSources->currentIndex() < 0 || mpSources->currentIndex() >= sources.size()) return;
    const QString file=sources[mpSources->currentIndex()].toObject().value("path").toString();
    QString error; const auto names = AnalysisFigureData::variables(file, &error);
    if (!error.isEmpty()) { showError(error); return; }
    auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("Available result variables"));
    auto *box = new QVBoxLayout(dialog); auto *list = new QComboBox; list->setEditable(true); list->addItems(names); box->addWidget(list);
    auto *add = new QPushButton(tr("Assign to Y / surface Z")); box->addWidget(add);
    connect(add, &QPushButton::clicked, dialog, [this, list, dialog, file] {
      assign((mpType->currentData() == "surface" || mpType->currentData() == "heatmap") ? "z" : "y", {{"file", file}, {"variable", list->currentText()}}); dialog->close();
    }); dialog->show();
  }); form->addRow(sourceButtons);
  mpPanels = new QComboBox; mpPanels->setObjectName("analysisPanels"); form->addRow(tr("Panel"), mpPanels);
  auto *panelButtons = new QHBoxLayout;
  button(panelButtons, tr("Add panel"), [this] { readControls(); auto a = mDefinition.value("panels").toArray(); if (a.size() >= 256) return; a.append(newPanel()); mDefinition.insert("panels", a); int columns=mpColumns->value(); if (a.size()>16*columns) { columns=(int(a.size())+15)/16; mDefinition.insert("columns", columns); } mDefinition.insert("rows", qMax(mpRows->value(), (int(a.size())+columns-1)/columns)); reloadControls(); mpPanels->setCurrentIndex(a.size()-1); scheduleRender(); });
  button(panelButtons, tr("Remove panel"), [this] { auto a = mDefinition.value("panels").toArray(); if (a.size() <= 1) return; a.removeAt(mpPanels->currentIndex()); mDefinition.insert("panels", a); reloadControls(); scheduleRender(); });
  form->addRow(panelButtons);
  mpType = new QComboBox; mpType->setObjectName("analysisPlotType");
  const QStringList types{"time", "xy", "scatter", "histogram", "heatmap", "trajectory3d", "scatter3d", "surface"};
  const QStringList titles{tr("Time series"), tr("XY lines"), tr("Scatter"), tr("Histogram"), tr("Heatmap"), tr("3D trajectory"), tr("3D scatter"), tr("3D surface")};
  for (int i = 0; i < types.size(); ++i) mpType->addItem(titles[i], types[i]);
  form->addRow(tr("Plot type"), mpType);
  mpSeries = new QComboBox; mpSeries->setObjectName("analysisSeries"); form->addRow(tr("Series"), mpSeries);
  auto *seriesButtons = new QHBoxLayout;
  button(seriesButtons, tr("Add series"), [this] { readControls(); auto p = panel(); auto a = p.value("series").toArray(); auto s = newSeries(); s.insert("source", mpSources->currentData().toString()); a.append(s); p.insert("series", a); putPanel(p); reloadControls(); mpSeries->setCurrentIndex(a.size()-1); scheduleRender(); });
  button(seriesButtons, tr("Remove series"), [this] { auto p = panel(); auto a = p.value("series").toArray(); if (a.size() <= 1) return; a.removeAt(mpSeries->currentIndex()); p.insert("series", a); putPanel(p); reloadControls(); scheduleRender(); });
  form->addRow(seriesButtons);
  for (const QString &key : QStringList{"x", "y", "z", "color"}) {
    auto *field = new VariableField; mFields.insert(key, field);
    field->setObjectName("analysis" + key.left(1).toUpper() + key.mid(1));
    field->setPlaceholderText(tr("Type a variable or drop from Variables Browser"));
    field->dropped = [this, key](QJsonObject value) { assign(key, value); };
    form->addRow(key == "color" ? tr("Color variable") : key.toUpper() + tr(" variable"), field);
    connect(field, &QLineEdit::editingFinished, this, [this] { readControls(); scheduleRender(); });
    if (key != "color") {
      auto *unit = new QLineEdit; mFields.insert(key + "Unit", unit); unit->setPlaceholderText(tr("Display unit"));
      unit->setObjectName("analysis" + key.toUpper() + "Unit");
      form->addRow(key.toUpper() + tr(" unit"), unit);
      connect(unit, &QLineEdit::editingFinished, this, [this] { readControls(); scheduleRender(); });
    }
  }
  for (const QString &key : QStringList{"title", "label", "xLabel", "yLabel", "zLabel", "linkX"}) {
    auto *field = new QLineEdit; mLabels.insert(key, field);
    const QMap<QString, QString> names{{"title", tr("Panel title")}, {"label", tr("Series legend")}, {"xLabel", tr("X label")}, {"yLabel", tr("Y label")}, {"zLabel", tr("Z label")}, {"linkX", tr("Link X group")}};
    form->addRow(names.value(key), field);
    connect(field, &QLineEdit::editingFinished, this, [this] { readControls(); scheduleRender(); });
  }
  for (const QString &key : QStringList{"legend", "grid", "logX", "logY"}) {
    const QMap<QString, QString> names{{"legend", tr("Show legend")}, {"grid", tr("Show grid")}, {"logX", tr("Logarithmic X axis")}, {"logY", tr("Logarithmic Y axis")}};
    auto *check = new QCheckBox; mChecks.insert(key, check); form->addRow(names.value(key), check);
    connect(check, &QCheckBox::toggled, this, [this] { readControls(); scheduleRender(); });
  }
  auto spin = [form](const QString &name, int low, int high) { auto *s = new QSpinBox; s->setRange(low, high); form->addRow(name, s); return s; };
  mpRows = spin(tr("Grid rows"), 1, 16); mpColumns = spin(tr("Grid columns"), 1, 16);
  mpRows->setObjectName("analysisRows"); mpColumns->setObjectName("analysisColumns");
  auto size = [form](const QString &name) { auto *s = new QDoubleSpinBox; s->setRange(1, 100); s->setSuffix(" in"); form->addRow(name, s); return s; };
  mpWidth = size(tr("Export width")); mpHeight = size(tr("Export height"));
  mpDpi = spin(tr("Export DPI"), 36, 1200); mpFont = spin(tr("Font size"), 4, 72);
  mpBackground = new QLineEdit; form->addRow(tr("Background color"), mpBackground);
  for (auto *s : {mpRows, mpColumns, mpDpi, mpFont}) connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { readControls(); scheduleRender(); });
  for (auto *s : {mpWidth, mpHeight}) connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this] { readControls(); scheduleRender(); });
  for (auto *s : {mpTitle, mpBackground}) connect(s, &QLineEdit::editingFinished, this, [this] { readControls(); scheduleRender(); });
#if !defined(__EMSCRIPTEN__) && !defined(OM_OMEDIT_NO_WEBENGINE)
  mpView = new QWebEngineView(splitter); mpView->setObjectName("analysisView");
  connect(mpView, &QWebEngineView::loadFinished, this, [this](bool ok) { if (!ok) showError(tr("The local analysis renderer page could not be loaded.")); });
#else
  auto *unavailable = new QLabel(tr("Analysis rendering requires a build with Qt WebEngine."), splitter); unavailable->setWordWrap(true);
#endif
  splitter->setStretchFactor(1, 1);
  auto *playback = new QHBoxLayout; mpPlay = new QPushButton(tr("Play")); playback->addWidget(mpPlay);
  mpPlay->setObjectName("analysisPlay");
  mpPlayback = new QSlider(Qt::Horizontal); mpPlayback->setObjectName("analysisTimeSlider"); mpPlayback->setRange(0, 0); playback->addWidget(mpPlayback, 1);
  mpTime = new QLabel(tr("Time: —")); playback->addWidget(mpTime); layout->addLayout(playback);
  mpErrors = new QLabel; mpErrors->setObjectName("analysisErrors"); mpErrors->setWordWrap(true); mpErrors->setTextInteractionFlags(Qt::TextSelectableByMouse); mpErrors->setStyleSheet("color: #b3261e"); layout->addWidget(mpErrors);
  mRenderTimer.setSingleShot(true); mRenderTimer.setInterval(180); connect(&mRenderTimer, &QTimer::timeout, this, &AnalysisFigureWindow::render);
  mPlaybackTimer.setInterval(100); connect(&mPlaybackTimer, &QTimer::timeout, this, [this] {
    if (mpPlayback->value() >= mpPlayback->maximum()) { mPlaybackTimer.stop(); mpPlay->setText(tr("Play")); return; }
    mpPlayback->setValue(mpPlayback->value() + qMax(1, mpPlayback->maximum()/100));
  });
  connect(mpPlay, &QPushButton::clicked, this, [this] { if (mPlaybackTimer.isActive()) { mPlaybackTimer.stop(); mpPlay->setText(tr("Play")); } else { if (mpPlayback->value() == mpPlayback->maximum()) mpPlayback->setValue(0); mPlaybackTimer.start(); mpPlay->setText(tr("Pause")); } });
  connect(mpPlayback, &QSlider::valueChanged, this, [this](int index) { if (index < mTimes.size()) { mTime = mTimes[index]; mpTime->setText(tr("Time: %1 s").arg(mTime, 0, 'g', 10)); scheduleRender(); } });
  connect(mpPanels, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { if (!mLoading) reloadControls(); });
  connect(mpSeries, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { if (!mLoading) reloadControls(); });
  connect(mpSources, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { if (!mLoading) { readControls(); scheduleRender(); } });
  connect(mpType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
    if (mLoading) return;
    auto p=panel(); const QString type=mpType->currentData().toString(); p.insert("type", type);
    if (type=="time") {
      auto all=p.value("series").toArray();
      for (int i=0; i<all.size(); ++i) {
        auto s=all[i].toObject();
        if (s.value("x").toString()!="time") {
          s.insert("x", "time"); s.insert("xOriginalUnit", "s"); s.insert("xUnit", "s");
          s.insert("xScale", 1); s.insert("xOffset", 0); s.insert("xUnitConversionVerified", false);
          all.replace(i, s);
        }
      }
      p.insert("series", all);
    }
    putPanel(p); reloadControls(); scheduleRender();
  });
  connect(mpWorker, &AnalysisWorkerClient::error, this, &AnalysisFigureWindow::showError);
  connect(mpWorker, &AnalysisWorkerClient::ready, this, [this](const QUrl &url) {
#if !defined(__EMSCRIPTEN__) && !defined(OM_OMEDIT_NO_WEBENGINE)
    mpView->load(url);
#else
    Q_UNUSED(url);
#endif
    scheduleRender();
  });
  reloadControls(); readControls(); mSavedDefinition=mDefinition; updateModified();
  QTimer::singleShot(0, mpWorker, &AnalysisWorkerClient::start);
}

QJsonObject AnalysisFigureWindow::panel() const
{ const auto a=mDefinition.value("panels").toArray(); const int i=qMax(0, mpPanels->currentIndex()); return i<a.size() ? a.at(i).toObject() : QJsonObject(); }
QJsonObject AnalysisFigureWindow::series() const
{ const auto a=panel().value("series").toArray(); const int i=qMax(0, mpSeries->currentIndex()); return i<a.size() ? a.at(i).toObject() : QJsonObject(); }
void AnalysisFigureWindow::putPanel(const QJsonObject &p)
{ auto a = mDefinition.value("panels").toArray(); const int i = qMax(0, mpPanels->currentIndex()); if (i < a.size()) a.replace(i, p); mDefinition.insert("panels", a); }
void AnalysisFigureWindow::putSeries(const QJsonObject &s)
{ auto p = panel(); auto a = p.value("series").toArray(); const int i = qMax(0, mpSeries->currentIndex()); if (a.isEmpty()) a.append(s); else if (i < a.size()) a.replace(i, s); p.insert("series", a); putPanel(p); }

void AnalysisFigureWindow::reloadControls()
{
  mLoading = true;
  const int pi = qMax(0, mpPanels->currentIndex()), si = qMax(0, mpSeries->currentIndex());
  mpPanels->clear(); const auto panels = mDefinition.value("panels").toArray();
  for (int i=0; i<panels.size(); ++i) mpPanels->addItem(tr("Panel %1: %2").arg(i+1).arg(panels[i].toObject().value("type").toString()));
  mpPanels->setCurrentIndex(qMin(pi, int(panels.size())-1));
  auto p = panel(); const auto allSeries = p.value("series").toArray(); mpSeries->clear();
  for (int i=0; i<allSeries.size(); ++i) mpSeries->addItem(tr("Series %1: %2").arg(i+1).arg(allSeries[i].toObject().value("y").toString()));
  mpSeries->setCurrentIndex(qMin(si, int(allSeries.size())-1)); auto s = series();
  mpSources->clear(); for (const auto &value : mDefinition.value("sources").toArray()) { const auto source = value.toObject(); mpSources->addItem(QFileInfo(source.value("path").toString()).fileName() + (source.value("pinned").toBool() ? tr(" [pinned]") : ""), source.value("id").toString()); }
  const bool surface = p.value("type") == "surface" || p.value("type") == "heatmap";
  const auto assignment = surface ? p.value("surface").toObject() : s;
  mpSources->setCurrentIndex(mpSources->findData(assignment.value("source").toString()));
  mpType->setCurrentIndex(qMax(0, mpType->findData(p.value("type").toString())));
  for (auto it=mFields.begin(); it!=mFields.end(); ++it) it.value()->setText(assignment.value(it.key()).toString());
  mFields["z"]->setPlaceholderText(surface ? tr("Rectangular array base, e.g. temperature") : tr("Z variable"));
  mFields["x"]->setPlaceholderText(surface ? tr("Coordinate array; blank uses column indices") : tr("X variable"));
  mFields["y"]->setPlaceholderText(surface ? tr("Coordinate array; blank uses row indices") : tr("Y variable"));
  for (auto it=mLabels.begin(); it!=mLabels.end(); ++it) it.value()->setText((it.key()=="label" ? s : p).value(it.key()).toString());
  for (auto it=mChecks.begin(); it!=mChecks.end(); ++it) it.value()->setChecked(p.value(it.key()).toBool());
  mpRows->setValue(mDefinition.value("rows").toInt(1)); mpColumns->setValue(mDefinition.value("columns").toInt(1));
  mpWidth->setValue(mDefinition.value("width").toDouble(10)); mpHeight->setValue(mDefinition.value("height").toDouble(6));
  mpDpi->setValue(mDefinition.value("dpi").toInt(120)); mpFont->setValue(mDefinition.value("fontSize").toInt(10));
  mpBackground->setText(mDefinition.value("background").toString("white")); mpTitle->setText(mDefinition.value("title").toString());
  const QString type=p.value("type").toString();
  const bool hasZ=surface || type=="trajectory3d" || type=="scatter3d";
  mpSeries->setEnabled(!surface);
  mFields["x"]->setEnabled(type!="histogram" && type!="time"); mFields["xUnit"]->setEnabled(type!="histogram");
  mFields["z"]->setEnabled(hasZ); mFields["zUnit"]->setEnabled(hasZ);
  mFields["color"]->setEnabled(type=="scatter" || type=="scatter3d");
  mLoading = false;
}

void AnalysisFigureWindow::readControls()
{
  if (mLoading) return;
  auto p = panel(), s = series();
  p.insert("type", mpType->currentData().toString());
  const bool surface = mpType->currentData() == "surface" || mpType->currentData() == "heatmap";
  auto assignment = surface ? p.value("surface").toObject() : s;
  assignment.insert("source", mpSources->currentData().toString());
  for (const QString &axis : QStringList{"x", "y", "z", "color"}) {
    const QString variable=mFields[axis]->text().trimmed();
    if (assignment.value(axis).toString()!=variable && axis!="color") {
      const QString unit=variable=="time" ? "s" : "";
      assignment.insert(axis+"OriginalUnit", unit); assignment.insert(axis+"Unit", unit);
      assignment.insert(axis+"UnitConversionVerified", false);
      mFields[axis+"Unit"]->setText(unit);
    }
    assignment.insert(axis, variable);
  }
  for (const QString &axis : QStringList{"x", "y", "z"}) {
    const QString unit = mFields[axis+"Unit"]->text().trimmed();
    const QString original = assignment.value(axis+"OriginalUnit").toString(assignment.value(axis+"Unit").toString());
    if (original != unit && !unit.isEmpty()) {
      if (original.isEmpty()) { showError(tr("The original %1 unit is unknown. Drop the variable from Variables Browser before converting units.").arg(axis.toUpper())); mFields[axis+"Unit"]->setText(assignment.value(axis+"Unit").toString()); continue; }
      const auto conversion = MainWindow::instance()->getOMCProxy()->convertUnits(original, unit);
      if (!conversion.unitsCompatible || conversion.scaleFactor == 0) { showError(tr("Cannot convert %1 to incompatible unit %2.").arg(original, unit)); mFields[axis+"Unit"]->setText(assignment.value(axis+"Unit").toString()); continue; }
      assignment.insert(axis+"Scale", 1.0/conversion.scaleFactor); assignment.insert(axis+"Offset", -conversion.offset/conversion.scaleFactor);
      assignment.insert(axis+"UnitConversionVerified", true);
    } else { assignment.insert(axis+"Scale", 1); assignment.insert(axis+"Offset", 0); }
    assignment.insert(axis+"Unit", unit); assignment.insert(axis+"OriginalUnit", original);
  }
  for (auto it=mLabels.begin(); it!=mLabels.end(); ++it) { if (it.key()=="label") assignment.insert("label", it.value()->text()); else p.insert(it.key(), it.value()->text()); }
  for (auto it=mChecks.begin(); it!=mChecks.end(); ++it) p.insert(it.key(), it.value()->isChecked());
  if (surface) p.insert("surface", assignment); else { auto a=p.value("series").toArray(); if (a.isEmpty()) a.append(assignment); else a.replace(qMax(0, mpSeries->currentIndex()), assignment); p.insert("series", a); }
  putPanel(p);
  mDefinition.insert("title", mpTitle->text()); mDefinition.insert("rows", mpRows->value()); mDefinition.insert("columns", mpColumns->value());
  mDefinition.insert("width", mpWidth->value()); mDefinition.insert("height", mpHeight->value()); mDefinition.insert("dpi", mpDpi->value()); mDefinition.insert("fontSize", mpFont->value()); mDefinition.insert("background", mpBackground->text());
}

QString AnalysisFigureWindow::ensureSource(const QString &file)
{
  const QString path = QFileInfo(file).absoluteFilePath(); auto sources = mDefinition.value("sources").toArray();
  for (const auto &v : sources) { const auto s=v.toObject(); if (s.value("path").toString()==path) return s.value("id").toString(); }
  const QString id = newId(); sources.append(QJsonObject{{"id", id}, {"path", path}, {"originalPath", path}, {"pinned", false}}); mDefinition.insert("sources", sources); return id;
}
void AnalysisFigureWindow::assign(const QString &field, const QJsonObject &variable)
{
  readControls(); const QString source=ensureSource(variable.value("file").toString()); auto p=panel();
  const bool surface=p.value("type")=="surface" || p.value("type")=="heatmap";
  auto s=surface ? p.value("surface").toObject() : series();
  if (!s.value("source").toString().isEmpty() && s.value("source").toString()!=source) {
    for (const QString &key : QStringList{"x", "y", "z", "color"}) s.insert(key, key=="x" && !surface ? "time" : "");
  }
  s.insert("source", source); s.insert(field, variable.value("variable"));
  if (field!="color") { const QString unit=variable.value("unit").toString(); s.insert(field+"OriginalUnit", unit); s.insert(field+"Unit", unit); s.insert(field+"Scale", 1); s.insert(field+"Offset", 0); s.insert(field+"UnitConversionVerified", false); }
  if (surface) { p.insert("surface", s); putPanel(p); } else putSeries(s);
  reloadControls(); const QString display=variable.value("displayUnit").toString();
  if (field!="color" && !display.isEmpty()) { mFields[field+"Unit"]->setText(display); readControls(); }
  scheduleRender();
}
void AnalysisFigureWindow::addVariable(const QString &file, const QString &variable, const QString &unit, const QString &displayUnit)
{
  readControls(); const bool surface=mpType->currentData()=="surface" || mpType->currentData()=="heatmap";
  if (!surface && !series().value("y").toString().isEmpty()) {
    auto p=panel(); auto a=p.value("series").toArray(); a.append(newSeries()); p.insert("series", a); putPanel(p); reloadControls(); mpSeries->setCurrentIndex(a.size()-1);
  }
  assign(surface ? "z" : "y", {{"file", file}, {"variable", variable}, {"unit", unit}, {"displayUnit", displayUnit}});
}
void AnalysisFigureWindow::refreshResult(const QString &file)
{
  for (const auto &v : mDefinition.value("sources").toArray()) { const auto s=v.toObject(); if (!s.value("pinned").toBool() && QFileInfo(s.value("path").toString()).absoluteFilePath()==QFileInfo(file).absoluteFilePath()) { scheduleRender(); return; } }
}
void AnalysisFigureWindow::updateModified() { setWindowModified(mDefinition!=mSavedDefinition); }
void AnalysisFigureWindow::scheduleRender() { if (!mLoading) { updateModified(); if (!mRenderTimer.isActive()) mRenderTimer.start(); } }
void AnalysisFigureWindow::showError(const QString &message) { mPersistentError=message; mpErrors->setText(message); }
void AnalysisFigureWindow::render()
{
  if (!mpWorker->isReady()) return;
  if (mRendering || mExporting) { mRenderAgain=true; return; }
  readControls(); QStringList errors;
  auto payload=AnalysisFigureData::renderPayload(mDefinition, mTime, &errors);
  if (mpRows->value()*mpColumns->value() < mDefinition.value("panels").toArray().size()) errors.append(tr("Increase grid rows or columns to display every panel."));
  QVector<double> times;
  for (const auto &t : payload.value("times").toArray()) if (t.isDouble()) times.append(t.toDouble());
  if (times.isEmpty()) for (const auto &pv : payload.value("panels").toArray()) {
    for (const auto &sv : pv.toObject().value("series").toArray()) {
      for (const auto &t : sv.toObject().value("time").toArray()) if (t.isDouble()) times.append(t.toDouble());
      if (!times.isEmpty()) break;
    }
    if (!times.isEmpty()) break;
  }
  if (!times.isEmpty()) {
    std::sort(times.begin(), times.end()); times.erase(std::unique(times.begin(), times.end()), times.end());
    mTimes=times; const double boundedTime=qBound(times.first(), mTime, times.last());
    if (boundedTime!=mTime) { mTime=boundedTime; scheduleRender(); }
    const bool blocked=mpPlayback->blockSignals(true); mpPlayback->setRange(0, times.size()-1);
    mpPlayback->setValue(qMax(0, int(std::upper_bound(times.begin(), times.end(), mTime)-times.begin())-1)); mpPlayback->blockSignals(blocked);
    mpTime->setText(tr("Time: %1 s").arg(mTime, 0, 'g', 10));
  }
  if (!mPersistentError.isEmpty()) errors.prepend(mPersistentError);
  mpErrors->setText(errors.join('\n')); updateModified(); mRendering=true;
  mpWorker->render(payload, [this](bool ok, const QString &error) { mRendering=false; if (!ok) showError(error); emit figureRendered(ok, error); if (mRenderAgain) { mRenderAgain=false; scheduleRender(); } });
}
void AnalysisFigureWindow::openFigure()
{
  if (!confirmSave()) return;
  const QString file=QFileDialog::getOpenFileName(this, tr("Open analysis figure"), {}, tr("Analysis figure (*.omfigure)")); if (file.isEmpty()) return;
  QString error; const auto definition=AnalysisFigureData::load(file, &error); if (!error.isEmpty() || definition.isEmpty()) { showError(error); return; }
  mPlaybackTimer.stop(); mpPlay->setText(tr("Play")); mDefinition=definition; mFile=file; mTimes.clear(); mTime=0; reloadControls(); readControls(); mSavedDefinition=mDefinition; showError({}); scheduleRender();
}
bool AnalysisFigureWindow::saveFigure()
{
  readControls(); QString file=QFileDialog::getSaveFileName(this, tr("Save editable analysis figure"), mFile, tr("Analysis figure (*.omfigure)")); if (file.isEmpty()) return false;
  if (QFileInfo(file).suffix().isEmpty()) file+=".omfigure";
  QString error; if (!AnalysisFigureData::save(mDefinition, file, &error)) { showError(error); return false; }
  mFile=file; mSavedDefinition=mDefinition; updateModified(); showError({}); return true;
}
bool AnalysisFigureWindow::confirmSave()
{
  readControls(); updateModified(); if (!isWindowModified()) return true;
  const auto answer=QMessageBox::warning(this, tr("Unsaved analysis figure"), tr("Save changes to this analysis figure?"), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  return answer==QMessageBox::Discard || (answer==QMessageBox::Save && saveFigure());
}
void AnalysisFigureWindow::closeEvent(QCloseEvent *event)
{
  if (confirmSave()) { mPlaybackTimer.stop(); mRenderTimer.stop(); event->accept(); }
  else event->ignore();
}
void AnalysisFigureWindow::pinSource()
{
  readControls(); if (mpSources->currentIndex()<0) { showError(tr("Select a result source to pin.")); return; }
  const QString directory=QFileDialog::getExistingDirectory(this, tr("Retain pinned result data in"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)); if (directory.isEmpty()) return;
  QString error; if (!AnalysisFigureData::pinSource(mDefinition, mpSources->currentData().toString(), directory, &error)) showError(error); else { reloadControls(); scheduleRender(); }
}
void AnalysisFigureWindow::exportFigure()
{
  if (mExporting) return;
  readControls(); QString filter=tr("PNG (*.png)"); const QString file=QFileDialog::getSaveFileName(this, tr("Export figure"), {}, tr("PNG (*.png);;SVG (*.svg);;PDF (*.pdf)"), &filter); if (file.isEmpty()) return;
  QString format=QFileInfo(file).suffix().toLower(); if (format.isEmpty()) format=filter.contains("svg") ? "svg" : filter.contains("pdf") ? "pdf" : "png";
  QStringList errors; const auto payload=AnalysisFigureData::renderPayload(mDefinition, mTime, &errors);
  if (!errors.isEmpty()) { showError(errors.join('\n')); return; }
  mExporting=true;
  mpWorker->render(payload, [this, file, format](bool ok, const QString &error) { if (!ok) { mExporting=false; showError(error); scheduleRender(); return; } mpWorker->exportFigure(format, QFileInfo(file).suffix().isEmpty() ? file+"."+format : file, [this](bool success, const QString &message) { mExporting=false; if (!success) showError(message); scheduleRender(); }); });
}
void AnalysisFigureWindow::exportPreview()
{
  if (mExporting) return;
  readControls(); auto directory=std::make_shared<QTemporaryDir>(); if (!directory->isValid()) { showError(tr("Cannot create export preview directory.")); return; }
  QStringList errors; const auto payload=AnalysisFigureData::renderPayload(mDefinition, mTime, &errors); if (!errors.isEmpty()) { showError(errors.join('\n')); return; }
  mExporting=true;
  mpWorker->render(payload, [this, directory](bool ok, const QString &error) {
    if (!ok) { mExporting=false; showError(error); scheduleRender(); return; } const QString path=directory->filePath("preview.png");
    mpWorker->exportFigure("png", path, [this, directory, path](bool success, const QString &message) {
      mExporting=false; scheduleRender();
      if (!success) { showError(message); return; }
      auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle(tr("Export preview — %1 × %2 in, %3 DPI").arg(mpWidth->value()).arg(mpHeight->value()).arg(mpDpi->value()));
      auto *layout=new QVBoxLayout(dialog); auto *scroll=new QScrollArea; auto *image=new QLabel; image->setPixmap(QPixmap(path)); scroll->setWidget(image); layout->addWidget(scroll); dialog->resize(900, 650); dialog->show();
    });
  });
}
