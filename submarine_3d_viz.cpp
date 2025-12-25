#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Frame.hpp"
#include "libobsensor/hpp/Device.hpp"
#include "libobsensor/hpp/Error.hpp"
#include <opencv2/opencv.hpp>
#include <opencv2/viz.hpp>
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>
#include <iostream>
#include <chrono>

// Map sensor type to stream type
OBStreamType SensorTypeToStreamType(OBSensorType sensorType) {
    switch(sensorType) {
    case OB_SENSOR_COLOR:
        return OB_STREAM_COLOR;
    case OB_SENSOR_DEPTH:
        return OB_STREAM_DEPTH;
    case OB_SENSOR_IR:
        return OB_STREAM_IR;
    case OB_SENSOR_IR_LEFT:
        return OB_STREAM_IR_LEFT;
    case OB_SENSOR_IR_RIGHT:
        return OB_STREAM_IR_RIGHT;
    default:
        return OB_STREAM_UNKNOWN;
    }
}

// Professional 3D Point Cloud Map with accumulation
class Map3D {
private:
    cv::viz::Viz3d window;
    std::vector<cv::Vec3f> accumulatedPoints;
    std::vector<cv::Vec3b> accumulatedColors;
    int maxPoints = 100000;  // Max accumulated points
    int skipStep = 2;  // Skip pixels for performance
    bool accumulate = true;  // Accumulate points over time

    // Camera intrinsics (for Orbbec Astra Pro)
    float fx = 525.0f;
    float fy = 525.0f;
    float cx = 320.0f;
    float cy = 240.0f;
    float max_range = 4.0f;

public:
    Map3D() : window("3D Point Cloud - Professional") {
        // Set window properties
        window.setWindowSize(cv::Size(1280, 720));
        window.setBackgroundColor(cv::viz::Color::black());

        // Set camera position
        cv::Vec3d cam_pos(0.0, -2.0, -2.0);
        cv::Vec3d cam_focal(0.0, 0.0, 0.0);
        cv::Vec3d cam_y_dir(0.0, -1.0, 0.0);
        window.setViewerPose(cv::viz::makeCameraPose(cam_pos, cam_focal, cam_y_dir));

        // Add coordinate widget
        window.showWidget("Coordinate", cv::viz::WCoordinateSystem(0.5));

        // Add floor grid
        cv::viz::WGrid grid(cv::Vec2i(20, 20), cv::Vec2d(0.2, 0.2), cv::viz::Color(0.3, 0.3, 0.3));
        window.showWidget("Grid", grid);

        std::cout << "3D Viewer initialized" << std::endl;
    }

    void update(std::shared_ptr<ob::DepthFrame> depthFrame, std::shared_ptr<ob::ColorFrame> colorFrame = nullptr) {
        uint32_t depth_width = depthFrame->width();
        uint32_t depth_height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        const uint16_t* depth_data = (const uint16_t*)depthFrame->data();

        std::vector<cv::Vec3f> newPoints;
        std::vector<cv::Vec3b> newColors;

        // Convert depth to 3D point cloud
        for (uint32_t y = 0; y < depth_height; y += skipStep) {
            for (uint32_t x = 0; x < depth_width; x += skipStep) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.3f) continue;

                // Convert to 3D coordinates
                float px = (x - cx) * depth_m / fx;
                float py = (y - cy) * depth_m / fy;
                float pz = depth_m;

                // Rotate to match camera orientation (OpenGL style)
                cv::Vec3f point(px, py, pz);

                // Get color
                cv::Vec3b color;
                if (colorFrame && colorFrame->data()) {
                    // Use actual color from RGB camera
                    int color_x = x * colorFrame->width() / depth_width;
                    int color_y = y * colorFrame->height() / depth_height;
                    const uint8_t* color_data = (const uint8_t*)colorFrame->data();
                    int idx = (color_y * colorFrame->width() + color_x) * 3;
                    color = cv::Vec3b(color_data[idx+2], color_data[idx+1], color_data[idx+0]); // BGR
                } else {
                    // Color based on depth (blue = far, red = close)
                    float intensity = 1.0f - (depth_m / max_range);
                    color = cv::Vec3b((int)(intensity * 255), (int)(intensity * 128), (int)((1.0f - intensity) * 255));
                }

                newPoints.push_back(point);
                newColors.push_back(color);
            }
        }

        // Accumulate points
        if (accumulate && !newPoints.empty()) {
            accumulatedPoints.insert(accumulatedPoints.end(), newPoints.begin(), newPoints.end());
            accumulatedColors.insert(accumulatedColors.end(), newColors.begin(), newColors.end());

            // Limit total points
            if (accumulatedPoints.size() > (size_t)maxPoints) {
                int excess = accumulatedPoints.size() - maxPoints;
                accumulatedPoints.erase(accumulatedPoints.begin(), accumulatedPoints.begin() + excess);
                accumulatedColors.erase(accumulatedColors.begin(), accumulatedColors.begin() + excess);
            }
        }

        // Update point cloud widget
        if (!accumulatedPoints.empty()) {
            cv::Mat cloudMat(accumulatedPoints.size(), 1, CV_32FC3, accumulatedPoints.data());
            cv::Mat colorMat(accumulatedColors.size(), 1, CV_8UC3, accumulatedColors.data());
            cv::viz::WCloud cloud(cloudMat, colorMat);
            cloud.setRenderingProperty(cv::viz::POINT_SIZE, 3);
            window.showWidget("Cloud", cloud);
        } else if (!newPoints.empty()) {
            cv::Mat cloudMat(newPoints.size(), 1, CV_32FC3, newPoints.data());
            cv::Mat colorMat(newColors.size(), 1, CV_8UC3, newColors.data());
            cv::viz::WCloud cloud(cloudMat, colorMat);
            cloud.setRenderingProperty(cv::viz::POINT_SIZE, 3);
            window.showWidget("Cloud", cloud);
        }
    }

    void render() {
        window.spinOnce(1, true);
    }

    bool wasStopped() {
        return window.wasStopped();
    }

    void clear() {
        accumulatedPoints.clear();
        accumulatedColors.clear();
    }

    void toggleAccumulation() {
        accumulate = !accumulate;
        if (!accumulate) {
            clear();
        }
        std::cout << "Point accumulation: " << (accumulate ? "ON" : "OFF") << std::endl;
    }

    size_t getNumPoints() const {
        return accumulatedPoints.size();
    }

    cv::viz::Viz3d& getWindow() {
        return window;
    }
};

// Keyboard callback
void keyboardCallback(const cv::viz::KeyboardEvent& event, void* userdata) {
    Map3D* map3d = static_cast<Map3D*>(userdata);

    if (event.action == cv::viz::KeyboardEvent::KEY_UP) {
        if (event.code == 'a' || event.code == 'A') {
            map3d->toggleAccumulation();
        }
        if (event.code == 'c' || event.code == 'C') {
            map3d->clear();
            std::cout << "Point cloud cleared" << std::endl;
        }
    }
}

int main(int argc, char **argv) try {
    std::cout << "========================================" << std::endl;
    std::cout << "  Professional 3D Point Cloud Viewer" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize Orbbec pipeline
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable all video streams
    config->enableVideoStream(OB_STREAM_COLOR);
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::mutex frameMutex;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;
    std::shared_ptr<ob::ColorFrame> lastColorFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        auto count = frameset->frameCount();
        for(int i = 0; i < count; i++) {
            auto frame = frameset->getFrame(i);
            std::unique_lock<std::mutex> lk(frameMutex);

            if(frame->type() == OB_FRAME_DEPTH) {
                lastDepthFrame = frame->as<ob::DepthFrame>();
            }
            if(frame->type() == OB_FRAME_COLOR) {
                lastColorFrame = frame->as<ob::ColorFrame>();
            }
        }
    });

    Map3D map3d;
    map3d.getWindow().registerKeyboardCallback(keyboardCallback, &map3d);

    std::cout << "\nControls:" << std::endl;
    std::cout << "  Mouse drag     - Rotate view" << std::endl;
    std::cout << "  Mouse scroll   - Zoom" << std::endl;
    std::cout << "  'A' key        - Toggle accumulation (ON/OFF)" << std::endl;
    std::cout << "  'C' key        - Clear point cloud" << std::endl;
    std::cout << "  ESC/Q          - Exit\n" << std::endl;

    int frame_count = 0;
    auto last_update_time = std::chrono::steady_clock::now();
    auto last_stats_time = std::chrono::steady_clock::now();

    while (!map3d.wasStopped()) {
        // Update 3D map at ~10 FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_time);

        if (lastDepthFrame && elapsed.count() >= 100) {
            std::unique_lock<std::mutex> lock(frameMutex);
            map3d.update(lastDepthFrame, lastColorFrame);
            last_update_time = now;
            frame_count++;
        }

        // Print stats every 2 seconds
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time);
        if (stats_elapsed.count() >= 2000) {
            std::cout << "✓ FPS: " << (frame_count * 1000 / stats_elapsed.count())
                      << " | Points: " << map3d.getNumPoints() << std::endl;
            frame_count = 0;
            last_stats_time = now;
        }

        map3d.render();
    }

    pipe.stop();

    std::cout << "\nExiting..." << std::endl;
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
