#include "GraphViewportItem.h"

#include <QPainter>
#include <QColor>
#include <QPen>
#include <QPainterPath>
#include <QTime>
#include <QFontMetrics>
#include <QtMath>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <limits>

namespace {
struct PlotLayoutMetrics {
    int leftPad = 58;
    int rightPad = 18;
    int topPad = 18;
    int bottomPad = 34;
    int plotW = 10;
    int plotH = 10;
    int plotX = 58;
    int plotY = 18;
    int edgeLabelWidth = 0;
};

QString normRenderMode(const QString& mode) {
    return mode.trimmed().isEmpty() ? QStringLiteral("bucket") : mode.trimmed().toLower();
}

double clampWindowMs(double fullWindowMs, double value, double fallback) {
    if (!qIsFinite(value)) return fallback;
    return qBound(0.0, value, fullWindowMs);
}

double pickNiceTimeStepMs(double rangeMs) {
    static const double steps[] = {
        10, 20, 50, 100, 200, 500,
        1000, 2000, 5000, 10000, 15000, 30000,
        60000, 120000, 300000, 600000, 900000,
        1800000, 3600000, 7200000
    };
    const double target = std::max(1.0, rangeMs / 6.0);
    for (double step : steps) {
        if (step >= target) return step;
    }
    return steps[std::size(steps) - 1];
}

QString formatAxisTime(double ms, double visibleRangeMs, bool exactEdge = false) {
    const qint64 totalMs = qMax<qint64>(0, qRound64(ms));
    const int hours = int(totalMs / 3600000);
    const int minutes = int((totalMs / 60000) % 60);
    const int seconds = int((totalMs / 1000) % 60);
    const int millis = int(totalMs % 1000);
    const bool showMillis = exactEdge || visibleRangeMs <= 15000.0;

    if (hours > 0) {
        return showMillis
            ? QStringLiteral("%1:%2:%3.%4")
                .arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'))
            : QStringLiteral("%1:%2:%3")
                .arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));
    }
    if (minutes > 0 || visibleRangeMs >= 60000.0) {
        return showMillis
            ? QStringLiteral("%1:%2.%3")
                .arg(minutes)
                .arg(seconds, 2, 10, QChar('0'))
                .arg(millis, 3, 10, QChar('0'))
            : QStringLiteral("%1:%2")
                .arg(minutes)
                .arg(seconds, 2, 10, QChar('0'));
    }
    if (showMillis) {
        return QStringLiteral("%1.%2s")
            .arg(totalMs / 1000)
            .arg(millis, 3, 10, QChar('0'));
    }
    const double sec = ms / 1000.0;
    return visibleRangeMs < 10000.0
        ? QString::number(sec, 'f', 1) + QStringLiteral("s")
        : QString::number(sec, 'f', 0) + QStringLiteral("s");
}

PlotLayoutMetrics computePlotLayoutMetrics(int width,
                                           int height,
                                           qreal uiScale,
                                           double yMin,
                                           double yMax,
                                           double visibleRangeMs,
                                           double viewStartMs,
                                           double viewEndMs,
                                           const QFontMetrics& tickMetrics) {
    PlotLayoutMetrics metrics;

    int yLabelWidth = 0;
    for (int i = 0; i <= 4; ++i) {
        const double ratio = double(i) / 4.0;
        const double value = yMax - ((yMax - yMin) * ratio);
        yLabelWidth = std::max(yLabelWidth, tickMetrics.horizontalAdvance(QString::number(value, 'f', 2)));
    }

    const QString startLabel = formatAxisTime(viewStartMs, visibleRangeMs, true);
    const QString endLabel = formatAxisTime(viewEndMs, visibleRangeMs, true);
    metrics.edgeLabelWidth = std::max(tickMetrics.horizontalAdvance(startLabel), tickMetrics.horizontalAdvance(endLabel));

    metrics.leftPad = std::max(qRound(60 * uiScale), yLabelWidth + qRound(22 * uiScale));
    metrics.rightPad = std::max(qRound(28 * uiScale), metrics.edgeLabelWidth + qRound(22 * uiScale));
    metrics.topPad = qRound(18 * uiScale);
    metrics.bottomPad = std::max(qRound(38 * uiScale), tickMetrics.height() + qRound(24 * uiScale));
    metrics.plotW = qMax(10, width - metrics.leftPad - metrics.rightPad);
    metrics.plotH = qMax(10, height - metrics.topPad - metrics.bottomPad);
    metrics.plotX = metrics.leftPad;
    metrics.plotY = metrics.topPad;
    return metrics;
}

} // namespace



bool GraphViewportItem::scanVisibleYRange(double viewStartMs, double viewEndMs, int yAxisMode, double& outMin, double& outMax) const {
    double localMin = std::numeric_limits<double>::max();
    double localMax = std::numeric_limits<double>::lowest();
    double maxAbs = 0.0;
    bool havePoint = false;

    auto considerValue = [&](double value) {
        localMin = std::min(localMin, value);
        localMax = std::max(localMax, value);
        maxAbs = std::max(maxAbs, std::abs(value));
        havePoint = true;
    };

    for (const auto& item : m_series) {
        if (item.renderMode == QStringLiteral("raw")) {
            for (int i = 0; i + 1 < item.rawFlat.size(); i += 2) {
                const double tMs = item.rawFlat.at(i);
                if (tMs < viewStartMs || tMs > viewEndMs) continue;
                considerValue(item.rawFlat.at(i + 1));
            }
            continue;
        }

        for (int i = 0; i + 3 < item.bucketFlat.size(); i += 4) {
            const double tMs = item.bucketFlat.at(i);
            if (tMs < viewStartMs || tMs > viewEndMs) continue;
            considerValue(item.bucketFlat.at(i + 1));
            considerValue(item.bucketFlat.at(i + 2));
            considerValue(item.bucketFlat.at(i + 3));
        }
    }

    if (!havePoint) return false;

    if (yAxisMode == 3) {
        const double pad = std::max(1e-6, maxAbs * 0.08);
        outMin = -(maxAbs + pad);
        outMax = maxAbs + pad;
        return true;
    }

    const double span = std::max(1e-6, localMax - localMin);
    const double pad = span * (yAxisMode == 2 ? 0.04 : 0.12);
    outMin = localMin - pad;
    outMax = localMax + pad;
    if (std::abs(outMax - outMin) < 1e-9) {
        outMin -= 1.0;
        outMax += 1.0;
    }
    return true;
}

GraphViewportItem::GraphViewportItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    setOpaquePainting(true);
    setFlag(ItemHasContents, true);
}

void GraphViewportItem::setSeries(const QVariantList& series) {
    if (m_seriesVariant == series) return;
    m_seriesVariant = series;
    rebuildParsedSeries();
    emit seriesChanged();
    update();
}

void GraphViewportItem::setUiScale(qreal scale) {
    if (qFuzzyCompare(m_uiScale, scale)) return;
    m_uiScale = scale;
    m_backdrop = QImage();
    emit uiScaleChanged();
    update();
}

void GraphViewportItem::setCursorMs(double value) {
    if (qFuzzyCompare(m_cursorMs + 1.0, value + 1.0)) return;
    m_cursorMs = value;
    emit cursorMsChanged();
    update();
}

void GraphViewportItem::setViewStartMs(double value) {
    if (qFuzzyCompare(m_viewStartMs + 1.0, value + 1.0)) return;
    m_viewStartMs = value;
    m_backdrop = QImage();
    emit viewRangeChanged();
    update();
}

void GraphViewportItem::setViewEndMs(double value) {
    if (qFuzzyCompare(m_viewEndMs + 1.0, value + 1.0)) return;
    m_viewEndMs = value;
    m_backdrop = QImage();
    emit viewRangeChanged();
    update();
}

void GraphViewportItem::setYAxisMode(int mode) {
    const int clamped = qBound(0, mode, 3);
    if (m_yAxisMode == clamped) return;
    m_yAxisMode = clamped;
    m_backdrop = QImage();
    emit yAxisModeChanged();
    update();
}

void GraphViewportItem::setSelectionStartMs(double value) {
    if (qFuzzyCompare(m_selectionStartMs + 1.0, value + 1.0)) return;
    m_selectionStartMs = value;
    emit selectionChanged();
    update();
}

void GraphViewportItem::setSelectionEndMs(double value) {
    if (qFuzzyCompare(m_selectionEndMs + 1.0, value + 1.0)) return;
    m_selectionEndMs = value;
    emit selectionChanged();
    update();
}

QVector<double> GraphViewportItem::variantListToDoubleVector(const QVariant& value) {
    QVector<double> out;
    const QVariantList list = value.toList();
    out.reserve(list.size());
    for (const QVariant& v : list) out.push_back(v.toDouble());
    return out;
}

void GraphViewportItem::rebuildParsedSeries() {
    m_series.clear();
    m_series.reserve(m_seriesVariant.size());
    for (const QVariant& rowVar : m_seriesVariant) {
        const QVariantMap row = rowVar.toMap();
        SeriesData s;
        s.color = row.value(QStringLiteral("color")).toString();
        s.renderMode = normRenderMode(row.value(QStringLiteral("renderMode")).toString());
        s.rawFlat = variantListToDoubleVector(row.value(QStringLiteral("rawFlat")));
        s.bucketFlat = variantListToDoubleVector(row.value(QStringLiteral("bucketFlat")));
        m_series.push_back(std::move(s));
    }
}

void GraphViewportItem::ensureBackdrop(int width, int height, double yMin, double yMax, double fullWindowMs, double viewStartMs, double viewEndMs) {
    const double visibleRangeMs = std::max(1.0, viewEndMs - viewStartMs);
    QFont tickFont;
    tickFont.setPixelSize(qRound(11 * m_uiScale));
    QFontMetrics tickMetrics(tickFont);
    const QString startLabel = formatAxisTime(viewStartMs, visibleRangeMs, true);
    const QString endLabel = formatAxisTime(viewEndMs, visibleRangeMs, true);
    const PlotLayoutMetrics metrics = computePlotLayoutMetrics(width, height, m_uiScale, yMin, yMax, visibleRangeMs, viewStartMs, viewEndMs, tickMetrics);

    const QSize needSize(width, height);
    if (!m_backdrop.isNull() && m_backdropSize == needSize && qFuzzyCompare(m_backdropYMin + 1.0, yMin + 1.0)
        && qFuzzyCompare(m_backdropYMax + 1.0, yMax + 1.0) && qFuzzyCompare(m_backdropWindowMs + 1.0, fullWindowMs + 1.0)
        && qFuzzyCompare(m_backdropViewStartMs + 1.0, viewStartMs + 1.0) && qFuzzyCompare(m_backdropViewEndMs + 1.0, viewEndMs + 1.0)
        && qFuzzyCompare(m_backdropUiScale + 1.0, m_uiScale + 1.0)) {
        return;
    }

    m_backdropSize = needSize;
    m_backdropYMin = yMin;
    m_backdropYMax = yMax;
    m_backdropWindowMs = fullWindowMs;
    m_backdropViewStartMs = viewStartMs;
    m_backdropViewEndMs = viewEndMs;
    m_backdropUiScale = m_uiScale;

    m_backdrop = QImage(needSize, QImage::Format_ARGB32_Premultiplied);
    m_backdrop.fill(Qt::white);

    QPainter p(&m_backdrop);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int leftPad = metrics.leftPad;
    const int rightPad = metrics.rightPad;
    const int topPad = metrics.topPad;
    const int bottomPad = metrics.bottomPad;
    const int plotW = metrics.plotW;
    const int plotH = metrics.plotH;
    const int plotX = metrics.plotX;
    const int plotY = metrics.plotY;

    p.fillRect(QRect(plotX, plotY, plotW, plotH), QColor(QStringLiteral("#ffffff")));
    p.setPen(QPen(QColor(QStringLiteral("#dbe5f0")), 1));
    p.drawRect(QRect(plotX, plotY, plotW, plotH));

    p.setFont(tickFont);

    for (int i = 0; i <= 4; ++i) {
        const double ratio = double(i) / 4.0;
        const int y = plotY + qRound(plotH * ratio);
        p.setPen(QPen(QColor(QStringLiteral("#e5edf5")), 1));
        p.drawLine(plotX, y, plotX + plotW, y);

        const double value = yMax - ((yMax - yMin) * ratio);
        p.setPen(QColor(QStringLiteral("#94a3b8")));
        p.drawText(QRect(0, y - 9, plotX - 8, 18), Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'f', 2));
    }

    const auto pxX = [&](double absoluteMs) {
        const double clamped = qBound(viewStartMs, absoluteMs, viewEndMs);
        return plotX + ((clamped - viewStartMs) / visibleRangeMs) * double(plotW);
    };

    p.setPen(QColor(QStringLiteral("#94a3b8")));
    const int labelTop = plotY + plotH + qRound(7 * m_uiScale);
    p.drawText(QRect(plotX - 2, labelTop, metrics.edgeLabelWidth + qRound(14 * m_uiScale), tickMetrics.height() + 4), Qt::AlignLeft | Qt::AlignTop, startLabel);

    const double stepMs = pickNiceTimeStepMs(visibleRangeMs);
    const double firstTickMs = std::ceil(viewStartMs / stepMs) * stepMs;
    for (double tickMs = firstTickMs; tickMs < viewEndMs; tickMs += stepMs) {
        if (tickMs <= viewStartMs + 0.5 || tickMs >= viewEndMs - 0.5) continue;
        const int x = qRound(pxX(tickMs));
        p.setPen(QPen(QColor(QStringLiteral("#eef3f8")), 1));
        p.drawLine(x, plotY, x, plotY + plotH);
        p.setPen(QColor(QStringLiteral("#94a3b8")));
        const QString tickLabel = formatAxisTime(tickMs, visibleRangeMs);
        const int tickWidth = tickMetrics.horizontalAdvance(tickLabel) + qRound(14 * m_uiScale);
        p.drawText(QRect(x - qRound(tickWidth / 2.0), labelTop, tickWidth, tickMetrics.height() + 4), Qt::AlignHCenter | Qt::AlignTop, tickLabel);
    }

    p.setPen(QPen(QColor(QStringLiteral("#eef3f8")), 1));
    p.drawLine(plotX + plotW, plotY, plotX + plotW, plotY + plotH);
    p.setPen(QColor(QStringLiteral("#94a3b8")));
    p.drawText(QRect(plotX + plotW - metrics.edgeLabelWidth, labelTop, metrics.edgeLabelWidth + qRound(10 * m_uiScale), tickMetrics.height() + 4), Qt::AlignRight | Qt::AlignTop, endLabel);
}

void GraphViewportItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(boundingRect(), Qt::white);

    if (m_seriesVariant.isEmpty()) {
        painter->setPen(QColor(QStringLiteral("#6b7280")));
        QFont f = painter->font();
        f.setPixelSize(qRound(15 * m_uiScale));
        painter->setFont(f);
        painter->drawText(boundingRect(), Qt::AlignCenter, QStringLiteral("선택된 그래프가 없습니다"));
        return;
    }

    const QVariantMap first = m_seriesVariant.first().toMap();
    const double seriesYMin = first.value(QStringLiteral("yMin")).toDouble();
    const double seriesYMax = first.value(QStringLiteral("yMax")).toDouble();
    const double fullWindowMs = first.value(QStringLiteral("windowMs")).toDouble();
    if (!(qIsFinite(seriesYMin) && qIsFinite(seriesYMax) && qIsFinite(fullWindowMs)) || fullWindowMs <= 0.0) {
        painter->setPen(QColor(QStringLiteral("#6b7280")));
        QFont f = painter->font();
        f.setPixelSize(qRound(15 * m_uiScale));
        painter->setFont(f);
        painter->drawText(boundingRect(), Qt::AlignCenter, QStringLiteral("표시 가능한 데이터가 없습니다"));
        return;
    }

    const double viewStartMs = clampWindowMs(fullWindowMs, m_viewStartMs, 0.0);
    const double requestedEndMs = (m_viewEndMs > 0.0) ? m_viewEndMs : fullWindowMs;
    const double viewEndMs = std::max(viewStartMs + 1.0, clampWindowMs(fullWindowMs, requestedEndMs, fullWindowMs));
    const double visibleRangeMs = std::max(1.0, viewEndMs - viewStartMs);

    QFont tickFont = painter->font();
    tickFont.setPixelSize(qRound(11 * m_uiScale));
    QFontMetrics tickMetrics(tickFont);

    double yMin = seriesYMin;
    double yMax = seriesYMax;
    if (m_yAxisMode != 0) {
        double localYMin = 0.0;
        double localYMax = 1.0;
        if (scanVisibleYRange(viewStartMs, viewEndMs, m_yAxisMode, localYMin, localYMax)) {
            yMin = localYMin;
            yMax = localYMax;
        }
    }

    const int width = qMax(1, int(boundingRect().width()));
    const int height = qMax(1, int(boundingRect().height()));
    const PlotLayoutMetrics metrics = computePlotLayoutMetrics(width, height, m_uiScale, yMin, yMax, visibleRangeMs, viewStartMs, viewEndMs, tickMetrics);
    const int leftPad = metrics.leftPad;
    const int rightPad = metrics.rightPad;
    const int topPad = metrics.topPad;
    const int bottomPad = metrics.bottomPad;
    const int plotW = metrics.plotW;
    const int plotH = metrics.plotH;
    const int plotX = metrics.plotX;
    const int plotY = metrics.plotY;

    if (m_plotLeftPadding != leftPad || m_plotRightPadding != rightPad || m_plotTopPadding != topPad || m_plotBottomPadding != bottomPad) {
        m_plotLeftPadding = leftPad;
        m_plotRightPadding = rightPad;
        m_plotTopPadding = topPad;
        m_plotBottomPadding = bottomPad;
        emit layoutMetricsChanged();
    }

    ensureBackdrop(width, height, yMin, yMax, fullWindowMs, viewStartMs, viewEndMs);
    if (!m_backdrop.isNull()) painter->drawImage(QPoint(0, 0), m_backdrop);

    auto pxX = [&](double tMs) {
        const double clamped = qBound(viewStartMs, tMs, viewEndMs);
        return plotX + ((clamped - viewStartMs) / visibleRangeMs) * double(plotW);
    };
    auto pxY = [&](double v) {
        const double denom = qMax(1e-9, (yMax - yMin));
        return plotY + (1.0 - ((v - yMin) / denom)) * double(plotH);
    };

    const double selA = std::min(m_selectionStartMs, m_selectionEndMs);
    const double selB = std::max(m_selectionStartMs, m_selectionEndMs);
    if (qIsFinite(selA) && qIsFinite(selB) && selA >= 0.0 && selB > selA) {
        const double clippedA = qBound(viewStartMs, selA, viewEndMs);
        const double clippedB = qBound(viewStartMs, selB, viewEndMs);
        if (clippedB > clippedA) {
            const QRectF selectionRect(QPointF(pxX(clippedA), plotY), QPointF(pxX(clippedB), plotY + plotH));
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(59, 130, 246, 38));
            painter->drawRect(selectionRect);
            QPen selectionPen(QColor(QStringLiteral("#3b82f6")));
            selectionPen.setCosmetic(true);
            selectionPen.setWidthF(1.0);
            selectionPen.setStyle(Qt::DashLine);
            painter->setPen(selectionPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(selectionRect);
        }
    }

    for (const SeriesData& item : m_series) {
        const QColor color(item.color.isEmpty() ? QStringLiteral("#2563eb") : item.color);
        QPen pen(color);
        pen.setCosmetic(true);

        if (item.renderMode == QStringLiteral("raw")) {
            if (item.rawFlat.size() < 4) continue;
            QPainterPath path;
            bool started = false;
            QPointF lastPt;
            for (int i = 0; i + 1 < item.rawFlat.size(); i += 2) {
                const double tMs = item.rawFlat.at(i);
                if (tMs < viewStartMs || tMs > viewEndMs) continue;
                const QPointF pt(pxX(tMs), pxY(item.rawFlat.at(i + 1)));
                if (!started) { path.moveTo(pt); started = true; }
                else path.lineTo(pt);
                lastPt = pt;
            }
            if (!started) continue;
            pen.setWidthF(1.5);
            painter->setOpacity(1.0);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
            painter->setBrush(color);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(lastPt, 3.0, 3.0);
            continue;
        }

        if (item.bucketFlat.size() < 4) continue;
        pen.setWidthF(1.0);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->setOpacity(0.33);
        for (int i = 0; i + 3 < item.bucketFlat.size(); i += 4) {
            const double tMs = item.bucketFlat.at(i);
            if (tMs < viewStartMs || tMs > viewEndMs) continue;
            const double x = pxX(tMs);
            const double yLo = pxY(item.bucketFlat.at(i + 1));
            const double yHi = pxY(item.bucketFlat.at(i + 2));
            if (qAbs(yHi - yLo) < 0.8) continue;
            painter->drawLine(QPointF(x, yLo), QPointF(x, yHi));
        }

        QPainterPath path;
        bool started = false;
        QPointF lastPt;
        for (int i = 0; i + 3 < item.bucketFlat.size(); i += 4) {
            const double tMs = item.bucketFlat.at(i);
            if (tMs < viewStartMs || tMs > viewEndMs) continue;
            const QPointF pt(pxX(tMs), pxY(item.bucketFlat.at(i + 3)));
            if (!started) { path.moveTo(pt); started = true; }
            else path.lineTo(pt);
            lastPt = pt;
        }
        if (!started) continue;
        pen.setWidthF(1.8);
        painter->setOpacity(1.0);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        painter->setBrush(color);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(lastPt, 3.0, 3.0);
    }

    if (qIsFinite(m_cursorMs) && m_cursorMs >= viewStartMs && m_cursorMs <= viewEndMs) {
        const double x = pxX(m_cursorMs);
        QPen cursorPen(QColor(QStringLiteral("#f59e0b")));
        cursorPen.setCosmetic(true);
        cursorPen.setWidthF(1.2);
        cursorPen.setStyle(Qt::DashLine);
        painter->setPen(cursorPen);
        painter->setBrush(Qt::NoBrush);
        painter->setOpacity(0.95);
        painter->drawLine(QPointF(x, plotY), QPointF(x, plotY + plotH));
    }
}
