#include "App/UnsavedWork.h"

#include "Editor/Input.h"

#include <exception>
#include <stdexcept>
#include <utility>


void UnsavedWork::request(Request request, CircuitViews& views, UI& ui, Input& input)
{
    if (pending() || request.index >= views.size() || (request.operation == Operation::Close && request.index == 0))
        return;
    m_request = request;
    m_dirty.clear();
    std::vector<std::string> names;
    for (std::size_t i = 0; i < views.size(); ++i)
        if ((request.operation == Operation::Exit || i == request.index) && views.hasUnsavedChanges(i))
        {
            m_dirty.push_back(i);
            names.push_back(views.name(i));
        }
    if (!names.empty())
        ui.showUnsavedPrompt(std::move(names), request.operation == Operation::Exit, input);
}


std::optional<UnsavedWork::Request>
UnsavedWork::resolve(CircuitViews& views, UI& ui, Input& input, const std::function<bool()>& saveActive)
{
    if (!m_request)
        return {};
    const auto choice = m_dirty.empty() ? std::optional{UI::UnsavedChoice::Discard} : ui.takeUnsavedChoice();
    if (!choice)
        return {};
    auto approved = std::exchange(m_request, std::nullopt);
    const auto original = views.activeIndex();
    std::string failure;
    if (*choice == UI::UnsavedChoice::Cancel)
        approved.reset();
    else if (*choice == UI::UnsavedChoice::Save)
    {
        try
        {
            const auto operation = approved->operation;
            const auto target = approved->index;
            // A child Save can publish changes into a previously saved parent/workspace.
            // Revisit only dirty designs until the finite open dependency chain is saved.
            for (std::size_t pass = 0; pass <= views.size(); ++pass)
            {
                bool remaining = false;
                for (std::size_t index = 0; index < views.size(); ++index)
                {
                    if (operation != Operation::Exit && index != target)
                        continue;
                    if (!views.hasUnsavedChanges(index))
                        continue;
                    views.select(index, input);
                    if (!saveActive() || views.hasUnsavedChanges(index))
                    {
                        approved.reset();
                        break;
                    }
                }
                if (!approved)
                    break;
                for (std::size_t index = 0; index < views.size(); ++index)
                    if ((operation == Operation::Exit || index == target) && views.hasUnsavedChanges(index))
                        remaining = true;
                if (!remaining)
                    break;
                if (pass == views.size())
                    throw std::runtime_error("Save All left pending changes; the app will stay open.");
            }
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            approved.reset();
        }
    }
    views.select(original, input);
    if (!failure.empty())
        ui.setFileStatus(failure, true);
    input.setUiCapture({});
    input.setCanvasFocused(true);
    return approved;
}
