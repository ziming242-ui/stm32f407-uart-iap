#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>

#include "firmware_updater.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refreshPorts();
    void toggleConnection();
    void chooseFirmware();
    void startUpgrade();
    void appendLog(const QString &message);
    void updateControls();

private:
    FirmwareUpdater updater_;
    QComboBox *portCombo_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLineEdit *firmwarePathEdit_ = nullptr;
    QPushButton *chooseButton_ = nullptr;
    QLineEdit *versionEdit_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QPlainTextEdit *logEdit_ = nullptr;
};

#endif
