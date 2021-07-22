#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include "upscontroller.h"

const std::string upsDeviceName = "salicru";

UpsController::UpsController(QObject *parent) :
    QObject(parent)
{

#ifdef UPS_ENABLE
    bool connectionDone = false;
    while (!connectionDone)
    {
        try {
            m_nutClient = new nut::TcpClient("localhost", 3493);
            connectionDone = true;
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while new class. Details: " << QString::fromStdString(e.str());
            connectionDone = false;
        }
        if (connectionDone) {
            try {
                m_nutClient->authenticate("admin", "admin");
                connectionDone = true;
            } catch (nut::NutException e) {
                qWarning() << "Nut driver error while authenticate. Details: " << QString::fromStdString(e.str());
                m_nutClient->logout();
                connectionDone = false;
            }
        }
    }

#endif
    connect(&m_pollSaiStatusTimer, &QTimer::timeout, this, &UpsController::sendUpsCommand);
    m_pollSaiStatusTimer.start(1000);

}
void UpsController::sendUpsCommand()
{
    static bool waitingForUpsOnline = false;
    QString upsState = "";
#ifdef UPS_ENABLE
    try {
        upsState = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName, "ups.status")[0]);
    } catch (nut::NutException e) {
        qWarning() << "Nut driver error while requesting ups data. Details: " << QString::fromStdString(e.str());
        QProcess::execute("upsdrvctl start");
        try {
            m_nutClient = new nut::TcpClient("localhost", 3493);
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while new class. Details: " << QString::fromStdString(e.str());
        }
        try {
            m_nutClient->authenticate("admin", "admin");
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while authenticate. Details: " << QString::fromStdString(e.str());
            m_nutClient->logout();
        }
        return;
    }

    qDebug() << upsState;

    if (upsState.contains("BYPASS") && !waitingForUpsOnline) {
        try {
            m_nutClient->executeDeviceCommand(upsDeviceName, "load.on"); // Be sure that the UPS is online mode!
            waitingForUpsOnline = true;
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while setting UPS online mode. Details: " << QString::fromStdString(e.str());
        }
    } else if (upsState == "OL" && waitingForUpsOnline) {
        waitingForUpsOnline = false;
    }
#endif
    emit newUpsState(upsState);

}
