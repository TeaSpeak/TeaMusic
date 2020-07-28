#pragma once

#include <providers/ffmpeg/FFMpegMusicPlayer.h>

namespace yt {
    class AudioInfo;
}

namespace music::player {
    class YoutubeMusicPlayer : public FFMpegMusicPlayer {
        public:
            explicit YoutubeMusicPlayer(std::shared_ptr<yt::AudioInfo>);
            ~YoutubeMusicPlayer() override;

            std::string songTitle() override;

            std::string songDescription() override;

            std::deque<std::shared_ptr<Thumbnail>> thumbnails() override;

        private:
            std::shared_ptr<yt::AudioInfo> video;
    };
}