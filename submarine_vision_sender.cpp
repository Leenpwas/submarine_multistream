#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Frame.hpp"
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

// 2D Mapper - generates top-down navigation map from depth data
// (Copied from submarine_multistream.cpp with modification to return cv::Mat)
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

    // Return as cv::Mat for easy encoding
    cv::Mat getMat() const {
        return cv::Mat(height, width, CV_8UC3, (void*)map_image.data()).clone();
    }
};

// Frame types for network protocol
enum FrameType {
    FRAME_DEPTH_VIS = 1,    // Colored depth visualization (JPEG)
    FRAME_2D_MAP = 2,       // 2D navigation map (JPEG)
    FRAME_3D_DEPTH = 3      // Raw depth for point cloud (PNG - preserves 16-bit)
};

// TCP Sender class - handles network communication
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

    // Send JPEG-encoded frame (for depth visualization and 2D map)
    void sendJPEGFrame(const cv::Mat& frame, int frame_id, int frame_type) {
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

    // Send PNG-encoded 16-bit depth frame (for 3D point cloud)
    void sendPNGDepth(const cv::Mat& depth_frame, int frame_id, int frame_type) {
        if (depth_frame.empty()) return;

        // Encode as PNG (preserves 16-bit depth data)
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
        cv::imencode(".png", depth_frame, buffer, params);

        // Create header: [frame_id(4)][frame_type(4)][data_size(4)]
        int32_t header[3];
        header[0] = frame_id;
        header[1] = frame_type;
        header[2] = buffer.size();

        // Send header
        if (send(sockfd, header, sizeof(header), 0) != sizeof(header)) {
            return;
        }

        // Send data
        size_t total_sent = 0;
        while (total_sent < buffer.size()) {
            ssize_t sent = send(sockfd, buffer.data() + total_sent,
                              buffer.size() - total_sent, 0);
            if (sent <= 0) return;
            total_sent += sent;
        }
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 192.168.1.100 5000" << std::endl;
        return 1;
    }

    std::string ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Submarine Vision Sender ===" << std::endl;
    std::cout << "Receiver IP: " << ip << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "\nStreams:" << std::endl;
    std::cout << "  1. Depth Visualization (colored depth map)" << std::endl;
    std::cout << "  2. 2D Navigation Map (top-down view)" << std::endl;
    std::cout << "  3. 3D Depth Data (for point cloud)" << std::endl;

    // Initialize Orbbec pipeline
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable only DEPTH stream (all we need for visualization)
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::mutex frameMutex;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        auto count = frameset->frameCount();
        for(int i = 0; i < count; i++) {
            auto frame = frameset->getFrame(i);
            std::unique_lock<std::mutex> lk(frameMutex);

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
    int depthVisCount = 0, mapCount = 0, depth3DCount = 0;

    // Main sending loop at ~20 FPS (50ms intervals)
    while(true) {
        std::shared_ptr<ob::DepthFrame> depthFrame;

        {
            std::unique_lock<std::mutex> lock(frameMutex);
            depthFrame = lastDepthFrame;
        }

        if (depthFrame) {
            try {
                // 1. Generate and send Depth Visualization (colormap)
                cv::Mat temp(depthFrame->height(), depthFrame->width(), CV_16UC1, (void*)depthFrame->data());
                cv::Mat depthMat = temp.clone();

                cv::Mat depthVis;
                depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                if (!depthVis.empty()) {
                    sender.sendJPEGFrame(depthVis, frame_id++, FRAME_DEPTH_VIS);
                    depthVisCount++;
                    totalFrames++;
                }

                // 2. Generate and send 2D Map
                mapper.update(depthFrame);
                cv::Mat mapMat = mapper.getMat();
                if (!mapMat.empty()) {
                    sender.sendJPEGFrame(mapMat, frame_id++, FRAME_2D_MAP);
                    mapCount++;
                    totalFrames++;
                }

                // 3. Send raw depth for 3D point cloud (PNG encoded to preserve 16-bit)
                if (!depthMat.empty()) {
                    sender.sendPNGDepth(depthMat, frame_id++, FRAME_3D_DEPTH);
                    depth3DCount++;
                    totalFrames++;
                }

            } catch (...) {}
        }

        // Debug output every second
        static int counter = 0;
        if (++counter >= 20) {
            std::cout << "✓ Total: " << totalFrames << " (DepthVis:" << depthVisCount
                      << " Map:" << mapCount << " 3D:" << depth3DCount << ")" << std::endl;
            counter = 0;
        }

        usleep(50000);  // ~50ms = 20 FPS
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
