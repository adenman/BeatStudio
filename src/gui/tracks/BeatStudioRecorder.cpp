#include "BeatStudioRecorder.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QMutexLocker>
#include <QFile>
#include <QDataStream>
#include <cstdint>
#include <algorithm>

namespace lmms::gui {

BeatStudioRecorder::BeatStudioRecorder(QObject* parent)
    : QThread(parent)
{
}

BeatStudioRecorder::~BeatStudioRecorder()
{
    m_recording = false;
    if (isRunning()) {
        wait(5000);
        terminate();
    }
}

void BeatStudioRecorder::startRecording()
{
    if (isRunning()) return;
    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }
    m_recording = true;
    start();
}

void BeatStudioRecorder::stopRecording()
{
    m_recording = false;
    // Thread will finish naturally and emit recordingFinished
}

int BeatStudioRecorder::paCallback(const void* input, void* /*output*/,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void* userData)
{
    auto* rec = static_cast<BeatStudioRecorder*>(userData);
    if (!rec->m_recording.load()) return paComplete;

    const float* in = static_cast<const float*>(input);
    if (in) {
        QMutexLocker lock(&rec->m_mutex);
        for (unsigned long i = 0; i < frameCount; ++i) {
            float s = in[i];
            rec->m_buffer.push_back(s);
            rec->m_buffer.push_back(s);
        }
    }
    return rec->m_recording.load() ? paContinue : paComplete;
}

void BeatStudioRecorder::run()
{
    PaError initErr = Pa_Initialize();
    if (initErr != paNoError) {
        qDebug("[BeatStudio] Pa_Initialize failed: %s", Pa_GetErrorText(initErr));
        return;
    }

    PaDeviceIndex device = Pa_GetDefaultInputDevice();
    if (device == paNoDevice) {
        qDebug("[BeatStudio] No input device");
        Pa_Terminate();
        return;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
    if (!info) {
        Pa_Terminate();
        return;
    }

    PaStreamParameters inputParams;
    inputParams.device = device;
    inputParams.channelCount = 1;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inputParams, nullptr,
        m_sampleRate, 256, paNoFlag, paCallback, this);

    if (err != paNoError || !stream) {
        qDebug("[BeatStudio] Pa_OpenStream failed: %s", Pa_GetErrorText(err));
        Pa_Terminate();
        return;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        Pa_CloseStream(stream);
        Pa_Terminate();
        return;
    }

    qDebug("[BeatStudio] Recording...");
    while (m_recording.load()) {
        msleep(50);
    }

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    // Copy buffer
    std::vector<float> data;
    {
        QMutexLocker lock(&m_mutex);
        data = m_buffer;
    }

    if (data.empty()) {
        qDebug("[BeatStudio] No data recorded");
        return;
    }

    // Save WAV
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outFile = dir + "/BeatStudio_" + ts + ".wav";

    const uint32_t numCh = 2;
    const uint32_t bps = 16;
    const uint32_t nSamples = (uint32_t)data.size();
    const uint32_t dataSize = nSamples * (bps / 8);

    QFile f(outFile);
    if (!f.open(QIODevice::WriteOnly)) {
        qDebug("[BeatStudio] Cannot open file for writing");
        return;
    }

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    f.write("RIFF", 4);
    ds << (uint32_t)(36 + dataSize);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    ds << (uint32_t)16 << (uint16_t)1 << (uint16_t)numCh;
    ds << (uint32_t)m_sampleRate;
    ds << (uint32_t)(m_sampleRate * numCh * bps / 8);
    ds << (uint16_t)(numCh * bps / 8) << (uint16_t)bps;
    f.write("data", 4);
    ds << dataSize;

    for (float s : data) {
        int16_t v = (int16_t)(std::max(-32768.f, std::min(32767.f, s * 32767.f)));
        ds << v;
    }
    f.close();

    qDebug("[BeatStudio] Saved to: %s", qPrintable(outFile));
    m_outputFile = outFile;
    emit recordingFinished(outFile);
}

} // namespace lmms::gui
