# turn77 build/runtime fix

## Observed
- runtime: Components.PreciseSlider unavailable / No such file or directory
- build: LNK2019 unresolved external symbol AppController::logActionText / logPhaseText

## Cause
1. PreciseSlider.qml was referenced from QML but omitted from qt_add_qml_module(QML_FILES).
2. moc linked against logPhaseText/logActionText declarations, but the current translation unit state caused unresolved symbols at link time.

## Fix
- Added qml/components/PreciseSlider.qml to CMakeLists.txt QML_FILES.
- Inlined logPhaseText/logActionText in AppController.h and removed duplicate out-of-class definitions from AppController.cpp to make moc/link path deterministic after clean rebuild.
