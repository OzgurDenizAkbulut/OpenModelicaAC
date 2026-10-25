#include "AnalysisFigureData.h"
#include "omc_result.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace AnalysisFigures {
namespace {
constexpr int maxPanels = 256;
constexpr int maxSeries = 1024;
constexpr int maxCells = 1000000;
constexpr size_t maxSamples = 10000000;

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toUtf8().constData()); }
void setError(QString *error, const QString &message) { if (error) *error = message; }
QString message(const std::exception &e) { return QString::fromUtf8(e.what()); }

int integer(const QJsonObject &object, const QString &key, int fallback, int maximum)
{
  const QJsonValue value = object.value(key);
  const double number = value.isUndefined() ? fallback : value.toDouble(-1);
  if (!std::isfinite(number) || number < 1 || number > maximum || number != std::floor(number))
    fail(QStringLiteral("Invalid %1").arg(key));
  return static_cast<int>(number);
}

void validate(const QJsonObject &definition)
{
  if (definition.value("version").toDouble() != 1) fail("Unsupported analysis figure version");
  const int rows = integer(definition, "rows", 1, 16);
  const int columns = integer(definition, "columns", 1, 16);
  for (const QString &key : {QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("dpi"), QStringLiteral("fontSize")}) {
    if (!definition.contains(key)) continue;
    const double value = definition.value(key).toDouble(-1);
    if (!std::isfinite(value) || value <= 0 || value > (key == "dpi" ? 2400 : 1000)) fail("Invalid figure " + key);
  }
  if (definition.value("width").toDouble(10) * definition.value("dpi").toDouble(120) > 32768
      || definition.value("height").toDouble(6) * definition.value("dpi").toDouble(120) > 32768)
    fail("Figure export dimensions exceed 32768 pixels");
  if (definition.value("width").toDouble(10) * definition.value("height").toDouble(6)
      * std::pow(definition.value("dpi").toDouble(120), 2) > 64000000)
    fail("Figure export exceeds 64 million pixels");
  if (!definition.value("sources").isArray() || !definition.value("panels").isArray()) fail("Sources and panels must be arrays");
  const QJsonArray sources = definition.value("sources").toArray();
  const QJsonArray panels = definition.value("panels").toArray();
  if (sources.size() > 1024 || panels.size() > maxPanels || panels.size() > rows * columns) fail("Too many sources or panels for figure grid");
  QSet<QString> ids;
  for (const QJsonValue &value : sources) {
    const QJsonObject source = value.toObject();
    const QString id = source.value("id").toString();
    if (!value.isObject() || id.isEmpty() || ids.contains(id) || source.value("path").toString().isEmpty()) fail("Invalid or duplicate result source");
    ids.insert(id);
  }
  ids.clear();
  const QSet<QString> types = {"time", "xy", "scatter", "histogram", "heatmap", "trajectory3d", "scatter3d", "surface"};
  int seriesCount = 0;
  for (const QJsonValue &value : panels) {
    const QJsonObject panel = value.toObject();
    const QString id = panel.value("id").toString();
    if (!value.isObject() || id.isEmpty() || ids.contains(id) || !types.contains(panel.value("type").toString())) fail("Invalid or duplicate panel");
    ids.insert(id);
    if (panel.contains("series") && !panel.value("series").isArray()) fail("Panel series must be an array");
    seriesCount += panel.value("series").toArray().size();
    if (seriesCount > maxSeries) fail("Too many figure series");
    for (const QJsonValue &series : panel.value("series").toArray()) if (!series.isObject()) fail("Invalid series");
  }
}

QJsonObject paths(QJsonObject definition, const QDir &base, bool relative)
{
  QJsonArray sources;
  for (const QJsonValue &value : definition.value("sources").toArray()) {
    QJsonObject source = value.toObject();
    for (const QString &key : {QStringLiteral("path"), QStringLiteral("originalPath")}) {
      const QString path = source.value(key).toString();
      if (!path.isEmpty()) source[key] = relative ? base.relativeFilePath(QFileInfo(path).absoluteFilePath()) : QDir::cleanPath(base.absoluteFilePath(path));
    }
    sources.append(source);
  }
  definition["sources"] = sources;
  return definition;
}

class Results
{
public:
  explicit Results(const QJsonObject &definition) {
    for (const QJsonValue &value : definition.value("sources").toArray()) {
      const QJsonObject source = value.toObject();
      paths.insert(source.value("id").toString(), source.value("path").toString());
    }
  }
  omc::ResultFile &get(const QString &id) {
    if (!paths.contains(id)) fail("Unknown result source: " + id);
    if (!files.contains(id)) files.insert(id, std::make_shared<omc::ResultFile>(paths.value(id).toUtf8().constData()));
    return *files.value(id);
  }
private:
  QMap<QString, QString> paths;
  QMap<QString, std::shared_ptr<omc::ResultFile>> files;
};

QJsonArray values(omc::ResultFile &file, const QString &variable, double scale = 1, double offset = 0)
{
  if (!file.hasVariable(variable.toStdString())) fail("Missing variable: " + variable);
  const auto data = file.trajectory(variable.toStdString());
  if (!data.first || !data.second) fail("No numeric samples for: " + variable);
  if (data.second > maxSamples) fail("Variable exceeds analysis sample limit: " + variable);
  QJsonArray output;
  for (size_t i = 0; i < data.second; ++i) {
    const double value = data.first[i] * scale + offset;
    output.append(std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null));
  }
  return output;
}

QString label(const QString &name, const QString &unit) { return unit.isEmpty() ? name : name + " [" + unit + "]"; }

QString dimension(QString unit)
{
  unit.remove(' ');
  static const QMap<QString, QString> dimensions = {
    {"1", "dimensionless"}, {"%", "dimensionless"}, {"rad", "angle"}, {"deg", "angle"},
    {"m", "length"}, {"s", "time"}, {"min", "time"}, {"h", "time"}, {"d", "time"},
    {"g", "mass"}, {"kg", "mass"}, {"K", "temperature"}, {"degC", "temperature"}, {"degF", "temperature"},
    {"A", "current"}, {"mol", "amount"}, {"cd", "luminous"}, {"Hz", "frequency"}, {"1/s", "frequency"},
    {"N", "force"}, {"Pa", "pressure"}, {"bar", "pressure"}, {"J", "energy"}, {"W", "power"},
    {"V", "voltage"}, {"Ohm", "resistance"}, {"S", "conductance"}, {"F", "capacitance"},
    {"H", "inductance"}, {"C", "charge"}, {"Wb", "flux"}, {"T", "fluxDensity"},
    {"m/s", "velocity"}, {"km/h", "velocity"}, {"m/s2", "acceleration"}, {"m/s^2", "acceleration"},
    {"m2", "area"}, {"m^2", "area"}, {"m3", "volume"}, {"m^3", "volume"}, {"l", "volume"}, {"L", "volume"}
  };
  if (dimensions.contains(unit)) return dimensions.value(unit);
  static const QStringList prefixes = {"da", "Y", "Z", "E", "P", "T", "G", "M", "k", "h", "d", "c", "m", "u", "n", "p", "f", "a", "z", "y"};
  static const QSet<QString> prefixable = {"m", "s", "g", "K", "A", "mol", "cd", "Hz", "N", "Pa", "J", "W", "V", "Ohm", "S", "F", "H", "C", "Wb", "T", "l", "L"};
  for (const QString &prefix : prefixes) {
    if (unit.startsWith(prefix) && prefixable.contains(unit.mid(prefix.size()))) return dimensions.value(unit.mid(prefix.size()));
  }
  return QString();
}

QJsonArray axis(omc::ResultFile &file, const QJsonObject &spec, const QString &key, const QString &variable, QJsonObject &output, int selected = -1, int sampleCount = 0)
{
  const QString nativeUnit = QString::fromStdString(file.unit(variable.toStdString()));
  const QString originalUnit = spec.value(key + "OriginalUnit").toString();
  if (!nativeUnit.isEmpty() && !originalUnit.isEmpty() && nativeUnit != originalUnit)
    fail("Result unit changed for " + variable + " from " + originalUnit + " to " + nativeUnit
         + "; reassign the variable and reselect its display unit");
  QString unit = spec.value(key + "Unit").toString();
  if (unit.isEmpty()) unit = nativeUnit;
  if (!nativeUnit.isEmpty() && unit != nativeUnit && (!spec.contains(key + "Scale") || !spec.contains(key + "Offset")))
    fail("Unit conversion requires explicit scale and offset for " + variable);
  const bool verifiedConversion = spec.value(key + "UnitConversionVerified").toBool()
      && spec.value(key + "OriginalUnit").toString() == nativeUnit;
  if (!nativeUnit.isEmpty() && unit != nativeUnit && !verifiedConversion && unit != QString::fromStdString(file.displayUnit(variable.toStdString()))) {
    const QString sourceDimension = dimension(nativeUnit);
    if (sourceDimension.isEmpty() || sourceDimension != dimension(unit)) fail("Cannot verify compatible units for " + variable + ": " + nativeUnit + " to " + unit);
  }
  const double scale = spec.value(key + "Scale").toDouble(1);
  const double offset = spec.value(key + "Offset").toDouble(0);
  if (!std::isfinite(scale) || scale == 0 || !std::isfinite(offset)) fail("Invalid unit conversion for " + variable);
  if ((spec.contains(key + "Scale") && !spec.value(key + "Scale").isDouble())
      || (spec.contains(key + "Offset") && !spec.value(key + "Offset").isDouble())) fail("Invalid unit conversion for " + variable);
  output[key + "Unit"] = unit;
  output[key + "Label"] = label(variable, unit);
  output[key + "Variable"] = variable;
  output[key + "NativeUnit"] = nativeUnit;
  if (selected >= 0) {
    const auto data = file.trajectory(variable.toStdString());
    if (!data.first || data.second != static_cast<size_t>(sampleCount)) fail("Surface variables have incompatible sample counts: " + variable);
    const double value = data.first[selected] * scale + offset;
    if (!std::isfinite(value)) fail("Nonfinite surface value: " + variable);
    return QJsonArray{value};
  }
  return values(file, variable, scale, offset);
}

int sample(const QJsonArray &times, double time)
{
  if (times.isEmpty()) fail("Result has no timestamps");
  int selected = -1;
  double previous = -INFINITY;
  for (int i = 0; i < times.size(); ++i) {
    if (!times.at(i).isDouble() || times.at(i).toDouble() < previous) fail("Result timestamps are invalid or unordered");
    previous = times.at(i).toDouble();
    if (previous <= time) selected = i;
  }
  if (selected < 0) fail("Playback time precedes the first stored sample");
  return selected;
}

QMap<QPair<int, int>, QString> arrayNames(omc::ResultFile &file, const QString &base, bool matrix)
{
  if (base.isEmpty()) fail("Assign an array variable");
  const QRegularExpression pattern("^" + QRegularExpression::escape(base) + (matrix ? "\\[([1-9][0-9]*),\\s*([1-9][0-9]*)\\]$" : "\\[([1-9][0-9]*)\\]$"));
  QMap<QPair<int, int>, QString> names;
  for (const std::string &name : file.variables()) {
    const QString variable = QString::fromStdString(name);
    const auto match = pattern.match(variable);
    if (!match.hasMatch()) continue;
    bool ok1 = false, ok2 = true;
    const int first = match.captured(1).toInt(&ok1);
    const int second = matrix ? match.captured(2).toInt(&ok2) : 1;
    if (!ok1 || !ok2 || first > maxCells || second > maxCells || names.size() >= maxCells) fail("Array dimensions exceed analysis limit");
    const QPair<int, int> index(first, second);
    if (names.contains(index)) fail("Duplicate array cell: " + variable);
    names.insert(index, variable);
  }
  if (names.isEmpty()) fail("Missing array: " + base);
  return names;
}

QJsonObject surface(omc::ResultFile &file, const QJsonObject &spec, double time)
{
  const auto names = arrayNames(file, spec.value("z").toString(), true);
  int rows = 0, columns = 0;
  for (auto it = names.cbegin(); it != names.cend(); ++it) { rows = qMax(rows, it.key().first); columns = qMax(columns, it.key().second); }
  if (qint64(rows) * columns != names.size()) fail("Z array must be a complete rectangular array starting at [1,1]");
  const QJsonArray times = values(file, QString::fromStdString(file.timeName()));
  const int index = sample(times, time);
  QJsonObject output = spec;
  QJsonArray z;
  for (int row = 1; row <= rows; ++row) {
    QJsonArray cells;
    for (int column = 1; column <= columns; ++column) {
      const QJsonArray trajectory = axis(file, spec, "z", names.value(qMakePair(row, column)), output, index, times.size());
      cells.append(trajectory.at(0));
    }
    z.append(cells);
  }
  output["zLabel"] = label(spec.value("z").toString(), output.value("zUnit").toString());
  output["z"] = z;
  for (const QString &key : {QStringLiteral("x"), QStringLiteral("y")}) {
    const int count = key == "x" ? columns : rows;
    const QString base = spec.value(key).toString();
    QJsonArray coordinates;
    if (base.isEmpty()) {
      for (int i = 1; i <= count; ++i) coordinates.append(i);
      output[key + "Label"] = key == "x" ? "Column index (1-based)" : "Row index (1-based)";
    } else {
      const auto coordinateNames = arrayNames(file, base, false);
      if (coordinateNames.size() != count || coordinateNames.lastKey().first != count) fail("Coordinate array length does not match Z array: " + base);
      for (int i = 1; i <= count; ++i) {
        const QJsonArray trajectory = axis(file, spec, key, coordinateNames.value(qMakePair(i, 1)), output, index, times.size());
        coordinates.append(trajectory.at(0));
      }
      output[key + "Label"] = label(base, output.value(key + "Unit").toString());
      if (coordinates.size() > 1) {
        const double direction = coordinates.at(1).toDouble() - coordinates.at(0).toDouble();
        if (direction == 0) fail("Surface coordinates must be strictly monotonic");
        for (int i = 1; i < coordinates.size(); ++i)
          if ((coordinates.at(i).toDouble() - coordinates.at(i - 1).toDouble()) * direction <= 0) fail("Surface coordinates must be strictly monotonic");
      }
    }
    output[key] = coordinates;
  }
  output["time"] = times.at(index);
  output["times"] = times;
  return output;
}

QString copiedResult(const QString &source, const QString &directory)
{
  const QFileInfo info(source);
  if (!info.isFile()) fail("Result file does not exist: " + source);
  const QString name = QUuid::createUuid().toString(QUuid::WithoutBraces) + "." + info.suffix();
  const QString target = QDir(directory).absoluteFilePath(name);
  if (!QFile::copy(source, target)) fail("Could not retain result data: " + source);
  return target;
}
QString csv(const QString &value) { QString escaped = value; escaped.replace('"', "\"\""); return '"' + escaped + '"'; }

QStringList activeAxes(const QString &type)
{
  if (type == "histogram") return {"y"};
  if (type == "trajectory3d") return {"x", "y", "z"};
  if (type == "scatter3d") return {"x", "y", "z", "color"};
  if (type == "scatter") return {"x", "y", "color"};
  if (type == "surface" || type == "heatmap") return {"x", "y", "z"};
  return {"x", "y"};
}
}

QJsonObject AnalysisFigureData::defaultDefinition()
{
  return {{"version", 1}, {"title", "Analysis Figure"}, {"rows", 1}, {"columns", 1}, {"width", 10}, {"height", 6},
          {"dpi", 120}, {"fontSize", 10}, {"background", "white"}, {"sources", QJsonArray()}, {"panels", QJsonArray()}};
}

QStringList AnalysisFigureData::variables(const QString &path, QString *error)
{
  setError(error, QString());
  try {
    omc::ResultFile file(path.toUtf8().constData());
    QStringList names;
    for (const std::string &name : file.variables()) names.append(QString::fromStdString(name));
    return names;
  } catch (const std::exception &e) { setError(error, message(e)); return {}; }
}

QJsonObject AnalysisFigureData::renderPayload(const QJsonObject &definition, double time, QStringList *errors)
{
  if (errors) errors->clear();
  QJsonObject output = definition;
  output["time"] = time;
  try {
    validate(definition);
    if (!std::isfinite(time)) fail("Playback time must be finite");
    Results results(definition);
    QJsonArray panels;
    for (const QJsonValue &value : definition.value("panels").toArray()) {
      QJsonObject panel = value.toObject();
      panel.remove("error");
      try {
        const QString type = panel.value("type").toString();
        if (type == "surface" || type == "heatmap") {
          const QJsonObject spec = panel.value("surface").toObject();
          omc::ResultFile &file = results.get(spec.value("source").toString());
          if (!output.contains("times")) output["times"] = values(file, QString::fromStdString(file.timeName()));
          const QJsonObject resolved = surface(file, spec, time);
          panel["surface"] = resolved;
          if (!output.contains("times")) output["times"] = resolved.value("times");
        } else {
          QJsonArray series;
          QMap<QString, QString> axisUnits;
          for (const QJsonValue &seriesValue : panel.value("series").toArray()) {
            const QJsonObject spec = seriesValue.toObject();
            QJsonObject resolved = spec;
            const QStringList axes = activeAxes(type);
            for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"), QStringLiteral("color")})
              if (!axes.contains(key)) resolved.remove(key);
            omc::ResultFile &file = results.get(spec.value("source").toString());
            const QJsonArray times = values(file, QString::fromStdString(file.timeName()));
            resolved["time"] = times;
            if (!output.contains("times")) output["times"] = times;
            for (const QString &key : axes) {
              QString variable = spec.value(key).toString();
              if (key == "x" && type == "time") variable = QString::fromStdString(file.timeName());
              const bool required = key == "y" || (key == "x" && type != "histogram") || (key == "z" && (type == "trajectory3d" || type == "scatter3d"));
              if (variable.isEmpty()) { if (required) fail("Assign " + key + " variable"); resolved.remove(key); continue; }
              const QJsonArray data = axis(file, spec, key, variable, resolved);
              if (data.size() != times.size()) fail("Variables have incompatible sample counts; implicit alignment is disabled");
              if (key != "color" && !(type == "histogram" && key == "x")) {
                const QString unit = resolved.value(key + "Unit").toString();
                if (axisUnits.contains(key) && axisUnits.value(key) != unit)
                  fail("Series use different " + key + " axis units; select compatible display units or use separate panels");
                axisUnits.insert(key, unit);
              }
              resolved[key] = data;
              if (type != "histogram" && panel.value(key + "Label").toString().isEmpty()) panel[key + "Label"] = resolved.value(key + "Label");
            }
            series.append(resolved);
          }
          panel["series"] = series;
          if (type == "histogram" && !series.isEmpty()) {
            if (panel.value("xLabel").toString().isEmpty()) panel["xLabel"] = series.at(0).toObject().value("yLabel");
            if (panel.value("yLabel").toString().isEmpty()) panel["yLabel"] = "Count";
          }
        }
      } catch (const std::exception &e) {
        const QString error = message(e);
        panel["error"] = error;
        if (errors) errors->append(panel.value("id").toString() + ": " + error);
      }
      panels.append(panel);
    }
    output["panels"] = panels;
  } catch (const std::exception &e) {
    output["error"] = message(e);
    output["panels"] = QJsonArray();
    if (errors) errors->append(message(e));
  }
  return output;
}

bool AnalysisFigureData::save(const QJsonObject &definition, const QString &file, QString *error)
{
  setError(error, QString());
  try {
    validate(definition);
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) fail(output.errorString());
    const QByteArray json = QJsonDocument(paths(definition, QFileInfo(file).absoluteDir(), true)).toJson();
    if (output.write(json) != json.size() || !output.commit()) fail(output.errorString());
    return true;
  } catch (const std::exception &e) { setError(error, message(e)); return false; }
}

QJsonObject AnalysisFigureData::load(const QString &file, QString *error)
{
  setError(error, QString());
  try {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) fail(input.errorString());
    if (input.size() > 16 * 1024 * 1024) fail("Figure definition exceeds 16 MiB limit");
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) fail("Invalid figure JSON: " + parse.errorString());
    validate(document.object());
    return paths(document.object(), QFileInfo(file).absoluteDir(), false);
  } catch (const std::exception &e) { setError(error, message(e)); return {}; }
}

bool AnalysisFigureData::pinSource(QJsonObject &definition, const QString &sourceId, const QString &storageDirectory, QString *error)
{
  setError(error, QString());
  try {
    validate(definition);
    QJsonArray sources = definition.value("sources").toArray();
    for (int i = 0; i < sources.size(); ++i) {
      QJsonObject source = sources.at(i).toObject();
      if (source.value("id").toString() != sourceId) continue;
      if (source.value("pinned").toBool()) {
        if (!QFileInfo::exists(source.value("path").toString())) fail("Pinned result data is missing");
        return true;
      }
      if (!QDir().mkpath(storageDirectory)) fail("Could not create pin storage directory");
      const QString original = source.value("path").toString();
      source["path"] = copiedResult(original, storageDirectory);
      source["originalPath"] = original;
      source["pinned"] = true;
      sources[i] = source;
      definition["sources"] = sources;
      return true;
    }
    fail("Unknown result source: " + sourceId);
  } catch (const std::exception &e) { setError(error, message(e)); return false; }
}

bool AnalysisFigureData::exportPackage(const QJsonObject &definition, const QString &directory, QString *error)
{
  setError(error, QString());
  bool created = false;
  try {
    validate(definition);
    const QFileInfo target(directory);
    if (target.exists()) fail("Portable package destination must be a new directory");
    if (!target.absoluteDir().mkdir(target.fileName())) fail("Could not create portable package directory");
    created = true;
    QDir package(target.absoluteFilePath());
    if (!package.mkdir("data")) fail("Could not create package data directory");
    QJsonObject portable = definition;
    QJsonArray sources;
    for (const QJsonValue &value : definition.value("sources").toArray()) {
      QJsonObject source = value.toObject();
      source["path"] = copiedResult(source.value("path").toString(), package.absoluteFilePath("data"));
      source["pinned"] = true;
      source.remove("originalPath");
      sources.append(source);
    }
    portable["sources"] = sources;
    QString saveError;
    if (!save(portable, package.absoluteFilePath("figure.omfigure"), &saveError)) fail(saveError);
    return true;
  } catch (const std::exception &e) {
    // This directory was exclusively created by this operation.
    if (created) QDir(QFileInfo(directory).absoluteFilePath()).removeRecursively();
    setError(error, message(e));
    return false;
  }
}

bool AnalysisFigureData::exportCsv(const QJsonObject &definition, const QString &file, QString *error)
{
  setError(error, QString());
  try {
    validate(definition);
    Results results(definition);
    QSaveFile output(file);
    if (!output.open(QIODevice::WriteOnly)) fail(output.errorString());
    auto write = [&output](const QByteArray &line) { if (output.write(line) != line.size()) fail(output.errorString()); };
    write("panel,series,source,axis,variable,time,value,unit\n");
    for (const QJsonValue &panelValue : definition.value("panels").toArray()) {
      const QJsonObject panel = panelValue.toObject();
      QJsonArray specifications = panel.value("series").toArray();
      const bool isSurface = panel.value("type").toString() == "surface" || panel.value("type").toString() == "heatmap";
      if (isSurface) specifications = QJsonArray{panel.value("surface")};
      for (int seriesIndex = 0; seriesIndex < specifications.size(); ++seriesIndex) {
        const QJsonObject spec = specifications.at(seriesIndex).toObject();
        const QString source = spec.value("source").toString();
        omc::ResultFile &result = results.get(source);
        const QJsonArray times = values(result, QString::fromStdString(result.timeName()));
        if (isSurface) surface(result, spec, result.stopTime());
        for (const QString &key : activeAxes(panel.value("type").toString())) {
          QString variable = spec.value(key).toString();
          if (!isSurface && key == "x" && panel.value("type").toString() == "time") variable = QString::fromStdString(result.timeName());
          if (variable.isEmpty()) continue;
          QStringList variables{variable};
          if (isSurface) {
            variables.clear();
            const auto names = arrayNames(result, variable, key == "z");
            for (auto it = names.cbegin(); it != names.cend(); ++it) variables.append(it.value());
          }
          for (const QString &name : variables) {
            QJsonObject resolved;
            const QJsonArray data = axis(result, spec, key, name, resolved);
            if (data.size() != times.size()) fail("Cannot export unmatched variable timestamps: " + name);
            const auto raw = result.trajectory(name.toStdString());
            const double scale = spec.value(key + "Scale").toDouble(1);
            const double offset = spec.value(key + "Offset").toDouble(0);
            const QString prefix = csv(panel.value("id").toString()) + ',' + QString::number(seriesIndex + 1) + ',' + csv(source) + ',' + csv(key) + ',' + csv(name) + ',';
            for (int i = 0; i < data.size(); ++i) {
              const QString row = prefix + QString::number(times.at(i).toDouble(), 'g', 17) + ','
                  + QString::number(raw.first[i] * scale + offset, 'g', 17) + ',' + csv(resolved.value(key + "Unit").toString()) + '\n';
              write(row.toUtf8());
            }
          }
        }
      }
    }
    if (!output.commit()) fail(output.errorString());
    return true;
  } catch (const std::exception &e) { setError(error, message(e)); return false; }
}
}
