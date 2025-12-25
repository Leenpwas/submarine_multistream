#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>
#include <cmath>

// Frame types (must match sender)
enum FrameType {
    FRAME_DEPTH_VIS = 1,    // Colored depth visualization (JPEG)
    FRAME_2D_MAP = 2,       // 2D navigation map (JPEG)
    FRAME_3D_DEPTH = 3      // Raw depth for point cloud (PNG)
};

// ==================== TCP Receiver Class ====================
class TCPReceiver {
private:
    int client_sock;

public:
    TCPReceiver(int server_sock) {
        // Accept connection
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            throw std::runtime_error("Accept failed");
        }

        // Set timeout for recv (1 second)
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~TCPReceiver() {
        close(client_sock);
    }

    // Generic receive method that handles all frame types
    bool receiveFrame(std::vector<uchar>& data, int& frame_type) {
        int32_t header[3];

        // Receive header (12 bytes)
        ssize_t total_received = 0;
        while (total_received < (ssize_t)sizeof(header)) {
            ssize_t n = recv(client_sock, ((char*)header) + total_received,
                            sizeof(header) - total_received, 0);
            if (n <= 0) return false;  // Error or timeout
            total_received += n;
        }

        frame_type = header[1];
        int data_size = header[2];

        // Validate
        if (data_size <= 0 || data_size > 5000000) {
            return false;
        }

        // Receive image data
        data.resize(data_size);
        total_received = 0;
        while (total_received < data_size) {
            ssize_t n = recv(client_sock, (char*)data.data() + total_received,
                            data_size - total_received, 0);
            if (n <= 0) return false;
            total_received += n;
        }

        return true;
    }
};

// ==================== Frame Buffer Class ====================
class FrameBuffer {
private:
    struct TimedFrame {
        cv::Mat frame;
        std::vector<uchar> raw_data;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::map<int, TimedFrame> frames;
    std::mutex mutex;
    const int timeout_ms = 2000;

public:
    void update(int frameType, const cv::Mat& frame) {
        std::unique_lock<std::mutex> lock(mutex);
        frames[frameType] = {frame.clone(), std::vector<uchar>(), std::chrono::steady_clock::now()};
    }

    void updateRaw(int frameType, const std::vector<uchar>& data) {
        std::unique_lock<std::mutex> lock(mutex);
        frames[frameType] = {cv::Mat(), data, std::chrono::steady_clock::now()};
    }

    cv::Mat get(int frameType) {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = frames.find(frameType);
        if (it != frames.end()) {
            auto now = std::chrono::steady_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.timestamp).count();

            if (age < timeout_ms) {
                return it->second.frame.clone();
            }
        }
        return cv::Mat();
    }

    std::vector<uchar> getRaw(int frameType) {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = frames.find(frameType);
        if (it != frames.end()) {
            auto now = std::chrono::steady_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - it->second.timestamp).count();

            if (age < timeout_ms) {
                return it->second.raw_data;
            }
        }
        return std::vector<uchar>();
    }
};

// ==================== Simple 3D Point Cloud Viewer (Software Rendering) ====================
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

    float getZoom() const { return zoom; }

    cv::Mat project(const cv::Mat& depthFrame) {
        cv::Mat display(height, width, CV_8UC3);
        display.setTo(cv::Scalar(20, 20, 30));

        if (depthFrame.empty()) return display;

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

    int getNumPoints(const cv::Mat& depthFrame) {
        if (depthFrame.empty()) return 0;
        uint32_t depth_width = depthFrame.cols;
        uint32_t depth_height = depthFrame.rows;
        const uint16_t* depth_data = (const uint16_t*)depthFrame.data;

        int count = 0;
        for (uint32_t y = 0; y < depth_height; y += 3) {
            for (uint32_t x = 0; x < depth_width; x += 3) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val > 0 && depth_val <= 5000) count++;
            }
        }
        return count;
    }
};

// ==================== Main Program ====================
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 5000" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    // Create server socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(server_sock, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    listen(server_sock, 1);

    std::cout << "=== Submarine Vision Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "\nWaiting for sender to connect...\n" << std::endl;

    // Accept connection
    TCPReceiver receiver(server_sock);
    close(server_sock);

    std::cout << "Sender connected!" << std::endl;
    std::cout << "\n=== Receiving Streams ===" << std::endl;
    std::cout << "Controls: Arrow keys to rotate 3D view, +/- to zoom" << std::endl;
    std::cout << "Press ESC to exit\n" << std::endl;

    FrameBuffer frameBuffer;
    PointCloudViewer viewer3D(1280, 360);

    int frameCount = 0;
    int lastDebugCount = 0;

    cv::Mat display(720, 1280, CV_8UC3);

    while (true) {
        std::vector<uchar> data;
        int frameType;

        if (receiver.receiveFrame(data, frameType)) {
            if (frameType == FRAME_DEPTH_VIS || frameType == FRAME_2D_MAP) {
                // JPEG encoded frame
                cv::Mat decoded = cv::imdecode(data, cv::IMREAD_COLOR);
                if (!decoded.empty()) {
                    frameBuffer.update(frameType, decoded);
                    frameCount++;

                    if (frameCount - lastDebugCount >= 30) {
                        std::string typeName = (frameType == FRAME_DEPTH_VIS) ? "DEPTH_VIS" : "2D_MAP";
                        std::cout << "✓ Received frame " << frameCount << " (" << typeName << ")" << std::endl;
                        lastDebugCount = frameCount;
                    }
                }
            } else if (frameType == FRAME_3D_DEPTH) {
                // PNG encoded raw depth
                frameBuffer.updateRaw(frameType, data);
                frameCount++;

                // Count points occasionally
                static int count = 0;
                if (++count >= 30) {
                    std::vector<uchar> rawDepth = frameBuffer.getRaw(FRAME_3D_DEPTH);
                    if (!rawDepth.empty()) {
                        cv::Mat depthDecoded = cv::imdecode(rawDepth, cv::IMREAD_UNCHANGED);
                        if (!depthDecoded.empty()) {
                            int points = viewer3D.getNumPoints(depthDecoded);
                            std::cout << "✓ Received frame " << frameCount << " (3D_DEPTH - " << points << " points)" << std::endl;
                            lastDebugCount = frameCount;
                        }
                    }
                    count = 0;
                }
            }
        }

        // Create display
        display.setTo(cv::Scalar(30, 30, 30));

        cv::Mat depthVis = frameBuffer.get(FRAME_DEPTH_VIS);
        cv::Mat map2D = frameBuffer.get(FRAME_2D_MAP);
        std::vector<uchar> rawDepth = frameBuffer.getRaw(FRAME_3D_DEPTH);

        int frameWidth = 640;
        int frameHeight = 360;

        // Top-left: Depth visualization
        try {
            if (!depthVis.empty() && depthVis.cols > 0) {
                cv::Mat depthResized;
                cv::resize(depthVis, depthResized, cv::Size(frameWidth, frameHeight));
                if (depthResized.cols == frameWidth && depthResized.rows == frameHeight) {
                    depthResized.copyTo(display(cv::Rect(0, 0, frameWidth, frameHeight)));
                    cv::putText(display, "DEPTH VISUALIZATION", cv::Point(10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO DEPTH SIGNAL", cv::Point(150, 200),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }
        } catch (...) {}

        // Top-right: 2D Map
        try {
            if (!map2D.empty() && map2D.cols > 0) {
                cv::Mat mapResized;
                cv::resize(map2D, mapResized, cv::Size(frameWidth, frameHeight));
                if (mapResized.cols == frameWidth && mapResized.rows == frameHeight) {
                    mapResized.copyTo(display(cv::Rect(frameWidth, 0, frameWidth, frameHeight)));
                    cv::putText(display, "2D NAVIGATION MAP", cv::Point(frameWidth + 10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO MAP SIGNAL", cv::Point(frameWidth + 150, 200),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }
        } catch (...) {}

        // Bottom: 3D Point Cloud
        try {
            if (!rawDepth.empty()) {
                cv::Mat decodedDepth = cv::imdecode(rawDepth, cv::IMREAD_UNCHANGED);
                if (!decodedDepth.empty() && decodedDepth.cols > 0) {
                    cv::Mat cloud3D = viewer3D.project(decodedDepth);
                    if (!cloud3D.empty() && cloud3D.cols == 1280 && cloud3D.rows == 360) {
                        cloud3D.copyTo(display(cv::Rect(0, 360, 1280, 360)));
                        cv::putText(display, "3D POINT CLOUD", cv::Point(10, 390),
                                   cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);

                        // Instructions
                        cv::putText(display, "Arrows:Rotate  +/-:Zoom", cv::Point(10, 700),
                                   cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(200, 200, 200), 1);
                    }
                }
            } else {
                cv::putText(display, "NO 3D DATA", cv::Point(500, 540),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }
        } catch (...) {}

        cv::imshow("Submarine Vision Receiver", display);

        int key = cv::waitKey(1);
        if (key == 27) {  // ESC
            break;
        } else if (key == 81 || key == 65361 || key == 2424832) {  // Left arrow (multiple possible codes)
            viewer3D.rotate(-0.1f, 0);
            std::cout << "Rotate Left" << std::endl;
        } else if (key == 83 || key == 65363 || key == 2555904) {  // Right arrow
            viewer3D.rotate(0.1f, 0);
            std::cout << "Rotate Right" << std::endl;
        } else if (key == 82 || key == 65362 || key == 2490368) {  // Up arrow
            viewer3D.rotate(0, -0.1f);
            std::cout << "Rotate Up" << std::endl;
        } else if (key == 84 || key == 65364 || key == 2621440) {  // Down arrow
            viewer3D.rotate(0, 0.1f);
            std::cout << "Rotate Down" << std::endl;
        } else if (key == 43 || key == 171 || key == 65451) {  // +
            viewer3D.setZoom(viewer3D.getZoom() + 25);
            std::cout << "Zoom In" << std::endl;
        } else if (key == 45 || key == 173 || key == 65453) {  // -
            viewer3D.setZoom(viewer3D.getZoom() - 25);
            std::cout << "Zoom Out" << std::endl;
        } else if (key > 0) {
            // Debug: show unknown key codes
            static int debugCount = 0;
            if (++debugCount <= 10) {
                std::cout << "Key pressed: " << key << " (try pressing arrow keys)" << std::endl;
            }
        }
    }

    cv::destroyAllWindows();
    std::cout << "\n=== Exiting ===" << std::endl;
    return 0;
}
