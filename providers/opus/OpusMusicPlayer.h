#include <include/MusicPlayer.h>
#include <ThreadPool/Mutex.h>
#include <opus/opusfile.h>
#include <memory>

namespace music {
    namespace player {
        class OpusMusicPlayer : public AbstractMusicPlayer {
            public:
                OpusMusicPlayer(const std::string&);
                ~OpusMusicPlayer();

                bool initialize() override;

                void pause() override;

                void play() override;
                void stop() override;

                bool finished() override;

                void forward(const PlayerUnits& duration) override;
                void rewind(const PlayerUnits& duration) override;

                PlayerUnits length() override;
                PlayerUnits currentIndex() override;

                size_t sampleRate() override;

                std::shared_ptr<SampleSegment> popNextSegment() override;
                std::shared_ptr<SampleSegment> peekNextSegment() override;

                std::string songTitle() override;
                std::string songDescription() override;

            private:
                std::string file;

                threads::Mutex fileLock;
                std::shared_ptr<SampleSegment> nextSegment = nullptr;

                OggOpusFile* opusFile = nullptr;

                bool reachedEnd = false;
                void endReached();

                void readNextSegment();
        };
    }
}