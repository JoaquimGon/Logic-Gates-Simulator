#pragma once

#include "UI/UI.h"

#include <functional>

/** @brief Protects only destructive Open, tab-close and exit operations. */
class UnsavedWork
{
  public:
    enum class Operation
    {
        Open,
        Close,
        Exit
    };

    struct Request
    {
        Operation operation;
        std::size_t index;
    };

    /** @brief Queue an operation and identify the dirty workspace that must be resolved before it can run. */
    void request(Request request, CircuitViews& views, UI& ui, Input& input);

    /** @brief Returns an approved request; a cancelled/failed Save aborts it. */
    std::optional<Request> resolve(CircuitViews& views, UI& ui, Input& input, const std::function<bool()>& saveActive);

    bool pending() const { return m_request.has_value(); }

  private:
    std::optional<Request> m_request;
    std::vector<std::size_t> m_dirty;
};
