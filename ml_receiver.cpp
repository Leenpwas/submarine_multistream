#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>

class ObjectDetector {
private:
    cv::dnn::Net net;
    std::vector<std::string> classes;
    cv::Scalar boxColor;
    float confThreshold;
    float nmsThreshold;

public:
    ObjectDetector(const std::string& modelPath, const std::string& configPath,
                   const std::string& classesPath)
        : confThreshold(0.5f), nmsThreshold(0.4f), boxColor(0, 255, 0) {

        // Load class names
        std::ifstream classNamesFile(classesPath);
        std::string line;
        while (std::getline(classNamesFile, line)) {
            classes.push_back(line);
        }

        // Load neural network
        net = cv::dnn::readNetFromTensorflow(modelPath, configPath);
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

        printf("Loaded model with %d classes\n", (int)classes.size());
    }

    std::vector<cv::Rect> detect(const cv::Mat& frame, std::vector<std::string>& labels) {
        cv::Mat blob;
        cv::dnn::blobFromImage(frame, blob, 1.0, cv::Size(300, 300),
                               cv::Scalar(127.5, 127.5, 127.5), true, false);

        net.setInput(blob);
        cv::Mat detections = net.forward();

        std::vector<cv::Rect> boxes;
        cv::Mat detectionMatrix(detections.size[2], detections.size[3], CV_32F,
                                detections.ptr<float>());

        for (int i = 0; i < detectionMatrix.rows; i++) {
            float confidence = detectionMatrix.at<float>(i, 2);

            if (confidence > confThreshold) {
                int classId = static_cast<int>(detectionMatrix.at<float>(i, 1));
                int xLeft = static_cast<int>(detectionMatrix.at<float>(i, 3) * frame.cols);
                int yTop = static_cast<int>(detectionMatrix.at<float>(i, 4) * frame.rows);
                int xRight = static_cast<int>(detectionMatrix.at<float>(i, 5) * frame.cols);
                int yBottom = static_cast<int>(detectionMatrix.at<float>(i, 6) * frame.rows);

                xLeft = std::max(0, std::min(xLeft, frame.cols - 1));
                yTop = std::max(0, std::min(yTop, frame.rows - 1));
                xRight = std::max(0, std::min(xRight, frame.cols - 1));
                yBottom = std::max(0, std::min(yBottom, frame.rows - 1));

                boxes.push_back(cv::Rect(xLeft, yTop, xRight - xLeft, yBottom - yTop));

                std::string label = cv::format("%s: %.2f",
                    classId < classes.size() ? classes[classId].c_str() : "Unknown",
                    confidence);
                labels.push_back(label);
            }
        }

        // Non-maximum suppression
        std::vector<int> indices;
        cv::dnn::NMSBoxes(boxes, std::vector<float>(boxes.size(), 1.0),
                          confThreshold, nmsThreshold, indices);

        std::vector<cv::Rect> filteredBoxes;
        std::vector<std::string> filteredLabels;
        for (int idx : indices) {
            filteredBoxes.push_back(boxes[idx]);
            filteredLabels.push_back(labels[idx]);
        }
        labels = filteredLabels;

        return filteredBoxes;
    }

    void drawDetections(cv::Mat& frame, const std::vector<cv::Rect>& boxes,
                       const std::vector<std::string>& labels) {
        for (size_t i = 0; i < boxes.size(); i++) {
            cv::rectangle(frame, boxes[i], boxColor, 2);

            int baseLine;
            cv::Size labelSize = cv::getTextSize(labels[i], cv::FONT_HERSHEY_SIMPLEX,
                                                   0.5, 1, &baseLine);
            cv::rectangle(frame, cv::Point(boxes[i].x, boxes[i].y - labelSize.height - baseLine),
                          cv::Point(boxes[i].x + labelSize.width, boxes[i].y),
                          boxColor, cv::FILLED);
            cv::putText(frame, labels[i], cv::Point(boxes[i].x, boxes[i].y - baseLine),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
        }
    }
};

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 5000;
    bool enableDetection = true;

    printf("=== ML Object Detection Receiver ===\n");
    printf("Listening on port: %d\n", port);

    // Initialize object detector
    ObjectDetector* detector = nullptr;
    if (enableDetection) {
        std::string modelPath = "frozen_inference_graph.pb";
        std::string configPath = "ssd_mobilenet_v2_coco.pbtxt";
        std::string classesPath = "coco_classes.txt";

        // Check if model files exist
        std::ifstream modelFile(modelPath);
        if (!modelFile.good()) {
            printf("Warning: Model files not found!\n");
            printf("Download them with: ./download_models.sh\n");
            printf("Continuing without detection...\n");
            enableDetection = false;
        } else {
            detector = new ObjectDetector(modelPath, configPath, classesPath);
            printf("Object detection enabled!\n");
        }
    }

    // Create TCP server socket
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 1);
    printf("Waiting for sender...\n\n");

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, clientIP, INET_ADDRSTRLEN);
    printf("Connected to: %s\n", clientIP);

    int frameCount = 0;
    time_t startTime = time(NULL);
    double lastFpsTime = startTime;
    int fpsFrameCount = 0;

    while (true) {
        // Receive frame size
        int size;
        int n = recv(client_sock, &size, sizeof(size), MSG_WAITALL);
        if (n <= 0) break;

        // Receive frame data
        std::vector<uchar> buffer(size);
        int totalReceived = 0;
        while (totalReceived < size) {
            int received = recv(client_sock, buffer.data() + totalReceived,
                               size - totalReceived, MSG_WAITALL);
            if (received <= 0) break;
            totalReceived += received;
        }

        if (totalReceived != size) break;

        // Decode frame
        cv::Mat frame = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (frame.empty()) continue;

        // Object detection
        if (enableDetection && detector) {
            std::vector<std::string> labels;
            std::vector<cv::Rect> boxes = detector->detect(frame, labels);
            detector->drawDetections(frame, boxes, labels);

            // Display detection info
            cv::putText(frame, cv::format("Objects: %lu", boxes.size()),
                       cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 0), 2);
        }

        // FPS calculation
        fpsFrameCount++;
        time_t currentTime = time(NULL);
        if (difftime(currentTime, lastFpsTime) >= 1.0) {
            double fps = fpsFrameCount / difftime(currentTime, lastFpsTime);
            cv::putText(frame, cv::format("FPS: %.1f", fps),
                       cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                       cv::Scalar(0, 255, 0), 2);
            fpsFrameCount = 0;
            lastFpsTime = currentTime;
        }

        // Display frame
        cv::imshow("Submarine Vision - Object Detection", frame);

        frameCount++;
        if (frameCount % 30 == 0) {
            printf("Received frame %d (%.1f KB/frame)\n", frameCount, size / 1024.0);
        }

        if (cv::waitKey(1) == 27) break; // ESC to exit
    }

    printf("\nDisconnected. Total frames: %d\n", frameCount);

    close(client_sock);
    close(server_sock);
    if (detector) delete detector;

    return 0;
}
