#pragma once

#include <string>
#include <vector>
#include <optional>
#include "nlohmann/json.hpp"

namespace a0::personas {

enum class PersonaNamespace {
    SYSTEM,
    LOCAL,
    GITHUB
};

struct PersonaManifest {
    std::string name;
    std::string description;
    std::string promptFile;
    std::vector<std::string> skills;
    std::vector<std::string> tools;
    PersonaNamespace ns;
    std::string dir;
};

struct Persona {
    PersonaManifest manifest;
    std::string prompt;
};

class PersonaLoader {
public:
    explicit PersonaLoader(const std::string& root = "./personas");

    int loadAll();
    std::optional<Persona> getPersona(const std::string& name) const;
    std::vector<Persona> listPersonas() const;

private:
    std::string m_root;
    std::vector<Persona> m_personas;

    int xLoadNamespace(const std::string& dirPath, PersonaNamespace ns);
    std::optional<PersonaManifest> xParseManifest(const std::string& dir) const;
    std::string xReadPrompt(const std::string& dir, const std::string& promptFile) const;
};

} // namespace a0::personas
