#include <include/MusicPlayer.h>
#include <event.h>
#include <sstream>
#include <memory>
#include <map>

#define DEBUG_FFMPEG
template <typename T>
inline std::string to_string(T* ptr) {
	std::ostringstream ss;
	ss << ptr;
	return ss.str();
}

namespace music {
    namespace player {
	    enum IOStreamType {
		    IO_ERROR,
		    IO_OUTPUT
	    };
        struct FFMpegStream {
		        typedef std::function<void(const std::string&)> ReadCallback;
		        typedef std::function<void(IOStreamType, int, int, const std::string&)> ErrorCallback;
		        typedef std::function<void()> EndCallback;
            public:

#ifdef REDI_PSTREAM_H_SEEN //So you could include this header event without the extra libs
                typedef redi::pstream pstream_t;
#else
                typedef void* pstream_t;
#endif

			    explicit FFMpegStream(pstream_t* stream) : stream(stream) {}
                ~FFMpegStream();

			    bool initializeEvents();
			    void finalize();

                pstream_t* stream = nullptr;
                size_t channels = 0;
                std::map<std::string, std::string> metadata;

                PlayerUnits duration = PlayerUnits(0);

			    //IO
			    threads::Mutex eventLock;
			    event_base* eventBase = nullptr;
			    event* outEvent = nullptr;
			    event* errEvent = nullptr;
			    bool buffering = false;

			    ReadCallback callback_read_error = [](const std::string&) {};
			    ReadCallback callback_read_output = [](const std::string&) {};
			    ErrorCallback callback_error = [](IOStreamType, int, int, const std::string&) {};
			    EndCallback callback_end = [](){};

			    void enableBuffering() {
				    threads::MutexLock lock(this->eventLock);
				    if(this->buffering) return;
				    this->buffering = true;
				    if(this->outEvent) event_add(this->outEvent, nullptr);
				    if(this->errEvent) event_add(this->errEvent, nullptr);
			    }

			    void disableBuffering() {
				    threads::MutexLock lock(this->eventLock);
				    if(!this->buffering) return;
				    this->buffering = false;
				    if(this->outEvent) event_del_noblock(this->outEvent);
				    if(this->errEvent) event_del_noblock(this->errEvent);
			    }


		    private:
			    void callback_read(int,bool);

			    static void callbackfn_read_error(int,short,void*);
			    static void callbackfn_read_output(int,short,void*);
        };

        class FFMpegMusicPlayer : public AbstractMusicPlayer {
            public:
                explicit FFMpegMusicPlayer(const std::string&);
                FFMpegMusicPlayer(const std::string&, bool);
                ~FFMpegMusicPlayer() override;

                bool initialize(size_t) override;

                void pause() override;

                void play() override;
                void stop() override;

                bool finished() override;

                void forward(const PlayerUnits& duration) override;
                void rewind(const PlayerUnits& duration) override;

                PlayerUnits length() override;
                PlayerUnits currentIndex() override;
		        PlayerUnits bufferedUntil() override;
		        size_t bufferedSampleCount();

                size_t sampleRate() override;

                std::shared_ptr<SampleSegment> popNextSegment() override;
                std::shared_ptr<SampleSegment> peekNextSegment() override;

		        std::deque<std::shared_ptr<Thumbnail>> thumbnails() override;

		        std::string songTitle() override;
                std::string songDescription() override;

            private:
                void spawnProcess();
                void destroyProcess();

                std::string file;

		        threads::Mutex sampleLock;
		        std::deque<std::shared_ptr<SampleSegment>> bufferedSamples{};
		        char byteBuffer[0xF]; //Buffer to store unuseable read overhead (max. 8 full samples)
		        size_t byteBufferIndex = 0;
                //std::shared_ptr<SampleSegment> nextSegment = nullptr;

                std::string fname;
                threads::Mutex streamLock;
                std::shared_ptr<FFMpegStream> stream;
		        void callback_read_output(const std::string &);
		        void callback_read_err(const std::string&);
		        void callback_end();
		        size_t sampleOffset = 0;
                PlayerUnits seekOffset = PlayerUnits(0);

                //void readNextSegment(const std::chrono::nanoseconds&);

                std::string errBuff;
#ifdef DEBUG_FFMPEG
                std::string errHistory;
#endif
                ssize_t readInfo(std::string&, const std::chrono::system_clock::time_point& = std::chrono::system_clock::time_point(), std::string delimiter = "");

                bool live_stream = false;
                bool end_reached = false;

			    void updateBufferState();
        };
    }
}