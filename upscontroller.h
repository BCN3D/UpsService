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
#include <qserialportinfo.h>
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
    NUT,
    MODBUS
};

enum UPS_STATE {
    OUT = 0,
    TEST_CONNECTION,
    TEST_OK,
    TEST_FAIL,
    CHECK,
    WAITING,
    ERROR
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

    void MBtestRequest();
    void MBtestResponse();

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
    QString mb_portname = "";

    UPS_STATE ups_state = UPS_STATE::OUT;
    UPS_CLIENT available_clients[2] = { UPS_CLIENT::MODBUS, UPS_CLIENT::NUT };
    int current_client = 0; // get first client
    QList<QSerialPortInfo> available_ports;
    int current_port = 0;

    int connection_tries = 0;

    void connect_client();
    void doWork();

    bool getNextClient();
    bool checkMODBUSPort();
    bool onlineRT3();
    QString stateName(UPS_STATE state);
    void changeState(UPS_STATE newstate);
};

#endif // UPSCONTROLLER_H
