#include "BeatStudioRecorder.h"
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <cstring>
#include <cstdint>

namespace lmms::gui {

BeatStudioRecorder::BeatStudioRecorder(QObject* parent)
    : QThread(parent)
{
    Pa_Initialize();
}

BeatStudioRecorder::~BeatStudioRecorder()
{
    stopRecording();
    Pa_Terminate();
}

void BeatStudioRecorder::startRecording()
{
    m_buffer.clear();
    m_recording = true;
    start();
}

void BeatStudioRecorder::stopRecording()
{
    m_recording = false;
    wait(3000);
}

int BeatStudioRecorder::paCallback(const void* input, void* /*output*/,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo*,
    PaStreamCallbackFlags,
    void* userData)
{
    auto* rec = static_cast<BeatStudioRecorder*>(userData);
    if (!rec->m_recording) return paComplete;

    const float* in = static_cast<const float*>(input);
    if (in) {
        for (unsigned long i = 0; i < frameCount; ++i) {
            rec->m_buffer.push_back(in[i]);
            rec->m_buffer.push_back(in[i]); // duplicate mono to stereo
        }
    }
    return rec->m_recording ? paContinue : paComplete;
}

void BeatStudioRecorder::run()
{
    PaStreamParameters inputParams;
    inputParams.device = Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        qDebug("[BeatStudio] No input device found");
        return;
    }

    const PaDeviceInfo* info = Pa_GetDeviceInfo(inputParams.device);
    inputParams.channelCount = 1;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = info->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    PaError err = Pa_OpenStream(&m_stream, &inputParams, nullptr,
        m_sampleRate, 256, paNoFlag, paCallback, this);

    if (err != paNoError) {
        qDebug("[BeatStudio] Pa_OpenStream failed: %s", Pa_GetErrorText(err));
        return;
    }

    Pa_StartStream(m_stream);
    qDebug("[BeatStudio] Recording started...");

    while (m_recording) {
        msleep(50);
    }

    Pa_StopStream(m_stream);
    Pa_CloseStream(m_stream);
    m_stream = nullptr;

    // Write WAV file
    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    QDir().mkpath(dir);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_outputFile = dir + "/BeatStudio_" + timestamp + ".wav";

    // WAV header
    uint32_t numSamples = m_buffer.size(); // stereo pairs
    uint32_t numChannels = 2;
    uint32_t sampleRate = m_sampleRate;
    uint32_t bitsPerSample = 16;
    uint32_t dataSize = numSamples * (bitsPerSample / 8);

    QFile file(m_outputFile);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug("[BeatStudio] Cannot write WAV: %s", qPrintable(m_outputFile));
        return;
    }

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);

    // RIFF header
    file.write("RIFF", 4);
    ds << (uint32_t)(36 + dataSize);
    file.write("WAVE", 4);
    // fmt chunk
    file.write("fmt ", 4);
    ds << (uint32_t)16;       // chunk size
    ds << (uint16_t)1;        // PCM
    ds << (uint16_t)numChannels;
    ds << sampleRate;
    ds << (uint32_t)(sampleRate * numChannels * bitsPerSample / 8);
    ds << (uint16_t)(numChannels * bitsPerSample / 8);
    ds << (uint16_t)bitsPerSample;
    // data chunk
    file.write("data", 4);
    ds << dataSize;

    // Convert float to int16
    for (float s : m_buffer) {
        int16_t sample = static_cast<int16_t>(std::max(-32768.f, std::min(32767.f, s * 32767.f)));
        ds << sample;
    }

    file.close();
    qDebug("[BeatStudio] Saved recording: %s", qPrintable(m_outputFile));
    emit recordingFinished(m_outputFile);
}

} // namespace lmms::gui
