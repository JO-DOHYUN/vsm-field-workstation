#include "TypedRecords.h"

#include <cstring>

namespace {

QByteArray typedPayloadView(const TypedRecord& record) {
    if (record.payload.size() == record.header.payloadLength) {
        return record.payload;
    }
    const qsizetype payloadLength = qsizetype(record.header.payloadLength);
    const qsizetype frameLength = kTypedTransportFrameOverhead + payloadLength;
    if (payloadLength >= 0 && record.frameBytes.size() >= frameLength) {
        return record.frameBytes.mid(9, payloadLength);
    }
    return record.payload;
}

} // namespace

quint16 typedReadU16Le(const quint8* p) {
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

quint32 typedReadU32Le(const quint8* p) {
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 typedReadU64Le(const quint8* p) {
    return quint64(typedReadU32Le(p)) | (quint64(typedReadU32Le(p + 4)) << 32);
}

qint64 typedReadI64Le(const quint8* p) {
    return qint64(typedReadU64Le(p));
}

QString typedRecordTypeName(quint8 recordType) {
    switch (static_cast<TypedRecordType>(recordType)) {
    case TypedRecordType::CanRxRaw: return QStringLiteral("CAN_RX_RAW");
    case TypedRecordType::CanTxRaw: return QStringLiteral("CAN_TX_RAW");
    case TypedRecordType::EncEdgeRaw: return QStringLiteral("ENC_EDGE_RAW");
    case TypedRecordType::EncDerived: return QStringLiteral("ENC_DERIVED");
    case TypedRecordType::AdcSample: return QStringLiteral("ADC_SAMPLE");
    case TypedRecordType::ControlAck: return QStringLiteral("CONTROL_ACK");
    case TypedRecordType::BoardEvent: return QStringLiteral("BOARD_EVENT");
    case TypedRecordType::BoardHealth: return QStringLiteral("BOARD_HEALTH");
    case TypedRecordType::Capability: return QStringLiteral("CAPABILITY");
    case TypedRecordType::HostCanTxRequest: return QStringLiteral("HOST_CAN_TX_REQUEST");
    case TypedRecordType::HostHeartbeat: return QStringLiteral("HOST_HEARTBEAT");
    case TypedRecordType::HostControlSession: return QStringLiteral("HOST_CONTROL_SESSION");
    case TypedRecordType::CanRxSegment: return QStringLiteral("CAN_RX_SEGMENT");
    case TypedRecordType::Unknown:
        break;
    }
    return QStringLiteral("TYPE_%1").arg(recordType);
}

quint64 typedRecordMonoUs(const TypedRecord& record) {
    if (record.isType(TypedRecordType::CanRxSegment)) {
        const auto first = decodeTypedCanRxSegmentEntry(record, 0);
        return first ? first->monoUs : 0;
    }
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < 8) return 0;
    return typedReadU64Le(reinterpret_cast<const quint8*>(payload.constData()));
}

std::optional<TypedCanRawRecord> decodeTypedCanRaw(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::CanRxRaw) && !record.isType(TypedRecordType::CanTxRaw)) {
        return std::nullopt;
    }
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedCanRawPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedCanRawRecord out;
    out.txAudit = record.isType(TypedRecordType::CanTxRaw);
    out.monoUs = typedReadU64Le(p + 0);
    out.canIdFlags = typedReadU32Le(p + 8);
    out.canId = out.canIdFlags & 0x1FFFFFFFu;
    out.extended = ((out.canIdFlags >> 29) & 0x01u) != 0;
    out.rtr = ((out.canIdFlags >> 30) & 0x01u) != 0;
    out.dlc = p[12] & 0x0F;
    if (out.dlc > 8) return std::nullopt;
    out.bus = p[13];
    std::memcpy(out.data, p + 14, 8);
    out.total = typedReadU32Le(p + 22);
    out.droppedOrFailed = typedReadU32Le(p + 26);
    return out;
}

std::optional<TypedCanRxSegmentHeader> decodeTypedCanRxSegmentHeader(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::CanRxSegment)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedCanRxSegmentHeaderSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedCanRxSegmentHeader out;
    out.segmentSeq = typedReadU64Le(p + 0);
    out.firstCaptureSeq = typedReadU64Le(p + 8);
    out.frameCount = typedReadU16Le(p + 16);
    out.entrySize = p[18];
    out.flags = p[19];
    out.droppedBeforeSegment = typedReadU32Le(p + 20);
    out.fifoBeforeSegment = typedReadU32Le(p + 24);
    if (out.entrySize < kTypedCanRxSegmentEntrySize) return std::nullopt;
    const qsizetype needed = kTypedCanRxSegmentHeaderSize + qsizetype(out.frameCount) * qsizetype(out.entrySize);
    if (needed > payload.size()) return std::nullopt;
    return out;
}

std::optional<TypedCanRxSegmentEntry> decodeTypedCanRxSegmentEntry(const TypedRecord& record, qsizetype frameIndex) {
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header || frameIndex < 0 || frameIndex >= header->frameCount) return std::nullopt;

    const qsizetype offset = kTypedCanRxSegmentHeaderSize + frameIndex * qsizetype(header->entrySize);
    const QByteArray payload = typedPayloadView(record);
    if (offset + kTypedCanRxSegmentEntrySize > payload.size()) return std::nullopt;
    const auto* p = reinterpret_cast<const quint8*>(payload.constData() + offset);

    TypedCanRxSegmentEntry out;
    out.captureSeq = typedReadU64Le(p + 0);
    out.monoUs = typedReadU64Le(p + 8);
    out.canIdFlags = typedReadU32Le(p + 16);
    out.canId = out.canIdFlags & 0x1FFFFFFFu;
    out.extended = ((out.canIdFlags >> 29) & 0x01u) != 0;
    out.rtr = ((out.canIdFlags >> 30) & 0x01u) != 0;
    out.dlc = p[20] & 0x0F;
    if (out.dlc > 8) return std::nullopt;
    out.bus = p[21];
    std::memcpy(out.data, p + 22, 8);
    return out;
}

QVector<TypedCanRxSegmentEntry> decodeTypedCanRxSegmentEntries(const TypedRecord& record) {
    QVector<TypedCanRxSegmentEntry> out;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    if (!header) return out;
    out.reserve(header->frameCount);
    for (qsizetype index = 0; index < header->frameCount; ++index) {
        const auto entry = decodeTypedCanRxSegmentEntry(record, index);
        if (!entry) {
            out.clear();
            return out;
        }
        out.push_back(*entry);
    }
    return out;
}

quint64 typedCanRxFrameCount(const TypedRecord& record) {
    if (record.isType(TypedRecordType::CanRxRaw)) return decodeTypedCanRaw(record).has_value() ? 1 : 0;
    const auto header = decodeTypedCanRxSegmentHeader(record);
    return header ? header->frameCount : 0;
}

std::optional<TypedAdcSampleRecord> decodeTypedAdcSample(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::AdcSample)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedAdcSamplePayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedAdcSampleRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.sampleTotal = typedReadU32Le(p + 8);
    out.droppedTotal = typedReadU32Le(p + 12);
    out.sourceId = p[16];
    out.channelCount = p[17];
    if (out.channelCount > 8) return std::nullopt;
    out.resolutionBits = p[18];
    out.flags = p[19];
    std::memcpy(out.channelId, p + 20, 8);
    for (int index = 0; index < 8; ++index) {
        out.raw[index] = typedReadU16Le(p + 28 + index * 2);
    }
    return out;
}

std::optional<TypedControlAckRecord> decodeTypedControlAck(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::ControlAck)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedControlAckPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedControlAckRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.commandId = typedReadU32Le(p + 8);
    out.status = p[12];
    out.reason = p[13];
    out.targetBus = p[14];
    out.targetDlcFlags = p[15];
    out.targetCanIdFlags = typedReadU32Le(p + 16);
    out.targetCanId = out.targetCanIdFlags & 0x1FFFFFFFu;
    out.targetExtended = ((out.targetCanIdFlags >> 29) & 0x01u) != 0;
    out.targetRtr = ((out.targetCanIdFlags >> 30) & 0x01u) != 0;
    out.counter = typedReadU32Le(p + 20);
    out.rejectedTotal = typedReadU32Le(p + 24);
    return out;
}

std::optional<TypedBoardEventRecord> decodeTypedBoardEvent(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::BoardEvent)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedBoardEventPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedBoardEventRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.code = typedReadU16Le(p + 8);
    out.detail = typedReadU16Le(p + 10);
    out.counter = typedReadU32Le(p + 12);
    return out;
}

std::optional<TypedBoardHealthRecord> decodeTypedBoardHealth(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::BoardHealth)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedBoardHealthPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedBoardHealthRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.canRxTotal = typedReadU32Le(p + 8);
    out.canDroppedTotal = typedReadU32Le(p + 12);
    out.canFifoOverflowTotal = typedReadU32Le(p + 16);
    out.serialRecordTxTotal = typedReadU32Le(p + 20);
    out.queueDepth = typedReadU32Le(p + 24);
    out.encoderFaultEvents = typedReadU32Le(p + 28);
    out.encoderWrapEvents = typedReadU32Le(p + 32);
    out.encoderPosition = typedReadI64Le(p + 36);
    out.safetyState = p[44];
    out.inputs = p[45];
    out.encoderTimerOk = p[46];
    out.flags = p[47];
    out.faultFlags = typedReadU32Le(p + 48);
    if (payload.size() >= kTypedBoardHealthExtendedPayloadSize) {
        out.hasExtendedTransportCounters = true;
        out.serialEnqueueFailTotal = typedReadU32Le(p + 160);
        out.serialRingClearTotal = typedReadU32Le(p + 164);
        out.serialRingClearedBytesTotal = typedReadU32Le(p + 168);
        out.serialBackpressureTotal = typedReadU32Le(p + 172);
        out.serialTxHighWaterBytes = typedReadU32Le(p + 176);
        out.sharedCanQueueHighWater = typedReadU32Le(p + 180);
        out.mcpDrainBudgetHitTotal = typedReadU32Le(p + 184);
        out.canSegmentEnqueueFailTotal = typedReadU32Le(p + 188);
    }
    if (payload.size() >= kTypedBoardHealthV5PayloadSize) {
        out.hasUplinkPoolCounters = true;
        out.uplinkLargePoolUsedBlocks = typedReadU32Le(p + 192);
        out.uplinkLargePoolCapacityBlocks = typedReadU32Le(p + 196);
        out.uplinkLargePoolCanReserveUsedBlocks = typedReadU32Le(p + 200);
        out.canTruthDescriptorQueueHighWater = typedReadU32Le(p + 204);
        out.uplinkPoolAllocFailTotal = typedReadU32Le(p + 208);
        out.canTruthPoolAllocFailTotal = typedReadU32Le(p + 212);
        out.uplinkDescriptorHighWaterTotal = typedReadU32Le(p + 216);
        out.diagnosticSuppressedTotal = typedReadU32Le(p + 220);
    }
    return out;
}

std::optional<TypedCapabilityRecord> decodeTypedCapability(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::Capability)) return std::nullopt;
    const QByteArray payload = typedPayloadView(record);
    if (payload.size() < kTypedCapabilityPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(payload.constData());
    TypedCapabilityRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.protocolVersion = p[8];
    out.profileMajor = p[9];
    out.profileMinor = p[10];
    out.monoUnit = p[11];
    out.canQueueSize = typedReadU32Le(p + 12);
    out.encoderPpr = typedReadU32Le(p + 16);
    out.encoderFrequencyLimit = typedReadU32Le(p + 20);
    out.supportsCanRxRaw = p[24] != 0;
    out.supportsCanTxRaw = p[25] != 0;
    out.supportsEncEdgeRaw = p[26] != 0;
    out.supportsEncDerived = p[27] != 0;
    out.supportsAdcSample = p[28] != 0;
    out.supportsBoardHealth = p[29] != 0;
    out.supportsBoardEvent = p[30] != 0;
    out.adcChannels = p[31];
    out.adcResolutionBits = p[32];
    out.adcPeriodMs = p[33];
    out.laneCapabilityFlags = p[34];
    out.limitationFlags = p[35];
    if (payload.size() >= kTypedCapabilityV2PayloadSize) {
        out.busCount = p[36];
        out.busDescriptorSize = p[37];
        out.capabilityV2Flags = typedReadU16Le(p + 38);
        const quint8 boundedBusCount = std::min<quint8>(out.busCount, 2);
        if (out.busDescriptorSize >= kTypedCapabilityBusDescriptorSize) {
            out.buses.reserve(boundedBusCount);
            for (quint8 index = 0; index < boundedBusCount; ++index) {
                const qsizetype offset = 40 + qsizetype(index) * out.busDescriptorSize;
                if (payload.size() < offset + kTypedCapabilityBusDescriptorSize) break;
                TypedCapabilityBusDescriptor bus;
                bus.busId = p[offset + 0];
                bus.roleHint = p[offset + 1];
                bus.backend = p[offset + 2];
                bus.transceiver = p[offset + 3];
                bus.rxSupported = p[offset + 4] != 0;
                bus.txSupported = p[offset + 5] != 0;
                bus.controlTxAllowed = p[offset + 6] != 0;
                bus.classicCanSupported = p[offset + 7] != 0;
                bus.canFdSupported = p[offset + 8] != 0;
                bus.maxLiveDlc = p[offset + 9];
                bus.nominalBitrate = typedReadU32Le(p + offset + 10);
                bus.dataBitrate = typedReadU32Le(p + offset + 14);
                bus.terminationPolicy = p[offset + 18];
                bus.isolationPolicy = p[offset + 19];
                out.buses.push_back(bus);
            }
        }
    }
    if (payload.size() >= kTypedCapabilityV3PayloadSize) {
        out.supportedUplinkRecords = typedReadU32Le(p + 80);
        out.supportedDownlinkRecords = typedReadU32Le(p + 84);
        out.safetyFeatureFlags = typedReadU32Le(p + 88);
        out.policyHash = typedReadU32Le(p + 92);
        out.firmwareBuildId = typedReadU32Le(p + 96);
        out.hostTxQueueSize = typedReadU16Le(p + 100);
        out.capabilityV3Flags = typedReadU16Le(p + 102);
        out.supportsCanRxRaw =
            (out.supportedUplinkRecords & (1u << static_cast<quint8>(TypedRecordType::CanRxRaw))) != 0 ||
            (out.supportedUplinkRecords & (1u << static_cast<quint8>(TypedRecordType::CanRxSegment))) != 0;
        out.supportsCanTxRaw =
            (out.supportedUplinkRecords & (1u << static_cast<quint8>(TypedRecordType::CanTxRaw))) != 0;
        out.supportsBoardHealth =
            (out.supportedUplinkRecords & (1u << static_cast<quint8>(TypedRecordType::BoardHealth))) != 0;
        out.supportsBoardEvent =
            (out.supportedUplinkRecords & (1u << static_cast<quint8>(TypedRecordType::BoardEvent))) != 0;
    }
    if (payload.size() >= kTypedCapabilityV4PayloadSize) {
        out.hasFirmwareIdentity = true;
        out.firmwareIdentityVersion = p[112];
        out.firmwareDirty = p[113] != 0;
        out.firmwareIrqMode = p[114];
        out.firmwareBuildEpoch = typedReadU32Le(p + 116);
        out.firmwareBuildId = typedReadU32Le(p + 120);
        out.mcpSpiHz = typedReadU32Le(p + 124);
        out.canRecordDrainBudget = typedReadU16Le(p + 128);
        out.serialRingKiB = typedReadU16Le(p + 130);
        out.firmwareGitSha = QString::fromLatin1(reinterpret_cast<const char*>(p + 132), 12).trimmed();
        out.firmwareEnvName = QString::fromLatin1(reinterpret_cast<const char*>(p + 144), 48).trimmed();
    }
    if (payload.size() >= kTypedCapabilityV5PayloadSize) {
        out.hasPassivePolicy = true;
        out.firmwareProfile = p[192];
        out.profileLockState = p[193];
        out.vehicleImpactState = p[194];
        out.hostCommandRx = p[195] != 0;
        out.controlPath = p[196] != 0;
        out.usbBackpressureIsolated = p[197] != 0;
        out.dtrResetSensitive = p[198] != 0;
        out.passiveAcceptanceAllowed = p[199] != 0;
        out.hardwareSafetyCaseId = typedReadU32Le(p + 200);
        out.benchVerificationId = typedReadU32Le(p + 204);
        for (int index = 0; index < 2; ++index) {
            const qsizetype offset = 208 + qsizetype(index) * 4;
            out.busMode[index] = p[offset + 0];
            out.busAckCapable[index] = p[offset + 1] != 0;
            out.busErrorFrameCapable[index] = p[offset + 2] != 0;
            out.busTransceiverResetSafe[index] = p[offset + 3] != 0;
        }
        out.usbCdcDtrSessionRequired = p[216] != 0;
        out.usbCdcDtrSessionOnly = p[217] != 0;
    }
    return out;
}
