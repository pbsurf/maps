#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include <string>

#define ENABLE_YAML 1

#if ENABLE_YAML
namespace YAML {
#else
namespace JSON {
#endif

}

enum JsonTag {
    UNDEFINED = 0,  // key not found (but can be added by assignment)
    JSON_NULL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_BOOL,
#if ENABLE_YAML
    YAML_COMMENT,
    YAML_SINGLEQUOTED,
    YAML_UNQUOTED,
    YAML_BLOCKSTRING,
    INVALID = 0xFF  // returned by [](char*) if not object, or [](int) if not array
#endif
};

struct JsonNode;

class JsonValue {
    friend class Node;
    friend class SettingNode;

    union {
      JsonNode* pval_ = nullptr;
      uint64_t ival_;
      double fval_;
    };
    std::string strVal;
    uint8_t tag_ = JSON_NULL;
    // we'll need to add additional fields for YAML (flow style flag)

public:
    JsonValue(double x) : fval_(x), tag_(JSON_NUMBER) {}
    JsonValue(std::string&& s, JsonTag tag = JSON_STRING) : strVal(s), tag_(tag) {}
    JsonValue(JsonTag tag = JSON_NULL, JsonNode* payload = nullptr) : pval_(payload), tag_(tag) {}
    //if(payload) assert(tag == JSON_ARRAY || tag == JSON_OBJECT);

    JsonValue(const JsonValue&) = delete;
    JsonValue(JsonValue&& b) { *this = std::move(b); }
    JsonValue& operator=(JsonValue&& b) { std::swap(pval_, b.pval_); std::swap(tag_, b.tag_); std::swap(strVal, b.strVal);}
    ~JsonValue() { if((getTag() == JSON_ARRAY || getTag() == JSON_OBJECT) && pval_) delete pval_; }

    JsonTag getTag() const { return (JsonTag)tag_; }
    const std::string& getString() const { return strVal; }
    const char* getCStr() const { return strVal.c_str(); }
    bool isNumber() const { return tag_ == JSON_NUMBER; }
    double getNumber() const { assert(isNumber()); return fval_; }

    bool getBoolean() const { assert(getTag() == JSON_BOOL); return bool(ival_); }
    JsonNode* getNode() const {
        return (getTag() == JSON_ARRAY || getTag() == JSON_OBJECT) ? pval_ : nullptr;
    }

};

class SettingNode;

class Node {
public:
  JsonValue* value;

  //operator JsonValue&() { return *node; }

  Node(JsonValue* v) : value(v) {}
  const std::string& Scalar() { return value->getString(); }

  operator bool() { return getTag() != UNDEFINED; }
  bool operator!() { return !operator bool(); }

  Node& operator=(double x) { operator=(JsonValue(x)); }
  Node& operator=(const char* s) { operator=(JsonValue(s)); }
  Node& operator=(const std::string& s) { operator=(s.c_str()); }

  Node& operator=(JsonValue&& val) {
    if (value != INVALID_VALUE) { *value = std::move(val); }
    return *this;
  }

  template <typename T> T as(const T& _default);  // = {});
  template<> int as(const int& _default);
  template<> float as(const float& _default);
  template<> double as(const double& _default);
  template<> std::string as(const std::string& _default);
  template<> bool as(const bool& _default);

  Node operator[](const char* key);
  Node operator[](const std::string& key) { return operator[](key.c_str()); }
  Node operator[](size_t idx);
  void push_back(JsonValue&& val);
  size_t size();

  SettingNode set() { return SettingNode(value); }


};

class SettingNode : public Node {
  using Node::Node;

  SettingNode operator[](const char* key);
  SettingNode operator[](const std::string& key) { return operator[](key.c_str()); }
  SettingNode operator[](size_t idx);
};

class DocumentNode : public Node {
  ~DocumentNode() { delete value; value = nullptr; }
};


//static JsonNode NULL_NODE = {JsonValue(), nullptr, nullptr};

struct JsonNode {
    JsonValue value;
    JsonNode *next = nullptr;
    std::string key;  //char *key;

    ~JsonNode();
    Node node() { return Node(&value); }
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

inline JsonIterator begin(const JsonValue& o) { return JsonIterator{o.getNode()}; }
inline JsonIterator end(const JsonValue&) { return JsonIterator{nullptr}; }

inline JsonIterator begin(Node o) { return JsonIterator{o.value->getNode()}; }
inline JsonIterator end(Node) { return JsonIterator{nullptr}; }

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
