#pragma once

#include <providers/ffmpeg/FFMpegMusicPlayer.h>

namespace yt {
    class AudioInfo;
}
namespace music {
    namespace player {
        class YoutubeMusicPlayer : public FFMpegMusicPlayer {
            public:
                YoutubeMusicPlayer(std::shared_ptr<yt::AudioInfo>);
                ~YoutubeMusicPlayer();

                std::string songTitle() override;

                std::string songDescription() override;

            private:
                std::shared_ptr<yt::AudioInfo> video;
        };
    }
}