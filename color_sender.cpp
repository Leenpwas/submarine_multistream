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
#include <iostream>
#include <cstring>

// UDP Sender for Color stream
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

    void sendFrame(const cv::Mat& frame, int frame_id) {
        if (frame.empty()) return;

        // Encode frame as JPEG
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 85};
        cv::imencode(".jpg", frame, buffer, params);

        // Create header: [frame_id(4)][data_size(4)]
        int total_size = 8 + buffer.size();
        std::vector<char> packet(total_size);

        // Pack header
        *(int*)&packet[0] = frame_id;
        *(int*)&packet[4] = buffer.size();

        // Copy image data
        memcpy(&packet[8], buffer.data(), buffer.size());

        // Send packet
        sendto(sockfd, packet.data(), packet.size(), 0,
               (const struct sockaddr*)&servaddr, sizeof(servaddr));
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 192.168.1.100 5001" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Color Stream Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Initialize UDP sender
    UDPSender sender(receiver_ip, port);

    // Initialize camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable only color stream
    config->enableVideoStream(OB_STREAM_COLOR);

    std::shared_ptr<ob::Frame> lastColorFrame;
    std::mutex frameMutex;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            auto colorFrame = frameset->colorFrame();
            if (colorFrame) {
                std::unique_lock<std::mutex> lock(frameMutex);
                lastColorFrame = colorFrame;
            }
        }
    });

    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    std::cout << "\n=== Sending Color Stream ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    while(true) {
        // Limit frame rate to ~30 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 33) {
            std::shared_ptr<ob::Frame> colorFrame;

            {
                std::unique_lock<std::mutex> lock(frameMutex);
                colorFrame = lastColorFrame;
            }

            if (colorFrame) {
                try {
                    auto cf = colorFrame->as<ob::ColorFrame>();
                    if (cf && cf->data()) {
                        cv::Mat temp(cf->height(), cf->width(), CV_8UC3, (void*)cf->data());
                        cv::Mat colorMat = temp.clone();
                        cv::cvtColor(colorMat, colorMat, cv::COLOR_RGB2BGR);

                        if (!colorMat.empty()) {
                            sender.sendFrame(colorMat, frame_id++);

                            if (frame_id % 60 == 0) {
                                std::cout << "✓ Sent " << frame_id << " color frames" << std::endl;
                            }
                        }
                    }
                } catch (...) {}
            }

            last_send_time = now;
        }

        usleep(5000);
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
