#include "system_tools/registry.h"
#include "command_runner.h"
#include <algorithm>
#include <cstring>
#include <fstream>
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

static bool globMatch(const std::string& pattern, const std::string& str) {
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
// Bash — with git command detection
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xBash(const json& params) {
    auto cmdIt = params.find("command");
    if (cmdIt == params.end() || !cmdIt->is_string()) {
        return {"ERROR: missing required string parameter 'command'"};
    }
    std::string command = cmdIt->get<std::string>();

    // Reject git commands even when wrapped (cd ... && git ..., git status)
    std::string c = trim(command);
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
        return {"ERROR: git commands must use the start_session skill prompt or "
                "run_skill with a git skill path. "
                "Browse skills: show_skills('/system/git'). "
                "Use run_skill('/system/git/start_session') for the standard workflow."};
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
    auto pathIt = params.find("file_path");
    if (pathIt == params.end() || !pathIt->is_string()) {
        pathIt = params.find("filePath");  // fallback for backward compat
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

    if (dirExists(filePath)) {
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
            if (pathIsExcluded(entry.path())) continue;
            if (results.size() >= static_cast<size_t>(maxResults)) break;
            std::string fp = entry.path().string();
            std::string rel = entry.path().lexically_relative(searchPath).string();
            if (std::count(rel.begin(), rel.end(), '/') > 32) continue;
            if (!includeFilter.empty() && !globMatch(includeFilter, entry.path().filename().string())) continue;
            if (isBinaryFile(fp)) continue;
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
        out << "\n  " << results[i].lineNum << ": " << trim(results[i].content);
    }
    return {out.str()};
}

// ---------------------------------------------------------------------------
// Edit
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xEdit(const json& params) {
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

    if (!fileExists(filePath)) {
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

SystemToolResult SystemToolRegistry::xWrite(const json& params) {
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
        if (!dirExists(parent)) {
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

} // namespace a0
