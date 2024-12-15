#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include <string>

namespace YAML {

enum class Tag {
    NONE = 0,
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
    YAML_UNQUOTED = 0,  // default to unquoted; Writer will check if quoting is necessary
    YAML_SINGLEQUOTED = 1 << 8,
    YAML_DBLQUOTED = 2 << 8,
    YAML_BLOCKSTRING = 3 << 8,
    YAML_STRINGMASK = 3 << 8,
    // bit 10
    YAML_FLOW = 1 << 10,
    // bit 11; Value is ignored by Writer if set
    NO_WRITE = 1 << 11,
    // set if parsed from input (instead of created in code)
    PARSED = 1 << 12
};

inline constexpr Tag operator&(Tag a, Tag b) { return Tag(int(a) & int(b)); }
inline constexpr Tag operator|(Tag a, Tag b) { return Tag(int(a) | int(b)); }
inline constexpr Tag operator~(Tag a) { return Tag(~int(a)); }
inline constexpr bool operator!(Tag a) { return int(a) == 0; }

class Node;
template<class T> struct NodeIteratorT;
using NodeIterator = NodeIteratorT<Node>;
using ConstNodeIterator = NodeIteratorT<const Node>;

template<class T> struct PairIteratorT;
using PairIterator = PairIteratorT<Node>;
using ConstPairIterator = PairIteratorT<const Node>;

struct ListNode;
struct InitPair;
struct ListItems;
struct PairItems;
struct ConstPairItems;

// yaml-cpp compatibility
enum class NodeType { Undefined, Null, Scalar, Sequence, Map };

class Node {
    union {
        ListNode* pval_ = nullptr;
        //uint64_t ival_;
        double fval_;
    };
    std::string strVal;
    Tag flags_ = Tag::UNDEFINED;

public:
    Node(std::string&& s, Tag flags = Tag::STRING);
    Node(const char* s, Tag flags = Tag::STRING) : Node(std::string(s), flags) {}
    Node(const std::string& s, Tag flags = Tag::STRING) : Node(std::string(s), flags) {}
    Node(double x, Tag flags = Tag::NUMBER) : fval_(x), flags_(flags) {}
    Node(Tag flags = Tag::UNDEFINED, ListNode* payload = nullptr) : pval_(payload), flags_(flags) {}

    Node(std::initializer_list<InitPair> items);

    ~Node();

    // non-copyable (since array/object contents are owned)
    Node(const Node&) = delete;
    Node(Node&& b) { *this = std::move(b); }
    Node& operator=(Node&& b);

    Node clone() const;

    Tag getTag() const { return flags_ & Tag::TYPE_MASK; }
    Tag getFlags() const { return flags_; }
    bool isString() const { return getTag() == Tag::STRING; }
    const std::string& getString() const { return strVal; }
    const char* getCStr() const { return strVal.c_str(); }
    bool isNumber() const { return getTag() == Tag::NUMBER; }
    double getNumber() const { assert(isNumber()); return fval_; }

    bool getBoolean() const { assert(getTag() == Tag::JSON_BOOL); return fval_ != 0; }
    ListNode* getNode() const {
        return (getTag() == Tag::ARRAY || getTag() == Tag::OBJECT) ? pval_ : nullptr;
    }

    // iterate over values
    ConstNodeIterator begin() const;
    ConstNodeIterator end() const;
    NodeIterator begin();
    NodeIterator end();

    // to iterate over keys and values
    ConstPairItems pairs() const;
    PairItems pairs();

    // iterate over ListNodes (mostly for internal use)
    ListItems items() const;

    Node& operator=(double x) { return operator=(Node(x)); }
    Node& operator=(const char* s) { return operator=(Node(s)); }
    Node& operator=(const std::string& s) { return operator=(s.c_str()); }

    // note that we use SFINAE on return type to create overloads
    template <typename T> typename std::enable_if<!std::is_arithmetic<T>::value, T>::type
    as(T _default = {}, bool* ok = nullptr) const;

    template <typename T> typename std::enable_if<std::is_arithmetic<T>::value, T>::type
    as(T _default = {}, bool* ok = nullptr) const {
        return static_cast<T>(as<double>(static_cast<double>(_default), ok));
    }

    // tried to use a cstr_view class, but this breaks ["..."]!
    const Node& operator[](const char* key) const;
    const Node& operator[](const std::string& key) const { return operator[](key.c_str()); }
    const Node& operator[](int idx) const;  // using size_t creates ambiguity w/ nullptr for other overloads
    Node& push_back(Node&& val);
    bool remove(const char* key);
    bool remove(const std::string& key) { return remove(key.c_str()); }
    bool remove(int idx);
    void merge(Node&& src);
    int size() const;  // use int instead of size_t to match operator[]

    bool has(const char* key) const { return bool(operator[](key)); }
    bool has(const std::string& key) const { return has(key.c_str()); }
    Node& add(const char* key, bool replace = false);
    Node& add(const std::string& key, bool replace = false) { return add(key.c_str(), replace); }
    // non-const [] adds Value if not present
    Node& operator[](const char* key) { return add(key); }
    Node& operator[](const std::string& key) { return add(key); }
    Node& operator[](int idx);

    operator bool() const { return getTag() != Tag::UNDEFINED && getTag() != Tag::INVALID; }
    bool operator!() const { return !operator bool(); }

    bool operator==(const char* s) { return getTag() == Tag::STRING && strVal == s; }
    bool operator==(const std::string& s) { return operator==(s.c_str()); }
    bool operator!=(const char* s) { return !operator==(s); }
    bool operator!=(const std::string& s) { return !operator==(s); }

    // match yaml-cpp Node for easy replacement
    const std::string& Scalar() const { return getString(); }
    bool IsScalar() const { return getTag() == Tag::STRING || getTag() == Tag::NUMBER; }
    bool IsSequence() const { return getTag() == Tag::ARRAY; }
    bool IsMap() const { return getTag() == Tag::OBJECT; }
    bool IsDefined() const { return bool(*this); }
    bool IsNull() const { return bool(*this); }
    bool IsQuoted() const { return getTag() == Tag::STRING &&
        (getFlags() & YAML::Tag::YAML_STRINGMASK) != YAML::Tag::YAML_UNQUOTED; }
    NodeType Type() const;
    void setNoWrite(bool nowrite = true);
};

template<> bool Node::as(bool _default, bool* ok) const;
template<> double Node::as(double _default, bool* ok) const;
template<> std::string Node::as(std::string _default, bool* ok) const;

// helpers for initializer list creation
Node Array(std::initializer_list<Node> items = {});
inline Node Map(std::initializer_list<InitPair> items = {}) { return Node(items); }

struct InitPair {
    std::string key;
    Node val;

    InitPair(std::string k, Node&& v) : key(std::move(k)), val(std::move(v)) {}
    InitPair(std::string k, std::string v) : key(std::move(k)), val(std::move(v)) {}
    InitPair(std::string k, const char* v): key(std::move(k)), val(v) {}
    InitPair(std::string k, double v) : key(std::move(k)), val(v) {}
    //JsonTuple() : val(Tag::OBJECT) {}
};

struct ListNode {
    Node value;
    ListNode *next = nullptr;
    Node key;  //std::string key;  //char *key;

    Node& node() { return value; }
    Node& keynode() { return key; }
};

// ListIterator - mostly for internal use, so no const version for now

struct ListIterator {
    ListNode* p;

    void operator++() { p = p->next;  }
    bool operator!=(const ListIterator &x) const { return p != x.p; }
    ListNode* operator*() const { return p; }
    ListNode* operator->() const { return p; }
};

struct ListItems {
    ListNode* n;
    ListIterator begin() { return ListIterator{n}; }
    ListIterator end() { return ListIterator{nullptr}; }
};

// NodeIterator, PairIterator
// this is what yaml-cpp iterator does to combine (!):
//class NodeOrPair : public Node, public std::pair<Node, Node> { ...

template<class T>
struct NodeIteratorT {
    ListNode* p;

    void operator++() { p = p->next; }
    bool operator==(const NodeIteratorT& x) const { return p == x.p; }
    bool operator!=(const NodeIteratorT& x) const { return !operator==(x); }
    T& operator*() const { return p->value; }
    T* operator->() const { return &p->value; }
};

template<class T>
struct PairIteratorT {
    ListNode* p;

    void operator++() { p = p->next; }
    bool operator==(const PairIteratorT& x) const { return p == x.p; }
    bool operator!=(const PairIteratorT& x) const { return !operator==(x); }
    std::pair<T&, T&> operator*() const { return std::pair<T&, T&>(p->key, p->value); }
    //ptr_wrapper<T> operator->() const { return {operator*()}; }
};

struct PairItems {
    ListNode* n;
    PairIterator begin() { return PairIterator{n}; }
    PairIterator end() { return PairIterator{nullptr}; }
};

struct ConstPairItems {
    ListNode* n;
    ConstPairIterator begin() { return ConstPairIterator{n}; }
    ConstPairIterator end() { return ConstPairIterator{nullptr}; }
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
    UNEXPECTED_CHAR,
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

ParseResult parseTo(const char *str, size_t len, Node *value, int flags = 0);

Node parse(const char* s, size_t len = 0, int flags = 0, ParseResult* resultout = nullptr);
Node parse(const std::string& s, int flags = 0, ParseResult* resultout = nullptr);

// yaml-cpp compatibility
inline Node Load(const char* s, size_t len = 0) { return parse(s, len); }
inline Node Load(const std::string& s) { return parse(s); }
Node LoadFile(const std::string& filename);

// Writer

struct Writer {
    char quote = '"';
    int indent = 2;  // size (i.e. number of spaces) of each indent step
    int flowLevel = 10; // switch to flow style beyond this indentation level
    int extraLines = 0;  // add (extraLines - level) lines between map/hash blocks
    //std::set<std::string> alwaysFlow; // always use flow style for specified key names

    std::string spacing(int level);
    std::string convertArray(const Node& obj, int level);
    std::string convertHash(const Node& obj, int level);

    std::string convert(const Node& obj, int level = 0);
};

std::string Dump(const Node& node);

}  // namespace YAML
