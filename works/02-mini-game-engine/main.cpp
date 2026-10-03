// Самостоятельная работа №2: простой игровой движок на OpenGL.
// Сцена читается из текстового файла, а текстуры — из файлов PPM (P3).
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>
#include <GL/glu.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 720;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kFieldOfView = 50.0f;
constexpr float kWorldLimit = 7.5f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec3 operator+(Vec3 left, Vec3 right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 operator-(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 operator*(Vec3 value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float dot(Vec3 left, Vec3 right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(Vec3 left, Vec3 right) {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

float length(Vec3 value) {
    return std::sqrt(dot(value, value));
}

Vec3 normalize(Vec3 value) {
    const float valueLength = length(value);
    if (valueLength < 1e-7f) {
        return {};
    }
    return value * (1.0f / valueLength);
}

float clampf(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(value, maximum));
}

enum class Shape {
    Cube,
    Sphere,
    Carrot,
};

enum class Appearance {
    Texture,
    Material,
};

enum class Role {
    Player,
    Collectible,
    Obstacle,
};

struct Camera {
    Vec3 eye{9.0f, 8.0f, 14.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
};

struct SceneObject {
    std::string name;
    Shape shape = Shape::Cube;
    Vec3 position;
    Vec3 initialPosition;
    float size = 1.0f;
    Appearance appearance = Appearance::Material;
    std::string appearanceValue;
    Role role = Role::Obstacle;
    GLuint textureId = 0;
    bool active = true;
};

struct Scene {
    Camera camera;
    Vec3 lightPosition{4.0f, 9.0f, 6.0f};
    std::string backgroundTexturePath;
    GLuint backgroundTextureId = 0;
    std::vector<SceneObject> objects;
};

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

struct Material {
    GLfloat ambient[4] = {0.18f, 0.18f, 0.18f, 1.0f};
    GLfloat diffuse[4] = {0.72f, 0.72f, 0.72f, 1.0f};
    GLfloat specular[4] = {0.25f, 0.25f, 0.25f, 1.0f};
    GLfloat emission[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    GLfloat shininess = 28.0f;
};

struct AppState {
    Scene scene;
    int windowWidth = kDefaultWidth;
    int windowHeight = kDefaultHeight;
    int selectedObject = -1;
    Vec3 dragOffset;
    bool texturesEnabled = true;
    bool shadowsEnabled = true;
    bool antialiasEnabled = true;
    bool needsRedraw = true;
    bool running = true;
    int score = 0;
    int collectibleCount = 0;
    bool won = false;
    HWND window = nullptr;
    HDC deviceContext = nullptr;
    HGLRC glContext = nullptr;
    GLUquadric* quadric = nullptr;
};

AppState g_app;

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool isAbsolutePath(const std::string& path) {
    return (!path.empty() && (path[0] == '\\' || path[0] == '/')) ||
           (path.size() > 1 && path[1] == ':');
}

std::string joinPath(const std::string& directory, const std::string& path) {
    if (isAbsolutePath(path) || directory.empty() || directory == ".") {
        return path;
    }
    return directory + "\\" + path;
}

std::string siblingPath(const std::string& fileName) {
    char executablePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, executablePath, MAX_PATH) == 0) {
        return fileName;
    }
    return joinPath(directoryOf(executablePath), fileName);
}

bool parseShape(const std::string& text, Shape& shape) {
    if (text == "cube") {
        shape = Shape::Cube;
        return true;
    }
    if (text == "sphere") {
        shape = Shape::Sphere;
        return true;
    }
    if (text == "carrot") {
        shape = Shape::Carrot;
        return true;
    }
    return false;
}

bool parseAppearance(const std::string& text, Appearance& appearance) {
    if (text == "texture") {
        appearance = Appearance::Texture;
        return true;
    }
    if (text == "material") {
        appearance = Appearance::Material;
        return true;
    }
    return false;
}

bool parseRole(const std::string& text, Role& role) {
    if (text == "player") {
        role = Role::Player;
        return true;
    }
    if (text == "collectible") {
        role = Role::Collectible;
        return true;
    }
    if (text == "obstacle") {
        role = Role::Obstacle;
        return true;
    }
    return false;
}

bool isKnownMaterial(const std::string& name) {
    return name == "gold" || name == "ruby" || name == "jade" ||
           name == "stone" || name == "carrot";
}

bool loadSceneFile(const std::string& path, Scene& scene, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Не удалось открыть файл сцены: " + path;
        return false;
    }

    Scene loadedScene;
    bool hasCamera = false;
    bool hasLight = false;
    bool hasBackground = false;
    int playerCount = 0;
    int collectibleCount = 0;
    const std::string baseDirectory = directoryOf(path);

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        std::istringstream input(line);
        std::string command;
        input >> command;

        if (command == "camera") {
            if (!(input >> loadedScene.camera.eye.x >> loadedScene.camera.eye.y >>
                  loadedScene.camera.eye.z >> loadedScene.camera.target.x >>
                  loadedScene.camera.target.y >> loadedScene.camera.target.z)) {
                error = "Неверная команда camera в строке " + std::to_string(lineNumber);
                return false;
            }
            hasCamera = true;
        } else if (command == "light") {
            if (!(input >> loadedScene.lightPosition.x >> loadedScene.lightPosition.y >>
                  loadedScene.lightPosition.z) ||
                loadedScene.lightPosition.y <= 0.0f) {
                error = "Неверная команда light в строке " + std::to_string(lineNumber);
                return false;
            }
            hasLight = true;
        } else if (command == "background") {
            std::string texturePath;
            if (!(input >> texturePath)) {
                error = "Не указан файл фоновой текстуры в строке " +
                        std::to_string(lineNumber);
                return false;
            }
            loadedScene.backgroundTexturePath = joinPath(baseDirectory, texturePath);
            hasBackground = true;
        } else if (command == "object") {
            SceneObject object;
            std::string shapeName;
            std::string appearanceName;
            std::string roleName;
            if (!(input >> object.name >> shapeName >> object.position.x >> object.position.y >>
                  object.position.z >> object.size >> appearanceName >>
                  object.appearanceValue >> roleName) ||
                object.size <= 0.0f) {
                error = "Неверное описание object в строке " + std::to_string(lineNumber);
                return false;
            }
            if (!parseShape(shapeName, object.shape)) {
                error = "Неизвестная форма '" + shapeName + "' в строке " +
                        std::to_string(lineNumber);
                return false;
            }
            if (!parseAppearance(appearanceName, object.appearance)) {
                error = "Неизвестный тип оформления '" + appearanceName + "' в строке " +
                        std::to_string(lineNumber);
                return false;
            }
            if (!parseRole(roleName, object.role)) {
                error = "Неизвестная роль '" + roleName + "' в строке " +
                        std::to_string(lineNumber);
                return false;
            }
            if (object.appearance == Appearance::Material &&
                !isKnownMaterial(object.appearanceValue)) {
                error = "Неизвестный материал '" + object.appearanceValue +
                        "' в строке " + std::to_string(lineNumber);
                return false;
            }
            if (object.appearance == Appearance::Texture) {
                object.appearanceValue = joinPath(baseDirectory, object.appearanceValue);
            }
            for (const SceneObject& existing : loadedScene.objects) {
                if (existing.name == object.name) {
                    error = "Имя объекта '" + object.name + "' повторяется";
                    return false;
                }
            }
            object.initialPosition = object.position;
            loadedScene.objects.push_back(object);
            playerCount += object.role == Role::Player ? 1 : 0;
            collectibleCount += object.role == Role::Collectible ? 1 : 0;
        } else {
            error = "Неизвестная команда '" + command + "' в строке " +
                    std::to_string(lineNumber);
            return false;
        }

        std::string extra;
        if (input >> extra) {
            error = "Лишние данные в строке " + std::to_string(lineNumber);
            return false;
        }
    }

    if (!hasCamera || length(loadedScene.camera.target - loadedScene.camera.eye) < 0.1f) {
        error = "В сцене отсутствует корректная команда camera";
        return false;
    }
    const Vec3 viewDirection = normalize(loadedScene.camera.target - loadedScene.camera.eye);
    if (length(cross(viewDirection, {0.0f, 1.0f, 0.0f})) < 0.01f) {
        error = "Направление камеры не должно быть параллельно вертикальной оси";
        return false;
    }
    if (!hasLight) {
        error = "В сцене отсутствует команда light";
        return false;
    }
    if (!hasBackground) {
        error = "В сцене отсутствует команда background";
        return false;
    }
    if (loadedScene.objects.size() < 2) {
        error = "В сцене должно быть не менее двух моделей";
        return false;
    }
    if (playerCount != 1) {
        error = "В сцене должен быть ровно один объект с ролью player";
        return false;
    }
    if (collectibleCount == 0) {
        error = "В сцене должен быть хотя бы один объект collectible";
        return false;
    }

    scene = loadedScene;
    return true;
}

bool loadRequestedScene(const std::string& requestedPath, std::string& loadedPath,
                        std::string& error) {
    if (!requestedPath.empty()) {
        if (loadSceneFile(requestedPath, g_app.scene, error)) {
            loadedPath = requestedPath;
            return true;
        }
        return false;
    }

    const std::string candidates[] = {"scene.txt", siblingPath("scene.txt")};
    for (const std::string& candidate : candidates) {
        if (loadSceneFile(candidate, g_app.scene, error)) {
            loadedPath = candidate;
            return true;
        }
    }
    error = "Не найден scene.txt. Запустите программу из папки работы или укажите "
            "путь к сцене аргументом.";
    return false;
}

bool readPpmToken(std::istream& input, std::string& token) {
    token.clear();
    char character = '\0';
    while (input.get(character)) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            continue;
        }
        if (character == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(character);
        break;
    }
    while (input.get(character)) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            break;
        }
        if (character == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        token.push_back(character);
    }
    return !token.empty();
}

bool parseInteger(const std::string& token, int& value) {
    try {
        std::size_t parsed = 0;
        value = std::stoi(token, &parsed);
        return parsed == token.size();
    } catch (...) {
        return false;
    }
}

bool loadPpmTexture(const std::string& path, GLuint& textureId, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "Не удалось открыть текстуру: " + path;
        return false;
    }

    std::string token;
    if (!readPpmToken(file, token) || token != "P3") {
        error = "Текстура должна быть в текстовом формате PPM P3: " + path;
        return false;
    }

    int width = 0;
    int height = 0;
    int maximum = 0;
    if (!readPpmToken(file, token) || !parseInteger(token, width) ||
        !readPpmToken(file, token) || !parseInteger(token, height) ||
        !readPpmToken(file, token) || !parseInteger(token, maximum) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        maximum <= 0 || maximum > 255) {
        error = "Некорректный заголовок PPM: " + path;
        return false;
    }

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
    for (unsigned char& channel : pixels) {
        int value = 0;
        if (!readPpmToken(file, token) || !parseInteger(token, value) ||
            value < 0 || value > maximum) {
            error = "Некорректные пиксели PPM: " + path;
            return false;
        }
        channel = static_cast<unsigned char>(value * 255 / maximum);
    }

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels.data());
    return true;
}

Material materialByName(const std::string& name) {
    Material material;
    if (name == "gold") {
        const GLfloat ambient[] = {0.25f, 0.16f, 0.03f};
        const GLfloat diffuse[] = {0.95f, 0.62f, 0.10f};
        const GLfloat specular[] = {1.0f, 0.86f, 0.45f};
        for (int index = 0; index < 3; ++index) {
            material.ambient[index] = ambient[index];
            material.diffuse[index] = diffuse[index];
            material.specular[index] = specular[index];
        }
        material.shininess = 72.0f;
    } else if (name == "ruby") {
        material.ambient[0] = 0.22f;
        material.ambient[1] = 0.02f;
        material.ambient[2] = 0.04f;
        material.diffuse[0] = 0.82f;
        material.diffuse[1] = 0.08f;
        material.diffuse[2] = 0.12f;
        material.specular[0] = 0.9f;
        material.specular[1] = 0.55f;
        material.specular[2] = 0.55f;
        material.shininess = 64.0f;
    } else if (name == "jade") {
        material.ambient[0] = 0.03f;
        material.ambient[1] = 0.18f;
        material.ambient[2] = 0.08f;
        material.diffuse[0] = 0.08f;
        material.diffuse[1] = 0.68f;
        material.diffuse[2] = 0.28f;
        material.specular[0] = 0.35f;
        material.specular[1] = 0.8f;
        material.specular[2] = 0.45f;
        material.shininess = 48.0f;
    } else if (name == "carrot") {
        material.ambient[0] = 0.28f;
        material.ambient[1] = 0.09f;
        material.ambient[2] = 0.01f;
        material.diffuse[0] = 1.0f;
        material.diffuse[1] = 0.34f;
        material.diffuse[2] = 0.025f;
        material.specular[0] = 0.38f;
        material.specular[1] = 0.18f;
        material.specular[2] = 0.05f;
        material.shininess = 22.0f;
    } else if (name == "stone") {
        material.ambient[0] = 0.16f;
        material.ambient[1] = 0.18f;
        material.ambient[2] = 0.20f;
        material.diffuse[0] = 0.48f;
        material.diffuse[1] = 0.53f;
        material.diffuse[2] = 0.58f;
        material.shininess = 10.0f;
    }
    return material;
}

Material selectedMaterial() {
    Material material;
    material.ambient[0] = 0.35f;
    material.ambient[1] = 0.22f;
    material.ambient[2] = 0.01f;
    material.diffuse[0] = 1.0f;
    material.diffuse[1] = 0.72f;
    material.diffuse[2] = 0.08f;
    material.specular[0] = 1.0f;
    material.specular[1] = 1.0f;
    material.specular[2] = 0.7f;
    material.emission[0] = 0.16f;
    material.emission[1] = 0.08f;
    material.shininess = 80.0f;
    return material;
}

void applyMaterial(const Material& material) {
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, material.ambient);
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, material.diffuse);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, material.specular);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, material.emission);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, material.shininess);
}

void drawUnitCube() {
    static const GLfloat vertices[6][4][3] = {
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
         {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f},
         {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f},
         {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}},
        {{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f},
         {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
         {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
         {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}},
    };
    static const GLfloat normals[6][3] = {
        {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},  {0.0f, -1.0f, 0.0f},
    };
    static const GLfloat textureCoordinates[4][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };

    glBegin(GL_QUADS);
    for (int face = 0; face < 6; ++face) {
        glNormal3fv(normals[face]);
        for (int vertex = 0; vertex < 4; ++vertex) {
            glTexCoord2fv(textureCoordinates[vertex]);
            glVertex3fv(vertices[face][vertex]);
        }
    }
    glEnd();
}

void drawCarrotGeometry(const SceneObject& object, bool usePartMaterials) {
    const float bodyLength = object.size * 0.9f;
    const float bodyRadius = object.size * 0.27f;
    const float topY = bodyLength * 0.5f;

    if (usePartMaterials) {
        applyMaterial(materialByName("carrot"));
    }
    glPushMatrix();
    glTranslatef(0.0f, topY, 0.0f);
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    gluDisk(g_app.quadric, 0.0, bodyRadius, 24, 1);
    gluCylinder(g_app.quadric, bodyRadius, 0.015f * object.size,
                bodyLength, 28, 5);
    glPopMatrix();

    if (usePartMaterials) {
        applyMaterial(materialByName("jade"));
    }
    constexpr float leafOffsets[3][2] = {
        {-0.12f, 0.02f}, {0.10f, 0.08f}, {0.0f, -0.10f}};
    constexpr float leafTilts[3] = {-13.0f, 15.0f, 3.0f};
    for (int leaf = 0; leaf < 3; ++leaf) {
        glPushMatrix();
        glTranslatef(leafOffsets[leaf][0] * object.size, topY,
                     leafOffsets[leaf][1] * object.size);
        glRotatef(leafTilts[leaf], 0.0f, 0.0f, 1.0f);
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        gluCylinder(g_app.quadric, object.size * 0.055f,
                    object.size * 0.018f, object.size * 0.42f, 10, 2);
        glPopMatrix();
    }
}

void drawObjectGeometry(const SceneObject& object, bool usePartMaterials) {
    glPushMatrix();
    glTranslatef(object.position.x, object.position.y, object.position.z);
    if (object.shape == Shape::Cube) {
        glScalef(object.size, object.size, object.size);
        drawUnitCube();
    } else if (object.shape == Shape::Sphere) {
        gluSphere(g_app.quadric, object.size * 0.5f, 36, 24);
    } else {
        drawCarrotGeometry(object, usePartMaterials);
    }
    glPopMatrix();
}

void drawObject(const SceneObject& object, bool selected) {
    const bool useTexture = g_app.texturesEnabled &&
                            object.appearance == Appearance::Texture &&
                            object.textureId != 0;
    applyMaterial(selected ? selectedMaterial()
                           : materialByName(object.appearanceValue));
    if (useTexture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, object.textureId);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        gluQuadricTexture(g_app.quadric, GL_TRUE);
    } else {
        glDisable(GL_TEXTURE_2D);
        gluQuadricTexture(g_app.quadric, GL_FALSE);
    }
    drawObjectGeometry(object, object.shape == Shape::Carrot && !selected);
    glDisable(GL_TEXTURE_2D);
}

void drawBackground() {
    const bool useTexture = g_app.texturesEnabled &&
                            g_app.scene.backgroundTextureId != 0;
    applyMaterial(materialByName("stone"));
    if (useTexture) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_app.scene.backgroundTextureId);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glBegin(GL_QUADS);
    glNormal3f(0.0f, 1.0f, 0.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-9.0f, 0.0f, -8.0f);
    glTexCoord2f(8.0f, 0.0f);
    glVertex3f(9.0f, 0.0f, -8.0f);
    glTexCoord2f(8.0f, 8.0f);
    glVertex3f(9.0f, 0.0f, 9.0f);
    glTexCoord2f(0.0f, 8.0f);
    glVertex3f(-9.0f, 0.0f, 9.0f);

    glNormal3f(0.0f, 0.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f);
    glVertex3f(-9.0f, 0.0f, -8.0f);
    glTexCoord2f(6.0f, 0.0f);
    glVertex3f(9.0f, 0.0f, -8.0f);
    glTexCoord2f(6.0f, 3.0f);
    glVertex3f(9.0f, 7.0f, -8.0f);
    glTexCoord2f(0.0f, 3.0f);
    glVertex3f(-9.0f, 7.0f, -8.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void makeShadowMatrix(GLfloat matrix[16], Vec3 light, float planeY) {
    const GLfloat plane[4] = {0.0f, 1.0f, 0.0f, -planeY};
    const GLfloat lightVector[4] = {light.x, light.y, light.z, 1.0f};
    const GLfloat planeDotLight =
        plane[0] * lightVector[0] + plane[1] * lightVector[1] +
        plane[2] * lightVector[2] + plane[3] * lightVector[3];

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            matrix[column * 4 + row] =
                (row == column ? planeDotLight : 0.0f) -
                lightVector[row] * plane[column];
        }
    }
}

void drawShadows() {
    if (!g_app.shadowsEnabled) {
        return;
    }

    GLfloat shadowMatrix[16] = {};
    makeShadowMatrix(shadowMatrix, g_app.scene.lightPosition, 0.015f);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.02f, 0.02f, 0.03f, 0.38f);
    glPushMatrix();
    glMultMatrixf(shadowMatrix);
    for (const SceneObject& object : g_app.scene.objects) {
        if (object.active) {
            drawObjectGeometry(object, false);
        }
    }
    glPopMatrix();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void setProjection(float jitterX, float jitterY) {
    const float aspect =
        static_cast<float>(g_app.windowWidth) /
        static_cast<float>(std::max(1, g_app.windowHeight));
    constexpr float nearPlane = 0.1f;
    const float top = nearPlane * std::tan(kFieldOfView * kPi / 360.0f);
    const float right = top * aspect;
    const float dx = jitterX * 2.0f * right / static_cast<float>(g_app.windowWidth);
    const float dy = jitterY * 2.0f * top / static_cast<float>(g_app.windowHeight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right - dx, right - dx, -top - dy, top - dy, nearPlane, 100.0f);
}

void setModelView() {
    const Camera& camera = g_app.scene.camera;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(camera.eye.x, camera.eye.y, camera.eye.z,
              camera.target.x, camera.target.y, camera.target.z,
              0.0f, 1.0f, 0.0f);
}

void setupLight() {
    const GLfloat position[] = {
        g_app.scene.lightPosition.x, g_app.scene.lightPosition.y,
        g_app.scene.lightPosition.z, 1.0f};
    const GLfloat diffuse[] = {0.95f, 0.92f, 0.84f, 1.0f};
    const GLfloat specular[] = {1.0f, 0.96f, 0.86f, 1.0f};
    const GLfloat ambient[] = {0.16f, 0.17f, 0.19f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.75f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0.025f);
}

void drawScene(float jitterX, float jitterY) {
    glViewport(0, 0, g_app.windowWidth, g_app.windowHeight);
    glClearColor(0.055f, 0.075f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setProjection(jitterX, jitterY);
    setModelView();
    setupLight();
    drawBackground();
    drawShadows();
    for (std::size_t index = 0; index < g_app.scene.objects.size(); ++index) {
        const SceneObject& object = g_app.scene.objects[index];
        if (object.active) {
            drawObject(object, static_cast<int>(index) == g_app.selectedObject);
        }
    }
}

void captureBackBuffer(std::vector<unsigned char>& pixels) {
    pixels.resize(static_cast<std::size_t>(g_app.windowWidth) *
                  static_cast<std::size_t>(g_app.windowHeight) * 3U);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, g_app.windowWidth, g_app.windowHeight,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
}

void drawPixelsToBackBuffer(const std::vector<unsigned char>& pixels) {
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, g_app.windowWidth, 0.0, g_app.windowHeight, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glRasterPos2f(0.0f, 0.0f);
    glDrawPixels(g_app.windowWidth, g_app.windowHeight,
                 GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void drawVictoryGlyph(int glyph, float x, float y, float width, float height) {
    const auto line = [=](float x1, float y1, float x2, float y2) {
        glVertex2f(x + x1 * width, y + y1 * height);
        glVertex2f(x + x2 * width, y + y2 * height);
    };

    switch (glyph) {
        case 0:  // П
            line(0.0f, 0.0f, 0.0f, 1.0f);
            line(0.0f, 1.0f, 1.0f, 1.0f);
            line(1.0f, 1.0f, 1.0f, 0.0f);
            break;
        case 1:  // О
            line(0.0f, 0.0f, 0.0f, 1.0f);
            line(0.0f, 1.0f, 1.0f, 1.0f);
            line(1.0f, 1.0f, 1.0f, 0.0f);
            line(1.0f, 0.0f, 0.0f, 0.0f);
            break;
        case 2:  // Б
            line(0.0f, 0.0f, 0.0f, 1.0f);
            line(0.0f, 1.0f, 1.0f, 1.0f);
            line(0.0f, 0.55f, 0.82f, 0.55f);
            line(0.82f, 0.55f, 1.0f, 0.38f);
            line(1.0f, 0.38f, 1.0f, 0.16f);
            line(1.0f, 0.16f, 0.82f, 0.0f);
            line(0.82f, 0.0f, 0.0f, 0.0f);
            break;
        case 3:  // Е
            line(0.0f, 0.0f, 0.0f, 1.0f);
            line(0.0f, 1.0f, 1.0f, 1.0f);
            line(0.0f, 0.52f, 0.82f, 0.52f);
            line(0.0f, 0.0f, 1.0f, 0.0f);
            break;
        case 4:  // Д
            line(0.0f, 0.14f, 1.0f, 0.14f);
            line(0.12f, 0.14f, 0.30f, 1.0f);
            line(0.30f, 1.0f, 0.82f, 1.0f);
            line(0.82f, 1.0f, 0.88f, 0.14f);
            line(0.0f, 0.14f, 0.0f, 0.0f);
            line(1.0f, 0.14f, 1.0f, 0.0f);
            break;
        case 5:  // А
            line(0.0f, 0.0f, 0.5f, 1.0f);
            line(0.5f, 1.0f, 1.0f, 0.0f);
            line(0.23f, 0.43f, 0.77f, 0.43f);
            break;
        case 6:  // !
            line(0.5f, 0.28f, 0.5f, 1.0f);
            line(0.5f, 0.0f, 0.5f, 0.08f);
            break;
        default:
            break;
    }
}

void drawVictoryOverlay() {
    if (!g_app.won) {
        return;
    }

    const float glyphHeight = std::min(g_app.windowHeight * 0.22f,
                                       g_app.windowWidth * 0.16f);
    const float glyphWidth = glyphHeight * 0.58f;
    const float gap = glyphHeight * 0.13f;
    const float exclamationWidth = glyphWidth * 0.38f;
    const float totalWidth =
        glyphWidth * 6.0f + gap * 6.0f + exclamationWidth;
    const float startX = (g_app.windowWidth - totalWidth) * 0.5f;
    const float startY = (g_app.windowHeight - glyphHeight) * 0.56f;
    const float margin = glyphHeight * 0.25f;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_LINE_BIT |
                 GL_CURRENT_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, g_app.windowWidth, 0.0, g_app.windowHeight, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(0.08f, 0.01f, 0.12f, 0.78f);
    glBegin(GL_QUADS);
    glVertex2f(startX - margin, startY - margin);
    glVertex2f(startX + totalWidth + margin, startY - margin);
    glVertex2f(startX + totalWidth + margin,
               startY + glyphHeight + margin);
    glVertex2f(startX - margin, startY + glyphHeight + margin);
    glEnd();

    constexpr int glyphs[] = {0, 1, 2, 3, 4, 5, 6};
    for (int pass = 0; pass < 2; ++pass) {
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        if (pass == 0) {
            glLineWidth(13.0f);
            glColor4f(1.0f, 0.18f, 0.0f, 0.78f);
        } else {
            glLineWidth(5.0f);
            glColor4f(1.0f, 0.96f, 0.22f, 1.0f);
        }

        float glyphX = startX;
        glBegin(GL_LINES);
        for (int glyph : glyphs) {
            const float currentWidth =
                glyph == 6 ? exclamationWidth : glyphWidth;
            drawVictoryGlyph(glyph, glyphX, startY,
                             currentWidth, glyphHeight);
            glyphX += currentWidth + gap;
        }
        glEnd();
    }

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

void renderFrame() {
    if (!g_app.antialiasEnabled) {
        drawScene(0.0f, 0.0f);
        drawVictoryOverlay();
        return;
    }

    constexpr float jitters[4][2] = {
        {-0.25f, -0.25f}, {0.25f, -0.25f},
        {-0.25f, 0.25f},  {0.25f, 0.25f},
    };
    const std::size_t channelCount =
        static_cast<std::size_t>(g_app.windowWidth) *
        static_cast<std::size_t>(g_app.windowHeight) * 3U;
    std::vector<unsigned int> accumulation(channelCount, 0U);
    std::vector<unsigned char> pass;

    for (const auto& jitter : jitters) {
        drawScene(jitter[0], jitter[1]);
        captureBackBuffer(pass);
        for (std::size_t index = 0; index < pass.size(); ++index) {
            accumulation[index] += pass[index];
        }
    }

    std::vector<unsigned char> averaged(channelCount);
    for (std::size_t index = 0; index < averaged.size(); ++index) {
        averaged[index] =
            static_cast<unsigned char>((accumulation[index] + 2U) / 4U);
    }
    drawPixelsToBackBuffer(averaged);
    drawVictoryOverlay();
}

int playerIndex() {
    for (std::size_t index = 0; index < g_app.scene.objects.size(); ++index) {
        if (g_app.scene.objects[index].role == Role::Player) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void updateWindowTitle() {
    std::ostringstream title;
    title << "OpenGL Carrot Collector - Score " << g_app.score << "/"
          << g_app.collectibleCount;
    if (g_app.won) {
        title << " - Victory! Press R";
    }
    SetWindowTextA(g_app.window, title.str().c_str());
}

void collectOverlappingObjects() {
    const int player = playerIndex();
    if (player < 0) {
        return;
    }
    const SceneObject& playerObject =
        g_app.scene.objects[static_cast<std::size_t>(player)];
    bool changed = false;
    for (std::size_t index = 0; index < g_app.scene.objects.size(); ++index) {
        SceneObject& object = g_app.scene.objects[index];
        if (!object.active || object.role != Role::Collectible) {
            continue;
        }
        const float dx = playerObject.position.x - object.position.x;
        const float dz = playerObject.position.z - object.position.z;
        const float collisionDistance =
            (playerObject.size + object.size) * 0.43f;
        if (dx * dx + dz * dz <= collisionDistance * collisionDistance) {
            object.active = false;
            ++g_app.score;
            changed = true;
            if (g_app.selectedObject == static_cast<int>(index)) {
                g_app.selectedObject = -1;
            }
            std::cout << "Собран объект " << object.name << " ("
                      << g_app.score << "/" << g_app.collectibleCount << ")\n";
        }
    }
    if (changed) {
        g_app.won = g_app.score == g_app.collectibleCount;
        if (g_app.won) {
            std::cout << "Все морковки собраны. Сценарий завершён!\n";
        }
        updateWindowTitle();
        g_app.needsRedraw = true;
    }
}

bool playerCollidesWithObstacle(Vec3 proposedPosition) {
    const int player = playerIndex();
    if (player < 0) {
        return false;
    }
    const SceneObject& playerObject =
        g_app.scene.objects[static_cast<std::size_t>(player)];
    for (const SceneObject& object : g_app.scene.objects) {
        if (!object.active || object.role != Role::Obstacle) {
            continue;
        }
        const float dx = proposedPosition.x - object.position.x;
        const float dz = proposedPosition.z - object.position.z;
        const float minimumDistance =
            (playerObject.size + object.size) * 0.48f;
        if (dx * dx + dz * dz < minimumDistance * minimumDistance) {
            return true;
        }
    }
    return false;
}

void placeObject(int objectIndex, Vec3 proposedPosition) {
    if (objectIndex < 0 ||
        objectIndex >= static_cast<int>(g_app.scene.objects.size())) {
        return;
    }
    SceneObject& object =
        g_app.scene.objects[static_cast<std::size_t>(objectIndex)];
    proposedPosition.x = clampf(proposedPosition.x, -kWorldLimit, kWorldLimit);
    proposedPosition.z = clampf(proposedPosition.z, -kWorldLimit, kWorldLimit);
    proposedPosition.y = object.position.y;
    if (object.role == Role::Player &&
        playerCollidesWithObstacle(proposedPosition)) {
        return;
    }
    object.position = proposedPosition;
    collectOverlappingObjects();
    g_app.needsRedraw = true;
}

void movePlayer(float dx, float dz) {
    const int index = playerIndex();
    if (index < 0) {
        return;
    }
    const SceneObject& player =
        g_app.scene.objects[static_cast<std::size_t>(index)];
    placeObject(index, player.position + Vec3{dx, 0.0f, dz});
}

void resetGame() {
    for (SceneObject& object : g_app.scene.objects) {
        object.position = object.initialPosition;
        object.active = true;
    }
    g_app.selectedObject = -1;
    g_app.score = 0;
    g_app.won = false;
    g_app.needsRedraw = true;
    updateWindowTitle();
    std::cout << "Сцена перезапущена\n";
}

Ray makeMouseRay(int mouseX, int mouseY) {
    const Camera& camera = g_app.scene.camera;
    const Vec3 forward = normalize(camera.target - camera.eye);
    const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vec3 up = normalize(cross(right, forward));
    const float aspect =
        static_cast<float>(g_app.windowWidth) /
        static_cast<float>(std::max(1, g_app.windowHeight));
    const float tangent = std::tan(kFieldOfView * kPi / 360.0f);
    const float normalizedX =
        (2.0f * static_cast<float>(mouseX) / g_app.windowWidth - 1.0f) *
        tangent * aspect;
    const float normalizedY =
        (1.0f - 2.0f * static_cast<float>(mouseY) / g_app.windowHeight) *
        tangent;
    return {camera.eye,
            normalize(forward + right * normalizedX + up * normalizedY)};
}

bool rayHitsSphere(const Ray& ray, const SceneObject& object, float& distance) {
    const Vec3 offset = ray.origin - object.position;
    const float radius = object.size * 0.5f;
    const float b = dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    const float root = std::sqrt(discriminant);
    const float nearDistance = -b - root;
    const float farDistance = -b + root;
    distance = nearDistance >= 0.0f ? nearDistance : farDistance;
    return distance >= 0.0f;
}

bool rayHitsCube(const Ray& ray, const SceneObject& object, float& distance) {
    const float halfSize = object.size * 0.5f;
    const float minimum[3] = {
        object.position.x - halfSize,
        object.position.y - halfSize,
        object.position.z - halfSize};
    const float maximum[3] = {
        object.position.x + halfSize,
        object.position.y + halfSize,
        object.position.z + halfSize};
    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[3] = {
        ray.direction.x, ray.direction.y, ray.direction.z};
    float nearDistance = 0.0f;
    float farDistance = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-7f) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
                return false;
            }
            continue;
        }
        float first = (minimum[axis] - origin[axis]) / direction[axis];
        float second = (maximum[axis] - origin[axis]) / direction[axis];
        if (first > second) {
            std::swap(first, second);
        }
        nearDistance = std::max(nearDistance, first);
        farDistance = std::min(farDistance, second);
        if (nearDistance > farDistance) {
            return false;
        }
    }
    distance = nearDistance;
    return farDistance >= 0.0f;
}

int pickObject(const Ray& ray) {
    int picked = -1;
    float nearest = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < g_app.scene.objects.size(); ++index) {
        const SceneObject& object = g_app.scene.objects[index];
        if (!object.active) {
            continue;
        }
        float distance = 0.0f;
        const bool hit = object.shape == Shape::Sphere
                             ? rayHitsSphere(ray, object, distance)
                             : rayHitsCube(ray, object, distance);
        if (hit && distance < nearest) {
            nearest = distance;
            picked = static_cast<int>(index);
        }
    }
    return picked;
}

bool rayPlaneIntersection(const Ray& ray, float planeY, Vec3& point) {
    if (std::abs(ray.direction.y) < 1e-7f) {
        return false;
    }
    const float distance = (planeY - ray.origin.y) / ray.direction.y;
    if (distance < 0.0f) {
        return false;
    }
    point = ray.origin + ray.direction * distance;
    return true;
}

void beginMouseDrag(HWND window, int mouseX, int mouseY) {
    const Ray ray = makeMouseRay(mouseX, mouseY);
    g_app.selectedObject = pickObject(ray);
    if (g_app.selectedObject < 0) {
        return;
    }
    const SceneObject& object =
        g_app.scene.objects[static_cast<std::size_t>(g_app.selectedObject)];
    Vec3 planePoint;
    if (rayPlaneIntersection(ray, object.position.y, planePoint)) {
        g_app.dragOffset = object.position - planePoint;
    } else {
        g_app.dragOffset = {};
    }
    SetCapture(window);
    g_app.needsRedraw = true;
}

void continueMouseDrag(int mouseX, int mouseY) {
    if (g_app.selectedObject < 0) {
        return;
    }
    const SceneObject& object =
        g_app.scene.objects[static_cast<std::size_t>(g_app.selectedObject)];
    Vec3 planePoint;
    if (rayPlaneIntersection(makeMouseRay(mouseX, mouseY),
                             object.position.y, planePoint)) {
        placeObject(g_app.selectedObject, planePoint + g_app.dragOffset);
    }
}

void endMouseDrag() {
    if (g_app.selectedObject >= 0) {
        g_app.selectedObject = -1;
        g_app.needsRedraw = true;
    }
    if (GetCapture() != nullptr) {
        ReleaseCapture();
    }
}

void printHelp() {
    std::cout
        << "Самостоятельная работа №2: простой игровой движок OpenGL\n"
        << "Цель: управляйте ящиком и соберите все морковки.\n\n"
        << "Управление:\n"
        << "  ЛКМ по модели + движение мыши — выбрать и перетащить\n"
        << "  WASD или стрелки             — двигать ящик игрока\n"
        << "  R                            — начать заново\n"
        << "  T                            — включить/выключить текстуры\n"
        << "  H                            — включить/выключить тени\n"
        << "  G                            — включить/выключить сглаживание\n"
        << "  F1                           — повторить эту подсказку\n"
        << "  Esc                          — выход\n";
}

void printUsage() {
    std::cout
        << "Использование:\n"
        << "  mini-game-engine.exe\n"
        << "  mini-game-engine.exe scene-example.txt\n"
        << "  mini-game-engine.exe --help\n";
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                 WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            g_app.running = false;
            PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            g_app.running = false;
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint = {};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            g_app.needsRedraw = true;
            return 0;
        }
        case WM_SIZE:
            g_app.windowWidth = std::max(1, static_cast<int>(LOWORD(lParam)));
            g_app.windowHeight = std::max(1, static_cast<int>(HIWORD(lParam)));
            g_app.needsRedraw = true;
            return 0;
        case WM_LBUTTONDOWN:
            beginMouseDrag(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if ((wParam & MK_LBUTTON) != 0) {
                continueMouseDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            endMouseDrag();
            return 0;
        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:
                    g_app.running = false;
                    PostQuitMessage(0);
                    break;
                case 'W':
                case VK_UP:
                    movePlayer(0.0f, -0.28f);
                    break;
                case 'S':
                case VK_DOWN:
                    movePlayer(0.0f, 0.28f);
                    break;
                case 'A':
                    movePlayer(-0.28f, 0.0f);
                    break;
                case 'D':
                    movePlayer(0.28f, 0.0f);
                    break;
                case VK_LEFT:
                    movePlayer(-0.28f, 0.0f);
                    break;
                case VK_RIGHT:
                    movePlayer(0.28f, 0.0f);
                    break;
                case 'R':
                    resetGame();
                    break;
                case 'T':
                    g_app.texturesEnabled = !g_app.texturesEnabled;
                    std::cout << "Текстуры: "
                              << (g_app.texturesEnabled ? "вкл" : "выкл")
                              << "\n";
                    g_app.needsRedraw = true;
                    break;
                case 'H':
                    g_app.shadowsEnabled = !g_app.shadowsEnabled;
                    std::cout << "Тени: "
                              << (g_app.shadowsEnabled ? "вкл" : "выкл")
                              << "\n";
                    g_app.needsRedraw = true;
                    break;
                case 'G':
                    g_app.antialiasEnabled = !g_app.antialiasEnabled;
                    std::cout << "Сглаживание: "
                              << (g_app.antialiasEnabled ? "вкл" : "выкл")
                              << "\n";
                    g_app.needsRedraw = true;
                    break;
                case VK_F1:
                    printHelp();
                    break;
                default:
                    break;
            }
            return 0;
        default:
            return DefWindowProcA(window, message, wParam, lParam);
    }
}

bool createOpenGlWindow(HINSTANCE instance) {
    WNDCLASSA windowClass = {};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = "MiniGameEngineWindow";
    if (!RegisterClassA(&windowClass)) {
        std::cerr << "Не удалось зарегистрировать класс окна\n";
        return false;
    }

    RECT rectangle{0, 0, kDefaultWidth, kDefaultHeight};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    g_app.window = CreateWindowA(
        windowClass.lpszClassName, "OpenGL Carrot Collector",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance, nullptr);
    if (!g_app.window) {
        std::cerr << "Не удалось создать окно\n";
        return false;
    }

    g_app.deviceContext = GetDC(g_app.window);
    PIXELFORMATDESCRIPTOR pixelFormat = {};
    pixelFormat.nSize = sizeof(pixelFormat);
    pixelFormat.nVersion = 1;
    pixelFormat.dwFlags =
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixelFormat.iPixelType = PFD_TYPE_RGBA;
    pixelFormat.cColorBits = 32;
    pixelFormat.cDepthBits = 24;
    pixelFormat.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(g_app.deviceContext, &pixelFormat);
    if (format == 0 ||
        !SetPixelFormat(g_app.deviceContext, format, &pixelFormat)) {
        std::cerr << "Не удалось выбрать формат пикселей OpenGL\n";
        return false;
    }
    g_app.glContext = wglCreateContext(g_app.deviceContext);
    if (!g_app.glContext ||
        !wglMakeCurrent(g_app.deviceContext, g_app.glContext)) {
        std::cerr << "Не удалось создать контекст OpenGL\n";
        return false;
    }
    return true;
}

bool initializeOpenGl(std::string& error) {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glShadeModel(GL_SMOOTH);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    g_app.quadric = gluNewQuadric();
    if (!g_app.quadric) {
        error = "Не удалось создать объект GLUquadric";
        return false;
    }
    gluQuadricNormals(g_app.quadric, GLU_SMOOTH);

    if (!loadPpmTexture(g_app.scene.backgroundTexturePath,
                        g_app.scene.backgroundTextureId, error)) {
        return false;
    }
    for (SceneObject& object : g_app.scene.objects) {
        if (object.appearance == Appearance::Texture &&
            !loadPpmTexture(object.appearanceValue, object.textureId, error)) {
            return false;
        }
    }
    return true;
}

void destroyOpenGlWindow() {
    if (g_app.glContext) {
        wglMakeCurrent(g_app.deviceContext, g_app.glContext);
        for (SceneObject& object : g_app.scene.objects) {
            if (object.textureId != 0) {
                glDeleteTextures(1, &object.textureId);
                object.textureId = 0;
            }
        }
        if (g_app.scene.backgroundTextureId != 0) {
            glDeleteTextures(1, &g_app.scene.backgroundTextureId);
            g_app.scene.backgroundTextureId = 0;
        }
        if (g_app.quadric) {
            gluDeleteQuadric(g_app.quadric);
            g_app.quadric = nullptr;
        }
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

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string requestedScene;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            printHelp();
            return 0;
        }
        if (!requestedScene.empty() || (!argument.empty() && argument[0] == '-')) {
            std::cerr << "Неизвестный или лишний аргумент: " << argument << "\n";
            printUsage();
            return 1;
        }
        requestedScene = argument;
    }

    std::string loadedPath;
    std::string error;
    if (!loadRequestedScene(requestedScene, loadedPath, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    for (const SceneObject& object : g_app.scene.objects) {
        g_app.collectibleCount += object.role == Role::Collectible ? 1 : 0;
    }
    std::cout << "Загружена сцена: " << loadedPath << "\n";

    if (!createOpenGlWindow(GetModuleHandleA(nullptr))) {
        destroyOpenGlWindow();
        return 1;
    }
    if (!initializeOpenGl(error)) {
        std::cerr << error << "\n";
        destroyOpenGlWindow();
        return 1;
    }

    printHelp();
    updateWindowTitle();
    MSG message = {};
    while (g_app.running) {
        if (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                g_app.running = false;
            } else {
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
        } else if (g_app.needsRedraw) {
            renderFrame();
            SwapBuffers(g_app.deviceContext);
            g_app.needsRedraw = false;
        } else {
            WaitMessage();
        }
    }

    destroyOpenGlWindow();
    return 0;
}
