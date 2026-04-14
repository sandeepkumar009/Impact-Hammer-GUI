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

    // ==========================================
    // --- STEP 5: 3-PANEL LAYOUT SETUP (Accel, Hammer, FRF) ---
    // ==========================================

    // Clear default layout
    ui->frameGraph->plotLayout()->clear();

    // Create a sub-layout for the left side (Accel and Hammer)
    QCPLayoutGrid *leftLayout = new QCPLayoutGrid;
    ui->frameGraph->plotLayout()->addElement(0, 0, leftLayout);

    // 1. Accel Rect (Top Left)
    QCPAxisRect *accelRect = new QCPAxisRect(ui->frameGraph);
    accelRect->axis(QCPAxis::atLeft)->setLabel("Accel (g)");
    accelRect->axis(QCPAxis::atBottom)->setTickLabels(false);
    leftLayout->addElement(0, 0, accelRect);

    // 2. Hammer Rect (Bottom Left)
    QCPAxisRect *hammerRect = new QCPAxisRect(ui->frameGraph);
    hammerRect->axis(QCPAxis::atLeft)->setLabel("Hammer (N)");
    hammerRect->axis(QCPAxis::atBottom)->setLabel("Time (s)");
    leftLayout->addElement(1, 0, hammerRect);

    // 3. FRF Rect (Right Side - spans the whole height natively)
    QCPAxisRect *frfRect = new QCPAxisRect(ui->frameGraph);
    frfRect->axis(QCPAxis::atLeft)->setLabel("Accelerance FRF |H| (g/N)");
    frfRect->axis(QCPAxis::atBottom)->setLabel("Frequency (Hz)");
    // Set FRF Y-Axis to Logarithmic scale (standard for FRF)
    frfRect->axis(QCPAxis::atLeft)->setScaleType(QCPAxis::stLogarithmic);
    QSharedPointer<QCPAxisTickerLog> logTicker(new QCPAxisTickerLog);
    frfRect->axis(QCPAxis::atLeft)->setTicker(logTicker);
    ui->frameGraph->plotLayout()->addElement(0, 1, frfRect);

    // Adjust column stretch factors (Make left and right equal width)
    ui->frameGraph->plotLayout()->setColumnStretchFactor(0, 1);
    ui->frameGraph->plotLayout()->setColumnStretchFactor(1, 1);

    // Create Graphs and attach to Rects
    ui->frameGraph->addGraph(accelRect->axis(QCPAxis::atBottom), accelRect->axis(QCPAxis::atLeft));
    ui->frameGraph->graph(0)->setPen(QPen(Qt::cyan));

    ui->frameGraph->addGraph(hammerRect->axis(QCPAxis::atBottom), hammerRect->axis(QCPAxis::atLeft));
    ui->frameGraph->graph(1)->setPen(QPen(QColor(255, 180, 100))); // Orange

    ui->frameGraph->addGraph(frfRect->axis(QCPAxis::atBottom), frfRect->axis(QCPAxis::atLeft));
    ui->frameGraph->graph(2)->setPen(QPen(Qt::magenta)); // Magenta for FRF

    // Set initial Y-axis ranges
    accelRect->axis(QCPAxis::atLeft)->setRange(-5.0, 5.0);
    hammerRect->axis(QCPAxis::atLeft)->setRange(-10.0, 10.0);
    frfRect->axis(QCPAxis::atBottom)->setRange(0, 2000); // Display 0 to 2000 Hz initially
    frfRect->axis(QCPAxis::atLeft)->setRange(0.001, 10); // Log scale needs non-zero min

    // --- NEW: Initialize Trigger Variables ---
    isCapturing = false;
    triggerThreshold = 10.0; // Trigger data capture when hammer force > 10 Newtons
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
    sendValue('H', ui->spinHits->value());  // 'H' for Hits
    sendValue('D', ui->spinDelay->value()); // 'D' for Delay

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

        // --- NEW: Trigger & Capture Logic ---
        // If we are not capturing, look for a strike exceeding the threshold
        if (!isCapturing && hammer[i] > triggerThreshold) {
            isCapturing = true;
            captureAccel.clear();
            captureHammer.clear();

            // PRE-TRIGGER: Copy the last 100 samples from the rolling history
            int preTriggerCount = qMin((int)PRE_TRIGGER_SAMPLES, accelData.size());
            for (int j = accelData.size() - preTriggerCount; j < accelData.size(); ++j) {
                captureAccel.append(accelData[j]);
                captureHammer.append(hammerData[j]);
            }

            updateLog(QString("Impact Detected! Capturing FFT block (including %1 pre-trigger samples)...").arg(preTriggerCount));
        }

        // If a strike occurred, record the block of data
        if (isCapturing) {
            captureAccel.append(accel[i]);
            captureHammer.append(hammer[i]);

            // Once we have 4096 samples (approx 0.4 seconds of data at 10kHz)
            if (captureAccel.size() >= FFT_SIZE) {
                isCapturing = false;
                calculateFRF(); // Compute and plot the FRF
            }
        }
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

        // SAFELY target the exact X-axes of the Accel and Hammer graphs
        ui->frameGraph->graph(0)->keyAxis()->setRange(latestTime - 2.0, latestTime);
        ui->frameGraph->graph(1)->keyAxis()->setRange(latestTime - 2.0, latestTime);
    }

    // 3. Command the GPU/CPU to redraw the visual widget
    ui->frameGraph->replot();
}

// ==========================================
// --- FRF AND SIGNAL PROCESSING MATH ---
// ==========================================

// Standard Cooley-Tukey Radix-2 FFT Algorithm
void MainWindow::performFFT(std::vector<std::complex<double>>& x)
{
    int n = x.size();
    if (n <= 1) return;

    std::vector<std::complex<double>> even(n / 2), odd(n / 2);
    for (int i = 0; i < n / 2; i++) {
        even[i] = x[i * 2];
        odd[i] = x[i * 2 + 1];
    }

    performFFT(even);
    performFFT(odd);

    const double PI = std::acos(-1.0);
    for (int k = 0; k < n / 2; k++) {
        std::complex<double> t = std::polar(1.0, -2.0 * PI * k / n) * odd[k];
        x[k] = even[k] + t;
        x[k + n / 2] = even[k] - t;
    }
}

void MainWindow::calculateFRF()
{
    int n = FFT_SIZE;
    std::vector<std::complex<double>> f_comp(n), a_comp(n);

    // 1. Move captured vectors into complex arrays
    for (int i = 0; i < n; i++) {
        f_comp[i] = std::complex<double>(captureHammer[i], 0);
        a_comp[i] = std::complex<double>(captureAccel[i], 0);
    }

    // 2. Compute Fast Fourier Transforms
    performFFT(f_comp);
    performFFT(a_comp);

    freqData.clear();
    frfMagnitude.clear();

    double sampleRate = 10000.0;
    double df = sampleRate / n; // Frequency resolution (approx 2.44 Hz)

    // 3. Compute H1 FRF = S_fa / S_ff
    // We only loop to n/2 because frequencies above Nyquist are mirrored
    for (int k = 1; k < n / 2; k++) { // Start at 1 to ignore DC offset (0 Hz)
        std::complex<double> f_conj = std::conj(f_comp[k]);

        // Cross-Power Spectrum (Force and Accel)
        std::complex<double> S_fa = f_conj * a_comp[k];

        // Auto-Power Spectrum (Force)
        std::complex<double> S_ff = f_conj * f_comp[k];

        // FRF (H1 Estimator)
        // Since S_ff is technically purely real, we can divide by its real part
        std::complex<double> H1 = S_fa / S_ff.real();

        // Get Magnitude for plotting
        double mag = std::abs(H1);

        // Prevent exact 0s from breaking the logarithmic plot
        if(mag < 0.0001) mag = 0.0001;

        freqData.append(k * df);
        frfMagnitude.append(mag);
    }

    // 4. Update the Graph
    ui->frameGraph->graph(2)->setData(freqData, frfMagnitude);

    // Auto-scale the Y-axis based on the new resonance peaks
    ui->frameGraph->graph(2)->valueAxis()->rescale();

    // Add a bit of padding to the top/bottom of the auto-scale
    ui->frameGraph->graph(2)->valueAxis()->scaleRange(1.5);

    ui->frameGraph->replot();
    updateLog("FRF Calculation Complete.");
}
