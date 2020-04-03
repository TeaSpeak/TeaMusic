#include <include/teaspeak/MusicPlayer.h>
#include <thread>
#include <sstream>
#include <memory>
#include <map>
#include "libevent.h"

#define DEBUG_FFMPEG
template <typename T>
inline std::string to_string(T* ptr) {
	std::ostringstream ss;
	ss << ptr;
	return ss.str();
}

namespace music::player {
    enum struct FFMPEGURLType {
        STREAM,
        FILE
    };

    struct FFMpegProcessHandle {
        public:
            enum struct ErrorCode {
                IO_ERROR,
                UNEXPECTED_EXIT
            };

            typedef std::function<void(const void* /* buffer */, size_t /* length */)> ReadCallback;
            typedef std::function<void(ErrorCode /* code */, int /* detail */)> ErrorCallback;
            typedef std::function<void()> EndCallback;
            typedef std::function<void()> TimerCallback;

#ifdef REDI_PSTREAM_H_SEEN //So you could include this header event without the extra libs
            typedef redi::pstream pstream_t;
#else
            typedef void* pstream_t;
#endif

            explicit FFMpegProcessHandle(pstream_t* stream) : process_handle{stream} {}
            ~FFMpegProcessHandle();

            bool initialize_events();
            void finalize();

            void enable_buffering();
            void disable_buffering();

            void schedule_timer(const std::chrono::system_clock::time_point& /* timestamp */);
            void cancel_timer();

            pstream_t* process_handle;

            /* event stuff */
            struct _io {
                std::mutex lock{};

                std::thread::id event_thread;
                void *event_base{nullptr};

                void *event_out{nullptr};
                void *event_err{nullptr};
                void *event_timer{nullptr};

            } io;

            bool buffering{false};

            /* callbacks are called within the event loop! */
            ReadCallback callback_read_error{};
            ReadCallback callback_read_output{};
            TimerCallback callback_timer{};
            ErrorCallback callback_error = [](ErrorCode, int) {};
            EndCallback callback_end = [](){};
        private:
            void callback_read(int, bool);
    };

    struct FFMpegStream {
        public:
            typedef std::function<void()> callback_end_t;
            typedef std::function<void()> callback_abort_t;
            typedef std::function<void()> callback_info_update_t;
            typedef std::function<void(const std::string&)> callback_connect_error_t;

            struct stream_info {
                std::mutex lock{};
                std::condition_variable update_cv{};

                bool initialized{false};
                std::map<std::string, std::string> metadata{};

                std::chrono::milliseconds stream_length{};

                /* contains data like size, time, bitrate, speed */
                std::map<std::string, std::string> stream_properties{};
                std::map<std::string, std::string> stream_stats{};
            };

            explicit FFMpegStream(std::string /* url */, FFMPEGURLType /* url type */, PlayerUnits /* seek offset */, size_t /* frame sample count */, size_t /* channel count */, size_t /* sample rate */);
            ~FFMpegStream();

            bool initialize(std::string& /* error */);
            void finalize();

            [[nodiscard]] std::shared_ptr<SampleSegment> peek_next_segment();
            [[nodiscard]] std::shared_ptr<SampleSegment> pop_next_segment();

            [[nodiscard]] struct stream_info& stream_info() { return this->_stream_info; }
            [[nodiscard]] PlayerUnits current_playback_index();
            [[nodiscard]] PlayerUnits current_buffer_index();

            const std::string url;
            const FFMPEGURLType url_type;
            const size_t frame_sample_count;
            const size_t channel_count;
            const size_t sample_rate;

            /* all events will be called directly. there might be some buffered data left */
            callback_end_t callback_ended{};
            callback_abort_t callback_abort{};
            callback_info_update_t callback_info_initialized{};
            callback_info_update_t callback_info_update{};
            callback_connect_error_t callback_connect_error{};
        private:
            /* call only when sample_lock is acquired */
            std::shared_ptr<SampleSegment> get_sample_buffer();
            [[nodiscard]] size_t buffered_sample_count(bool);

            void callback_read_output(const void* /* buffer */, size_t /* length */);
            void callback_read_err(const void* /* buffer */, size_t /* length */);
            void callback_end();
            void callback_error(FFMpegProcessHandle::ErrorCode, int);
            void update_buffer_state(bool /* lock */);

            std::mutex process_lock{};
#ifdef REDI_PSTREAM_H_SEEN //So you could include this header event without the extra libs
            typedef redi::pstream pstream_t;
#else
            typedef void pstream_t;
#endif
            pstream_t* process_stream{nullptr};
            std::shared_ptr<FFMpegProcessHandle> process_handle{nullptr};

            struct _audio {
                std::mutex lock{};
                std::deque<std::shared_ptr<SampleSegment>> buffered{};

                char overhead_buffer[0xF]{}; //Buffer to store unusable read overhead (max. 8 full samples)
                size_t overhead_index = 0;
            } audio;

            std::string meta_info_buffer{};
            bool meta_output_tag{false};

            PlayerUnits stream_seek_offset;
            size_t stream_sample_offset{0};
            bool end_reached{false};

            struct stream_info _stream_info{};
    };

    class FFMpegMusicPlayer : public AbstractMusicPlayer {
        public:
            struct FallbackStreamInfo {
                std::string title{};
                std::string description{};
            };

            FFMpegMusicPlayer(std::string, FFMPEGURLType /* type */, FallbackStreamInfo /* fallback info */);
            ~FFMpegMusicPlayer() override;

            bool initialize(size_t) override;
            [[nodiscard]] bool await_info(const std::chrono::system_clock::time_point& /* timeout */) const;

            void pause() override;

            void play() override;
            void stop() override;

            bool finished() override;

            void forward(const PlayerUnits& duration) override;
            void rewind(const PlayerUnits& duration) override;

            PlayerUnits length() override;
            PlayerUnits currentIndex() override;
            PlayerUnits bufferedUntil() override;

            size_t sampleRate() override;

            std::shared_ptr<SampleSegment> popNextSegment() override;
            std::shared_ptr<SampleSegment> peekNextSegment() override;

            std::deque<std::shared_ptr<Thumbnail>> thumbnails() override;

            std::string url() const { return this->url_; }
            std::string songTitle() override;
            std::string songDescription() override;

        private:
            struct CachedStreamInfo {
                bool has_title{false};
                bool has_description{false};

                std::string title{};
                std::string description{};
                PlayerUnits length{};

                bool up2date{false};
                mutable std::mutex cv_lock{};
                mutable std::condition_variable update_cv{};
            };

            void spawn_stream();
            void destroy_stream();

            void callback_stream_info();
            void callback_stream_ended();
            void callback_stream_aborted();
            void callback_stream_connect_error(const std::string&);

            void handle_stream_fail();

            std::string url_;
            FFMPEGURLType url_type{FFMPEGURLType::STREAM};
            std::shared_ptr<FFMpegStream> stream{};

            CachedStreamInfo cached_stream_info{};
            FallbackStreamInfo fallback_stream_info{};

            PlayerUnits start_offset{0};
            bool stream_ended{false}, stream_aborted{false};

            bool stream_successfull_started{false};
            size_t stream_fail_count{0};
    };
}