#include <misc/pstream.h>
#include <providers/yt/YoutubeMusicPlayer.h>
#include "YTVManager.h"

using namespace std;
using namespace yt;
using namespace sql;

YTVManager::YTVManager(sql::SqlData* handle) : sql(handle), _threads(4), root(fs::u8path("yt")) {}
YTVManager::~YTVManager() {}

threads::Future<std::shared_ptr<AudioInfo>> YTVManager::downloadAudio(std::string video) {
    threads::Future<std::shared_ptr<AudioInfo>> future;

    if(video.length() != 11) {
        auto index = video.find("v=");
        if(index == -1) video = "";
        else video = video.substr(index + 2);
        if(video.length() > 11) video = video.substr(0, 11);
    }
    if(video.length() != 11) {
        future.executionFailed("Invalid youtube video id!");
        return future;
    }

    fs::path videoPath = fs::u8path(this->root.string() + "/v" + video + ".opus");
    if(fs::exists(videoPath)) future.executionSucceed(std::make_shared<AudioInfo>(AudioInfo{"unknown", "unknown", videoPath.string()}));
    else {
        _threads.execute([future, video, videoPath](){
            auto command = "youtube-dl --print-json -x --audio-format opus --audio-quality 0 -o \"" + videoPath.parent_path().string() + "/v" + video + ".%(ext)s\" https://www.youtube.com/watch?v=" + video;
            redi::ipstream proc(command, redi::pstreams::pstdout | redi::pstreams::pstderr);

            string json;
            string err;
            string part;
            while(!proc.eof()) {
                usleep(10);
                part = "";
                proc.out() >> part;
                json += part;

                part = "";
                proc.err() >> part;
                err += part;
            }
            if(proc.fail() && json.empty()) {
                stringstream ss;
                ss << proc.rdbuf();
                err = ss.str();
                future.executionFailed(err);
            } else {
                future.executionSucceed(std::make_shared<AudioInfo>(AudioInfo{"unknown", "unknown", videoPath.string()}));
            }

        });
    }
    return future;
}

threads::Future<std::shared_ptr<music::MusicPlayer>> YTVManager::playAudio(std::string video) {
    threads::Future<std::shared_ptr<music::MusicPlayer>> future;

    auto fut = downloadAudio(video);
    fut.waitAndGetLater([future, fut](std::shared_ptr<AudioInfo> audio){
        if(fut.succeeded() && audio) return future.executionSucceed(make_shared<music::player::YoutubeMusicPlayer>(audio));
        else return future.executionFailed(fut.errorMegssage());
    }, nullptr);

    return future;
}