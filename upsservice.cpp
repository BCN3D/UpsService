#include <QDebug>
#include <QThread>
#include <QMap>
#include <QUuid>

#include "upsservice.h"

UpsService::UpsService(const QString& configPath, QObject *parent) :
    SigmaService("UpsService", configPath, new UpsServiceSettings, parent)
{
#ifdef __arm__
    QDBusConnection connection = QDBusConnection::systemBus();
#else
    QDBusConnection connection = QDBusConnection::sessionBus();
#endif

    this->m_upsServiceSettings = dynamic_cast<UpsServiceSettings*>(this->serviceSettings());

    this->m_upsController = new UpsController(this);

    // Forward driver signals

    connect(m_upsController, &UpsController::newUpsState, [=](const QString &upsState) { emit newUpsState(upsState); });

}

void UpsService::handleQuitSignal()
{
    qDebug() << "Closing Ups service...\n";
}
