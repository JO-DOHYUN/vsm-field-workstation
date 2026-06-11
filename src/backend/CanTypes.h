#pragma once

#include <QByteArray>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cstring>

struct FrameRecord {
    quint64 tExtUs = 0;
    quint32 canId = 0;
    bool ext = false;
    bool rtr = false;
    quint8 dlc = 0;
    quint8 data[8] = {0};
    quint8 bus = 0;
    quint8 seq = 0;
    bool hasObservedGap = false;
    quint64 observedGapUs = 0;
    QByteArray raw20;
};

struct StatsRecord {
    quint64 tExtUs = 0;
    quint32 droppedTotal = 0;
    quint32 fifoOverflowTotal = 0;
    quint16 rxFps1s = 0;
    quint16 txFps1s = 0;
    quint8 errPassive1s = 0;
    quint8 busOff1s = 0;
    quint8 seq = 0;
    QByteArray raw20;
};

struct PacketRecord {
    bool isStats = false;
    FrameRecord frame;
    StatsRecord stats;
};

using FrameRecordList = QVector<FrameRecord>;

Q_DECLARE_METATYPE(FrameRecord)
Q_DECLARE_METATYPE(FrameRecordList)
Q_DECLARE_METATYPE(StatsRecord)

static inline quint32 rdU32Le(const quint8* p) {
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

static inline QString hexBytes(const quint8 d[8], int len = 8) {
    QString s;
    const int count = std::clamp(len, 0, 8);
    for (int i = 0; i < count; ++i) {
        s += QString("%1").arg(d[i], 2, 16, QLatin1Char('0')).toUpper();
        if (i + 1 != count) s += ' ';
    }
    return s;
}

static inline QString idText(quint32 id) {
    return QString("0x%1").arg(id, 3, 16, QLatin1Char('0')).toUpper();
}
