#include <include/MusicPlayer.h>
#include <ThreadPool/Mutex.h>
#include <memory>
#include <map>

#define DEBUG_FFMPEG
namespace music {
    namespace player {
        struct FFMpegStream {
            public:

#ifdef REDI_PSTREAM_H_SEEN //So you could include this header event without the extra libs
                typedef redi::pstream pstream_t;
#else
                typedef void* pstream_t;
#endif
                FFMpegStream(pstream_t* stream) : stream(stream) {}
                ~FFMpegStream() {
                    delete stream;
                    this->stream = nullptr;
                }

                pstream_t* stream = nullptr;
                size_t channels = 0;
                std::map<std::string, std::string> metadata;

                PlayerUnits duration = PlayerUnits(0);
                PlayerUnits currentIndex = PlayerUnits(0);
                size_t sampleOffset = 0;
        };

        class FFMpegMusicPlayer : public AbstractMusicPlayer {
            public:
                FFMpegMusicPlayer(const std::string&);
                FFMpegMusicPlayer(const std::string&, bool);
                ~FFMpegMusicPlayer();

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
                void spawnProcess();
                void destroyProcess();

                std::string file;

                std::shared_ptr<SampleSegment> nextSegment = nullptr;

                std::string fname;
                threads::Mutex streamLock;
                std::shared_ptr<FFMpegStream> stream;
                PlayerUnits seekOffset = PlayerUnits(0);

                void readNextSegment();

                std::string errBuff;
#ifdef DEBUG_FFMPEG
                std::string errHistory;
#endif
                ssize_t readInfo(std::string&, const std::chrono::system_clock::time_point& = std::chrono::system_clock::time_point(), std::string delimiter = "");

                bool live_stream = false;
                bool end_reached = false;

                size_t failCount = 0;
        };
    }
}