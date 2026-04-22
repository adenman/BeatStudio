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
    if (isRunning()) {
        m_recording = false;
        wait(5000);
    }
}

void BeatStudioRecorder::startRecording()
{
    if (isRunning()) return;
    m_buffer.clear();
    m_recording = true;
    start();
}

void BeatStudioRecorder::stopRecording()
{
    m_recording = false;
    // Don't call wait() here — let the thread finish naturally
    // The recordingFinished signal will fire when done
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
            rec->m_buffer.push_back(in[i]);
            rec->m_buffer.push_back(in[i]); // mono -> stereo
        }
    }
    return rec->m_recording.load() ? paContinue : paComplete;
}

void BeatStudioRecorder::run()
{
    Pa_Initialize();

    PaStreamParameters inputParams;
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        qDebug("[BeatStudio] No input device found");
        Pa_Terminate();
        return;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(inputParams.device);
    inputParams.channelCount = 1;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaStream* stream = nullptr;
    PaError err = Pa_OpenStream(&stream, &inputParams, nullptr,
        m_sampleRate, 256, paNoFlag, paCallback, this);

    if (err != paNoError) {
        qDebug("[BeatStudio] Pa_OpenStream failed: %s", Pa_GetErrorText(err));
        Pa_Terminate();
        return;
    }

    Pa_StartStream(stream);
    qDebug("[BeatStudio] Recording started");

    while (m_recording.load()) {
        msleep(50);
    }

    // Stop and close stream before accessing buffer
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    qDebug("[BeatStudio] Recording stopped, saving WAV...");

    // Copy buffer safely
    std::vector<float> data;
    {
        QMutexLocker lock(&m_mutex);
        data = m_buffer;
    }

    if (data.empty()) {
        qDebug("[BeatStudio] No audio recorded");
        return;
    }

    // Write WAV
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    QDir().mkpath(dir);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_outputFile = dir + "/BeatStudio_" + timestamp + ".wav";

    uint32_t numChannels = 2;
    uint32_t bitsPerSample = 16;
    uint32_t numSamples = (uint32_t)data.size();
    uint32_t dataSize = numSamples * (bitsPerSample / 8);

    QFile file(m_outputFile);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug("[BeatStudio] Cannot write WAV: %s", qPrintable(m_outputFile));
        return;
    }

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);

    file.write("RIFF", 4);
    ds << (uint32_t)(36 + dataSize);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    ds << (uint32_t)16;
    ds << (uint16_t)1;           // PCM
    ds << (uint16_t)numChannels;
    ds << (uint32_t)m_sampleRate;
    ds << (uint32_t)(m_sampleRate * numChannels * bitsPerSample / 8);
    ds << (uint16_t)(numChannels * bitsPerSample / 8);
    ds << (uint16_t)bitsPerSample;
    file.write("data", 4);
    ds << dataSize;

    for (float s : data) {
        int16_t sample = static_cast<int16_t>(
            std::max(-32768.f, std::min(32767.f, s * 32767.f)));
        ds << sample;
    }

    file.close();
    qDebug("[BeatStudio] Saved: %s", qPrintable(m_outputFile));
    emit recordingFinished(m_outputFile);
}

} // namespace lmms::gui
