#include "BeatStudioRecorder.h"
#include <QTimer>
#include <QAudioSink>
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
    // Safe: m_audioSource owned by this via parent, Qt cleans up
}

void BeatStudioRecorder::startRecording()
{
    if (m_recording) return;
    m_saving = false;

    QAudioFormat format;
    format.setSampleRate(m_sampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        qDebug("[BeatStudio] No audio input device");
        return;
    }

    m_buffer.clear();

    // Create fresh source each time
    if (m_audioSource) {
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        m_audioDevice = nullptr;
    }

    m_audioSource = new QAudioSource(inputDevice, format, this);
    m_audioDevice = m_audioSource->start();

    if (!m_audioDevice) {
        qDebug("[BeatStudio] Failed to start");
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        return;
    }

    m_recording = true;

    // Single timer - polls and checks stop flag
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(50);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() {
        // Read available data
        if (m_audioDevice && m_audioSource &&
            m_audioSource->state() == QAudio::ActiveState) {
            QByteArray data = m_audioDevice->readAll();
            if (!data.isEmpty()) {
                const int16_t* s = reinterpret_cast<const int16_t*>(data.constData());
                int n = data.size() / sizeof(int16_t);
                for (int i = 0; i < n; ++i) {
                    float v = s[i] / 32768.f;
                    m_buffer.push_back(v);
                    m_buffer.push_back(v);
                }
            }
        }

        // Check if we should stop - don't stop timer from inside callback
        if (!m_recording && !m_saving) {
            m_saving = true;
            // Schedule cleanup on next event loop tick, not from inside timer callback
            QTimer::singleShot(0, this, [this]() {
                if (m_pollTimer) {
                    m_pollTimer->stop();
                    m_pollTimer->deleteLater();
                    m_pollTimer = nullptr;
                }
                m_audioDevice = nullptr;
                if (m_audioSource) {
                    m_audioSource->disconnect();
                    m_audioSource->deleteLater();
                    m_audioSource = nullptr;
                }
                saveWav();
            });
        }
    });
    m_pollTimer->start();

    qDebug("[BeatStudio] Recording started");
}

void BeatStudioRecorder::stopRecording()
{
    // Just set flag - timer callback handles everything
    m_recording = false;
}

void BeatStudioRecorder::saveWav()
{
    if (m_buffer.empty()) {
        qDebug("[BeatStudio] No audio recorded");
        return;
    }

    // Use a simple path without spaces to avoid issues
    QString dir = "C:/BeatStudio_Recordings";
    QDir().mkpath(dir);
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    m_outputFile = dir + "/BeatStudio_" + ts + ".wav";

    const uint32_t numCh = 2;
    const uint32_t bps = 16;
    const uint32_t nSamples = (uint32_t)m_buffer.size();
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

    for (float s : m_buffer) {
        int16_t v = (int16_t)(std::max(-32768.f, std::min(32767.f, s * 32767.f)));
        ds << v;
    }
    f.close();

    qDebug("[BeatStudio] Saved: %s (%u samples)", qPrintable(m_outputFile), nSamples);
    emit recordingFinished(m_outputFile);
}

} // namespace lmms::gui
