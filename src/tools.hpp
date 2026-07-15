#ifndef TOOLS_HPP
#define TOOLS_HPP

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <filesystem>
#include <QImage>
#include <QImageReader>

namespace HandWrite {

struct Color {
    unsigned char r, g, b, a;
    Color() : r(0), g(0), b(0), a(255) {}
    Color(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
        : r(r), g(g), b(b), a(a) {}
    std::tuple<int, int, int, int> toTuple() const { return {r, g, b, a}; }
    static Color fromTuple(const std::tuple<int, int, int, int>& t) {
        return Color(static_cast<unsigned char>(std::get<0>(t)),
                     static_cast<unsigned char>(std::get<1>(t)),
                     static_cast<unsigned char>(std::get<2>(t)),
                     static_cast<unsigned char>(std::get<3>(t)));
    }
};

struct PaperSize {
    int width, height;
    std::string name;
    PaperSize() : width(667), height(945), name("默认") {}
    PaperSize(int w, int h, const std::string& n) : width(w), height(h), name(n) {}
};

// 纸张纹理描述
struct PaperTextureInfo {
    std::string key;
    std::string displayName;
};

// 预设配置
struct PresetInfo {
    std::string name;
    std::string filePath;
};

class BasicTools {
public:
    BasicTools();

    // 字体颜色
    const std::map<std::string, Color>& fontColors() const { return m_fontColors; }
    Color getFontColor(const std::string& name) const;

    // 背景颜色
    const std::map<std::string, Color>& backgroundColors() const { return m_backgroundColors; }
    Color getBackgroundColor(const std::string& name) const;

    // 分辨率
    const std::map<std::string, int>& resolutionRates() const { return m_resolutionRates; }
    int getResolutionRate(const std::string& name) const;

    // 纸张尺寸
    const std::map<std::string, PaperSize>& paperSizes() const { return m_paperSizes; }
    PaperSize getPaperSize(const std::string& name) const;
    PaperSize getPaperSizeByDisplayName(const std::string& displayName) const;
    std::vector<std::string> getPaperSizeNames() const;

    // 纸张纹理
    const std::vector<PaperTextureInfo>& paperTextures() const { return m_paperTextures; }

    // TTF 字体
    std::vector<std::string> getTtfFileNames() const;
    std::vector<std::string> getTtfFilePaths() const;
    std::pair<std::vector<std::string>, std::vector<std::string>> getTtfFiles() const;
    void setTtfLibraryPath(const std::string& path);

private:
    std::map<std::string, Color> m_fontColors;
    std::map<std::string, Color> m_backgroundColors;
    std::map<std::string, int> m_resolutionRates;
    std::map<std::string, PaperSize> m_paperSizes;
    std::vector<PaperTextureInfo> m_paperTextures;
    std::string m_ttfLibraryPath;

    void initializeColors();
    void initializeResolutionRates();
    void initializePaperSizes();
    void initializePaperTextures();
};

// 通用图片加载（含 WebP 回退）
QImage loadImageWithWebpFallback(const std::string& filePath);

} // namespace HandWrite
#endif // TOOLS_HPP
