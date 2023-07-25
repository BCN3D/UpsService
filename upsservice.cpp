#include <QDebug>
#include <QThread>
#include <QMap>
#include <QUuid>

#include "upsservice.h"
#include "upsservice_interface.h"

UpsService::UpsService(const QString& configPath, const QString &upsDeviceName, QObject *parent) :
    SigmaService("UpsService", configPath, new UpsServiceSettings, parent)
{
#ifdef __arm__
    QDBusConnection connection = QDBusConnection::systemBus();
#else
    QDBusConnection connection = QDBusConnection::sessionBus();
#endif

    this->m_upsServiceSettings = dynamic_cast<UpsServiceSettings*>(this->serviceSettings());

    if(upsDeviceName != nullptr)
    {
        this->m_upsController = new UpsController(this, upsDeviceName);
    }
    else
    {
        this->m_upsController = new UpsController(this);
    }

    // Forward driver signals
    connect(m_upsController, &UpsController::newUpsState, [=](const QString &upsState) { emit newUpsState(upsState); });
}

void UpsService::handleQuitSignal()
{
    qDebug() << "Closing Ups service...\n";
}
