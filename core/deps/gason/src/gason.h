#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#define ENABLE_YAML 1

#if ENABLE_YAML
namespace YAML {
#else
namespace JSON {
#endif

}

enum JsonTag {
    JSON_NULL = 0,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_TRUE,
    JSON_FALSE,
#if ENABLE_YAML
    YAML_COMMENT,
    YAML_SINGLEQUOTED,
    YAML_UNQUOTED,
#endif
};

struct JsonNode;

struct JsonValue {
    union {
      uint64_t ival_ = 0;
      double fval_;
    };
    uint8_t tag_ = JSON_NULL;
    // we'll need to add additional fields for YAML (flow style flag)

    JsonValue(double x) : fval_(x), tag_(JSON_NUMBER) {}
    JsonValue(JsonTag tag = JSON_NULL, void *payload = nullptr) : ival_((uintptr_t)payload), tag_(tag) {}

    bool isDouble() const { return tag_ == JSON_NUMBER; }
    JsonTag getTag() const { return (JsonTag)tag_; }
    uint64_t getPayload() const { assert(!isDouble()); return ival_; }
    double toNumber() const { assert(isDouble()); return fval_; }
    char* toString() const { assert(getTag() == JSON_STRING); return (char *)getPayload();  }
    JsonNode *toNode() const {
        assert(getTag() == JSON_ARRAY || getTag() == JSON_OBJECT);
        return (JsonNode *)getPayload();
    }

    operator bool() { return getTag() != JSON_NULL; }
    bool operator!() { return getTag() == JSON_NULL; }

    JsonValue& operator=(double x) { tag_ = JSON_NUMBER; fval_ = x; }
    JsonValue& operator=(const char* s) { tag_ = JSON_STRING; ival_ = (uint64_t)s; }

    template <typename T> T as(const T& _default);  // = {});

    //JsonIterator begin() { return JsonIterator(toNode()); }
    //JsonIterator end() { return JsonIterator(nullptr); }

    JsonValue& operator[](const char* key) {
        JsonNode* obj = getTag() == JSON_OBJECT ? toNode() : nullptr;
        while(obj && strcmp(obj->key, key) != 0) {

          if (!obj->next)
            obj->next = JsonNode(JsonValue(), nullptr, );


          obj = obj->next;
        }
        return obj ? obj->value : NULL_NODE;
    }
    JsonValue& operator[](const std::string& key) { return operator[](key.c_str()); }

    JsonValue& operator[](size_t idx) {
        JsonNode* array = getTag() == JSON_ARRAY ? toNode() : nullptr;
        while (array && idx--) { array = array->next; }
        return array ? array->value : NULL_NODE;
    }

    size_t size() {
        size_t n = 0;
        for (JsonNode* obj = toNode(); obj; obj = obj->next) { ++n; }
        return n;
    }

};


template<> inline int JsonValue::as(const int& _default) {
    return int(as<double>(double(_default)));
}

template<> inline float JsonValue::as(const float& _default) {
    return float(as<double>(double(_default)));
}

template<> inline double JsonValue::as(const double& _default) {
    if(isDouble()) { return toNumber(); }
    if(!isString()) { return _default; }
    char* endptr;
    char* s = toString();
    double val = s[0] == '0' ? strtoul(s, &endptr, 0) : string2double(s, &endptr);
    return *endptr ? _default : val;  //endptr - s == strlen(s) ? val : _default;
}


template<> inline std::string JsonValue::as(const std::string& _default) {
    if(isDouble()) { return std::to_string(toNumber()); }
    return isString() ? toString() : _default;
}

template<> inline bool JsonValue::as(const bool& _default) {
    // YAML 1.2 only allows true/false ... but if caller is asking for bool be flexible
    static const char* boolstrs[] = {"true","false","True","False","TRUE","FALSE",
        "y","n","Y","N","yes","no","Yes","No","YES","NO","on","off","On","Off","ON","OFF"};

    if(isDouble()) { return toNumber() != 0; }
    char* s = toString();
    int idx = 0;
    for (const char* boolstr : boolstrs) {
        if (strcmp(s, boolstr) == 0) { return !(idx%2); }
        ++idx;
    }
    return  _default;
}


static JsonNode NULL_NODE = {JsonValue(), nullptr, nullptr};

struct JsonNode {
    JsonValue value;
    JsonNode *next;
    char *key;
};

struct JsonIterator {
    JsonNode *p;

    void operator++() {
        p = p->next;
    }
    bool operator!=(const JsonIterator &x) const {
        return p != x.p;
    }
    JsonNode *operator*() const {
        return p;
    }
    JsonNode *operator->() const {
        return p;
    }
};

inline JsonIterator begin(JsonValue o) {
    return JsonIterator{o.toNode()};
}
inline JsonIterator end(JsonValue) {
    return JsonIterator{nullptr};
}

#define JSON_ERRNO_MAP(XX)                           \
    XX(OK, "ok")                                     \
    XX(BAD_NUMBER, "bad number")                     \
    XX(BAD_STRING, "bad string")                     \
    XX(BAD_IDENTIFIER, "bad identifier")             \
    XX(STACK_OVERFLOW, "stack overflow")             \
    XX(STACK_UNDERFLOW, "stack underflow")           \
    XX(MISMATCH_BRACKET, "mismatch bracket")         \
    XX(UNEXPECTED_CHARACTER, "unexpected character") \
    XX(UNQUOTED_KEY, "unquoted key")                 \
    XX(BREAKING_BAD, "breaking bad")                 \
    XX(ALLOCATION_FAILURE, "allocation failure")

enum JsonErrno {
#define XX(no, str) JSON_##no,
    JSON_ERRNO_MAP(XX)
#undef XX
};

const char *jsonStrError(int err);

class JsonAllocator {
    struct Zone {
        Zone *next;
        size_t used;
    } *head;

public:
    JsonAllocator() : head(nullptr) {};
    JsonAllocator(const JsonAllocator &) = delete;
    JsonAllocator &operator=(const JsonAllocator &) = delete;
    JsonAllocator(JsonAllocator &&x) : head(x.head) {
        x.head = nullptr;
    }
    JsonAllocator &operator=(JsonAllocator &&x) {
        head = x.head;
        x.head = nullptr;
        return *this;
    }
    ~JsonAllocator() {
        deallocate();
    }
    void *allocate(size_t size);
    void deallocate();
};

enum ParseFlags {
  PARSE_COMMENTS = 0x1,
  PARSE_NO_NUMBER = 0x2,
};

int jsonParse(char *str, char **endptr, JsonValue *value, JsonAllocator &allocator, int flags = 0);
