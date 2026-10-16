#pragma once

#include "Persistence/CircuitFile.h"

#include <vector>

/** @brief App-owned editable JSON designs. Published versions and unfinished drafts stay separate. */
class SubcircuitLibrary
{
  public:
    struct Entry
    {
        DefinitionIdentity identity;
        std::string name;
        std::filesystem::path path;
        bool draft = false;
    };

    /** @brief Tests supply a temporary profile; the application uses the user's local app-data folder. */
    explicit SubcircuitLibrary(std::filesystem::path profile = defaultProfile());

    static std::filesystem::path defaultProfile();

    const std::filesystem::path& location() const { return m_location; }
    const std::vector<Entry>& entries() const { return m_entries; }
    const std::vector<std::string>& errors() const { return m_errors; }

    void reload();

    std::uint32_t version(const std::string& id) const;

    /** @brief Write a versioned design and remove a superseded draft only after its replacement is stored. */
    std::filesystem::path store(const SavedSubcircuit& design, bool draft);

    /** @brief Copies managed files before changing the remembered location; never deletes the source. */
    void changeLocation(const std::filesystem::path& destination);

  private:
    std::filesystem::path m_profile, m_location;
    std::vector<Entry> m_entries;
    std::vector<std::string> m_errors;
};
