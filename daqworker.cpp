#include "daqworker.h"

// Configuration constants
#define SAMPLE_RATE 10000.0
#define SAMPLES_PER_READ 1000
#define NUM_CHANNELS 2
#define HAMMER_SENSITIVITY 2.25

DaqWorker::DaqWorker(QObject *parent) : QObject(parent) {
    taskHandle = 0;
}

DaqWorker::~DaqWorker() {
    stopDaq();
}

void DaqWorker::startDaq() {
    int32 error = 0;
    char errBuff[2048] = {'\0'};

    error = DAQmxCreateTask("", &taskHandle);
    if (error < 0) goto Error;

    // Channel 0: Accelerometer
    error = DAQmxCreateAIAccelChan(taskHandle, "Dev1/ai0", "Accelerometer", DAQmx_Val_PseudoDiff,
                                   -100.0, 100.0, DAQmx_Val_AccelUnit_g, 50.0, DAQmx_Val_mVoltsPerG,
                                   DAQmx_Val_Internal, 0.004, NULL);
    if (error < 0) goto Error;

    // Channel 1: Impact Hammer
    error = DAQmxCreateAIForceIEPEChan(taskHandle, "Dev1/ai1", "Hammer", DAQmx_Val_PseudoDiff,
                                       -500.0, 500.0, DAQmx_Val_Newtons, HAMMER_SENSITIVITY, DAQmx_Val_mVoltsPerNewton,
                                       DAQmx_Val_Internal, 0.004, NULL);
    if (error < 0) goto Error;

    // Timing Configuration
    error = DAQmxCfgSampClkTiming(taskHandle, "", SAMPLE_RATE, DAQmx_Val_Rising,
                                  DAQmx_Val_ContSamps, SAMPLES_PER_READ);
    if (error < 0) goto Error;

    // Register Background Callback (Passing 'this' to link back to the class)
    error = DAQmxRegisterEveryNSamplesEvent(taskHandle, DAQmx_Val_Acquired_Into_Buffer,
                                            SAMPLES_PER_READ, 0, EveryNCallback, this);
    if (error < 0) goto Error;

    error = DAQmxStartTask(taskHandle);
    if (error < 0) goto Error;

    return;

Error:
    DAQmxGetExtendedErrorInfo(errBuff, 2048);
    emit daqError(QString::fromUtf8(errBuff));
    stopDaq();
}

void DaqWorker::stopDaq() {
    if (taskHandle != 0) {
        DAQmxStopTask(taskHandle);
        DAQmxClearTask(taskHandle);
        taskHandle = 0;
    }
}

int32 CVICALLBACK DaqWorker::EveryNCallback(TaskHandle taskHandle, int32 everyNsamplesEventType, uInt32 nSamples, void *callbackData) {
    // 1. Cast the void pointer back to our DaqWorker instance
    DaqWorker *worker = static_cast<DaqWorker*>(callbackData);

    int32 read = 0;
    double data[SAMPLES_PER_READ * NUM_CHANNELS];

    // 2. Read the hardware buffer
    int32 error = DAQmxReadAnalogF64(taskHandle, SAMPLES_PER_READ, 10.0,
                                     DAQmx_Val_GroupByScanNumber, data, SAMPLES_PER_READ * NUM_CHANNELS, &read, NULL);

    if (error == 0 && read > 0) {
        QVector<double> accelVec;
        QVector<double> hammerVec;
        accelVec.reserve(read);
        hammerVec.reserve(read);

        // 3. Separate the interleaved data
        for (int i = 0; i < read; i++) {
            accelVec.append(data[i * NUM_CHANNELS + 0]);
            hammerVec.append(data[i * NUM_CHANNELS + 1]);
        }

        // 4. Safely emit the arrays to the Qt Main Thread
        emit worker->dataReady(accelVec, hammerVec);

    } else if (error < 0) {
        char errBuff[2048] = {'\0'};
        DAQmxGetExtendedErrorInfo(errBuff, 2048);
        emit worker->daqError(QString::fromUtf8(errBuff));
    }

    return 0;
}
