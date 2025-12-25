#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Frame.hpp"
#include "libobsensor/hpp/Device.hpp"
#include "libobsensor/hpp/Error.hpp"
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>
#include <iostream>
#include <cstring>
#include <ffmpeg/libavformat.h>
#include <ffmpeg/libavcodec.h>
#include <ffmpeg/libavutil.h>

// Use Orbbec SDK to get frames, then encode with FFmpeg and send via UDP
// This combines the reliability of Orbbec SDK with efficient H.264 encoding

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Orbbec + H.264 Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Setup UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    inet_pton(AF_INET, receiver_ip.c_str(), &servaddr.sin_addr);

    // Initialize Orbbec camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();
    config->enableVideoStream(OB_STREAM_COLOR);

    std::shared_ptr<ob::Frame> lastColorFrame;
    std::mutex frameMutex;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            auto colorFrame = frameset->colorFrame();
            if (colorFrame) {
                std::unique_lock<std::mutex> lock(frameMutex);
                lastColorFrame = colorFrame;
            }
        }
    });

    std::cout << "Streaming... Press Ctrl+C to stop" << std::endl;

    // For now, send raw frames via UDP (simple approach)
    // TODO: Add H.264 encoding with FFmpeg

    while(true) {
        std::shared_ptr<ob::Frame> colorFrame;
        {
            std::unique_lock<std::mutex> lock(frameMutex);
            colorFrame = lastColorFrame;
        }

        if (colorFrame) {
            auto cf = colorFrame->as<ob::ColorFrame>();
            if (cf && cf->data()) {
                cv::Mat frame(cf->height(), cf->width(), CV_8UC3, (void*)cf->data());
                cv::Mat bgrFrame;
                cv::cvtColor(frame, bgrFrame, cv::COLOR_RGB2BGR);

                // Encode as JPEG and send
                std::vector<uchar> buffer;
                cv::imencode(".jpg", bgrFrame, buffer);

                sendto(sockfd, buffer.data(), buffer.size(), 0,
                       (const struct sockaddr*)&servaddr, sizeof(servaddr));
            }
        }

        usleep(33000);  // ~30 FPS
    }

    return 0;
}
