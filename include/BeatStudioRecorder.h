#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <vector>
#include <atomic>
#include <portaudio.h>

namespace lmms::gui {

class BeatStudioRecorder : public QThread
{
    Q_OBJECT
public:
    explicit BeatStudioRecorder(QObject* parent = nullptr);
    ~BeatStudioRecorder() override;

    void startRecording();
    void stopRecording();
    QString lastRecordedFile() const { return m_outputFile; }

signals:
    void recordingFinished(const QString& filePath);

protected:
    void run() override;

private:
    static int paCallback(const void* input, void* output,
        unsigned long frameCount,
        const PaStreamCallbackTimeInfo* timeInfo,
        PaStreamCallbackFlags statusFlags,
        void* userData);

    std::vector<float> m_buffer;
    std::atomic<bool> m_recording{false};
    QString m_outputFile;
    PaStream* m_stream{nullptr};
    int m_sampleRate{44100};
};

} // namespace lmms::gui
