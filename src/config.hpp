#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <optional>
#include <map>
#include <variant>
#include <vector>

namespace HandWrite {

class Config {
public:
    using Value = std::variant<int, double, std::string,
                               std::vector<int>, std::vector<double>,
                               std::vector<std::string>>;

    Config() = default;
    explicit Config(const std::string& path);

    bool load(const std::string& path);
    bool save(const std::string& path);

    std::optional<int> getInt(const std::string& key) const;
    std::optional<double> getDouble(const std::string& key) const;
    std::optional<std::string> getString(const std::string& key) const;
    std::optional<std::vector<int>> getIntArray(const std::string& key) const;
    std::optional<std::vector<double>> getDoubleArray(const std::string& key) const;
    std::optional<std::vector<std::string>> getStringArray(const std::string& key) const;

    void set(const std::string& key, int value);
    void set(const std::string& key, double value);
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const std::vector<int>& value);
    void set(const std::string& key, const std::vector<double>& value);
    void set(const std::string& key, const std::vector<std::string>& value);

    bool has(const std::string& key) const;

    // 纸张
    std::optional<int> width() const; void setWidth(int v);
    std::optional<int> height() const; void setHeight(int v);
    std::optional<std::string> paperTexture() const; void setPaperTexture(const std::string& v);
    std::optional<double> textureOpacity() const; void setTextureOpacity(double v);
    std::optional<std::string> backgroundImage() const; void setBackgroundImage(const std::string& v);

    // 字体
    std::optional<std::string> ttfSelector() const; void setTtfSelector(const std::string& v);
    std::optional<int> fontSize() const; void setFontSize(int v);
    std::optional<std::vector<std::string>> fontMixList() const; void setFontMixList(const std::vector<std::string>& v);
    std::optional<double> fontMixRate() const; void setFontMixRate(double v);

    // 排版
    std::optional<int> lineSpacing() const; void setLineSpacing(int v);
    std::optional<int> charDistance() const; void setCharDistance(int v);
    std::optional<int> marginTop() const; void setMarginTop(int v);
    std::optional<int> marginBottom() const; void setMarginBottom(int v);
    std::optional<int> marginLeft() const; void setMarginLeft(int v);
    std::optional<int> marginRight() const; void setMarginRight(int v);
    std::optional<bool> paragraphIndent() const; void setParagraphIndent(bool v);
    std::optional<int> paragraphSpacing() const; void setParagraphSpacing(int v);
    std::optional<int> textDirection() const; void setTextDirection(int v);
    std::optional<int> textWarp() const; void setTextWarp(int v);
    std::optional<double> textWarpStrength() const; void setTextWarpStrength(double v);

    // 颜色
    std::optional<std::vector<int>> charColor() const; void setCharColor(const std::vector<int>& v);
    std::optional<std::vector<int>> backgroundColor() const; void setBackgroundColor(const std::vector<int>& v);

    // 分辨率
    std::optional<int> resolution() const; void setResolution(int v);

    // 扰动
    std::optional<double> lineSpacingSigma() const; void setLineSpacingSigma(double v);
    std::optional<double> fontSizeSigma() const; void setFontSizeSigma(double v);
    std::optional<double> wordSpacingSigma() const; void setWordSpacingSigma(double v);
    std::optional<double> perturbXSigma() const; void setPerturbXSigma(double v);
    std::optional<double> perturbYSigma() const; void setPerturbYSigma(double v);
    std::optional<double> perturbThetaSigma() const; void setPerturbThetaSigma(double v);
    std::optional<double> strokeWidthSigma() const; void setStrokeWidthSigma(double v);

    // 效果
    std::optional<bool> inkBleed() const; void setInkBleed(bool v);
    std::optional<double> inkBleedRadius() const; void setInkBleedRadius(double v);
    std::optional<double> strikeThroughRate() const; void setStrikeThroughRate(double v);
    std::optional<bool> preserveChinesePunctuation() const; void setPreserveChinesePunctuation(bool v);

    // 背景图片锚点校准（点坐标按 x,y 顺序平铺存放）
    std::optional<bool> bgCalibEnabled() const; void setBgCalibEnabled(bool v);
    std::optional<int> bgCalibRows() const; void setBgCalibRows(int v);
    std::optional<int> bgCalibCols() const; void setBgCalibCols(int v);
    std::optional<std::vector<double>> bgCalibPoints() const; void setBgCalibPoints(const std::vector<double>& v);

    // 字符级覆盖：每条一个字符串（CSV 编码，见 mainwindow.cpp 的序列化）
    std::optional<std::vector<std::string>> charOverrides() const;
    void setCharOverrides(const std::vector<std::string>& v);

    // 复现种子（0 = 随机）
    std::optional<unsigned int> seed() const; void setSeed(unsigned int v);

private:
    std::map<std::string, Value> m_data;

    std::string trim(const std::string& str) const;
    std::string valueToString(const Value& value) const;
};

} // namespace HandWrite
#endif // CONFIG_HPP
