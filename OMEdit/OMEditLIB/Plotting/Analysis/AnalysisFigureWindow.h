#ifndef ANALYSISFIGUREWINDOW_H
#define ANALYSISFIGUREWINDOW_H

#include <QWidget>
#include <QJsonObject>
#include <QMap>
#include <QVector>
#include <QTimer>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QLabel;
class QPushButton;
class QWebEngineView;
class QCloseEvent;

namespace AnalysisFigures {
class AnalysisWorkerClient;
class AnalysisFigureWindow : public QWidget
{
  Q_OBJECT
public:
  explicit AnalysisFigureWindow(QWidget *parent = nullptr);
  void addVariable(const QString &resultFile, const QString &variable,
                   const QString &unit = QString(), const QString &displayUnit = QString());
  void refreshResult(const QString &resultFile);
signals:
  void figureRendered(bool success, const QString &error);
protected:
  void closeEvent(QCloseEvent *event) override;
private:
  bool confirmSave();
  void updateModified();
  QString ensureSource(const QString &file);
  QJsonObject panel() const;
  QJsonObject series() const;
  void putPanel(const QJsonObject &panel);
  void putSeries(const QJsonObject &series);
  void reloadControls();
  void readControls();
  void scheduleRender();
  void render();
  void showError(const QString &message);
  void assign(const QString &field, const QJsonObject &variable);
  void openFigure();
  bool saveFigure();
  void pinSource();
  void exportFigure();
  void exportPreview();
  QJsonObject mDefinition;
  QJsonObject mSavedDefinition;
  QString mPersistentError;
  QString mFile;
  AnalysisWorkerClient *mpWorker;
  QWebEngineView *mpView = nullptr;
  QComboBox *mpSources;
  QComboBox *mpPanels;
  QComboBox *mpSeries;
  QComboBox *mpType;
  QMap<QString, QLineEdit*> mFields;
  QMap<QString, QLineEdit*> mLabels;
  QMap<QString, QCheckBox*> mChecks;
  QSpinBox *mpRows;
  QSpinBox *mpColumns;
  QDoubleSpinBox *mpWidth;
  QDoubleSpinBox *mpHeight;
  QSpinBox *mpDpi;
  QSpinBox *mpFont;
  QLineEdit *mpBackground;
  QLineEdit *mpTitle;
  QLabel *mpErrors;
  QLabel *mpTime;
  QSlider *mpPlayback;
  QPushButton *mpPlay;
  QTimer mRenderTimer;
  QTimer mPlaybackTimer;
  QVector<double> mTimes;
  double mTime = 0;
  bool mLoading = false;
  bool mRendering = false;
  bool mExporting = false;
  bool mRenderAgain = false;
};
}
#endif
