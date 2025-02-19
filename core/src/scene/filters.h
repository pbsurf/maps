#pragma once

#include "util/variant.h"

#include <memory>
#include <vector>

namespace Tangram {

class StyleContext;
struct Feature;

enum class FilterKeyword : uint8_t {
    undefined,
    zoom,
    geometry,
    meters_per_pixel,
    latitude,
    longitude
};

FilterKeyword stringToFilterKeyword(const std::string& _key);

std::string filterKeywordToString(FilterKeyword keyword);

struct Filter {
    struct OperatorAll {
        std::vector<Filter> operands;
    };
    struct OperatorAny {
        std::vector<Filter> operands;
    };
    struct OperatorNone {
        std::vector<Filter> operands;
    };

    struct EqualitySet {
        std::string key;
        std::vector<Value> values;
        FilterKeyword keyword;
    };
    struct Equality {
        std::string key;
        Value value;
        FilterKeyword keyword;
    };
    struct Range {
        std::string key;
        float min;
        float max;
        FilterKeyword keyword;
        bool hasPixelArea;
    };
    struct Existence {
        std::string key;
        bool exists;
    };
    struct Function {
        uint32_t id;
    };
    struct Boolean {
        bool value;
    };
    using Data = variant<none_type,
                         OperatorAll,
                         OperatorNone,
                         OperatorAny,
                         EqualitySet,
                         Equality,
                         Range,
                         Existence,
                         Function,
                         Boolean>;
    Data data;

    Filter() : data(none_type{}) {}
    Filter(Data _data) : data(std::move(_data)) {}

    bool eval(const Feature& feat, StyleContext& ctx) const;

    // Create an 'any', 'all', or 'none' filter
    inline static Filter MatchAny(std::vector<Filter> filters) {
        sort(filters);
        return { OperatorAny{ std::move(filters) }};
    }
    inline static Filter MatchAll(std::vector<Filter> filters) {
        sort(filters);
        return { OperatorAll{ std::move(filters) }};
    }
    inline static Filter MatchNone(std::vector<Filter> filters) {
        sort(filters);
        return { OperatorNone{ std::move(filters) }};
    }
    // Create an 'equality' filter
    inline static Filter MatchEquality(const std::string& k, const std::vector<Value>& vals) {
        if (vals.size() == 1) {
            return { Equality{k, vals[0], stringToFilterKeyword(k) }};
        } else {
            return { EqualitySet{k, vals, stringToFilterKeyword(k) }};
        }
    }
    // Create a 'range' filter
    inline static Filter MatchRange(const std::string& k, float min, float max, bool sqA) {
        return { Range{k, min, max, stringToFilterKeyword(k), sqA }};
    }
    // Create an 'existence' filter
    inline static Filter MatchExistence(const std::string& k, bool ex) {
        return { Existence{ k, ex }};
    }
    // Create an 'function' filter with reference to Scene function id
    inline static Filter MatchFunction(uint32_t id) {
        return { Function{ id }};
    }
    // Create a fixed value boolean filter (to support global variables in filter block)
    inline static Filter BooleanValue(bool val) {
        return { Boolean{ val }};
    }

    /* Public for testing */
    static void sort(std::vector<Filter>& filters);
    void print(int _indent = 0) const;
    int filterCost() const;
    bool isOperator() const;
    const std::string& key() const;
    const std::vector<Filter>& operands() const;

    bool isValid() const { return !data.is<none_type>(); }
    operator bool() const { return isValid(); }
};
}
