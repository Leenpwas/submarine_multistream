#include "libobsensor/hpp/Pipeline.hpp"
#include "libobsensor/hpp/Frame.hpp"
#include "libobsensor/hpp/Error.hpp"
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <opencv2/opencv.hpp>

// Camera control state
struct CameraState {
    float yaw = -90.0f;
    float pitch = -30.0f;
    float distance = 5.0f;
    double lastX = 0;
    double lastY = 0;
    bool mouseDown = false;
};

CameraState camera;

// Simple shader
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vertexColor;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    vertexColor = aColor;
    gl_PointSize = 3.0;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 vertexColor;
out vec4 FragColor;

void main() {
    FragColor = vec4(vertexColor, 1.0);
}
)";

// Simple 4x4 matrix operations (avoiding GLM dependency)
struct Mat4 {
    float m[16];

    Mat4() {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1.0f;  // Identity
    }

    float* operator()() { return m; }
    const float* operator()() const { return m; }
};

// Simple perspective projection
Mat4 perspective(float fov, float aspect, float near, float far) {
    Mat4 result;
    float tanHalfFov = tan(fov * 0.5f);
    result.m[0] = 1.0f / (aspect * tanHalfFov);
    result.m[5] = 1.0f / tanHalfFov;
    result.m[10] = -(far + near) / (far - near);
    result.m[11] = -1.0f;
    result.m[14] = -(2.0f * far * near) / (far - near);
    result.m[15] = 0.0f;
    return result;
}

// Simple lookAt view matrix
Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ, float upX, float upY, float upZ) {
    Mat4 result;

    float fx = eyeX - centerX;
    float fy = eyeY - centerY;
    float fz = eyeZ - centerZ;

    float len = sqrt(fx*fx + fy*fy + fz*fz);
    fx /= len; fy /= len; fz /= len;

    float sx = fy*upZ - fz*upY;
    float sy = fz*upX - fx*upZ;
    float sz = fx*upY - fy*upX;

    len = sqrt(sx*sx + sy*sy + sz*sz);
    sx /= len; sy /= len; sz /= len;

    float ux = sy*fz - sz*fy;
    float uy = sz*fx - sx*fz;
    float uz = sx*fy - sy*fx;

    result.m[0] = sx; result.m[4] = ux; result.m[8] = fx; result.m[12] = -(sx*eyeX + ux*eyeY + fx*eyeZ);
    result.m[1] = sy; result.m[5] = uy; result.m[9] = fy; result.m[13] = -(sy*eyeX + uy*eyeY + fy*eyeZ);
    result.m[2] = sz; result.m[6] = uz; result.m[10] = fz; result.m[14] = -(sz*eyeX + uz*eyeY + fz*eyeZ);
    result.m[3] = 0;  result.m[7] = 0;  result.m[11] = 0;  result.m[15] = 1;

    return result;
}

// 3D Point Cloud Map
class Map3D {
private:
    GLuint VAO, VBO;
    GLuint shaderProgram;
    std::vector<float> pointCloud;
    int numPoints = 0;

    bool loadShaders() {
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);

        int success;
        char infoLog[512];
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
            return false;
        }

        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);

        glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
            std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
            return false;
        }

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);

        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
            std::cerr << "Shader program linking failed: " << infoLog << std::endl;
            return false;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return true;
    }

public:
    Map3D() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        loadShaders();
    }

    ~Map3D() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteProgram(shaderProgram);
    }

    void update(std::shared_ptr<ob::DepthFrame> depthFrame) {
        uint32_t depth_width = depthFrame->width();
        uint32_t depth_height = depthFrame->height();
        float scale = depthFrame->getValueScale();
        const uint16_t* depth_data = (const uint16_t*)depthFrame->data();

        pointCloud.clear();

        // Camera intrinsics
        float fx = 525.0f;
        float fy = 525.0f;
        float cx = depth_width / 2.0f;
        float cy = depth_height / 2.0f;
        float max_range = 4.0f;

        // Convert depth to 3D point cloud
        for (uint32_t y = 0; y < depth_height; y += 2) {
            for (uint32_t x = 0; x < depth_width; x += 2) {
                uint16_t depth_val = depth_data[y * depth_width + x];
                if (depth_val == 0) continue;

                float depth_m = (depth_val * scale) / 1000.0f;
                if (depth_m > max_range || depth_m < 0.3f) continue;

                // Convert to 3D coordinates
                float px = (x - cx) * depth_m / fx;
                float py = (y - cy) * depth_m / fy;
                float pz = depth_m;

                // Rotate to put camera looking forward
                float world_x = -px;
                float world_y = -py;
                float world_z = -pz;

                // Color based on depth (red = close, blue = far)
                float intensity = 1.0f - (depth_m / max_range);
                float r = intensity;
                float g = intensity * 0.5f;
                float b = 1.0f - intensity;

                pointCloud.push_back(world_x);
                pointCloud.push_back(world_y);
                pointCloud.push_back(world_z);
                pointCloud.push_back(r);
                pointCloud.push_back(g);
                pointCloud.push_back(b);
            }
        }

        numPoints = pointCloud.size() / 6;

        // Update GPU buffer
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, pointCloud.size() * sizeof(float), pointCloud.data(), GL_DYNAMIC_DRAW);

        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Color attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    void render(float aspectRatio) {
        if (numPoints == 0) return;

        glUseProgram(shaderProgram);

        // Create view matrix (orbit camera)
        float rad = M_PI / 180.0f;
        float camX = camera.distance * cos(camera.yaw * rad) * cos(camera.pitch * rad);
        float camY = camera.distance * sin(camera.pitch * rad);
        float camZ = camera.distance * sin(camera.yaw * rad) * cos(camera.pitch * rad);

        Mat4 view = lookAt(camX, camY, camZ, 0, 0, 0, 0, 1, 0);
        Mat4 projection = perspective(45.0f * rad, aspectRatio, 0.1f, 100.0f);
        Mat4 model;

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, view());
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, projection());
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, model());

        glBindVertexArray(VAO);
        glDrawArrays(GL_POINTS, 0, numPoints);
        glBindVertexArray(0);
    }

    int getNumPoints() const { return numPoints; }
};

// Mouse callback
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            camera.mouseDown = true;
            glfwGetCursorPos(window, &camera.lastX, &camera.lastY);
        } else if (action == GLFW_RELEASE) {
            camera.mouseDown = false;
        }
    }
}

// Cursor position callback
void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    if (camera.mouseDown) {
        double xoffset = xpos - camera.lastX;
        double yoffset = ypos - camera.lastY;

        camera.lastX = xpos;
        camera.lastY = ypos;

        camera.yaw += xoffset * 0.5;
        camera.pitch += yoffset * 0.5;

        // Constrain pitch
        if (camera.pitch > 89.0f) camera.pitch = 89.0f;
        if (camera.pitch < -89.0f) camera.pitch = -89.0f;
    }
}

// Scroll callback
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    camera.distance -= yoffset * 0.5f;
    if (camera.distance < 0.5f) camera.distance = 0.5f;
    if (camera.distance > 20.0f) camera.distance = 20.0f;
}

int main(int argc, char **argv) try {
    std::cout << "=== Submarine 3D Map Viewer ===" << std::endl;

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_REFRESH_RATE, 60);

    // Get primary monitor
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    // Create fullscreen window
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Submarine 3D Map Viewer - Fullscreen (Press ESC to exit)", monitor, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    // Get initial window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // Initialize Orbbec pipeline
    ob::Pipeline pipe;
    std::shared_ptr<ob::Config> config = std::make_shared<ob::Config>();

    // Enable all video streams
    config->enableVideoStream(OB_STREAM_COLOR);
    config->enableVideoStream(OB_STREAM_DEPTH);

    std::mutex frameMutex;
    std::shared_ptr<ob::DepthFrame> lastDepthFrame;

    pipe.start(config, [&](std::shared_ptr<ob::FrameSet> frameset) {
        auto count = frameset->frameCount();
        for(int i = 0; i < count; i++) {
            auto frame = frameset->getFrame(i);
            std::unique_lock<std::mutex> lk(frameMutex);

            if(frame->type() == OB_FRAME_DEPTH) {
                lastDepthFrame = frame->as<ob::DepthFrame>();
            }
        }
    });

    Map3D map3d;

    std::cout << "\n=== System Running ===" << std::endl;
    std::cout << "Mouse Controls:" << std::endl;
    std::cout << "  Left click + drag - Rotate camera" << std::endl;
    std::cout << "  Scroll wheel - Zoom in/out" << std::endl;
    std::cout << "\nPress ESC to exit\n" << std::endl;

    int frame_count = 0;
    auto last_update_time = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Get current window size (for window resizing)
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        // Clear screen
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Update 3D map
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update_time);

        if (lastDepthFrame && elapsed.count() >= 100) {
            std::unique_lock<std::mutex> lock(frameMutex);
            map3d.update(lastDepthFrame);
            last_update_time = now;

            if (frame_count % 30 == 0) {
                std::cout << "✓ 3D map updated - " << map3d.getNumPoints() << " points" << std::endl;
            }
            frame_count++;
        }

        // Render
        float aspectRatio = (float)width / (float)height;
        map3d.render(aspectRatio);

        glfwSwapBuffers(window);
    }

    pipe.stop();
    glfwTerminate();

    std::cout << "\n=== Exiting ===" << std::endl;

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
