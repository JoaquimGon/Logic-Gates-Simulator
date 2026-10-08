#include "Editor/Scene.h"
#include "Graphics/Text/TextGeometry.h"
#include "UI/UI.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string_view>
#include <utility>

namespace
{
constexpr std::size_t timestampPrefixLength = sizeof("[00:00:00] ") - 1;
}

CircuitViews::Messages& UI::messageState()
{
    return m_views ? m_views->activeMessages() : m_messages;
}

const CircuitViews::Messages& UI::messageState() const
{
    return m_views ? m_views->activeMessages() : m_messages;
}

const std::vector<std::string>& UI::messages() const
{
    return messageState().entries;
}

bool UI::hasUnreadMessages() const
{
    return messageState().unread;
}

void UI::addMessage(std::string message, MessageKind kind)
{
    if (message.empty() || kind == MessageKind::Info)
    {
        clearMessages();
        return;
    }
    const char* prefix = kind == MessageKind::Error     ? "Error: "
                         : kind == MessageKind::Warning ? "Warning: "
                                                        : "Info: ";
    message.insert(0, prefix);
    auto& log = messageState();
    if (!log.entries.empty() &&
        log.entries.back().compare(timestampPrefixLength, std::string::npos, message) == 0)
        return;
    log.entries.clear();
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char timestamp[12]{};
    std::strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", &local);
    log.entries.push_back(std::string(timestamp) + message);
    log.unread = m_bottomTab != BottomTab::Messages ||
                 m_overviewView != m_circuitTabs.activeViewIndex() ||
                 m_showSubcircuit != m_circuitTabs.activeIsSubcircuit();
    log.scroll = 0;
}

void UI::clearMessages()
{
    auto& log = messageState();
    log.entries.clear();
    log.scroll = 0;
    if (log.issues.empty())
        log.unread = false;
    if (log.fileError)
    {
        log.fileError = false;
        log.fileStatus.clear();
    }
}

CanvasViewport UI::messageBoxBounds() const
{
    return {
        m_bottom.x + 8,
        m_bottom.y + 60,
        std::max(0.0, m_bottom.width - 16),
        std::max(0.0, m_bottom.height - 68)
    };
}

const std::vector<CircuitViews::SimulationIssue>& UI::simulationIssues() const
{
    return messageState().issues;
}

std::vector<UI::MessageRow> UI::messageRows(const FontMetrics* font) const
{
    std::vector<MessageRow> rows;
    auto append = [&](const std::string& message,
                      MessageKind kind,
                      std::optional<std::size_t> issue = std::nullopt)
    {
        const bool find = issue && simulationIssues()[*issue].location;
        const float width =
            static_cast<float>(std::max(1.0, messageBoxBounds().width - 20 - (find ? 60 : 0)));
        std::size_t start = 0;
        while (start < message.size())
        {
            std::size_t end = start, space = std::string::npos;
            float used = 0;
            while (end < message.size() && message[end] != '\n')
            {
                const float advance =
                    font ? getTextWidth(std::string(1, message[end]), 0.4f, *font) : 7.0f;
                if (end > start && used + advance > width)
                    break;
                used += advance;
                if (message[end] == ' ')
                    space = end;
                ++end;
            }
            if (end < message.size() && message[end] != '\n' && space != std::string::npos &&
                space > start)
                end = space;
            rows.push_back({message.substr(start, end - start), kind, std::nullopt});
            start = end;
            if (start < message.size() && message[start] == '\n')
                ++start;
            while (start < message.size() && message[start] == ' ')
                ++start;
        }
        if (issue && !rows.empty())
            rows.back().issue = issue;
    };
    if (simulationIssues().empty())
        append("Circuit is fine.", MessageKind::Info);
    for (std::size_t i = 0; i < simulationIssues().size(); ++i)
    {
        const auto& issue = simulationIssues()[i];
        append(issue.message, issue.warning ? MessageKind::Warning : MessageKind::Error, i);
    }
    if (!messages().empty())
        append("Latest action:", MessageKind::Info);
    for (const auto& message : messages())
    {
        const auto content = std::string_view(message).substr(timestampPrefixLength);
        append(
            message, content.starts_with("Warning:") ? MessageKind::Warning : MessageKind::Error
        );
    }
    return rows;
}

CanvasViewport UI::findMessageBounds(std::size_t issue) const
{
    if (issue >= simulationIssues().size() || !simulationIssues()[issue].location)
        return {};
    const auto box = messageBoxBounds();
    const auto lines = messageRows(m_font);
    const int rows = static_cast<int>(box.height / 20);
    const int offset =
        std::clamp(messageState().scroll, 0, std::max(0, static_cast<int>(lines.size()) - rows));
    for (int row = 0; row < rows && row + offset < static_cast<int>(lines.size()); ++row)
        if (lines[row + offset].issue == issue)
        {
            const auto& line = lines[row + offset].text;
            const double textWidth = m_font ? getTextWidth(line, 0.4f, *m_font) : line.size() * 7.0;
            const double gap = m_font ? getTextWidth(" ", 0.4f, *m_font) : 7.0;
            const double linkWidth = m_font ? getTextWidth("FIND", 0.35f, *m_font) : 24.5;
            return {box.x + 10 + textWidth + gap, box.y + row * 20, linkWidth, 20};
        }
    return {};
}

namespace
{
std::optional<GridCoords> pinLocation(const Scene& scene, PinRef pin, PinType direction)
{
    const auto* view = scene.getCommittedComponentView(pin.componentId);
    if (!view)
        return std::nullopt;
    const auto& pins = direction == PinType::INPUT ? view->getInputPins() : view->getOutputPins();
    for (const auto& candidate : pins)
        if (pin.pinIndex >= 0 && candidate.pin_index == static_cast<std::uint32_t>(pin.pinIndex))
            return view->getAbsolutePinGridPos(candidate);
    return view->getGridPosition();
}

std::string positionText(std::optional<GridCoords> point)
{
    return point ? " at (" + std::to_string(point->x) + ", " + std::to_string(point->y) + ")" : "";
}
} // namespace

void UI::reportSimulation(const Scene& scene, double now)
{
    auto& log = messageState();
    log.simulationResult = scene.getLastEvalResult();
    log.shortedNets = scene.getShortedNetCount();
    // Brief catch-up is normal. Report only an accumulated delay lasting at least one second.
    const bool pending =
        log.simulationResult == SimulationResult::OK && scene.pendingClockTime() > 0.1;
    if (pending && log.backlogSince < 0)
        log.backlogSince = now;
    if (pending && now - log.backlogSince >= 1)
        log.behind = true;
    if (!pending)
    {
        log.backlogSince = -1;
        log.behind = false;
    }
    std::vector<CircuitViews::SimulationIssue> issues;
    if (log.simulationResult == SimulationResult::NON_CONVERGENT)
    {
        std::optional<GridCoords> point;
        if (const auto* view = scene.getCommittedComponentView(scene.unsettledComponent()))
            point = view->getGridPosition();
        issues.push_back(
            {"Simulation did not settle. Pending activity" + positionText(point) + ".", point}
        );
    }
    if (log.simulationResult == SimulationResult::CONNECTION_REJECTED)
    {
        for (const auto& rejected : scene.getRejectedConnections())
        {
            const auto& connection = rejected.connection;
            auto point = pinLocation(
                scene, {connection.destComponentId, connection.destPinIndex}, PinType::INPUT
            );
            if (!point)
                point = pinLocation(
                    scene, {connection.srcComponentId, connection.srcPinIndex}, PinType::OUTPUT
                );
            issues.push_back(
                {"Connection rejected" + positionText(point) + ". Repair the wiring.", point}
            );
        }
        if (scene.getRejectedConnections().empty())
            issues.push_back({"Connection rejected. Repair the wiring.", std::nullopt});
    }
    for (const auto& [id, net] : scene.getNets())
        if (net.shorted())
        {
            auto point = pinLocation(scene, net.getDrivers().back(), PinType::OUTPUT);
            if (!point && !net.getGeometry().empty())
                if (const auto* wire = scene.getWire(net.getGeometry().front());
                    wire && !wire->getPath().empty())
                    point = wire->getPath().front();
            issues.push_back(
                {"Short circuit" + positionText(point) + ": multiple outputs drive this net.",
                 point}
            );
        }
    if (log.behind)
        issues.push_back(
            {"Simulation falling behind real time. Reduce clock frequency.", std::nullopt, true}
        );
    if (issues != log.issues)
    {
        log.unread = !issues.empty() && (m_bottomTab != BottomTab::Messages ||
                                         m_overviewView != m_circuitTabs.activeViewIndex() ||
                                         m_showSubcircuit != m_circuitTabs.activeIsSubcircuit());
        log.issues = std::move(issues);
        log.scroll = 0;
    }
}
