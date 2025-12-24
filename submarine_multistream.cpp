#include "window.hpp"
#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Error.hpp"
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>

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

// 2D Mapper
class Map2D {
private:
    std::vector<uint8_t> map_image;
    int width = 640;
    int height = 480;
    float max_range = 4.0f;
    
public:
    Map2D() {
        map_image.resize(width * height * 3, 255);
    }

    void update(std::shared_ptr<ob::DepthFrame> depthFrame) {
        uint32_t depth_width = depthFrame->width();
        uint32_t depth_height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        const uint16_t* depth_data = (const uint16_t*)depthFrame->data();

        // Clear to white
        std::fill(map_image.begin(), map_image.end(), 255);

        // Draw grid lines
        for (int y = 0; y < height; y += 50) {
            for (int x = 0; x < width; x++) {
                int idx = (y * width + x) * 3;
                map_image[idx] = map_image[idx+1] = map_image[idx+2] = 200;
            }
        }
        for (int x = 0; x < width; x += 50) {
            for (int y = 0; y < height; y++) {
                int idx = (y * width + x) * 3;
                map_image[idx] = map_image[idx+1] = map_image[idx+2] = 200;
            }
        }

        // Center line (robot heading)
        int center_x = width / 2;
        for (int y = 0; y < height; y++) {
            int idx = (y * width + center_x) * 3;
            map_image[idx] = 255;
            map_image[idx+1] = map_image[idx+2] = 0;
        }

        // Project depth data to map
        for (uint32_t y = 0; y < depth_height; y += 4) {
            for (uint32_t x = 0; x < depth_width; x += 4) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.2f) continue;

                float fov = 60.0f * M_PI / 180.0f;
                float angle = (x - depth_width/2.0f) / depth_width * fov;
                float x_pos = depth_m * tan(angle);

                int map_x = width/2 + (int)(x_pos * width / (max_range * 2));
                int map_y = (int)(depth_m * height / max_range);

                if (map_x >= 0 && map_x < width && map_y >= 0 && map_y < height) {
                    int idx = (map_y * width + map_x) * 3;
                    float intensity = 1.0f - (depth_m / max_range);
                    map_image[idx] = (uint8_t)(intensity * 200);
                    map_image[idx+1] = 0;
                    map_image[idx+2] = (uint8_t)((1.0f-intensity)*100);
                }
            }
        }

        // Draw robot icon
        int robot_x = width / 2;
        int robot_y = height - 10;
        for (int dy = -5; dy <= 5; dy++) {
            for (int dx = -5; dx <= 5; dx++) {
                if (dx*dx + dy*dy <= 25) {
                    int px = robot_x + dx;
                    int py = robot_y + dy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        int idx = (py * width + px) * 3;
                        map_image[idx] = 0;
                        map_image[idx+1] = 255;
                        map_image[idx+2] = 0;
                    }
                }
            }
        }
    }

    void saveToFile(const std::string& filename) {
        cv::Mat mapMat(height, width, CV_8UC3, map_image.data());
        cv::imwrite(filename, mapMat);
    }

    const uint8_t* getImageData() const { return map_image.data(); }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
};

int main(int argc, char **argv) try {
    std::cout << "=== Submarine Multi-Stream + 2D Map ===" << std::endl;
    
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();
    
    auto device = pipe.getDevice();
    auto sensorList = device->getSensorList();
    for(int i = 0; i < sensorList->count(); i++) {
        auto sensorType = sensorList->type(i);
        if(sensorType == OB_SENSOR_GYRO || sensorType == OB_SENSOR_ACCEL) {
            continue;
        }
        auto streamType = SensorTypeToStreamType(sensorType);
        config->enableVideoStream(streamType);
    }
    
    std::mutex frameMutex;
    std::map<OBFrameType, std::shared_ptr<ob::Frame>> frameMap;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;
    
    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        auto count = frameset->frameCount();
        for(int i = 0; i < count; i++) {
            auto frame = frameset->getFrame(i);
            std::unique_lock<std::mutex> lk(frameMutex);
            frameMap[frame->type()] = frame;
            
            if(frame->type() == OB_FRAME_DEPTH) {
                lastDepthFrame = frame->as<ob::DepthFrame>();
            }
        }
    });
    
    Window app("Submarine Vision System", 1280, 720, RENDER_GRID);
    Map2D mapper;

    std::cout << "\n=== System Running ===" << std::endl;
    std::cout << "Main Window: All camera streams (IR, Color, Depth)" << std::endl;
    std::cout << "2D Map: Saved to 'submarine_2d_map.png' (updates every second)" << std::endl;
    std::cout << "\nOpen submarine_2d_map.png with an image viewer to see the live map!" << std::endl;
    std::cout << "Tip: Use 'eog submarine_2d_map.png' in another terminal" << std::endl;
    std::cout << "\nPress ESC in window to exit\n" << std::endl;
    
    int frame_count = 0;
    auto last_save_time = std::chrono::steady_clock::now();
    
    while(app) {
        std::vector<std::shared_ptr<ob::Frame>> framesForRender;

        {
            std::unique_lock<std::mutex> lock(frameMutex);
            for(auto &frame: frameMap) {
                framesForRender.push_back(frame.second);
            }
        }

        app.addToRender(framesForRender);

        // Update and save map every second
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_save_time);

        if(lastDepthFrame && elapsed.count() >= 1000) {
            mapper.update(lastDepthFrame);
            mapper.saveToFile("submarine_2d_map.png");
            last_save_time = now;

            if(frame_count % 30 == 0) {
                std::cout << "✓ 2D map updated (frame " << frame_count << ")" << std::endl;
            }
        }

        frame_count++;
    }
    
    pipe.stop();

    std::cout << "\n=== Exiting ===" << std::endl;
    std::cout << "Final map saved to submarine_2d_map.png" << std::endl;
    
    return 0;
}
catch(ob::Error &e) {
    std::cerr << "Error: " << e.getName() << " - " << e.getMessage() << std::endl;
    return -1;
}
