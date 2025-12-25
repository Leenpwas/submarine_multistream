#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <cmath>

// UDP Receiver for 3D depth data
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

    bool receiveRawFrame(cv::Mat& frame, int& frame_id) {
        const int bufferSize = 65536;
        char buffer[bufferSize];
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);

        int n = recvfrom(sockfd, buffer, bufferSize, 0,
                        (struct sockaddr*)&clientaddr, &len);

        if (n < 8) {  // Minimum header size
            return false;
        }

        // Parse header: [frame_id(4)][data_size(4)]
        frame_id = *(int*)&buffer[0];
        int data_size = *(int*)&buffer[4];

        // Validate
        if (data_size <= 0 || data_size > 4000000) {
            return false;
        }

        // Extract PNG data
        std::vector<uchar> pngData(buffer + 8, buffer + n);

        // Decode PNG (16-bit)
        cv::Mat decoded = cv::imdecode(pngData, cv::IMREAD_UNCHANGED);

        if (decoded.empty()) {
            return false;
        }

        frame = decoded;
        return true;
    }
};

// Project 3D point to 2D with rotation
class PointCloudProjector {
private:
    float yaw = 0.0f;
    float pitch = 0.5f;
    float zoom = 150.0f;
    int width, height;

public:
    PointCloudProjector(int w, int h) : width(w), height(h) {}

    void rotate(float dy, float dp) {
        yaw += dy;
        pitch += dp;
        pitch = std::max(-1.5f, std::min(1.5f, pitch));
    }

    void setZoom(float z) {
        zoom = std::max(50.0f, std::min(500.0f, z));
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
        std::cout << "Example: " << argv[0] << " 5004" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== 3D Point Cloud Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;

    try {
        UDPReceiver receiver(port);

        std::cout << "\n=== Receiving 3D Depth Data ===" << std::endl;
        std::cout << "Waiting for data from sender..." << std::endl;
        std::cout << "Controls: Arrow keys to rotate, +/- to zoom, ESC to exit\n" << std::endl;

        int frameCount = 0;
        cv::Mat lastDepth;
        auto lastFrameTime = std::chrono::steady_clock::now();

        PointCloudProjector projector(1280, 720);

        while (true) {
            cv::Mat frame;
            int frame_id;

            if (receiver.receiveRawFrame(frame, frame_id)) {
                lastDepth = frame;
                lastFrameTime = std::chrono::steady_clock::now();
                frameCount++;

                if (frameCount % 40 == 0) {
                    std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                }
            }

            // Display
            cv::Mat display;
            if (!lastDepth.empty()) {
                auto now = std::chrono::steady_clock::now();
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();

                if (age < 1000) {
                    display = projector.project(lastDepth);
                    cv::putText(display, "3D POINT CLOUD - LIVE",
                               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                               1, cv::Scalar(0, 255, 0), 2);
                } else {
                    display = cv::Mat::zeros(720, 1280, CV_8UC3);
                    display.setTo(cv::Scalar(20, 20, 30));
                    cv::putText(display, "NO SIGNAL",
                               cv::Point(500, 360), cv::FONT_HERSHEY_SIMPLEX,
                               1.5, cv::Scalar(100, 100, 100), 3);
                }
            } else {
                display = cv::Mat::zeros(720, 1280, CV_8UC3);
                display.setTo(cv::Scalar(20, 20, 30));
                cv::putText(display, "WAITING FOR STREAM...",
                           cv::Point(350, 360), cv::FONT_HERSHEY_SIMPLEX,
                           1, cv::Scalar(150, 150, 150), 2);
            }

            cv::imshow("3D Point Cloud Receiver", display);

            int key = cv::waitKey(1);
            if (key == 27) {  // ESC
                break;
            } else if (key == 81) {  // Left
                projector.rotate(-0.1f, 0);
            } else if (key == 83) {  // Right
                projector.rotate(0.1f, 0);
            } else if (key == 82) {  // Up
                projector.rotate(0, -0.1f);
            } else if (key == 84) {  // Down
                projector.rotate(0, 0.1f);
            } else if (key == 43 || key == 171) {  // +
                projector.setZoom(175);
            } else if (key == 45 || key == 173) {  // -
                projector.setZoom(125);
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
