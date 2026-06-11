#pragma once

#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVector>
#include <QString>
#include <QImage>

class GraphViewportItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList series READ series WRITE setSeries NOTIFY seriesChanged)
    Q_PROPERTY(qreal uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(double cursorMs READ cursorMs WRITE setCursorMs NOTIFY cursorMsChanged)
    Q_PROPERTY(double viewStartMs READ viewStartMs WRITE setViewStartMs NOTIFY viewRangeChanged)
    Q_PROPERTY(double viewEndMs READ viewEndMs WRITE setViewEndMs NOTIFY viewRangeChanged)
    Q_PROPERTY(int yAxisMode READ yAxisMode WRITE setYAxisMode NOTIFY yAxisModeChanged)
    Q_PROPERTY(double selectionStartMs READ selectionStartMs WRITE setSelectionStartMs NOTIFY selectionChanged)
    Q_PROPERTY(double selectionEndMs READ selectionEndMs WRITE setSelectionEndMs NOTIFY selectionChanged)
    Q_PROPERTY(int plotLeftPadding READ plotLeftPadding NOTIFY layoutMetricsChanged)
    Q_PROPERTY(int plotRightPadding READ plotRightPadding NOTIFY layoutMetricsChanged)
    Q_PROPERTY(int plotTopPadding READ plotTopPadding NOTIFY layoutMetricsChanged)
    Q_PROPERTY(int plotBottomPadding READ plotBottomPadding NOTIFY layoutMetricsChanged)
public:
    explicit GraphViewportItem(QQuickItem* parent = nullptr);

    QVariantList series() const { return m_seriesVariant; }
    void setSeries(const QVariantList& series);

    qreal uiScale() const { return m_uiScale; }
    void setUiScale(qreal scale);

    double cursorMs() const { return m_cursorMs; }
    void setCursorMs(double value);

    double viewStartMs() const { return m_viewStartMs; }
    void setViewStartMs(double value);

    double viewEndMs() const { return m_viewEndMs; }
    void setViewEndMs(double value);

    int yAxisMode() const { return m_yAxisMode; }
    void setYAxisMode(int mode);

    double selectionStartMs() const { return m_selectionStartMs; }
    void setSelectionStartMs(double value);

    double selectionEndMs() const { return m_selectionEndMs; }
    void setSelectionEndMs(double value);

    int plotLeftPadding() const { return m_plotLeftPadding; }
    int plotRightPadding() const { return m_plotRightPadding; }
    int plotTopPadding() const { return m_plotTopPadding; }
    int plotBottomPadding() const { return m_plotBottomPadding; }

    void paint(QPainter* painter) override;

signals:
    void seriesChanged();
    void uiScaleChanged();
    void cursorMsChanged();
    void viewRangeChanged();
    void yAxisModeChanged();
    void selectionChanged();
    void layoutMetricsChanged();

private:
    struct SeriesData {
        QString color;
        QString renderMode;
        QVector<double> rawFlat;
        QVector<double> bucketFlat;
    };

    void rebuildParsedSeries();
    void ensureBackdrop(int width, int height, double yMin, double yMax, double fullWindowMs, double viewStartMs, double viewEndMs);
    bool scanVisibleYRange(double viewStartMs, double viewEndMs, int yAxisMode, double& outMin, double& outMax) const;
    static QVector<double> variantListToDoubleVector(const QVariant& value);

    QVariantList m_seriesVariant;
    QVector<SeriesData> m_series;
    qreal m_uiScale = 1.0;
    double m_cursorMs = -1.0;
    double m_viewStartMs = 0.0;
    double m_viewEndMs = -1.0;
    int m_yAxisMode = 0;
    double m_selectionStartMs = -1.0;
    double m_selectionEndMs = -1.0;
    int m_plotLeftPadding = 58;
    int m_plotRightPadding = 18;
    int m_plotTopPadding = 18;
    int m_plotBottomPadding = 34;

    QImage m_backdrop;
    QSize m_backdropSize;
    double m_backdropYMin = 0.0;
    double m_backdropYMax = 1.0;
    double m_backdropWindowMs = 0.0;
    double m_backdropViewStartMs = 0.0;
    double m_backdropViewEndMs = 0.0;
    qreal m_backdropUiScale = 1.0;
};
