#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QVector>
#include <QtGlobal>

class FrameFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString idFilter READ idFilter WRITE setIdFilter NOTIFY idFilterChanged)
    Q_PROPERTY(int busFilter READ busFilter WRITE setBusFilter NOTIFY busFilterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit FrameFilterProxyModel(QObject* parent = nullptr);

    QString idFilter() const { return m_idFilter; }
    void setIdFilter(const QString& text);
    int busFilter() const { return m_busFilter; }
    void setBusFilter(int bus);
    int count() const { return rowCount(); }
    QHash<int, QByteArray> roleNames() const override;

signals:
    void idFilterChanged();
    void busFilterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    struct IdFilterToken {
        QString textUpper;
        bool hasId = false;
        quint32 id = 0;
    };

    static QVector<IdFilterToken> parseIdFilterTokens(const QString& text);

    QString m_idFilter;
    QVector<IdFilterToken> m_idFilterTokens;
    int m_busFilter = -1;
};
