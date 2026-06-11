#include "control/ControlSlewLimiter.h"

#include <algorithm>
#include <cmath>

namespace CanMonitorControl {
namespace {

int stepTowardInt(int current, int target, int maxStep) {
    if (current < target) return std::min(current + maxStep, target);
    if (current > target) return std::max(current - maxStep, target);
    return current;
}

double stepTowardDouble(double current, double target, double maxStep) {
    if (current < target) return std::min(current + maxStep, target);
    if (current > target) return std::max(current - maxStep, target);
    return current;
}

bool hasOppositeNonZeroSign(int current, int target) {
    return current != 0 && target != 0 && ((current < 0) != (target < 0));
}

bool hasOppositeNonZeroSign(double current, double target) {
    return std::abs(current) > 0.001 && std::abs(target) > 0.001 && ((current < 0.0) != (target < 0.0));
}

int stepTowardSignedCommand(int current, int target, int accelStep, int brakeStep) {
    if (hasOppositeNonZeroSign(current, target)) {
        return stepTowardInt(current, 0, brakeStep);
    }
    const bool braking = target == 0 || std::abs(target) < std::abs(current);
    return stepTowardInt(current, target, braking ? brakeStep : accelStep);
}

int stepTowardRpm(int current, int target, int accelStep, int brakeStep) {
    return stepTowardInt(current, target, target < current ? brakeStep : accelStep);
}

double stepTowardSteering(double current, double target, double steerStep, double returnStep) {
    if (hasOppositeNonZeroSign(current, target)) {
        return stepTowardDouble(current, 0.0, returnStep);
    }
    const bool returning = std::abs(target) < 0.001 || std::abs(target) < std::abs(current);
    return stepTowardDouble(current, target, returning ? returnStep : steerStep);
}

ControlSlewTarget clampTarget(const ControlSlewTarget& target) {
    return {
        std::clamp(target.signedCommand, -10000, 10000),
        std::clamp(target.rpm, 0, 10000),
        std::clamp(target.steeringDeg, -90.0, 90.0),
    };
}

ControlSlewState clampState(const ControlSlewState& state) {
    return {
        std::clamp(state.signedCommand, -10000, 10000),
        std::clamp(state.rpm, 0, 10000),
        std::clamp(state.steeringDeg, -90.0, 90.0),
    };
}

} // namespace

void ControlSlewLimiter::reset(const ControlSlewState& state) {
    m_state = clampState(state);
    m_target = {m_state.signedCommand, m_state.rpm, m_state.steeringDeg};
}

void ControlSlewLimiter::setTarget(const ControlSlewTarget& target) {
    m_target = clampTarget(target);
}

ControlSlewState ControlSlewLimiter::step() {
    m_state.signedCommand = stepTowardSignedCommand(m_state.signedCommand,
                                                    m_target.signedCommand,
                                                    kMaxCommandAccelStepPerCycle,
                                                    kMaxCommandBrakeStepPerCycle);
    m_state.rpm = stepTowardRpm(m_state.rpm,
                                m_target.rpm,
                                kMaxRpmAccelStepPerCycle,
                                kMaxRpmBrakeStepPerCycle);
    m_state.steeringDeg = stepTowardSteering(m_state.steeringDeg,
                                             m_target.steeringDeg,
                                             kMaxSteerStepDegPerCycle,
                                             kMaxSteerReturnStepDegPerCycle);
    return m_state;
}

} // namespace CanMonitorControl
