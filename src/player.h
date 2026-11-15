#pragma once
#include <string>
#include <memory>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
class ProLogic2Decoder;

class Player {
public:
    Player();
    ~Player();

    bool open(const std::string& filepath);
    void play();
    void close();

    void toggleSurround() { surroundEnabled_ = !surroundEnabled_; }
    void setSurroundEnabled(bool e) { surroundEnabled_ = e; }

private:
    void decodeLoop();
    void processAudioFrame(AVFrame* frame);
    void renderVideoFrame(AVFrame* frame);

    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext*  videoCtx_ = nullptr;
    AVCodecContext*  audioCtx_ = nullptr;
    SwrContext*      swrCtx_ = nullptr;
    int videoStreamIdx_ = -1;
    int audioStreamIdx_ = -1;

    SDL_Window*   window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_ = nullptr;

    std::unique_ptr<ProLogic2Decoder> pl2_;
    bool surroundEnabled_ = true;

    std::vector<float> workFL_, workFR_, workC_, workLFE_, workRL_, workRR_;
    std::vector<float> resampleBufL_, resampleBufR_;
};
