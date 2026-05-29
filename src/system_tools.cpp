#include "system_tools.h"
#include "agent_interfaces.h"
#include "command_runner.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;
using a0::CommandRunner;

namespace a0 {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r'))
        --end;
    return s.substr(start, end - start);
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool dirExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string getFileSize(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return "?";
    if (st.st_size < 1024) return std::to_string(st.st_size) + " B";
    if (st.st_size < 1024 * 1024) return std::to_string(st.st_size / 1024) + " KB";
    return std::to_string(st.st_size / (1024 * 1024)) + " MB";
}

static bool isBinaryFile(const std::string& path) {
    // Check extension-based heuristics for common binary types
    std::string ext;
    size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    static const std::vector<std::string> binaryExts = {
        ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp",
        ".pdf", ".doc", ".docx", ".xls", ".xlsx",
        ".zip", ".tar", ".gz", ".bz2", ".7z",
        ".exe", ".dll", ".so", ".dylib", ".o", ".a", ".lib",
        ".mp3", ".mp4", ".avi", ".mov", ".wav", ".flac",
        ".ttf", ".otf", ".woff", ".woff2"
    };
    for (const auto& be : binaryExts) {
        if (ext == be) return true;
    }
    return false;
}

// Check if a path component matches the exclusion list
static bool pathIsExcluded(const fs::path& p) {
    static const std::vector<std::string> excluded = {
        "node_modules", ".git", "build", "dist", "vendor",
        "external", "sessions", ".a0", ".opencode", ".cache",
        "thirdparty", "__pycache__", ".venv", "env",
        "coverage_html", ".artifacts"
    };
    for (const auto& part : p) {
        std::string s = part.string();
        for (const auto& ex : excluded) {
            if (s == ex) return true;
        }
    }
    return false;
}

static std::string shellEscape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += '\'';
    return result;
}

// ---------------------------------------------------------------------------
// Glob matching (basic: supports *, ?, **)
// ---------------------------------------------------------------------------

static bool globMatch(const std::string& pattern, const std::string& str) {
    // Convert glob to regex
    std::string regexStr;
    regexStr.reserve(pattern.size() * 2);
    regexStr += '^';
    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '*' && i + 1 < pattern.size() && pattern[i + 1] == '*') {
            // ** - match across directory separators
            regexStr += ".*";
            ++i;
            if (i + 1 < pattern.size() && pattern[i + 1] == '/')
                ++i;
        } else if (c == '*') {
            regexStr += "[^/]*";
        } else if (c == '?') {
            regexStr += "[^/]";
        } else if (c == '.' || c == '+' || c == '(' || c == ')' ||
                   c == '[' || c == ']' || c == '{' || c == '}' ||
                   c == '^' || c == '$' || c == '|' || c == '\\') {
            regexStr += '\\';
            regexStr += c;
        } else {
            regexStr += c;
        }
    }
    regexStr += '$';
    try {
        std::regex re(regexStr, std::regex::ECMAScript);
        return std::regex_match(str, re);
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// Bash
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xBash(const json& params) {
    auto cmdIt = params.find("command");
    if (cmdIt == params.end() || !cmdIt->is_string()) {
        return {"ERROR: missing required string parameter 'command'"};
    }
    std::string command = cmdIt->get<std::string>();

    int timeoutSecs = 30;
    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end() && timeoutIt->is_number()) {
        // Cap at 60s max, convert from ms to s
        timeoutSecs = std::max(1, std::min(60, timeoutIt->get<int>() / 1000));
    }

    std::string workdir;
    auto workdirIt = params.find("workdir");
    if (workdirIt != params.end() && workdirIt->is_string()) {
        workdir = workdirIt->get<std::string>();
    }

    // If workdir is specified, prefix command with cd
    std::string cmdToRun = command;
    if (!workdir.empty()) {
        cmdToRun = "cd " + shellEscape(workdir) + " && " + command;
    }

    auto result = CommandRunner::run(cmdToRun, "", timeoutSecs);

    if (result.timedOut) {
        return {"ERROR: timeout"};
    }
    if (result.exitCode != 0 && result.stdout.empty() && result.stderr.empty()) {
        return {"ERROR: command failed with exit code " + std::to_string(result.exitCode)};
    }

    const size_t maxSize = 1024 * 1024;
    std::string output = result.stdout;
    if (output.size() > maxSize) {
        output.resize(maxSize);
        output += "\n... (truncated)";
    }

    return {output};
}

// ---------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xRead(const json& params) {
    auto pathIt = params.find("filePath");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'filePath'"};
    }
    std::string filePath = pathIt->get<std::string>();

    int offset = 1;
    auto offsetIt = params.find("offset");
    if (offsetIt != params.end() && offsetIt->is_number()) {
        offset = std::max(1, offsetIt->get<int>());
    }

    int limit = 2000;
    auto limitIt = params.find("limit");
    if (limitIt != params.end() && limitIt->is_number()) {
        limit = std::max(1, limitIt->get<int>());
    }

    if (dirExists(filePath)) {
        // Directory listing (capped at 2000 entries)
        std::ostringstream out;
        try {
            int count = 0;
            const int maxDirEntries = 2000;
            for (const auto& entry : fs::directory_iterator(filePath)) {
                if (count >= maxDirEntries) {
                    out << "\n<" << (std::distance(fs::directory_iterator(filePath), fs::directory_iterator{}) - maxDirEntries)
                        << " more entries>";
                    break;
                }
                if (count > 0) out << "\n";
                std::string name = entry.path().filename().string();
                if (entry.is_directory()) name += "/";
                out << name;
                ++count;
            }
        } catch (const std::exception& e) {
            return {"ERROR: " + std::string(e.what())};
        }
        return {out.str()};
    }

    if (!fileExists(filePath)) {
        return {"ERROR: file not found: " + filePath};
    }

    if (isBinaryFile(filePath)) {
        std::ostringstream out;
        out << "File: " << filePath << "\n"
            << "Type: binary\n"
            << "Size: " << getFileSize(filePath);
        return {out.str()};
    }

    // Skip files larger than 10 MB
    std::error_code ec;
    auto fsize = fs::file_size(filePath, ec);
    if (!ec && fsize > 10LL * 1024 * 1024) {
        std::ostringstream out;
        out << "File: " << filePath << "\n"
            << "Type: text (over size limit)\n"
            << "Size: " << getFileSize(filePath) << "\n"
            << "First 500 bytes:\n";

        std::ifstream preview(filePath);
        std::string chunk(500, '\0');
        preview.read(&chunk[0], 500);
        chunk.resize(preview.gcount());
        return {out.str() + chunk};
    }

    std::ifstream file(filePath);
    if (!file) {
        return {"ERROR: could not open file: " + filePath};
    }

    std::ostringstream out;
    std::string line;
    int lineNum = 0;
    int printed = 0;

    while (std::getline(file, line)) {
        ++lineNum;
        if (lineNum < offset) continue;
        if (printed >= limit) break;

        // Truncate long lines
        if (line.size() > 2000)
            line = line.substr(0, 2000);

        if (printed > 0) out << "\n";
        out << lineNum << ": " << line;
        ++printed;
    }

    if (lineNum < offset) {
        return {"ERROR: offset " + std::to_string(offset) + " exceeds file length (" + std::to_string(lineNum) + " lines)"};
    }

    if (lineNum > offset + limit - 1) {
        out << "\n<" << (lineNum - (offset + limit - 1)) << " more lines>";

        // Add hint for small reads
        int remainingLines = lineNum - (offset + limit - 1);
        int nextOffset = offset + limit;
        out << "\n(" << remainingLines << " lines not shown. Call read with offset="
            << nextOffset << " to continue)";
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// Glob
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xGlob(const json& params) {
    auto patternIt = params.find("pattern");
    if (patternIt == params.end() || !patternIt->is_string()) {
        return {"ERROR: missing required string parameter 'pattern'"};
    }
    std::string pattern = patternIt->get<std::string>();

    std::string searchPath = ".";
    auto pathIt = params.find("path");
    if (pathIt != params.end() && pathIt->is_string()) {
        searchPath = pathIt->get<std::string>();
    }

    if (!fs::exists(searchPath)) {
        return {"ERROR: directory not found: " + searchPath};
    }

    // Normalize pattern: strip trailing / for matching (implies directory-only)
    std::string matchPattern = pattern;
    bool dirsOnly = false;
    if (!matchPattern.empty() && matchPattern.back() == '/') {
        matchPattern.pop_back();
        dirsOnly = true;
    }

    // Collect matching files with their modification times
    struct MatchEntry {
        std::string path;
        fs::file_time_type mtime;
    };
    std::vector<MatchEntry> matches;

    try {
        int depth = 0;
        for (const auto& entry : fs::recursive_directory_iterator(searchPath, fs::directory_options::skip_permission_denied)) {
            if (dirsOnly && !entry.is_directory()) continue;
            if (pathIsExcluded(entry.path())) continue;
            std::string relPath = entry.path().lexically_relative(searchPath).string();
            if (std::count(relPath.begin(), relPath.end(), '/') > 32) continue;
            if (relPath.empty()) continue;
            if (globMatch(matchPattern, relPath)) {
                matches.push_back({entry.path().string(), entry.last_write_time()});
            }
            if (matches.size() >= 100) break;
        }
    } catch (const std::exception& e) {
        return {"ERROR: " + std::string(e.what())};
    }

    // Sort by modification time (newest first)
    std::sort(matches.begin(), matches.end(),
        [](const MatchEntry& a, const MatchEntry& b) {
            return a.mtime > b.mtime;
        });

    std::ostringstream out;
    for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0) out << "\n";
        out << matches[i].path;
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// Grep
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xGrep(const json& params) {
    auto patternIt = params.find("pattern");
    if (patternIt == params.end() || !patternIt->is_string()) {
        return {"ERROR: missing required string parameter 'pattern'"};
    }
    std::string pattern = patternIt->get<std::string>();

    std::string searchPath = ".";
    auto pathIt = params.find("path");
    if (pathIt != params.end() && pathIt->is_string()) {
        searchPath = pathIt->get<std::string>();
    }

    std::string includeFilter;
    auto includeIt = params.find("include");
    if (includeIt != params.end() && includeIt->is_string()) {
        includeFilter = includeIt->get<std::string>();
        // Convert simple glob-like include to regex for file matching
    }

    if (!fs::exists(searchPath)) {
        return {"ERROR: directory not found: " + searchPath};
    }

    std::regex re;
    try {
        re = std::regex(pattern, std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
        return {"ERROR: invalid regex pattern: " + std::string(e.what())};
    }

    struct MatchLine {
        std::string filePath;
        int lineNum;
        std::string content;
    };
    std::vector<MatchLine> results;
    const int maxResults = 100;

    const uintmax_t maxFileSize = 10 * 1024 * 1024; // 10 MB

    try {
        for (const auto& entry : fs::recursive_directory_iterator(searchPath, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            if (pathIsExcluded(entry.path())) continue;
            if (results.size() >= static_cast<size_t>(maxResults)) break;

            std::string filePath = entry.path().string();
            // Depth limit: count path separators relative to searchPath
            {
                std::string rel = entry.path().lexically_relative(searchPath).string();
                if (std::count(rel.begin(), rel.end(), '/') > 32) continue;
            }

            // Apply include filter
            if (!includeFilter.empty()) {
                if (!globMatch(includeFilter, entry.path().filename().string())) {
                    continue;
                }
            }

            // Skip binary files
            if (isBinaryFile(filePath)) continue;

            // Skip large files
            std::error_code ec;
            auto fsize = fs::file_size(filePath, ec);
            if (ec || fsize > maxFileSize) continue;

            std::ifstream file(filePath);
            if (!file) continue;

            std::string line;
            int lineNum = 0;
            while (std::getline(file, line)) {
                ++lineNum;
                if (results.size() >= static_cast<size_t>(maxResults)) break;
                if (std::regex_search(line, re)) {
                    // Truncate long lines for display
                    std::string display = line;
                    if (display.size() > 500) {
                        display.resize(500);
                        display += "...";
                    }
                    results.push_back({filePath, lineNum, display});
                }
            }
        }
    } catch (const std::exception& e) {
        return {"ERROR: " + std::string(e.what())};
    }

    if (results.empty()) {
        return {"No matches found for: " + pattern};
    }

    std::ostringstream out;
    std::string currentFile;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].filePath != currentFile) {
            currentFile = results[i].filePath;
            if (i > 0) out << "\n";
            out << currentFile;
        }
        out << "\n  " << results[i].lineNum << ": " << trim(results[i].content);
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// Edit
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xEdit(const json& params) {
    auto pathIt = params.find("filePath");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'filePath'"};
    }
    std::string filePath = pathIt->get<std::string>();

    auto oldIt = params.find("oldString");
    if (oldIt == params.end() || !oldIt->is_string()) {
        return {"ERROR: missing required string parameter 'oldString'"};
    }
    std::string oldString = oldIt->get<std::string>();

    auto newIt = params.find("newString");
    if (newIt == params.end() || !newIt->is_string()) {
        return {"ERROR: missing required string parameter 'newString'"};
    }
    std::string newString = newIt->get<std::string>();

    bool replaceAll = false;
    auto replaceIt = params.find("replaceAll");
    if (replaceIt != params.end() && replaceIt->is_boolean()) {
        replaceAll = replaceIt->get<bool>();
    }

    if (!fileExists(filePath)) {
        return {"ERROR: file not found: " + filePath};
    }

    std::ifstream inFile(filePath);
    if (!inFile) {
        return {"ERROR: could not open file for reading: " + filePath};
    }
    std::string content((std::istreambuf_iterator<char>(inFile)),
                         std::istreambuf_iterator<char>());
    inFile.close();

    if (!replaceAll) {
        size_t pos = content.find(oldString);
        if (pos == std::string::npos) {
            return {"Error: oldString not found in content: " + oldString};
        }
        size_t nextPos = content.find(oldString, pos + oldString.size());
        if (nextPos != std::string::npos) {
            return {"Error: Found multiple matches for oldString. Provide more surrounding "
                    "context or set replaceAll=true."};
        }
        content.replace(pos, oldString.size(), newString);
    } else {
        size_t pos = 0;
        bool found = false;
        while ((pos = content.find(oldString, pos)) != std::string::npos) {
            content.replace(pos, oldString.size(), newString);
            pos += newString.size();
            found = true;
        }
        if (!found) {
            return {"Error: oldString not found in content: " + oldString};
        }
    }

    std::ofstream outFile(filePath);
    if (!outFile) {
        return {"ERROR: could not open file for writing: " + filePath};
    }
    outFile << content;
    outFile.close();

    return {"Edit applied successfully."};
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xWrite(const json& params) {
    auto pathIt = params.find("filePath");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'filePath'"};
    }
    std::string filePath = pathIt->get<std::string>();

    auto contentIt = params.find("content");
    if (contentIt == params.end() || !contentIt->is_string()) {
        return {"ERROR: missing required string parameter 'content'"};
    }
    std::string content = contentIt->get<std::string>();

    // Create parent directories
    size_t slashPos = filePath.rfind('/');
    if (slashPos != std::string::npos) {
        std::string parent = filePath.substr(0, slashPos);
        if (!dirExists(parent)) {
            try {
                fs::create_directories(parent);
            } catch (const std::exception& e) {
                return {"ERROR: could not create parent directory: " + std::string(e.what())};
            }
        }
    }

    std::ofstream outFile(filePath);
    if (!outFile) {
        return {"ERROR: could not open file for writing: " + filePath};
    }
    outFile << content;
    outFile.close();

    return {"Wrote file successfully."};
}

// ---------------------------------------------------------------------------
// SystemToolRegistry
// ---------------------------------------------------------------------------

SystemToolRegistry::SystemToolRegistry() {
    m_handlers["bash"] = xBash;
    m_handlers["read"] = xRead;
    m_handlers["glob"] = xGlob;
    m_handlers["grep"] = xGrep;
    m_handlers["edit"] = xEdit;
    m_handlers["write"] = xWrite;
}

bool SystemToolRegistry::isSystemTool(const std::string& name) {
    return name == "bash" || name == "read" || name == "glob" ||
           name == "grep" || name == "edit" || name == "write";
}

std::vector<std::string> SystemToolRegistry::listTools() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : m_handlers) {
        names.push_back(name);
    }
    return names;
}

SystemToolResult SystemToolRegistry::execute(const std::string& toolName, const json& params) {
    auto it = m_handlers.find(toolName);
    if (it == m_handlers.end()) {
        return {"ERROR: unknown system tool: " + toolName};
    }
    return it->second(params);
}

// ---------------------------------------------------------------------------
// Tool schemas for LLM function calling
// ---------------------------------------------------------------------------

static json xParam(const std::string& type, const std::string& desc,
                    bool required, const json& defaultVal) {
    json p = {{"type", type}, {"description", desc}};
    if (!defaultVal.is_null()) p["default"] = defaultVal;
    return p;
}

std::vector<ToolSchema> SystemToolRegistry::schemas() const {
    std::vector<ToolSchema> result;

    // bash
    result.push_back({
        "bash",
        "Executes a given bash command in a persistent shell session with optional timeout, ensuring proper handling and security measures.",
        {{"type", "object"},
         {"properties", {
             {"command", xParam("string", "The command to execute", true, {})},
             {"description", xParam("string", "Clear, concise description of what this command does in 5-10 words", true, {})},
             {"timeout", xParam("number", "Optional timeout in milliseconds", false, 120000)},
             {"workdir", xParam("string", "The working directory to run the command in. Use this instead of 'cd' commands.", false, {})}
         }},
         {"required", {"command", "description"}}}
    });

    // read
    result.push_back({
        "read",
        "Read a file or directory from the local filesystem. If the path does not exist, an error is returned.",
        {{"type", "object"},
         {"properties", {
             {"filePath", xParam("string", "The absolute path to the file or directory to read", true, {})},
             {"offset", xParam("number", "The line number to start reading from (1-indexed)", false, 1)},
             {"limit", xParam("number", "The maximum number of lines to read", false, 2000)}
         }},
         {"required", {"filePath"}}}
    });

    // glob
    result.push_back({
        "glob",
        "Fast file pattern matching tool that works with any codebase size. Supports glob patterns like \"**/*.js\" or \"src/**/*.ts\".",
        {{"type", "object"},
         {"properties", {
             {"pattern", xParam("string", "The glob pattern to match files against", true, {})},
             {"path", xParam("string", "The directory to search in. Defaults to the current directory.", false, {})}
         }},
         {"required", {"pattern"}}}
    });

    // grep
    result.push_back({
        "grep",
        "Fast content search tool that works with any codebase size. Searches file contents using regular expressions.",
        {{"type", "object"},
         {"properties", {
             {"pattern", xParam("string", "The regex pattern to search for in file contents", true, {})},
             {"path", xParam("string", "The directory to search in. Defaults to the current directory.", false, {})},
             {"include", xParam("string", "File pattern to include (e.g. \"*.js\", \"*.{ts,tsx}\")", false, {})}
         }},
         {"required", {"pattern"}}}
    });

    // edit
    result.push_back({
        "edit",
        "Performs exact string replacements in files.",
        {{"type", "object"},
         {"properties", {
             {"filePath", xParam("string", "The absolute path to the file to modify", true, {})},
             {"oldString", xParam("string", "The text to replace", true, {})},
             {"newString", xParam("string", "The text to replace it with (must be different from oldString)", true, {})},
             {"replaceAll", xParam("boolean", "Replace all occurrences of oldString", false, false)}
         }},
         {"required", {"filePath", "oldString", "newString"}}}
    });

    // write
    result.push_back({
        "write",
        "Writes a file to the local filesystem.",
        {{"type", "object"},
         {"properties", {
             {"filePath", xParam("string", "The absolute path to the file to write", true, {})},
             {"content", xParam("string", "The content to write to the file", true, {})}
         }},
         {"required", {"filePath", "content"}}}
    });

    return result;
}

} // namespace a0
