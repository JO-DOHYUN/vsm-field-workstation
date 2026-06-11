#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace CanModel {

struct ValidationIssue {
    enum class Severity {
        Warning,
        Error
    };

    Severity severity = Severity::Error;
    QString code;
    QString message;

    bool isError() const { return severity == Severity::Error; }
};

class ModelPackValidator {
public:
    static QVector<ValidationIssue> validate(const QJsonObject& root);
    static bool hasErrors(const QVector<ValidationIssue>& issues);
    static QString summarize(const QVector<ValidationIssue>& issues);
};

} // namespace CanModel
