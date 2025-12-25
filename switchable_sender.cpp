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
#include <vector>
#include <cmath>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

// 2D Mapper class (same as before)
class Map2D {
private:
    std::vector<uint8_t> map_image;
    int width = 640;
    int height = 480;
    float max_range = 4.0f;

public:
    Map2D() {
        map_image.resize(width * height * 3, 255);
    }

    void update(std::shared_ptr<ob::DepthFrame> depthFrame) {
        uint32_t depth_width = depthFrame->width();
        uint32_t depth_height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        const uint16_t* depth_data = (const uint16_t*)depthFrame->data();

        // Clear to white
        std::fill(map_image.begin(), map_image.end(), 255);

        // Draw grid lines
        for (int y = 0; y < height; y += 50) {
            for (int x = 0; x < width; x++) {
                int idx = (y * width + x) * 3;
                map_image[idx] = map_image[idx+1] = map_image[idx+2] = 200;
            }
        }
        for (int x = 0; x < width; x += 50) {
            for (int y = 0; y < height; y++) {
                int idx = (y * width + x) * 3;
                map_image[idx] = map_image[idx+1] = map_image[idx+2] = 200;
            }
        }

        // Center line (robot heading)
        int center_x = width / 2;
        for (int y = 0; y < height; y++) {
            int idx = (y * width + center_x) * 3;
            map_image[idx] = 255;
            map_image[idx+1] = map_image[idx+2] = 0;
        }

        // Project depth data to map
        for (uint32_t y = 0; y < depth_height; y += 4) {
            for (uint32_t x = 0; x < depth_width; x += 4) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.2f) continue;

                float fov = 60.0f * M_PI / 180.0f;
                float angle = (x - depth_width/2.0f) / depth_width * fov;
                float x_pos = depth_m * tan(angle);

                int map_x = width/2 + (int)(x_pos * width / (max_range * 2));
                int map_y = (int)(depth_m * height / max_range);

                if (map_x >= 0 && map_x < width && map_y >= 0 && map_y < height) {
                    int idx = (map_y * width + map_x) * 3;
                    float intensity = 1.0f - (depth_m / max_range);
                    map_image[idx] = (uint8_t)(intensity * 200);
                    map_image[idx+1] = 0;
                    map_image[idx+2] = (uint8_t)((1.0f-intensity)*100);
                }
            }
        }

        // Draw robot icon
        int robot_x = width / 2;
        int robot_y = height - 10;
        for (int dy = -5; dy <= 5; dy++) {
            for (int dx = -5; dx <= 5; dx++) {
                if (dx*dx + dy*dy <= 25) {
                    int px = robot_x + dx;
                    int py = robot_y + dy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        int idx = (py * width + px) * 3;
                        map_image[idx] = 0;
                        map_image[idx+1] = 255;
                        map_image[idx+2] = 0;
                    }
                }
            }
        }
    }

    cv::Mat getMat() {
        return cv::Mat(height, width, CV_8UC3, map_image.data()).clone();
    }
};

enum StreamCommand {
    CMD_COLOR = 1,
    CMD_DEPTH = 2
};

// TCP Sender that switches streams based on receiver command
class SwitchableSender {
private:
    std::string receiver_ip;
    int port;
    bool connected;

public:
    int sockfd;  // Public for non-blocking recv and select()

    bool connectToReceiver() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) return false;

        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, receiver_ip.c_str(), &servaddr.sin_addr);

        if (connect(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            close(sockfd);
            return false;
        }
        connected = true;
        return true;
    }

    void sendFrame(const cv::Mat& frame, int frame_type) {
        if (!connected || frame.empty()) return;

        // Encode as JPEG
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
        cv::imencode(".jpg", frame, buffer, params);

        // Header: [frame_type(1)][data_size(4)]
        uint8_t header[5];
        header[0] = (uint8_t)frame_type;
        *(uint32_t*)(header + 1) = buffer.size();

        // Send header
        if (send(sockfd, header, 5, 0) != 5) {
            connected = false;
            return;
        }

        // Send data
        send(sockfd, buffer.data(), buffer.size(), 0);
    }

public:
    SwitchableSender(std::string ip, int p) : receiver_ip(ip), port(p), connected(false) {
        connectToReceiver();
    }

    ~SwitchableSender() {
        if (connected) close(sockfd);
    }

    bool isConnected() const { return connected; }

    bool reconnect() {
        if (connected) close(sockfd);
        return connectToReceiver();
    }

    // Wait for command from receiver
    StreamCommand receiveCommand() {
        if (!connected) return CMD_COLOR;

        uint8_t cmd;
        ssize_t n = recv(sockfd, &cmd, 1, 0);
        if (n <= 0) {
            connected = false;
            return CMD_COLOR;  // Default to color
        }
        return (StreamCommand)cmd;
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Switchable Stream Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Initialize camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();
    config->enableVideoStream(OB_STREAM_COLOR);
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::shared_ptr<ob::Frame> lastColorFrame, lastDepthFrame;
    std::shared_ptr<ob::DepthFrame> lastDepthFrameData;
    std::mutex frameMutex;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            for(int i = 0; i < frameset->frameCount(); i++) {
                auto frame = frameset->getFrame(i);
                std::unique_lock<std::mutex> lk(frameMutex);

                if(frame->type() == OB_FRAME_COLOR) {
                    lastColorFrame = frame;
                } else if(frame->type() == OB_FRAME_DEPTH) {
                    lastDepthFrame = frame;
                    lastDepthFrameData = frame->as<ob::DepthFrame>();
                }
            }
        }
    });

    // 2D Map generator
    Map2D mapper;

    std::cout << "\n=== Waiting for receiver connection... ===" << std::endl;

    SwitchableSender sender(receiver_ip, port);

    StreamCommand currentMode = CMD_COLOR;
    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    std::cout << "Connected! Switching streams based on receiver request..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    while(true) {
        // Check for command from receiver (non-blocking)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sender.sockfd, &readfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        // Try to receive command
        uint8_t cmd;
        ssize_t n = recv(sender.sockfd, &cmd, 1, MSG_DONTWAIT);

        if (n > 0) {
            currentMode = (StreamCommand)cmd;
            std::cout << "Switched to " << (currentMode == CMD_COLOR ? "COLOR" : "DEPTH") << " mode" << std::endl;
        } else if (n < 0) {
            // Connection lost
            std::cout << "Connection lost, reconnecting..." << std::endl;
            if (!sender.reconnect()) {
                sleep(1);
                continue;
            }
        }

        // Limit frame rate to ~20 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 50) {
            if (currentMode == CMD_COLOR) {
                // Send COLOR stream
                std::shared_ptr<ob::Frame> colorFrame;
                {
                    std::unique_lock<std::mutex> lock(frameMutex);
                    colorFrame = lastColorFrame;
                }

                if (colorFrame) {
                    try {
                        auto cf = colorFrame->as<ob::ColorFrame>();
                        if (cf && cf->data()) {
                            const uint8_t* dataPtr = static_cast<const uint8_t*>(cf->data());
                            size_t dataSize = cf->dataSize();
                            std::vector<uchar> jpegData(dataPtr, dataPtr + dataSize);
                            cv::Mat colorMat = cv::imdecode(jpegData, cv::IMREAD_COLOR);

                            if (!colorMat.empty()) {
                                sender.sendFrame(colorMat, 0);  // 0 = COLOR
                            }
                        }
                    } catch (...) {}
                }

            } else {  // CMD_DEPTH
                // Send DEPTH + 2D MAP + RAW DEPTH
                std::shared_ptr<ob::Frame> depthFrame;
                std::shared_ptr<ob::DepthFrame> depthFrameData;
                {
                    std::unique_lock<std::mutex> lock(frameMutex);
                    depthFrame = lastDepthFrame;
                    depthFrameData = lastDepthFrameData;
                }

                if (depthFrame && depthFrameData) {
                    try {
                        auto df = depthFrame->as<ob::DepthFrame>();
                        if (df && df->data()) {
                            // Send colored depth visualization
                            cv::Mat temp(df->height(), df->width(), CV_16UC1, (void*)df->data());
                            cv::Mat depthMat = temp.clone();

                            cv::Mat depthVis;
                            depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                            cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                            if (!depthVis.empty()) {
                                sender.sendFrame(depthVis, 1);  // 1 = DEPTH
                            }

                            // Send 2D map
                            mapper.update(depthFrameData);
                            cv::Mat mapMat = mapper.getMat();
                            if (!mapMat.empty()) {
                                sender.sendFrame(mapMat, 2);  // 2 = MAP
                            }

                            // Send raw depth for 3D
                            std::vector<uchar> pngBuffer;
                            std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
                            cv::imencode(".png", depthMat, pngBuffer, params);

                            // Send raw data directly
                            if (sender.isConnected()) {
                                uint8_t header[5];
                                header[0] = 3;  // 3 = RAW DEPTH
                                *(uint32_t*)(header + 1) = pngBuffer.size();
                                send(sender.sockfd, header, 5, 0);
                                send(sender.sockfd, pngBuffer.data(), pngBuffer.size(), 0);
                            }
                        }
                    } catch (...) {}
                }
            }

            last_send_time = now;
        }

        usleep(10000);  // 10ms
    }

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
