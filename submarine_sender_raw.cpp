#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Frame.hpp"
#include "libobsensor/hpp/Device.hpp"
#include "libobsensor/hpp/Error.hpp"
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mutex>
#include <vector>
#include <iostream>
#include <cstring>

// UDP Sender class - sends RAW depth data only
class UDPSender {
private:
    int sockfd;
    struct sockaddr_in servaddr;

public:
    UDPSender(const std::string& ip, int port) {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Socket creation failed");
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
    }

    ~UDPSender() {
        close(sockfd);
    }

    void sendRawData(const std::vector<uchar>& data, int frame_id) {
        if (data.empty()) return;

        // Create header: [frame_id(4)][data_size(4)]
        size_t total_size = 8 + data.size();
        std::vector<char> packet(total_size);

        // Pack header
        *(int*)&packet[0] = frame_id;
        *(int*)&packet[4] = (int)data.size();

        // Copy data (starts after 8-byte header)
        memcpy(&packet[8], data.data(), data.size());

        // Send packet
        sendto(sockfd, packet.data(), packet.size(), 0,
               (const struct sockaddr*)&servaddr, sizeof(servaddr));
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Sends: RAW Depth Data (receiver does processing)" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Submarine RAW Depth Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;
    std::cout << "Sending: RAW depth (receiver processes)" << std::endl;

    // Initialize UDP sender
    UDPSender sender(receiver_ip, port);

    // Initialize camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable only depth stream
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::mutex frameMutex;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            auto depthFrame = frameset->depthFrame();
            if (depthFrame) {
                std::unique_lock<std::mutex> lock(frameMutex);
                lastDepthFrame = depthFrame->as<ob::DepthFrame>();
            }
        }
    });

    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    std::cout << "\n=== Sending RAW Depth ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    while(true) {
        // Limit frame rate to ~20 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 50) {
            std::shared_ptr<ob::DepthFrame> depthFrame;

            {
                std::unique_lock<std::mutex> lock(frameMutex);
                depthFrame = lastDepthFrame;
            }

            if (depthFrame) {
                try {
                    if (depthFrame->data()) {
                        // Get raw 16-bit depth data
                        cv::Mat temp(depthFrame->width(), depthFrame->height(), CV_16UC1, (void*)depthFrame->data());
                        cv::Mat depthMat = temp.clone();

                        // Encode as PNG to preserve 16-bit values
                        std::vector<uchar> pngBuffer;
                        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
                        cv::imencode(".png", depthMat, pngBuffer, params);

                        // Send raw depth
                        sender.sendRawData(pngBuffer, frame_id);

                        frame_id++;

                        if (frame_id % 60 == 0) {
                            std::cout << "✓ Sent " << frame_id << " raw depth frames" << std::endl;
                        }
                    }
                } catch (...) {}
            }

            last_send_time = now;
        }

        usleep(10000);  // 10ms
    }

    pipe.stop();
    return 0;
}
catch(ob::Error &e) {
    std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << std::endl;
    return -1;
}
catch(std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return -1;
}
