#include "BeatStudioRecorder.h"
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <QMutexLocker>
#include <QTimer>
#include <algorithm>
#include <cstdint>

namespace lmms::gui {

BeatStudioRecorder::BeatStudioRecorder(QObject* parent)
    : QObject(parent)
{
}

BeatStudioRecorder::~BeatStudioRecorder()
{
    m_recording = false;
    // Let Qt clean up via parent ownership
}

void BeatStudioRecorder::startRecording()
{
    if (m_recording) return;

    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug("[BeatStudio] No audio input device");
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }

    m_audioSource = new QAudioSource(inputDevice, format, this);
    m_audioSource->setBufferSize(4096);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        qDebug("[BeatStudio] Failed to start audio, state=%d", (int)m_audioSource->state());
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        return;
    }

    m_recording = true;

    // Use a timer to poll audio data instead of readyRead signal
    // This avoids any re-entrancy issues
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(50);
    connect(m_pollTimer, &QTimer::timeout, this, &BeatStudioRecorder::onAudioData);
    m_pollTimer->start();

    qDebug("[BeatStudio] Recording started");
}

void BeatStudioRecorder::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;

    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }

    // Suspend instead of stop - much safer
    if (m_audioSource) {
        m_audioSource->suspend();
    }

    // Save on next event loop tick
    QTimer::singleShot(200, this, &BeatStudioRecorder::saveWav);
}

void BeatStudioRecorder::onAudioData()
{
    if (!m_audioDevice || !m_recording) return;

    QByteArray data = m_audioDevice->readAll();
    if (data.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    const int16_t* samples = reinterpret_cast<const int16_t*>(data.constData());
    int count = data.size() / sizeof(int16_t);
    for (int i = 0; i < count; ++i) {
        float s = samples[i] / 32768.f;
        m_buffer.push_back(s);
        m_buffer.push_back(s); // mono to stereo
    }
}

void BeatStudioRecorder::saveWav()
{
    // Clean up audio source now that we're safely in event loop
    if (m_audioSource) {
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        m_audioDevice = nullptr;
    }

    std::vector<float> data;
    {
        QMutexLocker lock(&m_mutex);
        data = m_buffer;
    }

    if (data.empty()) {
        qDebug("[BeatStudio] No audio recorded");
        return;
    }

    QString dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (dir.isEmpty()) dir = QDir::homePath();
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_outputFile = dir + "/BeatStudio_" + ts + ".wav";

    const uint32_t numCh = 2;
    const uint32_t bps = 16;
    const uint32_t nSamples = (uint32_t)data.size();
    const uint32_t dataSize = nSamples * (bps / 8);

    QFile f(m_outputFile);
    if (!f.open(QIODevice::WriteOnly)) {
        qDebug("[BeatStudio] Cannot write file");
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

    qDebug("[BeatStudio] Saved: %s (%zu samples)", qPrintable(m_outputFile), data.size());
    emit recordingFinished(m_outputFile);
}

} // namespace lmms::gui
