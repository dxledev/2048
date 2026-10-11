/**
 * @file color.cpp
 * @brief Palette color parsing, formatting, and contrast helpers.
 *
 * Eight-digit hex values always follow Qt's #AARRGGBB order, so "#e00e1e36" is
 * the RGB color #0e1e36 at alpha 0xe0. Removing alpha never touches the RGB
 * channels, which keeps translucent and opaque renderings of a palette aligned.
 */
#include "color.h"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace g2048 {
namespace {

[[noreturn]] void invalid(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

double channel(const QString &expression) {
    const auto parts = expression.trimmed().split('/');
    if (parts.size() > 2)
        invalid("Unsupported color channel: " + expression.trimmed());
    bool valid = false;
    double value = parts[0].trimmed().toDouble(&valid);
    if (!valid)
        invalid("Invalid color channel: " + expression.trimmed());
    if (parts.size() == 2) {
        bool denominatorValid = false;
        const double denominator = parts[1].trimmed().toDouble(&denominatorValid);
        if (!denominatorValid || denominator <= 0)
            invalid("Invalid color channel divisor: " + expression.trimmed());
        value /= denominator;
    }
    if (!std::isfinite(value) || value < 0 || value > 1)
        invalid("rgba channels must be between 0 and 1");
    return value;
}

int byte(double value) {
    return std::clamp(int(std::lround(value * 255)), 0, 255);
}

}

QColor parseColor(const QString &input) {
    const QString text = input.trimmed();
    static const QRegularExpression hex("^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
    static const QRegularExpression rgba(R"(^(?:Qt\.)?rgba\(([^()]*)\)$)");
    if (const auto match = hex.match(text); match.hasMatch()) {
        QString digits = match.captured(1);
        if (digits.size() == 3)
            digits = QString("%1%1%2%2%3%3").arg(digits[0]).arg(digits[1]).arg(digits[2]);
        const uint value = digits.toUInt(nullptr, 16);
        if (digits.size() == 6)
            return QColor(QRgb(0xff000000u | value));
        return QColor::fromRgba(QRgb(value));
    }
    if (const auto match = rgba.match(text); match.hasMatch()) {
        const auto channels = match.captured(1).split(',');
        if (channels.size() != 4)
            invalid("rgba() needs four channels");
        return QColor(byte(channel(channels[0])), byte(channel(channels[1])),
                      byte(channel(channels[2])), byte(channel(channels[3])));
    }
    invalid("Unsupported color \"" + text + "\". Use #RRGGBB, #AARRGGBB, or Qt.rgba(r, g, b, a)");
}

QString formatColor(const QColor &color) {
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QColor opaque(const QColor &color) {
    return QColor(color.red(), color.green(), color.blue());
}

QColor mix(const QColor &base, const QColor &tint, double amount) {
    const auto blend = [amount](int from, int to) { return int(std::lround(from + (to - from) * amount)); };
    return QColor(blend(base.red(), tint.red()), blend(base.green(), tint.green()), blend(base.blue(), tint.blue()));
}

double luminance(const QColor &color) {
    const auto linear = [](double component) {
        return component <= .04045 ? component / 12.92 : std::pow((component + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

double contrast(const QColor &first, const QColor &second) {
    const double a = luminance(first), b = luminance(second);
    return (std::max(a, b) + .05) / (std::min(a, b) + .05);
}

int distance(const QColor &first, const QColor &second) {
    return std::max({std::abs(first.red() - second.red()), std::abs(first.green() - second.green()),
                     std::abs(first.blue() - second.blue())});
}

}
