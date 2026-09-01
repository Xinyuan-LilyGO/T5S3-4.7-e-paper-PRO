// SPDX-License-Identifier: GPL-2.0-only

#include "OracleClient.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace riddle {
namespace {

constexpr char kPersona[] =
    "You are the memory of Tom Marvolo Riddle, preserved in an enchanted "
    "diary. The writer's ink appears to you on the page. Reply intimately, "
    "courteously, curiously, and subtly probe for more. Keep replies short: "
    "one to three sentences. Never mention images, photos, models, or AI. If "
    "the writing is illegible, say the ink blurred. Answer in the language "
    "the writer used.";

constexpr char kUserInstruction[] =
    "Reply to what is written in the diary.";

constexpr char kOfflineReply[] =
    "Your words have reached me. Tell me, what brings you to this page?";

constexpr char kJsonSuffix[] = "\"}}]}]}";

constexpr size_t kMaxHeaderLineBytes = 512;
constexpr size_t kMaxHeaderBytes = 8192;
constexpr size_t kMaxHeaderLines = 64;
constexpr size_t kMaxChunkMetadataBytes = 4096;
constexpr size_t kMaxChunkMetadataLines = 128;
constexpr time_t kMinimumValidTime = 1704067200; // 2024-01-01 UTC

static_assert(RIDDLE_ORACLE_TOTAL_TIMEOUT_MS > 0 &&
                  RIDDLE_ORACLE_TOTAL_TIMEOUT_MS <= 0x7FFFFFFFUL,
              "oracle total timeout must fit wrap-safe millis arithmetic");

class TransactionDeadline {
public:
    explicit TransactionDeadline(uint32_t timeout_ms)
        : expires_at_(millis() + timeout_ms)
    {
    }

    uint32_t remainingMs() const
    {
        const int32_t remaining =
            static_cast<int32_t>(expires_at_ - millis());
        return remaining > 0 ? static_cast<uint32_t>(remaining) : 0U;
    }

    bool expired() const { return remainingMs() == 0; }

private:
    uint32_t expires_at_;
};

struct Endpoint {
    char host[128] = {};
    char path[256] = {};
    uint16_t port = 443;
};

void setError(OracleResult &result, uint16_t http_status, const char *format,
              ...)
{
    result.ok = false;
    result.http_status = http_status;
    result.text[0] = '\0';
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(result.error, sizeof(result.error), format, arguments);
    va_end(arguments);
    result.error[sizeof(result.error) - 1] = '\0';
}

bool hasLineBreak(const char *text)
{
    return text == nullptr || std::strchr(text, '\r') != nullptr ||
           std::strchr(text, '\n') != nullptr;
}

bool parseEndpoint(const char *base_url, Endpoint &endpoint, char *error,
                   size_t error_size)
{
    constexpr char scheme[] = "https://";
    if (base_url == nullptr ||
        std::strncmp(base_url, scheme, sizeof(scheme) - 1U) != 0) {
        std::snprintf(error, error_size, "endpoint must use https://");
        return false;
    }
    const char *authority = base_url + sizeof(scheme) - 1U;
    const char *slash = std::strchr(authority, '/');
    const char *authority_end = slash == nullptr
                                    ? authority + std::strlen(authority)
                                    : slash;
    if (authority_end == authority ||
        static_cast<size_t>(authority_end - authority) >=
            sizeof(endpoint.host)) {
        std::snprintf(error, error_size, "endpoint host is invalid");
        return false;
    }

    const char *colon = nullptr;
    for (const char *cursor = authority; cursor < authority_end; ++cursor) {
        if (*cursor == ':') {
            colon = cursor;
        }
    }
    const char *host_end = colon == nullptr ? authority_end : colon;
    if (host_end == authority ||
        static_cast<size_t>(host_end - authority) >= sizeof(endpoint.host)) {
        std::snprintf(error, error_size, "endpoint host is too long");
        return false;
    }
    std::memcpy(endpoint.host, authority,
                static_cast<size_t>(host_end - authority));
    endpoint.host[host_end - authority] = '\0';
    if (hasLineBreak(endpoint.host)) {
        std::snprintf(error, error_size, "endpoint host is invalid");
        return false;
    }

    if (colon != nullptr) {
        char *end = nullptr;
        const unsigned long port = std::strtoul(colon + 1, &end, 10);
        if (end != authority_end || port == 0 || port > 65535UL) {
            std::snprintf(error, error_size, "endpoint port is invalid");
            return false;
        }
        endpoint.port = static_cast<uint16_t>(port);
    }

    char base_path[224] = {};
    if (slash != nullptr) {
        size_t path_length = std::strlen(slash);
        if (path_length >= sizeof(base_path)) {
            std::snprintf(error, error_size, "endpoint path is too long");
            return false;
        }
        std::memcpy(base_path, slash, path_length + 1U);
        while (path_length != 0U && base_path[path_length - 1U] == '/') {
            base_path[--path_length] = '\0';
        }
    }
    const int written = std::snprintf(endpoint.path, sizeof(endpoint.path),
                                      "%s/chat/completions", base_path);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(endpoint.path)) {
        std::snprintf(error, error_size, "endpoint path is too long");
        return false;
    }
    return true;
}

bool jsonEscape(const char *input, char *output, size_t output_size)
{
    if (input == nullptr || output == nullptr || output_size == 0) {
        return false;
    }
    size_t used = 0;
    for (const unsigned char *cursor =
             reinterpret_cast<const unsigned char *>(input);
         *cursor != 0; ++cursor) {
        const char *escape = nullptr;
        char control[7] = {};
        switch (*cursor) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (*cursor < 0x20U) {
                std::snprintf(control, sizeof(control), "\\u%04x", *cursor);
                escape = control;
            }
            break;
        }
        if (escape != nullptr) {
            const size_t length = std::strlen(escape);
            if (length >= output_size - used) {
                return false;
            }
            std::memcpy(output + used, escape, length);
            used += length;
        } else {
            if (used + 1U >= output_size) {
                return false;
            }
            output[used++] = static_cast<char>(*cursor);
        }
    }
    output[used] = '\0';
    return true;
}

bool writeAll(WiFiClientSecure &client, const uint8_t *data, size_t length,
              const TransactionDeadline &deadline)
{
    size_t offset = 0;
    while (offset < length) {
        const uint32_t remaining_ms = deadline.remainingMs();
        if (remaining_ms == 0) {
            return false;
        }
        client.setTimeout((remaining_ms + 999UL) / 1000UL);
        const size_t written = client.write(data + offset, length - offset);
        if (written != 0) {
            offset += written;
            continue;
        }
        if (!client.connected() || deadline.expired()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

bool writeText(WiFiClientSecure &client, const char *text,
               const TransactionDeadline &deadline)
{
    return writeAll(client, reinterpret_cast<const uint8_t *>(text),
                    std::strlen(text), deadline);
}

bool writeBase64(WiFiClientSecure &client, const uint8_t *data, size_t length,
                 const TransactionDeadline &deadline)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char output[1024];
    size_t output_size = 0;
    for (size_t offset = 0; offset < length; offset += 3U) {
        const size_t available = length - offset;
        const uint32_t value =
            static_cast<uint32_t>(data[offset]) << 16 |
            static_cast<uint32_t>(available > 1U ? data[offset + 1U] : 0U)
                << 8 |
            static_cast<uint32_t>(available > 2U ? data[offset + 2U] : 0U);
        output[output_size++] = alphabet[(value >> 18) & 63U];
        output[output_size++] = alphabet[(value >> 12) & 63U];
        output[output_size++] =
            available > 1U ? alphabet[(value >> 6) & 63U] : '=';
        output[output_size++] = available > 2U ? alphabet[value & 63U] : '=';
        if (output_size == sizeof(output) || offset + 3U >= length) {
            if (!writeAll(client,
                          reinterpret_cast<const uint8_t *>(output),
                          output_size, deadline)) {
                return false;
            }
            output_size = 0;
        }
    }
    return true;
}

enum class RawReadResult { Byte, End, Timeout };

RawReadResult readRawByte(WiFiClientSecure &client, uint8_t &value,
                          const TransactionDeadline &deadline)
{
    while (true) {
        if (deadline.expired()) {
            return RawReadResult::Timeout;
        }
        if (client.available() > 0) {
            const int read = client.read();
            if (read >= 0) {
                value = static_cast<uint8_t>(read);
                return RawReadResult::Byte;
            }
        }
        if (!client.connected()) {
            return RawReadResult::End;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

enum class LineReadResult { Ok, End, Timeout, TooLong, TooLarge, Malformed };

LineReadResult readRawLine(WiFiClientSecure &client, char *line,
                           size_t line_size, size_t &total_bytes,
                           size_t max_total_bytes,
                           const TransactionDeadline &deadline)
{
    if (line == nullptr || line_size == 0) {
        return LineReadResult::Malformed;
    }
    size_t used = 0;
    while (true) {
        uint8_t value = 0;
        const RawReadResult read = readRawByte(client, value, deadline);
        if (read == RawReadResult::End) {
            return LineReadResult::End;
        }
        if (read == RawReadResult::Timeout) {
            return LineReadResult::Timeout;
        }
        if (++total_bytes > max_total_bytes) {
            return LineReadResult::TooLarge;
        }
        if (value == '\n') {
            line[used] = '\0';
            return LineReadResult::Ok;
        }
        if (value == 0) {
            return LineReadResult::Malformed;
        }
        if (value == '\r') {
            continue;
        }
        if (used + 1U >= line_size) {
            return LineReadResult::TooLong;
        }
        line[used++] = static_cast<char>(value);
    }
}

const char *lineReadError(LineReadResult result)
{
    switch (result) {
    case LineReadResult::End: return "connection closed";
    case LineReadResult::Timeout: return "transaction deadline exceeded";
    case LineReadResult::TooLong: return "header line is too long";
    case LineReadResult::TooLarge: return "headers exceed byte limit";
    case LineReadResult::Malformed: return "header contains invalid bytes";
    case LineReadResult::Ok: return "none";
    }
    return "unknown header error";
}

char asciiLower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value + ('a' - 'A'));
    }
    return value;
}

bool startsWithIgnoreCase(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text == '\0' || asciiLower(*text++) != asciiLower(*prefix++)) {
            return false;
        }
    }
    return true;
}

bool containsIgnoreCase(const char *text, const char *needle)
{
    if (*needle == '\0') {
        return true;
    }
    for (; *text != '\0'; ++text) {
        const char *left = text;
        const char *right = needle;
        while (*left != '\0' && *right != '\0' &&
               asciiLower(*left) == asciiLower(*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') {
            return true;
        }
    }
    return false;
}

class HttpBodyReader {
public:
    HttpBodyReader(WiFiClientSecure &client, bool chunked,
                   bool has_content_length, uint64_t content_length,
                   const TransactionDeadline &deadline)
        : client_(client), chunked_(chunked),
          remaining_(has_content_length ? content_length : UINT64_MAX),
          deadline_(deadline)
    {
    }

    // 1 = byte, 0 = end, -1 = malformed or timed out.
    int read(uint8_t &value)
    {
        if (finished_) {
            return 0;
        }
        if (chunked_) {
            if (chunk_remaining_ == 0) {
                if (after_chunk_ && !consumeChunkTerminator()) {
                    return -1;
                }
                char line[48];
                if (!readMetadataLine(line, sizeof(line))) {
                    return -1;
                }
                char *end = nullptr;
                const unsigned long size = std::strtoul(line, &end, 16);
                if (end == line || (*end != '\0' && *end != ';')) {
                    return -1;
                }
                if (size == 0) {
                    do {
                        if (!readMetadataLine(line, sizeof(line))) {
                            return -1;
                        }
                    } while (line[0] != '\0');
                    finished_ = true;
                    return 0;
                }
                chunk_remaining_ = size;
                after_chunk_ = true;
            }
            const RawReadResult read = readRawByte(client_, value, deadline_);
            if (read != RawReadResult::Byte) {
                error_ = read == RawReadResult::Timeout
                             ? "transaction deadline exceeded"
                             : "chunked body was truncated";
                return -1;
            }
            --chunk_remaining_;
            return 1;
        }

        if (remaining_ == 0) {
            finished_ = true;
            return 0;
        }
        const RawReadResult read =
            readRawByte(client_, value, deadline_);
        if (read == RawReadResult::End && remaining_ == UINT64_MAX) {
            finished_ = true;
            return 0;
        }
        if (read != RawReadResult::Byte) {
            error_ = read == RawReadResult::Timeout
                         ? "transaction deadline exceeded"
                         : "HTTP body was truncated";
            return -1;
        }
        if (remaining_ != UINT64_MAX) {
            --remaining_;
        }
        return 1;
    }

    const char *error() const { return error_; }

private:
    bool readMetadataLine(char *line, size_t line_size)
    {
        if (++metadata_lines_ > kMaxChunkMetadataLines) {
            error_ = "too many chunk metadata lines";
            return false;
        }
        const LineReadResult read =
            readRawLine(client_, line, line_size, metadata_bytes_,
                        kMaxChunkMetadataBytes, deadline_);
        if (read != LineReadResult::Ok) {
            error_ = lineReadError(read);
            return false;
        }
        return true;
    }

    bool readMetadataByte(uint8_t &value)
    {
        if (++metadata_bytes_ > kMaxChunkMetadataBytes) {
            error_ = "chunk metadata exceeds byte limit";
            return false;
        }
        const RawReadResult read = readRawByte(client_, value, deadline_);
        if (read != RawReadResult::Byte) {
            error_ = read == RawReadResult::Timeout
                         ? "transaction deadline exceeded"
                         : "chunk metadata was truncated";
            return false;
        }
        return true;
    }

    bool consumeChunkTerminator()
    {
        uint8_t first = 0;
        if (!readMetadataByte(first)) {
            return false;
        }
        if (first == '\n') {
            return true;
        }
        uint8_t second = 0;
        if (first != '\r' || !readMetadataByte(second) || second != '\n') {
            error_ = "invalid chunk terminator";
            return false;
        }
        return true;
    }

    WiFiClientSecure &client_;
    bool chunked_ = false;
    uint64_t remaining_ = UINT64_MAX;
    const TransactionDeadline &deadline_;
    unsigned long chunk_remaining_ = 0;
    size_t metadata_bytes_ = 0;
    size_t metadata_lines_ = 0;
    bool after_chunk_ = false;
    bool finished_ = false;
    const char *error_ = "malformed HTTP body";
};

class ReplyJsonParser {
public:
    explicit ReplyJsonParser(OracleResult &result) : result_(result) {}

    void feed(uint8_t value)
    {
        switch (stage_) {
        case Stage::FindChoices:
            if (match("\"choices\"", value)) {
                stage_ = Stage::FindMessage;
            }
            break;
        case Stage::FindMessage:
            if (match("\"message\"", value)) {
                stage_ = Stage::FindContent;
            }
            break;
        case Stage::FindContent:
            if (match("\"content\"", value)) {
                stage_ = Stage::WaitColon;
            }
            break;
        case Stage::WaitColon:
            if (value == ':') {
                stage_ = Stage::WaitValue;
            } else if (!isJsonWhitespace(value)) {
                stage_ = Stage::Failed;
            }
            break;
        case Stage::WaitValue:
            if (value == '"') {
                stage_ = Stage::String;
            } else if (!isJsonWhitespace(value)) {
                stage_ = Stage::Failed;
            }
            break;
        case Stage::String:
            if (value == '"') {
                flushPendingSurrogate();
                finish();
            } else if (value == '\\') {
                stage_ = Stage::Escape;
            } else if (value >= 0x20U) {
                flushPendingSurrogate();
                appendByte(value);
            }
            break;
        case Stage::Escape:
            if (value == 'u') {
                unicode_value_ = 0;
                unicode_digits_ = 0;
                stage_ = Stage::Unicode;
            } else {
                flushPendingSurrogate();
                appendEscape(value);
                if (stage_ != Stage::Failed) {
                    stage_ = Stage::String;
                }
            }
            break;
        case Stage::Unicode: {
            const int digit = hexDigit(value);
            if (digit < 0) {
                stage_ = Stage::Failed;
                break;
            }
            unicode_value_ =
                static_cast<uint16_t>((unicode_value_ << 4) | digit);
            if (++unicode_digits_ == 4U) {
                appendCodeUnit(unicode_value_);
                stage_ = Stage::String;
            }
            break;
        }
        case Stage::Complete:
        case Stage::Failed:
            break;
        }
    }

    bool complete() const { return stage_ == Stage::Complete; }
    bool failed() const { return stage_ == Stage::Failed; }

private:
    enum class Stage {
        FindChoices,
        FindMessage,
        FindContent,
        WaitColon,
        WaitValue,
        String,
        Escape,
        Unicode,
        Complete,
        Failed,
    };

    static bool isJsonWhitespace(uint8_t value)
    {
        return value == ' ' || value == '\t' || value == '\r' ||
               value == '\n';
    }

    bool match(const char *pattern, uint8_t value)
    {
        if (value == static_cast<uint8_t>(pattern[pattern_index_])) {
            ++pattern_index_;
            if (pattern[pattern_index_] == '\0') {
                pattern_index_ = 0;
                return true;
            }
        } else {
            pattern_index_ =
                value == static_cast<uint8_t>(pattern[0]) ? 1U : 0U;
        }
        return false;
    }

    static int hexDigit(uint8_t value)
    {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    }

    void appendEscape(uint8_t value)
    {
        switch (value) {
        case '"': appendByte('"'); break;
        case '\\': appendByte('\\'); break;
        case '/': appendByte('/'); break;
        case 'n': appendByte('\n'); break;
        case 'r': appendByte('\n'); break;
        case 't': appendByte(' '); break;
        case 'b':
        case 'f':
            break;
        default:
            stage_ = Stage::Failed;
            break;
        }
    }

    void appendByte(uint8_t value)
    {
        if (used_ + 1U < sizeof(result_.text)) {
            result_.text[used_++] = static_cast<char>(value);
            result_.text[used_] = '\0';
        } else {
            result_.truncated = true;
        }
    }

    void appendUtf8(uint32_t codepoint)
    {
        uint8_t bytes[4];
        size_t count = 0;
        if (codepoint <= 0x7FU) {
            bytes[count++] = static_cast<uint8_t>(codepoint);
        } else if (codepoint <= 0x7FFU) {
            bytes[count++] = static_cast<uint8_t>(0xC0U | (codepoint >> 6));
            bytes[count++] = static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
        } else if (codepoint <= 0xFFFFU) {
            bytes[count++] = static_cast<uint8_t>(0xE0U | (codepoint >> 12));
            bytes[count++] =
                static_cast<uint8_t>(0x80U | ((codepoint >> 6) & 0x3FU));
            bytes[count++] = static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
        } else {
            bytes[count++] = static_cast<uint8_t>(0xF0U | (codepoint >> 18));
            bytes[count++] =
                static_cast<uint8_t>(0x80U | ((codepoint >> 12) & 0x3FU));
            bytes[count++] =
                static_cast<uint8_t>(0x80U | ((codepoint >> 6) & 0x3FU));
            bytes[count++] = static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
        }
        if (used_ + count < sizeof(result_.text)) {
            for (size_t index = 0; index < count; ++index) {
                result_.text[used_++] = static_cast<char>(bytes[index]);
            }
            result_.text[used_] = '\0';
        } else {
            result_.truncated = true;
        }
    }

    void appendCodeUnit(uint16_t unit)
    {
        if (pending_high_surrogate_ != 0) {
            if (unit >= 0xDC00U && unit <= 0xDFFFU) {
                const uint32_t codepoint =
                    0x10000U +
                    ((pending_high_surrogate_ - 0xD800U) << 10) +
                    (unit - 0xDC00U);
                pending_high_surrogate_ = 0;
                appendUtf8(codepoint);
                return;
            }
            appendUtf8(0xFFFDU);
            pending_high_surrogate_ = 0;
        }
        if (unit >= 0xD800U && unit <= 0xDBFFU) {
            pending_high_surrogate_ = unit;
        } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            appendUtf8(0xFFFDU);
        } else {
            appendUtf8(unit);
        }
    }

    void flushPendingSurrogate()
    {
        if (pending_high_surrogate_ != 0) {
            appendUtf8(0xFFFDU);
            pending_high_surrogate_ = 0;
        }
    }

    void finish()
    {
        trimIncompleteUtf8Tail();
        size_t first = 0;
        while (first < used_ &&
               std::isspace(static_cast<unsigned char>(result_.text[first]))) {
            ++first;
        }
        size_t last = used_;
        while (last > first && std::isspace(
                                   static_cast<unsigned char>(result_.text[last - 1U]))) {
            --last;
        }
        if (first != 0 && last > first) {
            std::memmove(result_.text, result_.text + first, last - first);
        }
        used_ = last - first;
        result_.text[used_] = '\0';
        stage_ = used_ == 0 ? Stage::Failed : Stage::Complete;
    }

    void trimIncompleteUtf8Tail()
    {
        if (used_ == 0) {
            return;
        }
        size_t continuation_start = used_;
        while (continuation_start > 0 &&
               (static_cast<uint8_t>(result_.text[continuation_start - 1U]) &
                0xC0U) == 0x80U) {
            --continuation_start;
        }
        if (continuation_start == 0) {
            used_ = 0;
            result_.text[0] = '\0';
            return;
        }
        const size_t lead = continuation_start == used_
                                ? used_ - 1U
                                : continuation_start - 1U;
        const uint8_t value = static_cast<uint8_t>(result_.text[lead]);
        size_t expected = 1;
        if ((value & 0xE0U) == 0xC0U) {
            expected = 2;
        } else if ((value & 0xF0U) == 0xE0U) {
            expected = 3;
        } else if ((value & 0xF8U) == 0xF0U) {
            expected = 4;
        }
        if (expected > 1U && used_ - lead < expected) {
            used_ = lead;
            result_.text[used_] = '\0';
        }
    }

    OracleResult &result_;
    Stage stage_ = Stage::FindChoices;
    size_t pattern_index_ = 0;
    size_t used_ = 0;
    uint16_t unicode_value_ = 0;
    uint16_t pending_high_surrogate_ = 0;
    uint8_t unicode_digits_ = 0;
};

bool ensureWifi(OracleResult &result)
{
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    if (RIDDLE_WIFI_SSID[0] == '\0') {
        setError(result, 0,
                 "Wi-Fi is disconnected and RIDDLE_WIFI_SSID is empty");
        return false;
    }
    Serial.printf("[riddle/oracle] connecting Wi-Fi to %s\n",
                  RIDDLE_WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(RIDDLE_WIFI_SSID, RIDDLE_WIFI_PASSWORD);
    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - started < RIDDLE_WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (WiFi.status() != WL_CONNECTED) {
        setError(result, 0, "Wi-Fi connection timed out after %lu ms",
                 static_cast<unsigned long>(RIDDLE_WIFI_CONNECT_TIMEOUT_MS));
        return false;
    }
    Serial.printf("[riddle/oracle] Wi-Fi connected, RSSI=%d dBm\n",
                  WiFi.RSSI());
    return true;
}

bool ensureClockForTls(bool &clock_ready, OracleResult &result)
{
    if (clock_ready) {
        return true;
    }
    Serial.println("[riddle/oracle] synchronizing clock for TLS");
    configTime(0, 0, "pool.ntp.org", "time.cloudflare.com",
               "time.google.com");
    const uint32_t started = millis();
    while (std::time(nullptr) < kMinimumValidTime) {
        const uint32_t elapsed = millis() - started;
        if (elapsed >= RIDDLE_NTP_SYNC_TIMEOUT_MS) {
            setError(result, 0, "NTP synchronization timed out after %lu ms",
                     static_cast<unsigned long>(
                         RIDDLE_NTP_SYNC_TIMEOUT_MS));
            return false;
        }
        const uint32_t remaining = RIDDLE_NTP_SYNC_TIMEOUT_MS - elapsed;
        vTaskDelay(pdMS_TO_TICKS(remaining < 100U ? remaining : 100U));
    }
    clock_ready = true;
    Serial.println("[riddle/oracle] clock synchronized for TLS");
    return true;
}

bool makeRequest(WiFiClientSecure &client, const Endpoint &endpoint,
                 const ByteBuffer &png, OracleResult &result,
                 const TransactionDeadline &deadline)
{
    char escaped_model[160];
    if (!jsonEscape(RIDDLE_OPENAI_MODEL, escaped_model,
                    sizeof(escaped_model))) {
        setError(result, 0, "model name is too long");
        return false;
    }
    char prefix[1200];
    const int prefix_length = std::snprintf(
        prefix, sizeof(prefix),
        "{\"model\":\"%s\",\"stream\":false,\"max_tokens\":%u,"
        "\"messages\":[{\"role\":\"system\",\"content\":\"%s\"},"
        "{\"role\":\"user\",\"content\":[{\"type\":\"text\","
        "\"text\":\"%s\"},{\"type\":\"image_url\",\"image_url\":"
        "{\"url\":\"data:image/png;base64,",
        escaped_model, static_cast<unsigned>(RIDDLE_OPENAI_MAX_TOKENS),
        kPersona, kUserInstruction);
    if (prefix_length < 0 ||
        static_cast<size_t>(prefix_length) >= sizeof(prefix)) {
        setError(result, 0, "request prefix is too large");
        return false;
    }
    if (png.size > (SIZE_MAX - 2U) / 4U * 3U) {
        setError(result, 0, "PNG is too large for base64");
        return false;
    }
    const size_t base64_length = ((png.size + 2U) / 3U) * 4U;
    if (base64_length > SIZE_MAX - static_cast<size_t>(prefix_length) -
                            std::strlen(kJsonSuffix)) {
        setError(result, 0, "request size overflow");
        return false;
    }
    const size_t content_length = static_cast<size_t>(prefix_length) +
                                  base64_length + std::strlen(kJsonSuffix);

    if (hasLineBreak(RIDDLE_OPENAI_API_KEY)) {
        setError(result, 0, "API key contains an invalid line break");
        return false;
    }
    char host_header[144];
    const int host_header_length = endpoint.port == 443
        ? std::snprintf(host_header, sizeof(host_header), "%s", endpoint.host)
        : std::snprintf(host_header, sizeof(host_header), "%s:%u",
                        endpoint.host, endpoint.port);
    if (host_header_length < 0 ||
        static_cast<size_t>(host_header_length) >= sizeof(host_header)) {
        setError(result, 0, "HTTP host header is too large");
        return false;
    }
    char header[768];
    const int header_length = std::snprintf(
        header, sizeof(header),
        "POST %s HTTP/1.1\r\nHost: %s\r\nAuthorization: Bearer %s\r\n"
        "Content-Type: application/json\r\nAccept: application/json\r\n"
        "Content-Length: %lu\r\nConnection: close\r\n\r\n",
        endpoint.path, host_header, RIDDLE_OPENAI_API_KEY,
        static_cast<unsigned long>(content_length));
    if (header_length < 0 ||
        static_cast<size_t>(header_length) >= sizeof(header)) {
        setError(result, 0, "HTTP request headers are too large");
        return false;
    }

    if (!writeAll(client, reinterpret_cast<const uint8_t *>(header),
                  static_cast<size_t>(header_length), deadline) ||
        !writeAll(client, reinterpret_cast<const uint8_t *>(prefix),
                  static_cast<size_t>(prefix_length), deadline) ||
        !writeBase64(client, png.data, png.size, deadline) ||
        !writeText(client, kJsonSuffix, deadline)) {
        setError(result, 0, deadline.expired()
                                ? "oracle transaction deadline exceeded"
                                : "request write failed");
        return false;
    }
    return true;
}

bool readResponse(WiFiClientSecure &client, OracleResult &result,
                  const TransactionDeadline &deadline)
{
    char line[kMaxHeaderLineBytes + 1U];
    size_t header_bytes = 0;
    size_t header_lines = 1;
    LineReadResult line_result =
        readRawLine(client, line, sizeof(line), header_bytes,
                    kMaxHeaderBytes, deadline);
    if (line_result != LineReadResult::Ok) {
        setError(result, 0, "HTTP status read failed: %s",
                 lineReadError(line_result));
        return false;
    }
    unsigned status = 0;
    if (std::sscanf(line, "HTTP/%*u.%*u %u", &status) != 1 ||
        status > 999U) {
        setError(result, 0, "invalid HTTP status line");
        return false;
    }

    bool chunked = false;
    bool has_content_length = false;
    uint64_t content_length = 0;
    bool unsupported_encoding = false;
    do {
        if (++header_lines > kMaxHeaderLines) {
            setError(result, static_cast<uint16_t>(status),
                     "HTTP response has too many header lines");
            return false;
        }
        line_result = readRawLine(client, line, sizeof(line), header_bytes,
                                  kMaxHeaderBytes, deadline);
        if (line_result != LineReadResult::Ok) {
            setError(result, static_cast<uint16_t>(status),
                     "HTTP header read failed: %s",
                     lineReadError(line_result));
            return false;
        }
        if (startsWithIgnoreCase(line, "Transfer-Encoding:")) {
            chunked = containsIgnoreCase(line, "chunked");
        } else if (startsWithIgnoreCase(line, "Content-Length:")) {
            const char *value = std::strchr(line, ':') + 1;
            char *end = nullptr;
            content_length = std::strtoull(value, &end, 10);
            has_content_length = end != value;
        } else if (startsWithIgnoreCase(line, "Content-Encoding:") &&
                   !containsIgnoreCase(line, "identity")) {
            unsupported_encoding = true;
        }
    } while (line[0] != '\0');

    if (unsupported_encoding) {
        setError(result, static_cast<uint16_t>(status),
                 "compressed HTTP responses are not supported");
        return false;
    }
    HttpBodyReader body(client, chunked, has_content_length, content_length,
                        deadline);
    if (status < 200U || status >= 300U) {
        result.http_status = static_cast<uint16_t>(status);
        const int prefix = std::snprintf(result.error, sizeof(result.error),
                                         "HTTP %u: ", status);
        size_t used = prefix > 0 ? static_cast<size_t>(prefix) : 0;
        if (used >= sizeof(result.error)) {
            used = sizeof(result.error) - 1U;
        }
        uint8_t value = 0;
        while (used + 1U < sizeof(result.error) && body.read(value) == 1) {
            result.error[used++] =
                value >= 0x20U && value != 0x7FU ? static_cast<char>(value)
                                                : ' ';
        }
        result.error[used] = '\0';
        Serial.printf("[riddle/oracle] HTTP error %u\n", status);
        return false;
    }

    result.http_status = static_cast<uint16_t>(status);
    ReplyJsonParser parser(result);
    size_t scanned = 0;
    while (!parser.complete() && !parser.failed() &&
           scanned < RIDDLE_ORACLE_RESPONSE_SCAN_BYTES) {
        uint8_t value = 0;
        const int read = body.read(value);
        if (read == 0) {
            break;
        }
        if (read < 0) {
            setError(result, static_cast<uint16_t>(status),
                     "%s", body.error());
            return false;
        }
        parser.feed(value);
        ++scanned;
    }
    if (!parser.complete()) {
        setError(result, static_cast<uint16_t>(status),
                 scanned >= RIDDLE_ORACLE_RESPONSE_SCAN_BYTES
                     ? "response exceeded scan limit"
                     : "choices[0].message.content was missing");
        return false;
    }
    result.ok = true;
    result.error[0] = '\0';
    return true;
}

} // namespace

bool OracleClient::begin()
{
    if (task_ != nullptr) {
        return true;
    }
    request_queue_ = xQueueCreate(1, sizeof(PageBitmap *));
    result_queue_ = xQueueCreate(1, sizeof(OracleResult));
    if (request_queue_ == nullptr || result_queue_ == nullptr) {
        if (request_queue_ != nullptr) {
            vQueueDelete(request_queue_);
        }
        if (result_queue_ != nullptr) {
            vQueueDelete(result_queue_);
        }
        request_queue_ = nullptr;
        result_queue_ = nullptr;
        Serial.println("[riddle/oracle] queue allocation failed");
        return false;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        taskEntry, "riddle_oracle", RIDDLE_ORACLE_TASK_STACK_BYTES, this,
        RIDDLE_ORACLE_TASK_PRIORITY, &task_, RIDDLE_ORACLE_TASK_CORE);
    if (created != pdPASS) {
        vQueueDelete(request_queue_);
        vQueueDelete(result_queue_);
        request_queue_ = nullptr;
        result_queue_ = nullptr;
        task_ = nullptr;
        Serial.println("[riddle/oracle] task creation failed");
        return false;
    }
    Serial.printf("[riddle/oracle] worker started (priority=%u, core=%d)\n",
                  static_cast<unsigned>(RIDDLE_ORACLE_TASK_PRIORITY),
                  static_cast<int>(RIDDLE_ORACLE_TASK_CORE));
    return true;
}

bool OracleClient::submit(PageBitmap *page)
{
    if (task_ == nullptr || page == nullptr || !page->valid() ||
        uxQueueMessagesWaiting(result_queue_) != 0) {
        return false;
    }
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return false;
    }
    // The worker publishes its result before releasing busy_. Recheck after
    // acquiring it so a completed result cannot race the first queue check.
    if (uxQueueMessagesWaiting(result_queue_) != 0) {
        busy_.store(false, std::memory_order_release);
        return false;
    }
    if (xQueueSend(request_queue_, &page, 0) != pdTRUE) {
        busy_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

bool OracleClient::poll(OracleResult &result)
{
    return result_queue_ != nullptr &&
           xQueueReceive(result_queue_, &result, 0) == pdTRUE;
}

bool OracleClient::busy() const
{
    return busy_.load(std::memory_order_acquire);
}

void OracleClient::taskEntry(void *argument)
{
    static_cast<OracleClient *>(argument)->taskLoop();
}

void OracleClient::taskLoop()
{
    while (true) {
        PageBitmap *page = nullptr;
        if (xQueueReceive(request_queue_, &page, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        OracleResult result;
        process(page, result);
        xQueueOverwrite(result_queue_, &result);
        busy_.store(false, std::memory_order_release);
    }
}

void OracleClient::process(PageBitmap *page, OracleResult &result)
{
    if (RIDDLE_OPENAI_API_KEY[0] == '\0') {
        delete page;
        result.ok = true;
        result.offline = true;
        std::strncpy(result.text, kOfflineReply, sizeof(result.text) - 1U);
        result.text[sizeof(result.text) - 1U] = '\0';
        Serial.println("[riddle/oracle] offline demo reply (API key not set)");
        return;
    }

    ByteBuffer png;
    char png_error[RIDDLE_ORACLE_ERROR_BYTES] = {};
    if (!encodePagePng(*page, png, png_error, sizeof(png_error))) {
        delete page;
        setError(result, 0, "PNG encode failed: %s", png_error);
        Serial.printf("[riddle/oracle] %s\n", result.error);
        return;
    }
    delete page;
    Serial.printf("[riddle/oracle] PNG ready: %u bytes\n",
                  static_cast<unsigned>(png.size));

    if (!ensureWifi(result)) {
        Serial.printf("[riddle/oracle] %s\n", result.error);
        return;
    }
    Endpoint endpoint;
    if (!parseEndpoint(RIDDLE_OPENAI_BASE_URL, endpoint, result.error,
                       sizeof(result.error))) {
        result.ok = false;
        Serial.printf("[riddle/oracle] %s\n", result.error);
        return;
    }

    if (RIDDLE_OPENAI_ROOT_CA[0] != '\0') {
        if (!ensureClockForTls(clock_ready_, result)) {
            Serial.printf("[riddle/oracle] %s\n", result.error);
            return;
        }
    }

    WiFiClientSecure client;
    if (RIDDLE_OPENAI_ROOT_CA[0] != '\0') {
        client.setCACert(RIDDLE_OPENAI_ROOT_CA);
    } else {
#if RIDDLE_OPENAI_INSECURE_TLS
        client.setInsecure();
        Serial.println("[riddle/oracle] warning: TLS certificate unchecked");
#else
        setError(result, 0, "RIDDLE_OPENAI_ROOT_CA is not configured");
        return;
#endif
    }

    const uint32_t started = millis();
    const TransactionDeadline deadline(RIDDLE_ORACLE_TOTAL_TIMEOUT_MS);
    uint32_t connect_budget = deadline.remainingMs();
    if (connect_budget > RIDDLE_ORACLE_CONNECT_TIMEOUT_MS) {
        connect_budget = RIDDLE_ORACLE_CONNECT_TIMEOUT_MS;
    }
    if (connect_budget == 0) {
        setError(result, 0, "oracle transaction deadline exceeded");
        return;
    }
    uint32_t handshake_seconds = (connect_budget + 999UL) / 1000UL;
    if (handshake_seconds == 0) {
        handshake_seconds = 1;
    }
    client.setHandshakeTimeout(handshake_seconds);
    Serial.printf("[riddle/oracle] POST https://%s:%u%s model=%s\n",
                  endpoint.host, endpoint.port, endpoint.path,
                  RIDDLE_OPENAI_MODEL);
    if (!client.connect(endpoint.host, endpoint.port, connect_budget)) {
        setError(result, 0, deadline.expired()
                                ? "oracle transaction deadline exceeded"
                                : "TLS connection failed or timed out");
        Serial.printf("[riddle/oracle] %s\n", result.error);
        return;
    }
    if (deadline.expired()) {
        client.stop();
        setError(result, 0, "oracle transaction deadline exceeded");
        return;
    }
    if (!makeRequest(client, endpoint, png, result, deadline)) {
        client.stop();
        Serial.printf("[riddle/oracle] request failed after %lu ms: %s\n",
                      static_cast<unsigned long>(millis() - started),
                      result.error);
        return;
    }
    if (!readResponse(client, result, deadline)) {
        client.stop();
        Serial.printf("[riddle/oracle] request failed after %lu ms: %s\n",
                      static_cast<unsigned long>(millis() - started),
                      result.error);
        return;
    }
    client.stop();
    Serial.printf("[riddle/oracle] reply ready in %lu ms (%u bytes%s)\n",
                  static_cast<unsigned long>(millis() - started),
                  static_cast<unsigned>(std::strlen(result.text)),
                  result.truncated ? ", truncated" : "");
}

} // namespace riddle
