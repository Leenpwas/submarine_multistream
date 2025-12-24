#include "libobsensor/hpp/Frame.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <cmath>
#include <chrono>

// Frame header structure (must match sender)
struct FrameHeader {
    uint32_t frame_type;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
    uint32_t format;
    float value_scale;
    uint64_t timestamp;
};

// 2D Mapper class
class Map2D {
private:
    cv::Mat map_image;
    float max_range = 4.0f;

public:
    Map2D(int width = 640, int height = 480) : map_image(height, width, CV_8UC3, cv::Scalar(255, 255, 255)) {}

    void update(const uint16_t* depth_data, int depth_width, int depth_height, float scale) {
        map_image.setTo(cv::Scalar(255, 255, 255));

        // Draw grid
        for (int y = 0; y < map_image.rows; y += 50) {
            cv::line(map_image, cv::Point(0, y), cv::Point(map_image.cols, y), cv::Scalar(200, 200, 200), 1);
        }
        for (int x = 0; x < map_image.cols; x += 50) {
            cv::line(map_image, cv::Point(x, 0), cv::Point(x, map_image.rows), cv::Scalar(200, 200, 200), 1);
        }

        // Center line
        int center_x = map_image.cols / 2;
        cv::line(map_image, cv::Point(center_x, 0), cv::Point(center_x, map_image.rows), cv::Scalar(0, 255, 255), 2);

        // Project depth points
        for (int y = 0; y < depth_height; y += 4) {
            for (int x = 0; x < depth_width; x += 4) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.2f) continue;

                float fov = 60.0f * M_PI / 180.0f;
                float angle = (x - depth_width / 2.0f) / depth_width * fov;
                float x_pos = depth_m * tan(angle);

                int map_x = map_image.cols / 2 + (int)(x_pos * map_image.cols / (max_range * 2));
                int map_y = (int)(depth_m * map_image.rows / max_range);

                if (map_x >= 0 && map_x < map_image.cols && map_y >= 0 && map_y < map_image.rows) {
                    float intensity = 1.0f - (depth_m / max_range);
                    cv::Vec3b& color = map_image.at<cv::Vec3b>(map_y, map_x);
                    color[0] = (uint8_t)((1.0f - intensity) * 100);  // B
                    color[1] = 0;                                      // G
                    color[2] = (uint8_t)(intensity * 200);            // R
                }
            }
        }

        // Draw robot position
        cv::circle(map_image, cv::Point(map_image.cols / 2, map_image.rows - 10), 8, cv::Scalar(0, 255, 0), -1);
    }

    cv::Mat& getImage() { return map_image; }

    void saveToFile(const std::string& filename) {
        cv::imwrite(filename, map_image);
    }
};

class CameraReceiver {
private:
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;

public:
    CameraReceiver(int port) {
        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        if (listen(server_sock, 1) < 0) {
            throw std::runtime_error("Listen failed");
        }

        std::cout << "Listening on port " << port << "..." << std::endl;
    }

    bool acceptConnection() {
        socklen_t addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);

        if (client_sock < 0) {
            return false;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Connection from " << client_ip << std::endl;

        return true;
    }

    bool receiveFrame(FrameHeader& header, std::vector<uint8_t>& data) {
        // Receive header
        ssize_t received = recv(client_sock, &header, sizeof(header), MSG_WAITALL);
        if (received != sizeof(header)) {
            return false;
        }

        // Receive data
        data.resize(header.data_size);
        received = 0;
        while (received < (ssize_t)header.data_size) {
            ssize_t n = recv(client_sock, data.data() + received,
                           header.data_size - received, 0);
            if (n <= 0) {
                return false;
            }
            received += n;
        }

        return true;
    }

    ~CameraReceiver() {
        if (client_sock >= 0) close(client_sock);
        if (server_sock >= 0) close(server_sock);
    }
};

// Convert depth frame to color image for visualization
cv::Mat depthToVisual(const uint16_t* depth_data, int width, int height, float scale) {
    cv::Mat visual(height, width, CV_8UC3);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t depth_val = depth_data[y * width + x];
            float depth_m = (depth_val * scale) / 1000.0f;

            if (depth_val == 0 || depth_m > 5.0f) {
                visual.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 0, 0);
            } else {
                // Color gradient: blue (near) to red (far)
                float t = std::min(depth_m / 5.0f, 1.0f);
                visual.at<cv::Vec3b>(y, x) = cv::Vec3b(
                    (uint8_t)(t * 255),      // B
                    (uint8_t)(0),            // G
                    (uint8_t)((1-t) * 255)   // R
                );
            }
        }
    }

    return visual;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 5000" << std::endl;
        return 1;
    }

    int port = std::stoi(argv[1]);

    try {
        std::cout << "=== Camera Receiver (Remote Display) ===" << std::endl;

        CameraReceiver receiver(port);

        std::cout << "Waiting for camera sender to connect..." << std::endl;
        if (!receiver.acceptConnection()) {
            std::cerr << "Failed to accept connection" << std::endl;
            return 1;
        }

        Map2D mapper(640, 480);

        std::cout << "\n=== Receiving Frames ===" << std::endl;
        std::cout << "Windows:" << std::endl;
        std::cout << "  - Color View (RGB camera)" << std::endl;
        std::cout << "  - Depth View (depth camera)" << std::endl;
        std::cout << "  - 2D Map (top-down projection)" << std::endl;
        std::cout << "Press 'q' or ESC to exit\n" << std::endl;

        cv::namedWindow("Color View", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("Depth View", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("2D Map", cv::WINDOW_AUTOSIZE);

        int frame_count = 0;
        auto last_map_update = std::chrono::steady_clock::now();

        // Store latest frames
        cv::Mat latest_color;
        cv::Mat latest_depth_visual;
        bool has_color = false;
        bool has_depth = false;

        while (true) {
            FrameHeader header;
            std::vector<uint8_t> data;

            if (!receiver.receiveFrame(header, data)) {
                std::cerr << "Connection lost" << std::endl;
                break;
            }

            frame_count++;

            // Process color frames
            if (header.frame_type == OB_FRAME_COLOR) {
                // Color format is typically RGB or YUYV
                // Assuming RGB format from Orbbec
                if (header.format == OB_FORMAT_RGB) {
                    latest_color = cv::Mat(header.height, header.width, CV_8UC3, data.data()).clone();
                } else if (header.format == OB_FORMAT_MJPG) {
                    // MJPG compressed format
                    latest_color = cv::imdecode(data, cv::IMREAD_COLOR);
                } else if (header.format == OB_FORMAT_YUYV) {
                    // Convert YUYV to RGB
                    cv::Mat yuyv(header.height, header.width, CV_8UC2, data.data());
                    cv::cvtColor(yuyv, latest_color, cv::COLOR_YUV2RGB_YUYV);
                } else {
                    // Try to interpret as RGB
                    latest_color = cv::Mat(header.height, header.width, CV_8UC3, data.data()).clone();
                }
                has_color = true;

                if (frame_count % 30 == 0) {
                    std::cout << "Color frame " << frame_count << " - " << header.width << "x" << header.height << std::endl;
                }
            }

            // Process depth frames
            if (header.frame_type == OB_FRAME_DEPTH) {
                const uint16_t* depth_data = (const uint16_t*)data.data();

                // Convert depth to visual
                latest_depth_visual = depthToVisual(depth_data, header.width, header.height, header.value_scale);
                has_depth = true;

                // Update 2D map (twice per second)
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_map_update);

                if (elapsed.count() >= 500) {
                    mapper.update(depth_data, header.width, header.height, header.value_scale);
                    mapper.saveToFile("remote_2d_map.png");
                    last_map_update = now;
                }

                if (frame_count % 30 == 0) {
                    std::cout << "Depth frame " << frame_count << " - " << header.width << "x" << header.height << std::endl;
                }
            }

            // Display all available views
            if (has_color && !latest_color.empty()) {
                cv::imshow("Color View", latest_color);
            }
            if (has_depth && !latest_depth_visual.empty()) {
                cv::imshow("Depth View", latest_depth_visual);
                cv::imshow("2D Map", mapper.getImage());
            }

            // Check for quit key
            int key = cv::waitKey(1);
            if (key == 27 || key == 'q') {  // ESC or 'q'
                std::cout << "Exiting..." << std::endl;
                break;
            }
        }

        cv::destroyAllWindows();

    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
