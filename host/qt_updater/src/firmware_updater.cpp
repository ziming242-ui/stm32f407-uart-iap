#include "firmware_updater.h"

#include "iap_protocol.h"

#include <QFile>

namespace {

quint32 readU32Le(const char *bytes)
{
    return static_cast<quint8>(bytes[0]) |
           (static_cast<quint32>(static_cast<quint8>(bytes[1])) << 8u) |
           (static_cast<quint32>(static_cast<quint8>(bytes[2])) << 16u) |
           (static_cast<quint32>(static_cast<quint8>(bytes[3])) << 24u);
}

} // namespace

FirmwareUpdater::FirmwareUpdater(QObject *parent)
    : QObject(parent)
{
    timeoutTimer_.setSingleShot(true);
    connect(&timeoutTimer_, &QTimer::timeout, this, &FirmwareUpdater::onTimeout);
    connect(&serial_, &QSerialPort::readyRead, this, &FirmwareUpdater::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred,
            this, &FirmwareUpdater::onSerialError);
}

bool FirmwareUpdater::connectPort(const QString &portName)
{
    if (isBusy()) {
        emit logMessage(QStringLiteral("升级过程中不能切换串口"));
        return false;
    }
    if (serial_.isOpen()) {
        disconnectPort();
    }

    serial_.setPortName(portName);
    serial_.setBaudRate(QSerialPort::Baud115200);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial_.open(QIODevice::ReadWrite)) {
        emit logMessage(QStringLiteral("打开串口 %1 失败：%2")
                        .arg(portName, serial_.errorString()));
        return false;
    }

    receiveBuffer_.clear();
    emit logMessage(QStringLiteral("已连接 %1，115200 8-N-1")
                    .arg(portName));
    emit connectionChanged(true, portName);
    return true;
}

void FirmwareUpdater::disconnectPort()
{
    if (isBusy()) {
        cancelUpgrade();
    }
    const QString oldPort = serial_.portName();
    if (serial_.isOpen()) {
        serial_.close();
    }
    receiveBuffer_.clear();
    emit connectionChanged(false, oldPort);
    emit logMessage(QStringLiteral("串口已断开"));
}

bool FirmwareUpdater::isConnected() const
{
    return serial_.isOpen();
}

bool FirmwareUpdater::isBusy() const
{
    return step_ != Step::Idle;
}

bool FirmwareUpdater::hasFirmware() const
{
    return !firmware_.isEmpty();
}

bool FirmwareUpdater::loadFirmware(const QString &path)
{
    if (isBusy()) {
        emit logMessage(QStringLiteral("升级过程中不能更换固件"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage(QStringLiteral("读取固件失败：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 8 ||
        static_cast<quint64>(bytes.size()) > IapProtocol::FirmwareSizeMax) {
        emit logMessage(QStringLiteral("固件大小必须为 8～262144 字节，当前为 %1 字节")
                        .arg(bytes.size()));
        return false;
    }

    const quint32 initialMsp = readU32Le(bytes.constData());
    const quint32 resetHandler = readU32Le(bytes.constData() + 4);
    const quint32 resetCode = resetHandler & ~1u;
    const quint32 imageEnd = static_cast<quint32>(IAP_RUN_BASE) +
                             static_cast<quint32>(bytes.size());
    if (initialMsp < 0x20000000u || initialMsp > 0x20020000u ||
        (initialMsp & 7u) != 0u) {
        emit logMessage(QStringLiteral("拒绝固件：初始 MSP 0x%1 不在主 SRAM 范围或未按 8 字节对齐")
                        .arg(initialMsp, 8, 16, QLatin1Char('0')).toUpper());
        return false;
    }
    if ((resetHandler & 1u) == 0u || resetCode < IAP_RUN_BASE ||
        resetCode >= imageEnd) {
        emit logMessage(QStringLiteral("拒绝固件：Reset_Handler 0x%1 不是指向 0x08040000 镜像的 Thumb 入口")
                        .arg(resetHandler, 8, 16, QLatin1Char('0')).toUpper());
        return false;
    }

    firmware_ = bytes;
    firmwarePath_ = path;
    firmwareCrc32_ = IapProtocol::crc32Ieee(firmware_);
    emit firmwareLoaded(path, static_cast<quint32>(firmware_.size()),
                        firmwareCrc32_);
    emit logMessage(QStringLiteral("已加载固件：%1 字节，CRC32/IEEE=0x%2")
                    .arg(firmware_.size())
                    .arg(firmwareCrc32_, 8, 16, QLatin1Char('0')).toUpper());
    emit logMessage(QStringLiteral("向量检查：MSP=0x%1，Reset_Handler=0x%2")
                    .arg(initialMsp, 8, 16, QLatin1Char('0'))
                    .arg(resetHandler, 8, 16, QLatin1Char('0')).toUpper());
    return true;
}

bool FirmwareUpdater::startUpgrade(quint32 version)
{
    if (!serial_.isOpen()) {
        emit logMessage(QStringLiteral("请先连接串口"));
        return false;
    }
    if (firmware_.isEmpty()) {
        emit logMessage(QStringLiteral("请先选择 .bin 固件"));
        return false;
    }
    if (version == 0u) {
        emit logMessage(QStringLiteral("版本号必须为 1～4294967295"));
        return false;
    }
    if (isBusy()) {
        emit logMessage(QStringLiteral("已有升级正在进行"));
        return false;
    }

    version_ = version;
    nextSequence_ = 0u;
    nextOffset_ = 0u;
    receiveBuffer_.clear();
    emit progressChanged(0);
    emit logMessage(QStringLiteral("开始升级：自动选择目标槽，版本=%1，大小=%2")
                    .arg(version_).arg(firmware_.size()));
    sendHold();
    if (isBusy()) {
        emit busyChanged(true);
    }
    return isBusy();
}

void FirmwareUpdater::cancelUpgrade()
{
    if (!isBusy()) {
        return;
    }

    timeoutTimer_.stop();
    if (serial_.isOpen()) {
        const QByteArray abortFrame =
                IapProtocol::encodeFrame(IapProtocol::CommandAbort, {});
        serial_.write(abortFrame);
        emit logMessage(QStringLiteral("已发送 ABORT；本机停止等待"));
    }
    finish(false, QStringLiteral("升级已由用户取消"));
}

void FirmwareUpdater::sendHold()
{
    sendAndWait(IapProtocol::CommandHold, {}, Step::WaitHold);
}

void FirmwareUpdater::sendStart()
{
    QByteArray data;
    data.reserve(13);
    data.append(static_cast<char>(IapProtocol::SlotAuto));
    IapProtocol::appendU32Be(data, version_);
    IapProtocol::appendU32Be(data, static_cast<quint32>(firmware_.size()));
    IapProtocol::appendU32Be(data, firmwareCrc32_);
    sendAndWait(IapProtocol::CommandStart, data, Step::WaitStart);
}

void FirmwareUpdater::sendNextDataOrEnd()
{
    if (nextOffset_ >= static_cast<quint32>(firmware_.size())) {
        sendAndWait(IapProtocol::CommandEnd, {}, Step::WaitEnd);
        return;
    }

    const int remaining = firmware_.size() - static_cast<int>(nextOffset_);
    const int amount = qMin(remaining, IapProtocol::FirmwareChunkMax);
    QByteArray data;
    data.reserve(8 + amount);
    IapProtocol::appendU32Be(data, nextSequence_);
    IapProtocol::appendU32Be(data, nextOffset_);
    data.append(firmware_.constData() + static_cast<int>(nextOffset_), amount);

    pendingAckSequence_ = nextSequence_ + 1u;
    pendingAckOffset_ = nextOffset_ + static_cast<quint32>(amount);
    sendAndWait(IapProtocol::CommandData, data, Step::WaitData);
}

void FirmwareUpdater::sendBoot()
{
    sendAndWait(IapProtocol::CommandBoot, {}, Step::WaitBoot);
}

bool FirmwareUpdater::sendAndWait(quint16 command, const QByteArray &data, Step step)
{
    pendingWire_ = IapProtocol::encodeFrame(command, data);
    if (pendingWire_.isEmpty()) {
        finish(false, QStringLiteral("无法编码 %1 帧")
               .arg(IapProtocol::commandName(command)));
        return false;
    }
    pendingCommand_ = command;
    timeoutRetries_ = 0;
    step_ = step;
    return writePendingFrame(QStringLiteral("发送"));
}

bool FirmwareUpdater::writePendingFrame(const QString &reason)
{
    if (!serial_.isOpen()) {
        finish(false, QStringLiteral("串口未连接"));
        return false;
    }
    const qint64 accepted = serial_.write(pendingWire_);
    if (accepted != pendingWire_.size()) {
        finish(false, QStringLiteral("串口写入失败：%1").arg(serial_.errorString()));
        return false;
    }
    timeoutTimer_.start(AckTimeoutMs);
    emit logMessage(QStringLiteral("%1 %2，等待 ACK（%3 字节）")
                    .arg(reason, IapProtocol::commandName(pendingCommand_))
                    .arg(pendingWire_.size()));
    return true;
}

void FirmwareUpdater::onReadyRead()
{
    const QByteArray incoming = serial_.readAll();
    if (!isBusy()) {
        const QString text = QString::fromUtf8(incoming).trimmed();
        if (!text.isEmpty()) {
            emit logMessage(QStringLiteral("APP 串口：%1").arg(text));
        }
        return;
    }

    receiveBuffer_.append(incoming);
    for (;;) {
        if (!isBusy()) {
            const QString text = QString::fromUtf8(receiveBuffer_).trimmed();
            receiveBuffer_.clear();
            if (!text.isEmpty()) {
                emit logMessage(QStringLiteral("APP 串口：%1").arg(text));
            }
            break;
        }
        IapProtocol::Frame frame;
        QString parseError;
        const auto result = IapProtocol::takeFrame(receiveBuffer_, frame, parseError);
        if (result == IapProtocol::ParseResult::NeedMore) {
            break;
        }
        if (result == IapProtocol::ParseResult::DroppedByte) {
            emit logMessage(parseError);
            continue;
        }

        IapProtocol::Reply reply;
        if (!IapProtocol::decodeReply(frame, reply, parseError)) {
            emit logMessage(parseError);
            continue;
        }
        handleReply(reply.command, reply.nextSequence,
                    reply.expectedOffset, reply.error);
    }
}

void FirmwareUpdater::handleReply(quint16 command, quint32 nextSequence,
                                  quint32 expectedOffset, quint16 error)
{
    if (!isBusy()) {
        emit logMessage(QStringLiteral("忽略空闲状态下的 %1")
                        .arg(IapProtocol::commandName(command)));
        return;
    }

    timeoutTimer_.stop();
    emit logMessage(QStringLiteral("收到 %1：next_seq=%2 offset=%3 error=%4")
                    .arg(IapProtocol::commandName(command))
                    .arg(nextSequence)
                    .arg(expectedOffset)
                    .arg(IapProtocol::errorName(error)));

    if (command != IapProtocol::CommandAck || error != 0u) {
        finish(false, QStringLiteral("设备拒绝 %1：%2")
               .arg(IapProtocol::commandName(pendingCommand_),
                    IapProtocol::errorName(error)));
        return;
    }

    switch (step_) {
    case Step::WaitHold:
        sendStart();
        break;
    case Step::WaitStart:
        nextSequence_ = 0u;
        nextOffset_ = 0u;
        sendNextDataOrEnd();
        break;
    case Step::WaitData:
        if (nextSequence != pendingAckSequence_ ||
            expectedOffset != pendingAckOffset_) {
            finish(false, QStringLiteral("DATA ACK 与预期不一致：预期 seq=%1 offset=%2")
                   .arg(pendingAckSequence_).arg(pendingAckOffset_));
            return;
        }
        nextSequence_ = nextSequence;
        nextOffset_ = expectedOffset;
        emit progressChanged(static_cast<int>(
                (static_cast<quint64>(nextOffset_) * 100u) /
                static_cast<quint64>(firmware_.size())));
        sendNextDataOrEnd();
        break;
    case Step::WaitEnd:
        sendBoot();
        break;
    case Step::WaitBoot:
        emit progressChanged(100);
        finish(true, QStringLiteral("设备已确认 BOOT，升级传输完成"));
        break;
    case Step::Idle:
        break;
    }
}

void FirmwareUpdater::onTimeout()
{
    if (!isBusy()) {
        return;
    }
    if (timeoutRetries_ >= MaxTimeoutRetries) {
        finish(false, QStringLiteral("等待 %1 ACK 超时，已重发 %2 次")
               .arg(IapProtocol::commandName(pendingCommand_))
               .arg(MaxTimeoutRetries));
        return;
    }

    ++timeoutRetries_;
    writePendingFrame(QStringLiteral("超时重发 %1/%2")
                      .arg(timeoutRetries_).arg(MaxTimeoutRetries));
}

void FirmwareUpdater::onSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError) {
        return;
    }
    if (error == QSerialPort::ResourceError ||
        error == QSerialPort::DeviceNotFoundError ||
        error == QSerialPort::PermissionError) {
        const QString message = QStringLiteral("串口错误：%1").arg(serial_.errorString());
        if (isBusy()) {
            finish(false, message);
        }
        if (serial_.isOpen()) {
            serial_.close();
        }
        emit connectionChanged(false, serial_.portName());
        emit logMessage(message);
    }
}

void FirmwareUpdater::finish(bool success, const QString &message)
{
    const bool wasBusy = isBusy();
    timeoutTimer_.stop();
    pendingWire_.clear();
    pendingCommand_ = 0u;
    timeoutRetries_ = 0;
    step_ = Step::Idle;
    if (wasBusy) {
        emit busyChanged(false);
    }
    emit logMessage(message);
    emit upgradeFinished(success, message);
}
