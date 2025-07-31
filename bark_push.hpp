#ifndef BARK_PUSH_HPP
#define BARK_PUSH_HPP

#include <string>
#include <iostream>
#include <map>
#include <vector>
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <regex>

const std::string DEFAULT_BARK_SERVER = "https://api.day.app/";

enum class BarkError
{
    SUCCESS = 0,
    CURL_INIT_FAILED,
    INVALID_URL,
    HTTP_ERROR,
    NETWORK_ERROR,
    EMPTY_RESPONSE,
    NO_DEVICES_SPECIFIED
};

inline std::string barkErrorToString(BarkError err)
{
    switch (err)
    {
        case BarkError::SUCCESS: return "Success";
        case BarkError::CURL_INIT_FAILED: return "cURL initialization failed";
        case BarkError::INVALID_URL: return "Invalid URL format";
        case BarkError::HTTP_ERROR: return "HTTP request failed";
        case BarkError::NETWORK_ERROR: return "Network communication error";
        case BarkError::EMPTY_RESPONSE: return "Server returned empty response";
        case BarkError::NO_DEVICES_SPECIFIED: return "No device keys specified";
        default: return "Unknown error";
    }
}

class BarkPush
{
private:
    std::vector<std::string> device_keys_;
    std::string server_;
    CURL *curl_handle_;
    std::string last_error_;
    long http_status_code_;

    BarkPush(const BarkPush&) = delete;
    BarkPush& operator=(const BarkPush&) = delete;

    static bool initCurlGlobal()
    {
        static bool initialized = false;
        if (!initialized)
        {
            CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (res != CURLE_OK)
            {
                std::cerr << "Global cURL initialization failed: " << curl_easy_strerror(res) << std::endl;
                return false;
            }
            initialized = true;
            std::atexit([]() { curl_global_cleanup(); });
        }
        return true;
    }

    static std::string normalizeUrl(const std::string &url)
    {
        if (url.empty())
            return url;
        
        const std::string http_prefix = "http://";
        const std::string https_prefix = "https://";
        
        if (url.size() >= http_prefix.size() && url.substr(0, http_prefix.size()) == http_prefix)
            return url;
        if (url.size() >= https_prefix.size() && url.substr(0, https_prefix.size()) == https_prefix)
            return url;
            
        return https_prefix + url;
    }

    static std::string escapeJson(const std::string &input)
    {
        std::ostringstream output;
        for (char c : input)
        {
            switch (c)
            {
            case '"':  output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1F)
                {
                    output << "\\u" << std::setw(4) << std::setfill('0')
                           << std::hex << static_cast<int>(c);
                }
                else
                {
                    output << c;
                }
            }
        }
        return output.str();
    }

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *s)
    {
        size_t newLength = size * nmemb;
        try
        {
            s->append(static_cast<char *>(contents), newLength);
            return newLength;
        }
        catch (...)
        {
            return 0;
        }
    }

    bool setCurlOption(CURLoption option, const char* value)
    {
        CURLcode res = curl_easy_setopt(curl_handle_, option, value);
        if (res != CURLE_OK)
        {
            last_error_ = "Failed to set cURL option: " + std::string(curl_easy_strerror(res));
            return false;
        }
        return true;
    }

    bool setCurlOption(CURLoption option, long value)
    {
        CURLcode res = curl_easy_setopt(curl_handle_, option, value);
        if (res != CURLE_OK)
        {
            last_error_ = "Failed to set cURL option: " + std::string(curl_easy_strerror(res));
            return false;
        }
        return true;
    }

    bool setCurlOption(CURLoption option, size_t (*callback)(void*, size_t, size_t, std::string*))
    {
        CURLcode res = curl_easy_setopt(curl_handle_, option, callback);
        if (res != CURLE_OK)
        {
            last_error_ = "Failed to set cURL callback: " + std::string(curl_easy_strerror(res));
            return false;
        }
        return true;
    }

public:
    BarkPush(const std::string &single_key, const std::string &server = DEFAULT_BARK_SERVER)
        : server_(server), curl_handle_(nullptr), http_status_code_(0)
    {
        if (!single_key.empty())
        {
            device_keys_.push_back(single_key);
        }
        init();
    }

    BarkPush(const std::vector<std::string> &multi_keys, const std::string &server = DEFAULT_BARK_SERVER)
        : device_keys_(multi_keys), server_(server), curl_handle_(nullptr), http_status_code_(0)
    {
        init();
    }

    ~BarkPush()
    {
        if (curl_handle_)
        {
            curl_easy_cleanup(curl_handle_);
            curl_handle_ = nullptr;
        }
    }

    void addDeviceKey(const std::string &key)
    {
        if (!key.empty())
        {
            device_keys_.push_back(key);
        }
    }

    void clearDeviceKeys()
    {
        device_keys_.clear();
    }

    std::vector<std::string> getDeviceKeys() const
    {
        return device_keys_;
    }

    void setDefaultOptions()
    {
        if (!curl_handle_)
            return;
            
        setCurlOption(CURLOPT_CONNECTTIMEOUT, 5L);
        setCurlOption(CURLOPT_TIMEOUT, 10L);
        setCurlOption(CURLOPT_SSL_VERIFYPEER, 1L);
        setCurlOption(CURLOPT_SSL_VERIFYHOST, 2L);
        setCurlOption(CURLOPT_USERAGENT, "BarkPush-C++/1.0");
    }

    void disableSslVerification()
    {
        if (curl_handle_)
        {
            setCurlOption(CURLOPT_SSL_VERIFYPEER, 0L);
            setCurlOption(CURLOPT_SSL_VERIFYHOST, 0L);
        }
    }

    std::string getLastError() const
    {
        return last_error_;
    }

    long getLastHttpStatusCode() const
    {
        return http_status_code_;
    }

    BarkError send(const std::string &title,
                   const std::string &message,
                   const std::map<std::string, std::string> &params = {})
    {
        last_error_.clear();
        http_status_code_ = 0;

        if (device_keys_.empty())
        {
            last_error_ = "No device keys specified";
            return BarkError::NO_DEVICES_SPECIFIED;
        }

        if (!curl_handle_)
        {
            last_error_ = "cURL handle not initialized";
            return BarkError::CURL_INIT_FAILED;
        }

        std::string url = server_;
        if (url.back() != '/')
            url += '/';
        url += "push";

        std::map<std::string, std::string> processed_params = params;
        auto url_it = processed_params.find("url");
        if (url_it != processed_params.end())
        {
            url_it->second = normalizeUrl(url_it->second);
        }

        std::ostringstream json_stream;
        json_stream << "{";
        json_stream << "\"device_keys\":[";
        for (size_t i = 0; i < device_keys_.size(); ++i)
        {
            if (i > 0)
                json_stream << ",";
            json_stream << "\"" << escapeJson(device_keys_[i]) << "\"";
        }
        json_stream << "],";
        json_stream << "\"title\":\"" << escapeJson(title) << "\",";
        json_stream << "\"body\":\"" << escapeJson(message) << "\"";

        static const std::regex number_regex("^[-+]?[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+)?$");
        
        for (const auto &[key, value] : processed_params)
        {
            json_stream << ",";
            json_stream << "\"" << key << "\":";
            
            if (value == "true" || value == "false")
            {
                json_stream << value;
            }
            else if (std::regex_match(value, number_regex))
            {
                json_stream << value;
            }
            else
            {
                json_stream << "\"" << escapeJson(value) << "\"";
            }
        }
        json_stream << "}";

        std::string json_data = json_stream.str();

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        setCurlOption(CURLOPT_URL, url.c_str());
        setCurlOption(CURLOPT_POST, 1L);
        setCurlOption(CURLOPT_POSTFIELDS, json_data.c_str());
        curl_easy_setopt(curl_handle_, CURLOPT_HTTPHEADER, headers);
        setCurlOption(CURLOPT_WRITEFUNCTION, writeCallback);

        std::string response_string;
        curl_easy_setopt(curl_handle_, CURLOPT_WRITEDATA, &response_string);

        CURLcode res = curl_easy_perform(curl_handle_);
        curl_slist_free_all(headers);

        if (res != CURLE_OK)
        {
            last_error_ = "cURL error: " + std::string(curl_easy_strerror(res));
            return BarkError::NETWORK_ERROR;
        }

        curl_easy_getinfo(curl_handle_, CURLINFO_RESPONSE_CODE, &http_status_code_);
        if (http_status_code_ != 200)
        {
            last_error_ = "HTTP error " + std::to_string(http_status_code_) +
                          ", Response: " + response_string;
            return BarkError::HTTP_ERROR;
        }

        if (response_string.empty())
        {
            last_error_ = "Empty response from server";
            return BarkError::EMPTY_RESPONSE;
        }

        return BarkError::SUCCESS;
    }

    BarkError sendAdvanced(
        const std::string &title,
        const std::string &message,
        const std::string &url = "",
        const std::string &sound = "",
        const std::string &group = "",
        const std::string &level = "",
        const std::string &icon = "",
        const std::string &archive = "1",
        const std::string &autoCopy = "0") 
    {
        std::map<std::string, std::string> params;
        if (!url.empty())
            params["url"] = normalizeUrl(url);
        if (!sound.empty())
            params["sound"] = sound;
        if (!group.empty())
            params["group"] = group;
        if (!level.empty())
            params["level"] = level;
        if (!icon.empty())
            params["icon"] = icon;
        params["archive"] = archive;
        params["autoCopy"] = autoCopy;
        return send(title, message, params);
    }

    BarkError sendCopy(const std::string &title, const std::string &message)
    {
        return sendAdvanced(title, message, "", "", "", "", "", "1", "1");
    }

    BarkError sendUrl(const std::string &url)
    {
        std::string normalizedUrl = normalizeUrl(url);
        return sendAdvanced("跳转链接", normalizedUrl, normalizedUrl, 
                           "", "", "", "", "1", "0");
    }

    BarkError sendUrl(const std::string &title, const std::string &message, const std::string &url)
    {
        return sendAdvanced(title, message, normalizeUrl(url), 
                           "", "", "", "", "1", "0");
    }

    BarkError sendCritical(const std::string &title, const std::string &message)
    {
        return sendAdvanced(title, message, "", "", "", "critical", "", "1", "0");
    }

    BarkError sendCall(const std::string &title, const std::string &message)
    {
        std::map<std::string, std::string> params;
        params["call"] = "1";
        params["archive"] = "1";
        return send(title, message, params);
    }

    BarkError sendSilence(const std::string &title, const std::string &message)
    {
        return sendAdvanced(title, message, "", "silence", "", "", "", "1", "0");
    }

private:
    void init()
    {
        if (!initCurlGlobal())
        {
            throw std::runtime_error("Failed to initialize cURL globally");
        }
        
        CURL* temp_handle = curl_easy_init();
        if (!temp_handle)
        {
            throw std::runtime_error("cURL initialization failed");
        }
        
        curl_handle_ = temp_handle;
        setDefaultOptions();
    }
};

#endif
