#include "iap_protocol.h"

namespace {

quint16 readU16Be(const char *bytes)
{
    return (static_cast<quint16>(static_cast<quint8>(bytes[0])) << 8u) |
           static_cast<quint8>(bytes[1]);
}

quint32 readU32Be(const char *bytes)
{
    return (static_cast<quint32>(static_cast<quint8>(bytes[0])) << 24u) |
           (static_cast<quint32>(static_cast<quint8>(bytes[1])) << 16u) |
           (static_cast<quint32>(static_cast<quint8>(bytes[2])) << 8u) |
           static_cast<quint8>(bytes[3]);
}

void appendU16Be(QByteArray &bytes, quint16 value)
{
    bytes.append(static_cast<char>((value >> 8u) & 0xFFu));
    bytes.append(static_cast<char>(value & 0xFFu));
}

bool isKnownCommand(quint16 command)
{
    switch (command) {
    case IapProtocol::CommandHello:
    case IapProtocol::CommandHold:
    case IapProtocol::CommandStart:
    case IapProtocol::CommandData:
    case IapProtocol::CommandEnd:
    case IapProtocol::CommandAbort:
    case IapProtocol::CommandStatus:
    case IapProtocol::CommandBoot:
    case IapProtocol::CommandAck:
    case IapProtocol::CommandNack:
        return true;
    default:
        return false;
    }
}

} // namespace

namespace IapProtocol {

quint16 crc16CcittFalse(const QByteArray &bytes)
{
    quint16 crc = 0xFFFFu;
    for (const char value : bytes) {
        crc ^= static_cast<quint16>(static_cast<quint8>(value)) << 8u;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0u
                    ? static_cast<quint16>((crc << 1u) ^ 0x1021u)
                    : static_cast<quint16>(crc << 1u);
        }
    }
    return crc;
}

quint32 crc32Ieee(const QByteArray &bytes)
{
    quint32 crc = 0xFFFFFFFFu;
    for (const char value : bytes) {
        crc ^= static_cast<quint8>(value);
        for (int bit = 0; bit < 8; ++bit) {
            const quint32 mask = (crc & 1u) != 0u ? 0xFFFFFFFFu : 0u;
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendU32Be(QByteArray &bytes, quint32 value)
{
    bytes.append(static_cast<char>((value >> 24u) & 0xFFu));
    bytes.append(static_cast<char>((value >> 16u) & 0xFFu));
    bytes.append(static_cast<char>((value >> 8u) & 0xFFu));
    bytes.append(static_cast<char>(value & 0xFFu));
}

QByteArray encodeFrame(quint16 command, const QByteArray &data)
{
    if (data.size() > FrameDataMax) {
        return {};
    }

    QByteArray wire;
    wire.reserve(FrameOverhead + data.size());
    appendU16Be(wire, command);
    appendU16Be(wire, static_cast<quint16>(FrameOverhead + data.size()));
    wire.append(data);
    appendU32Be(wire, 0u);
    appendU16Be(wire, crc16CcittFalse(wire));
    return wire;
}

ParseResult takeFrame(QByteArray &buffer, Frame &frame, QString &errorText)
{
    errorText.clear();
    if (buffer.size() < 4) {
        return ParseResult::NeedMore;
    }

    const quint16 command = readU16Be(buffer.constData());
    const quint16 totalLength = readU16Be(buffer.constData() + 2);
    if (!isKnownCommand(command) || totalLength < FrameOverhead ||
        totalLength > FrameWireMax) {
        buffer.remove(0, 1);
        errorText = QStringLiteral("无法识别帧头，丢弃 1 字节重新同步");
        return ParseResult::DroppedByte;
    }
    if (buffer.size() < totalLength) {
        return ParseResult::NeedMore;
    }

    const QByteArray wire = buffer.left(totalLength);
    const quint16 receivedCrc = readU16Be(wire.constData() + totalLength - 2);
    const quint16 calculatedCrc = crc16CcittFalse(wire.left(totalLength - 2));
    if (receivedCrc != calculatedCrc) {
        buffer.remove(0, 1);
        errorText = QStringLiteral("接收帧 CRC16 错误，丢弃 1 字节重新同步");
        return ParseResult::DroppedByte;
    }

    const int dataLength = totalLength - FrameOverhead;
    const quint32 reserved = readU32Be(wire.constData() + 4 + dataLength);
    buffer.remove(0, totalLength);
    if (reserved != 0u) {
        errorText = QStringLiteral("接收帧 reserved 字段不是 0");
        return ParseResult::DroppedByte;
    }

    frame.command = command;
    frame.data = wire.mid(4, dataLength);
    return ParseResult::FrameReady;
}

bool decodeReply(const Frame &frame, Reply &reply, QString &errorText)
{
    if ((frame.command != CommandAck && frame.command != CommandNack) ||
        frame.data.size() != AckDataSize) {
        errorText = QStringLiteral("响应不是合法的 ACK/NACK 帧");
        return false;
    }

    reply.command = frame.command;
    reply.nextSequence = readU32Be(frame.data.constData());
    reply.expectedOffset = readU32Be(frame.data.constData() + 4);
    reply.error = readU16Be(frame.data.constData() + 8);
    errorText.clear();
    return true;
}

QString commandName(quint16 command)
{
    switch (command) {
    case CommandHello: return QStringLiteral("HELLO");
    case CommandHold: return QStringLiteral("HOLD");
    case CommandStart: return QStringLiteral("START");
    case CommandData: return QStringLiteral("DATA");
    case CommandEnd: return QStringLiteral("END");
    case CommandAbort: return QStringLiteral("ABORT");
    case CommandStatus: return QStringLiteral("STATUS");
    case CommandBoot: return QStringLiteral("BOOT");
    case CommandAck: return QStringLiteral("ACK");
    case CommandNack: return QStringLiteral("NACK");
    default: return QStringLiteral("UNKNOWN");
    }
}

QString errorName(quint16 error)
{
    switch (error) {
    case IAP_ERR_OK: return QStringLiteral("OK");
    case IAP_ERR_FRAME_LENGTH: return QStringLiteral("FRAME_LENGTH");
    case IAP_ERR_FRAME_CRC: return QStringLiteral("FRAME_CRC");
    case IAP_ERR_RESERVED: return QStringLiteral("RESERVED");
    case IAP_ERR_COMMAND: return QStringLiteral("COMMAND");
    case IAP_ERR_STATE: return QStringLiteral("STATE");
    case IAP_ERR_TARGET: return QStringLiteral("TARGET");
    case IAP_ERR_IMAGE_SIZE: return QStringLiteral("IMAGE_SIZE");
    case IAP_ERR_SEQUENCE: return QStringLiteral("SEQUENCE");
    case IAP_ERR_OFFSET: return QStringLiteral("OFFSET");
    case IAP_ERR_IMAGE_CRC: return QStringLiteral("IMAGE_CRC");
    case IAP_ERR_NO_IMAGE: return QStringLiteral("NO_IMAGE");
    case IAP_ERR_VECTOR: return QStringLiteral("VECTOR");
    case IAP_ERR_FLASH: return QStringLiteral("FLASH");
    case IAP_ERR_META_FULL: return QStringLiteral("META_FULL");
    case IAP_ERR_TIMEOUT: return QStringLiteral("TIMEOUT");
    default: return QStringLiteral("UNKNOWN_ERROR");
    }
}

} // namespace IapProtocol
