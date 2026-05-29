#include "schema_inference_engine.h"
#include "trace.h"
#include <sstream>
#include <stdexcept>

static const std::string TOOL_INFERENCE_PROMPT =
    "You are a tool generator. Given a natural language description, "
    "output a JSON object with these exact fields:\n"
    "{\n"
    "  \"name\": \"short_tool_name\",\n"
    "  \"description\": \"what the tool does\",\n"
    "  \"command\": \"shell command to run\",\n"
    "  \"inputMode\": \"stdin\"\n"
    "}\n"
    "Output ONLY the JSON object, no other text.";

static const std::string SKILL_INFERENCE_PROMPT =
    "You are a skill generator. Given a natural language description, "
    "output a JSON object with these exact fields:\n"
    "{\n"
    "  \"name\": \"short_skill_name\",\n"
    "  \"description\": \"what the skill does\",\n"
    "  \"prompt\": \"prompt template with {{tool:name args}} placeholders\",\n"
    "  \"dependencies\": [\"tool_name1\", \"tool_name2\"],\n"
    "  \"validators\": []\n"
    "}\n"
    "Output ONLY the JSON object, no other text.";

static void fillDefaults(json& j, const std::string& nameField) {
    if (!j.contains(nameField) || !j[nameField].is_string() || j[nameField].get<std::string>().empty()) {
        j[nameField] = "inferred";
    }
}

DefaultSchemaInferenceEngine::DefaultSchemaInferenceEngine(InferenceProvider* provider)
    : m_provider(provider) {}

Tool DefaultSchemaInferenceEngine::inferTool(const std::string& naturalLanguageDescription) {
    TRACE_LOG("inferTool(" << naturalLanguageDescription << ")");
    std::string response = m_provider->complete(TOOL_INFERENCE_PROMPT, naturalLanguageDescription);
    if (response.empty()) {
        throw std::runtime_error("empty response from inference provider");
    }

    json j;
    try {
        j = json::parse(response);
    } catch (...) {
        // retry once
        response = m_provider->complete(TOOL_INFERENCE_PROMPT, naturalLanguageDescription);
        j = json::parse(response);
    }

    fillDefaults(j, "name");
    fillDefaults(j, "command");

    Tool t;
    t.name = j.value("name", "inferred");
    t.description = j.value("description", "");
    t.command = j.value("command", "");
    t.inputMode = j.value("inputMode", "stdin");
    return t;
}

Prompt DefaultSchemaInferenceEngine::inferPrompt(const std::string& naturalLanguageDescription) {
    TRACE_LOG("inferPrompt(" << naturalLanguageDescription << ")");
    std::string response = m_provider->complete(SKILL_INFERENCE_PROMPT, naturalLanguageDescription);
    if (response.empty()) {
        throw std::runtime_error("empty response from inference provider");
    }

    json j;
    try {
        j = json::parse(response);
    } catch (...) {
        response = m_provider->complete(SKILL_INFERENCE_PROMPT, naturalLanguageDescription);
        j = json::parse(response);
    }

    fillDefaults(j, "name");
    fillDefaults(j, "prompt");

    Prompt p;
    p.name = j.value("name", "inferred");
    p.description = j.value("description", "");
    p.prompt = j.value("prompt", "");
    for (const auto& dep : j["dependencies"]) {
        p.dependencies.push_back(dep.get<std::string>());
    }
    for (const auto& v : j["validators"]) {
        ValidatorBinding vb;
        vb.toolName = v.value("toolName", "");
        p.validators.push_back(std::move(vb));
    }
    return p;
}
