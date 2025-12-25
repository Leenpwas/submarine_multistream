#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <iostream>
#include <mutex>
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

// 3D Viewer process manager
class Viewer3DManager {
private:
    pid_t pid = 0;
    std::string pythonScript;
    int port;

public:
    Viewer3DManager(int p) : port(p) {
        // Find Python script path
        pythonScript = "/home/leenos/submarine_multistream/submarine_3d_receiver.py";
    }

    void start() {
        if (pid > 0) return;  // Already running

        pid = fork();
        if (pid == 0) {
            // Child process - run 3D viewer
            std::string portStr = std::to_string(port);
            execlp("python3", "python3", pythonScript.c_str(), portStr.c_str(), NULL);
            exit(1);  // Should not reach here
        } else if (pid > 0) {
            std::cout << "Started 3D Viewer (PID: " << pid << ")" << std::endl;
        }
    }

    void stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            usleep(100000);  // Wait 100ms
            // Force kill if still running
            kill(pid, SIGKILL);
            waitpid(pid, NULL, WNOHANG);
            pid = 0;
            std::cout << "Stopped 3D Viewer" << std::endl;
        }
    }

    bool isRunning() {
        if (pid <= 0) return false;
        // Check if process is still alive
        return kill(pid, 0) == 0;
    }

    ~Viewer3DManager() {
        stop();
    }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== UDP Switchable Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "Press:\n  1 - Color mode\n  2 - Depth mode (with 3D)\n  ESC - Exit\n" << std::endl;

    // Create UDP socket for receiving video
    int recv_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    recv_addr.sin_port = htons(port);
    bind(recv_sock, (const struct sockaddr*)&recv_addr, sizeof(recv_addr));

    // Create UDP socket for sending commands
    int cmd_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in send_addr;
    memset(&send_addr, 0, sizeof(send_addr));
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(port + 1);
    inet_pton(AF_INET, "127.0.0.1", &send_addr.sin_addr);

    // Set timeout for recv
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(recv_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    FrameBuffer frameBuffer;
    Viewer3DManager viewer3D(port);
    StreamCommand currentMode = CMD_COLOR;

    // Send initial command
    sendto(cmd_sock, &currentMode, 1, 0, (const struct sockaddr*)&send_addr, sizeof(send_addr));

    cv::Mat display(720, 960, CV_8UC3);
    int frameCount = 0;

    std::cout << "\n=== Receiving Streams ===" << std::endl;

    while (true) {
        // Receive frame
        const int bufferSize = 65536;
        char buffer[bufferSize];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t n = recvfrom(recv_sock, buffer, bufferSize, 0,
                        (struct sockaddr*)&from_addr, &from_len);

        if (n >= 12) {  // Have header
            int frame_id = *(int*)&buffer[0];
            int frame_type = *(int*)&buffer[4];
            int data_size = *(int*)&buffer[8];

            if (data_size > 0 && data_size < 2000000) {
                std::vector<uchar> jpegData(buffer + 12, buffer + n);
                cv::Mat decoded = cv::imdecode(jpegData, cv::IMREAD_COLOR);

                if (!decoded.empty()) {
                    frameBuffer.update(frame_type, decoded);
                    frameCount++;

                    if (frameCount % 60 == 0) {
                        std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                    }
                }
            }
        }

        // Check for mode switch
        int key = cv::waitKey(1) & 0xFF;

        if (key == 27) {  // ESC
            break;
        } else if (key == '1') {
            if (currentMode != CMD_COLOR) {
                currentMode = CMD_COLOR;
                uint8_t cmd = CMD_COLOR;
                sendto(cmd_sock, &cmd, 1, 0, (const struct sockaddr*)&send_addr, sizeof(send_addr));
                std::cout << "Switched to COLOR mode" << std::endl;
                viewer3D.stop();  // Stop 3D viewer
            }
        } else if (key == '2') {
            if (currentMode != CMD_DEPTH) {
                currentMode = CMD_DEPTH;
                uint8_t cmd = CMD_DEPTH;
                sendto(cmd_sock, &cmd, 1, 0, (const struct sockaddr*)&send_addr, sizeof(send_addr));
                std::cout << "Switched to DEPTH mode with 3D visualization" << std::endl;
                viewer3D.start();  // Start 3D viewer
            }
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
                cv::putText(display, "COLOR STREAM (Press 1=Color, 2=Depth+3D)",
                           cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                           1, cv::Scalar(0, 255, 0), 2);
            } else {
                cv::putText(display, "WAITING FOR COLOR...",
                           cv::Point(280, 360), cv::FONT_HERSHEY_SIMPLEX,
                           1, cv::Scalar(150, 150, 150), 2);
                cv::putText(display, "(Press 1 to request Color, 2 for Depth+3D)",
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

            // Instructions with 3D info
            std::string viewerStatus = viewer3D.isRunning() ? "3D: ACTIVE" : "3D: Starting...";
            cv::putText(display, viewerStatus, cv::Point(10, 660),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
            cv::putText(display, "Press 1=Color, 2=Depth",
                       cv::Point(280, 700), cv::FONT_HERSHEY_SIMPLEX,
                       0.8, cv::Scalar(200, 200, 200), 2);
        }

        cv::imshow("Submarine Vision System", display);
    }

    viewer3D.stop();
    cv::destroyAllWindows();
    std::cout << "\n=== Exiting ===" << std::endl;
    return 0;
}
