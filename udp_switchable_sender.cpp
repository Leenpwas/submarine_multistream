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

enum StreamCommand {
    CMD_COLOR = 1,
    CMD_DEPTH = 2
};

// 2D Mapper
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

        std::fill(map_image.begin(), map_image.end(), 255);

        // Grid
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

        // Center line
        int center_x = width / 2;
        for (int y = 0; y < height; y++) {
            int idx = (y * width + center_x) * 3;
            map_image[idx] = 255;
            map_image[idx+1] = map_image[idx+2] = 0;
        }

        // Project depth
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

        // Robot icon
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

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== UDP Switchable Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Create UDP socket for receiving commands
    int cmd_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in cmd_addr;
    memset(&cmd_addr, 0, sizeof(cmd_addr));
    cmd_addr.sin_family = AF_INET;
    cmd_addr.sin_addr.s_addr = INADDR_ANY;
    cmd_addr.sin_port = htons(port + 1);  // Listen for commands on port+1
    bind(cmd_sock, (const struct sockaddr*)&cmd_addr, sizeof(cmd_addr));

    // Create UDP socket for sending video
    int send_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in send_addr;
    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(port);
    inet_pton(AF_INET, receiver_ip.c_str(), &send_addr.sin_addr);

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

    Map2D mapper;

    std::cout << "\n=== Streaming Started ===" << std::endl;
    std::cout << "Waiting for commands from receiver..." << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    StreamCommand currentMode = CMD_COLOR;
    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    while(true) {
        // Check for command (non-blocking)
        uint8_t cmd;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t n = recvfrom(cmd_sock, &cmd, 1, MSG_DONTWAIT,
                           (struct sockaddr*)&from_addr, &from_len);

        if (n > 0) {
            currentMode = (StreamCommand)cmd;
            std::cout << "Switched to " << (currentMode == CMD_COLOR ? "COLOR" : "DEPTH") << " mode" << std::endl;
        }

        // Send frames at ~20 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 50) {
            if (currentMode == CMD_COLOR) {
                // Send COLOR
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
                                // Encode and send
                                std::vector<uchar> buffer;
                                std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
                                cv::imencode(".jpg", colorMat, buffer, params);

                                // Header: [frame_id(4)][frame_type(4)][data_size(4)][data]
                                int total_size = 12 + buffer.size();
                                std::vector<char> packet(total_size);
                                *(int*)&packet[0] = frame_id++;
                                *(int*)&packet[4] = 0;  // COLOR
                                *(int*)&packet[8] = buffer.size();
                                memcpy(&packet[12], buffer.data(), buffer.size());

                                sendto(send_sock, packet.data(), packet.size(), 0,
                                       (const struct sockaddr*)&send_addr, sizeof(send_addr));
                            }
                        }
                    } catch (...) {}
                }

            } else {  // CMD_DEPTH
                // Send DEPTH + MAP
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
                            cv::Mat temp(df->height(), df->width(), CV_16UC1, (void*)df->data());
                            cv::Mat depthMat = temp.clone();

                            // Send colored depth
                            cv::Mat depthVis;
                            depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                            cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                            if (!depthVis.empty()) {
                                std::vector<uchar> buffer;
                                cv::imencode(".jpg", depthVis, buffer);

                                int total_size = 12 + buffer.size();
                                std::vector<char> packet(total_size);
                                *(int*)&packet[0] = frame_id++;
                                *(int*)&packet[4] = 1;  // DEPTH
                                *(int*)&packet[8] = buffer.size();
                                memcpy(&packet[12], buffer.data(), buffer.size());

                                sendto(send_sock, packet.data(), packet.size(), 0,
                                       (const struct sockaddr*)&send_addr, sizeof(send_addr));
                            }

                            // Send 2D map
                            mapper.update(depthFrameData);
                            cv::Mat mapMat = mapper.getMat();
                            if (!mapMat.empty()) {
                                std::vector<uchar> buffer;
                                cv::imencode(".jpg", mapMat, buffer);

                                int total_size = 12 + buffer.size();
                                std::vector<char> packet(total_size);
                                *(int*)&packet[0] = frame_id++;
                                *(int*)&packet[4] = 2;  // MAP
                                *(int*)&packet[8] = buffer.size();
                                memcpy(&packet[12], buffer.data(), buffer.size());

                                sendto(send_sock, packet.data(), packet.size(), 0,
                                       (const struct sockaddr*)&send_addr, sizeof(send_addr));
                            }

                            // Send raw depth for 3D (PNG compressed)
                            std::vector<uchar> pngBuffer;
                            std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
                            cv::imencode(".png", depthMat, pngBuffer, params);

                            if (!pngBuffer.empty()) {
                                int total_size = 12 + pngBuffer.size();
                                std::vector<char> packet(total_size);
                                *(int*)&packet[0] = frame_id++;
                                *(int*)&packet[4] = 3;  // RAW DEPTH for 3D
                                *(int*)&packet[8] = pngBuffer.size();
                                memcpy(&packet[12], pngBuffer.data(), pngBuffer.size());

                                sendto(send_sock, packet.data(), packet.size(), 0,
                                       (const struct sockaddr*)&send_addr, sizeof(send_addr));
                            }
                        }
                    } catch (...) {}
                }
            }

            last_send_time = now;
        }

        usleep(10000);
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
