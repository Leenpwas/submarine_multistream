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

// Frame types (must match sender)
enum FrameType {
    FRAME_COLOR = 0,
    FRAME_DEPTH = 1,
    FRAME_IR = 2,
    FRAME_MAP = 3
};

// TCP Receiver class
class TCPReceiver {
private:
    int client_sock;
    std::mutex recvMutex;

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

    bool receiveFrame(cv::Mat& frame, int& frame_type) {
        int32_t header[3];

        // Receive header (12 bytes)
        ssize_t total_received = 0;
        while (total_received < (ssize_t)sizeof(header)) {
            ssize_t n = recv(client_sock, ((char*)header) + total_received,
                            sizeof(header) - total_received, 0);
            if (n <= 0) return false;  // Error or timeout
            total_received += n;
        }

        int frame_id = header[0];
        frame_type = header[1];
        int data_size = header[2];

        // Validate
        if (frame_type < 0 || frame_type > 3 || data_size <= 0 || data_size > 2000000) {
            return false;
        }

        // Receive image data
        std::vector<uchar> buffer(data_size);
        total_received = 0;
        while (total_received < data_size) {
            ssize_t n = recv(client_sock, (char*)buffer.data() + total_received,
                            data_size - total_received, 0);
            if (n <= 0) return false;
            total_received += n;
        }

        // Decode JPEG
        cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (decoded.empty()) return false;

        frame = decoded;
        return true;
    }
};

// Frame buffer with timeout
class FrameBuffer {
private:
    struct TimedFrame {
        cv::Mat frame;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::map<int, TimedFrame> frames;
    std::mutex mutex;
    const int timeout_ms = 2000;  // 2 second timeout (increased for TCP)

public:
    void update(int frameType, const cv::Mat& frame) {
        std::unique_lock<std::mutex> lock(mutex);
        frames[frameType] = {frame.clone(), std::chrono::steady_clock::now()};
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
};

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

    std::cout << "=== Submarine TCP Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "\nWaiting for sender to connect...\n" << std::endl;

    // Accept connection
    TCPReceiver receiver(server_sock);
    close(server_sock);

    std::cout << "Sender connected!" << std::endl;
    std::cout << "\n=== Receiving Streams ===" << std::endl;
    std::cout << "Press ESC to exit\n" << std::endl;

    FrameBuffer frameBuffer;
    int frameCount = 0;
    int lastDebugCount = 0;

    while (true) {
        cv::Mat frame;
        int frameType;

        if (receiver.receiveFrame(frame, frameType)) {
            std::string typeName;
            switch(frameType) {
                case 0: typeName = "COLOR"; break;
                case 1: typeName = "DEPTH"; break;
                case 2: typeName = "IR"; break;
                case 3: typeName = "MAP"; break;
                default: typeName = "?"; break;
            }

            frameBuffer.update(frameType, frame);
            frameCount++;

            if (frameCount - lastDebugCount >= 30) {
                std::cout << "✓ Received frame " << frameCount << " (" << typeName << ")" << std::endl;
                lastDebugCount = frameCount;
            }
        }

        // Create display
        cv::Mat color = frameBuffer.get(FRAME_COLOR);
        cv::Mat depth = frameBuffer.get(FRAME_DEPTH);
        cv::Mat ir = frameBuffer.get(FRAME_IR);
        cv::Mat map = frameBuffer.get(FRAME_MAP);

        cv::Mat display(720, 1280, CV_8UC3);
        display.setTo(cv::Scalar(50, 50, 50));

        int frameWidth = 640;
        int frameHeight = 360;

        // Top-left: Color
        if (!color.empty()) {
            cv::Mat colorResized;
            cv::resize(color, colorResized, cv::Size(frameWidth, frameHeight));
            colorResized.copyTo(display(cv::Rect(0, 0, frameWidth, frameHeight)));
            cv::putText(display, "COLOR", cv::Point(10, 30),
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(display, "NO COLOR SIGNAL", cv::Point(150, 200),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
        }

        // Top-right: Depth
        if (!depth.empty()) {
            cv::Mat depthResized;
            cv::resize(depth, depthResized, cv::Size(frameWidth, frameHeight));
            depthResized.copyTo(display(cv::Rect(frameWidth, 0, frameWidth, frameHeight)));
            cv::putText(display, "DEPTH", cv::Point(frameWidth + 10, 30),
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(display, "NO DEPTH SIGNAL", cv::Point(frameWidth + 150, 200),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
        }

        // Bottom-left: IR
        if (!ir.empty()) {
            cv::Mat irResized;
            cv::resize(ir, irResized, cv::Size(frameWidth, frameHeight));
            irResized.copyTo(display(cv::Rect(0, frameHeight, frameWidth, frameHeight)));
            cv::putText(display, "IR", cv::Point(10, frameHeight + 30),
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(display, "NO IR SIGNAL", cv::Point(200, frameHeight + 200),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
        }

        // Bottom-right: Map
        if (!map.empty()) {
            cv::Mat mapResized;
            cv::resize(map, mapResized, cv::Size(frameWidth, frameHeight));
            mapResized.copyTo(display(cv::Rect(frameWidth, frameHeight, frameWidth, frameHeight)));
            cv::putText(display, "2D MAP", cv::Point(frameWidth + 10, frameHeight + 30),
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
        } else {
            cv::putText(display, "NO MAP SIGNAL", cv::Point(frameWidth + 150, frameHeight + 200),
                       cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
        }

        cv::imshow("Submarine Vision System (TCP)", display);

        if (cv::waitKey(1) == 27) break;
    }

    cv::destroyAllWindows();
    std::cout << "\n=== Exiting ===" << std::endl;
    return 0;
}
