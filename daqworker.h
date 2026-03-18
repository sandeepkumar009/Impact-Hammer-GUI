#ifndef DAQWORKER_H
#define DAQWORKER_H

#include <QObject>
#include <QVector>
#include <NIDAQmx.h>

class DaqWorker : public QObject
{
    Q_OBJECT

public:
    explicit DaqWorker(QObject *parent = nullptr);
    ~DaqWorker();

public slots:
    void startDaq();
    void stopDaq();

signals:
    // This signal bridges the background thread to your main GUI thread
    void dataReady(QVector<double> accelData, QVector<double> hammerData);
    void daqError(QString errorMsg);

private:
    TaskHandle taskHandle;

    // The static callback required by the C API
    static int32 CVICALLBACK EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData);
};

#endif // DAQWORKER_H
