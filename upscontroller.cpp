#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <unistd.h>
#include "qserialport.h"
#include "upscontroller.h"


bool UpsController::getNextClient(){

    bool ok = false;
    size_t l = sizeof(available_clients) / sizeof(UPS_CLIENT);
    if (++current_client < l) {
        ok = true;
    }
    return ok;
}

UpsController::UpsController(QObject *parent, const QString &upsDeviceName) :
    QObject(parent),
    upsDeviceName(upsDeviceName)
{

#ifdef UPS_ENABLE
    bool connectionDone = false;
    int tries = 0;

    ups_client = available_clients[0]; // get first client
    qDebug() << "Check available drivers";
    while (!connectionDone)
    {
        switch (available_clients[current_client]) {
        case NUT:
            try {
                m_nutClient = new nut::TcpClient("localhost", 3493);
                qInfo("NUT client connected");

                QString test = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName.toStdString(), "ups.status")[0]);
                qDebug() << "Nut driver test: " << test;
                if (test.contains("DRIVER-NOT-CONNECTED")) {
                    qWarning() << "Nut driver not connected";
                    //connectionDone = false;
                } else {
                    connectionDone = true;
                }
            } catch (nut::NutException e) {
                qWarning() << "Nut driver error while new class. Details: " << QString::fromStdString(e.str());
                //connectionDone = false;
            }
            if (connectionDone) {
                try {
                    m_nutClient->authenticate("admin", "admin");
                    connectionDone = true;
                } catch (nut::NutException e)                                               {
                    qWarning() << "Nut driver error while authenticate. Details: " << QString::fromStdString(e.str());
                    m_nutClient->logout();
                    //connectionDone = false;
                }
            }
            break;

        case MODBUS:
            modbusDevice = new QModbusRtuSerialMaster(this);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialPortNameParameter, "/dev/ttyUSB0");
            modbusDevice->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::MarkParity);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, QSerialPort::Baud19200);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);

            if (modbusDevice->connectDevice()) {
                qInfo("MODBUS client connected");
                connectionDone = true;
                OnlineRT3(); // Send the online command to SAI RT3 (in case it is in bypass)
            } else {
                qWarning() << "MODBUS connection fail";
            }
            break;

        default:
            qWarning("UNKNOWN UPS CLIENT!");
            break;
        }

        if (!connectionDone) {
            sleep(NEXT_CONNECTION_WAIT_SECS);
            if (tries++ > MAX_CONNECTIONS_TRIED) {
                tries = 0;
                if (!getNextClient()) {
                    //connectionDone = false;
                    break;
                }
                qDebug() << "max tries reached, trying next client (" << ups_client << ")";
            }
        }
    }

#endif
    if (connectionDone){
        connect(&m_pollSaiStatusTimer, &QTimer::timeout, this, &UpsController::sendUpsCommand);
        m_pollSaiStatusTimer.start(1000);
    }
}


void UpsController::OnlineRT3() {

    qDebug() << "SAI RT3 Online";
    QModbusDataUnit pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_ONLINE, 0);
    auto *reply = modbusDevice->sendReadRequest(pdu, MODBUS_SLAVE_ID);
    while (!reply->isFinished()) { // wait for it...
        usleep(100000);
    }
    delete reply;
}


void UpsController::sendUpsCommand()
{
    static bool waitingForUpsOnline = false;
    QString upsState = "";
    QModbusDataUnit pdu;

#ifdef UPS_ENABLE

    switch (available_clients[current_client]) {
    case NUT:
        try {
            upsState = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName.toStdString(), "ups.status")[0]);
        } catch (nut::NutException e) {
            qWarning() << "Nut driver error while requesting ups data. Details: " << QString::fromStdString(e.str());
            return;
        }
        qDebug() << upsState;

        if (upsState.contains("BYPASS") && !waitingForUpsOnline) {
            try {
                m_nutClient->executeDeviceCommand(upsDeviceName.toStdString(), "load.on"); // Be sure that the UPS is online mode!
                waitingForUpsOnline = true;
            } catch (nut::NutException e) {
                qWarning() << "Nut driver error while setting UPS online mode. Details: " << QString::fromStdString(e.str());
            }
        } else if (upsState == "OL" && waitingForUpsOnline) {
            waitingForUpsOnline = false;
        }
        qDebug() << "NUT emit newUpsState " << upsState;
        emit newUpsState(upsState);
        break;

    case MODBUS:
        pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_ALARM, 1);
        if (auto *reply = modbusDevice->sendReadRequest(pdu, MODBUS_SLAVE_ID)) {
            if (!reply->isFinished())
                connect(reply, &QModbusReply::finished, this, &UpsController::MODBUSresponse);
            else
                delete reply; // broadcast replies return immediately
        } else {
            qWarning() << "MODBUS Read error (alarm): " << modbusDevice->errorString();
        }

        // Read battery registers
        pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_BATTERY, 4);
        if (auto *reply = modbusDevice->sendReadRequest(pdu, MODBUS_SLAVE_ID)) {
            if (!reply->isFinished())
                connect(reply, &QModbusReply::finished, this, &UpsController::MODBUSresponse);
            else
                delete reply; // broadcast replies return immediately
        } else {
            qWarning() << "MODBUS Read error (battery): " << modbusDevice->errorString();
        }
        break;
    default:
        // do nothing
        break;
    }
#endif
}


void UpsController::MODBUSresponse() {

    QString upsState = "";

    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError) {
        const QModbusDataUnit unit = reply->result();
        int batlvl = 0;
        int minleft = 0;

        switch (unit.startAddress()) {
        case MB_REG_ALARM:
            if (1 & unit.value(0) >> MB_ALARM_AC_BIT) {
                upsState = "OL: on";
            } else {
                upsState = "OB: OFF";
            }
            break;

        case MB_REG_BATTERY:
            /*** debug ***
            for (int i = 0, total = int(unit.valueCount()); i < total; ++i) {
                const QString entry = QString("Address: %1, Value: %2")
                                        .arg(unit.startAddress() + i)
                                        .arg(QString::number(unit.value(i), unit.registerType() <= QModbusDataUnit::Coils ? 10 : 16));
                qDebug() << "MODBUS: " << entry;
            }
            //*/
            batlvl = unit.value(0);
            minleft = unit.value(2)/60;
            upsState = QString(" battery level: %1%  (minutes left: %2)").arg(batlvl).arg(minleft);
            break;

        default:
            // ignore response
            upsState = "";
            break;
        }

    } else if (reply->error() == QModbusDevice::ProtocolError) {
        qWarning() << QString("Read response error: %1 (MODBUS exception: 0x%2)")
                          .arg(reply->errorString())
                          .arg(reply->rawResult().exceptionCode(), -1, 16);
    } else {
        qWarning() << QString("Read response error: %1 (code: 0x%2)")
                          .arg(reply->errorString())
                          .arg(reply->error(), -1, 16);
    }
    reply->deleteLater();
    if (upsState.length() > 1) {
        qInfo() << "MODBUS newUpsState: " << upsState;
        emit newUpsState(upsState);
    }
}
