#include "TypedRecords.h"

#include <cstring>

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
    case TypedRecordType::Unknown:
        break;
    }
    return QStringLiteral("TYPE_%1").arg(recordType);
}

quint64 typedRecordMonoUs(const TypedRecord& record) {
    if (record.payload.size() < 8) return 0;
    return typedReadU64Le(reinterpret_cast<const quint8*>(record.payload.constData()));
}

std::optional<TypedCanRawRecord> decodeTypedCanRaw(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::CanRxRaw) && !record.isType(TypedRecordType::CanTxRaw)) {
        return std::nullopt;
    }
    if (record.payload.size() < kTypedCanRawPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
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

std::optional<TypedAdcSampleRecord> decodeTypedAdcSample(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::AdcSample)) return std::nullopt;
    if (record.payload.size() < kTypedAdcSamplePayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
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
    if (record.payload.size() < kTypedControlAckPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
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
    if (record.payload.size() < kTypedBoardEventPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
    TypedBoardEventRecord out;
    out.monoUs = typedReadU64Le(p + 0);
    out.code = typedReadU16Le(p + 8);
    out.detail = typedReadU16Le(p + 10);
    out.counter = typedReadU32Le(p + 12);
    return out;
}

std::optional<TypedBoardHealthRecord> decodeTypedBoardHealth(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::BoardHealth)) return std::nullopt;
    if (record.payload.size() < kTypedBoardHealthPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
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
    return out;
}

std::optional<TypedCapabilityRecord> decodeTypedCapability(const TypedRecord& record) {
    if (!record.isType(TypedRecordType::Capability)) return std::nullopt;
    if (record.payload.size() < kTypedCapabilityPayloadSize) return std::nullopt;

    const auto* p = reinterpret_cast<const quint8*>(record.payload.constData());
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
    if (record.payload.size() >= kTypedCapabilityV2PayloadSize) {
        out.busCount = p[36];
        out.busDescriptorSize = p[37];
        out.capabilityV2Flags = typedReadU16Le(p + 38);
        const quint8 boundedBusCount = std::min<quint8>(out.busCount, 2);
        if (out.busDescriptorSize >= kTypedCapabilityBusDescriptorSize) {
            out.buses.reserve(boundedBusCount);
            for (quint8 index = 0; index < boundedBusCount; ++index) {
                const qsizetype offset = 40 + qsizetype(index) * out.busDescriptorSize;
                if (record.payload.size() < offset + kTypedCapabilityBusDescriptorSize) break;
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
    if (record.payload.size() >= kTypedCapabilityV3PayloadSize) {
        out.supportedUplinkRecords = typedReadU32Le(p + 80);
        out.supportedDownlinkRecords = typedReadU32Le(p + 84);
        out.safetyFeatureFlags = typedReadU32Le(p + 88);
        out.policyHash = typedReadU32Le(p + 92);
        out.firmwareBuildId = typedReadU32Le(p + 96);
        out.hostTxQueueSize = typedReadU16Le(p + 100);
        out.capabilityV3Flags = typedReadU16Le(p + 102);
    }
    return out;
}
