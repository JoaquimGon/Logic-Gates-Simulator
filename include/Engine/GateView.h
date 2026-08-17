#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>

class GateView
{
public:
    GateView(glm::vec2 position, glm::vec2 size, std::string shaderName)
        : m_position(position), m_size(size), m_shaderName(std::move(shaderName)) {
    }

    glm::vec2 getPosition() const { return m_position; }
    glm::vec2 getSize()     const { return m_size; }
    const std::string& getShaderName() const { return m_shaderName; }

    void setPosition(glm::vec2 pos) { m_position = pos; }

private:
    glm::vec2   m_position; // world-space center, same units as uGridSpacing
    glm::vec2   m_size;     // world-space width/height
    std::string m_shaderName;
};