#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>

// Frame types (must match sender)
enum FrameType {
    FRAME_DEPTH_VIS = 1,
    FRAME_2D_MAP = 2,
    FRAME_3D_DEPTH = 3
};

// UDP Receiver
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

    bool receivePacket(std::vector<char>& buffer, int& frame_id, int& frame_type) {
        const int bufferSize = 65536;
        char tempBuf[bufferSize];
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);

        int n = recvfrom(sockfd, tempBuf, bufferSize, 0,
                        (struct sockaddr*)&clientaddr, &len);

        if (n < 12) {  // Minimum header size
            return false;
        }

        // Parse header: [frame_id(4)][frame_type(4)][data_size(4)]
        frame_id = *(int*)&tempBuf[0];
        frame_type = *(int*)&tempBuf[4];
        int data_size = *(int*)&tempBuf[8];

        // Validate
        if (data_size <= 0 || data_size > 5000000) {
            return false;
        }

        // Copy data
        buffer.assign(tempBuf + 12, tempBuf + n);

        return true;
    }
};

// Frame buffer with timeout
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

// Simple 3D Point Cloud Viewer
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
        std::cout << "Displays: Depth + 2D Map + 3D Point Cloud" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== Submarine Depth+3D Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "\nWaiting for data from sender..." << std::endl;
    std::cout << "Controls: Arrow keys to rotate, +/- to zoom, ESC to exit\n" << std::endl;

    try {
        UDPReceiver receiver(port);
        FrameBuffer frameBuffer;
        PointCloudViewer viewer3D(1280, 360);

        int frameCount = 0;
        int lastDebugCount = 0;

        cv::Mat display(720, 1280, CV_8UC3);

        while (true) {
            std::vector<char> buffer;
            int frame_id, frame_type;

            if (receiver.receivePacket(buffer, frame_id, frame_type)) {
                if (frame_type == FRAME_DEPTH_VIS || frame_type == FRAME_2D_MAP) {
                    // JPEG encoded frame
                    std::vector<uchar> jpegData(buffer.begin(), buffer.end());
                    cv::Mat decoded = cv::imdecode(jpegData, cv::IMREAD_COLOR);
                    if (!decoded.empty()) {
                        frameBuffer.update(frame_type, decoded);
                        frameCount++;
                    }
                } else if (frame_type == FRAME_3D_DEPTH) {
                    // PNG encoded raw depth
                    std::vector<uchar> pngData(buffer.begin(), buffer.end());
                    frameBuffer.updateRaw(frame_type, pngData);
                }

                if (frameCount - lastDebugCount >= 60) {
                    std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                    lastDebugCount = frameCount;
                }
            }

            // Create display
            display.setTo(cv::Scalar(50, 50, 50));

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
                    if (depthResized.cols == frameWidth && depthResized.rows == frameHeight &&
                        display.cols >= frameWidth && display.rows >= frameHeight) {
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
                if (!map2D.empty() && map2D.cols > 0) {
                    cv::Mat mapResized;
                    cv::resize(map2D, mapResized, cv::Size(frameWidth, frameHeight));
                    if (mapResized.cols == frameWidth && mapResized.rows == frameHeight &&
                        display.cols >= frameWidth * 2 && display.rows >= frameHeight) {
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
                if (!rawDepth.empty()) {
                    cv::Mat decodedDepth = cv::imdecode(rawDepth, cv::IMREAD_UNCHANGED);
                    if (!decodedDepth.empty() && decodedDepth.cols > 0) {
                        cv::Mat cloud3D = viewer3D.project(decodedDepth);
                        if (!cloud3D.empty() && cloud3D.cols == 1280 && cloud3D.rows == 360 &&
                            display.cols >= 1280 && display.rows >= 720) {
                            cloud3D.copyTo(display(cv::Rect(0, 360, 1280, 360)));
                            cv::putText(display, "3D POINT CLOUD", cv::Point(10, 390),
                                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 255), 2);

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

            cv::imshow("Submarine Vision: Depth + 2D Map + 3D", display);

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
