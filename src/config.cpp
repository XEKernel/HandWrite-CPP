#include "config.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace HandWrite {

Config::Config(const std::string& path) { load(path); }

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: Configuration file not found at " << path << std::endl;
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = trim(line.substr(0, eqPos));
        std::string value = trim(line.substr(eqPos + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
            m_data[key] = value;
        } else if (value.size() >= 3 && value.front() == '[' && value.back() == ']') {
            std::string arrayContent = value.substr(1, value.size() - 2);
            std::stringstream ss(arrayContent);
            std::string item;
            std::vector<int> intArray;
            std::vector<double> doubleArray;
            bool isDouble = false;
            while (std::getline(ss, item, ',')) {
                item = trim(item);
                if (item.empty()) continue;
                try {
                    if (item.find('.') != std::string::npos) {
                        isDouble = true;
                        doubleArray.push_back(std::stod(item));
                    } else {
                        intArray.push_back(std::stoi(item));
                    }
                } catch (const std::exception&) { continue; }
            }
            m_data[key] = isDouble ? Value(doubleArray) : Value(intArray);
        } else if (value == "true" || value == "false") {
            m_data[key] = (value == "true") ? 1 : 0;
        } else if (value.find('.') != std::string::npos) {
            m_data[key] = std::stod(value);
        } else {
            try { m_data[key] = std::stoi(value); }
            catch (...) { m_data[key] = value; }
        }
    }
    return true;
}

bool Config::save(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) { std::cerr << "Error: Cannot write " << path << std::endl; return false; }
    file << "# HandWrite Configuration File\n\n";
    for (const auto& [key, value] : m_data) file << key << " = " << valueToString(value) << "\n";
    std::cout << "Configuration saved to " << path << std::endl;
    return true;
}

std::string Config::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return str.substr(start, str.find_last_not_of(" \t\r\n") - start + 1);
}

std::string Config::valueToString(const Value& value) const {
    if (std::holds_alternative<int>(value)) return std::to_string(std::get<int>(value));
    if (std::holds_alternative<double>(value)) {
        std::ostringstream oss; oss << std::get<double>(value); return oss.str();
    }
    if (std::holds_alternative<std::string>(value))
        return "\"" + std::get<std::string>(value) + "\"";
    if (std::holds_alternative<std::vector<int>>(value)) {
        const auto& a = std::get<std::vector<int>>(value);
        std::string r = "["; for (size_t i = 0; i < a.size(); ++i)
        { r += std::to_string(a[i]); if (i < a.size()-1) r += ", "; } r += "]"; return r;
    }
    if (std::holds_alternative<std::vector<double>>(value)) {
        const auto& a = std::get<std::vector<double>>(value);
        std::string r = "["; for (size_t i = 0; i < a.size(); ++i)
        { std::ostringstream oss; oss << a[i]; r += oss.str(); if (i < a.size()-1) r += ", "; } r += "]"; return r;
    }
    return "";
}

std::optional<int> Config::getInt(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end() && std::holds_alternative<int>(it->second)) return std::get<int>(it->second);
    return std::nullopt;
}
std::optional<double> Config::getDouble(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (std::holds_alternative<double>(it->second)) return std::get<double>(it->second);
        if (std::holds_alternative<int>(it->second)) return static_cast<double>(std::get<int>(it->second));
    }
    return std::nullopt;
}
std::optional<std::string> Config::getString(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end() && std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
    return std::nullopt;
}
std::optional<std::vector<int>> Config::getIntArray(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end() && std::holds_alternative<std::vector<int>>(it->second)) return std::get<std::vector<int>>(it->second);
    return std::nullopt;
}
std::optional<std::vector<double>> Config::getDoubleArray(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end() && std::holds_alternative<std::vector<double>>(it->second)) return std::get<std::vector<double>>(it->second);
    return std::nullopt;
}

void Config::set(const std::string& key, int v) { m_data[key] = v; }
void Config::set(const std::string& key, double v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::string& v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::vector<int>& v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::vector<double>& v) { m_data[key] = v; }
bool Config::has(const std::string& key) const { return m_data.find(key) != m_data.end(); }

// ---- Property accessors ----
std::optional<int> Config::width() const { return getInt("width"); }
void Config::setWidth(int v) { set("width", v); }
std::optional<int> Config::height() const { return getInt("height"); }
void Config::setHeight(int v) { set("height", v); }
std::optional<std::string> Config::paperTexture() const { return getString("paper_texture"); }
void Config::setPaperTexture(const std::string& v) { set("paper_texture", v); }
std::optional<double> Config::textureOpacity() const { return getDouble("texture_opacity"); }
void Config::setTextureOpacity(double v) { set("texture_opacity", v); }
std::optional<std::string> Config::backgroundImage() const { return getString("background_image"); }
void Config::setBackgroundImage(const std::string& v) { set("background_image", v); }
std::optional<std::string> Config::ttfSelector() const { return getString("ttf_selector"); }
void Config::setTtfSelector(const std::string& v) { set("ttf_selector", v); }
std::optional<int> Config::fontSize() const { return getInt("font_size"); }
void Config::setFontSize(int v) { set("font_size", v); }
std::optional<int> Config::lineSpacing() const { return getInt("line_spacing"); }
void Config::setLineSpacing(int v) { set("line_spacing", v); }
std::optional<int> Config::charDistance() const { return getInt("char_distance"); }
void Config::setCharDistance(int v) { set("char_distance", v); }
std::optional<int> Config::marginTop() const { return getInt("margin_top"); }
void Config::setMarginTop(int v) { set("margin_top", v); }
std::optional<int> Config::marginBottom() const { return getInt("margin_bottom"); }
void Config::setMarginBottom(int v) { set("margin_bottom", v); }
std::optional<int> Config::marginLeft() const { return getInt("margin_left"); }
void Config::setMarginLeft(int v) { set("margin_left", v); }
std::optional<int> Config::marginRight() const { return getInt("margin_right"); }
void Config::setMarginRight(int v) { set("margin_right", v); }
std::optional<bool> Config::paragraphIndent() const {
    auto v = getInt("paragraph_indent"); return v.has_value() ? std::optional<bool>(*v != 0) : std::nullopt; }
void Config::setParagraphIndent(bool v) { set("paragraph_indent", v ? 1 : 0); }
std::optional<int> Config::paragraphSpacing() const { return getInt("paragraph_spacing"); }
void Config::setParagraphSpacing(int v) { set("paragraph_spacing", v); }
std::optional<std::vector<int>> Config::charColor() const { return getIntArray("char_color"); }
void Config::setCharColor(const std::vector<int>& v) { set("char_color", v); }
std::optional<std::vector<int>> Config::backgroundColor() const { return getIntArray("background_color"); }
void Config::setBackgroundColor(const std::vector<int>& v) { set("background_color", v); }
std::optional<int> Config::resolution() const { return getInt("resolution"); }
void Config::setResolution(int v) { set("resolution", v); }
std::optional<double> Config::lineSpacingSigma() const { return getDouble("line_spacing_sigma"); }
void Config::setLineSpacingSigma(double v) { set("line_spacing_sigma", v); }
std::optional<double> Config::fontSizeSigma() const { return getDouble("font_size_sigma"); }
void Config::setFontSizeSigma(double v) { set("font_size_sigma", v); }
std::optional<double> Config::wordSpacingSigma() const { return getDouble("word_spacing_sigma"); }
void Config::setWordSpacingSigma(double v) { set("word_spacing_sigma", v); }
std::optional<double> Config::perturbXSigma() const { return getDouble("perturb_x_sigma"); }
void Config::setPerturbXSigma(double v) { set("perturb_x_sigma", v); }
std::optional<double> Config::perturbYSigma() const { return getDouble("perturb_y_sigma"); }
void Config::setPerturbYSigma(double v) { set("perturb_y_sigma", v); }
std::optional<double> Config::perturbThetaSigma() const { return getDouble("perturb_theta_sigma"); }
void Config::setPerturbThetaSigma(double v) { set("perturb_theta_sigma", v); }
std::optional<double> Config::strokeWidthSigma() const { return getDouble("stroke_width_sigma"); }
void Config::setStrokeWidthSigma(double v) { set("stroke_width_sigma", v); }
std::optional<bool> Config::inkBleed() const {
    auto v = getInt("ink_bleed"); return v.has_value() ? std::optional<bool>(*v != 0) : std::nullopt; }
void Config::setInkBleed(bool v) { set("ink_bleed", v ? 1 : 0); }
std::optional<double> Config::inkBleedRadius() const { return getDouble("ink_bleed_radius"); }
void Config::setInkBleedRadius(double v) { set("ink_bleed_radius", v); }
std::optional<double> Config::strikeThroughRate() const { return getDouble("strikethrough_rate"); }
void Config::setStrikeThroughRate(double v) { set("strikethrough_rate", v); }

} // namespace HandWrite
