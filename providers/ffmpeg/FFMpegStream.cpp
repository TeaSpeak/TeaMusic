//
// Created by WolverinDEV on 21/02/2020.
//
#include <regex>
#include <algorithm>
#include <StringVariable.h>
#include <providers/shared/pstream.h>
#include "./FFMpegMusicPlayer.h"
#include "./FFMpegProvider.h"
#include "./string_utils.h"

using namespace music::player;

namespace ffmpeg {
    /**
     * @param time format: '00:03:53.50'
     * @return
     */
    inline std::chrono::milliseconds parse_time(const std::string& time) {
        using namespace std::chrono;

        int hour, minute, second, milli;
        if(sscanf(time.c_str(), "%2d:%2d:%2d.%2d", &hour, &minute, &second, &milli) != 4)
            return milliseconds{0};

        return milliseconds{milli * 10} +  seconds{second} + minutes{minute} + hours{hour};
    }

    inline std::string build_time(std::chrono::milliseconds units){
        using namespace std::chrono;

        auto hour = duration_cast<hours>(units);
        units -= hour;

        auto minute = duration_cast<minutes>(units);
        units -= minute;

        auto second = duration_cast<seconds>(units);
        units -= second;

        auto milli = duration_cast<milliseconds>(units);

        char buffer[11 + 1];
        sprintf(buffer, "%02d:%02d:%02d.%02d", (int) hour.count(), (int) minute.count(), (int) second.count(), (int) milli.count() / 10);
        return std::string{buffer};
    }

    struct MetaEntry {
        std::string entry;
        std::deque<std::shared_ptr<MetaEntry>> children;

        [[nodiscard]] std::map<std::string, std::string> as_metamap() const {
            std::map<std::string, std::string> result{};

            for(const auto& entry : this->children) {
                auto dp = entry->entry.find(':');
                std::string key, value;
                if(dp == std::string::npos)
                    key = entry->entry;
                else {
                    key = entry->entry.substr(0, dp);
                    value = entry->entry.substr(dp + 1);
                }
                result[strings::trim(key)] = strings::trim(value);
            }

            return result;
        }
    };


/*
   Example valid MP3 file data:
     [mp3 @ 0x671fe0] Skipping 0 bytes of junk at 4722.
     Input #0, mp3, from 'test.mp3':
       Metadata:
         title           : Princess
         artist          : BooztKidz
         album           : Test-Albunm
       Duration: 00:03:54.16, start: 0.025057, bitrate: 192 kb/s
         Stream #0:0: Audio: mp3, 44100 Hz, stereo, s16p, 192 kb/s
         Metadata:
           encoder         : Lavc56.10
     Output #0, s16le, to 'pipe:1':
       Metadata:
         title           : Princess
         artist          : BooztKidz
         album           : Test-Albunm
         encoder         : Lavf56.40.101
         Stream #0:0: Audio: pcm_s16le, 48000 Hz, stereo, s16, 1536 kb/s
         Metadata:
           encoder         : Lavc56.60.100 pcm_s16le
     Stream mapping:
       Stream #0:0 -> #0:0 (mp3 (native) -> pcm_s16le (native))
     Press [q] to stop, [?] for help


     ---------------------
     Could be also like this:

[19:43:01] [TRACE] Input #0, mp3, from 'http://stream01.iloveradio.de/iloveradio1.mp3':
[19:43:01] [TRACE]   Metadata:
[19:43:01] [TRACE]     StreamTitle     : SEAN PAUL - GOT 2 LUV U
[19:43:01] [TRACE]     icy-br          : 128
[19:43:01] [TRACE]     icy-name        : I Love Radio - Charts & Hits by iloveradio.de
[19:43:01] [TRACE]     icy-pub         : -1
[19:43:01] [TRACE]     icy-url         : iloveradio.de
[19:43:01] [TRACE]   Duration: N/A, start: 0.000000, bitrate: 128 kb/s
[19:43:01] [TRACE]     Stream #0:0: Audio: mp3, 44100 Hz, stereo, s16p, 128 kb/s
[19:43:01] [TRACE] Stream mapping:
[19:43:01] [TRACE]   Stream #0:0 -> #0:0 (mp3 (native) -> pcm_s16le (native))
[19:43:01] [TRACE] Press [q] to stop, [?] for help

[2018-10-21 17:51:14] [DEBUG] Input #0, hls,applehttp, from 'https://cf-hls-media.sndcdn.com/playlist/Kwf':
[2018-10-21 17:51:14] [DEBUG]   Duration: 00:16:25.32, start: 0.000000, bitrate: 0 kb/s
[2018-10-21 17:51:14] [DEBUG]   Program 0
[2018-10-21 17:51:14] [DEBUG]     Metadata:
[2018-10-21 17:51:14] [DEBUG]       variant_bitrate : 0
[2018-10-21 17:51:14] [DEBUG]     Stream #0:0: Audio: mp3, 44100 Hz, stereo, s16p, 128 kb/s
[2018-10-21 17:51:14] [DEBUG] Output #0, s16le, to 'pipe:1':
[2018-10-21 17:51:14] [DEBUG]   Metadata:
[2018-10-21 17:51:14] [DEBUG]     encoder         : Lavf56.40.101
[2018-10-21 17:51:14] [DEBUG]     Stream #0:0: Audio: pcm_s16le, 48000 Hz, stereo, s16, 1536 kb/s
[2018-10-21 17:51:14] [DEBUG]     Metadata:
[2018-10-21 17:51:14] [DEBUG]       encoder         : Lavc56.60.100 pcm_s16le
[2018-10-21 17:51:14] [DEBUG] Stream mapping:
[2018-10-21 17:51:14] [DEBUG]   Stream #0:0 -> #0:0 (mp3 (native) -> pcm_s16le (native))
[2018-10-21 17:51:14] [DEBUG] Press [q] to stop, [?] for help
  */
    inline std::deque<std::shared_ptr<MetaEntry>> parse_metadata(const std::string& in) {
        std::deque<std::shared_ptr<MetaEntry>> stack;
        stack.push_back(std::make_shared<MetaEntry>());

        size_t index = 0;
        do {
            auto old_index = index;
            index = in.find('\n', index);
            auto line = in.substr(old_index, index - old_index);
            {
                size_t space = line.find_first_not_of(' ');
                if(space == std::string::npos) continue;

                if(space % 2 != 0) space += 1; //Round that max one up
                auto stack_index = space / 2 + 1;
                if(stack_index > stack.size()) {
                    music::log::log(music::log::err, "Got metadata without parent!");
                    continue;
                }

                stack.erase(stack.begin() + stack_index, stack.end());

                auto entry = std::make_shared<MetaEntry>();
                entry->entry = strings::trim<std::string>(line);
                stack.back()->children.push_back(entry);
                stack.push_back(std::move(entry));
            }
        } while(++index != 0);

        return stack.front()->children;
    }

    namespace regex {
        static auto property_regex = [] {
            try {
                return std::make_unique<std::regex>(R"((size|time|bitrate|speed)=([ \t]+)?([a-zA-Z0-9\:\.\,\/%]+)([ \t]+)?)");
            } catch (std::exception& ex) {
                music::log::log(music::log::critical, "[FFMPEG] Could not compile property regex!");
            }
            return std::unique_ptr<std::regex>{};
        }();

        //video:0kB audio:5644kB subtitle:0kB other streams:0kB global headers:0kB muxing overhead: 0.000000%
        static auto stats_regex = [] {
            try {
                return std::make_unique<std::regex>(R"((video|audio|subtitle|other|streams|global|headers|muxing|overhead)(:( +)?([a-zA-Z0-9\:\.\,\/%]+))?( +)?)");
            } catch (std::exception& ex) {
                music::log::log(music::log::critical, "[FFMPEG] Could not compile stats regex!");
            }
            return std::unique_ptr<std::regex>{};
        }();
    }
}

FFMpegStream::FFMpegStream(std::string url, FFMPEGURLType type, PlayerUnits seek, size_t fsc, size_t channels, size_t sample_rate)
    : url{std::move(url)}, url_type{type}, frame_sample_count{fsc}, channel_count{channels}, sample_rate{sample_rate}, stream_seek_offset{seek} {
}

FFMpegStream::~FFMpegStream() {
    this->finalize();
}

bool cli_params_to_tokens(std::string_view cli, std::vector<std::string>& args) {
    if(cli.empty()) return true;
    args.reserve(std::count(cli.begin(), cli.end(), ' '));

    size_t index{0}, findex{0};
    do {
        if(index >= cli.length()) break;
        auto needle = cli[index] == '"' || cli[index] == '\'' ? cli[index++] : ' ';
        findex = index;
        while(true) {
            findex = cli.find(needle, findex);
            if(findex >= cli.length() || findex < 1) break;
            if(cli[findex - 1] != '\\') break;
            findex++;
        };

        if(findex == std::string::npos) {
            if(index < cli.length())
                args.emplace_back(cli.substr(index));
            break;
        } else if(findex == index) {
            /* just two spaces after each other */
            index++;
            continue;
        } else {
            args.emplace_back(cli.substr(index, findex - index));
            index = findex + 1;
        }
    } while(index != 0);
    return true;
}

bool FFMpegStream::initialize(std::string &error) {
    std::lock_guard plock{this->process_lock};
    if(this->process_handle || this->process_stream) {
        error = "already initialized";
        return false;
    }

    std::string ffmpeg_command;
    {
        const auto is_seek = this->stream_seek_offset.count() > 0;
        const auto config = FFMpegProvider::instance->configuration();
        switch (this->url_type) {
            case FFMPEGURLType::STREAM:
                ffmpeg_command = is_seek ? config->commands.playback_seek : config->commands.playback;
                break;
            case FFMPEGURLType::FILE:
                ffmpeg_command = is_seek ? config->commands.file_playback_seek : config->commands.file_playback;
                break;
        }
        ffmpeg_command = strvar::transform(ffmpeg_command,
                                           strvar::StringValue{"command", FFMpegProvider::instance->configuration()->ffmpeg_command},
                                           strvar::StringValue{"path", this->url},
                                           strvar::StringValue{"channel_count", std::to_string(this->channel_count)},
                                           strvar::StringValue{"seek_offset", ffmpeg::build_time(this->stream_seek_offset)}
        );
    }

    std::vector<std::string> ffmpeg_command_argv{};
    if(!cli_params_to_tokens(ffmpeg_command, ffmpeg_command_argv)) {
        error = "failed to generate ffmpeg command line arguments";
        return false;
    }
    if(ffmpeg_command_argv.empty()) {
        error = "invalid ffmpeg command line argument count";
        return false;
    }

    this->process_stream = new redi::pstream{ffmpeg_command, redi::pstreams::pstderr | redi::pstreams::pstdout};
    this->process_handle = std::make_shared<FFMpegProcessHandle>(this->process_stream);

    this->process_handle->io.event_base = FFMpegProvider::instance->readerBase;
    this->process_handle->io.event_thread = FFMpegProvider::instance->readerDispatch.get_id();
    this->process_handle->initialize_events();

    this->process_handle->callback_read_error = std::bind(&FFMpegStream::callback_read_err, this, std::placeholders::_1, std::placeholders::_2);
    this->process_handle->callback_read_output = std::bind(&FFMpegStream::callback_read_output, this, std::placeholders::_1, std::placeholders::_2);
    this->process_handle->callback_error = std::bind(&FFMpegStream::callback_error, this, std::placeholders::_1, std::placeholders::_2);
    this->process_handle->callback_end = std::bind(&FFMpegStream::callback_end, this);
    this->process_handle->enable_buffering();
    return true;
}

void FFMpegStream::finalize() {
    /*
     * The destruction will block 'till callback_read_output or callback_read_error have finished.
     * Bt callback_read_output/callback_read_error aqire the process lock
     */
    std::shared_ptr<FFMpegProcessHandle> phandle{};
    {
        std::lock_guard plock{this->process_lock};

        if(this->process_handle)
            std::swap(phandle, this->process_handle);

        if(this->process_stream) {
            std::string send_signals{};
            if(!this->process_stream->rdbuf()->exited()) {
                this->process_stream->rdbuf()->kill(SIGQUIT);
                send_signals += "SIGQUIT";
            }

            if(!this->process_stream->rdbuf()->exited()) {
                this->process_stream->rdbuf()->kill(SIGKILL);
                send_signals += ", SIGKILL";
            }

            if(this->process_stream->rdbuf()->exited()) {
                delete this->process_stream;
            } else {
                /* not the best practice, but we do not want the bot to hang */
                log::log(log::debug, "[FFMPEG] Failed to exit ffmpeg process handle. Deleting process handle (" + std::to_string((uintptr_t) this->process_stream) + ") within another thread (signals send: " + (send_signals.empty() ? "none" : send_signals) + ").");
                std::thread([stream{this->process_stream}]{
                    while(!stream->rdbuf()->exited()) {
                        stream->rdbuf()->kill(SIGKILL);
                        std::this_thread::sleep_for(std::chrono::milliseconds{500});
                    }
                    stream->close();
                    delete stream;
                    log::log(log::debug, "[FFMPEG] Deleting process handle (" + std::to_string((uintptr_t) stream) + ") done.");
                }).detach();
            }

            this->process_stream = nullptr;
        }
    }

    {
        std::lock_guard block{this->audio.lock};
        this->audio.overhead_index = 0;
        this->audio.buffered.clear();

        this->stream_sample_offset = 0;
    }

    this->meta_info_buffer = "";
}

std::shared_ptr<music::SampleSegment> FFMpegStream::get_sample_buffer() {
    if(!this->audio.buffered.empty()) {
        auto last = this->audio.buffered.back();
        if(!last->full) return last;
    }

    auto buffer = SampleSegment::allocate(this->frame_sample_count, this->channel_count);
    this->audio.buffered.push_back(buffer);
    return buffer;
}

void FFMpegStream::callback_read_output(const void *buffer, size_t length) {
    const auto bytes_per_frame = this->channel_count * sizeof(uint16_t);

    {
        std::lock_guard buffer_lock{this->audio.lock};
        auto sample_buffer{this->get_sample_buffer()};

        char* target_byte_buffer{(char*) (sample_buffer->segments + sample_buffer->channels * sample_buffer->segmentLength)};
        size_t target_byte_length = (sample_buffer->maxSegmentLength - sample_buffer->segmentLength) * sample_buffer->channels * sizeof(uint16_t);

        if(this->audio.overhead_index > 0) {
            /* check whatever the last frame has been completed */
            if(this->audio.overhead_index + length < bytes_per_frame) {
                /* we've not enough data to buffer */
                memcpy(this->audio.overhead_buffer + this->audio.overhead_index, buffer, length);
                this->audio.overhead_index += length;
                return;
            } else {
                const auto required_bytes{bytes_per_frame - this->audio.overhead_index};
                memcpy(target_byte_buffer, this->audio.overhead_buffer, this->audio.overhead_index);
                memcpy(target_byte_buffer + this->audio.overhead_index, buffer, required_bytes);

                length -= required_bytes;
                buffer = (const char*) buffer + required_bytes;
                //this->overhead_index = 0; //Will be set later, no need to do that here

                sample_buffer->segmentLength++;
                target_byte_buffer += bytes_per_frame;
                target_byte_length -= bytes_per_frame;
            }
        }

        while(length >= bytes_per_frame) {
            if(target_byte_length < bytes_per_frame) {
                assert(target_byte_length == 0);
                sample_buffer = this->get_sample_buffer(); /* new buffer must not be a total empty one but in theory should be */

                target_byte_buffer = (char*) (sample_buffer->segments + sample_buffer->channels * sample_buffer->segmentLength);
                target_byte_length = (sample_buffer->maxSegmentLength - sample_buffer->segmentLength) * sample_buffer->channels * sizeof(uint16_t);
            }

            const auto samples_read = std::min(target_byte_length, length) / bytes_per_frame;
            const auto byte_read = samples_read * bytes_per_frame;
            memcpy(target_byte_buffer, buffer, byte_read);

            buffer = (const char*) buffer + byte_read;
            length -= byte_read;

            target_byte_buffer += byte_read;
            target_byte_length -= byte_read;
            sample_buffer->segmentLength += samples_read;
            sample_buffer->full = sample_buffer->segmentLength == sample_buffer->maxSegmentLength;
        }

        memcpy(this->audio.overhead_buffer, buffer, length);
        this->audio.overhead_index = length;
    }

    this->update_buffer_state(true);
}

void FFMpegStream::callback_read_err(const void *_buffer, size_t length) {
    std::unique_lock ilock{this->_stream_info.lock};
    if(length > 0) this->meta_info_buffer.append((const char*) _buffer, length);

    if(!this->_stream_info.initialized) {
        constexpr static auto head_tail = "Press [q] to stop, [?] for help";
        auto tail = this->meta_info_buffer.find(head_tail);
        if(tail == std::string::npos)
            return;

        auto data = ffmpeg::parse_metadata(this->meta_info_buffer.substr(0, tail));
        this->meta_info_buffer = this->meta_info_buffer.substr(tail + strlen(head_tail) + 1);

        for(const auto& entry : data) {
            log::log(log::trace, "Got root entry: " + entry->entry);
            if(entry->entry.find("Input #") == 0) {
                for(const auto& entry_data : entry->children) {
                    log::log(log::trace, "Got input stream data: " + entry_data->entry);
                    if(entry_data->entry.find("Metadata:") == 0) {
                        this->_stream_info.metadata = entry_data->as_metamap();
                        log::log(log::debug, "Available metadata:");
                        for(const auto& entry : this->_stream_info.metadata)
                            log::log(log::debug, " Key: '" + entry.first + "' Value: '" + entry.second + "'");
                    } else if(entry_data->entry.find("Duration:") == 0) { //Duration: N/A, start: 0.000000, bitrate: 128 kb/s
                        auto duration_data = entry_data->entry.substr(10);
                        auto duration = duration_data.substr(0, duration_data.find(','));
                        if(duration != "N/A") {
                            this->_stream_info.stream_length = ffmpeg::parse_time(duration);
                            log::log(log::debug, "Parsed duration " + duration + " to " + std::to_string(duration_cast<std::chrono::seconds>(this->_stream_info.stream_length).count()) + " seconds");
                        } else {
                            this->_stream_info.stream_length = std::chrono::milliseconds{0};
                            log::log(log::debug, "Stream does not contains a duration");
                        }
                    }
                }
            } else if(entry->entry.find("Stream mapping") == 0) {
                /* not from interest yet */
            } else if(entry->entry.find("Output #") == 0) {
                /* not from interest yet */
            } else {
                log::log(log::debug, "Got unknown root entry: " + entry->entry);
            }
        }

        this->_stream_info.initialized = true;
        ilock.unlock();
        if(auto callback{this->callback_info_initialized}; callback)
            callback();

        this->callback_read_err(nullptr, 0); /* just in case meta_info_buffer isn't empty */
    } else {
        std::vector<std::string> lines{};
        lines.reserve(32);
        strings::split_lines(lines, this->meta_info_buffer);

        /* evaluate only fulfilled lines */
        if(lines.size() > 1 && !strings::trim(lines.back()).empty())
            this->meta_info_buffer = lines.back();
        else
            this->meta_info_buffer = "";
        lines.pop_back(); //last entry will be empty or stored for later


        bool error_send = false;
        for(const auto& line : lines) {
            if(line.find_first_not_of(" \n\t\r") == std::string::npos && !error_send) {
                this->meta_output_tag = false;
                continue;
            }

            if(ffmpeg::regex::property_regex) {
                auto properties_begin = std::sregex_iterator(line.cbegin(), line.cend(), *ffmpeg::regex::property_regex);
                auto properties_end = std::sregex_iterator();
                if(properties_begin != properties_end) {
                    this->_stream_info.stream_properties.clear();
                    for(auto index = properties_begin; index != properties_end; index++) {
                        if(index->length() < 3) {
                            log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - <invalid group size for \"" + index->str() + "\">");
                            continue;
                        }

                        this->_stream_info.stream_properties[index->operator[](1).str()] = index->operator[](3).str();
                    }

#if false
                    log::log(log::trace, "[FFMPEG][" + to_string(this) + "] Got " + std::to_string(this->_stream_info.stream_properties.size()) + " property values on err stream. (Attention: These properties may differ with the known expected properties!)");
                    for(const auto& [key, value] : this->_stream_info.stream_properties)
                        log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - " + key + " => " + value);
#endif
                    continue;
                }
            }
            if(ffmpeg::regex::stats_regex) {
                auto stats_begin = std::sregex_iterator(line.cbegin(), line.cend(), *ffmpeg::regex::stats_regex);
                auto stats_end = std::sregex_iterator();
                if(stats_begin != stats_end) {
                    for(auto index = stats_begin; index != stats_end; index++) {
                        if(index->length() < 5) {
                            log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - <invalid group size for \"" + index->str() + "\">");
                            continue;
                        }
                        this->_stream_info.stream_stats[index->operator[](1).str()] = index->operator[](4).str();
                    }
#if false
                    log::log(log::trace, "[FFMPEG][" + to_string(this) + "] Got " + std::to_string(this->_stream_info.stream_states.size()) + " stats values. (Attention: These properties may differ with the known expected properties!)");
                    for(const auto& [key, value] : this->_stream_info.stream_stats)
                        log::log(log::trace, "[FFMPEG][" + to_string(this) + "] - " + key + " => " + value);
#endif
                    continue;
                }
            }
            if(line.find("Output #") == 0) {
                this->meta_output_tag = true;
                continue;
            } else if(this->meta_output_tag)
                continue;

            if(!error_send) {
                log::log(log::err, "[FFMPEG][" + to_string(this) + "] Got error message from FFMpeg:");
                error_send = true;
            }
            log::log(log::err, "[FFMPEG][" + to_string(this) + "] " + std::string{line});
        }

        ilock.unlock();
        if(auto callback{this->callback_info_update}; callback)
            callback();
    }
    this->_stream_info.update_cv.notify_all();
}

void FFMpegStream::callback_end() {
    {
        std::lock_guard block{this->audio.lock};
        if(!this->audio.buffered.empty())
            this->audio.buffered.back()->full = true;

        this->end_reached = true;
    }
    if(auto callback{this->callback_ended}; callback)
        callback();
}

void FFMpegStream::callback_error(FFMpegProcessHandle::ErrorCode code, int data) {
    if(code == FFMpegProcessHandle::ErrorCode::UNEXPECTED_EXIT && !this->_stream_info.initialized)
        if(auto callback{this->callback_connect_error}; callback)
            callback(this->meta_info_buffer.empty() ? "ffmpeg exited with " + std::to_string(data) : this->meta_info_buffer);
    if(auto callback{this->callback_abort}; callback)
        callback();
}

void FFMpegStream::update_buffer_state(bool lock) {
    if(this->end_reached) return;

    auto buffered_samples{this->buffered_sample_count(lock)};
    auto buffered_seconds{buffered_samples / this->sample_rate};

    {
        std::lock_guard plock{this->process_lock};
        if(!this->process_handle) return;

        if(buffered_seconds > 20 && this->process_handle->buffering) {
            log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Stop buffering");
            this->process_handle->disable_buffering();
        }

        if(buffered_seconds < 10 && !this->process_handle->buffering) {
            log::log(log::debug, "[FFMPEG][" + to_string(this) + "] Start buffering");
            this->process_handle->enable_buffering();
        }
    }
}

size_t FFMpegStream::buffered_sample_count(bool lock) {
    if(lock) {
        std::lock_guard block{this->audio.lock};
        return this->buffered_sample_count(false);
    }

    size_t result{0};
    for(auto& buffer : this->audio.buffered)
        result += buffer->segmentLength;
    return result;
}

std::shared_ptr<music::SampleSegment> FFMpegStream::peek_next_segment() {
    std::lock_guard block{this->audio.lock};
    return this->audio.buffered.empty() ? nullptr : this->audio.buffered.back();
}

std::shared_ptr<music::SampleSegment> FFMpegStream::pop_next_segment() {
    std::lock_guard block{this->audio.lock};
    if(this->audio.buffered.empty() || !this->audio.buffered.front()->full)
        return nullptr;
    auto buffer = std::move(this->audio.buffered.front());
    this->audio.buffered.pop_front();
    this->stream_sample_offset += buffer->segmentLength;
    this->update_buffer_state(false);
    return buffer;
}

music::PlayerUnits FFMpegStream::current_playback_index() {
    return std::chrono::floor<PlayerUnits>(this->stream_seek_offset + std::chrono::microseconds{(int64_t) ((this->stream_sample_offset * 1e6) / this->sample_rate)});
}

music::PlayerUnits FFMpegStream::current_buffer_index() {
    const auto samples = this->stream_sample_offset + this->buffered_sample_count(true);
    return std::chrono::floor<PlayerUnits>(this->stream_seek_offset + std::chrono::microseconds{(int64_t) ((samples * 1e6) / this->sample_rate)});
}