#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "Plotting/Analysis/AnalysisFigureData.h"

using AnalysisFigures::AnalysisFigureData;

class AnalysisFigureDataTest : public QObject
{
  Q_OBJECT
private:
  static QString fixture(const QString &directory) {
    const QString path = QDir(directory).filePath("result.csv");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    file.write("\"time\",\"a\",\"temperature[1,1]\",\"temperature[1,2]\",\"temperature[2,1]\",\"temperature[2,2]\",\"distance[1]\",\"distance[2]\",\"ragged[1,1]\",\"ragged[2,2]\"\n"
               "0,1,10,11,12,13,2,4,1,2\n"
               "0.3,2,20,21,22,23,2,4,2,3\n"
               "0.3,3,30,31,32,33,2,4,3,4\n"
               "1,4,40,41,42,43,2,4,4,5\n");
    return path;
  }
  static QJsonObject definition(const QString &path, const QString &type = "time") {
    QJsonObject figure = AnalysisFigureData::defaultDefinition();
    figure["sources"] = QJsonArray{QJsonObject{{"id", "run1"}, {"path", path}, {"pinned", false}}};
    figure["panels"] = QJsonArray{QJsonObject{{"id", "panel1"}, {"type", type},
      {"series", QJsonArray{QJsonObject{{"source", "run1"}, {"x", "time"}, {"y", "a"}, {"yScale", 2}, {"yOffset", 10}}}},
      {"surface", QJsonObject{{"source", "run1"}, {"z", "temperature"}, {"x", "distance"}}}}};
    return figure;
  }
  static QJsonObject panel(const QJsonObject &payload) { return payload.value("panels").toArray().at(0).toObject(); }
private slots:
  void preservesEventSamplesAndConverts() {
    QTemporaryDir directory;
    const QString path = fixture(directory.path());
    QVERIFY(!path.isEmpty());
    QString error;
    QVERIFY2(AnalysisFigureData::variables(path, &error).contains("a"), qPrintable(error));
    QStringList errors;
    const QJsonObject resolved = panel(AnalysisFigureData::renderPayload(definition(path), 0.5, &errors));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    const QJsonObject series = resolved.value("series").toArray().at(0).toObject();
    QCOMPARE(series.value("time").toArray(), QJsonArray({0, 0.3, 0.3, 1}));
    QCOMPARE(series.value("y").toArray(), QJsonArray({12, 14, 16, 18}));
    QCOMPARE(series.value("xVariable").toString(), QString("time"));
    QCOMPARE(series.value("yVariable").toString(), QString("a"));
  }
  void ignoresAssignmentsUnusedByPlotType() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()));
    auto p = figure.value("panels").toArray().at(0).toObject();
    auto s = p.value("series").toArray().at(0).toObject();
    s["z"] = "missingZ";
    s["color"] = "missingColor";
    p["series"] = QJsonArray{s};
    figure["panels"] = QJsonArray{p};
    QStringList errors;
    const auto resolved = panel(AnalysisFigureData::renderPayload(figure, 0, &errors));
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    const auto series = resolved.value("series").toArray().at(0).toObject();
    QVERIFY(!series.contains("z"));
    QVERIFY(!series.contains("color"));
    QCOMPARE(figure.value("panels").toArray().at(0).toObject().value("series").toArray().at(0).toObject().value("z").toString(), QString("missingZ"));
    QString error;
    QVERIFY2(AnalysisFigureData::exportCsv(figure, QDir(directory.path()).filePath("active.csv"), &error), qPrintable(error));
    p["type"] = "histogram";
    s["x"] = "missingX";
    p["series"] = QJsonArray{s};
    figure["panels"] = QJsonArray{p};
    AnalysisFigureData::renderPayload(figure, 0, &errors);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    QVERIFY2(AnalysisFigureData::exportCsv(figure, QDir(directory.path()).filePath("histogram.csv"), &error), qPrintable(error));
  }
  void samplesRectangularArraysWithoutInterpolation() {
    QTemporaryDir directory;
    const auto figure = definition(fixture(directory.path()), "surface");
    QStringList errors;
    const QJsonObject sampled = panel(AnalysisFigureData::renderPayload(figure, 0.3, &errors)).value("surface").toObject();
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
    QCOMPARE(sampled.value("z").toArray(), QJsonArray({QJsonArray({30, 31}), QJsonArray({32, 33})}));
    QCOMPARE(sampled.value("x").toArray(), QJsonArray({2, 4}));
    QCOMPARE(sampled.value("y").toArray(), QJsonArray({1, 2}));
    QVERIFY(sampled.value("yLabel").toString().contains("1-based"));
    const QJsonObject before = panel(AnalysisFigureData::renderPayload(figure, 0.299)).value("surface").toObject();
    QCOMPARE(before.value("z").toArray().at(0).toArray().at(0).toDouble(), 10.0);
  }
  void missingVariablesArePanelErrors() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()));
    auto panels = figure.value("panels").toArray();
    auto missing = panels.at(0).toObject();
    missing["id"] = "missing";
    missing["series"] = QJsonArray{QJsonObject{{"source", "run1"}, {"y", "gone"}}};
    panels.append(missing);
    figure["panels"] = panels;
    figure["columns"] = 2;
    QStringList errors;
    const auto payload = AnalysisFigureData::renderPayload(figure, 0.5, &errors);
    QVERIFY(panel(payload).value("error").toString().isEmpty());
    QVERIFY(payload.value("panels").toArray().at(1).toObject().value("error").toString().contains("gone"));
    QCOMPARE(errors.size(), 1);
  }
  void failedSurfaceStillExposesNonzeroTimeline() {
    QTemporaryDir directory;
    const QString path = QDir(directory.path()).filePath("late.csv");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\"time\",\"temperature[1,1]\"\n5,10\n6,20\n");
    file.close();
    auto figure = definition(path, "surface");
    auto p = figure.value("panels").toArray().at(0).toObject();
    p["surface"] = QJsonObject{{"source", "run1"}, {"z", "temperature"}};
    figure["panels"] = QJsonArray{p};
    const auto initial = AnalysisFigureData::renderPayload(figure, 0);
    QCOMPARE(initial.value("times").toArray(), QJsonArray({5, 6}));
    QVERIFY(panel(initial).value("error").toString().contains("precedes"));
    const auto clamped = AnalysisFigureData::renderPayload(figure, initial.value("times").toArray().at(0).toDouble());
    QVERIFY(panel(clamped).value("error").toString().isEmpty());
    QCOMPARE(panel(clamped).value("surface").toObject().value("z").toArray().at(0).toArray().at(0).toDouble(), 10.0);
  }
  void histogramDefaultsDescribeSamplesAndCounts() {
    QTemporaryDir directory;
    const auto resolved = panel(AnalysisFigureData::renderPayload(definition(fixture(directory.path()), "histogram"), 0));
    QCOMPARE(resolved.value("xLabel").toString(), QString("a"));
    QCOMPARE(resolved.value("yLabel").toString(), QString("Count"));
  }
  void rejectsRaggedAndMismatchedCoordinates() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()), "heatmap");
    auto panels = figure.value("panels").toArray();
    auto p = panels.at(0).toObject();
    auto surface = p.value("surface").toObject();
    surface["z"] = "ragged";
    p["surface"] = surface;
    figure["panels"] = QJsonArray{p};
    QVERIFY(panel(AnalysisFigureData::renderPayload(figure, 0.5)).value("error").toString().contains("rectangular"));
    surface["z"] = "temperature";
    surface["x"] = "temperature";
    p["surface"] = surface;
    figure["panels"] = QJsonArray{p};
    QVERIFY(!panel(AnalysisFigureData::renderPayload(figure, 0.5)).value("error").toString().isEmpty());
  }
  void incompatibleAxisUnitsRequireSeparatePanels() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()));
    auto p = figure.value("panels").toArray().at(0).toObject();
    p["series"] = QJsonArray{
      QJsonObject{{"source", "run1"}, {"y", "a"}, {"yUnit", "m"}},
      QJsonObject{{"source", "run1"}, {"y", "a"}, {"yUnit", "s"}}};
    figure["panels"] = QJsonArray{p};
    QVERIFY(panel(AnalysisFigureData::renderPayload(figure, 0.5)).value("error").toString().contains("separate panels"));
  }
  void savePinAndRelocatePackage() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()));
    QString error;
    const QString saved = QDir(directory.path()).filePath("saved.omfigure");
    QVERIFY2(AnalysisFigureData::save(figure, saved, &error), qPrintable(error));
    QFile json(saved);
    QVERIFY(json.open(QIODevice::ReadOnly));
    const auto stored = QJsonDocument::fromJson(json.readAll()).object();
    QCOMPARE(stored.value("sources").toArray().at(0).toObject().value("path").toString(), QString("result.csv"));
    json.close();
    QCOMPARE(AnalysisFigureData::load(saved).value("sources"), figure.value("sources"));
    QVERIFY2(AnalysisFigureData::pinSource(figure, "run1", QDir(directory.path()).filePath("pins"), &error), qPrintable(error));
    const QString retained = figure.value("sources").toArray().at(0).toObject().value("path").toString();
    QVERIFY(QFile::remove(QDir(directory.path()).filePath("result.csv")));
    QVERIFY(QFileInfo::exists(retained));
    const QString package = QDir(directory.path()).filePath("package");
    QVERIFY2(AnalysisFigureData::exportPackage(figure, package, &error), qPrintable(error));
    QVERIFY(!AnalysisFigureData::exportPackage(figure, package, &error));
    const QString moved = QDir(directory.path()).filePath("moved");
    QVERIFY(QDir().rename(package, moved));
    QVERIFY(QFile::remove(retained));
    const auto reopened = AnalysisFigureData::load(QDir(moved).filePath("figure.omfigure"), &error);
    QVERIFY2(!reopened.isEmpty(), qPrintable(error));
    QStringList errors;
    AnalysisFigureData::renderPayload(reopened, 0.5, &errors);
    QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
  }
  void exportsNativeLongCsv() {
    QTemporaryDir directory;
    const auto figure = definition(fixture(directory.path()));
    const QString path = QDir(directory.path()).filePath("export.csv");
    QString error;
    QVERIFY2(AnalysisFigureData::exportCsv(figure, path, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QCOMPARE(bytes.count('\n'), 9);
    QVERIFY(bytes.contains("\"y\",\"a\",0.29999999999999999,14"));
    QVERIFY(bytes.contains("\"y\",\"a\",0.29999999999999999,16"));
  }
  void rejectsHostileDefinitions() {
    QTemporaryDir directory;
    auto figure = definition(fixture(directory.path()));
    QString error;
    figure["rows"] = 1e100;
    QVERIFY(!AnalysisFigureData::save(figure, QDir(directory.path()).filePath("bad.omfigure"), &error));
    QVERIFY(!error.isEmpty());
    figure["rows"] = 1;
    figure["version"] = 2;
    QVERIFY(AnalysisFigureData::renderPayload(figure, 0).contains("error"));
  }
};

QTEST_GUILESS_MAIN(AnalysisFigureDataTest)
#include "AnalysisFigureDataTest.moc"
