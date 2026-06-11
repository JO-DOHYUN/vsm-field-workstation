#pragma once

#include "CanTypes.h"
#include "PacketParser.h"

#include <QObject>
#include <QTimer>
#include <vector>

class ReplayEngine : public QObject {
    Q_OBJECT
public:
    explicit ReplayEngine(QObject* parent = nullptr);

    Q_INVOKABLE bool loadFile(const QString& path);
    bool loadFrames(const QString& sourceName, const std::vector<FrameRecord>& frames);
    Q_INVOKABLE void play(double speed = 1.0);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setLoop(bool enabled);
    Q_INVOKABLE void seekToRatio(double ratio);
    Q_INVOKABLE void stepFrames(int delta);
    Q_INVOKABLE bool isLoaded() const { return !m_frames.empty(); }
    Q_INVOKABLE int frameCount() const { return int(m_frames.size()); }
    Q_INVOKABLE int currentIndex() const { return m_index; }
    Q_INVOKABLE double progress() const;
    Q_INVOKABLE quint64 currentUs() const;
    Q_INVOKABLE quint64 durationUs() const;
    Q_INVOKABLE bool loopEnabled() const { return m_loop; }
    Q_INVOKABLE double speed() const { return m_speed; }
    const std::vector<FrameRecord>& frames() const { return m_frames; }
    Q_INVOKABLE void setCurrentIndex(int index);

signals:
    void replayLoaded(bool ok, const QString& message);
    void replayFrame(const FrameRecord& fr);
    void replayFinished();
    void replayCursorChanged(int index, int frameCount, quint64 currentUs, quint64 durationUs, double progress);
    void replayLoopChanged(bool enabled);

private slots:
    void tick();

private:
    void emitCursor();

    std::vector<FrameRecord> m_frames;
    QTimer m_timer;
    int m_index = 0;
    double m_speed = 1.0;
    bool m_loop = false;
};
