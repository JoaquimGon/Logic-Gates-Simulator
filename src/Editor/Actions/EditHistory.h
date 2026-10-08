#pragma once

#include "Editor/Actions/EditTypes.h"

#include <deque>

/** Bounded editor history; records reuse the action service's complete scene snapshots. */
class EditHistory
{
  public:
    static constexpr std::size_t Limit = 100;
    void record(const EditResult& result);
    EditResult undo(Scene& scene);
    EditResult redo(Scene& scene);
    void clear();

    std::size_t undoCount() const { return m_undo.size(); }

    std::size_t redoCount() const { return m_redo.size(); }

    /** External definition updates invalidate edits which depended on their old behaviour. */
    bool referencesDefinition(const std::string& id) const;

  private:
    std::deque<std::shared_ptr<const EditRecord>> m_undo, m_redo;
    static EditResult restore(Scene& scene, const Scene& snapshot);
};
