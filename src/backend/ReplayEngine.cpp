#include "ReplayEngine.h"

#include <QFile>
#include <algorithm>
#include <cmath>

ReplayEngine::ReplayEngine(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ReplayEngine::tick);
}

double ReplayEngine::progress() const {
    if (m_frames.empty()) return 0.0;
    if (m_frames.size() == 1) return 1.0;
    const int clamped = std::clamp(m_index, 0, int(m_frames.size()) - 1);
    return double(clamped) / double(int(m_frames.size()) - 1);
}

quint64 ReplayEngine::currentUs() const {
    if (m_frames.empty()) return 0;
    const int clamped = std::clamp(m_index, 0, int(m_frames.size()) - 1);
    return m_frames[size_t(clamped)].tExtUs;
}

quint64 ReplayEngine::durationUs() const {
    if (m_frames.size() < 2) return 0;
    return m_frames.back().tExtUs - m_frames.front().tExtUs;
}

void ReplayEngine::emitCursor() {
    emit replayCursorChanged(m_index, frameCount(), currentUs(), durationUs(), progress());
}

bool ReplayEngine::loadFile(const QString& path) {
    m_timer.stop();
    m_frames.clear();
    m_index = 0;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit replayLoaded(false, QStringLiteral("파일 열기 실패: %1").arg(f.errorString()));
        emitCursor();
        return false;
    }

    quint64 lastTus = 0;
    bool haveLastTus = false;
    quint64 wrapCount = 0;
    while (!f.atEnd()) {
        QByteArray rec20 = f.read(20);
        if (rec20.size() != 20) break;
        auto decoded = PacketParser::decode20(rec20, lastTus, haveLastTus, wrapCount);
        if (decoded && !decoded->isStats) m_frames.push_back(decoded->frame);
    }

    emit replayLoaded(!m_frames.empty(), QStringLiteral("재생 로드: %1 프레임").arg(m_frames.size()));
    emitCursor();
    return !m_frames.empty();
}

bool ReplayEngine::loadFrames(const QString& sourceName, const std::vector<FrameRecord>& frames) {
    m_timer.stop();
    m_frames = frames;
    std::stable_sort(m_frames.begin(), m_frames.end(), [](const FrameRecord& lhs, const FrameRecord& rhs) {
        return lhs.tExtUs < rhs.tExtUs;
    });
    m_index = 0;

    const bool ok = !m_frames.empty();
    emit replayLoaded(ok, ok
        ? QStringLiteral("Replay loaded: %1 frames (%2)").arg(m_frames.size()).arg(sourceName)
        : QStringLiteral("Replay load failed: no CAN_RX_RAW frames (%1)").arg(sourceName));
    emitCursor();
    return ok;
}

void ReplayEngine::play(double speedValue) {
    if (m_frames.empty()) return;
    m_speed = (!std::isfinite(speedValue) || speedValue <= 0.0) ? 1.0 : std::clamp(speedValue, 0.1, 8.0);
    if (m_index >= int(m_frames.size())) m_index = 0;
    emitCursor();
    tick();
}

void ReplayEngine::pause() {
    m_timer.stop();
    emitCursor();
}

void ReplayEngine::stop() {
    m_timer.stop();
    m_index = 0;
    emitCursor();
}

void ReplayEngine::setLoop(bool enabled) {
    if (m_loop == enabled) return;
    m_loop = enabled;
    emit replayLoopChanged(m_loop);
}

void ReplayEngine::setCurrentIndex(int index) {
    if (m_frames.empty()) return;
    m_timer.stop();
    m_index = std::clamp(index, 0, int(m_frames.size()) - 1);
    emitCursor();
}

void ReplayEngine::seekToRatio(double ratio) {
    if (m_frames.empty()) return;
    m_timer.stop();
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    if (m_frames.size() == 1) {
        m_index = 0;
    } else {
        m_index = std::clamp(int(clamped * double(int(m_frames.size()) - 1)), 0, int(m_frames.size()) - 1);
    }
    emitCursor();
}

void ReplayEngine::stepFrames(int delta) {
    if (m_frames.empty() || delta == 0) return;
    m_timer.stop();
    const int target = std::clamp(m_index + delta, 0, int(m_frames.size()) - 1);
    m_index = target;
    const FrameRecord& fr = m_frames[size_t(m_index)];
    emit replayFrame(fr);
    if (m_index < int(m_frames.size()) - 1) ++m_index;
    emitCursor();
}

void ReplayEngine::tick() {
    if (m_frames.empty()) return;

    if (m_index >= int(m_frames.size())) {
        if (m_loop) {
            m_index = 0;
        } else {
            m_timer.stop();
            emit replayFinished();
            emitCursor();
            return;
        }
    }

    const int curIndex = m_index;
    const FrameRecord fr = m_frames[size_t(curIndex)];
    emit replayFrame(fr);
    ++m_index;
    emitCursor();

    if (m_index >= int(m_frames.size())) {
        if (m_loop) {
            m_index = 0;
            const int delayMs = std::max(1, int(20.0 / m_speed));
            m_timer.start(delayMs);
            emitCursor();
            return;
        }
        emit replayFinished();
        emitCursor();
        return;
    }

    const qint64 nextUs = qint64(m_frames[size_t(m_index)].tExtUs);
    const qint64 curUs = qint64(fr.tExtUs);
    const qint64 gapUs = std::max<qint64>(0, nextUs - curUs);
    const int delayMs = std::max(1, int(double(gapUs) / 1000.0 / m_speed));
    m_timer.start(delayMs);
}
