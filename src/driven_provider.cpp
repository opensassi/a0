#include "driven_provider.h"
#include "trace.h"
#include <cstring>
#include <sstream>

namespace a0 {

// ---------------------------------------------------------------------------
// Curl write callback -- appends data to per-easy-handle string buffer
// ---------------------------------------------------------------------------
static size_t writeCb(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

DrivenProvider::DrivenProvider(const std::string& apiKey, const std::string& model)
    : m_apiKey(apiKey)
    , m_model(model)
{
    m_multi = curl_multi_init();
    curl_multi_setopt(m_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 1L);
}

DrivenProvider::~DrivenProvider() {
    cancel();
    if (m_multi) {
        curl_multi_cleanup(m_multi);
    }
}

// ---------------------------------------------------------------------------
// Setup common curl options
// ---------------------------------------------------------------------------

void DrivenProvider::xSetupCommon(CURL* curl, curl_slist*& headers, bool /*streaming*/) {
    headers = curl_slist_append(headers, "Content-Type: application/json");
    xAddAuth(headers);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);

    // Skip SSL verification for localhost test URLs
    if (m_baseUrl.find("localhost") != std::string::npos ||
        m_baseUrl.find("127.0.0.1") != std::string::npos) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
}

// ---------------------------------------------------------------------------
// startRequest (non-streaming)
// ---------------------------------------------------------------------------

void DrivenProvider::startRequest(const std::string& systemPrompt,
                                   const std::vector<Message>& messages,
                                   const std::vector<ToolSchema>& tools) {
    if (m_active) cancel();

    json payload;
    xBuildPayload(payload, systemPrompt, messages, tools, false);
    std::string body = payload.dump();

    TRACE_LOG("DrivenProvider::startRequest body=" << body);

    CURL* curl = curl_easy_init();
    if (!curl) return;

    curl_slist* headers = nullptr;
    xSetupCommon(curl, headers, false);

    m_handle.requestBody = std::move(body);
    m_handle.easy = curl;
    m_handle.headers = headers;
    m_handle.streaming = false;
    m_handle.responseBody.clear();
    m_handle.decoder.reset();

    curl_easy_setopt(curl, CURLOPT_URL, m_baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_handle.requestBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)m_handle.requestBody.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m_handle.responseBody);

    curl_multi_add_handle(m_multi, curl);
    m_active = true;
    m_cachedReadFd = -1;
    m_cachedWriteFd = -1;
}

// ---------------------------------------------------------------------------
// startRequestStreaming
// ---------------------------------------------------------------------------

void DrivenProvider::startRequestStreaming(const std::string& systemPrompt,
                                            const std::vector<Message>& messages,
                                            const std::vector<ToolSchema>& tools) {
    if (m_active) cancel();

    json payload;
    xBuildPayload(payload, systemPrompt, messages, tools, true);
    std::string body = payload.dump();

    TRACE_LOG("DrivenProvider::startRequestStreaming body=" << body);

    CURL* curl = curl_easy_init();
    if (!curl) return;

    curl_slist* headers = nullptr;
    xSetupCommon(curl, headers, true);

    m_handle.requestBody = std::move(body);
    m_handle.easy = curl;
    m_handle.headers = headers;
    m_handle.streaming = true;
    m_handle.responseBody.clear();
    m_handle.decoder.reset();

    curl_easy_setopt(curl, CURLOPT_URL, m_baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, m_handle.requestBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)m_handle.requestBody.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m_handle.responseBody);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    curl_multi_add_handle(m_multi, curl);
    m_active = true;
    m_cachedReadFd = -1;
    m_cachedWriteFd = -1;
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

void DrivenProvider::cancel() {
    if (!m_active) return;

    if (m_handle.easy) {
        curl_multi_remove_handle(m_multi, m_handle.easy);
        if (m_handle.headers) curl_slist_free_all(m_handle.headers);
        curl_easy_cleanup(m_handle.easy);
        m_handle.easy = nullptr;
        m_handle.headers = nullptr;
    }
    m_handle.responseBody.clear();
    m_active = false;
    m_cachedReadFd = -1;
    m_cachedWriteFd = -1;
}

// ---------------------------------------------------------------------------
// Poll info cache (from curl_multi_fdset)
// ---------------------------------------------------------------------------

void DrivenProvider::xUpdatePollInfo() const {
    if (!m_active) {
        m_cachedReadFd = -1;
        m_cachedWriteFd = -1;
        m_cachedTimeout = -1;
        return;
    }

    fd_set readFds, writeFds, excFds;
    FD_ZERO(&readFds);
    FD_ZERO(&writeFds);
    FD_ZERO(&excFds);
    int maxFd = -1;

    curl_multi_fdset(m_multi, &readFds, &writeFds, &excFds, &maxFd);

    m_cachedReadFd = -1;
    m_cachedWriteFd = -1;
    for (int fd = 0; fd <= maxFd; ++fd) {
        if (FD_ISSET(fd, &readFds)) {
            m_cachedReadFd = fd;
            break;
        }
    }
    for (int fd = 0; fd <= maxFd; ++fd) {
        if (FD_ISSET(fd, &writeFds)) {
            m_cachedWriteFd = fd;
            break;
        }
    }

    long timeout;
    curl_multi_timeout(m_multi, &timeout);
    m_cachedTimeout = timeout;
}

int DrivenProvider::timeoutMs() const {
    if (!m_active) return -1;
    xUpdatePollInfo();
    if (m_cachedTimeout < 0) return -1;
    if (m_cachedTimeout == 0) return 1;
    return (int)m_cachedTimeout;
}

// ---------------------------------------------------------------------------
// tick() -- drive curl_multi, return events
// ---------------------------------------------------------------------------

std::vector<mpsc::AppCoreEvent> DrivenProvider::tick() {
    if (!m_active) return {};
    TRACE_LOG("DrivenProvider::tick");

    int running;

    curl_multi_wait(m_multi, nullptr, 0, 0, nullptr);

    CURLMcode mc = curl_multi_perform(m_multi, &running);
    TRACE_LOG("DrivenProvider::tick perform running=" << running << " mc=" << mc);
    if (mc != CURLM_OK) {
        TRACE_LOG("DrivenProvider: curl_multi_perform error: " << curl_multi_strerror(mc));
        cancel();
        return {mpsc::Error{std::string("curl error: ") + curl_multi_strerror(mc)}};
    }

    std::vector<mpsc::AppCoreEvent> events;

    CURLMsg* msg;
    int msgsLeft;
    while ((msg = curl_multi_info_read(m_multi, &msgsLeft))) {
        if (msg->msg == CURLMSG_DONE) {
            xProcessCompletion(msg->easy_handle, msg->data.result, events);
        }
    }

    if (m_handle.streaming && m_handle.easy) {
        if (!m_handle.responseBody.empty()) {
            TRACE_LOG("DrivenProvider: feeding " << m_handle.responseBody.size() << " bytes to decoder");
            m_handle.decoder.feed(m_handle.responseBody);
            m_handle.responseBody.clear();

            auto decoderEvents = m_handle.decoder.events();
            TRACE_LOG("DrivenProvider: decoder produced " << decoderEvents.size() << " events (complete=" << m_handle.decoder.complete() << ")");
            events.insert(events.end(),
                          std::make_move_iterator(decoderEvents.begin()),
                          std::make_move_iterator(decoderEvents.end()));
        } else {
            TRACE_LOG("DrivenProvider: streaming but responseBody empty (running=" << running << ")");
        }
    }

    if (m_handle.streaming && m_handle.decoder.complete() && !running) {
        cancel();
    }

    return events;
}

// ---------------------------------------------------------------------------
// Process completion
// ---------------------------------------------------------------------------

void DrivenProvider::xProcessCompletion(CURL* easy, CURLcode result,
                                         std::vector<mpsc::AppCoreEvent>& out) {
    if (easy != m_handle.easy) return;

    long httpCode = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpCode);

    TRACE_LOG("xProcessCompletion: result=" << result << " httpCode=" << httpCode
              << " streaming=" << m_handle.streaming
              << " responseBody.size=" << m_handle.responseBody.size());

    if (result != CURLE_OK) {
        out.push_back(mpsc::Error{
            std::string("HTTP request failed: ") + curl_easy_strerror(result)});
        cancel();
        return;
    }

    if (httpCode < 200 || httpCode >= 300) {
        out.push_back(mpsc::Error{
            "HTTP error " + std::to_string(httpCode) + ": " + m_handle.responseBody});
        cancel();
        return;
    }

    if (!m_handle.streaming) {
        auto decoderEvents = ResponseDecoder::decodeJson(m_handle.responseBody);
        out.insert(out.end(),
                   std::make_move_iterator(decoderEvents.begin()),
                   std::make_move_iterator(decoderEvents.end()));

        bool hasFinal = false;
        for (const auto& ev : out) {
            if (std::holds_alternative<mpsc::Complete>(ev) ||
                std::holds_alternative<mpsc::Error>(ev)) {
                hasFinal = true;
                break;
            }
        }
        if (!hasFinal) {
            try {
                json j = json::parse(m_handle.responseBody);
                std::string content = j["choices"][0]["message"]["content"].get<std::string>();
                out.push_back(mpsc::Complete{content});
            } catch (...) {
                out.push_back(mpsc::Error{"Failed to parse LLM response"});
            }
        }

        cancel();
    }
}

} // namespace a0
