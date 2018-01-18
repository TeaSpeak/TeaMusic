#include <providers/yt/YoutubeMusicPlayer.h>
#include <misc/pstream.h>
#include "include/MusicPlayer.h"
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

            music::log::log(music::log::debug, "[YT-DL] Command: " + command);
            //system(command.c_str());

            redi::pstream proc;
            proc.open(command, redi::pstreams::pstdout | redi::pstreams::pstderr | redi::pstreams::pstdin);
            string json;
            string err;
            size_t bufferLength = 512;
            char buffer[bufferLength];
            string part;
            while(!proc.eof()) {
                usleep(10);
                if(proc.out().rdbuf()->in_avail()){
                    auto read = proc.out().readsome(buffer, bufferLength);
                    if(read > 0) json += string(buffer, read);
                }

                if(proc.err().rdbuf()->in_avail()){
                    auto read = proc.err().readsome(buffer, bufferLength);
                    if(read > 0) err += string(buffer, read);
                }
            }
            if(err.find('\n') == err.length() - 1) err = err.substr(0, err.length() - 1);
            if(!err.empty()) {
                music::log::log(music::log::err, "[YT-DL] Invalid execution of command " + command);
                music::log::log(music::log::err, "[YT-DL] Message: " + err);
            }
            if(!err.empty() || (proc.fail() && json.empty())) {
                future.executionFailed(err);
            } else {
                future.executionSucceed(std::make_shared<AudioInfo>(AudioInfo{"unknown", "unknown", videoPath.string()}));
            }
        });
    }
    return future;
}

threads::Future<std::shared_ptr<music::MusicPlayer>> YTVManager::playAudio(const std::string& video) {
    threads::Future<std::shared_ptr<music::MusicPlayer>> future;

    auto fut = downloadAudio(video);
    fut.waitAndGetLater([future, fut](std::shared_ptr<AudioInfo> audio){
        if(fut.succeeded() && audio) return future.executionSucceed(make_shared<music::player::YoutubeMusicPlayer>(audio));
        else return future.executionFailed(fut.errorMegssage());
    }, nullptr);

    return future;
}