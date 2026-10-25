#ifndef ANALYSISFIGUREDATA_H
#define ANALYSISFIGUREDATA_H

#include <QJsonObject>
#include <QStringList>

namespace AnalysisFigures {
class AnalysisFigureData
{
public:
  static QJsonObject defaultDefinition();
  static QStringList variables(const QString &path, QString *error = nullptr);
  static QJsonObject renderPayload(const QJsonObject &definition, double time, QStringList *errors = nullptr);
  static bool save(const QJsonObject &definition, const QString &file, QString *error = nullptr);
  static QJsonObject load(const QString &file, QString *error = nullptr);
  static bool pinSource(QJsonObject &definition, const QString &sourceId, const QString &storageDirectory, QString *error = nullptr);
  static bool exportPackage(const QJsonObject &definition, const QString &directory, QString *error = nullptr);
  static bool exportCsv(const QJsonObject &definition, const QString &file, QString *error = nullptr);
};
}
#endif
