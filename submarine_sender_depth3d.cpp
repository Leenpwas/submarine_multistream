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

// UDP Sender class
class UDPSender {
private:
    int sockfd;
    struct sockaddr_in servaddr;

public:
    UDPSender(const std::string& ip, int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    }

    ~UDPSender() {
        close(sockfd);
    }

    void sendFrame(const cv::Mat& frame, int frame_id, int frame_type) {
        if (frame.empty()) return;

        // Encode frame as JPEG
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
        cv::imencode(".jpg", frame, buffer, params);

        // Create header: [frame_id(4)][frame_type(4)][data_size(4)]
        int total_size = 12 + buffer.size();
        std::vector<char> packet(total_size);

        // Pack header
        *(int*)&packet[0] = frame_id;
        *(int*)&packet[4] = frame_type;
        *(int*)&packet[8] = buffer.size();

        // Copy image data
        memcpy(&packet[12], buffer.data(), buffer.size());

        // Send packet
        sendto(sockfd, packet.data(), packet.size(), 0,
               (const struct sockaddr*)&servaddr, sizeof(servaddr));
    }

    void sendRawData(const std::vector<uchar>& data, int frame_id, int frame_type) {
        if (data.empty()) return;

        // Create header: [frame_id(4)][frame_type(4)][data_size(4)]
        int total_size = 12 + data.size();
        std::vector<char> packet(total_size);

        // Pack header
        *(int*)&packet[0] = frame_id;
        *(int*)&packet[4] = frame_type;
        *(int*)&packet[8] = data.size();

        // Copy data
        memcpy(&packet[12], data.data(), data.size());

        // Send packet
        sendto(sockfd, packet.data(), packet.size(), 0,
               (const struct sockaddr*)&servaddr, sizeof(servaddr));
    }
};

// Frame type definitions
enum FrameType {
    FRAME_DEPTH_VIS = 1,    // Depth visualization (colored)
    FRAME_2D_MAP = 2,       // 2D top-down map
    FRAME_3D_DEPTH = 3      // Raw depth for 3D point cloud
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Sends: Depth + 2D Map + 3D Point Cloud Data" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Submarine Depth+3D Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Streams: Depth Vis + 2D Map + 3D Data" << std::endl;

    // Initialize UDP sender
    UDPSender sender(receiver_ip, port);

    // Initialize camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable only depth stream
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::mutex frameMutex;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            auto depthFrame = frameset->depthFrame();
            if (depthFrame) {
                std::unique_lock<std::mutex> lock(frameMutex);
                lastDepthFrame = depthFrame->as<ob::DepthFrame>();
            }
        }
    });

    Map2D mapper;
    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    std::cout << "\n=== Sending Streams ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    while(true) {
        // Limit frame rate to ~20 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 50) {
            std::shared_ptr<ob::DepthFrame> depthFrame;

            {
                std::unique_lock<std::mutex> lock(frameMutex);
                depthFrame = lastDepthFrame;
            }

            if (depthFrame) {
                try {
                    if (depthFrame->data()) {
                        cv::Mat temp(depthFrame->width(), depthFrame->height(), CV_16UC1, (void*)depthFrame->data());
                        cv::Mat depthMat = temp.clone();

                        // 1. Send depth visualization (colored)
                        cv::Mat depthVis;
                        depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                        cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                        if (!depthVis.empty()) {
                            sender.sendFrame(depthVis, frame_id, FRAME_DEPTH_VIS);
                        }

                        // 2. Send 2D map
                        mapper.update(depthFrame);
                        cv::Mat mapMat = mapper.getMat();
                        if (!mapMat.empty()) {
                            sender.sendFrame(mapMat, frame_id, FRAME_2D_MAP);
                        }

                        // 3. Send raw depth for 3D (PNG compressed to preserve 16-bit)
                        std::vector<uchar> pngBuffer;
                        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
                        cv::imencode(".png", depthMat, pngBuffer, params);
                        if (!pngBuffer.empty()) {
                            sender.sendRawData(pngBuffer, frame_id, FRAME_3D_DEPTH);
                        }

                        frame_id++;

                        if (frame_id % 60 == 0) {
                            std::cout << "✓ Sent " << frame_id << " frame sets" << std::endl;
                        }
                    }
                } catch (...) {}
            }

            last_send_time = now;
        }

        usleep(10000);  // 10ms
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
