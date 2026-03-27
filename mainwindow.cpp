#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qcustomplot.h"
#include <QMessageBox>
#include <QDateTime>
#include <QMetaType> // ADD THIS

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , serial(new QSerialPort(this))
{
    ui->setupUi(this);

    // // --- TEMPORARY GRAPH TEST SETUP ---
    // // Add two graphs (one for Accel, one for Hammer)
    // ui->frameGraph->addGraph();
    // ui->frameGraph->graph(0)->setPen(QPen(Qt::cyan)); // Graph 0: Accel

    // ui->frameGraph->addGraph();
    // ui->frameGraph->graph(1)->setPen(QPen(QColor(255, 180, 100))); // Graph 1: Hammer (Orange)

    // // Give the axes some labels
    // ui->frameGraph->xAxis->setLabel("Time / Samples");
    // ui->frameGraph->yAxis->setLabel("Amplitude");

    // // Set axis ranges so we can see something
    // ui->frameGraph->xAxis->setRange(0, 1000);
    // ui->frameGraph->yAxis->setRange(-10, 10);
    // // -----------------------------------

    // ==========================================
    // --- STEP 5: DUAL SUBPLOT SETUP ---
    // ==========================================

    // 1. Configure the default Top Axis Rect (Accelerometer)
    QCPAxisRect *accelRect = ui->frameGraph->axisRect(); // Get the default axis rect
    accelRect->axis(QCPAxis::atLeft)->setLabel("Accel (g)");
    // Hide the X-axis tick labels on the top graph to make it look cleaner
    accelRect->axis(QCPAxis::atBottom)->setTickLabels(false);

    // 2. Create the Bottom Axis Rect (Impact Hammer)
    QCPAxisRect *hammerRect = new QCPAxisRect(ui->frameGraph);
    hammerRect->axis(QCPAxis::atLeft)->setLabel("Hammer (N)");
    hammerRect->axis(QCPAxis::atBottom)->setLabel("Time (s)");

    // 3. Add the new rect to the layout (Row 1, Col 0)
    ui->frameGraph->plotLayout()->addElement(1, 0, hammerRect);

    // Add some spacing between the top and bottom plots
    ui->frameGraph->plotLayout()->setRowSpacing(15);

    // 4. Create Graph 0 (Accel) and attach it to the Top Rect
    ui->frameGraph->addGraph(accelRect->axis(QCPAxis::atBottom), accelRect->axis(QCPAxis::atLeft));
    ui->frameGraph->graph(0)->setPen(QPen(Qt::cyan));

    // 5. Create Graph 1 (Hammer) and attach it to the Bottom Rect
    ui->frameGraph->addGraph(hammerRect->axis(QCPAxis::atBottom), hammerRect->axis(QCPAxis::atLeft));
    ui->frameGraph->graph(1)->setPen(QPen(QColor(255, 180, 100))); // Orange

    // 6. Set initial Y-axis ranges
    accelRect->axis(QCPAxis::atLeft)->setRange(-5.0, 5.0);   // Expected g-force range
    hammerRect->axis(QCPAxis::atLeft)->setRange(-10.0, 10.0); // Expected Newton range
    // ==========================================

    // --- STEP 4: PLOTTING & DAQ THREAD SETUP ---
    currentTime = 0.0;

    // 1. Setup Render Timer (~33 FPS)
    renderTimer = new QTimer(this);
    connect(renderTimer, &QTimer::timeout, this, &MainWindow::updatePlot);
    renderTimer->start(30);

    // 2. Register QVector so Qt can pass it between threads safely
    qRegisterMetaType<QVector<double>>("QVector<double>");

    // 3. Create the Thread and the Worker
    daqThread = new QThread(this);
    worker = new DaqWorker();

    // 4. Move the worker to the background thread
    worker->moveToThread(daqThread);

    // 5. Connect the Worker's signals to our MainWindow slots
    connect(worker, &DaqWorker::dataReady, this, &MainWindow::onDataReady);
    connect(worker, &DaqWorker::daqError, this, &MainWindow::onDaqError);

    // Clean up memory automatically when the thread stops
    connect(daqThread, &QThread::finished, worker, &QObject::deleteLater);

    // 6. Start the background thread (it idles until we click Start)
    daqThread->start();

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

    // Safely stop DAQ and kill the thread
    worker->stopDaq();
    daqThread->quit();
    daqThread->wait();

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
    // sendValue('H', ui->spinHits->value());  // 'H' for Hits
    // sendValue('D', ui->spinDelay->value()); // 'D' for Delay

    // 2. Start the DAQ Hardware (Invoke on the background thread!)
    QMetaObject::invokeMethod(worker, "startDaq");

    // 3. Send Start Command to Teensy
    sendCommand('s');

    updateLog("DAQ and Teensy Started.");
}

void MainWindow::on_btnStop_clicked()
{
    // 1. Stop the DAQ Hardware
    QMetaObject::invokeMethod(worker, "stopDaq");

    // 2. Stop the Teensy
    sendCommand('n');  // 'n' for Stop/Abort

    updateLog("System Stopped.");
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

// ==========================================
// --- DAQ & GRAPH PLOTTING LOGIC ---
// ==========================================

void MainWindow::onDaqError(QString errorMsg)
{
    // If the DAQ throws an error (e.g., USB unplugged), show it in the log
    updateLog("DAQ ERROR: " + errorMsg);
    QMessageBox::critical(this, "DAQ Hardware Error", errorMsg);
}

void MainWindow::onDataReady(QVector<double> accel, QVector<double> hammer)
{
    // This receives chunks of exactly 1000 samples from the DAQ
    // Our sample rate is 10,000 Hz. So each sample is 0.0001 seconds apart.
    double timeIncrement = 1.0 / 10000.0;

    for (int i = 0; i < accel.size(); ++i) {
        currentTime += timeIncrement;
        timeData.append(currentTime);
        accelData.append(accel[i]);
        hammerData.append(hammer[i]);
    }

    // Keep memory usage low.
    // At 10,000 Hz, 2 seconds of data = 20,000 points.
    while (timeData.size() > 20000) {
        timeData.removeFirst();
        accelData.removeFirst();
        hammerData.removeFirst();
    }
}

void MainWindow::updatePlot()
{
    // 1. Give the latest arrays to QCustomPlot
    ui->frameGraph->graph(0)->setData(timeData, accelData);
    ui->frameGraph->graph(1)->setData(timeData, hammerData);

    // // 2. Make the X-Axis scroll with the data
    // if (!timeData.isEmpty()) {
    //     double latestTime = timeData.last();
    //     // Show the last 2 seconds of data (latestTime - 2.0 to latestTime)
    //     ui->frameGraph->xAxis->setRange(latestTime - 2.0, latestTime);
    // }

    // 2. Make BOTH X-Axes scroll with the data
    if (!timeData.isEmpty()) {
        double latestTime = timeData.last();

        // Get pointers to both axis rectangles
        QCPAxisRect *accelRect = ui->frameGraph->axisRect(0);
        QCPAxisRect *hammerRect = ui->frameGraph->axisRect(1);

        // Scroll both to show the last 2 seconds
        accelRect->axis(QCPAxis::atBottom)->setRange(latestTime - 2.0, latestTime);
        hammerRect->axis(QCPAxis::atBottom)->setRange(latestTime - 2.0, latestTime);
    }

    // 3. Command the GPU/CPU to redraw the visual widget
    ui->frameGraph->replot();
}
