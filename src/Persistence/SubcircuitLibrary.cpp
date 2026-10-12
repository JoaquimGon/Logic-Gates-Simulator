#include "Persistence/SubcircuitLibrary.h"

#include "Editor/Subcircuits.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace
{
std::string fileName(const std::string& id)
{
    // Encoding makes imported IDs safe filenames, independent of names and path separators.
    if (id.empty() || id.size() > 100)
        throw std::invalid_argument("Subcircuit ID is too long for library storage.");
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : id)
    {
        result += hex[c >> 4];
        result += hex[c & 15];
    }
    return result + ".json";
}

std::string bytes(const std::filesystem::path& path)
{
    const auto size = std::filesystem::file_size(path);
    if (size > 16 * 1024 * 1024)
        throw std::runtime_error("Library file is too large.");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read library file.");
    std::string result{std::istreambuf_iterator<char>(input), {}};
    if (input.bad() || result.size() != size)
        throw std::runtime_error("Library file could not be read completely.");
    return result;
}
} // namespace

std::filesystem::path SubcircuitLibrary::defaultProfile()
{
#ifdef _WIN32
    wchar_t path[32768]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", path, 32768);
    if (length > 0 && length < 32768)
        return std::filesystem::path(path) / "LogicGateSimulator";
#else
    if (const auto* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local/share/LogicGateSimulator";
#endif
    throw std::runtime_error("Cannot locate the user's application data folder.");
}

SubcircuitLibrary::SubcircuitLibrary(std::filesystem::path profile)
    : m_profile(std::filesystem::absolute(profile)), m_location(m_profile / "storage")
{
    try
    {
        const auto settings = m_profile / "settings.json";
        if (std::filesystem::exists(settings))
        {
            const auto path =
                nlohmann::json::parse(bytes(settings)).at("storage_location").get<std::string>();
            m_location = std::filesystem::path(std::u8string(path.begin(), path.end()));
            if (!m_location.is_absolute())
                throw std::runtime_error("Library location must be an absolute path.");
        }
    }
    catch (const std::exception& error)
    {
        // Do not fall back to an empty library and subsequently overwrite a broken setting.
        throw std::runtime_error(std::string("Cannot read library location: ") + error.what());
    }
    reload();
}

void SubcircuitLibrary::reload()
{
    m_entries.clear();
    m_errors.clear();
    for (bool draft : {false, true})
    {
        const auto directory = m_location / (draft ? "drafts" : "library");
        std::filesystem::create_directories(directory);
        for (const auto& file : std::filesystem::directory_iterator(directory))
        {
            if (!file.is_regular_file() || file.path().extension() != ".json")
                continue;
            try
            {
                auto loaded = loadSubcircuit(file.path());
                if (file.path().filename() != fileName(loaded.identity.id))
                    throw std::runtime_error("Library filename does not match the design ID.");
                if (!draft)
                    makeSubcircuit(loaded.design.scene, loaded.identity, loaded.design.name);
                // An older draft left after an interrupted cleanup is already superseded.
                if (draft && version(loaded.identity.id) >= loaded.identity.version)
                    continue;
                m_entries.push_back({loaded.identity, loaded.design.name, file.path(), draft});
            }
            catch (const std::exception& error)
            {
                m_errors.push_back(file.path().filename().string() + ": " + error.what());
            }
        }
    }
    std::sort(
        m_entries.begin(),
        m_entries.end(),
        [](const Entry& a, const Entry& b)
        { return a.draft != b.draft ? !a.draft : a.name < b.name; }
    );
}

std::uint32_t SubcircuitLibrary::version(const std::string& id) const
{
    std::uint32_t result = 0;
    for (const auto& entry : m_entries)
        if (entry.identity.id == id)
            result = std::max(result, entry.identity.version);
    return result;
}

std::filesystem::path SubcircuitLibrary::store(const SavedSubcircuit& design, bool draft)
{
    if (!draft)
        makeSubcircuit(design.design.scene, design.identity, design.design.name);
    const auto path = m_location / (draft ? "drafts" : "library") / fileName(design.identity.id);
    saveSubcircuit(path, design.design.scene, design.identity, design.design.name);
    if (!draft)
    {
        std::error_code ignored;
        std::filesystem::remove(m_location / "drafts" / fileName(design.identity.id), ignored);
    }
    reload();
    return path;
}

void SubcircuitLibrary::changeLocation(const std::filesystem::path& destination)
{
    const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
    if (target == std::filesystem::weakly_canonical(m_location))
        return;
    // Preflight conflicts before copying. Preserve damaged files too, for manual recovery.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
    for (const char* folder : {"library", "drafts"})
    {
        std::filesystem::create_directories(target / folder);
        for (const auto& file : std::filesystem::directory_iterator(m_location / folder))
        {
            if (!file.is_regular_file() || file.path().extension() != ".json")
                continue;
            const auto to = target / folder / file.path().filename();
            if (std::filesystem::exists(to) && bytes(to) != bytes(file.path()))
                throw std::runtime_error(
                    "Destination already contains a different subcircuit. "
                    "Choose an empty storage folder."
                );
            copies.emplace_back(file.path(), to);
        }
    }
    for (const auto& [from, to] : copies)
        if (!std::filesystem::exists(to))
            writeTextAtomically(to, bytes(from));
    std::filesystem::create_directories(m_profile);
    const auto utf8 = target.u8string();
    writeTextAtomically(
        m_profile / "settings.json",
        nlohmann::json{{"storage_location", std::string(utf8.begin(), utf8.end())}}.dump(2)
    );
    m_location = target;
    reload();
}
