#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QVector>
#include <QString>
#include <QtGlobal>
#include <optional>

inline constexpr quint8 kTypedTransportSof0 = 0xA5;
inline constexpr quint8 kTypedTransportSof1 = 0x5A;
inline constexpr quint8 kTypedTransportVersion = 1;
inline constexpr qsizetype kTypedTransportHeaderSize = 7;
inline constexpr qsizetype kTypedTransportFrameOverhead = 2 + kTypedTransportHeaderSize + 2;
inline constexpr quint16 kTypedTransportMaxPayloadLength = 4096;
inline constexpr qsizetype kTypedCanRawPayloadSize = 30;
inline constexpr qsizetype kTypedAdcSamplePayloadSize = 44;
inline constexpr qsizetype kTypedControlAckPayloadSize = 28;
inline constexpr qsizetype kTypedBoardEventPayloadSize = 16;
inline constexpr qsizetype kTypedBoardHealthPayloadSize = 52;
inline constexpr qsizetype kTypedCapabilityPayloadSize = 36;
inline constexpr qsizetype kTypedCapabilityV2PayloadSize = 80;
inline constexpr qsizetype kTypedCapabilityV3PayloadSize = 112;
inline constexpr qsizetype kTypedCapabilityBusDescriptorSize = 20;
inline constexpr qsizetype kTypedHostCanTxRequestPayloadSize = 19;
inline constexpr qsizetype kTypedHostHeartbeatPayloadSize = 12;
inline constexpr qsizetype kTypedHostControlSessionPayloadSize = 24;

enum class TypedRecordType : quint8 {
    Unknown = 0,
    CanRxRaw = 1,
    CanTxRaw = 2,
    EncEdgeRaw = 3,
    EncDerived = 4,
    AdcSample = 5,
    ControlAck = 6,
    BoardEvent = 7,
    BoardHealth = 8,
    Capability = 9,
    HostCanTxRequest = 10,
    HostHeartbeat = 11,
    HostControlSession = 12
};

struct TypedFrameHeader {
    quint8 version = 0;
    quint8 recordType = 0;
    quint8 flags = 0;
    quint16 seq = 0;
    quint16 payloadLength = 0;

    TypedRecordType type() const { return static_cast<TypedRecordType>(recordType); }
};

struct TypedRecord {
    TypedFrameHeader header;
    QByteArray payload;
    QByteArray frameBytes;

    bool isType(TypedRecordType type) const { return header.recordType == static_cast<quint8>(type); }
};

using TypedRecordList = QVector<TypedRecord>;

struct TypedCanRawRecord {
    bool txAudit = false;
    quint64 monoUs = 0;
    quint32 canIdFlags = 0;
    quint32 canId = 0;
    bool extended = false;
    bool rtr = false;
    quint8 dlc = 0;
    quint8 bus = 0;
    quint8 data[8] = {0};
    quint32 total = 0;
    quint32 droppedOrFailed = 0;
};

struct TypedAdcSampleRecord {
    quint64 monoUs = 0;
    quint32 sampleTotal = 0;
    quint32 droppedTotal = 0;
    quint8 sourceId = 0;
    quint8 channelCount = 0;
    quint8 resolutionBits = 0;
    quint8 flags = 0;
    quint8 channelId[8] = {0};
    quint16 raw[8] = {0};
};

struct TypedControlAckRecord {
    quint64 monoUs = 0;
    quint32 commandId = 0;
    quint8 status = 0;
    quint8 reason = 0;
    quint8 targetBus = 0;
    quint8 targetDlcFlags = 0;
    quint32 targetCanIdFlags = 0;
    quint32 targetCanId = 0;
    bool targetExtended = false;
    bool targetRtr = false;
    quint32 counter = 0;
    quint32 rejectedTotal = 0;
};

struct TypedBoardEventRecord {
    quint64 monoUs = 0;
    quint16 code = 0;
    quint16 detail = 0;
    quint32 counter = 0;
};

struct TypedBoardHealthRecord {
    quint64 monoUs = 0;
    quint32 canRxTotal = 0;
    quint32 canDroppedTotal = 0;
    quint32 canFifoOverflowTotal = 0;
    quint32 serialRecordTxTotal = 0;
    quint32 queueDepth = 0;
    quint32 encoderFaultEvents = 0;
    quint32 encoderWrapEvents = 0;
    qint64 encoderPosition = 0;
    quint8 safetyState = 0;
    quint8 inputs = 0;
    quint8 encoderTimerOk = 0;
    quint8 flags = 0;
    quint32 faultFlags = 0;
};

struct TypedCapabilityBusDescriptor {
    quint8 busId = 0;
    quint8 roleHint = 0;
    quint8 backend = 0;
    quint8 transceiver = 0;
    bool rxSupported = false;
    bool txSupported = false;
    bool controlTxAllowed = false;
    bool classicCanSupported = false;
    bool canFdSupported = false;
    quint8 maxLiveDlc = 0;
    quint32 nominalBitrate = 0;
    quint32 dataBitrate = 0;
    quint8 terminationPolicy = 0;
    quint8 isolationPolicy = 0;
};

struct TypedCapabilityRecord {
    quint64 monoUs = 0;
    quint8 protocolVersion = 0;
    quint8 profileMajor = 0;
    quint8 profileMinor = 0;
    quint8 monoUnit = 0;
    quint32 canQueueSize = 0;
    quint32 encoderPpr = 0;
    quint32 encoderFrequencyLimit = 0;
    bool supportsCanRxRaw = false;
    bool supportsCanTxRaw = false;
    bool supportsEncEdgeRaw = false;
    bool supportsEncDerived = false;
    bool supportsAdcSample = false;
    bool supportsBoardHealth = false;
    bool supportsBoardEvent = false;
    quint8 adcChannels = 0;
    quint8 adcResolutionBits = 0;
    quint8 adcPeriodMs = 0;
    quint8 laneCapabilityFlags = 0;
    quint8 limitationFlags = 0;
    quint8 busCount = 0;
    quint8 busDescriptorSize = 0;
    quint16 capabilityV2Flags = 0;
    QVector<TypedCapabilityBusDescriptor> buses;
    quint32 supportedUplinkRecords = 0;
    quint32 supportedDownlinkRecords = 0;
    quint32 safetyFeatureFlags = 0;
    quint32 policyHash = 0;
    quint32 firmwareBuildId = 0;
    quint16 hostTxQueueSize = 0;
    quint16 capabilityV3Flags = 0;
};

quint16 typedReadU16Le(const quint8* p);
quint32 typedReadU32Le(const quint8* p);
quint64 typedReadU64Le(const quint8* p);
qint64 typedReadI64Le(const quint8* p);

QString typedRecordTypeName(quint8 recordType);
quint64 typedRecordMonoUs(const TypedRecord& record);

std::optional<TypedCanRawRecord> decodeTypedCanRaw(const TypedRecord& record);
std::optional<TypedAdcSampleRecord> decodeTypedAdcSample(const TypedRecord& record);
std::optional<TypedControlAckRecord> decodeTypedControlAck(const TypedRecord& record);
std::optional<TypedBoardEventRecord> decodeTypedBoardEvent(const TypedRecord& record);
std::optional<TypedBoardHealthRecord> decodeTypedBoardHealth(const TypedRecord& record);
std::optional<TypedCapabilityRecord> decodeTypedCapability(const TypedRecord& record);

Q_DECLARE_METATYPE(TypedRecord)
Q_DECLARE_METATYPE(TypedRecordList)
