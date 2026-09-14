#include "main_window.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSerialPortInfo>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("STM32F407 UART IAP 升级演示"));
    resize(860, 600);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);

    auto *serialRow = new QHBoxLayout;
    portCombo_ = new QComboBox(central);
    refreshButton_ = new QPushButton(QStringLiteral("刷新串口"), central);
    connectButton_ = new QPushButton(QStringLiteral("连接"), central);
    connectionLabel_ = new QLabel(QStringLiteral("未连接"), central);
    serialRow->addWidget(new QLabel(QStringLiteral("串口："), central));
    serialRow->addWidget(portCombo_, 1);
    serialRow->addWidget(refreshButton_);
    serialRow->addWidget(connectButton_);
    serialRow->addWidget(connectionLabel_);
    rootLayout->addLayout(serialRow);

    auto *fileRow = new QHBoxLayout;
    firmwarePathEdit_ = new QLineEdit(central);
    firmwarePathEdit_->setReadOnly(true);
    firmwarePathEdit_->setPlaceholderText(QStringLiteral("选择按 0x08040000 链接生成的 APP .bin"));
    chooseButton_ = new QPushButton(QStringLiteral("选择 .bin"), central);
    fileRow->addWidget(new QLabel(QStringLiteral("固件："), central));
    fileRow->addWidget(firmwarePathEdit_, 1);
    fileRow->addWidget(chooseButton_);
    rootLayout->addLayout(fileRow);

    auto *actionRow = new QHBoxLayout;
    versionEdit_ = new QLineEdit(QStringLiteral("1"), central);
    versionEdit_->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("[0-9]{1,10}")), versionEdit_));
    versionEdit_->setMaximumWidth(150);
    startButton_ = new QPushButton(QStringLiteral("开始升级"), central);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), central);
    actionRow->addWidget(new QLabel(QStringLiteral("版本号："), central));
    actionRow->addWidget(versionEdit_);
    actionRow->addStretch(1);
    actionRow->addWidget(startButton_);
    actionRow->addWidget(cancelButton_);
    rootLayout->addLayout(actionRow);

    progressBar_ = new QProgressBar(central);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    rootLayout->addWidget(progressBar_);

    logEdit_ = new QPlainTextEdit(central);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(2000);
    rootLayout->addWidget(new QLabel(QStringLiteral("升级日志："), central));
    rootLayout->addWidget(logEdit_, 1);
    setCentralWidget(central);

    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(chooseButton_, &QPushButton::clicked, this, &MainWindow::chooseFirmware);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startUpgrade);
    connect(cancelButton_, &QPushButton::clicked, &updater_, &FirmwareUpdater::cancelUpgrade);
    connect(&updater_, &FirmwareUpdater::logMessage, this, &MainWindow::appendLog);
    connect(&updater_, &FirmwareUpdater::progressChanged,
            progressBar_, &QProgressBar::setValue);
    connect(&updater_, &FirmwareUpdater::busyChanged,
            this, &MainWindow::updateControls);
    connect(&updater_, &FirmwareUpdater::connectionChanged,
            this, [this](bool connected, const QString &portName) {
        connectionLabel_->setText(connected
                ? QStringLiteral("已连接 %1").arg(portName)
                : QStringLiteral("未连接"));
        connectButton_->setText(connected ? QStringLiteral("断开")
                                          : QStringLiteral("连接"));
        updateControls();
    });
    connect(&updater_, &FirmwareUpdater::firmwareLoaded,
            this, [this](const QString &path, quint32, quint32) {
        firmwarePathEdit_->setText(path);
        updateControls();
    });

    refreshPorts();
    updateControls();
    appendLog(QStringLiteral("就绪。协议流程：HOLD → START → DATA → END → BOOT"));
}

void MainWindow::refreshPorts()
{
    const QString previous = portCombo_->currentData().toString();
    portCombo_->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString text = port.portName();
        if (!port.description().isEmpty()) {
            text += QStringLiteral(" — ") + port.description();
        }
        portCombo_->addItem(text, port.portName());
    }
    const int previousIndex = portCombo_->findData(previous);
    if (previousIndex >= 0) {
        portCombo_->setCurrentIndex(previousIndex);
    }
    appendLog(QStringLiteral("发现 %1 个串口").arg(ports.size()));
    updateControls();
}

void MainWindow::toggleConnection()
{
    if (updater_.isConnected()) {
        updater_.disconnectPort();
        return;
    }
    const QString portName = portCombo_->currentData().toString();
    if (portName.isEmpty()) {
        appendLog(QStringLiteral("没有可连接的串口"));
        return;
    }
    updater_.connectPort(portName);
}

void MainWindow::chooseFirmware()
{
    const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择 APP 固件"), QString(),
            QStringLiteral("Binary firmware (*.bin);;All files (*)"));
    if (!path.isEmpty()) {
        updater_.loadFirmware(path);
    }
}

void MainWindow::startUpgrade()
{
    bool ok = false;
    const qulonglong parsed = versionEdit_->text().toULongLong(&ok, 10);
    if (!ok || parsed == 0u || parsed > 0xFFFFFFFFull) {
        appendLog(QStringLiteral("版本号必须为 1～4294967295"));
        return;
    }
    updater_.startUpgrade(static_cast<quint32>(parsed));
}

void MainWindow::appendLog(const QString &message)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    logEdit_->appendPlainText(QStringLiteral("[%1] %2").arg(time, message));
}

void MainWindow::updateControls()
{
    const bool connected = updater_.isConnected();
    const bool busy = updater_.isBusy();
    portCombo_->setEnabled(!connected && !busy);
    refreshButton_->setEnabled(!connected && !busy);
    connectButton_->setEnabled(!busy && (connected || portCombo_->count() > 0));
    chooseButton_->setEnabled(!busy);
    versionEdit_->setEnabled(!busy);
    startButton_->setEnabled(connected && updater_.hasFirmware() && !busy);
    cancelButton_->setEnabled(busy);
}
