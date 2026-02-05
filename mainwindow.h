#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QSerialPortInfo>

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

private:
    Ui::MainWindow *ui;
    QSerialPort *serial;

    // Helper functions
    void updateLog(const QString &message);
    void sendCommand(char commandChar);
    void sendValue(char prefixChar, int value);
};
#endif // MAINWINDOW_H
