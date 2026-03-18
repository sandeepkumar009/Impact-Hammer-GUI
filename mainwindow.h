#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QVector>
#include <QThread>
#include "daqworker.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // UI Button Slots
    void on_btnConnect_clicked();
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnIncAngle_clicked();
    void on_btnDecAngle_clicked();

    // Serial Slots
    void readSerialData(); // Function to handle incoming data

    // ADD THESE TWO NEW SLOTS
    void updatePlot();     // The 60 FPS render loop
    void onDataReady(QVector<double> accel, QVector<double> hammer); // REPLACED dummy slot
    void onDaqError(QString errorMsg);                               // NEW error handler

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;

    // ADD THESE VARIABLES FOR PLOTTING
    QTimer *renderTimer;
    QVector<double> timeData;
    QVector<double> accelData;
    QVector<double> hammerData;
    double currentTime;

    // DAQ Threading Variables
    QThread *daqThread;  // NEW
    DaqWorker *worker;   // NEW

    // Helper functions
    void updateLog(const QString &message);
    void sendCommand(char commandChar);
    void sendValue(char prefixChar, int value);
};
#endif // MAINWINDOW_H
