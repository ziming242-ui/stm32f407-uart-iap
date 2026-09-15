#include <QApplication>
#include <QTemporaryFile>
#include <QTimer>

#include "firmware_updater.h"
#include "iap_protocol.h"
#include "main_window.h"

namespace {

void appendU32Le(QByteArray &bytes, quint32 value)
{
    bytes.append(static_cast<char>(value & 0xFFu));
    bytes.append(static_cast<char>((value >> 8u) & 0xFFu));
    bytes.append(static_cast<char>((value >> 16u) & 0xFFu));
    bytes.append(static_cast<char>((value >> 24u) & 0xFFu));
}

bool writeTemporaryImage(QTemporaryFile &file, quint32 resetHandler)
{
    QByteArray image;
    appendU32Le(image, 0x20020000u);
    appendU32Le(image, resetHandler);
    image.append(QByteArray(8, static_cast<char>(0xA5)));
    if (!file.open() || file.write(image) != image.size()) {
        return false;
    }
    file.close();
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("STM32F407 IAP Updater"));
    QApplication::setOrganizationName(QStringLiteral("UART IAP Demo"));

    MainWindow window;
    if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
        const QByteArray vector("123456789");
        QTemporaryFile validImage;
        QTemporaryFile invalidImage;
        FirmwareUpdater updater;
        if (IapProtocol::crc16CcittFalse(vector) != 0x29B1u ||
            IapProtocol::crc32Ieee(vector) != 0xCBF43926u ||
            IapProtocol::errorName(IAP_ERR_COMMAND) != QStringLiteral("COMMAND") ||
            IapProtocol::errorName(IAP_ERR_TIMEOUT) != QStringLiteral("TIMEOUT") ||
            !writeTemporaryImage(validImage, 0x08040009u) ||
            !updater.loadFirmware(validImage.fileName()) ||
            !writeTemporaryImage(invalidImage, 0x08000001u) ||
            updater.loadFirmware(invalidImage.fileName())) {
            return 2;
        }
        QTimer::singleShot(100, &application, &QCoreApplication::quit);
        return application.exec();
    }
    window.show();
    return application.exec();
}
