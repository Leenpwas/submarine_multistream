#include <opencv2/opencv.hpp>
#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Button structure
struct Button {
    cv::Rect rect;
    std::string label;
    std::string command;
    bool needsPort;
    bool needsIPAndPort;
    cv::Scalar color;
    cv::Scalar hoverColor;
};

pid_t child_pid = 0;
cv::Point clickPos(-1, -1);

// Signal handler to kill child process when parent exits
void signalHandler(int signum) {
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
    }
    exit(signum);
}

// Mouse callback
void onMouse(int event, int x, int y, int flags, void* userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        cv::Point* clickPos = static_cast<cv::Point*>(userdata);
        *clickPos = cv::Point(x, y);
    }
}

// Draw a button
void drawButton(cv::Mat& canvas, const Button& btn, bool hover) {
    cv::Scalar color = hover ? btn.hoverColor : btn.color;
    cv::rectangle(canvas, btn.rect, color, -1);
    cv::rectangle(canvas, btn.rect, cv::Scalar(255, 255, 255), 2);

    // Center text
    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.8;
    int thickness = 2;

    int baseline = 0;
    cv::Size textSize = cv::getTextSize(btn.label, fontFace, fontScale, thickness, &baseline);

    cv::Point textPos(
        btn.rect.x + (btn.rect.width - textSize.width) / 2,
        btn.rect.y + (btn.rect.height + textSize.height) / 2
    );

    cv::putText(canvas, btn.label, textPos, fontFace, fontScale,
                cv::Scalar(255, 255, 255), thickness);
}

// Check if point is inside button
bool isInside(const cv::Point& point, const cv::Rect& rect) {
    return rect.contains(point);
}

int main() {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create window
    cv::Mat canvas(500, 700, CV_8UC3);
    cv::namedWindow("Submarine Vision System", cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback("Submarine Vision System", onMouse, &clickPos);

    clickPos = cv::Point(-1, -1);

    // Get paths
    std::string buildDir = "/home/leenos/submarine_multistream/build";

    // Define buttons - only Sender and Receiver
    std::vector<Button> buttons = {
        {cv::Rect(100, 180, 500, 80), "Sender (Submarine)", "", true, true, cv::Scalar(255, 140, 0), cv::Scalar(255, 170, 30)},
        {cv::Rect(100, 300, 500, 80), "Receiver (Surface)", "", true, false, cv::Scalar(220, 60, 60), cv::Scalar(250, 90, 90)},
    };

    while (true) {
        // Clear canvas with dark background
        canvas.setTo(cv::Scalar(30, 30, 40));

        // Draw title
        cv::putText(canvas, "Submarine Vision System",
                    cv::Point(350, 60),
                    cv::FONT_HERSHEY_DUPLEX, 1.3,
                    cv::Scalar(255, 255, 255), 2, false);

        cv::putText(canvas, "Depth + 2D Map Streaming",
                    cv::Point(350, 110),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(180, 180, 180), 1, false);

        // Draw buttons
        cv::Point mousePos = clickPos;
        for (auto& btn : buttons) {
            bool hover = isInside(mousePos, btn.rect);
            drawButton(canvas, btn, hover);
        }

        // Draw instructions
        cv::putText(canvas, "Press ESC or close window to exit",
                    cv::Point(350, 470),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    cv::Scalar(150, 150, 150), 1, false);

        // Show window
        cv::imshow("Submarine Vision System", canvas);

        // Check for clicks
        if (clickPos.x >= 0) {
            for (size_t i = 0; i < buttons.size(); i++) {
                if (isInside(clickPos, buttons[i].rect)) {
                    cv::destroyWindow("Submarine Vision System");

                    std::string command;
                    std::string ip, port;

                    // Get input based on button
                    if (buttons[i].needsIPAndPort) {
                        std::cout << "\n=== Submarine Sender Configuration ===" << std::endl;
                        std::cout << "Enter receiver IP (default: 192.168.1.100): ";
                        std::getline(std::cin, ip);
                        if (ip.empty()) ip = "192.168.1.100";

                        std::cout << "Enter port (default: 5000): ";
                        std::getline(std::cin, port);
                        if (port.empty()) port = "5000";

                        command = buildDir + "/submarine_sender " + ip + " " + port;
                    } else if (buttons[i].needsPort) {
                        std::cout << "\n=== Surface Receiver Configuration ===" << std::endl;
                        std::cout << "Enter port (default: 5000): ";
                        std::getline(std::cin, port);
                        if (port.empty()) port = "5000";

                        command = buildDir + "/submarine_receiver " + port;
                    }

                    std::cout << "\nLaunching: " << command << std::endl;
                    std::cout << "Press Ctrl+C to return to menu\n" << std::endl;

                    // Fork and execute
                    child_pid = fork();
                    if (child_pid == 0) {
                        // Child process
                        system(command.c_str());
                        exit(0);
                    } else if (child_pid > 0) {
                        // Parent process - wait for child
                        int status;
                        waitpid(child_pid, &status, 0);
                        child_pid = 0;
                    }

                    // Recreate window
                    cv::namedWindow("Submarine Vision System", cv::WINDOW_AUTOSIZE);
                    cv::setMouseCallback("Submarine Vision System", onMouse, &clickPos);

                    break;
                }
            }
            clickPos = cv::Point(-1, -1);
        }

        // Exit on ESC
        int key = cv::waitKey(30) & 0xFF;
        if (key == 27) {  // ESC
            break;
        }
    }

    cv::destroyAllWindows();
    std::cout << "\nExiting Submarine Vision System" << std::endl;
    return 0;
}
