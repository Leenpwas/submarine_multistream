#include <libobsensor/hpp/Pipeline.hpp>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <laptop_ip> <port>\n", argv[0]);
        printf("Example: %s 192.168.2.3 5001\n\n", argv[0]);
        printf("This is a lightweight sender - Pi just captures and sends!\n");
        printf("All ML processing happens on the laptop.\n");
        return 1;
    }

    const char *laptop_ip = argv[1];
    int port = atoi(argv[2]);

    printf("=== Submarine Video Sender ===\n");
    printf("Target: %s:%d\n", laptop_ip, port);
    printf("Note: Pi just streams video - laptop does the ML work!\n\n");

    // Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Error: Cannot create socket\n");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, laptop_ip, &server_addr.sin_addr);

    // Connect to laptop
    printf("Connecting to laptop...\n");
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Error: Connection failed!\n");
        printf("Make sure the web server is running on the laptop\n");
        close(sock);
        return 1;
    }
    printf("✓ Connected!\n");

    // Disable Nagle's algorithm for lower latency
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Initialize Orbbec pipeline
    ob::Pipeline pipeline;
    auto device = pipeline.getDevice();
    printf("✓ Camera: %s\n", device->getDeviceName());

    // Start COLOR stream (640x480, 30 FPS)
    pipeline.startStream(ob::StreamType::COLOR);
    printf("✓ Started color stream\n");

    printf("\n🎥 Streaming video to laptop...\n");
    printf("Press Ctrl+C to stop\n\n");

    int frameCount = 0;
    time_t startTime = time(NULL);

    while (true) {
        auto frameSet = pipeline.waitForFrames(100);
        if (frameSet == nullptr) continue;

        auto colorFrame = frameSet->colorFrame();
        if (colorFrame == nullptr) continue;

        // Get frame dimensions
        int width = colorFrame->width();
        int height = colorFrame->height();

        // Convert to OpenCV format (direct pointer, no copy)
        cv::Mat colorMat(height, width, CV_8UC3, (void*)colorFrame->data());

        // Encode as JPEG (80% quality)
        std::vector<uchar> buffer;
        std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, 80};
        cv::imencode(".jpg", colorMat, buffer, params);

        // Send frame size as 4 bytes (big-endian)
        int size = buffer.size();
        uint32_t size_be = htobe32(size);
        if (send(sock, &size_be, 4, 0) <= 0) {
            printf("\n✗ Connection lost!\n");
            break;
        }

        // Send JPEG data
        if (send(sock, buffer.data(), size, 0) <= 0) {
            printf("\n✗ Connection lost!\n");
            break;
        }

        frameCount++;

        // Stats every 30 frames
        if (frameCount % 30 == 0) {
            time_t currentTime = time(NULL);
            double elapsed = difftime(currentTime, startTime);
            double fps = 30.0 / elapsed;
            printf("Sent frame %4d | FPS: %.1f | Size: %4d KB\n",
                   frameCount, fps, size / 1024);
            startTime = currentTime;
        }
    }

    pipeline.stopStream(ob::StreamType::COLOR);
    close(sock);

    printf("\n✓ Stopped streaming\n");
    return 0;
}
