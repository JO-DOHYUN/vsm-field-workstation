# 2026-05-26 Build And Token Budget Harness

## Decision
- VSM code implementation and build verification default to `C:\Users\JEON0295\Documents\PlatformIO\Projects\J_ArdP7_AM2_CSM\qt`.
- `C:\WORKS\VS\turn81_full_buildfix2` remains a reference/migration workspace and is not duplicated for routine edits or builds.
- Qt/CMake/MSVC verification must run through `VsDevCmd.bat` or an already initialized Visual Studio developer shell.
- Successful configure/build/deploy logs are summarized; full logs are used only for failures or explicit user requests.

## Reason
- Ordinary PowerShell can invoke cached `cl.exe` without MSVC standard library `INCLUDE`/`LIB`, creating false build failures such as missing `type_traits` or `utility`.
- Maintaining both standalone VSM and monorepo `qt/` as active edit targets doubles file reads, patches, builds, tests, and CI discussion.
- `windeployqt` and MSVC include tracing produce high-volume output that does not help when the command succeeds.

## Expected Gain
- Avoid the recurring false first-build failure.
- Reduce duplicate local verification.
- Keep token usage focused on code changes, actual errors, and acceptance results.

## Rollback Rule
- Re-enable dual workspace edits only if the monorepo `qt/` copy diverges or a recovery comparison is explicitly requested.
- Use full build/deploy logs only when a command fails, when diagnosing packaging, or when the user asks for raw output.
