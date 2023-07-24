#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <unistd.h>
#include "qserialport.h"
#include <qserialportinfo.h>
#include "upscontroller.h"


UpsController::UpsController(QObject *parent, const QString &upsDeviceName) :
    QObject(parent),
    upsDeviceName(upsDeviceName)
{

    qDebug() << "**************** Test RC14";

#ifdef UPS_ENABLE

    qInfo() << "starting UpsController...";
    ups_client = available_clients[current_client]; // get first client
    ups_state = UPS_STATE::TEST_CONNECTION;

    doWork();
#endif
}


bool UpsController::getNextClient(){

    bool ok = false;
    size_t l = sizeof(available_clients) / sizeof(UPS_CLIENT);
    if (++current_client < l) {
        ok = true;
    }
    return ok;
}

bool UpsController::checkMODBUSPort() {

    const auto serialPortInfos = QSerialPortInfo::availablePorts();
    const size_t numports = serialPortInfos.length();
    bool ok = true;

    qDebug() << "checkMODBUSPort()";
    if (numports > 0) {

        for (const QSerialPortInfo &portInfo : serialPortInfos) {

            qDebug() << "trying MODBUS on port " << portInfo.portName();
            modbusDevice = new QModbusRtuSerialMaster(this);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialPortNameParameter, portInfo.portName());
            modbusDevice->setConnectionParameter(QModbusDevice::SerialParityParameter, QSerialPort::MarkParity);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, QSerialPort::Baud19200);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, QSerialPort::Data8);
            modbusDevice->setConnectionParameter(QModbusDevice::SerialStopBitsParameter, QSerialPort::OneStop);
            modbusDevice->setTimeout(1000); // milliseconds
            modbusDevice->setNumberOfRetries(1);
            if (modbusDevice->connectDevice()) {

                qDebug() << "device connected";
                sendUpsCommand();
            }
        }
    } else {
        qWarning() << "No serial ports available";
        ok = false;
    }
    return ok;
}


void UpsController::doWork() {

    switch (ups_state) {
    case UPS_STATE::TEST_CONNECTION:
        qDebug() << "S: TEST_CONNECTION (client: " << ups_client << " try #" << connection_tries << "/" << MAX_CONNECTIONS_TRIED <<  ")";
        connect_client();
        if (!connectionDone) {
            sleep(NEXT_CONNECTION_WAIT_SECS);
            if (++connection_tries == MAX_CONNECTIONS_TRIED) {
                connection_tries = 0;
                if (!getNextClient()) {
                    qInfo("No more clients to test, there is not UPS");
                    ups_state = UPS_STATE::ERROR;
                }
                qInfo() << "max tries reached, trying next client (" << ups_client << ")";
            }
        } else {
            ups_state = UPS_STATE::TEST_OK;
        }
        break;

    case UPS_STATE::TEST_OK:
        qDebug() << "S: TEST_OK";
        ups_state = CHECK;
        connect(&m_pollSaiStatusTimer, &QTimer::timeout, this, &UpsController::sendUpsCommand);
        m_pollSaiStatusTimer.start(1000);

    case UPS_STATE::CHECK:
        qDebug() << "S: CHECK";
        //sendUpsCommand();
        ups_state = UPS_STATE::WAITING;
        break;

    case UPS_STATE::WAITING:
        qDebug() << "S: WAITING";
        // do nothing
        usleep(10000);
        break;

    case UPS_STATE::ERROR:
        qDebug() << "S: ERROR";
        break;

    default:
        qDebug() << "S: ??? " << ups_state;
        // do nothing
        usleep(10000);
        doWork();
        break;
    }
}


void UpsController::connect_client() {

    qDebug() << "testing client " << available_clients[current_client];
    switch (available_clients[current_client]) {
    case NUT:
        try {
            m_nutClient = new nut::TcpClient("localhost", 3493);
            QString test = QString::fromStdString(m_nutClient->getDeviceVariableValue(upsDeviceName.toStdString(), "ups.status")[0]);
            qDebug() << "NUT driver test: " << test;
            if (test.contains("DRIVER-NOT-CONNECTED")) {
                qWarning() << "NUT driver not connected";
            } else {
                qInfo("NUT client connected");
                m_nutClient->authenticate("admin", "admin");
                ups_state = UPS_STATE::TEST_OK;
                connectionDone = true;
            }
        } catch (nut::NutException e) {
            qWarning() << "NUT driver error: " << QString::fromStdString(e.str());
        }
        doWork();
        break;

    case MODBUS:
        if (!checkMODBUSPort()) {
            doWork();
        }
        break;

    default:
        qWarning("UNKNOWN UPS CLIENT!");
        break;
    }
}


bool UpsController::onlineRT3() {

    qDebug() << "SAI RT3 Online";
    bool ok = false;

    QModbusDataUnit pdu = QModbusDataUnit(QModbusDataUnit::RegisterType::HoldingRegisters, MB_REG_ONLINE, 0);
    waiting_modbus_request   = true;
    modbus_request_completed = false;
    if (auto *reply = modbusDevice->sendWriteRequest(pdu, MODBUS_SLAVE_ID)) {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &UpsController::MODBUSresponse);
        else
            delete reply; // broadcast replies return immediately
    } else {
        qWarning() << "MODBUS Read error (alarm): " << modbusDevice->errorString();
    }
    while (waiting_modbus_request) {
        usleep(100000);
        qDebug() << "waiting for request completion";
    }
    qDebug() << "SAI RT3 Online: " << ok;

    return ok;
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
                connectionDone = true;
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
            connectionDone = false;
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
            connectionDone = false;
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
    connectionDone = false;

    qDebug() << "MODBUSresponse";

    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError) {

        qDebug() << "MODBUS reply OK";
        if (ups_state == UPS_STATE::TEST_CONNECTION) {
            ups_state = UPS_STATE::TEST_OK;
        }
        connectionDone = true;

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
/*
        case MB_REG_ONLINE:
            qDebug() << "MB_REG_ONLINE completed";
            modbus_state = ONLINE_OK;
            break;
*/
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
    doWork();
}
