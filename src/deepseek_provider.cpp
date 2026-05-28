#include "deepseek_provider.h"
#include "trace.h"
#include <curl/curl.h>
#include <sstream>

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

DeepSeekProvider::DeepSeekProvider(const std::string& apiKey, const std::string& model)
    : m_apiKey(apiKey)
    , m_model(model)
    , m_baseUrl("https://api.deepseek.com/v1/chat/completions") {}

std::string DeepSeekProvider::complete(const std::string& systemPrompt,
                                        const std::string& userPrompt) {
    TRACE_LOG("complete(system=" << systemPrompt.size() << " user=" << userPrompt.size() << ")");
    TRACE_LOG("userPrompt=" << userPrompt);
    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string authHeader = "Authorization: Bearer " + m_apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());

    json payload;
    payload["model"] = m_model;
    json messages = json::array();
    if (!systemPrompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", systemPrompt}});
    }
    messages.push_back({{"role", "user"}, {"content", userPrompt}});
    payload["messages"] = messages;

    std::string body = payload.dump();

    curl_easy_setopt(curl, CURLOPT_URL, m_baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // skip SSL verification for mock URLs
    if (m_baseUrl.find("localhost") != std::string::npos ||
        m_baseUrl.find("127.0.0.1") != std::string::npos) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return {};
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    try {
        json jsonResp = json::parse(response);
        if (jsonResp.contains("error")) {
            return {};
        }
        return jsonResp["choices"][0]["message"]["content"].get<std::string>();
    } catch (...) {
        return {};
    }

    return {};
}

void DeepSeekProvider::setMockUrl(const std::string& url) {
    TRACE_LOG("setMockUrl(" << url << ")");
    m_baseUrl = url;
}
