#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

// Streams the plugin's output to a 24-bit WAV file on disk while recording.
// Audio is handed to a background thread through a lock-free FIFO, so long
// takes don't use memory and the audio thread never touches the disk.
class Recorder
{
public:
    Recorder();
    ~Recorder();

    bool start (const juce::File& file, double sampleRate, int numChannels);
    void stop();

    bool isRecording() const noexcept { return activeWriter.load() != nullptr; }
    double getSecondsRecorded() const noexcept;

    // The most recently finished (or in-progress) take.
    juce::File getLastTake() const;

    // Called from the audio thread.
    void write (const juce::AudioBuffer<float>& buffer) noexcept;

    // Default place where takes are kept: ~/Music/Scrape-Scape/Takes
    static juce::File getTakesFolder();
    static juce::File createNewTakeFile();

private:
    juce::TimeSliceThread backgroundThread { "Scrape-Scape Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<juce::int64> samplesWritten { 0 };
    std::atomic<double> recordingSampleRate { 44100.0 };

    mutable juce::CriticalSection fileLock;
    juce::File lastTake;
};
