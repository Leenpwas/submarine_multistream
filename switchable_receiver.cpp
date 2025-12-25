#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>

enum StreamCommand {
    CMD_COLOR = 1,
    CMD_DEPTH = 2
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
    const int timeout_ms = 2000;

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

// TCP Receiver with command sending
class SwitchableReceiver {
private:
    int server_sock, client_sock;
    bool connected;

public:
    SwitchableReceiver(int port) : connected(false) {
        server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(port);

        if (bind(server_sock, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            throw std::runtime_error("Bind failed");
        }

        listen(server_sock, 1);
    }

    ~SwitchableReceiver() {
        if (connected) close(client_sock);
        close(server_sock);
    }

    bool waitForConnection() {
        std::cout << "Waiting for sender..." << std::endl;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            return false;
        }

        // Set timeout for recv
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        connected = true;
        std::cout << "Sender connected!" << std::endl;
        return true;
    }

    void sendCommand(StreamCommand cmd) {
        if (!connected) return;
        uint8_t c = (uint8_t)cmd;
        send(client_sock, &c, 1, 0);
    }

    bool receiveFrame(cv::Mat& frame, int& frame_type) {
        if (!connected) return false;

        uint8_t header[5];
        ssize_t n = recv(client_sock, header, 5, 0);

        if (n != 5) {
            if (n == 0) {
                connected = false;
                return false;
            }
            return false;  // Timeout or error
        }

        frame_type = header[0];
        int data_size = *(uint32_t*)(header + 1);

        if (data_size <= 0 || data_size > 5000000) {
            return false;
        }

        // Receive data
        std::vector<uchar> buffer(data_size);
        size_t total_received = 0;

        while (total_received < (size_t)data_size) {
            n = recv(client_sock, (char*)buffer.data() + total_received,
                     data_size - total_received, 0);
            if (n <= 0) {
                connected = false;
                return false;
            }
            total_received += n;
        }

        // Decode JPEG
        cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (decoded.empty()) return false;

        frame = decoded;
        return true;
    }

    int getSocket() const { return client_sock; }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== Switchable Stream Receiver ===" << std::endl;
    std::cout << "Press:\n  1 - Color mode\n  2 - Depth mode\n  ESC - Exit\n" << std::endl;

    try {
        SwitchableReceiver receiver(port);

        if (!receiver.waitForConnection()) {
            return 1;
        }

        FrameBuffer frameBuffer;
        StreamCommand currentMode = CMD_COLOR;  // Start with COLOR
        receiver.sendCommand(currentMode);

        int frameCount = 0;

        cv::Mat display(720, 960, CV_8UC3);

        while (true) {
            cv::Mat frame;
            int frame_type;

            if (receiver.receiveFrame(frame, frame_type)) {
                frameBuffer.update(frame_type, frame);
                frameCount++;

                if (frameCount % 60 == 0) {
                    std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                }
            }

            // Check for mode switch
            int key = cv::waitKey(1) & 0xFF;

            if (key == 27) {  // ESC
                break;
            } else if (key == '1') {
                currentMode = CMD_COLOR;
                receiver.sendCommand(currentMode);
                std::cout << "Switched to COLOR mode" << std::endl;
            } else if (key == '2') {
                currentMode = CMD_DEPTH;
                receiver.sendCommand(currentMode);
                std::cout << "Switched to DEPTH mode" << std::endl;
            }

            // Display based on mode
            display.setTo(cv::Scalar(50, 50, 50));

            if (currentMode == CMD_COLOR) {
                // Show COLOR full screen
                cv::Mat color = frameBuffer.get(0);  // 0 = COLOR
                if (!color.empty()) {
                    cv::Mat colorResized;
                    cv::resize(color, colorResized, cv::Size(960, 720));
                    colorResized.copyTo(display);
                    cv::putText(display, "COLOR STREAM (Press 1=Color, 2=Depth)",
                               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                               1, cv::Scalar(0, 255, 0), 2);
                } else {
                    cv::putText(display, "WAITING FOR COLOR...",
                               cv::Point(280, 360), cv::FONT_HERSHEY_SIMPLEX,
                               1, cv::Scalar(150, 150, 150), 2);
                    cv::putText(display, "(Press 1 to request Color, 2 for Depth)",
                               cv::Point(230, 400), cv::FONT_HERSHEY_SIMPLEX,
                               0.7, cv::Scalar(120, 120, 120), 1);
                }

            } else {  // CMD_DEPTH
                // Show DEPTH + MAP side by side
                cv::Mat depth = frameBuffer.get(1);   // 1 = DEPTH
                cv::Mat map = frameBuffer.get(2);     // 2 = MAP

                int halfW = 480;
                int halfH = 360;

                // Depth (left)
                if (!depth.empty()) {
                    cv::Mat depthResized;
                    cv::resize(depth, depthResized, cv::Size(halfW, halfH));
                    depthResized.copyTo(display(cv::Rect(0, 0, halfW, halfH)));
                    cv::putText(display, "DEPTH", cv::Point(10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                } else {
                    cv::putText(display, "NO DEPTH", cv::Point(170, 200),
                               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
                }

                // 2D Map (right)
                if (!map.empty()) {
                    cv::Mat mapResized;
                    cv::resize(map, mapResized, cv::Size(halfW, halfH));
                    mapResized.copyTo(display(cv::Rect(halfW, 0, halfW, halfH)));
                    cv::putText(display, "2D MAP", cv::Point(halfW + 10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                } else {
                    cv::putText(display, "NO MAP", cv::Point(halfW + 170, 200),
                               cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
                }

                // Instructions at bottom
                cv::putText(display, "Press 1=Color, 2=Depth",
                           cv::Point(280, 600), cv::FONT_HERSHEY_SIMPLEX,
                           0.8, cv::Scalar(200, 200, 200), 2);
            }

            cv::imshow("Submarine Vision System", display);
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
