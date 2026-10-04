#pragma once

#include "Editor/UiInput.h"
#include "Geometry/CanvasViewport.h"

#include <cstddef>
#include <optional>
#include <string>

class CircuitViews;
class Input;
class Renderer;
struct FontMetrics;

/** A tab strip and one small rename popup, drawn with existing rectangles and text. */
class CircuitTabs
{
  public:
    void bind(CircuitViews* views, const FontMetrics* font);

    bool enabled() const { return m_views && m_font; }

    void layout(CanvasViewport bar);
    void update(double now, Input& input);
    bool handleInput(const UiInputEvent& event, Input& input);
    void cancel(Input& input);
    void draw(Renderer& renderer) const;

    CanvasViewport tabBounds(std::size_t index) const;
    CanvasViewport addBounds() const;
    CanvasViewport popupBounds() const;
    CanvasViewport nameBounds() const;
    bool contains(double x, double y) const;
    std::string tabName(std::size_t index) const;

    std::optional<std::size_t> popupIndex() const { return m_popup; }

  private:
    std::optional<std::size_t> tabAt(double x, double y) const;
    void open(std::size_t index);
    void stopEditing(Input& input);
    double maxScroll() const;

    CircuitViews* m_views = nullptr;
    const FontMetrics* m_font = nullptr;
    CanvasViewport m_bar{}, m_tabList{};
    double m_tabWidth = 0, m_scroll = 0, m_now = 0, m_hoverSince = 0;
    double m_pointerX = 0, m_pointerY = 0;
    std::optional<std::size_t> m_hover, m_popup, m_lastActive;
    bool m_editing = false, m_selectAll = false, m_escapePressed = false;
    std::string m_draft;
};
