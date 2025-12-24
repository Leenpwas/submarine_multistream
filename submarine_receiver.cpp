#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <map>
#include <mutex>

// Frame type definitions (must match sender)
enum FrameType {
    FRAME_COLOR = 0,
    FRAME_DEPTH = 1,
    FRAME_IR = 2,
    FRAME_MAP = 3
};

// UDP Receiver class
class UDPReceiver {
private:
    int sockfd;
    struct sockaddr_in servaddr;
    std::map<int, std::vector<uchar>> frameBuffers;
    std::mutex bufferMutex;

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
    }

    ~UDPReceiver() {
        close(sockfd);
    }

    bool receiveFrame(cv::Mat& frame, int& frame_type) {
        const int bufferSize = 65536;
        char buffer[bufferSize];
        struct sockaddr_in clientaddr;
        socklen_t len = sizeof(clientaddr);

        int n = recvfrom(sockfd, buffer, bufferSize, 0,
                        (struct sockaddr*)&clientaddr, &len);

        if (n < 12) {  // Minimum header size
            return false;
        }

        // Parse header
        int frame_id = *(int*)&buffer[0];
        frame_type = *(int*)&buffer[4];
        int data_size = *(int*)&buffer[8];

        // Validate frame_type
        if (frame_type < 0 || frame_type > 3) {
            return false;
        }

        // Extract image data
        std::vector<uchar> jpegData(buffer + 12, buffer + n);

        // Decode JPEG
        cv::Mat decoded = cv::imdecode(jpegData, cv::IMREAD_COLOR);

        if (decoded.empty()) {
            return false;
        }

        // Validate decoded image size
        if (decoded.cols <= 0 || decoded.rows <= 0 || decoded.cols > 2000 || decoded.rows > 2000) {
            return false;
        }

        frame = decoded;
        return true;
    }
};

// Frame buffer for display synchronization
class FrameBuffer {
private:
    std::map<int, cv::Mat> frames;
    std::mutex mutex;

public:
    void update(int frameType, const cv::Mat& frame) {
        std::unique_lock<std::mutex> lock(mutex);
        frames[frameType] = frame.clone();
    }

    cv::Mat get(int frameType) {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = frames.find(frameType);
        if (it != frames.end()) {
            return it->second.clone();
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

    std::cout << "=== Submarine Receiver ===" << std::endl;
    std::cout << "Listening on port: " << port << std::endl;

    try {
        UDPReceiver receiver(port);
        FrameBuffer frameBuffer;

        std::cout << "\n=== Receiving Streams ===" << std::endl;
        std::cout << "Waiting for data from sender..." << std::endl;
        std::cout << "Press ESC to exit\n" << std::endl;

        int frameCount = 0;

        while (true) {
            cv::Mat frame;
            int frameType;

            if (receiver.receiveFrame(frame, frameType)) {
                // Store frame in buffer
                frameBuffer.update(frameType, frame);
                frameCount++;

                if (frameCount % 60 == 0) {
                    std::cout << "✓ Received " << frameCount << " frames" << std::endl;
                }
            }

            // Create display image with all available frames
            cv::Mat color = frameBuffer.get(FRAME_COLOR);
            cv::Mat depth = frameBuffer.get(FRAME_DEPTH);
            cv::Mat ir = frameBuffer.get(FRAME_IR);
            cv::Mat map = frameBuffer.get(FRAME_MAP);

            // Create display grid
            cv::Mat display(720, 1280, CV_8UC3);
            display.setTo(cv::Scalar(50, 50, 50));

            int frameWidth = 640;
            int frameHeight = 480;

            // Top-left: Color
            if (!color.empty() && color.cols > 0 && color.rows > 0) {
                cv::Mat colorResized;
                cv::resize(color, colorResized, cv::Size(frameWidth, frameHeight));
                if (colorResized.cols == frameWidth && colorResized.rows == frameHeight) {
                    colorResized.copyTo(display(cv::Rect(0, 0, frameWidth, frameHeight)));
                    cv::putText(display, "COLOR", cv::Point(10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO COLOR SIGNAL", cv::Point(150, 240),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }

            // Top-right: Depth
            if (!depth.empty() && depth.cols > 0 && depth.rows > 0) {
                cv::Mat depthResized;
                cv::resize(depth, depthResized, cv::Size(frameWidth, frameHeight));
                if (depthResized.cols == frameWidth && depthResized.rows == frameHeight) {
                    depthResized.copyTo(display(cv::Rect(frameWidth, 0, frameWidth, frameHeight)));
                    cv::putText(display, "DEPTH", cv::Point(frameWidth + 10, 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO DEPTH SIGNAL", cv::Point(frameWidth + 150, 240),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }

            // Bottom-left: IR
            if (!ir.empty() && ir.cols > 0 && ir.rows > 0) {
                cv::Mat irResized;
                cv::resize(ir, irResized, cv::Size(frameWidth, frameHeight));
                if (irResized.cols == frameWidth && irResized.rows == frameHeight) {
                    irResized.copyTo(display(cv::Rect(0, frameHeight, frameWidth, frameHeight)));
                    cv::putText(display, "IR", cv::Point(10, frameHeight + 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO IR SIGNAL", cv::Point(150, frameHeight + 240),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }

            // Bottom-right: 2D Map
            if (!map.empty() && map.cols > 0 && map.rows > 0) {
                cv::Mat mapResized;
                cv::resize(map, mapResized, cv::Size(frameWidth, frameHeight));
                if (mapResized.cols == frameWidth && mapResized.rows == frameHeight) {
                    mapResized.copyTo(display(cv::Rect(frameWidth, frameHeight, frameWidth, frameHeight)));
                    cv::putText(display, "2D MAP", cv::Point(frameWidth + 10, frameHeight + 30),
                               cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 0), 2);
                }
            } else {
                cv::putText(display, "NO MAP SIGNAL", cv::Point(frameWidth + 150, frameHeight + 240),
                           cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 100), 2);
            }

            // Show display
            cv::imshow("Submarine Vision System", display);

            // Exit on ESC
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
