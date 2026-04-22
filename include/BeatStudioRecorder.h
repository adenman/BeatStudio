#pragma once
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QString>
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

private slots:
    void onAudioData();

private:
    void saveWav();

    class QAudioSource* m_audioSource{nullptr};
    class QIODevice* m_audioDevice{nullptr};
    std::vector<float> m_buffer;
    QMutex m_mutex;
    std::atomic<bool> m_recording{false};
    QString m_outputFile;
    int m_sampleRate{44100};
};

} // namespace lmms::gui
