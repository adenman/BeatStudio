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
    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
    }
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

    if (!inputDevice.isFormatSupported(format)) {
        qDebug("[BeatStudio] Format not supported");
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        m_buffer.clear();
    }

    m_audioSource = new QAudioSource(inputDevice, format, this);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        qDebug("[BeatStudio] Failed to start audio");
        delete m_audioSource;
        m_audioSource = nullptr;
        return;
    }

    m_recording = true;
    connect(m_audioDevice, &QIODevice::readyRead, this, &BeatStudioRecorder::onAudioData);
    qDebug("[BeatStudio] Recording started");
}

void BeatStudioRecorder::stopRecording()
{
    // Don't do anything here directly - use a timer to stop safely
    // after the current event loop iteration completes
    m_recording = false;
    QTimer::singleShot(100, this, &BeatStudioRecorder::doStop);
}

void BeatStudioRecorder::doStop()
{
    if (m_audioDevice) {
        disconnect(m_audioDevice, nullptr, this, nullptr);
        m_audioDevice = nullptr;
    }

    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
    }

    saveWav();
}

void BeatStudioRecorder::onAudioData()
{
    if (!m_audioDevice) return;
    QByteArray data = m_audioDevice->readAll();
    if (data.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    const int16_t* samples = reinterpret_cast<const int16_t*>(data.constData());
    int count = data.size() / sizeof(int16_t);
    for (int i = 0; i < count; ++i) {
        float s = samples[i] / 32768.f;
        m_buffer.push_back(s);
        m_buffer.push_back(s);
    }
}

void BeatStudioRecorder::saveWav()
{
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

    qDebug("[BeatStudio] Saved: %s", qPrintable(m_outputFile));
    emit recordingFinished(m_outputFile);
}

} // namespace lmms::gui
