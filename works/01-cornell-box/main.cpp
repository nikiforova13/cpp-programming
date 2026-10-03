// Задание №15: Cornell Box. Сцена, камера и освещение читаются из geometry.txt
// и выводятся фиксированным конвейером OpenGL + GLU.
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kDefaultWidth = 800;
constexpr int kDefaultHeight = 800;
constexpr int kLightRows = 2;
constexpr int kLightCols = 4;
constexpr int kMaxLights = 8;  // OpenGL фиксирует не больше восьми источников.

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(Vec3 v) { return std::sqrt(dot(v, v)); }

Vec3 normalize(Vec3 v) {
    const float len = length(v);
    if (len < 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return v * (1.0f / len);
}

Vec3 mix(Vec3 a, Vec3 b, float t) { return a * (1.0f - t) + b * t; }

struct Material {
    float ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    float specular[4] = {0.2f, 0.2f, 0.2f, 1.0f};
    float emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float shininess = 24.0f;
};

struct Quad {
    std::string name;
    Vec3 vertices[4];
    std::string materialName;
    std::string textureName;
    bool castsShadow = false;
};

struct Sphere {
    std::string name;
    Vec3 center;
    float radius = 1.0f;
    std::string materialName;
};

struct Camera {
    Vec3 position{278.0f, 273.0f, -800.0f};
    Vec3 direction{0.0f, 0.0f, 1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float focalLength = 0.035f;
    float sensorWidth = 0.025f;
    float sensorHeight = 0.025f;
};

struct Scene {
    Camera camera;
    std::vector<Quad> quads;
    std::vector<Sphere> spheres;
};

struct OrbitState {
    Vec3 target{278.0f, 273.0f, 280.0f};
    float distance = 1080.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct AppState {
    Scene scene;
    OrbitState orbit;
    Camera defaultCamera;
    int windowWidth = kDefaultWidth;
    int windowHeight = kDefaultHeight;
    int subdivision = 12;
    bool texturesEnabled = true;
    bool shadowsEnabled = true;
    bool antialiasEnabled = true;
    int aaSamples = 4;
    bool mouseDragging = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
    GLuint woodTexture = 0;
    GLuint tileTexture = 0;
    GLUquadric* quadric = nullptr;
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    HGLRC glContext = nullptr;
    bool running = true;
    std::string screenshotPath;
};

AppState g_app;

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool startsWith(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

Material materialByName(const std::string& name) {
    Material material;
    if (name == "red") {
        const float rgb[3] = {0.63f, 0.065f, 0.05f};
        for (int i = 0; i < 3; ++i) {
            material.ambient[i] = rgb[i] * 0.25f;
            material.diffuse[i] = rgb[i];
        }
        material.shininess = 8.0f;
    } else if (name == "green") {
        const float rgb[3] = {0.14f, 0.45f, 0.091f};
        for (int i = 0; i < 3; ++i) {
            material.ambient[i] = rgb[i] * 0.25f;
            material.diffuse[i] = rgb[i];
        }
        material.shininess = 8.0f;
    } else if (name == "light") {
        for (int i = 0; i < 3; ++i) {
            material.ambient[i] = 0.0f;
            material.diffuse[i] = 0.0f;
            material.specular[i] = 0.0f;
            material.emission[i] = 1.0f;
        }
        material.emission[1] = 0.96f;
        material.emission[2] = 0.88f;
    } else if (name == "glass") {
        material.ambient[0] = 0.05f;
        material.ambient[1] = 0.07f;
        material.ambient[2] = 0.08f;
        material.ambient[3] = 0.32f;
        material.diffuse[0] = 0.18f;
        material.diffuse[1] = 0.28f;
        material.diffuse[2] = 0.32f;
        material.diffuse[3] = 0.32f;
        material.specular[0] = 0.95f;
        material.specular[1] = 0.95f;
        material.specular[2] = 0.95f;
        material.shininess = 96.0f;
    } else {
        const float rgb[3] = {0.76f, 0.74f, 0.68f};
        for (int i = 0; i < 3; ++i) {
            material.ambient[i] = rgb[i] * 0.22f;
            material.diffuse[i] = rgb[i];
        }
        material.specular[0] = material.specular[1] = material.specular[2] = 0.28f;
        material.shininess = 20.0f;
    }
    return material;
}

void applyMaterial(const Material& material) {
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, material.ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material.diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, material.specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, material.emission);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, material.shininess);
}

Vec3 quadNormal(const Quad& quad) {
    return normalize(cross(quad.vertices[1] - quad.vertices[0], quad.vertices[2] - quad.vertices[0]));
}

Vec3 bilinear(const Vec3 v[4], float u, float vCoord) {
    const Vec3 a = mix(v[0], v[1], u);
    const Vec3 b = mix(v[3], v[2], u);
    return mix(a, b, vCoord);
}

bool parseVec3(std::istream& in, Vec3& out) {
    return static_cast<bool>(in >> out.x >> out.y >> out.z);
}

bool readVertexLine(std::ifstream& file, Vec3& out, std::string& error, int lineNumber) {
    std::string line;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream in(line);
        if (!parseVec3(in, out)) {
            error = "Не удалось прочитать вершину около строки " + std::to_string(lineNumber);
            return false;
        }
        return true;
    }
    error = "В файле не хватает вершин для очередного прямоугольника";
    return false;
}

bool loadSceneFromFile(const std::string& path, Scene& scene, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Не удалось открыть файл геометрии: " + path;
        return false;
    }

    scene.quads.clear();
    scene.spheres.clear();

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream in(line);
        std::string command;
        in >> command;

        if (command == "camera_position") {
            if (!parseVec3(in, scene.camera.position)) {
                error = "Неверная camera_position в строке " + std::to_string(lineNumber);
                return false;
            }
        } else if (command == "camera_direction") {
            if (!parseVec3(in, scene.camera.direction) || length(scene.camera.direction) < 1e-6f) {
                error = "Неверная camera_direction в строке " + std::to_string(lineNumber);
                return false;
            }
        } else if (command == "camera_up") {
            if (!parseVec3(in, scene.camera.up) || length(scene.camera.up) < 1e-6f) {
                error = "Неверная camera_up в строке " + std::to_string(lineNumber);
                return false;
            }
        } else if (command == "camera_focal") {
            if (!(in >> scene.camera.focalLength) || scene.camera.focalLength <= 0.0f) {
                error = "Неверная camera_focal в строке " + std::to_string(lineNumber);
                return false;
            }
        } else if (command == "camera_sensor") {
            if (!(in >> scene.camera.sensorWidth >> scene.camera.sensorHeight) ||
                scene.camera.sensorWidth <= 0.0f || scene.camera.sensorHeight <= 0.0f) {
                error = "Неверная camera_sensor в строке " + std::to_string(lineNumber);
                return false;
            }
        } else if (command == "quad") {
            Quad quad;
            std::string shadowFlag;
            if (!(in >> quad.name >> quad.materialName >> quad.textureName)) {
                error = "Неверное описание quad в строке " + std::to_string(lineNumber);
                return false;
            }
            if (in >> shadowFlag && shadowFlag == "shadow") {
                quad.castsShadow = true;
            }
            for (int i = 0; i < 4; ++i) {
                if (!readVertexLine(file, quad.vertices[i], error, lineNumber)) {
                    return false;
                }
            }
            if (length(quadNormal(quad)) < 1e-8f) {
                error = "Вырожденный прямоугольник " + quad.name;
                return false;
            }
            scene.quads.push_back(quad);
        } else if (command == "sphere") {
            Sphere sphere;
            if (!(in >> sphere.name >> sphere.materialName >> sphere.center.x >> sphere.center.y >>
                  sphere.center.z >> sphere.radius) ||
                sphere.radius <= 0.0f) {
                error = "Неверное описание sphere в строке " + std::to_string(lineNumber);
                return false;
            }
            scene.spheres.push_back(sphere);
        } else {
            error = "Неизвестная команда '" + command + "' в строке " + std::to_string(lineNumber);
            return false;
        }
    }

    if (scene.quads.empty()) {
        error = "В файле нет ни одного прямоугольника";
        return false;
    }
    return true;
}

void syncOrbitFromCamera(const Camera& camera, OrbitState& orbit) {
    orbit.target = camera.position + normalize(camera.direction) * 1080.0f;
    orbit.target.y = camera.position.y;
    const Vec3 offset = camera.position - orbit.target;
    orbit.distance = length(offset);
    orbit.yaw = std::atan2(offset.x, -offset.z);
    const float horizontal = std::sqrt(offset.x * offset.x + offset.z * offset.z);
    orbit.pitch = std::atan2(offset.y, horizontal);
}

Vec3 orbitEye(const OrbitState& orbit) {
    const float cosPitch = std::cos(orbit.pitch);
    return orbit.target + Vec3{std::sin(orbit.yaw) * cosPitch, std::sin(orbit.pitch),
                               -std::cos(orbit.yaw) * cosPitch} *
                              orbit.distance;
}

float verticalFovDegrees(const Camera& camera) {
    return 2.0f * std::atan((camera.sensorHeight * 0.5f) / camera.focalLength) * 180.0f / kPi;
}

void setProjection(float jitterX, float jitterY) {
    const float aspect = static_cast<float>(g_app.windowWidth) / static_cast<float>(std::max(1, g_app.windowHeight));
    const float fovY = verticalFovDegrees(g_app.scene.camera);
    const float nearPlane = 8.0f;
    const float top = nearPlane * std::tan(fovY * kPi / 360.0f);
    const float right = top * aspect;
    const float dx = jitterX * (2.0f * right) / static_cast<float>(g_app.windowWidth);
    const float dy = jitterY * (2.0f * top) / static_cast<float>(g_app.windowHeight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right - dx, right - dx, -top - dy, top - dy, nearPlane, 4000.0f);
}

void setModelView() {
    const Vec3 eye = orbitEye(g_app.orbit);
    const Vec3 up = g_app.scene.camera.up;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(eye.x, eye.y, eye.z, g_app.orbit.target.x, g_app.orbit.target.y, g_app.orbit.target.z, up.x, up.y,
              up.z);
}

Vec3 lightCenter() {
    for (const Quad& quad : g_app.scene.quads) {
        if (quad.materialName == "light") {
            return (quad.vertices[0] + quad.vertices[1] + quad.vertices[2] + quad.vertices[3]) * 0.25f;
        }
    }
    return {278.0f, 548.0f, 280.0f};
}

void setupLights() {
    const Vec3 center = lightCenter();
    GLfloat globalAmbient[] = {0.16f, 0.16f, 0.15f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, globalAmbient);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    int lightIndex = 0;
    for (int row = 0; row < kLightRows && lightIndex < kMaxLights; ++row) {
        for (int col = 0; col < kLightCols && lightIndex < kMaxLights; ++col) {
            const float u = (col + 0.5f) / kLightCols - 0.5f;
            const float v = (row + 0.5f) / kLightRows - 0.5f;
            const Vec3 position{center.x + u * 90.0f, center.y - 4.0f, center.z + v * 70.0f};
            const GLfloat pos4[] = {position.x, position.y, position.z, 1.0f};
            const GLfloat diffuse[] = {0.55f, 0.52f, 0.46f, 1.0f};
            const GLfloat specular[] = {0.35f, 0.33f, 0.28f, 1.0f};
            const GLenum light = GL_LIGHT0 + lightIndex;
            glEnable(light);
            glLightfv(light, GL_POSITION, pos4);
            glLightfv(light, GL_DIFFUSE, diffuse);
            glLightfv(light, GL_SPECULAR, specular);
            glLightf(light, GL_CONSTANT_ATTENUATION, 0.6f);
            glLightf(light, GL_LINEAR_ATTENUATION, 0.00055f);
            glLightf(light, GL_QUADRATIC_ATTENUATION, 0.0000008f);
            ++lightIndex;
        }
    }
    for (int unused = lightIndex; unused < kMaxLights; ++unused) {
        glDisable(GL_LIGHT0 + unused);
    }
}

GLuint makeTexture(int size, bool wood) {
    std::vector<unsigned char> pixels(static_cast<size_t>(size * size * 3));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const size_t index = static_cast<size_t>((y * size + x) * 3);
            if (wood) {
                const float noise = std::sin(x * 0.11f + std::sin(y * 0.037f) * 6.0f);
                const float grain = 0.5f + 0.5f * noise;
                const Vec3 dark{0.36f, 0.22f, 0.12f};
                const Vec3 light{0.72f, 0.55f, 0.32f};
                const Vec3 color = mix(dark, light, clampf(grain, 0.0f, 1.0f));
                pixels[index] = static_cast<unsigned char>(color.x * 255.0f);
                pixels[index + 1] = static_cast<unsigned char>(color.y * 255.0f);
                pixels[index + 2] = static_cast<unsigned char>(color.z * 255.0f);
            } else {
                const int cell = ((x / 32) + (y / 32)) & 1;
                const unsigned char tone = cell ? 210 : 150;
                pixels[index] = tone;
                pixels[index + 1] = static_cast<unsigned char>(tone - 8);
                pixels[index + 2] = static_cast<unsigned char>(tone - 20);
            }
        }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    return texture;
}

void bindTexture(const std::string& textureName) {
    if (!g_app.texturesEnabled || textureName == "none" || textureName.empty()) {
        glDisable(GL_TEXTURE_2D);
        return;
    }
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    if (textureName == "wood") {
        glBindTexture(GL_TEXTURE_2D, g_app.woodTexture);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_app.tileTexture);
    }
}

void drawSubdividedQuad(const Quad& quad) {
    const Vec3 normal = quadNormal(quad);
    const int divisions = std::max(1, g_app.subdivision);
    applyMaterial(materialByName(quad.materialName));
    bindTexture(quad.textureName);

    glBegin(GL_QUADS);
    glNormal3f(normal.x, normal.y, normal.z);
    for (int i = 0; i < divisions; ++i) {
        for (int j = 0; j < divisions; ++j) {
            const float u0 = static_cast<float>(i) / divisions;
            const float u1 = static_cast<float>(i + 1) / divisions;
            const float v0 = static_cast<float>(j) / divisions;
            const float v1 = static_cast<float>(j + 1) / divisions;
            const Vec3 p00 = bilinear(quad.vertices, u0, v0);
            const Vec3 p10 = bilinear(quad.vertices, u1, v0);
            const Vec3 p11 = bilinear(quad.vertices, u1, v1);
            const Vec3 p01 = bilinear(quad.vertices, u0, v1);
            glTexCoord2f(u0, v0);
            glVertex3f(p00.x, p00.y, p00.z);
            glTexCoord2f(u1, v0);
            glVertex3f(p10.x, p10.y, p10.z);
            glTexCoord2f(u1, v1);
            glVertex3f(p11.x, p11.y, p11.z);
            glTexCoord2f(u0, v1);
            glVertex3f(p01.x, p01.y, p01.z);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void drawSphere(const Sphere& sphere, bool asShadow) {
    glPushMatrix();
    glTranslatef(sphere.center.x, sphere.center.y, sphere.center.z);
    if (asShadow) {
        glColor4f(0.0f, 0.0f, 0.0f, 0.35f);
        gluQuadricDrawStyle(g_app.quadric, GLU_FILL);
        gluSphere(g_app.quadric, sphere.radius, 28, 18);
    } else {
        applyMaterial(materialByName(sphere.materialName));
        gluQuadricNormals(g_app.quadric, GLU_SMOOTH);
        gluSphere(g_app.quadric, sphere.radius, 48, 32);
    }
    glPopMatrix();
}

void makeShadowMatrix(float matrix[16], Vec3 light, float planeY) {
    const float a = 0.0f;
    const float b = 1.0f;
    const float c = 0.0f;
    const float d = -planeY;
    const float lx = light.x;
    const float ly = light.y;
    const float lz = light.z;
    const float lw = 1.0f;
    const float planeDot = a * lx + b * ly + c * lz + d * lw;

    matrix[0] = planeDot - a * lx;
    matrix[4] = -a * ly;
    matrix[8] = -a * lz;
    matrix[12] = -a * lw;
    matrix[1] = -b * lx;
    matrix[5] = planeDot - b * ly;
    matrix[9] = -b * lz;
    matrix[13] = -b * lw;
    matrix[2] = -c * lx;
    matrix[6] = -c * ly;
    matrix[10] = planeDot - c * lz;
    matrix[14] = -c * lw;
    matrix[3] = -d * lx;
    matrix[7] = -d * ly;
    matrix[11] = -d * lz;
    matrix[15] = planeDot - d * lw;
}

void drawShadowCasters(bool asShadow) {
    const int savedSubdivision = g_app.subdivision;
    if (asShadow) {
        g_app.subdivision = 1;
    }
    for (const Quad& quad : g_app.scene.quads) {
        if (quad.castsShadow) {
            if (asShadow) {
                glBegin(GL_QUADS);
                for (const Vec3& vertex : quad.vertices) {
                    glVertex3f(vertex.x, vertex.y, vertex.z);
                }
                glEnd();
            } else {
                drawSubdividedQuad(quad);
            }
        }
    }
    for (const Sphere& sphere : g_app.scene.spheres) {
        drawSphere(sphere, asShadow);
    }
    g_app.subdivision = savedSubdivision;
}

void drawFloorWithStencil() {
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    for (const Quad& quad : g_app.scene.quads) {
        if (quad.name == "Floor") {
            drawSubdividedQuad(quad);
        }
    }
}

void drawShadows() {
    if (!g_app.shadowsEnabled) {
        glDisable(GL_STENCIL_TEST);
        return;
    }

    float matrix[16];
    makeShadowMatrix(matrix, lightCenter(), 0.4f);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor4f(0.0f, 0.0f, 0.0f, 0.38f);

    glPushMatrix();
    glMultMatrixf(matrix);
    drawShadowCasters(true);
    glPopMatrix();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_LIGHTING);
}

void drawOpaqueScene() {
    for (const Quad& quad : g_app.scene.quads) {
        if (quad.name == "Floor" || quad.materialName == "glass") {
            continue;
        }
        drawSubdividedQuad(quad);
    }
}

void drawTransparentObjects() {
    if (g_app.scene.spheres.empty()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const Sphere& sphere : g_app.scene.spheres) {
        drawSphere(sphere, false);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void drawScene(float jitterX, float jitterY) {
    glViewport(0, 0, g_app.windowWidth, g_app.windowHeight);
    glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    setProjection(jitterX, jitterY);
    setModelView();
    setupLights();
    drawFloorWithStencil();
    drawShadows();
    drawOpaqueScene();
    drawTransparentObjects();
}

bool saveBmp(const std::string& path, const std::vector<unsigned char>& rgb, int width, int height) {
    const int rowStride = ((width * 3 + 3) / 4) * 4;
    const int pixelBytes = rowStride * height;
    std::vector<unsigned char> bgr(static_cast<size_t>(pixelBytes), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int src = (y * width + x) * 3;
            const int dst = y * rowStride + x * 3;
            bgr[static_cast<size_t>(dst)] = rgb[static_cast<size_t>(src + 2)];
            bgr[static_cast<size_t>(dst + 1)] = rgb[static_cast<size_t>(src + 1)];
            bgr[static_cast<size_t>(dst + 2)] = rgb[static_cast<size_t>(src)];
        }
    }

    const std::uint32_t fileSize = 54 + static_cast<std::uint32_t>(pixelBytes);
    unsigned char header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    header[2] = static_cast<unsigned char>(fileSize);
    header[3] = static_cast<unsigned char>(fileSize >> 8);
    header[4] = static_cast<unsigned char>(fileSize >> 16);
    header[5] = static_cast<unsigned char>(fileSize >> 24);
    header[10] = 54;
    header[14] = 40;
    header[18] = static_cast<unsigned char>(width);
    header[19] = static_cast<unsigned char>(width >> 8);
    header[20] = static_cast<unsigned char>(width >> 16);
    header[21] = static_cast<unsigned char>(width >> 24);
    header[22] = static_cast<unsigned char>(height);
    header[23] = static_cast<unsigned char>(height >> 8);
    header[24] = static_cast<unsigned char>(height >> 16);
    header[25] = static_cast<unsigned char>(height >> 24);
    header[26] = 1;
    header[28] = 24;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(header), 54);
    out.write(reinterpret_cast<const char*>(bgr.data()), pixelBytes);
    return static_cast<bool>(out);
}

void blitRgb(const std::vector<unsigned char>& pixels, int width, int height) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, width, 0.0, height, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRasterPos2f(0.5f, 0.5f);
    glDrawPixels(width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void captureFramebuffer(std::vector<unsigned char>& pixels, int width, int height) {
    pixels.resize(static_cast<size_t>(width * height * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
}

void renderSupersampled(int samples, std::vector<unsigned char>& pixels) {
    const float jitters[8][2] = {
        {0.5625f, 0.4375f}, {0.0625f, 0.9375f}, {0.3125f, 0.6875f}, {0.6875f, 0.8125f},
        {0.8125f, 0.1875f}, {0.9375f, 0.5625f}, {0.4375f, 0.0625f}, {0.1875f, 0.3125f},
    };
    const int width = g_app.windowWidth;
    const int height = g_app.windowHeight;
    std::vector<float> accum(static_cast<size_t>(width * height * 3), 0.0f);
    std::vector<unsigned char> pass;

    const int usedSamples = std::max(1, std::min(samples, 8));
    for (int sample = 0; sample < usedSamples; ++sample) {
        const float jitterX = jitters[sample][0] - 0.5f;
        const float jitterY = jitters[sample][1] - 0.5f;
        drawScene(jitterX, jitterY);
        captureFramebuffer(pass, width, height);
        for (size_t i = 0; i < pass.size(); ++i) {
            accum[i] += pass[i];
        }
    }

    pixels.resize(accum.size());
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<unsigned char>(accum[i] / static_cast<float>(usedSamples) + 0.5f);
    }
    blitRgb(pixels, width, height);
}

void renderFrame() {
    std::vector<unsigned char> pixels;
    const bool needPixels = g_app.antialiasEnabled || !g_app.screenshotPath.empty();

    if (g_app.antialiasEnabled) {
        renderSupersampled(g_app.aaSamples, pixels);
    } else {
        drawScene(0.0f, 0.0f);
        if (needPixels) {
            captureFramebuffer(pixels, g_app.windowWidth, g_app.windowHeight);
        }
    }

    if (!g_app.screenshotPath.empty()) {
        if (pixels.empty()) {
            captureFramebuffer(pixels, g_app.windowWidth, g_app.windowHeight);
        }
        if (saveBmp(g_app.screenshotPath, pixels, g_app.windowWidth, g_app.windowHeight)) {
            std::cout << "Снимок сохранён: " << g_app.screenshotPath << "\n";
        } else {
            std::cerr << "Не удалось записать снимок: " << g_app.screenshotPath << "\n";
        }
        g_app.screenshotPath.clear();
    }
}

void resetCamera() {
    g_app.scene.camera = g_app.defaultCamera;
    syncOrbitFromCamera(g_app.scene.camera, g_app.orbit);
}

void printHelp() {
    std::cout
        << "Cornell Box (задание №15)\n"
        << "Управление:\n"
        << "  мышь (ЛКМ)     — орбита камеры\n"
        << "  колёсико / +/- — приближение\n"
        << "  стрелки        — орбита\n"
        << "  R              — камера из файла геометрии\n"
        << "  G              — плотность разбиения полигонов\n"
        << "  T              — текстуры\n"
        << "  H              — тени\n"
        << "  A              — сглаживание (суперсэмплинг)\n"
        << "  F5             — сохранить screenshot.bmp\n"
        << "  Esc            — выход\n";
}

void initGl() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
    glClearStencil(0);
    g_app.woodTexture = makeTexture(256, true);
    g_app.tileTexture = makeTexture(256, false);
    g_app.quadric = gluNewQuadric();
}

void destroyGl() {
    if (g_app.quadric) {
        gluDeleteQuadric(g_app.quadric);
        g_app.quadric = nullptr;
    }
    if (g_app.woodTexture) {
        glDeleteTextures(1, &g_app.woodTexture);
    }
    if (g_app.tileTexture) {
        glDeleteTextures(1, &g_app.tileTexture);
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_DESTROY:
            g_app.running = false;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            g_app.windowWidth = std::max(1, static_cast<int>(LOWORD(lParam)));
            g_app.windowHeight = std::max(1, static_cast<int>(HIWORD(lParam)));
            return 0;
        case WM_LBUTTONDOWN:
            g_app.mouseDragging = true;
            g_app.lastMouseX = LOWORD(lParam);
            g_app.lastMouseY = HIWORD(lParam);
            SetCapture(window);
            return 0;
        case WM_LBUTTONUP:
            g_app.mouseDragging = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (g_app.mouseDragging) {
                const int x = LOWORD(lParam);
                const int y = HIWORD(lParam);
                g_app.orbit.yaw += (x - g_app.lastMouseX) * 0.005f;
                g_app.orbit.pitch += (y - g_app.lastMouseY) * 0.005f;
                g_app.orbit.pitch = clampf(g_app.orbit.pitch, -1.2f, 1.2f);
                g_app.lastMouseX = x;
                g_app.lastMouseY = y;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app.orbit.distance *= (delta > 0) ? 0.92f : 1.08f;
            g_app.orbit.distance = clampf(g_app.orbit.distance, 200.0f, 2500.0f);
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_app.running = false;
            } else if (wParam == VK_LEFT) {
                g_app.orbit.yaw -= 0.06f;
            } else if (wParam == VK_RIGHT) {
                g_app.orbit.yaw += 0.06f;
            } else if (wParam == VK_UP) {
                g_app.orbit.pitch = clampf(g_app.orbit.pitch + 0.05f, -1.2f, 1.2f);
            } else if (wParam == VK_DOWN) {
                g_app.orbit.pitch = clampf(g_app.orbit.pitch - 0.05f, -1.2f, 1.2f);
            } else if (wParam == VK_ADD || wParam == 0xBB) {
                g_app.orbit.distance = clampf(g_app.orbit.distance * 0.92f, 200.0f, 2500.0f);
            } else if (wParam == VK_SUBTRACT || wParam == 0xBD) {
                g_app.orbit.distance = clampf(g_app.orbit.distance * 1.08f, 200.0f, 2500.0f);
            } else if (wParam == 'R') {
                resetCamera();
            } else if (wParam == 'G') {
                const int next[] = {1, 4, 8, 12, 16};
                int index = 0;
                for (int i = 0; i < 5; ++i) {
                    if (next[i] == g_app.subdivision) {
                        index = i;
                    }
                }
                g_app.subdivision = next[(index + 1) % 5];
                std::cout << "Разбиение полигонов: " << g_app.subdivision << " x " << g_app.subdivision << "\n";
            } else if (wParam == 'T') {
                g_app.texturesEnabled = !g_app.texturesEnabled;
                std::cout << "Текстуры: " << (g_app.texturesEnabled ? "вкл" : "выкл") << "\n";
            } else if (wParam == 'H') {
                g_app.shadowsEnabled = !g_app.shadowsEnabled;
                std::cout << "Тени: " << (g_app.shadowsEnabled ? "вкл" : "выкл") << "\n";
            } else if (wParam == 'A') {
                g_app.antialiasEnabled = !g_app.antialiasEnabled;
                std::cout << "Сглаживание: " << (g_app.antialiasEnabled ? "вкл" : "выкл") << "\n";
            } else if (wParam == VK_F5) {
                g_app.screenshotPath = "screenshot.bmp";
            }
            return 0;
        default:
            return DefWindowProcA(window, message, wParam, lParam);
    }
}

bool createGlWindow(HINSTANCE instance) {
    WNDCLASSA windowClass = {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "CornellBoxWindow";
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&windowClass)) {
        std::cerr << "Не удалось зарегистрировать класс окна\n";
        return false;
    }

    RECT rect{0, 0, kDefaultWidth, kDefaultHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_app.window = CreateWindowA("CornellBoxWindow", "Cornell Box", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                 nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
        std::cerr << "Не удалось создать окно\n";
        return false;
    }

    g_app.deviceContext = GetDC(g_app.window);
    PIXELFORMATDESCRIPTOR pixelFormat = {};
    pixelFormat.nSize = sizeof(pixelFormat);
    pixelFormat.nVersion = 1;
    pixelFormat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixelFormat.iPixelType = PFD_TYPE_RGBA;
    pixelFormat.cColorBits = 32;
    pixelFormat.cDepthBits = 24;
    pixelFormat.cStencilBits = 8;
    pixelFormat.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(g_app.deviceContext, &pixelFormat);
    if (format == 0 || !SetPixelFormat(g_app.deviceContext, format, &pixelFormat)) {
        std::cerr << "Не удалось выбрать формат пикселей OpenGL\n";
        return false;
    }

    g_app.glContext = wglCreateContext(g_app.deviceContext);
    if (!g_app.glContext || !wglMakeCurrent(g_app.deviceContext, g_app.glContext)) {
        std::cerr << "Не удалось создать контекст OpenGL\n";
        return false;
    }
    return true;
}

void destroyGlWindow() {
    destroyGl();
    if (g_app.glContext) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_app.glContext);
        g_app.glContext = nullptr;
    }
    if (g_app.deviceContext && g_app.window) {
        ReleaseDC(g_app.window, g_app.deviceContext);
        g_app.deviceContext = nullptr;
    }
    if (g_app.window) {
        DestroyWindow(g_app.window);
        g_app.window = nullptr;
    }
}

std::string siblingPath(const std::string& fileName) {
    char modulePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    std::string path = modulePath;
    const auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return fileName;
    }
    return path.substr(0, slash + 1) + fileName;
}

bool loadSceneOrExit(const std::string& requestedPath) {
    const std::string candidates[] = {requestedPath, "geometry.txt", siblingPath("geometry.txt")};
    std::string error;
    for (const std::string& path : candidates) {
        if (path.empty()) {
            continue;
        }
        if (loadSceneFromFile(path, g_app.scene, error)) {
            std::cout << "Загружена геометрия: " << path << "\n";
            return true;
        }
        if (!requestedPath.empty() && path == requestedPath) {
            std::cerr << error << "\n";
            return false;
        }
    }
    std::cerr << "Не найден geometry.txt. Положите файл рядом с программой или укажите путь аргументом.\n";
    if (!error.empty()) {
        std::cerr << error << "\n";
    }
    return false;
}

void printUsage() {
    std::cout << "Использование:\n"
              << "  cornell-box.exe [geometry.txt]\n"
              << "  cornell-box.exe [geometry.txt] --screenshot output.bmp\n"
              << "  cornell-box.exe --help\n";
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string geometryPath;
    std::string screenshotPath;
    bool screenshotOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            printHelp();
            return 0;
        }
        if (arg == "--screenshot") {
            if (i + 1 >= argc) {
                std::cerr << "После --screenshot нужно указать имя BMP-файла\n";
                return 1;
            }
            screenshotPath = argv[++i];
            screenshotOnly = true;
        } else if (startsWith(arg, "--")) {
            std::cerr << "Неизвестный аргумент: " << arg << "\n";
            printUsage();
            return 1;
        } else {
            geometryPath = arg;
        }
    }

    if (!loadSceneOrExit(geometryPath)) {
        return 1;
    }

    g_app.defaultCamera = g_app.scene.camera;
    syncOrbitFromCamera(g_app.scene.camera, g_app.orbit);
    printHelp();

    if (!createGlWindow(GetModuleHandleA(nullptr))) {
        return 1;
    }
    initGl();

    if (screenshotOnly) {
        g_app.screenshotPath = screenshotPath;
        g_app.aaSamples = 8;
        g_app.antialiasEnabled = true;
        renderFrame();
        SwapBuffers(g_app.deviceContext);
        destroyGlWindow();
        return 0;
    }

    MSG message = {};
    while (g_app.running) {
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                g_app.running = false;
            }
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        renderFrame();
        SwapBuffers(g_app.deviceContext);
    }

    destroyGlWindow();
    return 0;
}
