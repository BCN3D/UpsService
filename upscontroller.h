#ifndef UPSCONTROLLER_H
#define UPSCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QSharedPointer>
#include <QFileSystemWatcher>

#include "nutclient.h"
//#include "serialmanager/serialportwatcher.h"
#include <QModbusRtuSerialMaster>
#include <QModbusDataUnit>

#define UPS_ENABLE

#define MAX_CONNECTIONS_TRIED 3
#define NEXT_CONNECTION_WAIT_SECS 1
#define MODBUS_SLAVE_ID 1

#define MB_SLAVE = 0x01
#define MB_PORT = "/dev/ttyUSB0"

#define MB_REG_ALARM    5
#define MB_REG_BATTERY 64
#define MB_REG_ONLINE  730

#define MB_ALARM_AC_BIT 14


class QModbusClient;
class QModbusReply;

enum UPS_CLIENT {
    NONE = 0,
    NUT = 1,
    MODBUS
};


class UpsController : public QObject
{
    Q_OBJECT
public:
    UpsController(QObject *parent = nullptr, const QString &upsDeviceName = "salicru");

signals:
    void newUpsState(const QString& upsState);

public slots:
    void sendUpsCommand();
    void MODBUSresponse();

protected slots:

protected:

private:
    nut::Client* m_nutClient;
    QTimer m_pollSaiStatusTimer;
    QString upsDeviceName;

    QModbusClient *modbusDevice = nullptr;

    QModbusDataUnit readRequest() const;
    QModbusDataUnit writeRequest() const;
    QModbusReply *lastRequest = nullptr;
    void OnlineRT3();

    UPS_CLIENT available_clients[2] = { UPS_CLIENT::NUT, UPS_CLIENT::MODBUS };
    UPS_CLIENT ups_client;
    size_t current_client;
    bool getNextClient();
};

#endif // UPSCONTROLLER_H
