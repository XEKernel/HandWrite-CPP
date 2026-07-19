#include "tools.hpp"
#include <iostream>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QBuffer>

namespace HandWrite {

BasicTools::BasicTools() {
    initializeColors();
    initializeResolutionRates();
    initializePaperSizes();
    initializePaperTextures();
    m_ttfLibraryPath = "ttf_library";
}

void BasicTools::initializeColors() {
    // Font colors (matching Python version)
    m_fontColors = {
        {"black", Color(0, 0, 0, 255)},
        {"white", Color(255, 255, 255, 255)},
        {"red", Color(255, 0, 0, 255)},
        {"blue", Color(0, 0, 255, 255)}
    };
    
    // Background colors - extended with common paper colors
    m_backgroundColors = {
        {"transparent", Color(0, 0, 0, 0)},
        {"white", Color(255, 255, 255, 255)},
        {"cream", Color(255, 253, 248, 255)},           // 米白色
        {"ivory", Color(255, 255, 240, 255)},           // 象牙色
        {"light_yellow", Color(255, 255, 224, 255)},    // 淡黄色
        {"yellow", Color(255, 250, 205, 255)},          // 柠檬黄
        {"light_blue", Color(240, 248, 255, 255)},      // 淡蓝色
        {"azure", Color(240, 255, 255, 255)},           // 天蓝色
        {"light_pink", Color(255, 245, 245, 255)},      // 淡粉色
        {"pink", Color(255, 228, 225, 255)},            // 粉红色
        {"light_gray", Color(245, 245, 245, 255)},      // 淡灰色
        {"gray", Color(220, 220, 220, 255)},            // 灰色
        {"light_green", Color(240, 255, 240, 255)},     // 淡绿色
        {"mint", Color(245, 255, 250, 255)}             // 薄荷色
    };
}

void BasicTools::initializeResolutionRates() {
    // Resolution multipliers (matching Python version)
    m_resolutionRates = {
        {"x1", 1},
        {"x2", 2},
        {"x4", 4},
        {"x8", 8},
        {"x16", 16},
        {"x32", 32},
        {"x64", 64}
    };
}

void BasicTools::initializePaperSizes() {
    // Common paper size templates (in pixels at 96 DPI)
    m_paperSizes = {
        {"default", PaperSize(667, 945, "默认")},
        {"a4_portrait", PaperSize(794, 1123, "A4 竖版")},
        {"a4_landscape", PaperSize(1123, 794, "A4 横版")},
        {"a5_portrait", PaperSize(559, 794, "A5 竖版")},
        {"a5_landscape", PaperSize(794, 559, "A5 横版")},
        {"letter_portrait", PaperSize(816, 1056, "Letter 竖版")},
        {"letter_landscape", PaperSize(1056, 816, "Letter 横版")},
        {"b5_portrait", PaperSize(701, 992, "B5 竖版")},
        {"b5_landscape", PaperSize(992, 701, "B5 横版")},
        {"16k_portrait", PaperSize(580, 820, "16K 竖版")},
        {"16k_landscape", PaperSize(820, 580, "16K 横版")},
        {"custom", PaperSize(667, 945, "自定义")}
    };
}

PaperSize BasicTools::getPaperSize(const std::string& name) const {
    auto it = m_paperSizes.find(name);
    if (it != m_paperSizes.end()) {
        return it->second;
    }
    return PaperSize(667, 945, "默认"); // Default
}

PaperSize BasicTools::getPaperSizeByDisplayName(const std::string& displayName) const {
    for (const auto& [key, size] : m_paperSizes) {
        if (size.name == displayName) {
            return size;
        }
    }
    return PaperSize(667, 945, "默认"); // Default
}

std::vector<std::string> BasicTools::getPaperSizeNames() const {
    std::vector<std::string> names;
    for (const auto& [key, size] : m_paperSizes) {
        names.push_back(size.name);
    }
    return names;
}

Color BasicTools::getFontColor(const std::string& name) const {
    auto it = m_fontColors.find(name);
    if (it != m_fontColors.end()) {
        return it->second;
    }
    return Color(0, 0, 0, 255); // Default black
}

Color BasicTools::getBackgroundColor(const std::string& name) const {
    auto it = m_backgroundColors.find(name);
    if (it != m_backgroundColors.end()) {
        return it->second;
    }
    return Color(0, 0, 0, 0); // Default transparent
}

int BasicTools::getResolutionRate(const std::string& name) const {
    auto it = m_resolutionRates.find(name);
    if (it != m_resolutionRates.end()) {
        return it->second;
    }
    return 4; // Default x4
}

void BasicTools::setTtfLibraryPath(const std::string& path) {
    m_ttfLibraryPath = path;
}

std::vector<std::string> BasicTools::getTtfFileNames() const {
    std::vector<std::string> names;
    
    try {
        if (!std::filesystem::exists(m_ttfLibraryPath)) {
            std::cerr << "Warning: TTF library path does not exist: " << m_ttfLibraryPath << std::endl;
            return names;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(m_ttfLibraryPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                // 转换为小写进行比较
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (ext == ".ttf") {
                    std::string name = entry.path().stem().string();
                    names.push_back(name);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing TTF library: " << e.what() << std::endl;
    }
    
    return names;
}

std::vector<std::string> BasicTools::getTtfFilePaths() const {
    std::vector<std::string> paths;
    
    try {
        if (!std::filesystem::exists(m_ttfLibraryPath)) {
            std::cerr << "Warning: TTF library path does not exist: " << m_ttfLibraryPath << std::endl;
            return paths;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(m_ttfLibraryPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (ext == ".ttf") {
                    paths.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing TTF library: " << e.what() << std::endl;
    }
    
    return paths;
}

void BasicTools::initializePaperTextures() {
    m_paperTextures = {
        {"none", "无纹理"},
        {"horizontal_line", "横线纸"},
        {"grid", "方格纸"},
        {"tianzige", "田字格"},
        {"composition", "作文纸"},
        {"dot_grid", "点阵纸"}
    };
}

std::pair<std::vector<std::string>, std::vector<std::string>> BasicTools::getTtfFiles() const {
    std::vector<std::string> names;
    std::vector<std::string> paths;
    
    try {
        if (!std::filesystem::exists(m_ttfLibraryPath)) {
            return {names, paths};
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(m_ttfLibraryPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return std::tolower(c); });
                if (ext == ".ttf") {
                    names.push_back(entry.path().stem().string());
                    paths.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error accessing TTF library: " << e.what() << std::endl;
    }
    
    return {names, paths};
}

//=============================================================================
// 通用图片加载（QImageReader → WebP 回退）
//=============================================================================

#include <webp/decode.h>

QImage loadImageWithWebpFallback(const std::string& filePath) {
    QString qPath = QString::fromStdString(filePath);
    
    // 1. 尝试 QImageReader
    QImageReader reader(qPath);
    reader.setAutoTransform(true);
    if (reader.canRead()) {
        QImage img = reader.read();
        if (!img.isNull() && img.width() > 0) return img;
    }
    
    // 2. WebP 回退（MSYS2 Qt6 无 qwebp 插件，用 libwebp 解码）
    QString ext = QFileInfo(qPath).suffix().toLower();
    if (ext == "webp") {
        QFile file(qPath);
        if (!file.open(QIODevice::ReadOnly)) return QImage();
        QByteArray data = file.readAll();
        file.close();
        
        int w = 0, h = 0;
        if (!WebPGetInfo(reinterpret_cast<const uint8_t*>(data.data()), data.size(), &w, &h))
            return QImage();
        
        QImage img(w, h, QImage::Format_ARGB32);
        // ARGB32 在小端序下的内存布局 = [B,G,R,A]，与 WebPDecodeBGRAInto 输出一致
        uint8_t* dst = WebPDecodeBGRAInto(
            reinterpret_cast<const uint8_t*>(data.data()), data.size(),
            reinterpret_cast<uint8_t*>(img.bits()), img.sizeInBytes(), img.bytesPerLine());
        if (!dst) return QImage();
        return img;
    }
    
    return QImage();
}

} // namespace HandWrite
