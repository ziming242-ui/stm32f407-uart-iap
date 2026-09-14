#ifndef IAP_PROTOCOL_H
#define IAP_PROTOCOL_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include "iap_config.h"

namespace IapProtocol {

constexpr quint16 CommandHello = static_cast<quint16>(IAP_CMD_HELLO);
constexpr quint16 CommandHold = static_cast<quint16>(IAP_CMD_HOLD);
constexpr quint16 CommandStart = static_cast<quint16>(IAP_CMD_START);
constexpr quint16 CommandData = static_cast<quint16>(IAP_CMD_DATA);
constexpr quint16 CommandEnd = static_cast<quint16>(IAP_CMD_END);
constexpr quint16 CommandAbort = static_cast<quint16>(IAP_CMD_ABORT);
constexpr quint16 CommandStatus = static_cast<quint16>(IAP_CMD_STATUS);
constexpr quint16 CommandBoot = static_cast<quint16>(IAP_CMD_BOOT);
constexpr quint16 CommandAck = static_cast<quint16>(IAP_CMD_ACK);
constexpr quint16 CommandNack = static_cast<quint16>(IAP_CMD_NACK);
constexpr quint8 SlotAuto = static_cast<quint8>(IAP_SLOT_AUTO);

constexpr int FrameOverhead = static_cast<int>(IAP_FRAME_OVERHEAD);
constexpr int FrameDataMax = static_cast<int>(IAP_FRAME_DATA_MAX);
constexpr int FrameWireMax = static_cast<int>(IAP_FRAME_WIRE_MAX);
constexpr int AckDataSize = 10;
constexpr int FirmwareChunkMax = static_cast<int>(IAP_DATA_BYTES);
constexpr quint32 FirmwareSizeMax = static_cast<quint32>(IAP_RUN_SIZE);

struct Frame {
    quint16 command = 0u;
    QByteArray data;
};

struct Reply {
    quint16 command = 0u;
    quint32 nextSequence = 0u;
    quint32 expectedOffset = 0u;
    quint16 error = 0u;
};

enum class ParseResult {
    NeedMore,
    FrameReady,
    DroppedByte
};

quint16 crc16CcittFalse(const QByteArray &bytes);
quint32 crc32Ieee(const QByteArray &bytes);
QByteArray encodeFrame(quint16 command, const QByteArray &data);
ParseResult takeFrame(QByteArray &buffer, Frame &frame, QString &errorText);
bool decodeReply(const Frame &frame, Reply &reply, QString &errorText);
void appendU32Be(QByteArray &bytes, quint32 value);
QString commandName(quint16 command);
QString errorName(quint16 error);

} // namespace IapProtocol

#endif
