#include "config.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <limits>

namespace HandWrite {

namespace {

std::string trimCopy(const std::string& str) {
    const size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return str.substr(start, str.find_last_not_of(" \t\r\n") - start + 1);
}

// 写文件时转义双引号与反斜杠
// 注意：注释行末不能出现反斜杠，否则会把下一行代码吞进注释（-Wcomment）
std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\')      out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else                out += c;
    }
    return out;
}

std::string unescapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += (n == 'n') ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

// 按 ',' 切分，忽略引号内的逗号
// （字符级覆盖的 CSV 编码本身含逗号，朴素切分会把它切碎）
std::vector<std::string> splitListItems(const std::string& content) {
    std::vector<std::string> items;
    std::string cur;
    bool inQuotes = false;
    for (char ch : content) {
        if (ch == '"') { inQuotes = !inQuotes; cur += ch; continue; }
        if (ch == ',' && !inQuotes) { items.push_back(trimCopy(cur)); cur.clear(); continue; }
        cur += ch;
    }
    items.push_back(trimCopy(cur));
    return items;
}

} // namespace

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
        const size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eqPos));
        const std::string value = trim(line.substr(eqPos + 1));
        if (key.empty()) continue;

        // 带引号的字符串
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            m_data[key] = unescapeString(value.substr(1, value.size() - 2));
            continue;
        }

        // 数组：可能是 int[]、double[] 或 string[]
        if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
            const std::vector<std::string> items = splitListItems(value.substr(1, value.size() - 2));
            std::vector<int> intArray;
            std::vector<double> doubleArray;
            std::vector<std::string> stringArray;
            bool anyDouble = false, anyString = false;
            for (const std::string& raw : items) {
                if (raw.empty()) continue;
                if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
                    anyString = true;
                    stringArray.push_back(unescapeString(raw.substr(1, raw.size() - 2)));
                    continue;
                }
                try {
                    const double d = std::stod(raw);
                    if (raw.find('.') != std::string::npos ||
                        raw.find('e') != std::string::npos ||
                        raw.find('E') != std::string::npos) anyDouble = true;
                    // 两种都存，最后由 anyDouble 决定用哪个：
                    // 旧实现遇到混合数组（[1, 2.5]）会把已解析的整数整批丢掉
                    doubleArray.push_back(d);
                    intArray.push_back(static_cast<int>(d));
                } catch (const std::exception&) {
                    anyString = true;
                    stringArray.push_back(raw);
                }
            }
            if (anyString)      m_data[key] = stringArray;
            else if (anyDouble) m_data[key] = doubleArray;
            else                m_data[key] = intArray;
            continue;
        }

        if (value == "true" || value == "false") {
            m_data[key] = (value == "true") ? 1 : 0;
            continue;
        }

        // 数字：必须「整串被消费」才算数字。
        // 旧实现直接用 std::stoi("0.35")：它只解析前导 "0" 就返回 0，且不抛异常，
        // 于是预设里所有小数（font_mix_rate=0.35、texture_opacity=0.3、
        // perturb_theta_sigma=0.05 …）在保存/加载往返中被静默截断成整数。
        {
            bool parsed = false;
            try {
                size_t pos = 0;
                const int iv = std::stoi(value, &pos);
                if (pos == value.size()) { m_data[key] = iv; parsed = true; }
            } catch (const std::exception&) {}
            if (!parsed) {
                try {
                    size_t pos = 0;
                    const double dv = std::stod(value, &pos);
                    if (pos == value.size()) { m_data[key] = dv; parsed = true; }
                } catch (const std::exception&) {}
            }
            if (parsed) continue;
        }
        m_data[key] = value;
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
        // precision 15：既能让 0.35 保持写成 "0.35"（默认格式会去掉尾随零），
        // 又能保证绝大多数小数往返不丢精度
        std::ostringstream oss;
        oss << std::setprecision(15) << std::get<double>(value);
        return oss.str();
    }
    if (std::holds_alternative<std::string>(value))
        return "\"" + escapeString(std::get<std::string>(value)) + "\"";
    if (std::holds_alternative<std::vector<int>>(value)) {
        const auto& a = std::get<std::vector<int>>(value);
        std::string r = "["; for (size_t i = 0; i < a.size(); ++i)
        { r += std::to_string(a[i]); if (i < a.size()-1) r += ", "; } r += "]"; return r;
    }
    if (std::holds_alternative<std::vector<double>>(value)) {
        const auto& a = std::get<std::vector<double>>(value);
        std::string r = "["; for (size_t i = 0; i < a.size(); ++i)
        { std::ostringstream oss; oss << std::setprecision(15) << a[i]; r += oss.str(); if (i < a.size()-1) r += ", "; } r += "]"; return r;
    }
    if (std::holds_alternative<std::vector<std::string>>(value)) {
        const auto& a = std::get<std::vector<std::string>>(value);
        std::string r = "["; for (size_t i = 0; i < a.size(); ++i)
        { r += "\"" + escapeString(a[i]) + "\""; if (i < a.size()-1) r += ", "; } r += "]"; return r;
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
std::optional<std::vector<std::string>> Config::getStringArray(const std::string& key) const {
    auto it = m_data.find(key);
    if (it != m_data.end() && std::holds_alternative<std::vector<std::string>>(it->second))
        return std::get<std::vector<std::string>>(it->second);
    return std::nullopt;
}

void Config::set(const std::string& key, int v) { m_data[key] = v; }
void Config::set(const std::string& key, double v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::string& v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::vector<int>& v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::vector<double>& v) { m_data[key] = v; }
void Config::set(const std::string& key, const std::vector<std::string>& v) { m_data[key] = v; }
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

// ---- 新增：排版方向 / 变形 / 标点 ----
std::optional<int> Config::textDirection() const { return getInt("text_direction"); }
void Config::setTextDirection(int v) { set("text_direction", v); }
std::optional<int> Config::textWarp() const { return getInt("text_warp"); }
void Config::setTextWarp(int v) { set("text_warp", v); }
std::optional<double> Config::textWarpStrength() const { return getDouble("text_warp_strength"); }
void Config::setTextWarpStrength(double v) { set("text_warp_strength", v); }
std::optional<bool> Config::preserveChinesePunctuation() const {
    auto v = getInt("preserve_chinese_punctuation");
    return v.has_value() ? std::optional<bool>(*v != 0) : std::nullopt; }
void Config::setPreserveChinesePunctuation(bool v) { set("preserve_chinese_punctuation", v ? 1 : 0); }

// ---- 新增：混合字体 ----
std::optional<std::vector<std::string>> Config::fontMixList() const { return getStringArray("font_mix_list"); }
void Config::setFontMixList(const std::vector<std::string>& v) { set("font_mix_list", v); }
std::optional<double> Config::fontMixRate() const { return getDouble("font_mix_rate"); }
void Config::setFontMixRate(double v) { set("font_mix_rate", v); }

// ---- 新增：背景图片锚点校准 ----
std::optional<bool> Config::bgCalibEnabled() const {
    auto v = getInt("bg_calib_enabled");
    return v.has_value() ? std::optional<bool>(*v != 0) : std::nullopt; }
void Config::setBgCalibEnabled(bool v) { set("bg_calib_enabled", v ? 1 : 0); }
std::optional<int> Config::bgCalibRows() const { return getInt("bg_calib_rows"); }
void Config::setBgCalibRows(int v) { set("bg_calib_rows", v); }
std::optional<int> Config::bgCalibCols() const { return getInt("bg_calib_cols"); }
void Config::setBgCalibCols(int v) { set("bg_calib_cols", v); }
std::optional<std::vector<double>> Config::bgCalibPoints() const { return getDoubleArray("bg_calib_points"); }
void Config::setBgCalibPoints(const std::vector<double>& v) { set("bg_calib_points", v); }

// ---- 新增：字符级覆盖 ----
std::optional<std::vector<std::string>> Config::charOverrides() const { return getStringArray("char_overrides"); }
void Config::setCharOverrides(const std::vector<std::string>& v) { set("char_overrides", v); }

// ---- 新增：复现种子 ----
std::optional<unsigned int> Config::seed() const {
    auto it = m_data.find("seed");
    if (it == m_data.end()) return std::nullopt;
    if (std::holds_alternative<int>(it->second))
        return static_cast<unsigned int>(std::get<int>(it->second));
    if (std::holds_alternative<double>(it->second))
        return static_cast<unsigned int>(std::get<double>(it->second));
    return std::nullopt;
}
void Config::setSeed(unsigned int v) {
    // 存为 int（GUI 生成的种子限制在 int 正区间内），超范围时退回 double
    if (v <= static_cast<unsigned int>(std::numeric_limits<int>::max()))
        set("seed", static_cast<int>(v));
    else
        set("seed", static_cast<double>(v));
}

} // namespace HandWrite
