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
#include <thread>
#include <vector>
#include <cmath>
#include <iostream>
#include <cstring>

// 2D Mapper (simplified)
class Map2D {
private:
    cv::Mat map_image;

public:
    Map2D() {
        map_image = cv::Mat::zeros(480, 640, CV_8UC3);
        map_image.setTo(cv::Scalar(255, 255, 255));
    }

    void update(std::shared_ptr<ob::DepthFrame> depthFrame) {
        uint32_t depth_width = depthFrame->width();
        uint32_t depth_height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        const uint16_t* depth_data = (const uint16_t*)depthFrame->data();

        map_image.setTo(cv::Scalar(255, 255, 255));

        // Draw grid
        for (int y = 0; y < 480; y += 50)
            cv::line(map_image, cv::Point(0, y), cv::Point(640, y), cv::Scalar(200, 200, 200), 1);
        for (int x = 0; x < 640; x += 50)
            cv::line(map_image, cv::Point(x, 0), cv::Point(x, 480), cv::Scalar(200, 200, 200), 1);

        // Center line
        cv::line(map_image, cv::Point(320, 0), cv::Point(320, 480), cv::Scalar(0, 255, 0), 2);

        // Project depth
        float max_range = 4.0f;
        for (uint32_t y = 0; y < depth_height; y += 4) {
            for (uint32_t x = 0; x < depth_width; x += 4) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.2f) continue;

                float fov = 60.0f * M_PI / 180.0f;
                float angle = (x - depth_width/2.0f) / depth_width * fov;
                float x_pos = depth_m * tan(angle);

                int map_x = 320 + (int)(x_pos * 100);
                int map_y = (int)(depth_m * 100);

                if (map_x >= 0 && map_x < 640 && map_y >= 0 && map_y < 480) {
                    float intensity = 1.0f - (depth_m / max_range);
                    cv::circle(map_image, cv::Point(map_x, map_y), 2,
                              cv::Scalar((int)(intensity*200), 0, (int)((1-intensity)*100)), -1);
                }
            }
        }

        // Robot icon
        cv::circle(map_image, cv::Point(320, 470), 8, cv::Scalar(0, 0, 255), -1);
    }

    cv::Mat getMat() { return map_image; }
};

// Frame types
enum FrameType {
    FRAME_COLOR = 0,
    FRAME_DEPTH = 1,
    FRAME_IR = 2,
    FRAME_MAP = 3
};

// TCP Sender class
class TCPSender {
private:
    int sockfd;
    struct sockaddr_in servaddr;

public:
    TCPSender(const std::string& ip, int port) {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);

        // Connect to receiver
        std::cout << "Connecting to " << ip << ":" << port << "..." << std::flush;
        if (connect(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            throw std::runtime_error("Connection failed - make sure receiver is running first!");
        }
        std::cout << " Connected!" << std::endl;
    }

    ~TCPSender() {
        close(sockfd);
    }

    void sendFrame(const cv::Mat& frame, int frame_id, int frame_type) {
        if (frame.empty()) return;

        // Encode as JPEG
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
        cv::imencode(".jpg", frame, buffer, params);

        // Create header: [frame_id(4)][frame_type(4)][data_size(4)]
        int32_t header[3];
        header[0] = frame_id;
        header[1] = frame_type;
        header[2] = buffer.size();

        // Send header
        if (send(sockfd, header, sizeof(header), 0) != sizeof(header)) {
            return;  // Send failed
        }

        // Send image data
        size_t total_sent = 0;
        while (total_sent < buffer.size()) {
            ssize_t sent = send(sockfd, buffer.data() + total_sent,
                              buffer.size() - total_sent, 0);
            if (sent <= 0) return;  // Error or disconnect
            total_sent += sent;
        }
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Submarine TCP Sender ===" << std::endl;
    std::cout << "Receiver IP: " << ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Initialize Orbbec pipeline
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable all video streams
    config->enableVideoStream(OB_STREAM_COLOR);
    config->enableVideoStream(OB_STREAM_DEPTH);
    config->enableVideoStream(OB_STREAM_IR);

    std::mutex frameMutex;
    std::map<OBFrameType, std::shared_ptr<ob::Frame>> frameMap;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        auto count = frameset->frameCount();
        for(int i = 0; i < count; i++) {
            auto frame = frameset->getFrame(i);
            std::unique_lock<std::mutex> lk(frameMutex);
            frameMap[frame->type()] = frame;

            if(frame->type() == OB_FRAME_DEPTH) {
                lastDepthFrame = frame->as<ob::DepthFrame>();
            }
        }
    });

    // Create TCP sender (this will connect to receiver)
    TCPSender sender(ip, port);
    Map2D mapper;
    int frame_id = 0;

    std::cout << "\n=== Sending Streams ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(2);  // Wait for first frames

    int totalFrames = 0;
    int colorCount = 0, depthCount = 0, irCount = 0, mapCount = 0;

    while(true) {
        std::shared_ptr<ob::Frame> colorFrame, depthFrame, irFrame;

        {
            std::unique_lock<std::mutex> lock(frameMutex);
            auto it = frameMap.find(OB_FRAME_COLOR);
            if (it != frameMap.end()) colorFrame = it->second;

            it = frameMap.find(OB_FRAME_DEPTH);
            if (it != frameMap.end()) depthFrame = it->second;

            it = frameMap.find(OB_FRAME_IR);
            if (it != frameMap.end()) irFrame = it->second;
        }

        // Send Color
        if (colorFrame) {
            try {
                auto cf = colorFrame->as<ob::ColorFrame>();
                if (cf && cf->data()) {
                    cv::Mat temp(cf->height(), cf->width(), CV_8UC3, (void*)cf->data());
                    cv::Mat colorMat = temp.clone();
                    cv::cvtColor(colorMat, colorMat, cv::COLOR_RGB2BGR);
                    if (!colorMat.empty()) {
                        sender.sendFrame(colorMat, frame_id++, FRAME_COLOR);
                        colorCount++;
                        totalFrames++;
                    }
                }
            } catch (...) {}
        }

        // Send Depth
        if (depthFrame) {
            try {
                auto df = depthFrame->as<ob::DepthFrame>();
                if (df && df->data()) {
                    cv::Mat temp(df->height(), df->width(), CV_16UC1, (void*)df->data());
                    cv::Mat depthMat = temp.clone();

                    cv::Mat depthVis;
                    depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                    cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                    if (!depthVis.empty()) {
                        sender.sendFrame(depthVis, frame_id++, FRAME_DEPTH);
                        depthCount++;
                        totalFrames++;
                    }
                }
            } catch (...) {}
        }

        // Send IR
        if (irFrame) {
            try {
                auto irf = irFrame->as<ob::IRFrame>();
                if (irf && irf->data()) {
                    cv::Mat temp(irf->height(), irf->width(), CV_8UC1, (void*)irf->data());
                    cv::Mat irMat = temp.clone();
                    cv::cvtColor(irMat, irMat, cv::COLOR_GRAY2BGR);
                    if (!irMat.empty()) {
                        sender.sendFrame(irMat, frame_id++, FRAME_IR);
                        irCount++;
                        totalFrames++;
                    }
                }
            } catch (...) {}
        }

        // Send 2D Map
        if (lastDepthFrame) {
            try {
                mapper.update(lastDepthFrame);
                cv::Mat mapMat = mapper.getMat();
                if (!mapMat.empty()) {
                    sender.sendFrame(mapMat, frame_id++, FRAME_MAP);
                    mapCount++;
                    totalFrames++;
                }
            } catch (...) {}
        }

        // Debug output every second
        static int counter = 0;
        if (++counter >= 100) {
            std::cout << "✓ Total: " << totalFrames << " (C:" << colorCount
                      << " D:" << depthCount << " I:" << irCount << " M:" << mapCount << ")" << std::endl;
            counter = 0;
        }

        usleep(10000);  // ~10ms
    }

    pipe.stop();
    return 0;
}
catch(ob::Error &e) {
    std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << std::endl;
    return -1;
}
catch(std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return -1;
}
