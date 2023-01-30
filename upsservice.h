#ifndef UPSSERVICE_H
#define UPSSERVICE_H

#include <QObject>
#include <QVariantMap>

#include "sigmaservice.h"
#include "upscontroller.h"
#include "upsservicesettings.h"

class UpsService : public SigmaService
{
    Q_OBJECT
public:
    explicit UpsService(const QString& configPath, const QString &upsDeviceName, QObject *parent = nullptr);

signals:
    void newUpsState(const QString upsState);

public slots:
    void handleQuitSignal(); /* last service actions */

protected:
    UpsServiceSettings* m_upsServiceSettings;
    UpsController* m_upsController;
};
#endif // UPSSERVICE_H
