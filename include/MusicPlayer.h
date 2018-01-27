#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <deque>
#include <ThreadPool/Mutex.h>
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
        int16_t* segments;
        size_t segmentLength;
        int channels;

        ~SampleSegment(){
            if(segments) free(segments);
        }
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
        EVENT_ERROR
    };

    typedef std::chrono::milliseconds PlayerUnits;
    class MusicPlayer {
        public:
            virtual bool initialize() = 0;

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

            virtual size_t sampleRate() = 0; //Returns the samples per second
            virtual size_t preferredSampleCount() = 0; //Returns the samples per packet
            virtual void preferredSampleCount(size_t) = 0; //Change the sample count

            virtual std::shared_ptr<SampleSegment> popNextSegment() = 0;
            virtual std::shared_ptr<SampleSegment> peekNextSegment() = 0;

            virtual std::string songTitle() = 0;
            virtual std::string songDescription() = 0;

            virtual bool good() = 0;
            virtual std::string error() = 0;
            virtual void clearError() = 0;

            virtual void registerEventHandler(const std::string&, const std::function<void(MusicEvent)>&) = 0;
            virtual void unregisterEventHandler(const std::string&) = 0;
    };

    class AbstractMusicPlayer : public MusicPlayer {
        public:
            AbstractMusicPlayer() { }
            ~AbstractMusicPlayer() { }

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

            bool seek_supported() override { return true; }

            void registerEventHandler(const std::string&, const std::function<void(MusicEvent)>& function) override;

            void unregisterEventHandler(const std::string&) override;

        protected:
            void applayError(const std::string& _err) {
                this->_error = _err;
                this->fireEvent(MusicEvent::EVENT_ERROR);
            }
            void fireEvent(MusicEvent);

            PlayerState playerState = PlayerState::STATE_STOPPED;
            std::string _error = "";
            size_t _preferredSampleCount = 0;

            threads::Mutex eventLock;
            std::deque<std::pair<std::string, std::function<void(MusicEvent)>>> eventHandlers;
    };

    namespace manager {
        struct PlayerProvider {
            std::string providerName;
            std::string providerDescription;

            virtual threads::Future<std::shared_ptr<MusicPlayer>> createPlayer(const std::string&) = 0;
            virtual bool acceptType(const std::string& type) = 0;
            virtual bool acceptProtocol(const std::string&) = 0;

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
    };
}

























