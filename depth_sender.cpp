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

// UDP Sender for Depth stream
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

    void sendRawData(const std::vector<uchar>& encoded_data, int frame_id) {
        if (encoded_data.empty()) return;

        // Create header: [frame_id(4)][data_size(4)]
        int total_size = 8 + encoded_data.size();
        std::vector<char> packet(total_size);

        // Pack header
        *(int*)&packet[0] = frame_id;
        *(int*)&packet[4] = encoded_data.size();

        // Copy data
        memcpy(&packet[8], encoded_data.data(), encoded_data.size());

        // Send packet
        sendto(sockfd, packet.data(), packet.size(), 0,
               (const struct sockaddr*)&servaddr, sizeof(servaddr));
    }
};

int main(int argc, char **argv) try {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 192.168.1.100 5002" << std::endl;
        return 1;
    }

    std::string receiver_ip = argv[1];
    int port = std::atoi(argv[2]);

    std::cout << "=== Depth Stream Sender ===" << std::endl;
    std::cout << "Receiver IP: " << receiver_ip << std::endl;
    std::cout << "Port: " << port << std::endl;

    // Initialize UDP sender
    UDPSender sender(receiver_ip, port);

    // Initialize camera
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable only depth stream
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::shared_ptr<ob::Frame> lastDepthFrame;
    std::mutex frameMutex;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        if (frameset) {
            auto depthFrame = frameset->depthFrame();
            if (depthFrame) {
                std::unique_lock<std::mutex> lock(frameMutex);
                lastDepthFrame = depthFrame;
            }
        }
    });

    int frame_id = 0;
    auto last_send_time = std::chrono::steady_clock::now();

    std::cout << "\n=== Sending Depth Stream ===" << std::endl;
    std::cout << "Press Ctrl+C to stop\n" << std::endl;

    sleep(1);

    while(true) {
        // Limit frame rate to ~30 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_send_time);

        if (elapsed.count() >= 33) {
            std::shared_ptr<ob::Frame> depthFrame;

            {
                std::unique_lock<std::mutex> lock(frameMutex);
                depthFrame = lastDepthFrame;
            }

            if (depthFrame) {
                try {
                    auto df = depthFrame->as<ob::DepthFrame>();
                    if (df && df->data()) {
                        cv::Mat temp(df->height(), df->width(), CV_16UC1, (void*)df->data());
                        cv::Mat depthMat = temp.clone();

                        // Send colored depth visualization
                        cv::Mat depthVis;
                        depthMat.convertTo(depthVis, CV_8UC1, 255.0 / 5000.0);
                        cv::applyColorMap(depthVis, depthVis, cv::COLORMAP_JET);
                        if (!depthVis.empty()) {
                            sender.sendFrame(depthVis, frame_id++);
                        }

                        if (frame_id % 60 == 0) {
                            std::cout << "✓ Sent " << frame_id << " depth frames" << std::endl;
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
