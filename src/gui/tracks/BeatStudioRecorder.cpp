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
#include <algorithm>
#include <cstdint>

namespace lmms::gui {

BeatStudioRecorder::BeatStudioRecorder(QObject* parent)
    : QObject(parent)
{
}

BeatStudioRecorder::~BeatStudioRecorder()
{
    stopRecording();
}

void BeatStudioRecorder::startRecording()
{
    if (m_recording) return;

    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Float);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug("[BeatStudio] No audio input device found");
        return;
    }

    // Check format is supported, fall back to Int16 if needed
    if (!inputDevice.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        if (!inputDevice.isFormatSupported(format)) {
            qDebug("[BeatStudio] Audio format not supported");
            return;
        }
    }

    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }

    m_audioSource = new QAudioSource(inputDevice, format, this);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        qDebug("[BeatStudio] Failed to start audio source");
        delete m_audioSource;
        m_audioSource = nullptr;
        return;
    }

    m_recording = true;

    // Poll for data every 50ms
    connect(m_audioDevice, &QIODevice::readyRead, this, &BeatStudioRecorder::onAudioData);

    qDebug("[BeatStudio] Recording started via Qt QAudioSource");
}

void BeatStudioRecorder::onAudioData()
{
    if (!m_recording || !m_audioDevice) return;

    QByteArray data = m_audioDevice->readAll();
    if (data.isEmpty()) return;

    QMutexLocker lock(&m_mutex);

    // Check what format we got
    if (m_audioSource->format().sampleFormat() == QAudioFormat::Float) {
        const float* samples = reinterpret_cast<const float*>(data.constData());
        int count = data.size() / sizeof(float);
        for (int i = 0; i < count; ++i) {
            m_buffer.push_back(samples[i]);
            m_buffer.push_back(samples[i]); // mono -> stereo
        }
    } else {
        // Int16
        const int16_t* samples = reinterpret_cast<const int16_t*>(data.constData());
        int count = data.size() / sizeof(int16_t);
        for (int i = 0; i < count; ++i) {
            float s = samples[i] / 32768.f;
            m_buffer.push_back(s);
            m_buffer.push_back(s);
        }
    }
}

void BeatStudioRecorder::stopRecording()
{
    if (!m_recording) return;
    m_recording = false;

    if (m_audioDevice) {
        disconnect(m_audioDevice, &QIODevice::readyRead, this, &BeatStudioRecorder::onAudioData);
        m_audioDevice = nullptr;
    }

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource->deleteLater(); // safe async delete
        m_audioSource = nullptr;
    }

    qDebug("[BeatStudio] Recording stopped, saving...");
    QMetaObject::invokeMethod(this, &BeatStudioRecorder::saveWav, Qt::QueuedConnection);
}

void BeatStudioRecorder::saveWav()
{
    std::vector<float> data;
    {
        QMutexLocker lock(&m_mutex);
        data = m_buffer;
    }

    if (data.empty()) {
        qDebug("[BeatStudio] No audio data recorded");
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
        qDebug("[BeatStudio] Cannot write file: %s", qPrintable(m_outputFile));
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
