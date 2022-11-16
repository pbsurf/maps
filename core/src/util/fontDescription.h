#pragma once

#include <string>

struct FontDescription {
    std::string uri;
    std::string alias;

    FontDescription(std::string family, std::string style, std::string weight, std::string uri)
        : uri(uri) {
        alias = Alias(family, style, weight);
    }

    static std::string Alias(const std::string& family, const std::string& style, const std::string& weight) {
        return family + "_" + getNumericFontWeight(weight) + "_" + style;
    }

    static std::string getNumericFontWeight(const std::string& weight) {
        if (weight == "normal") { return "400"; }
        if (weight == "bold") { return "700"; }
        return weight;
    }
};
