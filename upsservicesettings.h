#ifndef UPSSERVICESETTINGS_H
#define UPSSERVICESETTINGS_H

#include <QtJsonSerializer>
#include <servicesettings.h>

class UpsServiceSettings : public ServiceSettings
{
    Q_OBJECT

public:
    Q_INVOKABLE UpsServiceSettings(QObject *parent = nullptr);

Q_JSON_POLYMORPHIC(true)
};
#endif // UPSSERVICESETTINGS_H
