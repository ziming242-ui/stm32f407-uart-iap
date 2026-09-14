#ifndef FIRMWARE_UPDATER_H
#define FIRMWARE_UPDATER_H

#include <QObject>
#include <QByteArray>
#include <QSerialPort>
#include <QString>
#include <QTimer>

class FirmwareUpdater final : public QObject
{
    Q_OBJECT

public:
    explicit FirmwareUpdater(QObject *parent = nullptr);

    bool connectPort(const QString &portName);
    void disconnectPort();
    bool isConnected() const;
    bool isBusy() const;
    bool hasFirmware() const;
    bool loadFirmware(const QString &path);
    bool startUpgrade(quint32 version);
    void cancelUpgrade();

signals:
    void logMessage(const QString &message);
    void connectionChanged(bool connected, const QString &portName);
    void firmwareLoaded(const QString &path, quint32 size, quint32 crc32);
    void busyChanged(bool busy);
    void progressChanged(int percent);
    void upgradeFinished(bool success, const QString &message);

private slots:
    void onReadyRead();
    void onTimeout();
    void onSerialError(QSerialPort::SerialPortError error);

private:
    enum class Step {
        Idle,
        WaitHold,
        WaitStart,
        WaitData,
        WaitEnd,
        WaitBoot
    };

    void sendHold();
    void sendStart();
    void sendNextDataOrEnd();
    void sendBoot();
    bool sendAndWait(quint16 command, const QByteArray &data, Step step);
    bool writePendingFrame(const QString &reason);
    void handleReply(quint16 command, quint32 nextSequence,
                     quint32 expectedOffset, quint16 error);
    void finish(bool success, const QString &message);
    static constexpr int AckTimeoutMs = 1500;
    static constexpr int MaxTimeoutRetries = 3;

    QSerialPort serial_;
    QTimer timeoutTimer_;
    QByteArray receiveBuffer_;
    QByteArray firmware_;
    QString firmwarePath_;
    quint32 firmwareCrc32_ = 0u;
    quint32 version_ = 0u;
    quint32 nextSequence_ = 0u;
    quint32 nextOffset_ = 0u;
    quint32 pendingAckSequence_ = 0u;
    quint32 pendingAckOffset_ = 0u;
    QByteArray pendingWire_;
    quint16 pendingCommand_ = 0u;
    int timeoutRetries_ = 0;
    Step step_ = Step::Idle;
};

#endif
