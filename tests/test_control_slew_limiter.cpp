#include "control/ControlSlewLimiter.h"

#include <QtTest/QtTest>

using CanMonitorControl::ControlSlewLimiter;
using CanMonitorControl::ControlSlewState;
using CanMonitorControl::ControlSlewTarget;

class ControlSlewLimiterTest : public QObject {
    Q_OBJECT

private slots:
    void clampsTargetsAndStepsTowardPositiveCommand() {
        ControlSlewLimiter limiter;
        limiter.setTarget(ControlSlewTarget{20000, 20000, 120.0});

        const auto first = limiter.step();
        QCOMPARE(first.signedCommand, 250);
        QCOMPARE(first.rpm, 250);
        QCOMPARE(first.steeringDeg, 5.0);
        QCOMPARE(limiter.target().signedCommand, 10000);
        QCOMPARE(limiter.target().rpm, 10000);
        QCOMPARE(limiter.target().steeringDeg, 90.0);
    }

    void rampsSteeringWithoutOvershoot() {
        ControlSlewLimiter limiter;
        limiter.setTarget(ControlSlewTarget{0, 0, 2.5});

        QCOMPARE(limiter.step().steeringDeg, 2.5);
        QCOMPARE(limiter.step().steeringDeg, 2.5);
    }

    void handlesReverseDirectionWithoutOvershoot() {
        ControlSlewLimiter limiter;
        limiter.reset(ControlSlewState{500, 500, 2.0});
        limiter.setTarget(ControlSlewTarget{-300, 0, -0.5});

        auto state = limiter.step();
        QCOMPARE(state.signedCommand, 0);
        QCOMPARE(state.rpm, 0);
        QCOMPARE(state.steeringDeg, 0.0);

        state = limiter.step();
        QCOMPARE(state.signedCommand, -250);
        QCOMPARE(state.rpm, 0);
        QCOMPARE(state.steeringDeg, -0.5);

        state = limiter.step();
        QCOMPARE(state.signedCommand, -300);
        QCOMPARE(state.rpm, 0);
        QCOMPARE(state.steeringDeg, -0.5);

        state = limiter.step();
        QCOMPARE(state.signedCommand, -300);
        QCOMPARE(state.rpm, 0);
        QCOMPARE(state.steeringDeg, -0.5);
    }

    void usesGentleAccelerationAndFasterStopProfile() {
        ControlSlewLimiter limiter;
        limiter.setTarget(ControlSlewTarget{6000, 6000, 45.0});

        QCOMPARE(limiter.step().signedCommand, 250);
        QCOMPARE(limiter.step().signedCommand, 500);

        limiter.reset(ControlSlewState{3000, 3000, 45.0});
        limiter.setTarget(ControlSlewTarget{0, 0, 0.0});
        auto state = limiter.step();
        QCOMPARE(state.signedCommand, 2000);
        QCOMPARE(state.rpm, 2000);
        QCOMPARE(state.steeringDeg, 30.0);
    }

    void resetClampsStateAndMakesItTheCurrentTarget() {
        ControlSlewLimiter limiter;
        limiter.reset(ControlSlewState{-20000, 15000, -120.0});

        QCOMPARE(limiter.state().signedCommand, -10000);
        QCOMPARE(limiter.state().rpm, 10000);
        QCOMPARE(limiter.state().steeringDeg, -90.0);
        QCOMPARE(limiter.target().signedCommand, -10000);
        QCOMPARE(limiter.target().rpm, 10000);
        QCOMPARE(limiter.target().steeringDeg, -90.0);
        QCOMPARE(limiter.step().signedCommand, -10000);
    }
};

QTEST_APPLESS_MAIN(ControlSlewLimiterTest)

#include "test_control_slew_limiter.moc"
