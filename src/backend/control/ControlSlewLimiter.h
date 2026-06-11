#pragma once

namespace CanMonitorControl {

struct ControlSlewTarget {
    int signedCommand = 0;
    int rpm = 0;
    double steeringDeg = 0.0;
};

struct ControlSlewState {
    int signedCommand = 0;
    int rpm = 0;
    double steeringDeg = 0.0;
};

class ControlSlewLimiter {
public:
    static constexpr int kMaxCommandAccelStepPerCycle = 250;
    static constexpr int kMaxCommandBrakeStepPerCycle = 1000;
    static constexpr int kMaxRpmAccelStepPerCycle = 250;
    static constexpr int kMaxRpmBrakeStepPerCycle = 1000;
    static constexpr double kMaxSteerStepDegPerCycle = 5.0;
    static constexpr double kMaxSteerReturnStepDegPerCycle = 15.0;

    static constexpr int kMaxCommandStepPerCycle = kMaxCommandAccelStepPerCycle;
    static constexpr int kMaxRpmStepPerCycle = kMaxRpmAccelStepPerCycle;

    void reset(const ControlSlewState& state = {});
    void setTarget(const ControlSlewTarget& target);
    ControlSlewState step();

    const ControlSlewTarget& target() const { return m_target; }
    const ControlSlewState& state() const { return m_state; }

private:
    ControlSlewTarget m_target;
    ControlSlewState m_state;
};

} // namespace CanMonitorControl
