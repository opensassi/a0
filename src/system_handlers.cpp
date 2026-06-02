#include "system_handlers.h"
#include "skills/skills.h"
#include "command_runner.h"
#include "docker_security_filter.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>
#include <filesystem>

namespace fs = std::filesystem;
using a0::CommandRunner;

namespace a0 {

// ---------------------------------------------------------------------------
// Helpers (shared by multiple handlers)
// ---------------------------------------------------------------------------

static std::string xTrim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\r'))
        --end;
    return s.substr(start, end - start);
}

static bool xFileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool xDirExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string xGetFileSize(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return "?";
    if (st.st_size < 1024) return std::to_string(st.st_size) + " B";
    if (st.st_size < 1024 * 1024) return std::to_string(st.st_size / 1024) + " KB";
    return std::to_string(st.st_size / (1024 * 1024)) + " MB";
}

static bool xIsBinaryFile(const std::string& path) {
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

static bool xPathIsExcluded(const fs::path& p) {
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

static std::string xShellEscape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += '\'';
    return result;
}

static bool xGlobMatch(const std::string& pattern, const std::string& str) {
    std::string regexStr;
    regexStr.reserve(pattern.size() * 2);
    regexStr += '^';
    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '*' && i + 1 < pattern.size() && pattern[i + 1] == '*') {
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

HandlerResult xBash(const json& params) {
    auto cmdIt = params.find("command");
    if (cmdIt == params.end() || !cmdIt->is_string()) {
        return {"ERROR: missing required string parameter 'command'"};
    }
    std::string command = cmdIt->get<std::string>();

    // Reject git commands
    std::string c = xTrim(command);
    auto hasTool = [&](const std::string& tool) -> bool {
        if (c == tool) return true;
        if (c.rfind(tool + " ", 0) == 0) return true;
        if (c.rfind(tool + "\t", 0) == 0) return true;
        size_t pos = c.find(" " + tool + " ");
        if (pos != std::string::npos) return true;
        if (c.size() >= tool.size() + 2 && c.rfind(" " + tool, c.size() - tool.size() - 1) != std::string::npos) return true;
        return false;
    };

    if (hasTool("git")) {
        return {"ERROR: git commands must use the git_* system tools. "
                "Browse skills: show_skills('/system/git'). "
                "Use git_start_session for the standard workflow."};
    }

    if (hasTool("docker") || hasTool("docker-compose")) {
        return {"ERROR: docker and docker-compose commands must use the docker skill prompts. "
                "Browse skills: show_skills('/system/docker'). "
                "Use show_skill_tools('/docker') to list available docker commands."};
    }

    int timeoutSecs = 30;
    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end() && timeoutIt->is_number()) {
        timeoutSecs = std::max(1, std::min(60, timeoutIt->get<int>() / 1000));
    }

    std::string workdir;
    auto workdirIt = params.find("workdir");
    if (workdirIt != params.end() && workdirIt->is_string()) {
        workdir = workdirIt->get<std::string>();
    }

    std::string cmdToRun = command;
    if (!workdir.empty()) {
        cmdToRun = "cd " + xShellEscape(workdir) + " && " + command;
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

HandlerResult xRead(const json& params) {
    auto pathIt = params.find("file_path");
    if (pathIt == params.end() || !pathIt->is_string()) {
        pathIt = params.find("filePath");
    }
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'file_path'"};
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

    if (xDirExists(filePath)) {
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

    if (!xFileExists(filePath)) {
        return {"ERROR: file not found: " + filePath};
    }

    if (xIsBinaryFile(filePath)) {
        std::ostringstream out;
        out << "File: " << filePath << "\n"
            << "Type: binary\n"
            << "Size: " << xGetFileSize(filePath);
        return {out.str()};
    }

    std::error_code ec;
    auto fsize = fs::file_size(filePath, ec);
    if (!ec && fsize > 10LL * 1024 * 1024) {
        std::ostringstream out;
        out << "File: " << filePath << "\n"
            << "Type: text (over size limit)\n"
            << "Size: " << xGetFileSize(filePath) << "\n"
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
        if (line.size() > 2000) line = line.substr(0, 2000);
        if (printed > 0) out << "\n";
        out << lineNum << ": " << line;
        ++printed;
    }

    if (lineNum < offset) {
        return {"ERROR: offset " + std::to_string(offset) + " exceeds file length (" + std::to_string(lineNum) + " lines)"};
    }

    if (lineNum > offset + limit - 1) {
        out << "\n<" << (lineNum - (offset + limit - 1)) << " more lines>";
        int nextOffset = offset + limit;
        out << "\n(" << (lineNum - (offset + limit - 1)) << " lines not shown. Call read with offset="
            << nextOffset << " to continue)";
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// Glob
// ---------------------------------------------------------------------------

HandlerResult xGlob(const json& params) {
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

    std::string matchPattern = pattern;
    bool dirsOnly = false;
    if (!matchPattern.empty() && matchPattern.back() == '/') {
        matchPattern.pop_back();
        dirsOnly = true;
    }

    struct MatchEntry { std::string path; fs::file_time_type mtime; };
    std::vector<MatchEntry> matches;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(searchPath, fs::directory_options::skip_permission_denied)) {
            if (dirsOnly && !entry.is_directory()) continue;
            if (xPathIsExcluded(entry.path())) continue;
            std::string relPath = entry.path().lexically_relative(searchPath).string();
            if (std::count(relPath.begin(), relPath.end(), '/') > 32) continue;
            if (relPath.empty()) continue;
            if (xGlobMatch(matchPattern, relPath)) {
                matches.push_back({entry.path().string(), entry.last_write_time()});
            }
            if (matches.size() >= 100) break;
        }
    } catch (const std::exception& e) {
        return {"ERROR: " + std::string(e.what())};
    }

    std::sort(matches.begin(), matches.end(),
        [](const MatchEntry& a, const MatchEntry& b) { return a.mtime > b.mtime; });

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

HandlerResult xGrep(const json& params) {
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

    struct MatchLine { std::string filePath; int lineNum; std::string content; };
    std::vector<MatchLine> results;
    const int maxResults = 100;
    const uintmax_t maxFileSize = 10 * 1024 * 1024;

    try {
        for (const auto& entry : fs::recursive_directory_iterator(searchPath, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;
            if (xPathIsExcluded(entry.path())) continue;
            if (results.size() >= static_cast<size_t>(maxResults)) break;
            std::string fp = entry.path().string();
            std::string rel = entry.path().lexically_relative(searchPath).string();
            if (std::count(rel.begin(), rel.end(), '/') > 32) continue;
            if (!includeFilter.empty() && !xGlobMatch(includeFilter, entry.path().filename().string())) continue;
            if (xIsBinaryFile(fp)) continue;
            std::error_code ec;
            auto fsize = fs::file_size(fp, ec);
            if (ec || fsize > maxFileSize) continue;
            std::ifstream file(fp);
            if (!file) continue;
            std::string line;
            int lineNum = 0;
            while (std::getline(file, line)) {
                ++lineNum;
                if (results.size() >= static_cast<size_t>(maxResults)) break;
                if (std::regex_search(line, re)) {
                    std::string display = line;
                    if (display.size() > 500) { display.resize(500); display += "..."; }
                    results.push_back({fp, lineNum, display});
                }
            }
        }
    } catch (const std::exception& e) {
        return {"ERROR: " + std::string(e.what())};
    }

    if (results.empty()) return {"No matches found for: " + pattern};

    std::ostringstream out;
    std::string currentFile;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i].filePath != currentFile) {
            currentFile = results[i].filePath;
            if (i > 0) out << "\n";
            out << currentFile;
        }
        out << "\n  " << results[i].lineNum << ": " << xTrim(results[i].content);
    }
    return {out.str()};
}

// ---------------------------------------------------------------------------
// Edit
// ---------------------------------------------------------------------------

HandlerResult xEdit(const json& params) {
    auto pathIt = params.find("file_path");
    if (pathIt == params.end() || !pathIt->is_string())
        pathIt = params.find("filePath");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'file_path'"};
    }
    std::string filePath = pathIt->get<std::string>();

    auto oldIt = params.find("old_string");
    if (oldIt == params.end() || !oldIt->is_string())
        oldIt = params.find("oldString");
    if (oldIt == params.end() || !oldIt->is_string()) {
        return {"ERROR: missing required string parameter 'old_string'"};
    }
    std::string oldString = oldIt->get<std::string>();

    auto newIt = params.find("new_string");
    if (newIt == params.end() || !newIt->is_string())
        newIt = params.find("newString");
    if (newIt == params.end() || !newIt->is_string()) {
        return {"ERROR: missing required string parameter 'new_string'"};
    }
    std::string newString = newIt->get<std::string>();

    bool replaceAll = false;
    auto replaceIt = params.find("replace_all");
    if (replaceIt == params.end() || !replaceIt->is_boolean())
        replaceIt = params.find("replaceAll");
    if (replaceIt != params.end() && replaceIt->is_boolean()) {
        replaceAll = replaceIt->get<bool>();
    }

    if (!xFileExists(filePath)) {
        return {"ERROR: file not found: " + filePath};
    }

    std::ifstream inFile(filePath);
    if (!inFile) return {"ERROR: could not open file for reading: " + filePath};
    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    if (!replaceAll) {
        size_t pos = content.find(oldString);
        if (pos == std::string::npos) return {"Error: oldString not found in content"};
        size_t nextPos = content.find(oldString, pos + oldString.size());
        if (nextPos != std::string::npos)
            return {"Error: Found multiple matches for oldString. Provide more surrounding context or set replaceAll=true."};
        content.replace(pos, oldString.size(), newString);
    } else {
        size_t pos = 0;
        bool found = false;
        while ((pos = content.find(oldString, pos)) != std::string::npos) {
            content.replace(pos, oldString.size(), newString);
            pos += newString.size();
            found = true;
        }
        if (!found) return {"Error: oldString not found in content"};
    }

    std::ofstream outFile(filePath);
    if (!outFile) return {"ERROR: could not open file for writing: " + filePath};
    outFile << content;
    outFile.close();
    return {"Edit applied successfully."};
}

// ---------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------

HandlerResult xWrite(const json& params) {
    auto pathIt = params.find("file_path");
    if (pathIt == params.end() || !pathIt->is_string())
        pathIt = params.find("filePath");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'file_path'"};
    }
    std::string filePath = pathIt->get<std::string>();

    auto contentIt = params.find("content");
    if (contentIt == params.end() || !contentIt->is_string()) {
        return {"ERROR: missing required string parameter 'content'"};
    }
    std::string content = contentIt->get<std::string>();

    size_t slashPos = filePath.rfind('/');
    if (slashPos != std::string::npos) {
        std::string parent = filePath.substr(0, slashPos);
        if (!xDirExists(parent)) {
            try { fs::create_directories(parent); }
            catch (const std::exception& e) {
                return {"ERROR: could not create parent directory: " + std::string(e.what())};
            }
        }
    }

    std::ofstream outFile(filePath);
    if (!outFile) return {"ERROR: could not open file for writing: " + filePath};
    outFile << content;
    outFile.close();
    return {"Wrote file successfully."};
}

// ---------------------------------------------------------------------------
// Git command helpers
// ---------------------------------------------------------------------------

static std::string xBuildCommand(const std::string& baseCmd, const json& params) {
    std::string cmd = baseCmd;

    auto argsIt = params.find("args");
    if (argsIt != params.end() && argsIt->is_array()) {
        for (const auto& arg : *argsIt) {
            if (arg.is_string()) cmd += " " + xShellEscape(arg.get<std::string>());
            else cmd += " " + arg.dump();
        }
    }

    for (const auto& [key, val] : params.items()) {
        if (key == "args" || key == "timeout") continue;
        std::string flag = key;
        std::replace(flag.begin(), flag.end(), '_', '-');

        if (val.is_boolean() && val.get<bool>()) {
            cmd += " --" + flag;
        } else if (val.is_string() && !val.get<std::string>().empty()) {
            cmd += " --" + flag + "=" + xShellEscape(val.get<std::string>());
        } else if (val.is_number()) {
            cmd += " --" + flag + "=" + std::to_string(val.get<int>());
        }
    }

    return cmd;
}

static HandlerResult xRunCliCommand(const std::string& cmd, const json& params, int defaultTimeout = 60) {
    int timeout = defaultTimeout;
    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end() && timeoutIt->is_number()) {
        timeout = std::min(120, timeoutIt->get<int>() / 1000);
    }

    auto result = CommandRunner::run(cmd, "", timeout);

    if (result.timedOut) {
        return {"ERROR: command timed out"};
    }

    const size_t maxSize = 1024 * 1024;
    std::string output = result.stdout;
    if (!result.stderr.empty()) {
        if (!output.empty()) output += "\n";
        output += result.stderr;
    }
    if (output.size() > maxSize) {
        output.resize(maxSize);
        output += "\n... (truncated)";
    }

    return {output};
}

bool isDockerCommand(const std::string& command) {
    std::string c = command;
    size_t start = 0;
    while (start < c.size() && (c[start] == ' ' || c[start] == '\t')) ++start;
    c = c.substr(start);
    if (c == "docker" || c.rfind("docker ", 0) == 0 || c.rfind("docker\t", 0) == 0) return true;
    if (c == "docker-compose" || c.rfind("docker-compose ", 0) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Git command dispatch
// ---------------------------------------------------------------------------

HandlerResult xGitCommand(const std::string& subcommand, const json& params) {
    std::string cmd = xBuildCommand("git " + subcommand, params);
    return xRunCliCommand(cmd, params);
}

// ---------------------------------------------------------------------------
// Docker command dispatch
// ---------------------------------------------------------------------------

HandlerResult xDockerCommand(const std::string& subcommand, const json& params,
                             DockerSecurityFilter* filter) {
    static const std::vector<std::string> destructive = {
        "rm", "kill", "stop", "pause", "unpause", "rename", "update", "wait"
    };
    bool isDestructive = false;
    for (const auto& d : destructive) {
        if (subcommand == d) { isDestructive = true; break; }
    }

    if (isDestructive && filter) {
        auto containerIt = params.find("container");
        if (containerIt != params.end() && containerIt->is_string()) {
            if (!filter->canAccess(containerIt->get<std::string>())) {
                return {"ERROR: container '" + containerIt->get<std::string>()
                        + "' is a system-managed sandbox container and cannot be modified."};
            }
        }
        auto argsIt = params.find("args");
        if (argsIt != params.end() && argsIt->is_array() && !argsIt->empty()) {
            std::vector<std::string> args;
            for (const auto& a : *argsIt) {
                if (a.is_string()) args.push_back(a.get<std::string>());
            }
            if (!args.empty() && filter && !filter->canAccessAll(args)) {
                return {"ERROR: one or more specified containers are system-managed sandbox containers."};
            }
        }
    }

    std::string cmd = xBuildCommand("docker " + subcommand, params);
    return xRunCliCommand(cmd, params, 120);
}

HandlerResult xDockerComposeCommand(const std::string& subcommand, const json& params) {
    std::string cmd = xBuildCommand("docker compose " + subcommand, params);
    return xRunCliCommand(cmd, params, 120);
}

// ---------------------------------------------------------------------------
// show_skills
// ---------------------------------------------------------------------------

HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr) {
    auto pathIt = params.find("path");
    std::string path = (pathIt != params.end() && pathIt->is_string())
        ? pathIt->get<std::string>() : "/";

    std::ostringstream out;
    out << "Path: " << path << "\n";

    if (path == "/" || path.empty()) {
        out << "Available namespaces: system, local\n";
        out << "  system/ — built-in skills\n";
        out << "  local/ — user-created skills\n";
        out << "\nUse show_skills('/system') or show_skills('/local') to browse.";
        return {out.str()};
    }

    std::string rest = path;
    if (rest.front() == '/') rest = rest.substr(1);
    size_t firstSlash = rest.find('/');
    std::string ns = (firstSlash == std::string::npos) ? rest : rest.substr(0, firstSlash);

    if (skillMgr) {
        auto nsEnum = (ns == "system") ? a0::skills::SkillNamespace::SYSTEM
                    : (ns == "local")  ? a0::skills::SkillNamespace::LOCAL
                    : a0::skills::SkillNamespace::GITHUB;

        auto components = skillMgr->listSkills(nsEnum);

        if (rest.find('/') == std::string::npos) {
            out << "Namespace: " << ns << "\n";
            out << "Components:\n";
            for (const auto& comp : components) {
                out << "  /" << ns << "/" << comp << "/\n";
            }
            out << "\nUse show_skills('/" << ns << "/component') to browse a component's skills.";
            return {out.str()};
        }

        std::string component = rest.substr(firstSlash + 1);
        if (!component.empty() && component.back() == '/') component.pop_back();

        static const char* knownPrompts[] = {
            "start_session", "finish_session", "sync",
            "system_design_base", "generate", "create_issue"
        };
        out << "Skills in /" << ns << "/" << component << ":\n";
        bool found = false;
        for (const auto& pn : knownPrompts) {
            Prompt resolved;
            std::string qn = ns + ":" + component + ":" + pn;
            if (skillMgr->getPromptResolved(qn, resolved) == 0) {
                out << "  " << pn << " — " << resolved.description << "\n";
                found = true;
            }
        }
        if (!found) {
            out << "  (no skills found)\n";
            out << "\nAvailable components in " << ns << ":\n";
            for (const auto& comp : components) {
                out << "  /" << ns << "/" << comp << "/\n";
            }
        }
    } else {
        out << "(no skill manager)\n";
    }

    out << "\nSkills are invoked by their short name as tool calls.";
    return {out.str()};
}

// ---------------------------------------------------------------------------
// show_skill_tools
// ---------------------------------------------------------------------------

HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr) {
    auto pathIt = params.find("path");
    std::string path = (pathIt != params.end() && pathIt->is_string())
        ? pathIt->get<std::string>() : "/";

    std::ostringstream out;
    out << "Path: " << path << "\n";

    if (path == "/" || path.empty()) {
        out << "System tool categories:\n";
        out << "  /system/ — core tools (bash, read, glob, grep, edit, write, etc.)\n";
        out << "  /git/ — git version control tools (120+ commands)\n";
        out << "\nUse show_skill_tools('/system') or show_skill_tools('/git') to browse.";
        return {out.str()};
    }

    std::string rest = path;
    if (rest.front() == '/') rest = rest.substr(1);
    std::string firstPart = rest;
    if (firstPart.find('/') != std::string::npos)
        firstPart = firstPart.substr(0, firstPart.find('/'));

    if (skillMgr) {
        a0::skills::SkillTool skillTool;
        std::string managerComp = firstPart;

        auto components = skillMgr->listSkills(std::nullopt);
        bool isComponent = false;
        for (const auto& c : components) {
            if (c == managerComp) { isComponent = true; break; }
        }

        if (isComponent) {
            if (rest == firstPart) {
                out << "Tools in /" << firstPart << ":\n";
                a0::skills::SkillManifest manifest;
                for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                        a0::skills::SkillNamespace::LOCAL}) {
                    if (skillMgr->getManifest(ns, managerComp, manifest) == 0) {
                        for (const auto& t : manifest.tools) {
                            out << "  " << t.name;
                            if (!t.description.empty())
                                out << " — " << t.description.substr(0, 80);
                            out << "\n";
                        }
                        if (manifest.tools.empty())
                            out << "  (uses {{tool:...}} expansion in skill templates)\n";
                        break;
                    }
                }
                return {out.str()};
            }

            std::string toolName = rest.substr(firstPart.find('/') + 1);
            a0::skills::SkillManifest manifest;
            for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                    a0::skills::SkillNamespace::LOCAL}) {
                if (skillMgr->getManifest(ns, managerComp, manifest) == 0) {
                    for (const auto& t : manifest.tools) {
                        if (t.name == toolName) {
                            std::string nsName = (ns == a0::skills::SkillNamespace::SYSTEM) ? "system" : "local";
                            out << "Tool: " << toolName << "\n";
                            out << "Description: " << t.description << "\n";
                            out << "Path: /" << nsName << "/" << managerComp << "/" << toolName << "\n";
                            if (t.command.empty())
                                out << "Implementation: built-in (system tool handler)\n";
                            else
                                out << "Command: " << t.command << "\n";
                            return {out.str()};
                        }
                    }
                    out << "Tool '" << toolName << "' not found. Available tools in " << firstPart << ":\n";
                    for (const auto& t : manifest.tools) {
                        out << "  " << t.name << "\n";
                    }
                    return {out.str()};
                }
            }
        }
    }

    if (firstPart == "git") {
        out << "Git tools: 120+ commands available via {{tool:commit ...}} in skill templates.\n";
        out << "Use git_start_session for the standard workflow.\n";
    } else if (firstPart == "system") {
        out << "Core system tools: bash, read, glob, grep, edit, write, show_skills, show_skill_tools, tools_for_prompt\n";
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// tools_for_prompt
// ---------------------------------------------------------------------------

HandlerResult xToolsForPrompt(const json& params,
                              a0::skills::SkillManager* skillMgr,
                              InferenceProvider* provider) {
    auto promptIt = params.find("prompt");
    if (promptIt == params.end() || !promptIt->is_string()) {
        return {"ERROR: missing required string parameter 'prompt'"};
    }
    std::string promptText = promptIt->get<std::string>();

    std::ostringstream toolInv;
    std::vector<ToolSchema> schemas;

    if (skillMgr) {
        schemas = skillMgr->schemas(true);

        for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                a0::skills::SkillNamespace::LOCAL}) {
            auto components = skillMgr->listSkills(ns);
            for (const auto& comp : components) {
                a0::skills::SkillManifest manifest;
                if (skillMgr->getManifest(ns, comp, manifest) == 0) {
                    std::string nsName = (ns == a0::skills::SkillNamespace::SYSTEM) ? "system" : "local";
                    if (!manifest.prompts.empty()) {
                        toolInv << "Skills under /" << nsName << "/" << comp << ":\n";
                        for (const auto& p : manifest.prompts) {
                            toolInv << "  /" << nsName << "/" << comp << "/" << p.name;
                            if (!p.description.empty())
                                toolInv << " — " << p.description;
                            toolInv << "\n";
                        }
                    }
                    if (!manifest.tools.empty()) {
                        toolInv << "Tools under /" << nsName << "/" << comp << ":\n";
                        std::vector<std::string> toolNames;
                        for (const auto& t : manifest.tools)
                            toolNames.push_back(t.name);
                        std::sort(toolNames.begin(), toolNames.end());
                        size_t perLine = 0;
                        for (const auto& tn : toolNames) {
                            if (perLine == 0) toolInv << "  ";
                            else if (perLine >= 6) { toolInv << "\n  "; perLine = 0; }
                            else toolInv << ", ";
                            toolInv << tn;
                            ++perLine;
                        }
                        toolInv << "\n";
                    }
                }
            }
        }
    }

    std::ostringstream schemaBlock;
    schemaBlock << "Available tool schemas:\n";
    for (const auto& s : schemas) {
        schemaBlock << "  \"" << s.name << "\": " << s.inputSchema.dump(2) << "\n";
    }

    std::string analysisPrompt =
        "Analyze the user's request and recommend skills/tools.\n\n"
        "User request: \"" + promptText + "\"\n\n"
        + toolInv.str() + "\n"
        + schemaBlock.str() + "\n"
        "Output JSON with this structure:\n"
        "```json\n"
        "{\n"
        "  \"intent\": \"<one-line summary>\",\n"
        "  \"plan\": \"<step-by-step execution plan>\",\n"
        "  \"tools\": [\n"
        "    {\n"
        "      \"name\": \"<tool_name>\",\n"
        "      \"schema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"properties\": {\n"
        "          \"<param_name>\": { \"type\": \"<string|boolean|number>\", \"description\": \"<purpose>\" }\n"
        "        },\n"
        "        \"required\": [\"<param_name>\", ...]\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n"
        "```\n\n"
        "Tool names must come from the available skills/tools listed above.";

    if (!provider) {
        return {"Intent analysis unavailable (no inference provider configured).\n\n"
            "User request: \"" + promptText + "\"\n\n"
            + toolInv.str() + "\n"
            "Use show_skills('/') to browse skills."};
    }

    std::string raw = provider->complete(analysisPrompt, "");

    std::string jsonStr;
    std::smatch match;
    static const std::regex jsonFence(R"(```(?:json)?\s*\n(.+?)```)");
    if (std::regex_search(raw, match, jsonFence)) {
        jsonStr = match[1].str();
    } else {
        jsonStr = raw;
    }

    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
    };
    trim(jsonStr);

    json parsed;
    try {
        parsed = json::parse(jsonStr);
    } catch (...) {
        return {raw};
    }

    std::string plan = parsed.value("plan", raw);
    HandlerResult sr;
    sr.output = plan;

    if (parsed.contains("tools") && parsed["tools"].is_array()) {
        bool allValid = true;
        for (const auto& entry : parsed["tools"]) {
            if (!entry.contains("name") || !entry["name"].is_string()) {
                allValid = false;
                break;
            }
            std::string toolName = entry["name"].get<std::string>();

            const ToolSchema* actual = nullptr;
            for (const auto& s : schemas) {
                if (s.name == toolName) { actual = &s; break; }
            }
            if (!actual) { allValid = false; break; }

            if (!entry.contains("schema") || !entry["schema"].is_object()) {
                allValid = false;
                break;
            }
            json genSchema = entry["schema"];
            json actualSchema = actual->inputSchema;

            if (genSchema.value("type", "") != actualSchema.value("type", "")) {
                allValid = false;
                break;
            }

            json genProps = genSchema.value("properties", json::object());
            json actualProps = actualSchema.value("properties", json::object());
            for (auto it = genProps.begin(); it != genProps.end(); ++it) {
                auto actualIt = actualProps.find(it.key());
                if (actualIt == actualProps.end()) { allValid = false; break; }
                if (it.value().value("type", "") != actualIt->value("type", "")) { allValid = false; break; }
            }
            if (!allValid) break;

            json genRequired = genSchema.value("required", json::array());
            json actualRequired = actualSchema.value("required", json::array());
            std::unordered_set<std::string> genReqSet;
            for (const auto& r : genRequired) genReqSet.insert(r.get<std::string>());
            for (const auto& r : actualRequired) {
                if (genReqSet.find(r.get<std::string>()) == genReqSet.end()) { allValid = false; break; }
            }
            if (!allValid) break;

            sr.recommendedTools.push_back(toolName);
        }

        if (!allValid) sr.recommendedTools.clear();
    }

    return sr;
}

} // namespace a0
