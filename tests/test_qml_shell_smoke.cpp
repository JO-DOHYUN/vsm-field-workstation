#include "AppController.h"
#include "CanTypes.h"
#include "GraphViewportItem.h"
#include "TypedRecords.h"

#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

QStringList g_qmlErrors;

void qmlSmokeMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    Q_UNUSED(context);
    const QByteArray encoded = message.toLocal8Bit();
    if (type != QtWarningMsg && type != QtCriticalMsg && type != QtFatalMsg) {
        std::fprintf(stdout, "%s\n", encoded.constData());
        std::fflush(stdout);
        return;
    }

    const QString lower = message.toLower();
    const bool looksLikeQmlFailure =
        message.contains(QStringLiteral(".qml")) ||
        lower.contains(QStringLiteral("referenceerror")) ||
        lower.contains(QStringLiteral("typeerror")) ||
        lower.contains(QStringLiteral("cannot assign")) ||
        lower.contains(QStringLiteral("failed to load"));
    if (looksLikeQmlFailure) g_qmlErrors.push_back(message);
    std::fprintf(stderr, "%s\n", encoded.constData());
    std::fflush(stderr);
}

void registerRuntimeTypes() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    qRegisterMetaType<FrameRecord>("FrameRecord");
    qRegisterMetaType<FrameRecordList>("FrameRecordList");
    qRegisterMetaType<StatsRecord>("StatsRecord");
    qRegisterMetaType<TypedRecord>("TypedRecord");
    qRegisterMetaType<TypedRecordList>("TypedRecordList");
    qmlRegisterType<GraphViewportItem>("CanMonitor", 1, 0, "GraphViewport");
}

QString writeGraphModelFixture(const QString& rootPath) {
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("can-monitor-model-pack.v1"));
    root.insert(QStringLiteral("model_key"), QStringLiteral("qml_graph_fixture"));
    root.insert(QStringLiteral("model_name"), QStringLiteral("QML Graph Fixture"));
    root.insert(QStringLiteral("model_version"), QStringLiteral("2026-05-28"));
    root.insert(QStringLiteral("vendor"), QStringLiteral("Codex"));

    QJsonArray rules;
    QJsonArray messages;
    for (int i = 0; i < 24; ++i) {
        const QString idText = QStringLiteral("0x%1").arg(0x420 + i, 3, 16, QLatin1Char('0'));
        QJsonObject rule;
        rule.insert(QStringLiteral("id"), idText);
        rule.insert(QStringLiteral("name_en"), QStringLiteral("GRAPH_%1").arg(i));
        rule.insert(QStringLiteral("expected_period_ms"), 20.0);
        rule.insert(QStringLiteral("ttl_warn_ms"), 100.0);
        rule.insert(QStringLiteral("ttl_err_ms"), 200.0);
        rule.insert(QStringLiteral("period_err_warn_pct"), 20.0);
        rule.insert(QStringLiteral("period_err_err_pct"), 50.0);
        rules.push_back(rule);

        QJsonObject signal;
        signal.insert(QStringLiteral("name"), QStringLiteral("Voltage %1").arg(i));
        signal.insert(QStringLiteral("byte_index_1based"), 1);
        signal.insert(QStringLiteral("bit_text"), QStringLiteral("8..1"));
        signal.insert(QStringLiteral("length_bits"), 8);
        signal.insert(QStringLiteral("start_bit_lsb"), 0);
        signal.insert(QStringLiteral("bit_positions_lsb"), QJsonArray{});
        signal.insert(QStringLiteral("scale"), 1.0);
        signal.insert(QStringLiteral("offset"), 0.0);
        signal.insert(QStringLiteral("signed"), false);
        signal.insert(QStringLiteral("range_text"), QStringLiteral("0 to 255"));
        signal.insert(QStringLiteral("operating_text"), QStringLiteral("unit: 1 V"));
        signal.insert(QStringLiteral("description"), QStringLiteral("scroll color fixture voltage"));
        signal.insert(QStringLiteral("reserved"), false);
        signal.insert(QStringLiteral("unit"), QStringLiteral("V"));
        signal.insert(QStringLiteral("alarm_mode"), QStringLiteral("none"));

        QJsonObject message;
        message.insert(QStringLiteral("id"), idText);
        message.insert(QStringLiteral("name"), QStringLiteral("Graph Fixture %1").arg(i));
        message.insert(QStringLiteral("signals"), QJsonArray{signal});
        messages.push_back(message);
    }

    root.insert(QStringLiteral("rules"), rules);
    root.insert(QStringLiteral("messages"), messages);

    const QString path = rootPath + QStringLiteral("/qml_graph_model.json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return path;
}

QObject* findGraphSignalCheckBox(QObject* graphPage, const QString& key) {
    const auto boxes = graphPage->findChildren<QObject*>(QStringLiteral("graphSignalCheckBox"));
    for (QObject* box : boxes) {
        if (box && box->property("signalKey").toString() == key) return box;
    }
    return nullptr;
}

bool clickQuickItemCenter(QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled() || !item->window()) return false;
    const QPointF scenePoint = item->mapToScene(QPointF(item->width() * 0.5, item->height() * 0.5));
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, scenePoint.toPoint());
    return true;
}

bool isVisibleInTree(QQuickItem* item) {
    for (QQuickItem* current = item; current; current = current->parentItem()) {
        if (!current->isVisible()) return false;
    }
    return true;
}

QString layoutScreenshotRoot() {
    static const QString path = [] {
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
        QDir root(QStringLiteral(CAN_MONITOR_SOURCE_DIR "/.logs"));
        root.mkpath(QStringLiteral("ui_layout_rc_%1").arg(stamp));
        return root.filePath(QStringLiteral("ui_layout_rc_%1").arg(stamp));
    }();
    return path;
}

QString safeFileName(QString text) {
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
    return text.left(120);
}

void saveLayoutScreenshot(QObject* root, const QString& name) {
    auto* window = qobject_cast<QQuickWindow*>(root);
    if (!window) return;
    const QImage image = window->grabWindow();
    if (image.isNull()) return;
    image.save(QDir(layoutScreenshotRoot()).filePath(safeFileName(name) + QStringLiteral(".png")));
}

bool hasScrollableAncestor(QQuickItem* item) {
    for (QQuickItem* current = item->parentItem(); current; current = current->parentItem()) {
        const QVariant contentWidth = current->property("contentWidth");
        const QVariant contentHeight = current->property("contentHeight");
        const bool horizontal = contentWidth.isValid() && contentWidth.toDouble() > current->width() + 1.0;
        const bool vertical = contentHeight.isValid() && contentHeight.toDouble() > current->height() + 1.0;
        if (horizontal || vertical) return true;
    }
    return false;
}

bool findVisibleSafeTextPaintOverflow(QObject* root, QString* detail) {
    const auto objects = root->findChildren<QObject*>();
    for (QObject* object : objects) {
        if (!object || !object->property("paintedOverflowed").isValid()) continue;
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || item->width() <= 4.0 || item->height() <= 4.0 || !isVisibleInTree(item)) continue;
        if (!object->property("paintedOverflowed").toBool()) continue;

        const QString text = object->property("text").toString();
        const double paintedWidth = object->property("paintedWidth").toDouble();
        const double paintedHeight = object->property("paintedHeight").toDouble();
        if (detail) {
            *detail = QStringLiteral("SafeText painted outside its item: text='%1' item=%2x%3 painted=%4x%5")
                          .arg(text.left(96))
                          .arg(item->width(), 0, 'f', 1)
                          .arg(item->height(), 0, 'f', 1)
                          .arg(paintedWidth, 0, 'f', 1)
                          .arg(paintedHeight, 0, 'f', 1);
        }
        return true;
    }
    return false;
}

bool findVisibleLayoutProbeOutsideWindow(QObject* root, QString* detail) {
    auto* window = qobject_cast<QQuickWindow*>(root);
    if (!window) return false;
    const QRectF visibleRect(0.0, 0.0, window->width(), window->height());
    const QRectF allowedRect = visibleRect.adjusted(-1.0, -1.0, 1.0, 1.0);

    const auto objects = root->findChildren<QObject*>();
    for (QObject* object : objects) {
        if (!object || !object->property("layoutProbe").toBool()) continue;
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || item->width() <= 3.0 || item->height() <= 3.0 || !isVisibleInTree(item)) continue;

        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        const QPointF bottomRight = item->mapToScene(QPointF(item->width(), item->height()));
        const QRectF rect = QRectF(topLeft, bottomRight).normalized();
        if (allowedRect.contains(rect)) continue;
        if (hasScrollableAncestor(item)) continue;

        if (detail) {
            *detail = QStringLiteral("Visible layout probe outside window: object='%1' class=%2 rect=(%3,%4 %5x%6) window=%7x%8")
                          .arg(object->objectName().isEmpty() ? object->property("text").toString().left(64) : object->objectName())
                          .arg(QString::fromLatin1(object->metaObject()->className()))
                          .arg(rect.x(), 0, 'f', 1)
                          .arg(rect.y(), 0, 'f', 1)
                          .arg(rect.width(), 0, 'f', 1)
                          .arg(rect.height(), 0, 'f', 1)
                          .arg(window->width())
                          .arg(window->height());
        }
        return true;
    }
    return false;
}

bool findBadPageViewport(QObject* root, QString* detail) {
    const auto viewports = root->findChildren<QObject*>(QStringLiteral("pageViewport"));
    for (QObject* object : viewports) {
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || !isVisibleInTree(item) || item->width() <= 3.0 || item->height() <= 3.0) continue;
        const double minPageWidth = object->property("minPageWidth").toDouble();
        const double uiScale = object->property("uiScale").toDouble();
        const double expectedWidth = std::max(item->width(), minPageWidth * uiScale);
        const double contentWidth = object->property("contentWidth").toDouble();
        const bool needsHorizontalScroll = object->property("needsHorizontalScroll").toBool();
        const bool horizontalScrollEnabled = object->property("horizontalScrollEnabled").toBool();
        if (contentWidth + 1.0 < expectedWidth || (expectedWidth > item->width() + 1.0 && (!needsHorizontalScroll || !horizontalScrollEnabled))) {
            if (detail) {
                *detail = QStringLiteral("PageViewport scroll contract failed: item=%1x%2 min=%3 scale=%4 content=%5 needs=%6 enabled=%7")
                              .arg(item->width(), 0, 'f', 1)
                              .arg(item->height(), 0, 'f', 1)
                              .arg(minPageWidth, 0, 'f', 1)
                              .arg(uiScale, 0, 'f', 2)
                              .arg(contentWidth, 0, 'f', 1)
                              .arg(needsHorizontalScroll)
                              .arg(horizontalScrollEnabled);
            }
            return true;
        }
    }
    return false;
}

} // namespace

class QmlShellSmokeTest : public QObject {
    Q_OBJECT

private slots:
    void loadsEveryTabAtSupportedScales() {
        registerRuntimeTypes();
        qInstallMessageHandler(qmlSmokeMessageHandler);
        g_qmlErrors.clear();

        AppController controller;
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(CAN_MONITOR_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);

        bool creationFailed = false;
        connect(&engine, &QQmlApplicationEngine::objectCreationFailed, this, [&creationFailed]() {
            creationFailed = true;
        });

        engine.load(QUrl::fromLocalFile(QStringLiteral(CAN_MONITOR_QML_DIR "/Main.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(creationFailed || !engine.rootObjects().isEmpty(), 5000);
        QVERIFY2(!creationFailed, "QML object creation failed");
        QVERIFY2(!engine.rootObjects().isEmpty(), "QML root object was not created");

        QObject* root = engine.rootObjects().first();
        QVERIFY(root);
        QVERIFY2(root->property("visible").toBool(), "QML root window is not visible");

        const QStringList keys = {
            QStringLiteral("overview"),
            QStringLiteral("live"),
            QStringLiteral("replay"),
            QStringLiteral("timing"),
            QStringLiteral("value"),
            QStringLiteral("graph"),
            QStringLiteral("graph_overview"),
            QStringLiteral("alarm"),
            QStringLiteral("settings"),
            QStringLiteral("control"),
        };

        const QList<QSize> responsiveSizes = {
            QSize(820, 600),
            QSize(900, 640),
            QSize(1024, 600),
            QSize(980, 720),
            QSize(1100, 760),
            QSize(1366, 768),
            QSize(1280, 800),
            QSize(1680, 980),
        };
        for (const QSize& size : responsiveSizes) {
            QVERIFY(root->setProperty("width", size.width()));
            QVERIFY(root->setProperty("height", size.height()));
            for (int scaleIndex = 0; scaleIndex < 3; ++scaleIndex) {
                QVERIFY(root->setProperty("uiScaleIndex", scaleIndex));
                QTest::qWait(60);
                for (const QString& key : keys) {
                    const QVariant keyArg(key);
                    QVERIFY2(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, keyArg)),
                             qPrintable(QStringLiteral("openPanelByKey failed for %1 at %2x%3 scale %4")
                                            .arg(key)
                                            .arg(size.width())
                                            .arg(size.height())
                                            .arg(scaleIndex)));
                    QTest::qWait(55);
                    QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));
                    QString overflowDetail;
                    QVERIFY2(!findVisibleSafeTextPaintOverflow(root, &overflowDetail), qPrintable(overflowDetail));
                    QString layoutDetail;
                    QVERIFY2(!findVisibleLayoutProbeOutsideWindow(root, &layoutDetail), qPrintable(layoutDetail));
                    QString viewportDetail;
                    QVERIFY2(!findBadPageViewport(root, &viewportDetail), qPrintable(viewportDetail));
                    if (scaleIndex == 1 && (size == QSize(820, 600) || size == QSize(1366, 768))) {
                        saveLayoutScreenshot(root, QStringLiteral("%1_%2x%3_scale100").arg(key).arg(size.width()).arg(size.height()));
                    }
                }
            }
        }

        QVERIFY(root->setProperty("workspaceMode", true));
        QTest::qWait(80);
        QVERIFY(root->setProperty("width", 1180));
        QVERIFY(root->setProperty("height", 720));
        QVERIFY(root->setProperty("uiScaleIndex", 1));
        for (const QString& key : {QStringLiteral("live"), QStringLiteral("graph_overview"), QStringLiteral("control")}) {
            const QVariant keyArg(key);
            QVERIFY(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, keyArg)));
            QTest::qWait(80);
            QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));
            QString overflowDetail;
            QVERIFY2(!findVisibleSafeTextPaintOverflow(root, &overflowDetail), qPrintable(overflowDetail));
            QString layoutDetail;
            QVERIFY2(!findVisibleLayoutProbeOutsideWindow(root, &layoutDetail), qPrintable(layoutDetail));
            QString viewportDetail;
            QVERIFY2(!findBadPageViewport(root, &viewportDetail), qPrintable(viewportDetail));
            saveLayoutScreenshot(root, QStringLiteral("workspace_%1_1180x720_scale100").arg(key));
        }
    }

    void graphSignalTogglePreservesScrollAndCatalogColor() {
        registerRuntimeTypes();
        qInstallMessageHandler(qmlSmokeMessageHandler);
        g_qmlErrors.clear();

        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QByteArray appDataPath = QDir::toNativeSeparators(tempDir.path()).toUtf8();
        qputenv("APPDATA", appDataPath);
        qputenv("LOCALAPPDATA", appDataPath);

        const QString modelPath = writeGraphModelFixture(tempDir.path());
        QVERIFY(!modelPath.isEmpty());

        AppController controller;
        controller.clearSavedSession();
        controller.setRulesPath(modelPath);
        QTRY_VERIFY(controller.graphCatalog().size() >= 12);

        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(CAN_MONITOR_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
        engine.load(QUrl::fromLocalFile(QStringLiteral(CAN_MONITOR_QML_DIR "/Main.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.rootObjects().isEmpty(), 5000);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));

        QObject* root = engine.rootObjects().first();
        QVERIFY(root);
        const QStringList pageKeys = {QStringLiteral("graph"), QStringLiteral("graph_overview")};
        const QStringList objectNames = {QStringLiteral("graphPage"), QStringLiteral("graphOverviewPage")};
        for (int pageIndex = 0; pageIndex < pageKeys.size(); ++pageIndex) {
            const QVariant graphKey(pageKeys.at(pageIndex));
            QVERIFY(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, graphKey)));
            QTest::qWait(120);

            QObject* graphPage = root->findChild<QObject*>(objectNames.at(pageIndex));
            QVERIFY2(graphPage, qPrintable(objectNames.at(pageIndex) + QStringLiteral(" object not found")));

            QVariant countValue;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testGraphCatalogCount", Q_RETURN_ARG(QVariant, countValue)));
            QVERIFY2(countValue.toInt() >= 2, "graph catalog fixture must expose multiple signals");

            QVariant maxYValue;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testSignalListMaxY", Q_RETURN_ARG(QVariant, maxYValue)));
            if (maxYValue.toDouble() <= 2.0) QSKIP("graph list is not scrollable in this viewport");

            const QVariant targetIndexValue(countValue.toInt() - 1);
            QVariant setYValue;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testSetSignalListContentY",
                                              Q_RETURN_ARG(QVariant, setYValue),
                                              Q_ARG(QVariant, maxYValue)));
            QVERIFY(setYValue.toDouble() > 1.0);
            const double yBefore = setYValue.toDouble();
            QTest::qWait(120);

            QVariant keyValue;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testGraphCatalogKeyAt",
                                              Q_RETURN_ARG(QVariant, keyValue),
                                              Q_ARG(QVariant, targetIndexValue)));
            const QString targetKey = keyValue.toString();
            QVERIFY(!targetKey.isEmpty());

            QVariant colorBefore;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testGraphCatalogColorAt",
                                              Q_RETURN_ARG(QVariant, colorBefore),
                                              Q_ARG(QVariant, targetIndexValue)));
            QVERIFY(!colorBefore.toString().isEmpty());

            QObject* signalCheckBox = findGraphSignalCheckBox(graphPage, targetKey);
            QVERIFY2(signalCheckBox, qPrintable(QStringLiteral("visible graph signal checkbox not found for %1").arg(targetKey)));
            QVERIFY2(clickQuickItemCenter(signalCheckBox), "graph signal checkbox could not be clicked");
            QTest::qWait(220);
            QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));

            QVariant yAfter;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testSignalListContentY", Q_RETURN_ARG(QVariant, yAfter)));
            QVERIFY2(std::abs(yAfter.toDouble() - yBefore) <= 1.0,
                     qPrintable(pageKeys.at(pageIndex) + QStringLiteral(" signal toggle moved the list instead of updating the row in place")));

            QVariant colorAfter;
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testGraphCatalogColorForKey",
                                              Q_RETURN_ARG(QVariant, colorAfter),
                                              Q_ARG(QVariant, QVariant(targetKey))));
            QCOMPARE(colorAfter.toString(), colorBefore.toString());

            QVariant keyValueFromWrapper;
            const double yBeforeWrapper = yAfter.toDouble();
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testUserToggleGraphSignalAt",
                                              Q_RETURN_ARG(QVariant, keyValueFromWrapper),
                                              Q_ARG(QVariant, targetIndexValue)));
            QCOMPARE(keyValueFromWrapper.toString(), targetKey);
            QTest::qWait(220);
            QVERIFY(QMetaObject::invokeMethod(graphPage, "testSignalListContentY", Q_RETURN_ARG(QVariant, yAfter)));
            QVERIFY2(std::abs(yAfter.toDouble() - yBeforeWrapper) <= 1.0,
                     qPrintable(pageKeys.at(pageIndex) + QStringLiteral(" wrapper toggle moved the list instead of updating the row in place")));
        }
    }

    void controlPageExposesOperatorEvidenceStages() {
        registerRuntimeTypes();
        qInstallMessageHandler(qmlSmokeMessageHandler);
        g_qmlErrors.clear();

        AppController controller;
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(CAN_MONITOR_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
        engine.load(QUrl::fromLocalFile(QStringLiteral(CAN_MONITOR_QML_DIR "/Main.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.rootObjects().isEmpty(), 5000);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));

        QObject* root = engine.rootObjects().first();
        QVERIFY(root);
        const QVariant controlKey(QStringLiteral("control"));
        QVERIFY(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, controlKey)));
        QTest::qWait(120);

        QObject* controlPage = root->findChild<QObject*>(QStringLiteral("controlPage"));
        QVERIFY2(controlPage, "controlPage object not found");

        QVariant countValue;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageCount", Q_RETURN_ARG(QVariant, countValue)));
        QCOMPARE(countValue.toInt(), 6);

        QVariant operatorSummary;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlOperatorSummary",
                                          Q_RETURN_ARG(QVariant, operatorSummary)));
        QVERIFY(operatorSummary.toString().contains(QStringLiteral("제어 차단")));
        QVERIFY(operatorSummary.toString().contains(QStringLiteral("CAN_TX_RAW 미확인")));

        QVariant verdict;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlActionVerdict",
                                          Q_RETURN_ARG(QVariant, verdict)));
        QVERIFY(verdict.toString().contains(QStringLiteral("COM 연결 없음")));

        QVariant checklistCount;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlChecklistCount",
                                          Q_RETURN_ARG(QVariant, checklistCount)));
        QCOMPARE(checklistCount.toInt(), 8);

        QVariant policySummary;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlPolicySummary",
                                          Q_RETURN_ARG(QVariant, policySummary)));
        QVERIFY(policySummary.toString().contains(QStringLiteral("default policy")));

        QVariant policyCount;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlPolicyChecklistCount",
                                          Q_RETURN_ARG(QVariant, policyCount)));
        QCOMPARE(policyCount.toInt(), 1);

        QVariant modeKey;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlChecklistField",
                                          Q_RETURN_ARG(QVariant, modeKey),
                                          Q_ARG(QVariant, QVariant(0)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("key")))));
        QCOMPARE(modeKey.toString(), QStringLiteral("mode"));

        QVariant serialBlocking;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlChecklistField",
                                          Q_RETURN_ARG(QVariant, serialBlocking),
                                          Q_ARG(QVariant, QVariant(1)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("blocking")))));
        QCOMPARE(serialBlocking.toBool(), true);

        QVariant txDetail;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlChecklistField",
                                          Q_RETURN_ARG(QVariant, txDetail),
                                          Q_ARG(QVariant, QVariant(6)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("detail")))));
        QVERIFY(txDetail.toString().contains(QStringLiteral("ACK/write")));

        QVariant armEnabled;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlActionEnabled",
                                          Q_RETURN_ARG(QVariant, armEnabled),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("arm")))));
        QCOMPARE(armEnabled.toBool(), false);

        QVariant ackValue;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageSummary",
                                          Q_RETURN_ARG(QVariant, ackValue),
                                          Q_ARG(QVariant, QVariant(2))));
        QVERIFY(ackValue.toString().contains(QStringLiteral("CONTROL_ACK")));
        QVERIFY(ackValue.toString().contains(QStringLiteral("No CONTROL_ACK")));
        QVariant ackAuthority;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageField",
                                          Q_RETURN_ARG(QVariant, ackAuthority),
                                          Q_ARG(QVariant, QVariant(2)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("authority")))));
        QCOMPARE(ackAuthority.toString(), QStringLiteral("board_acceptance_only"));
        QVariant ackSuccessAuthority;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageField",
                                          Q_RETURN_ARG(QVariant, ackSuccessAuthority),
                                          Q_ARG(QVariant, QVariant(2)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("successAuthority")))));
        QCOMPARE(ackSuccessAuthority.toBool(), false);

        QVariant txValue;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageSummary",
                                          Q_RETURN_ARG(QVariant, txValue),
                                          Q_ARG(QVariant, QVariant(3))));
        QVERIFY(txValue.toString().contains(QStringLiteral("CAN_TX_RAW")));
        QVERIFY(txValue.toString().contains(QStringLiteral("No CAN_TX_RAW")));
        QVariant txAuthority;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageField",
                                          Q_RETURN_ARG(QVariant, txAuthority),
                                          Q_ARG(QVariant, QVariant(3)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("authority")))));
        QCOMPARE(txAuthority.toString(), QStringLiteral("actual_can_tx"));
        QVariant txSuccessAuthority;
        QVERIFY(QMetaObject::invokeMethod(controlPage, "testControlStageField",
                                          Q_RETURN_ARG(QVariant, txSuccessAuthority),
                                          Q_ARG(QVariant, QVariant(3)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("successAuthority")))));
        QCOMPARE(txSuccessAuthority.toBool(), true);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));
    }

    void replayPageExposesTypedDiagnosticStateProbe() {
        registerRuntimeTypes();
        qInstallMessageHandler(qmlSmokeMessageHandler);
        g_qmlErrors.clear();

        AppController controller;
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(CAN_MONITOR_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
        engine.load(QUrl::fromLocalFile(QStringLiteral(CAN_MONITOR_QML_DIR "/Main.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.rootObjects().isEmpty(), 5000);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));

        QObject* root = engine.rootObjects().first();
        QVERIFY(root);
        const QVariant replayKey(QStringLiteral("replay"));
        QVERIFY(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, replayKey)));
        QTest::qWait(120);

        QObject* replayPage = root->findChild<QObject*>(QStringLiteral("replayPage"));
        QVERIFY2(replayPage, "replayPage object not found");

        QVariant countValue;
        QVERIFY(QMetaObject::invokeMethod(replayPage, "testReplayTypedDiagnosticCount",
                                          Q_RETURN_ARG(QVariant, countValue)));
        QCOMPARE(countValue.toInt(), 0);

        QVariant timelineValue;
        QVERIFY(QMetaObject::invokeMethod(replayPage, "testReplayTypedDiagnosticValue",
                                          Q_RETURN_ARG(QVariant, timelineValue),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("timeline")))));
        QCOMPARE(timelineValue.toString(), QString());

        QVariant busFilter;
        QVERIFY(QMetaObject::invokeMethod(replayPage, "testSetReplayBusFilter",
                                          Q_RETURN_ARG(QVariant, busFilter),
                                          Q_ARG(QVariant, QVariant(1))));
        QCOMPARE(busFilter.toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(replayPage, "testSetReplayBusFilter",
                                          Q_RETURN_ARG(QVariant, busFilter),
                                          Q_ARG(QVariant, QVariant(-1))));
        QCOMPARE(busFilter.toInt(), -1);

        QVariant maxSpeed;
        QVERIFY(QMetaObject::invokeMethod(replayPage, "testReplayMaxSpeedButton",
                                          Q_RETURN_ARG(QVariant, maxSpeed)));
        QCOMPARE(maxSpeed.toDouble(), 8.0);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));
    }

    void livePageExposesFieldStateProbe() {
        registerRuntimeTypes();
        qInstallMessageHandler(qmlSmokeMessageHandler);
        g_qmlErrors.clear();

        AppController controller;
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(CAN_MONITOR_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
        engine.load(QUrl::fromLocalFile(QStringLiteral(CAN_MONITOR_QML_DIR "/Main.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(!engine.rootObjects().isEmpty(), 5000);
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));

        QObject* root = engine.rootObjects().first();
        QVERIFY(root);
        const QVariant liveKey(QStringLiteral("live"));
        QVERIFY(QMetaObject::invokeMethod(root, "openPanelByKey", Q_ARG(QVariant, liveKey)));
        QTest::qWait(120);

        QObject* livePage = root->findChild<QObject*>(QStringLiteral("livePage"));
        QVERIFY2(livePage, "livePage object not found");

        QVariant countText;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testLiveFrameCountText",
                                          Q_RETURN_ARG(QVariant, countText)));
        QVERIFY(countText.toString().contains(QStringLiteral("0 / 0")));

        QVariant pauseText;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testLivePauseText",
                                          Q_RETURN_ARG(QVariant, pauseText)));
        QCOMPARE(pauseText.toString(), QStringLiteral("실시간 반영"));

        QVariant autoFollowValue;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testLiveAutoFollow",
                                          Q_RETURN_ARG(QVariant, autoFollowValue)));
        QCOMPARE(autoFollowValue.toBool(), true);

        QVariant liveBusFilter;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testSetLiveBusFilter",
                                          Q_RETURN_ARG(QVariant, liveBusFilter),
                                          Q_ARG(QVariant, QVariant(0))));
        QCOMPARE(liveBusFilter.toInt(), 0);
        QVERIFY(QMetaObject::invokeMethod(livePage, "testSetLiveBusFilter",
                                          Q_RETURN_ARG(QVariant, liveBusFilter),
                                          Q_ARG(QVariant, QVariant(-1))));
        QCOMPARE(liveBusFilter.toInt(), -1);

        QVariant logPreview;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testLiveLogTargetPreview",
                                          Q_RETURN_ARG(QVariant, logPreview)));
        QVERIFY(logPreview.toString().contains(QStringLiteral("replay_data")));

        QVariant transportCount;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testTransportDiagnosticCount",
                                          Q_RETURN_ARG(QVariant, transportCount)));
        QCOMPARE(transportCount.toInt(), 6);

        QVariant transportSummary;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testTransportDiagnosticSummary",
                                          Q_RETURN_ARG(QVariant, transportSummary)));
        QVERIFY(transportSummary.toString().contains(QStringLiteral("transport")));

        QVariant parserKey;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testTransportDiagnosticField",
                                          Q_RETURN_ARG(QVariant, parserKey),
                                          Q_ARG(QVariant, QVariant(1)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("key")))));
        QCOMPARE(parserKey.toString(), QStringLiteral("typed_parser"));

        QVariant captureKey;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testTransportDiagnosticField",
                                          Q_RETURN_ARG(QVariant, captureKey),
                                          Q_ARG(QVariant, QVariant(0)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("key")))));
        QCOMPARE(captureKey.toString(), QStringLiteral("capture_storage"));

        QVariant projectionKey;
        QVERIFY(QMetaObject::invokeMethod(livePage, "testTransportDiagnosticField",
                                          Q_RETURN_ARG(QVariant, projectionKey),
                                          Q_ARG(QVariant, QVariant(4)),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("key")))));
        QCOMPARE(projectionKey.toString(), QStringLiteral("live_projection"));
        QVERIFY2(g_qmlErrors.isEmpty(), qPrintable(g_qmlErrors.join(QStringLiteral("\n"))));
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Fusion"));
    QGuiApplication app(argc, argv);
    QmlShellSmokeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qml_shell_smoke.moc"
