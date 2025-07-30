#ifndef BARK_PUSH_HPP
#define BARK_PUSH_HPP

#include <string>
#include <iostream>
#include <curl/curl.h>

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s)
{
    size_t newLength = size * nmemb;
    try
    {
        s->append((char *)contents, newLength);
    }
    catch (std::bad_alloc &e)
    {
        return 0;
    }
    return newLength;
}

class BarkPush
{
private:
    std::string token_;
    std::string server_;

public:
    BarkPush(const std::string &token,
             const std::string &server = "https://api.day.app/") // <—— Default server URL
        : token_(token), server_(server)
    {
        // curl init
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~BarkPush()
    {
        // curl cleanup
        curl_global_cleanup();
    }

    // send msg
    bool send(const std::string &title, const std::string &message)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            std::cerr << "BarkPush: curl initialization failed" << std::endl;
            return false;
        }

        // build the URL
        std::string url = server_ + token_;

        // build the JSON data (escape special characters)
        std::string json_title = escapeJson(title);
        std::string json_message = escapeJson(message);
        std::string json_data = "{\"title\":\"" + json_title + "\",\"body\":\"" + json_message + "\"}";

        // set curl options
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

        // set HTTP headers
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // handle response
        std::string response_string;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        // perform the request
        CURLcode res = curl_easy_perform(curl);
        bool success = false;

        if (res != CURLE_OK)
        {
            std::cerr << "BarkPush: curl request failed: " << curl_easy_strerror(res) << std::endl;
        }
        else
        {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            success = (response_code == 200);

            if (!success)
            {
                std::cerr << "BarkPush: server returned error: " << response_code << std::endl;
                std::cerr << "BarkPush: response content: " << response_string << std::endl;
            }
        }

        // cleanup
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);

        return success;
    }

private:
    // Escape special characters in JSON strings
    std::string escapeJson(const std::string &input)
    {
        std::string output;
        output.reserve(input.length());

        for (char c : input)
        {
            switch (c)
            {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output += c;
                break;
            }
        }

        return output;
    }
};

#endif
