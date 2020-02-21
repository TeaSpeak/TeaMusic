#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <deque>
#include <utility>
#include <variant>
#include <map>
#include <ThreadPool/Future.h>

#if defined(_MSC_VER)
//  Microsoft
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
//  GCC
#define EXPORT __attribute__((visibility("default")))
#define IMPORT
#else
//  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

namespace music {
    namespace log {
        enum Level { //Copy for spdlog::level::level_enum
            trace = 0,
            debug = 1,
            info = 2,
            warn = 3,
            err = 4,
            critical = 5,
            off = 6
        };
        extern void log(const Level&, const std::string&);
    };

    struct SampleSegment {
        /**
         * Array of samples.
         * Length      : channels * segmentLength
         * char length : channels * segmentLength * sizeof(s16le)
         * Encoding    : s16le
         */
        mutable int16_t* segments;
	    const size_t maxSegmentLength{0};
	    const size_t channels{0};
        size_t segmentLength{0};
        bool full{false};


	    SampleSegment(int16_t *segments, const size_t maxSegmentLength, const size_t channels) : segments(segments), maxSegmentLength(maxSegmentLength), channels(channels) {}
	    ~SampleSegment() = default;

	    inline static std::shared_ptr<SampleSegment> allocate(size_t maxSamples, size_t channels) {
	    	auto memory = malloc(maxSamples * channels * sizeof(int16_t) + sizeof(SampleSegment));
	    	new(memory) SampleSegment((int16_t*) ((char*) memory + sizeof(SampleSegment)), maxSamples, channels);

	    	return std::shared_ptr<SampleSegment>((SampleSegment*) memory, [](SampleSegment* data) {
	    	    data->~SampleSegment();
	    	    ::free(data);
	    	});
	    }
    };

    typedef std::chrono::milliseconds PlayerUnits;
	enum ThumbnailType {
		THUMBNAIL_NONE,
		THUMBNAIL_URL
	};

	class Thumbnail {
		public:
			explicit Thumbnail(ThumbnailType _type) : _type(_type) {}
			virtual ~Thumbnail() = default;

			[[nodiscard]] ThumbnailType type() const { return this->_type; }
		private:
			ThumbnailType _type;
	};

	class ThumbnailUrl : public Thumbnail {
		public:
			explicit ThumbnailUrl(std::string url) : Thumbnail(ThumbnailType::THUMBNAIL_URL), _url(std::move(url)) {}
			~ThumbnailUrl() override = default;

            [[nodiscard]] std::string url() const { return this->_url; }
		private:
			std::string _url;
	};

	enum UrlType {
		TYPE_VIDEO,
		TYPE_STREAM,
		TYPE_PLAYLIST
	};

	struct UrlInfo {
		UrlType type;
		std::string url;
	};

	struct UrlSongInfo : public UrlInfo {
		std::string title;
		std::string description;

		std::shared_ptr<Thumbnail> thumbnail;
        PlayerUnits length;

		std::map<std::string, std::string> metadata;
	};

	struct UrlPlaylistInfo : public UrlInfo {
		std::deque<std::shared_ptr<UrlSongInfo>> entries;
	};

    enum PlayerState {
        STATE_UNINIZALISIZED,
        STATE_PLAYING,
        STATE_PAUSE,
        STATE_STOPPED
    };
    extern const char* stateNames[];

    enum MusicEvent {
        EVENT_PLAY,
        EVENT_STOP,
        EVENT_PAUSE,
        EVENT_ABORT,
        EVENT_END,
        EVENT_ERROR,

        EVENT_INFO_UPDATE
    };

    class MusicPlayer {
        public:
            virtual bool initialize(size_t channelCount) = 0;

            virtual PlayerState state() = 0;
            virtual void play() = 0;
            virtual void pause() = 0;
            virtual void stop() = 0;
            virtual bool finished() = 0;

            virtual bool seek_supported() = 0;
            virtual void forward(const PlayerUnits&) = 0;
            virtual void rewind(const PlayerUnits&) = 0;

            virtual PlayerUnits length() = 0;
            virtual PlayerUnits currentIndex() = 0;
		    virtual PlayerUnits bufferedUntil() = 0;

            virtual size_t sampleRate() = 0; //Returns the samples per second
            virtual size_t preferredSampleCount() = 0; //Returns the samples per packet
            virtual void preferredSampleCount(size_t) = 0; //Change the sample count
		    virtual size_t channelCount() = 0;

            virtual std::shared_ptr<SampleSegment> popNextSegment() = 0;
            virtual std::shared_ptr<SampleSegment> peekNextSegment() = 0;

            virtual std::string songTitle() = 0;
            virtual std::string songDescription() = 0;
		    virtual std::deque<std::shared_ptr<Thumbnail>> thumbnails() = 0;

            virtual bool good() = 0;
            virtual std::string error() = 0;
            virtual void clearError() = 0;

            virtual void registerEventHandler(const std::string&, const std::function<void(MusicEvent)>&) = 0;
            virtual void unregisterEventHandler(const std::string&) = 0;
    };

    class AbstractMusicPlayer : public MusicPlayer {
        public:
            AbstractMusicPlayer() = default;

		    virtual ~AbstractMusicPlayer() = default;

		    PlayerState state() override {
                return playerState;
            }

            void play() override {
                playerState = PlayerState::STATE_PLAYING;
                this->fireEvent(MusicEvent::EVENT_PLAY);
            }

            void pause() override {
                playerState = PlayerState::STATE_PAUSE;
                this->fireEvent(MusicEvent::EVENT_PAUSE);
            }

            void stop() override {
                playerState = PlayerState::STATE_STOPPED;
                this->fireEvent(MusicEvent::EVENT_STOP);
            }

            std::string error() override {
                return _error;
            }

            void clearError() override {
                _error = "";
            }

            bool good() override { return _error.empty(); }

            size_t preferredSampleCount() override {
                return _preferredSampleCount;
            }

            void preferredSampleCount(size_t size) override {
                this->_preferredSampleCount = size;
            }

		    bool initialize(size_t channelCount) override {
			    this->_channelCount = channelCount;
			    return true;
		    }

		    size_t channelCount() override {
			    return this->_channelCount;
		    }

		    bool seek_supported() override { return true; }

            void registerEventHandler(const std::string&, const std::function<void(MusicEvent)>& function) override;

            void unregisterEventHandler(const std::string&) override;
        protected:
            void apply_error(const std::string &_err) {
                this->_error = _err;
                this->fireEvent(MusicEvent::EVENT_ERROR);
            }
            void fireEvent(MusicEvent);

            PlayerState playerState = PlayerState::STATE_STOPPED;
            std::string _error = "";
            size_t _preferredSampleCount = 0;
		    size_t _channelCount = 0;

            std::mutex eventLock;
            std::deque<std::pair<std::string, std::function<void(MusicEvent)>>> eventHandlers;
    };

    namespace manager {
        struct PlayerProvider {
            std::string providerName;
            std::string providerDescription;

            virtual threads::Future<std::shared_ptr<UrlInfo>> query_info(const std::string& /* url */, void* /* custom data */, void* /* internal use */) = 0;

            virtual threads::Future<std::shared_ptr<MusicPlayer>> createPlayer(const std::string& /* url */, void* /* custom data */, void* /* internal use */) = 0;
            virtual std::vector<std::string> availableFormats() = 0;
            virtual std::vector<std::string> availableProtocols() = 0;

            bool acceptProtocol(const std::string& protocol) {
                for(const auto& entry : availableProtocols())
                    if(entry == protocol) return true;
                return false;
            }

            bool acceptType(const std::string &type) {
                for(const auto& entry : availableFormats())
                    if(entry == type) return true;
                return false;
            }

            virtual bool acceptString(const std::string& str){
                auto index = str.find_last_of('.');
                if(index != std::string::npos) {
                    if(this->acceptType(str.substr(index + 1))) return true;
                }
                index = str.find_first_of(':');
                if(index != std::string::npos) {
                    if(this->acceptProtocol(str.substr(0, index))) return true;
                }
                return false;
            }

            virtual size_t weight(const std::string&) { return 0; }
        };

        extern std::deque<std::shared_ptr<PlayerProvider>> registeredTypes();
        extern void registerType(const std::shared_ptr<PlayerProvider>&);

        //empty for not set
        extern std::shared_ptr<PlayerProvider> resolveProvider(const std::string& provName, const std::string& str);

        template <typename T>
        inline std::deque<std::shared_ptr<PlayerProvider>> resolveProvider(){
            std::deque<std::shared_ptr<PlayerProvider>> result;
            for(const auto& prov : registeredTypes())
                if(std::dynamic_pointer_cast<T>(prov))
                    result.push_back(std::dynamic_pointer_cast<T>(prov));
            return result;
        }

        extern void loadProviders(const std::string&);
	    extern void finalizeProviders();
	    extern void register_provider(const std::shared_ptr<PlayerProvider>&);
    };
}

























