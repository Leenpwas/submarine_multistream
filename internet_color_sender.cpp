#include <libobsensor/hpp/Pipeline.hpp>
#include <libobsensor/hpp/ObSensor.hpp>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <receiver_ip> <port>\n", argv[0]);
        printf("Example: %s your-public-ip.com 5000\n", argv[0]);
        return 1;
    }

    const char *receiver_ip = argv[1];
    int port = atoi(argv[2]);

    printf("=== Internet Color Sender ===\n");
    printf("Streaming to: %s:%d\n", receiver_ip, port);

    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Error: Cannot create socket\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, receiver_ip, &server_addr.sin_addr);

    // Connect to receiver
    printf("Connecting...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error: Connection failed!\n");
        printf("Make sure:\n");
        printf("  - Receiver is running\n");
        printf("  - Port %d is forwarded on router\n", port);
        printf("  - Firewall allows port %d\n", port);
        close(sock);
        return 1;
    }
    printf("Connected!\n");

    // Disable Nagle's algorithm for lower latency
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Initialize Orbbec pipeline
    ob::Pipeline pipeline;
    auto device = pipeline.getDevice();
    printf("Camera: %s\n", device->getDeviceName());

    // Start COLOR stream (640x480, 30 FPS)
    auto colorProfile = pipeline.getStreamProfile(ob::StreamType::COLOR);
    pipeline.startStream(ob::StreamType::COLOR);

    cv::Mat lastFrame;
    int frameCount = 0;
    time_t startTime = time(NULL);

    while (true) {
        auto frameSet = pipeline.waitForFrames(100);
        if (frameSet == nullptr) continue;

        auto colorFrame = frameSet->colorFrame();
        if (colorFrame == nullptr) continue;

        // Convert to OpenCV format
        int width = colorFrame->width();
        int height = colorFrame->height();
        cv::Mat colorMat(height, width, CV_8UC3, (void*)colorFrame->data());

        // Clone and encode as JPEG
        cv::Mat frame = colorMat.clone();
        std::vector<uchar> buffer;
        cv::imencode(".jpg", frame, buffer, {cv::IMWRITE_JPEG_QUALITY, 80});

        // Send frame size first
        int size = buffer.size();
        if (send(sock, &size, sizeof(size), 0) <= 0) {
            printf("Connection lost!\n");
            break;
        }

        // Send frame data
        if (send(sock, buffer.data(), size, 0) <= 0) {
            printf("Connection lost!\n");
            break;
        }

        frameCount++;

        // FPS calculation every 30 frames
        if (frameCount % 30 == 0) {
            time_t currentTime = time(NULL);
            double fps = 30.0 / difftime(currentTime, startTime);
            printf("Sent frame %d (%.1f FPS, %d KB/frame)\n",
                   frameCount, fps, size / 1024);
            startTime = currentTime;
        }
    }

    pipeline.stopStream(ob::StreamType::COLOR);
    close(sock);
    return 0;
}
