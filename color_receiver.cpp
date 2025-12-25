#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <chrono>

// UDP Receiver for Color stream
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

    bool receiveFrame(cv::Mat& frame, int& frame_id) {
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
        if (data_size <= 0 || data_size > 2000000) {
            return false;
        }

        // Extract image data
        std::vector<uchar> jpegData(buffer + 8, buffer + n);

        // Decode JPEG
        cv::Mat decoded = cv::imdecode(jpegData, cv::IMREAD_COLOR);

        if (decoded.empty()) {
            return false;
        }

        frame = decoded;
        return true;
    }
};

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 5001" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);

    std::cout << "=== Color Stream Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;

    try {
        UDPReceiver receiver(port);

        std::cout << "\n=== Receiving Color Stream ===" << std::endl;
        std::cout << "Waiting for data from sender..." << std::endl;
        std::cout << "Press ESC to exit\n" << std::endl;

        int frameCount = 0;
        cv::Mat lastFrame;
        auto lastFrameTime = std::chrono::steady_clock::now();

        while (true) {
            cv::Mat frame;
            int frame_id;

            if (receiver.receiveFrame(frame, frame_id)) {
                lastFrame = frame;
                lastFrameTime = std::chrono::steady_clock::now();
                frameCount++;

                if (frameCount % 60 == 0) {
                    std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                }
            }

            // Display last frame or "NO SIGNAL"
            cv::Mat display;
            if (!lastFrame.empty()) {
                // Check if frame is too old (>1 second)
                auto now = std::chrono::steady_clock::now();
                auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();

                if (age < 1000) {
                    display = lastFrame;
                    cv::putText(display, "COLOR STREAM - LIVE",
                               cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                               1, cv::Scalar(0, 255, 0), 2);
                } else {
                    display = cv::Mat::zeros(480, 640, CV_8UC3);
                    cv::putText(display, "NO SIGNAL",
                               cv::Point(200, 240), cv::FONT_HERSHEY_SIMPLEX,
                               1.5, cv::Scalar(100, 100, 100), 3);
                }
            } else {
                display = cv::Mat::zeros(480, 640, CV_8UC3);
                cv::putText(display, "WAITING FOR STREAM...",
                           cv::Point(120, 240), cv::FONT_HERSHEY_SIMPLEX,
                           1, cv::Scalar(150, 150, 150), 2);
            }

            cv::imshow("Color Stream Receiver", display);

            if (cv::waitKey(1) == 27) {
                break;
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
