#ifndef VINOX_SERVING_JSON_HPP
#define VINOX_SERVING_JSON_HPP

#include <cctype>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vinox::serving {

/**
 * @brief Categorizes primitive JSON data types.
 */
enum class JsonType { Null, Bool, Number, String, Array, Object };

/**
 * @brief Represents a parsed JSON value node.
 */
struct JsonValue {
    JsonType type{JsonType::Null};
    bool bool_value{false};
    double number_value{0.0};
    std::string string_value;
    std::vector<JsonValue> array_value;
    std::map<std::string, JsonValue> object_value;
};

/**
 * @brief Lightweight, zero-dependency C++20 JSON parser for VINOX model manifests.
 *
 * @note SCOPE BOUNDARY & SECURITY CONTRACT:
 * This parser is specifically scoped for local, trusted VINOX model-manifest parsing.
 * For future untrusted network, MCP JSON-RPC, or public API traffic (Phases 9/10),
 * an extended security-hardening pass (surrogate pairs, strict recursion limits, duplicate key policy)
 * or a dedicated external library (e.g. nlohmann_json / simdjson) will be evaluated.
 */
class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input), pos_(0) {}

    static JsonValue parse(std::string_view input) {
        JsonParser parser(input);
        parser.skip_whitespace();
        auto value = parser.parse_value();
        parser.skip_whitespace();
        if (parser.pos_ < parser.input_.size()) {
            throw std::runtime_error("Unexpected characters after JSON root object");
        }
        return value;
    }

private:
    std::string_view input_;
    size_t pos_{0};

    void skip_whitespace() {
        while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n' || input_[pos_] == '\r')) {
            pos_++;
        }
    }

    char peek() {
        skip_whitespace();
        if (pos_ >= input_.size()) throw std::runtime_error("Unexpected end of JSON input");
        return input_[pos_];
    }

    char get() {
        char c = peek();
        pos_++;
        return c;
    }

    JsonValue parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        throw std::runtime_error(std::string("Unexpected token in JSON: ") + c);
    }

    JsonValue parse_object() {
        get(); // '{'
        JsonValue val;
        val.type = JsonType::Object;
        skip_whitespace();
        if (pos_ < input_.size() && input_[pos_] == '}') {
            pos_++;
            return val;
        }

        while (pos_ < input_.size()) {
            skip_whitespace();
            if (peek() != '"') throw std::runtime_error("Expected string key in object");
            std::string key = parse_string().string_value;
            skip_whitespace();
            if (get() != ':') throw std::runtime_error("Expected ':' after key in object");
            val.object_value[key] = parse_value();
            skip_whitespace();
            char next = get();
            if (next == '}') break;
            if (next != ',') throw std::runtime_error("Expected ',' or '}' in object");
        }
        return val;
    }

    JsonValue parse_array() {
        get(); // '['
        JsonValue val;
        val.type = JsonType::Array;
        skip_whitespace();
        if (pos_ < input_.size() && input_[pos_] == ']') {
            pos_++;
            return val;
        }

        while (pos_ < input_.size()) {
            val.array_value.push_back(parse_value());
            skip_whitespace();
            char next = get();
            if (next == ']') break;
            if (next != ',') throw std::runtime_error("Expected ',' or ']' in array");
        }
        return val;
    }

    JsonValue parse_string() {
        get(); // '"'
        std::string res;
        while (pos_ < input_.size()) {
            char c = input_[pos_++];
            if (c == '"') {
                JsonValue val;
                val.type = JsonType::String;
                val.string_value = res;
                return val;
            }
            if (c == '\\') {
                if (pos_ >= input_.size()) throw std::runtime_error("Unterminated escape sequence");
                char esc = input_[pos_++];
                if (esc == '"') res += '"';
                else if (esc == '\\') res += '\\';
                else if (esc == '/') res += '/';
                else if (esc == 'b') res += '\b';
                else if (esc == 'f') res += '\f';
                else if (esc == 'n') res += '\n';
                else if (esc == 'r') res += '\r';
                else if (esc == 't') res += '\t';
                else if (esc == 'u') {
                    if (pos_ + 4 > input_.size()) throw std::runtime_error("Incomplete \\uXXXX escape sequence");
                    uint32_t codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        char hex_ch = input_[pos_++];
                        codepoint <<= 4;
                        if (hex_ch >= '0' && hex_ch <= '9') codepoint |= static_cast<uint32_t>(hex_ch - '0');
                        else if (hex_ch >= 'a' && hex_ch <= 'f') codepoint |= static_cast<uint32_t>(hex_ch - 'a' + 10);
                        else if (hex_ch >= 'A' && hex_ch <= 'F') codepoint |= static_cast<uint32_t>(hex_ch - 'A' + 10);
                        else throw std::runtime_error("Invalid hex digit in \\uXXXX escape sequence");
                    }
                    if (codepoint <= 0x7F) {
                        res += static_cast<char>(codepoint);
                    } else if (codepoint <= 0x7FF) {
                        res += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                        res += static_cast<char>(0x80 | (codepoint & 0x3F));
                    } else {
                        res += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                        res += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                        res += static_cast<char>(0x80 | (codepoint & 0x3F));
                    }
                } else {
                    throw std::runtime_error(std::string("Invalid escape sequence in JSON string: \\") + esc);
                }
            } else {
                res += c;
            }
        }
        throw std::runtime_error("Unterminated string literal");
    }

    JsonValue parse_number() {
        size_t start = pos_;
        if (input_[pos_] == '-') pos_++;
        while (pos_ < input_.size() && (std::isdigit(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '.' || input_[pos_] == 'e' || input_[pos_] == 'E' || input_[pos_] == '+' || input_[pos_] == '-')) {
            pos_++;
        }
        std::string num_str(input_.substr(start, pos_ - start));
        JsonValue val;
        val.type = JsonType::Number;
        val.number_value = std::stod(num_str);
        return val;
    }

    JsonValue parse_bool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            JsonValue val;
            val.type = JsonType::Bool;
            val.bool_value = true;
            return val;
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            JsonValue val;
            val.type = JsonType::Bool;
            val.bool_value = false;
            return val;
        }
        throw std::runtime_error("Invalid boolean literal");
    }

    JsonValue parse_null() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            JsonValue val;
            val.type = JsonType::Null;
            return val;
        }
        throw std::runtime_error("Invalid null literal");
    }
};

}  // namespace vinox::serving

#endif
