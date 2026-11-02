#pragma once
// 模块自己的 config.ini 读写（位于宿主分配的 module_dir）。
//
// 只支持 `key = value` 与 # / ; 注释；未知键忽略并计数，用户写错键名时
// 能在日志里看到，而不是静默失效。

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fgb::kit {

class ConfigIni {
public:
    // 返回 false 只表示文件不存在（首次运行属正常），解析错误会带在 error 里。
    bool Load(const std::string& directory, std::string_view filename, std::string& error) {
        if (directory.empty()) {
            return false;
        }
        const std::filesystem::path path =
            std::filesystem::path{directory} / std::string{filename};
        std::ifstream stream{path};
        if (!stream) {
            return false;
        }
        std::string line;
        std::size_t number = 0;
        while (std::getline(stream, line)) {
            ++number;
            ParseLine(line, error);
        }
        loaded_ = true;
        return true;
    }

    bool Loaded() const { return loaded_; }
    std::size_t Size() const { return values_.size(); }
    const std::vector<std::string>& UnknownKeys() const { return unknown_keys_; }

    void AllowKeys(std::vector<std::string> keys) { known_keys_ = std::move(keys); }

    std::string Get(std::string_view key, std::string_view fallback) const {
        const auto found = values_.find(std::string{key});
        if (found == values_.end() || found->second.empty()) {
            return std::string{fallback};
        }
        return found->second;
    }

    int GetInt(std::string_view key, int fallback) const {
        const auto found = values_.find(std::string{key});
        if (found == values_.end()) {
            return fallback;
        }
        try {
            return std::stoi(found->second);
        } catch (const std::exception&) {
            return fallback;
        }
    }

    double GetDouble(std::string_view key, double fallback) const {
        const auto found = values_.find(std::string{key});
        if (found == values_.end()) {
            return fallback;
        }
        try {
            return std::stod(found->second);
        } catch (const std::exception&) {
            return fallback;
        }
    }

    bool GetBool(std::string_view key, bool fallback) const {
        const auto found = values_.find(std::string{key});
        if (found == values_.end()) {
            return fallback;
        }
        const std::string& raw = found->second;
        if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") {
            return true;
        }
        if (raw == "0" || raw == "false" || raw == "no" || raw == "off") {
            return false;
        }
        return fallback;
    }

private:
    static std::string Trim(std::string_view text) {
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
            ++begin;
        }
        while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                               text[end - 1] == '\r')) {
            --end;
        }
        return std::string{text.substr(begin, end - begin)};
    }

    void ParseLine(const std::string& line, std::string& error) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '[') {
            return;
        }
        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            error = "config.ini 有无法解析的行：" + trimmed;
            return;
        }
        const std::string key = Trim(trimmed.substr(0, separator));
        std::string value = Trim(trimmed.substr(separator + 1));
        if (key.empty()) {
            return;
        }
        // 去掉行尾注释与包裹的引号
        const std::size_t comment = value.find_first_of(";#");
        if (comment != std::string::npos) {
            value = Trim(value.substr(0, comment));
        }
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
            value.front() == value.back()) {
            value = value.substr(1, value.size() - 2);
        }
        values_[key] = value;
        if (!known_keys_.empty() &&
            std::find(known_keys_.begin(), known_keys_.end(), key) == known_keys_.end()) {
            unknown_keys_.push_back(key);
        }
    }

    std::map<std::string, std::string> values_;
    std::vector<std::string> known_keys_;
    std::vector<std::string> unknown_keys_;
    bool loaded_ = false;
};

} // namespace fgb::kit
