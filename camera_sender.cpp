#include "libobsensor/ObSensor.hpp"
#include "libobsensor/hpp/Pipeline.hpp"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>

// Frame header structure
struct FrameHeader {
    uint32_t frame_type;      // OB_FRAME_DEPTH, OB_FRAME_COLOR, etc.
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
    uint32_t format;
    float value_scale;        // For depth frames
    uint64_t timestamp;
};

class CameraSender {
private:
    int sock;
    struct sockaddr_in server_addr;
    bool connected = false;
    
public:
    CameraSender(const std::string& server_ip, int port) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            throw std::runtime_error("Failed to create socket");
        }
        
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr);
        
        // Set socket timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    
    bool connect() {
        std::cout << "Connecting to receiver..." << std::endl;
        if (::connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed: " << strerror(errno) << std::endl;
            return false;
        }
        connected = true;
        std::cout << "Connected successfully!" << std::endl;
        return true;
    }
    
    bool sendFrame(std::shared_ptr<ob::Frame> frame) {
        if (!connected) return false;
        
        auto videoFrame = frame->as<ob::VideoFrame>();
        
        // Prepare header
        FrameHeader header;
        header.frame_type = frame->type();
        header.width = videoFrame->width();
        header.height = videoFrame->height();
        header.data_size = videoFrame->dataSize();
        header.format = videoFrame->format();
        header.timestamp = frame->timeStamp();
        
        // Get value scale for depth frames
        if (frame->type() == OB_FRAME_DEPTH) {
            auto depthFrame = frame->as<ob::DepthFrame>();
            header.value_scale = depthFrame->getValueScale();
        } else {
            header.value_scale = 0.0f;
        }
        
        // Send header
        if (send(sock, &header, sizeof(header), 0) != sizeof(header)) {
            std::cerr << "Failed to send header" << std::endl;
            connected = false;
            return false;
        }
        
        // Send frame data
        const uint8_t* data = (const uint8_t*)videoFrame->data();
        size_t sent = 0;
        while (sent < header.data_size) {
            ssize_t n = send(sock, data + sent, header.data_size - sent, 0);
            if (n <= 0) {
                std::cerr << "Failed to send frame data" << std::endl;
                connected = false;
                return false;
            }
            sent += n;
        }
        
        return true;
    }
    
    ~CameraSender() {
        if (sock >= 0) {
            close(sock);
        }
    }
};

OBStreamType SensorTypeToStreamType(OBSensorType sensorType) {
    switch(sensorType) {
    case OB_SENSOR_COLOR:
        return OB_STREAM_COLOR;
    case OB_SENSOR_DEPTH:
        return OB_STREAM_DEPTH;
    case OB_SENSOR_IR:
        return OB_STREAM_IR;
    default:
        return OB_STREAM_UNKNOWN;
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <receiver_ip> <port>" << std::endl;
        std::cout << "Example: " << argv[0] << " 192.168.1.100 5000" << std::endl;
        return 1;
    }
    
    std::string receiver_ip = argv[1];
    int port = std::stoi(argv[2]);
    
    try {
        std::cout << "=== Camera Sender (Submarine Pi) ===" << std::endl;
        std::cout << "Initializing camera..." << std::endl;
        
        // Initialize camera
        ob::Pipeline pipe;
        auto config = std::make_shared<ob::Config>();
        
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
        
        pipe.start(config);
        std::cout << "Camera initialized!" << std::endl;
        
        // Connect to receiver
        CameraSender sender(receiver_ip, port);
        while (!sender.connect()) {
            std::cout << "Retrying in 2 seconds..." << std::endl;
            sleep(2);
        }
        
        std::cout << "\n=== Streaming Started ===" << std::endl;
        std::cout << "Sending frames to " << receiver_ip << ":" << port << std::endl;
        std::cout << "Press Ctrl+C to stop\n" << std::endl;
        
        int frame_count = 0;
        int error_count = 0;
        
        while (true) {
            auto frameset = pipe.waitForFrames(1000);
            if (!frameset) continue;
            
            // Send all frames in frameset
            for (uint32_t i = 0; i < frameset->frameCount(); i++) {
                auto frame = frameset->getFrame(i);
                
                if (!sender.sendFrame(frame)) {
                    error_count++;
                    std::cerr << "Send error #" << error_count << std::endl;
                    
                    if (error_count > 10) {
                        std::cout << "Too many errors, reconnecting..." << std::endl;
                        while (!sender.connect()) {
                            sleep(2);
                        }
                        error_count = 0;
                    }
                } else {
                    if (frame_count % 30 == 0) {
                        std::cout << "Sent frame " << frame_count 
                                  << " (type=" << frame->type() << ")" << std::endl;
                    }
                }
            }
            
            frame_count++;
        }
        
        pipe.stop();
        
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
