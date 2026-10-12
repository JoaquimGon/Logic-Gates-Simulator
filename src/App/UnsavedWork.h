#pragma once

#include "UI/UI.h"

#include <functional>

/** Protects only destructive Open, tab-close and exit operations. */
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

    void request(Request request, CircuitViews& views, UI& ui, Input& input);
    /** Returns an approved request; a cancelled/failed Save aborts it. */
    std::optional<Request>
    resolve(CircuitViews& views, UI& ui, Input& input, const std::function<bool()>& saveActive);

    bool pending() const { return m_request.has_value(); }

  private:
    std::optional<Request> m_request;
    std::vector<std::size_t> m_dirty;
};
