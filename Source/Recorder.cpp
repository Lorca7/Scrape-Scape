#include "Recorder.h"

Recorder::Recorder()
{
    backgroundThread.startThread();
}

Recorder::~Recorder()
{
    stop();
    backgroundThread.stopThread (2000);
}

juce::File Recorder::getTakesFolder()
{
    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    auto folder = music.getChildFile ("Scrape-Scape").getChildFile ("Takes");

    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Scrape-Scape Takes");

    folder.createDirectory();
    return folder;
}

juce::File Recorder::createNewTakeFile()
{
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    return getTakesFolder().getNonexistentChildFile ("Take " + stamp, ".wav", false);
}

bool Recorder::start (const juce::File& file, double sampleRate, int numChannels)
{
    stop();

    if (sampleRate <= 0.0)
        return false;

    file.deleteFile();

    if (auto fileStream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::WavAudioFormat wav;

        if (auto* writer = wav.createWriterFor (fileStream.get(), sampleRate, (unsigned int) numChannels, 24, {}, 0))
        {
            fileStream.release(); // the writer now owns the stream

            // ~6 seconds of buffering at 48 kHz in case the disk stalls.
            threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, backgroundThread, 1 << 18);

            samplesWritten = 0;
            recordingSampleRate = sampleRate;
            {
                const juce::ScopedLock fl (fileLock);
                lastTake = file;
            }

            const juce::ScopedLock sl (writerLock);
            activeWriter = threadedWriter.get();
            return true;
        }
    }

    return false;
}

void Recorder::stop()
{
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }

    // Deleting the ThreadedWriter flushes any remaining audio and closes the file.
    threadedWriter.reset();
}

double Recorder::getSecondsRecorded() const noexcept
{
    return (double) samplesWritten.load() / recordingSampleRate.load();
}

juce::File Recorder::getLastTake() const
{
    const juce::ScopedLock fl (fileLock);
    return lastTake;
}

void Recorder::write (const juce::AudioBuffer<float>& buffer) noexcept
{
    const juce::ScopedLock sl (writerLock);

    if (auto* w = activeWriter.load())
    {
        if (w->write (buffer.getArrayOfReadPointers(), buffer.getNumSamples()))
            samplesWritten += buffer.getNumSamples();
    }
}
