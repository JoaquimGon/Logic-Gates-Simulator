#include "Editor/CircuitViews.h"

#include "Editor/Input.h"

#include <utility>

CircuitViews::CircuitViews()
{
    m_views.push_back(View{"Main"});
}

void CircuitViews::create(Input& input)
{
    m_views.push_back(View{"unnamed circuit"});
    select(m_views.size() - 1, input);
}

bool CircuitViews::select(std::size_t index, Input& input)
{
    if (index >= m_views.size())
        return false;
    if (index != m_active)
    {
        m_views[m_active].pan = input.getPanOffset();
        m_views[m_active].zoom = input.getZoom();
        m_active = index;
        input.setScene(&activeScene());
        input.setPanOffset(m_views[m_active].pan);
        input.setZoom(m_views[m_active].zoom);
    }
    else
        input.setScene(&activeScene());
    input.setCanvasFocused(true);
    return true;
}

void CircuitViews::rename(std::size_t index, std::string name)
{
    const auto first = name.find_first_not_of(' ');
    if (first == std::string::npos)
        name = index == 0 ? "Main" : "unnamed circuit";
    else
        name = name.substr(first, name.find_last_not_of(' ') - first + 1);
    m_views.at(index).name = std::move(name);
}
