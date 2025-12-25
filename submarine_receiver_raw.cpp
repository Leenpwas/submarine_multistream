#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <chrono>
#include <cmath>

// UDP Receiver - gets raw depth only
class UDPReceiver {
private:
    int sockfd;
    struct sockaddr_in servaddr;

public:
    UDPReceiver(int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(port);

        if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        // Set timeout
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~UDPReceiver() {
        close(sockfd);
    }

    bool receivePacket(std::vector<uchar>& buffer, int& frame_id) {
        const int maxSize = 2000000;  // 2MB max for compressed frame
        std::vector<char> tempBuf(maxSize);
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);

        int n = recvfrom(sockfd, tempBuf.data(), maxSize, 0,
                        (struct sockaddr*)&clientaddr, &len);

        if (n < 8) {  // Minimum header size
            return false;
        }

        // Parse header: [frame_id(4)][data_size(4)]
        frame_id = *(int*)&tempBuf[0];
        int data_size = *(int*)&tempBuf[4];

        // Validate
        if (data_size <= 0 || data_size > 2000000) {
            return false;
        }

        // Extract PNG data (starts after header, at offset 8)
        buffer.assign(tempBuf.begin() + 8, tempBuf.begin() + 8 + data_size);

        return true;
    }
};

// 2D Mapper - generates top-down map from depth
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

    void update(const cv::Mat& depthFrame, float value_scale) {
        uint32_t depth_width = depthFrame.cols;
        uint32_t depth_height = depthFrame.rows;
        const uint16_t* depth_data = (const uint16_t*)depthFrame.data;

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

                float depth_m = (depth_val * value_scale) / 1000.0f;
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

// 3D Point Cloud Viewer
class PointCloudViewer {
private:
    float yaw = 0.0f;
    float pitch = 0.5f;
    float zoom = 150.0f;
    int width, height;

public:
    PointCloudViewer(int w, int h) : width(w), height(h) {}

    void rotate(float dy, float dp) {
        yaw += dy;
        pitch += dp;
        pitch = std::max(-1.5f, std::min(1.5f, pitch));
    }

    void setZoom(float z) {
        zoom = std::max(50.0f, std::min(500.0f, z));
    }

    float getZoom() const {
        return zoom;
    }

    cv::Mat project(const cv::Mat& depthFrame) {
        cv::Mat display(height, width, CV_8UC3);
        display.setTo(cv::Scalar(20, 20, 30));

        uint32_t depth_width = depthFrame.cols;
        uint32_t depth_height = depthFrame.rows;
        const uint16_t* depth_data = (const uint16_t*)depthFrame.data;

        float cx = width / 2.0f;
        float cy = height / 2.0f;

        // Rotation matrices
        float cyaw = cos(yaw), syaw = sin(yaw);
        float cpitch = cos(pitch), spitch = sin(pitch);

        int pointCount = 0;

        // Sample every 3rd pixel for performance
        for (uint32_t y = 0; y < depth_height; y += 3) {
            for (uint32_t x = 0; x < depth_width; x += 3) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0 || depth_val > 5000) continue;

                float depth = depth_val / 1000.0f;  // Convert to meters

                // Camera coordinates (assuming ~60 degree FOV)
                float fx = depth * tan((x - depth_width/2.0f) / depth_width * 1.0f);
                float fy = -depth;
                float fz = depth * tan((y - depth_height/2.0f) / depth_width * 1.0f);

                // Rotate
                float rx = fx * cyaw - fy * syaw;
                float ry = fx * syaw * cpitch + fy * cyaw * cpitch - fz * spitch;
                float rz = fx * syaw * spitch + fy * cyaw * spitch + fz * cpitch;

                // Move back
                rz += 2.0f;

                if (rz <= 0.1f) continue;

                // Project
                int px = (int)(cx + rx / rz * zoom);
                int py = (int)(cy + ry / rz * zoom);

                if (px >= 0 && px < width && py >= 0 && py < height) {
                    // Color by depth
                    float normalizedDepth = std::min(1.0f, depth / 4.0f);
                    cv::Vec3b color;
                    color[0] = (int)(normalizedDepth * 255);       // Blue = far
                    color[1] = (int)((1 - normalizedDepth) * 200); // Green
                    color[2] = (int)((1 - normalizedDepth) * 100); // Red = close

                    display.at<cv::Vec3b>(py, px) = color;
                    pointCount++;
                }
            }
        }

        return display;
    }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        std::cout << "Receives RAW depth, generates: Depth Vis + 2D Map + 3D" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== Submarine RAW Depth Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "\nWaiting for raw depth data..." << std::endl;
    std::cout << "Controls: Arrow keys to rotate 3D, +/- to zoom, ESC to exit\n" << std::endl;

    try {
        UDPReceiver receiver(port);

        Map2D mapper;
        PointCloudViewer viewer3D(1280, 360);

        int frameCount = 0;
        int lastDebugCount = 0;

        cv::Mat display(720, 1280, CV_8UC3);
        cv::Mat lastDepth;
        auto lastFrameTime = std::chrono::steady_clock::now();

        while (true) {
            std::vector<uchar> pngData;
            int frame_id;

            if (receiver.receivePacket(pngData, frame_id)) {
                // Decode PNG to get raw 16-bit depth
                cv::Mat decodedDepth = cv::imdecode(pngData, cv::IMREAD_UNCHANGED);

                if (!decodedDepth.empty()) {
                    lastDepth = decodedDepth;
                    lastFrameTime = std::chrono::steady_clock::now();
                    frameCount++;

                    if (frameCount - lastDebugCount >= 60) {
                        std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                        lastDebugCount = frameCount;
                    }
                }
            }

            // Generate all views from raw depth
            display.setTo(cv::Scalar(50, 50, 50));

            bool hasDepth = !lastDepth.empty();

            // Check if depth is too old
            if (hasDepth) {
                auto now = std::chrono::steady_clock::now();
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
                if (age > 2000) {
                    hasDepth = false;
                }
            }

            int frameWidth = 640;
            int frameHeight = 360;

            // Top-left: Depth visualization
            try {
                if (hasDepth && lastDepth.cols > 0) {
                    cv::Mat depthVis;
                    lastDepth.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                    cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);

                    cv::Mat depthResized;
                    cv::resize(depthVis, depthResized, cv::Size(frameWidth, frameHeight));

                    if (depthResized.cols == frameWidth && depthResized.rows == frameHeight) {
                        depthResized.copyTo(display(cv::Rect(0, 0, frameWidth, frameHeight)));
                        cv::putText(display, "DEPTH", cv::Point(10, 30),
                                   cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                    }
                } else {
                    cv::putText(display, "NO DEPTH", cv::Point(170, 200),
                               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
                }
            } catch (...) {}

            // Top-right: 2D Map
            try {
                if (hasDepth && lastDepth.cols > 0) {
                    // Update 2D map from raw depth (Orbbec value scale is typically 1.0)
                    mapper.update(lastDepth, 1.0f);
                    cv::Mat mapMat = mapper.getMat();

                    cv::Mat mapResized;
                    cv::resize(mapMat, mapResized, cv::Size(frameWidth, frameHeight));

                    if (mapResized.cols == frameWidth && mapResized.rows == frameHeight) {
                        mapResized.copyTo(display(cv::Rect(frameWidth, 0, frameWidth, frameHeight)));
                        cv::putText(display, "2D MAP", cv::Point(frameWidth + 10, 30),
                                   cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                    }
                } else {
                    cv::putText(display, "NO MAP", cv::Point(frameWidth + 170, 200),
                               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
                }
            } catch (...) {}

            // Bottom: 3D Point Cloud (full width)
            try {
                if (hasDepth && lastDepth.cols > 0) {
                    cv::Mat cloud3D = viewer3D.project(lastDepth);

                    if (cloud3D.cols == 1280 && cloud3D.rows == 360) {
                        cloud3D.copyTo(display(cv::Rect(0, 360, 1280, 360)));
                        cv::putText(display, "3D POINT CLOUD", cv::Point(10, 390),
                                   cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 255), 2);

                        // Instructions
                        cv::putText(display, "Arrows:Rotate  +/-:Zoom", cv::Point(10, 700),
                                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(200, 200, 200), 1);
                    }
                } else {
                    cv::putText(display, "NO 3D DATA", cv::Point(500, 540),
                               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
                }
            } catch (...) {}

            cv::imshow("Submarine: Depth + 2D Map + 3D (from RAW)", display);

            int key = cv::waitKey(1);
            if (key == 27) {  // ESC
                break;
            } else if (key == 81) {  // Left
                viewer3D.rotate(-0.1f, 0);
            } else if (key == 83) {  // Right
                viewer3D.rotate(0.1f, 0);
            } else if (key == 82) {  // Up
                viewer3D.rotate(0, -0.1f);
            } else if (key == 84) {  // Down
                viewer3D.rotate(0, 0.1f);
            } else if (key == 43 || key == 171) {  // +
                viewer3D.setZoom(viewer3D.getZoom() + 25);
            } else if (key == 45 || key == 173) {  // -
                viewer3D.setZoom(viewer3D.getZoom() - 25);
            }
        }

        cv::destroyAllWindows();
    }
    catch(std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== Exiting ===" << std::endl;
    return 0;
}
