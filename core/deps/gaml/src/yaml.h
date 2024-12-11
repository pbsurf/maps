#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include <string>

namespace YAML {

enum class Tag {
    // bits 0 - 7
    UNDEFINED = 0,  // key not found (but can be added by assignment)
    NUMBER = 1,
    STRING = 2,
    ARRAY = 3,
    OBJECT = 4,
    JSON_NULL = 5,
    JSON_BOOL = 6,
    YAML_COMMENT = 7,
    INVALID = 0xFF,  // returned by [](char*) if not object, or [](int) if not array
    TYPE_MASK = 0xFF,
    // bits 8 - 9
    YAML_DBLQUOTED = 0,
    YAML_SINGLEQUOTED = 1 << 8,
    YAML_UNQUOTED = 2 << 8,
    YAML_BLOCKSTRING = 3 << 8,
    YAML_STRINGMASK = 3 << 8,
    // bit 10
    YAML_FLOW = 1 << 10
};

inline constexpr Tag operator&(Tag a, Tag b) { return Tag(int(a) & int(b)); }
inline constexpr Tag operator|(Tag a, Tag b) { return Tag(int(a) | int(b)); }

struct JsonNode;

class JsonValue {
    friend class Node;
    friend class Builder;

    union {
        JsonNode* pval_ = nullptr;
        //uint64_t ival_;
        double fval_;
    };
    std::string strVal;
    Tag flags_;  // = Tag::UNDEFINED;

public:
    JsonValue(double x, Tag flags = Tag::NUMBER) : fval_(x), flags_(flags) {}
    JsonValue(std::string&& s, Tag flags = Tag::STRING)
        : strVal(s), flags_((flags & Tag::TYPE_MASK) != Tag::UNDEFINED ? flags : (flags | Tag::STRING)) {}
    JsonValue(const std::string& s, Tag flags = Tag::STRING) : JsonValue(std::string(s), flags) {}
    JsonValue(JsonNode* payload, Tag flags) : pval_(payload), flags_(flags) {}
    JsonValue(Tag flags = Tag::UNDEFINED) : flags_(flags) {}

    //JsonValue(std::initializer_list<JsonValue&&> items) {
    //    JsonNode* tail = nullptr;
    //    for(JsonValue&& item : items) {
    //        JsonNode* obj = new JsonNode{std::move(item), nullptr, nullptr};
    //        if(!tail) { pval_ = obj; } else { tail->next = obj; }
    //        tail = obj;
    //    }
    //}
    //
    //JsonValue(std::initializer_list<std::pair<std::string, JsonValue&&>> items) {
    //    JsonNode* tail = nullptr;
    //    for(auto& item : items) {
    //        JsonNode* obj = new JsonNode{std::move(item.second), nullptr, item.first};
    //        if(!tail) { pval_ = obj; } else { tail->next = obj; }
    //        tail = obj;
    //    }
    //}

    ~JsonValue();

    // non-copyable (since array/object contents are owned)
    JsonValue(const JsonValue&) = delete;
    JsonValue(JsonValue&& b) { *this = std::move(b); }
    JsonValue& operator=(JsonValue&& b);

    JsonValue clone() const;

    Tag getTag() const { return flags_ & Tag::TYPE_MASK; }
    Tag getFlags() const { return flags_; }
    const std::string& getString() const { return strVal; }
    const char* getCStr() const { return strVal.c_str(); }
    bool isNumber() const { return getTag() == Tag::NUMBER; }
    double getNumber() const { assert(isNumber()); return fval_; }

    bool getBoolean() const { assert(getTag() == Tag::JSON_BOOL); return fval_ != 0; }
    JsonNode* getNode() const {
        return (getTag() == Tag::ARRAY || getTag() == Tag::OBJECT) ? pval_ : nullptr;
    }

    operator bool() const { return getTag() != Tag::UNDEFINED && getTag() != Tag::INVALID; }
    bool operator!() const { return !operator bool(); }
};

using Value = JsonValue;

class Builder;
struct NodeIterator;

enum class NodeType { Undefined, Null, Scalar, Sequence, Map };

class Node {
public:
    JsonValue* value;

    Node();
    Node(JsonValue* v) : value(v) {}

    // match yaml-cpp Node for easy replacement
    const std::string& Scalar() const { return value->getString(); }
    bool IsScalar() const { return value->getTag() == Tag::STRING || value->getTag() == Tag::NUMBER; }
    bool IsSequence() const { return value->getTag() == Tag::ARRAY; }
    bool IsMap() const { return value->getTag() == Tag::OBJECT; }
    bool IsDefined() const { return bool(*value); }
    bool IsNull() const { return bool(*value); }
    bool IsQuoted() const { return value->getTag() == Tag::STRING &&
        (value->getFlags() & YAML::Tag::YAML_STRINGMASK) != YAML::Tag::YAML_UNQUOTED; }
    NodeType Type() const;
    void reset(const Node& other) { value = other.value; }

    NodeIterator begin() const;
    NodeIterator end() const;

    //operator JsonValue&() { return *node; }

    operator bool() const { return bool(*value); }
    bool operator!() const { return !operator bool(); }

    Node& operator=(double x) { return operator=(JsonValue(x)); }
    Node& operator=(const char* s) { return operator=(JsonValue(s)); }
    Node& operator=(const std::string& s) { return operator=(s.c_str()); }
    Node& operator=(JsonValue&& val);

    template <typename T> T as(const T& _default = {}, bool* ok = nullptr) const;

    Node operator[](const char* key) const;
    Node operator[](const std::string& key) const { return operator[](key.c_str()); }
    Node operator[](int idx) const;  // using size_t creates ambiguity w/ nullptr for other overloads
    void push_back(JsonValue&& val);
    bool remove(const char* key);
    bool remove(const std::string& key) { return remove(key.c_str()); }
    bool remove(int idx);
    size_t size() const;

    JsonValue clone() const { return value->clone(); }

    Builder build();
};

template<> int Node::as(const int& _default, bool* ok) const;
template<> float Node::as(const float& _default, bool* ok) const;
template<> double Node::as(const double& _default, bool* ok) const;
template<> std::string Node::as(const std::string& _default, bool* ok) const;
template<> bool Node::as(const bool& _default, bool* ok) const;

class Builder : public Node {
public:
    using Node::Node;
    using Node::operator=;

    Builder operator[](const char* key);
    Builder operator[](const std::string& key) { return operator[](key.c_str()); }
    Builder operator[](int idx);
};

class Document : public Node {
public:
    Document() : Node(new JsonValue(Tag::UNDEFINED)) {}
    Document(JsonValue* v) : Node(v) {}
    ~Document() { delete value; value = nullptr; }

    Document(const Document&) = delete;
    Document(Document&& b) : Node(nullptr) { *this = std::move(b); }
    Document& operator=(Document&& b) { std::swap(value, b.value); return *this; }
};

struct JsonNode {
    JsonValue value;
    JsonNode *next = nullptr;
    std::string key;  //char *key;

    Node node() { return Node(&value); }
    Node keynode() { return Node(&key); }
};

// Iterator

struct Iterator {
    JsonNode* p;

    void operator++() { p = p->next;  }
    bool operator!=(const Iterator &x) const { return p != x.p; }
    JsonNode* operator*() const { return p; }
    JsonNode* operator->() const { return p; }
};

inline Iterator begin(const JsonValue& o) { return Iterator{o.getNode()}; }
inline Iterator end(const JsonValue&) { return Iterator{nullptr}; }

// this is what yaml-cpp iterator does (!)
class NodeOrPair : public Node, public std::pair<Node, Node> {
public:
    NodeOrPair(JsonValue* v);
    NodeOrPair(JsonValue* k, JsonValue* v);
};

struct NodeIterator {
    JsonNode* p;

    void operator++() { p = p->next;  }
    bool operator!=(const NodeIterator &x) const { return p != x.p; }
    NodeOrPair operator*() const;
    NodeOrPair operator->() const;
};

// Parser

enum class Error {
    OK = 0,
    BAD_NUMBER,
    BAD_STRING,
    BAD_IDENTIFIER,
    STACK_OVERFLOW,
    STACK_UNDERFLOW,
    MISMATCH_BRACKET,
    UNEXPECTED_CHARACTER,
    UNQUOTED_KEY,
    BREAKING_BAD,
    ALLOCATION_FAILURE,
    COUNT
};

const char* jsonStrError(Error err);

enum ParseFlags {
    PARSE_COMMENTS = 0x1,  // include comments
    PARSE_JSON = 0x2,  // require JSON (exit with error if invalid JSON)
};

struct ParseResult {
    Error error;
    int linenum;
    const char* endptr;
};

ParseResult parseTo(const char *str, JsonValue *value, int flags = 0);

Document parse(const char* s, int flags = 0, ParseResult* resultout = nullptr);
Document parse(const std::string& s, int flags = 0, ParseResult* resultout = nullptr);

// yaml-cpp compatibility
Document Load(const char* s) { return parse(s); }
Document Load(const std::string& s) { return parse(s); }
Document LoadFile(const std::string& filename);

// Writer

struct Writer {
    char quote = '"';
    int indent = 2;  // size (i.e. number of spaces) of each indent step
    int flowLevel = 10; // switch to flow style beyond this indentation level
    int extraLines = 0;  // add (extraLines - level) lines between map/hash blocks
    //std::set<std::string> alwaysFlow; // always use flow style for specified key names

    std::string spacing(int level);
    std::string keyString(std::string str);
    std::string convertArray(const JsonValue& obj, int level);
    std::string convertHash(const JsonValue& obj, int level);

    std::string convert(const JsonValue& obj, int level = 0);
};

std::string Dump(const Node& node);

}  // namespace YAML