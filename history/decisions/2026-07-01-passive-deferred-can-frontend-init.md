# Passive Deferred CAN Front-End Init

## Reason
Vehicle logs and bench behavior separated two concerns: ACK-capable observe is required after a stable CSM host session, but USB power-up / CDC enumeration while the board is already attached to vehicle CAN must not initialize or reconfigure the CAN front-end.

## Decision
The product contract now requires the CSM passive firmware to hold CAN front-end initialization through USB power-up, emit `CAN_FRONTEND_PRESESSION_HOLD`, wait the configured quiet window after a valid CDC/DTR session, initialize the two CAN front ends, then emit `CAN_FRONTEND_SESSION_READY` before VSM trusts CAN_RX_SEGMENT evidence.

## Expected Gain
This closes the firmware handoff gap between USB attach and ACK-observe. It does not claim hardware vehicle-impact-free PASS; final PASS still requires external analyzer/scope/DTC evidence.

## Rollback Rule
Rollback only if bench evidence shows deferred initialization itself causes worse vehicle disturbance than the previous boot-time initialization. If rolled back, VSM must mark the profile as hardware-unverified and product-blocking for vehicle hotplug.
