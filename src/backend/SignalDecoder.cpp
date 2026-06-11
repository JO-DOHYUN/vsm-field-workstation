#include "SignalDecoder.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

bool isPreviewNoiseName(const QString& rawName) {
    const QString n = rawName.trimmed().toLower();
    if (n.isEmpty()) return true;
    return n.contains(QStringLiteral("reserved")) ||
           n.contains(QStringLiteral("not defined")) ||
           n.contains(QStringLiteral("undefined")) ||
           n.contains(QStringLiteral("unused")) ||
           n.contains(QStringLiteral("spare")) ||
           n.contains(QStringLiteral("dummy")) ||
           n.contains(QStringLiteral("미정")) ||
           n.contains(QStringLiteral("미사용")) ||
           n.contains(QStringLiteral("예비"));
}

QString cleanSignalName(const QString& rawName) {
    QString name = rawName.trimmed();
    name.replace(QRegularExpression(QStringLiteral(R"([_\s]+)")), QStringLiteral(" "));
    name.replace(QRegularExpression(QStringLiteral(R"(\s*\([^)]*byte[^)]*\))"), QRegularExpression::CaseInsensitiveOption), QString());
    name.replace(QRegularExpression(QStringLiteral(R"(\s*\([^)]*bit[^)]*\))"), QRegularExpression::CaseInsensitiveOption), QString());
    return name.trimmed();
}

QString fmtCompactNumber(double value) {
    const double absValue = std::abs(value);
    const double rounded1 = std::round(value * 10.0) / 10.0;
    const double rounded2 = std::round(value * 100.0) / 100.0;
    int decimals = 0;
    if (absValue < 1.0) decimals = 3;
    else if (absValue < 10.0) decimals = 2;
    else if (absValue < 100.0) decimals = (std::abs(value - rounded1) > 1e-9) ? 2 : 1;
    else if (absValue < 1000.0) decimals = (std::abs(value - rounded2) > 1e-9) ? 2 : 1;
    QString s = QString::number(value, 'f', decimals);
    while (s.contains('.') && (s.endsWith('0') || s.endsWith('.'))) {
        if (s.endsWith('.')) { s.chop(1); break; }
        s.chop(1);
    }
    return s;
}


int decimalsFromStep(double step) {
    const double absStep = std::abs(step);
    if (absStep < 1e-12) return 0;
    for (int d = 0; d <= 4; ++d) {
        const double scaled = absStep * std::pow(10.0, d);
        if (std::abs(scaled - std::round(scaled)) < 1e-6) return d;
    }
    return 2;
}

QString fmtStablePhysicalNumber(double value, double scale, double offset) {
    int decimals = 0;
    if (std::abs(scale - 1.0) > 1e-12 || std::abs(offset) > 1e-12) {
        decimals = decimalsFromStep(scale);
        if (std::abs(offset - std::round(offset)) > 1e-9)
            decimals = std::max(decimals, 2);
    }
    decimals = std::clamp(decimals, 0, 4);
    return QString::number(value, 'f', decimals);
}

QString appendUnitText(const QString& valueText, const QString& unitText) {
    const QString unit = unitText.trimmed();
    if (unit.isEmpty()) return valueText;
    return QStringLiteral("%1 %2").arg(valueText, unit);
}

QString shortenPreview(const QString& text, int maxLen = 24) {
    const QString t = text.trimmed();
    if (t.size() <= maxLen) return t;
    return t.left(maxLen - 1) + QStringLiteral("…");
}

bool parseNumericRangeText(const QString& text, double* outMin, double* outMax) {
    if (!outMin || !outMax) return false;
    const QRegularExpression re(QStringLiteral(R"(([+-]?\d+(?:\.\d+)?)\s*(?:to|~|-)\s*([+-]?\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return false;
    bool ok1 = false, ok2 = false;
    const double a = m.captured(1).toDouble(&ok1);
    const double b = m.captured(2).toDouble(&ok2);
    if (!ok1 || !ok2) return false;
    *outMin = a;
    *outMax = b;
    return true;
}

qint64 signExtendRaw(quint64 raw, int bits) {
    if (bits <= 0 || bits >= 63) return qint64(raw);
    const quint64 signBit = (1ULL << (bits - 1));
    if ((raw & signBit) == 0) return qint64(raw);
    const quint64 mask = ~((1ULL << bits) - 1ULL);
    return qint64(raw | mask);
}

quint64 extractContiguousBits(const FrameRecord& frame, int startBit, int lengthBits) {
    quint64 value = 0;
    for (int i = 0; i < lengthBits && i < 64; ++i) {
        const int globalBit = startBit + i;
        const int byteIndex = globalBit / 8;
        const int bitIndex = globalBit % 8;
        if (byteIndex < 0 || byteIndex >= 8) break;
        if (byteIndex >= int(frame.dlc)) continue;
        if (frame.data[byteIndex] & (1u << bitIndex)) value |= (1ULL << i);
    }
    return value;
}

quint64 extractExplicitBits(const FrameRecord& frame, int byteIndex0, QVector<int> bitPositions) {
    if (byteIndex0 < 0 || byteIndex0 >= 8 || byteIndex0 >= int(frame.dlc)) return 0;
    std::sort(bitPositions.begin(), bitPositions.end());
    quint64 value = 0;
    for (int i = 0; i < bitPositions.size() && i < 64; ++i) {
        const int bitIndex = bitPositions.at(i);
        if (bitIndex < 0 || bitIndex > 7) continue;
        if (frame.data[byteIndex0] & (1u << bitIndex)) value |= (1ULL << i);
    }
    return value;
}

bool extractLittleEndianWord(const FrameRecord& frame, int byteIndex0, int lengthBits, quint64* outValue) {
    if (!outValue) return false;
    if (byteIndex0 < 0 || byteIndex0 >= 8) return false;
    if (lengthBits <= 0 || lengthBits > 64 || (lengthBits % 8) != 0) return false;
    const int byteCount = lengthBits / 8;
    if ((byteIndex0 + byteCount) > int(frame.dlc) || (byteIndex0 + byteCount) > 8) return false;

    quint64 raw = 0;
    for (int i = 0; i < byteCount; ++i) {
        raw |= (quint64(frame.data[byteIndex0 + i]) << (8 * i));
    }
    *outValue = raw;
    return true;
}

QString enumLabelForRaw(const QString& operatingText, qint64 rawValue) {
    if (operatingText.trimmed().isEmpty()) return {};
    const QRegularExpression re(QStringLiteral(R"((\d+)\s*[:.\-]\s*([^,/]+))"));
    auto it = re.globalMatch(operatingText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool ok = false;
        const qint64 key = m.captured(1).toLongLong(&ok);
        if (ok && key == rawValue) return m.captured(2).trimmed();
    }
    return {};
}

double inferScaleFromText(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) return 1.0;
    const QRegularExpression re(QStringLiteral(R"((?:unit|resolution|res)\s*[:=]?\s*([+-]?\d+(?:\.\d+)?))"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(t);
    if (m.hasMatch()) {
        bool ok = false;
        const double v = m.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    const QRegularExpression re2(QStringLiteral(R"(([+-]?0\.\d+))"));
    const auto m2 = re2.match(t);
    if (m2.hasMatch()) {
        bool ok = false;
        const double v = m2.captured(1).toDouble(&ok);
        if (ok && std::abs(v) > 0.0) return v;
    }
    return 1.0;
}

QString compactStateLabel(QString text) {
    QString t = cleanSignalName(text);
    t.replace(QRegularExpression(QStringLiteral(R"(^\s*(FAULT|WARNING|WARN|ERROR|ALARM)\s*[-:]?\s*)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QRegularExpression(QStringLiteral(R"(\(CHARGE\))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" CHG"));
    t.replace(QRegularExpression(QStringLiteral(R"(\(DISCHARGE\))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(" DSG"));
    const QList<QPair<QString, QString>> reps = {
        {QStringLiteral("OVER VOLTAGE"), QStringLiteral("OVP")},
        {QStringLiteral("UNDER VOLTAGE"), QStringLiteral("UVP")},
        {QStringLiteral("OVER CURRENT CHG"), QStringLiteral("OCC")},
        {QStringLiteral("OVER CURRENT DSG"), QStringLiteral("OCD")},
        {QStringLiteral("OVER CURRENT"), QStringLiteral("OCP")},
        {QStringLiteral("OVER TEMP CHG"), QStringLiteral("OTC")},
        {QStringLiteral("OVER TEMP DSG"), QStringLiteral("OTD")},
        {QStringLiteral("OVER TEMP"), QStringLiteral("OVT")},
        {QStringLiteral("UNDER TEMP"), QStringLiteral("UVT")},
        {QStringLiteral("SHORT PROTECTION"), QStringLiteral("SCP")},
        {QStringLiteral("SHORT"), QStringLiteral("SCP")},
        {QStringLiteral("PROTECTION"), QString()},
        {QStringLiteral("NORMAL"), QStringLiteral("정상")},
        {QStringLiteral("NOT DEFINED"), QString()},
        {QStringLiteral("DISCHARGE FET ON"), QStringLiteral("DSG FET")},
        {QStringLiteral("CHARGE FET ON"), QStringLiteral("CHG FET")}
    };
    QString upper = t.toUpper();
    for (const auto& p : reps) upper.replace(p.first, p.second);
    upper.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return upper.trimmed();
}

QString compactSignalName(QString text) {
    QString t = cleanSignalName(text);
    t.replace(QRegularExpression(QStringLiteral(R"(\bLow Byte\b|\bHigh Byte\b|\bUpper Byte\b|\bLower Byte\b)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QStringLiteral("Average"), QStringLiteral("Avg"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Minimum"), QStringLiteral("Min"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Maximum"), QStringLiteral("Max"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Difference"), QStringLiteral("Δ"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Voltage"), QStringLiteral("V"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Current"), QStringLiteral("I"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Temperature"), QStringLiteral("Temp"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Capacity"), QStringLiteral("Cap"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Counter"), QStringLiteral("Cnt"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Encoder"), QStringLiteral("Enc"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Battery"), QStringLiteral("Batt"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Motor"), QStringLiteral("MTR"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Hardware"), QStringLiteral("HW"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("Software"), QStringLiteral("SW"), Qt::CaseInsensitive);
    t.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return t.trimmed();
}

enum class PairPart { None, Low, High };

PairPart detectPairPart(const QString& name) {
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("low byte")) || n.contains(QStringLiteral("lower byte")) || n.contains(QStringLiteral("하위바이트"))) return PairPart::Low;
    if (n.contains(QStringLiteral("high byte")) || n.contains(QStringLiteral("upper byte")) || n.contains(QStringLiteral("상위바이트"))) return PairPart::High;
    return PairPart::None;
}

QString pairBaseName(QString name) {
    QString t = cleanSignalName(name);
    t.replace(QRegularExpression(QStringLiteral(R"(\b(Low|High|Upper|Lower)\s*Byte\b)"), QRegularExpression::CaseInsensitiveOption), QString());
    t.replace(QStringLiteral("상위바이트"), QString());
    t.replace(QStringLiteral("하위바이트"), QString());
    t.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return t.trimmed();
}

bool looksInactiveLabel(const QString& text) {
    const QString t = text.trimmed().toLower();
    return t.isEmpty() || t == QStringLiteral("0") || t == QStringLiteral("off") || t == QStringLiteral("inactive") ||
           t == QStringLiteral("normal") || t == QStringLiteral("정상") || t == QStringLiteral("none") ||
           t == QStringLiteral("disable") || t == QStringLiteral("disabled") || t == QStringLiteral("ready");
}

bool containsCaseInsensitive(const QStringList& list, const QString& value) {
    const QString needle = value.trimmed().toLower();
    if (needle.isEmpty()) return false;
    for (const QString& item : list) {
        if (item.trimmed().toLower() == needle) return true;
    }
    return false;
}

QString explicitThresholdText(const CanModel::SignalSpec& sig) {
    QStringList parts;
    if (sig.hasWarnMin || sig.hasWarnMax) {
        QStringList warn;
        if (sig.hasWarnMin) warn << QStringLiteral("min %1").arg(fmtCompactNumber(sig.warnMin));
        if (sig.hasWarnMax) warn << QStringLiteral("max %1").arg(fmtCompactNumber(sig.warnMax));
        parts << QStringLiteral("WARN[%1]").arg(warn.join(QStringLiteral(", ")));
    }
    if (sig.hasErrMin || sig.hasErrMax) {
        QStringList err;
        if (sig.hasErrMin) err << QStringLiteral("min %1").arg(fmtCompactNumber(sig.errMin));
        if (sig.hasErrMax) err << QStringLiteral("max %1").arg(fmtCompactNumber(sig.errMax));
        parts << QStringLiteral("ERR[%1]").arg(err.join(QStringLiteral(", ")));
    }
    return parts.join(QStringLiteral(" · "));
}

QString previewTokenColor(const QString& name, const QString& valueText, bool rangeBad) {
    const QString joined = (name + QLatin1Char(' ') + valueText).toLower();
    if (rangeBad || joined.contains(QStringLiteral("fault")) || joined.contains(QStringLiteral("error")) || joined.contains(QStringLiteral("ovp")) || joined.contains(QStringLiteral("uvp")) || joined.contains(QStringLiteral("ocp")) || joined.contains(QStringLiteral("occ")) || joined.contains(QStringLiteral("ocd")) || joined.contains(QStringLiteral("ovt")) || joined.contains(QStringLiteral("otc")) || joined.contains(QStringLiteral("otd")) || joined.contains(QStringLiteral("uvt")) || joined.contains(QStringLiteral("scp"))) return QStringLiteral("#c0392b");
    if (joined.contains(QStringLiteral("warn")) || joined.contains(QStringLiteral("warning"))) return QStringLiteral("#d97706");
    if (joined.contains(QStringLiteral("정상")) || joined.contains(QStringLiteral("ok")) || joined.contains(QStringLiteral("normal"))) return QStringLiteral("#118a42");
    return QStringLiteral("#0f4c81");
}

DetailRow makeModelDetailRow(const QString& key, const QString& value, const QString& note = QString()) {
    return DetailRow{key, value, note};
}

CanMonitorAnalysis::SignalPreviewResult buildPreviewResult(quint32 id,
                                                           const FrameRecord& frame,
                                                           const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                                           bool modelEnabled,
                                                           int maxTokens,
                                                           int maxTokenLen,
                                                           bool verboseNames) {
    CanMonitorAnalysis::SignalPreviewResult out;
    if (!modelEnabled) {
        out.plain = QStringLiteral("RAW %1").arg(hexBytes(frame.data, frame.dlc));
        out.rich = out.plain;
        return out;
    }

    const auto it = messages.constFind(id);
    if (it == messages.cend() || it.value().signalSpecs.isEmpty()) {
        out.plain = QStringLiteral("미등록 ID · RAW %1").arg(hexBytes(frame.data, frame.dlc));
        out.rich = out.plain;
        return out;
    }

    struct PreviewToken { QString text; QString color; int score = 0; };
    struct PairBytes { bool hasLow = false; bool hasHigh = false; quint64 low = 0; quint64 high = 0; double scale = 1.0; double offset = 0.0; bool signedValue = false; };
    QVector<PreviewToken> tokens;
    QHash<QString, PairBytes> pairParts;

    const auto& specs = it.value().signalSpecs;
    for (const CanModel::SignalSpec& sig : specs) {
        if (isPreviewNoiseName(sig.name)) continue;
        const int byteIndex0 = sig.byteIndex1Based - 1;
        if (byteIndex0 < 0 || byteIndex0 >= 8 || byteIndex0 >= int(frame.dlc)) continue;
        quint64 raw = 0;
        if (!sig.bitPositionsLsb.isEmpty() && sig.lengthBits <= sig.bitPositionsLsb.size()) raw = extractExplicitBits(frame, byteIndex0, sig.bitPositionsLsb);
        else if (sig.startBitLsb == 0 && extractLittleEndianWord(frame, byteIndex0, sig.lengthBits, &raw)) {
            // Byte-aligned little-endian multi-byte word.
        } else raw = extractContiguousBits(frame, byteIndex0 * 8 + sig.startBitLsb, sig.lengthBits);
        const qint64 signedRaw = sig.signedValue ? signExtendRaw(raw, sig.lengthBits) : qint64(raw);

        double scale = sig.scale;
        if (std::abs(scale - 1.0) < 1e-12 && std::abs(sig.offset) < 1e-12) {
            const double inferred1 = inferScaleFromText(sig.operatingText);
            if (std::abs(inferred1 - 1.0) > 1e-12) scale = inferred1;
            else {
                const double inferred2 = inferScaleFromText(sig.description);
                if (std::abs(inferred2 - 1.0) > 1e-12) scale = inferred2;
            }
        }
        const double physical = double(signedRaw) * scale + sig.offset;
        QString valueText = enumLabelForRaw(sig.operatingText, signedRaw);
        if (valueText.isEmpty()) valueText = enumLabelForRaw(sig.description, signedRaw);
        if (valueText.isEmpty()) {
            if (std::abs(scale - 1.0) > 1e-12 || std::abs(sig.offset) > 1e-12) valueText = appendUnitText(fmtStablePhysicalNumber(physical, scale, sig.offset), sig.unit);
            else valueText = QString::number(signedRaw);
        }

        const QString cleanName = cleanSignalName(sig.name);
        const PairPart part = detectPairPart(cleanName);
        if (part != PairPart::None && sig.lengthBits <= 8) {
            const QString base = pairBaseName(cleanName);
            PairBytes pair = pairParts.value(base);
            if (part == PairPart::Low) { pair.hasLow = true; pair.low = quint64(raw & 0xFFULL); }
            if (part == PairPart::High) { pair.hasHigh = true; pair.high = quint64(raw & 0xFFULL); }
            pair.scale = scale;
            pair.offset = sig.offset;
            pair.signedValue = sig.signedValue;
            pairParts.insert(base, pair);
            continue;
        }

        double minV = 0.0, maxV = 0.0;
        bool rangeBad = false;
        const QString rangeText = !sig.rangeText.trimmed().isEmpty() ? sig.rangeText : sig.description;
        if (!sig.reserved && sig.lengthBits > 2) {
            if (parseNumericRangeText(rangeText, &minV, &maxV)) rangeBad = physical < minV || physical > maxV;
        }

        QString tokenText;
        if (!valueText.trimmed().isEmpty() && valueText != QStringLiteral("0")) {
            if (verboseNames) {
                const QString verboseValueText = compactStateLabel(valueText);
                tokenText = QStringLiteral("%1 = %2").arg(cleanName, verboseValueText.isEmpty() ? valueText : verboseValueText);
            } else {
                const QString compactNameText = compactSignalName(cleanName);
                const QString compactValueText = compactStateLabel(valueText);
                const bool enumLike = compactValueText.toLower() != valueText.toLower() || valueText.contains(QRegularExpression(QStringLiteral("[A-Za-z가-힣]")));
                if (enumLike) tokenText = compactValueText;
                else tokenText = QStringLiteral("%1 %2").arg(compactNameText, valueText);
            }
        } else {
            tokenText = verboseNames ? cleanName : compactSignalName(cleanName);
        }

        const QString color = previewTokenColor(cleanName, valueText, rangeBad || sig.reserved);
        int score = 1;
        if (color == QStringLiteral("#c0392b")) score = 3;
        else if (color == QStringLiteral("#d97706")) score = 2;
        if (!tokenText.trimmed().isEmpty()) tokens.push_back({shortenPreview(tokenText, maxTokenLen), color, score});
    }

    for (auto itPair = pairParts.cbegin(); itPair != pairParts.cend(); ++itPair) {
        const QString base = itPair.key();
        const PairBytes pair = itPair.value();
        const quint64 mergedRaw = (pair.hasHigh ? ((pair.high & 0xFFULL) << 8) : 0ULL) | (pair.hasLow ? (pair.low & 0xFFULL) : 0ULL);
        const qint64 merged = pair.signedValue ? signExtendRaw(mergedRaw, 16) : qint64(mergedRaw);
        const double physical = double(merged) * pair.scale + pair.offset;
        QString tokenText;
        if (std::abs(pair.scale - 1.0) > 1e-12 || std::abs(pair.offset) > 1e-12) tokenText = QStringLiteral("%1 %2").arg(verboseNames ? base : compactSignalName(base), fmtStablePhysicalNumber(physical, pair.scale, pair.offset));
        else tokenText = QStringLiteral("%1 %2").arg(verboseNames ? base : compactSignalName(base)).arg(merged);
        tokens.push_back({shortenPreview(tokenText, maxTokenLen), QStringLiteral("#0f4c81"), 1});
    }

    std::sort(tokens.begin(), tokens.end(), [](const PreviewToken& a, const PreviewToken& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.text < b.text;
    });

    QStringList plainParts;
    QStringList richParts;
    const int limit = std::min(maxTokens, int(tokens.size()));
    for (int i = 0; i < limit; ++i) {
        plainParts << tokens.at(i).text;
        richParts << QStringLiteral("<font color=\"%1\"><b>%2</b></font>").arg(tokens.at(i).color, tokens.at(i).text.toHtmlEscaped());
    }

    if (plainParts.isEmpty()) {
        out.plain = QStringLiteral("해석값 없음");
        out.rich = out.plain;
    } else {
        out.plain = plainParts.join(QStringLiteral("  ·  "));
        out.rich = richParts.join(QStringLiteral("  ·  "));
    }
    return out;
}

} // namespace

namespace CanMonitorAnalysis {

QString SignalDecoder::displayNameForId(quint32 id,
                                        const QHash<quint32, CanModel::RuleSpec>& rules,
                                        const QHash<quint32, CanModel::SignalMessageSpec>& messages) {
    auto rit = rules.constFind(id);
    if (rit != rules.cend() && !rit.value().name.trimmed().isEmpty()) return rit.value().name.trimmed();
    auto sit = messages.constFind(id);
    if (sit != messages.cend()) {
        if (!sit.value().name.trimmed().isEmpty()) return sit.value().name.trimmed();
        if (!sit.value().signalSpecs.isEmpty()) return cleanSignalName(sit.value().signalSpecs.first().name);
    }
    return QStringLiteral("미등록 %1").arg(idText(id));
}

SignalPreviewResult SignalDecoder::makePreview(quint32 id,
                                               const FrameRecord& frame,
                                               const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                               bool modelEnabled) {
    return buildPreviewResult(id, frame, messages, modelEnabled, 10, 240, false);
}

QString SignalDecoder::makeVerbosePreview(quint32 id,
                                          const FrameRecord& frame,
                                          const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                          bool modelEnabled) {
    return buildPreviewResult(id, frame, messages, modelEnabled, 16, 2000, true).plain;
}

ValueAlarmResult SignalDecoder::makeValueAlarm(quint32 id,
                                               const FrameRecord& frame,
                                               const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                               bool modelEnabled) {
    ValueAlarmResult out;
    if (!modelEnabled) return out;
    const auto it = messages.constFind(id);
    if (it == messages.cend()) return out;

    int total = 0;
    int bad = 0;
    bool hardFault = false;
    QStringList labels;
    const auto& specs = it.value().signalSpecs;
    for (const CanModel::SignalSpec& sig : specs) {
        const int byteIndex0 = sig.byteIndex1Based - 1;
        if (byteIndex0 < 0 || byteIndex0 >= int(frame.dlc)) continue;
        if (isPreviewNoiseName(sig.name) || sig.monitorOnly) continue;

        quint64 raw = 0;
        if (!sig.bitPositionsLsb.isEmpty() && sig.lengthBits <= sig.bitPositionsLsb.size()) raw = extractExplicitBits(frame, byteIndex0, sig.bitPositionsLsb);
        else if (sig.startBitLsb == 0 && extractLittleEndianWord(frame, byteIndex0, sig.lengthBits, &raw)) {
            // Byte-aligned little-endian multi-byte word.
        } else raw = extractContiguousBits(frame, byteIndex0 * 8 + sig.startBitLsb, sig.lengthBits);
        const qint64 signedRaw = sig.signedValue ? signExtendRaw(raw, sig.lengthBits) : qint64(raw);
        const QString cleanName = cleanSignalName(sig.name);
        QString enumLabel = enumLabelForRaw(sig.operatingText, signedRaw);
        if (enumLabel.isEmpty()) enumLabel = enumLabelForRaw(sig.description, signedRaw);

        double scale = sig.scale;
        if (std::abs(scale - 1.0) < 1e-12 && std::abs(sig.offset) < 1e-12) {
            const double inferred1 = inferScaleFromText(sig.operatingText);
            if (std::abs(inferred1 - 1.0) > 1e-12) scale = inferred1;
            else {
                const double inferred2 = inferScaleFromText(sig.description);
                if (std::abs(inferred2 - 1.0) > 1e-12) scale = inferred2;
            }
        }
        const double physical = double(signedRaw) * scale + sig.offset;

        const QString explicitMode = sig.alarmMode.trimmed().toLower();
        const bool explicitRange = sig.hasWarnMin || sig.hasWarnMax || sig.hasErrMin || sig.hasErrMax;
        const bool hasInactiveConfig = !sig.inactiveRawValues.isEmpty() || !sig.inactiveLabels.isEmpty();
        const bool hasExplicitAlarm = sig.reserved || explicitRange || hasInactiveConfig ||
                                      !sig.alarmSeverity.trimmed().isEmpty() || !sig.alarmMessage.trimmed().isEmpty() ||
                                      (!explicitMode.isEmpty() && explicitMode != QStringLiteral("auto"));
        if (!hasExplicitAlarm) continue;

        total += 1;
        bool issue = false;
        bool errIssue = false;
        QString label = sig.alarmMessage.trimmed();

        if (explicitMode == QStringLiteral("none")) {
            total -= 1;
            continue;
        }

        const bool inactiveRaw = sig.inactiveRawValues.contains(signedRaw) || sig.inactiveRawValues.contains(qint64(raw));
        const bool inactiveLabel = containsCaseInsensitive(sig.inactiveLabels, enumLabel) || containsCaseInsensitive(sig.inactiveLabels, cleanName);
        QString effectiveMode = explicitMode;
        if (effectiveMode.isEmpty() || effectiveMode == QStringLiteral("auto")) {
            if (explicitRange) effectiveMode = QStringLiteral("range");
            else if (sig.reserved) effectiveMode = QStringLiteral("reserved");
            else effectiveMode = (sig.lengthBits <= 2) ? QStringLiteral("flag") : QStringLiteral("enum");
        }

        if (effectiveMode == QStringLiteral("reserved")) {
            if (signedRaw != 0) {
                issue = true;
                errIssue = true;
                if (label.isEmpty()) label = compactSignalName(cleanName) + QStringLiteral(" 예약값");
            }
        } else if (effectiveMode == QStringLiteral("range")) {
            const bool errLow = sig.hasErrMin && physical < sig.errMin;
            const bool errHigh = sig.hasErrMax && physical > sig.errMax;
            const bool warnLow = !errLow && !errHigh && sig.hasWarnMin && physical < sig.warnMin;
            const bool warnHigh = !errLow && !errHigh && sig.hasWarnMax && physical > sig.warnMax;
            if (errLow || errHigh || warnLow || warnHigh) {
                issue = true;
                errIssue = errLow || errHigh;
                if (label.isEmpty()) label = compactSignalName(cleanName) + QLatin1Char(' ') + fmtStablePhysicalNumber(physical, scale, sig.offset);
            }
        } else if (effectiveMode == QStringLiteral("flag")) {
            const QString stateLabel = !enumLabel.isEmpty() ? compactStateLabel(enumLabel) : compactStateLabel(cleanName);
            if (signedRaw != 0 && !inactiveRaw && !inactiveLabel && !looksInactiveLabel(stateLabel)) {
                issue = true;
                if (label.isEmpty()) label = stateLabel.isEmpty() ? compactSignalName(cleanName) : stateLabel;
            }
        } else if (effectiveMode == QStringLiteral("enum")) {
            const QString stateLabel = !enumLabel.isEmpty() ? compactStateLabel(enumLabel) : compactStateLabel(cleanName);
            if (!inactiveRaw && !inactiveLabel && !looksInactiveLabel(stateLabel)) {
                issue = true;
                if (label.isEmpty()) label = stateLabel.isEmpty() ? compactSignalName(cleanName) : stateLabel;
            }
        }

        if (issue) {
            const QString sev = sig.alarmSeverity.trimmed().toUpper();
            if (sev == QStringLiteral("ERR")) errIssue = true;
            if (errIssue) hardFault = true;
            bad += 1;
            if (!label.trimmed().isEmpty() && !labels.contains(label) && labels.size() < 4) labels << label.trimmed();
        }
    }

    if (bad == 0 || total == 0) return out;
    const double pct = (double(bad) * 100.0) / double(std::max(total, 1));
    out.active = true;
    out.severity = hardFault ? QStringLiteral("ERR") : (pct >= 50.0 ? QStringLiteral("ERR") : QStringLiteral("WARN"));
    out.message = labels.join(QStringLiteral(" | "));
    if (out.message.isEmpty()) out.message = QStringLiteral("값 이상");
    out.metricText = QString::number(pct, 'f', 1) + QStringLiteral(" %");
    out.gaugePct = pct;
    QString keyText = out.message.toLower().trimmed();
    keyText.replace(QRegularExpression(QStringLiteral(R"(([+-]?\d+(?:\.\d+)?)(?:\s*(%|ms|v|a|c|soc|soh)?))")), QStringLiteral("#"));
    keyText.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    out.alarmKey = QStringLiteral("value|%1|%2").arg(idText(id), keyText.trimmed());
    return out;
}


QVector<DetailRow> SignalDecoder::makeDetailRows(quint32 id,
                                                 const FrameRecord& frame,
                                                 const QHash<quint32, CanModel::SignalMessageSpec>& messages,
                                                 bool modelEnabled) {
    QVector<DetailRow> rows;
    if (!modelEnabled) {
        for (int i = 0; i < 8; ++i) {
            const int value = int(frame.data[i]);
            const QString hex = QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
            rows.push_back(makeModelDetailRow(QStringLiteral("BYTE %1").arg(i + 1), QStringLiteral("%1 / %2").arg(hex).arg(value), i < int(frame.dlc) ? QStringLiteral("모델 해제 상태") : QStringLiteral("DLC 밖 영역")));
        }
        return rows;
    }

    const auto it = messages.constFind(id);
    if (it == messages.cend()) {
        for (int i = 0; i < 8; ++i) {
            const int value = int(frame.data[i]);
            const QString hex = QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
            rows.push_back(makeModelDetailRow(QStringLiteral("BYTE %1").arg(i + 1), QStringLiteral("%1 / %2").arg(hex).arg(value), i < int(frame.dlc) ? QStringLiteral("모델 미등록 ID") : QStringLiteral("DLC 밖 영역")));
        }
        return rows;
    }

    const auto& msg = it.value();
    rows.push_back(makeModelDetailRow(QStringLiteral("메시지"), msg.name.isEmpty() ? idText(id) : msg.name,
                                      QStringLiteral("시그널 %1개 · DLC %2").arg(msg.signalSpecs.size()).arg(frame.dlc)));

    QHash<int, QStringList> byteNotes;
    for (const CanModel::SignalSpec& sig : msg.signalSpecs) {
        if (sig.byteIndex1Based < 1 || sig.byteIndex1Based > 8) continue;
        const int byteIndex0 = sig.byteIndex1Based - 1;
        const bool inDlc = byteIndex0 < int(frame.dlc);

        quint64 raw = 0;
        QString extractText;
        if (inDlc) {
            if (!sig.bitPositionsLsb.isEmpty() && sig.lengthBits <= sig.bitPositionsLsb.size()) {
                raw = extractExplicitBits(frame, byteIndex0, sig.bitPositionsLsb);
                QStringList bits;
                for (int bit : sig.bitPositionsLsb) bits << QString::number(bit);
                extractText = QStringLiteral("BYTE %1 bits[%2] LSB조합").arg(sig.byteIndex1Based).arg(bits.join(QStringLiteral(",")));
            } else if (sig.startBitLsb == 0 && extractLittleEndianWord(frame, byteIndex0, sig.lengthBits, &raw)) {
                extractText = QStringLiteral("BYTE %1 len %2 little-endian").arg(sig.byteIndex1Based).arg(sig.lengthBits);
            } else {
                raw = extractContiguousBits(frame, byteIndex0 * 8 + sig.startBitLsb, sig.lengthBits);
                extractText = QStringLiteral("BYTE %1 lsb %2 len %3").arg(sig.byteIndex1Based).arg(sig.startBitLsb).arg(sig.lengthBits);
            }
        } else {
            extractText = QStringLiteral("BYTE %1 · 현재 DLC %2 밖").arg(sig.byteIndex1Based).arg(frame.dlc);
        }
        const qint64 signedRaw = sig.signedValue ? signExtendRaw(raw, sig.lengthBits) : qint64(raw);
        double scale = sig.scale;
        if (std::abs(scale - 1.0) < 1e-12 && std::abs(sig.offset) < 1e-12) {
            const double inferred = inferScaleFromText(sig.operatingText);
            if (std::abs(inferred - 1.0) > 1e-12) scale = inferred;
        }
        const double physical = double(signedRaw) * scale + sig.offset;
        QString enumText = enumLabelForRaw(sig.operatingText, signedRaw);
        if (enumText.isEmpty()) enumText = enumLabelForRaw(sig.description, signedRaw);

        QString valueText;
        if (!inDlc) valueText = QStringLiteral("DLC 밖");
        else if (!enumText.isEmpty()) valueText = enumText;
        else if (std::abs(scale - 1.0) > 1e-12 || std::abs(sig.offset) > 1e-12) valueText = appendUnitText(fmtStablePhysicalNumber(physical, scale, sig.offset), sig.unit);
        else valueText = QString::number(signedRaw);

        double minV = 0.0, maxV = 0.0;
        const bool hasNumericRange = parseNumericRangeText(sig.rangeText, &minV, &maxV);
        const QString cleanName = cleanSignalName(sig.name);
        const QString explicitMode = sig.alarmMode.trimmed().toLower();
        const bool explicitRange = sig.hasWarnMin || sig.hasWarnMax || sig.hasErrMin || sig.hasErrMax;
        const bool hasInactiveConfig = !sig.inactiveRawValues.isEmpty() || !sig.inactiveLabels.isEmpty();
        const bool hasExplicitAlarm = sig.reserved || explicitRange || hasInactiveConfig ||
                                      !sig.alarmSeverity.trimmed().isEmpty() || !sig.alarmMessage.trimmed().isEmpty() ||
                                      (!explicitMode.isEmpty() && explicitMode != QStringLiteral("auto"));
        const bool inactiveRaw = sig.inactiveRawValues.contains(signedRaw) || sig.inactiveRawValues.contains(qint64(raw));
        const bool inactiveLabel = containsCaseInsensitive(sig.inactiveLabels, enumText) || containsCaseInsensitive(sig.inactiveLabels, cleanName);
        QString effectiveMode = explicitMode;
        if ((effectiveMode.isEmpty() || effectiveMode == QStringLiteral("auto")) && hasExplicitAlarm) {
            if (explicitRange) effectiveMode = QStringLiteral("range");
            else if (sig.reserved) effectiveMode = QStringLiteral("reserved");
            else effectiveMode = (sig.lengthBits <= 2) ? QStringLiteral("flag") : QStringLiteral("enum");
        }
        bool issue = false;
        QString issueText;
        if (hasExplicitAlarm && effectiveMode == QStringLiteral("reserved") && signedRaw != 0) {
            issue = true;
            issueText = QStringLiteral("reserved 비트가 0 아님");
        }
        if (!issue && hasExplicitAlarm && effectiveMode == QStringLiteral("range") && inDlc) {
            const bool errLow = sig.hasErrMin && physical < sig.errMin;
            const bool errHigh = sig.hasErrMax && physical > sig.errMax;
            const bool warnLow = !errLow && !errHigh && sig.hasWarnMin && physical < sig.warnMin;
            const bool warnHigh = !errLow && !errHigh && sig.hasWarnMax && physical > sig.warnMax;
            if (errLow || errHigh || warnLow || warnHigh) {
                issue = true;
                issueText = QStringLiteral("임계값 이탈 %1").arg(explicitThresholdText(sig));
            }
        }
        if (!issue && hasExplicitAlarm && effectiveMode == QStringLiteral("flag")) {
            const QString stateLabel = !enumText.isEmpty() ? compactStateLabel(enumText) : compactStateLabel(cleanName);
            if (inDlc && signedRaw != 0 && !inactiveRaw && !inactiveLabel && !looksInactiveLabel(stateLabel)) {
                issue = true;
                issueText = QStringLiteral("상태 비트 활성");
            }
        }
        if (!issue && hasExplicitAlarm && effectiveMode == QStringLiteral("enum")) {
            const QString stateLabel = !enumText.isEmpty() ? compactStateLabel(enumText) : compactStateLabel(cleanName);
            if (inDlc && !inactiveRaw && !inactiveLabel && !looksInactiveLabel(stateLabel)) {
                issue = true;
                issueText = QStringLiteral("비정상 enum 상태");
            }
        }

        QStringList noteParts;
        noteParts << extractText;
        noteParts << (sig.signedValue ? QStringLiteral("signed") : QStringLiteral("unsigned"));
        noteParts << QStringLiteral("raw %1 (0x%2)").arg(signedRaw).arg(QString::number(raw, 16).toUpper());
        if (std::abs(scale - 1.0) > 1e-12 || std::abs(sig.offset) > 1e-12) {
            noteParts << QStringLiteral("식 raw×%1 + %2 → %3").arg(fmtCompactNumber(scale), fmtCompactNumber(sig.offset), fmtStablePhysicalNumber(physical, scale, sig.offset));
        } else {
            noteParts << QStringLiteral("물리값=raw");
        }
        if (hasNumericRange && inDlc) {
            noteParts << ((physical < minV || physical > maxV) ? QStringLiteral("범위 벗어남 %1").arg(sig.rangeText)
                                                               : QStringLiteral("범위 %1").arg(sig.rangeText));
        } else if (!sig.rangeText.isEmpty()) {
            noteParts << QStringLiteral("range %1").arg(sig.rangeText);
        }
        const QString thresholdText = explicitThresholdText(sig);
        if (!thresholdText.isEmpty()) noteParts << thresholdText;
        if (!sig.unit.isEmpty()) noteParts << QStringLiteral("unit %1").arg(sig.unit);
        if (!enumText.isEmpty()) noteParts << QStringLiteral("enum %1").arg(enumText);
        if (!sig.operatingText.isEmpty()) noteParts << QStringLiteral("운용 %1").arg(sig.operatingText.left(96));
        if (sig.reserved) noteParts << (signedRaw == 0 ? QStringLiteral("reserved=0") : QStringLiteral("reserved인데 0 아님"));
        if (!sig.inactiveRawValues.isEmpty()) {
            QStringList vals;
            for (qint64 v : sig.inactiveRawValues) vals << QString::number(v);
            noteParts << QStringLiteral("inactive raw %1").arg(vals.join(QStringLiteral(",")));
        }
        if (!sig.inactiveLabels.isEmpty()) noteParts << QStringLiteral("inactive label %1").arg(sig.inactiveLabels.join(QStringLiteral(", ")));
        if (!sig.alarmMode.trimmed().isEmpty()) noteParts << QStringLiteral("alarm_mode %1").arg(sig.alarmMode);
        if (issue) noteParts << QStringLiteral("현재 값 경보 근거: %1").arg(issueText);
        if (!sig.description.isEmpty()) noteParts << sig.description;
        if (!inDlc) noteParts << QStringLiteral("현재 DLC %1").arg(frame.dlc);

        rows.push_back(makeModelDetailRow(QStringLiteral("SIG %1").arg(cleanName), valueText, noteParts.join(QStringLiteral(" · "))));

        QStringList byteNoteParts;
        byteNoteParts << QStringLiteral("%1=%2").arg(sig.name, valueText);
        byteNoteParts << extractText;
        if (std::abs(scale - 1.0) > 1e-12 || std::abs(sig.offset) > 1e-12) byteNoteParts << QStringLiteral("raw %1 → %2").arg(signedRaw).arg(fmtStablePhysicalNumber(physical, scale, sig.offset));
        if (hasNumericRange && inDlc) {
            byteNoteParts << ((physical < minV || physical > maxV) ? QStringLiteral("범위 벗어남(%1)").arg(sig.rangeText)
                                                                   : QStringLiteral("범위 %1").arg(sig.rangeText));
        } else if (!sig.rangeText.isEmpty()) {
            byteNoteParts << QStringLiteral("범위 %1").arg(sig.rangeText);
        }
        const QString byteThresholdText = explicitThresholdText(sig);
        if (!byteThresholdText.isEmpty()) byteNoteParts << byteThresholdText;
        if (!sig.unit.isEmpty()) byteNoteParts << QStringLiteral("unit %1").arg(sig.unit);
        if (issue) byteNoteParts << QStringLiteral("경보근거 %1").arg(issueText);
        if (sig.reserved) byteNoteParts << (signedRaw == 0 ? QStringLiteral("reserved=0") : QStringLiteral("reserved인데 0 아님"));
        if (!sig.description.isEmpty()) byteNoteParts << sig.description;
        if (!inDlc) byteNoteParts << QStringLiteral("현재 DLC %1").arg(frame.dlc);
        byteNotes[sig.byteIndex1Based] << byteNoteParts.join(QStringLiteral(" · "));
    }

    for (int i = 0; i < 8; ++i) {
        const int value = int(frame.data[i]);
        const QString hex = QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
        const QString valueText = QStringLiteral("%1 / %2").arg(hex).arg(value);
        QString note;
        if (i >= int(frame.dlc)) note = QStringLiteral("DLC 밖 영역");
        else if (byteNotes.contains(i + 1)) note = byteNotes.value(i + 1).join(QStringLiteral(" || "));
        else note = QStringLiteral("정의된 시그널 없음");
        rows.push_back(makeModelDetailRow(QStringLiteral("BYTE %1").arg(i + 1), valueText, note));
    }

    if (rows.isEmpty()) rows.push_back(makeModelDetailRow(QStringLiteral("바이트 해석 없음"), QStringLiteral("시그널 DB 미등록"), QStringLiteral("BYTE 단위 RAW만 표시")));
    return rows;
}

} // namespace CanMonitorAnalysis
