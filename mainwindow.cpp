#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serial(new QSerialPort(this))
{
    ui->setupUi(this);

    // Populate Ports
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ui->comboPort->addItem(info.portName());
    }

    // Connect Serial Signal to our Slot
    connect(serial, &QSerialPort::readyRead, this, &MainWindow::readSerialData);

    // Apply Styling (Paste the styling code from Phase 4 here)
}

MainWindow::~MainWindow()
{
    if (serial->isOpen()) serial->close();
    delete ui;
}

// --- CONNECTION LOGIC ---
void MainWindow::on_btnConnect_clicked()
{
    if (serial->isOpen()) {
        serial->close();
        ui->btnConnect->setText("Connect");
        ui->lblStatus->setText("System Status: DISCONNECTED");
        ui->lblStatus->setStyleSheet("color: red;");
        updateLog("Disconnected.");
    } else {
        serial->setPortName(ui->comboPort->currentText());
        serial->setBaudRate(QSerialPort::Baud115200); // Faster speed for Teensy
        serial->setDataBits(QSerialPort::Data8);
        serial->setParity(QSerialPort::NoParity);
        serial->setStopBits(QSerialPort::OneStop);
        serial->setFlowControl(QSerialPort::NoFlowControl);

        if (serial->open(QIODevice::ReadWrite)) {
            ui->btnConnect->setText("Disconnect");
            ui->lblStatus->setText("System Status: CONNECTED");
            ui->lblStatus->setStyleSheet("color: #00ff00;"); // Green text
            updateLog("Connected to " + ui->comboPort->currentText());
        } else {
            QMessageBox::critical(this, "Error", "Could not open port!");
        }
    }
}

// --- SENDING COMMANDS ---
// Protocol: Simply send a single char for actions
void MainWindow::sendCommand(char commandChar) {
    if (!serial->isOpen()) return;

    QByteArray data;
    data.append(commandChar);
    serial->write(data);
    updateLog(QString("Sent Command: %1").arg(commandChar));
}

// Protocol: Send Prefix + Value + Newline (e.g., "H100\n")
void MainWindow::sendValue(char prefixChar, int value) {
    if (!serial->isOpen()) return;

    QString data = QString("%1%2\n").arg(prefixChar).arg(value);
    serial->write(data.toUtf8());
    updateLog("Sent Value: " + data.trimmed());
}

// --- BUTTON EVENTS ---

void MainWindow::on_btnStart_clicked()
{
    // 1. Send Configuration first
    sendValue('H', ui->spinHits->value());  // 'H' for Hits
    sendValue('D', ui->spinDelay->value()); // 'D' for Delay

    // 2. Send Start Command
    sendCommand('S'); // 'S' for Start
}

void MainWindow::on_btnStop_clicked()
{
    sendCommand('X'); // 'X' for Stop/Abort
}

void MainWindow::on_btnIncAngle_clicked()
{
    sendCommand(']'); // ']' for Up
}

void MainWindow::on_btnDecAngle_clicked()
{
    sendCommand('['); // '[' for Lower (Down)
}

// --- RECEIVING DATA ---
void MainWindow::readSerialData() {
    // Read all available data
    QByteArray data = serial->readAll();
    QString message = QString::fromUtf8(data).trimmed();

    // Display in log
    updateLog("Received: " + message);

    // Simple Parsing (Placeholder Logic)
    // If Teensy sends "STATUS:5", update UI
    if (message.startsWith("STATUS:")) {
        ui->lblStatus->setText("System Status: " + message.mid(7));
    }
}

void MainWindow::updateLog(const QString &message) {
    QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->txtLog->append(QString("[%1] %2").arg(time, message));
}
