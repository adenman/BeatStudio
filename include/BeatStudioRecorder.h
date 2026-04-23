#pragma once
#include <QObject>
#include <QString>
#include <QAudioSource>
#include <QAudioFormat>
#include <QTimer>
#include <vector>
#include <atomic>

namespace lmms::gui {

class BeatStudioRecorder : public QObject
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

private:
    void saveWav();

    QAudioSource* m_audioSource{nullptr};
    QIODevice* m_audioDevice{nullptr};
    QTimer* m_pollTimer{nullptr};
    std::vector<float> m_buffer;
    std::atomic<bool> m_recording{false};
    std::atomic<bool> m_saving{false};
    QString m_outputFile;
    int m_sampleRate{44100};
};

} // namespace lmms::gui
