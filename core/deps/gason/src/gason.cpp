#include "gason.h"
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <vector>
#include <set>
#include <utility>

// Why this was written:
// 1. NIH syndrome
// 2. data race in tangrams/yaml-cpp causing crashes (due to custom, non-atomic ref counting)
//  - upstream yaml-cpp uses shared_ptr instead and probably would have fixed the issue, but
//   divergence between branches may have made swapping it in difficult
// 3. huge reduction in LOC by replacing yaml-cpp and rapidjson

namespace YAML {

// helper fns

static inline bool isspace(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static inline bool isdelim(char c) {
    return c == ',' || c == ':' || c == ']' || c == '}' || isspace(c) || c == '#' || !c;
}

// technically, we should check for space after ',' and ':' if not in flow mode
static inline bool isendscalar(char c) {
    return c == ',' || c == ':' || c == ']' || c == '}' || c == '\r' || c == '\n' || c == '#'|| !c;
}

static inline bool isdigit(char c) {
    return c >= '0' && c <= '9';
}

static inline bool isxdigit(char c) {
    return (c >= '0' && c <= '9') || ((c & ~' ') >= 'A' && (c & ~' ') <= 'F');
}

static inline int char2int(char c) {
    if (c <= '9')
        return c - '0';
    return (c & ~' ') - 'A' + 10;
}

static double string2double(const char *s, char **endptr) {
    char ch = *s;
    if (ch == '-')
        ++s;

    double result = 0;
    while (isdigit(*s))
        result = (result * 10) + (*s++ - '0');

    if (*s == '.') {
        ++s;

        double fraction = 1;
        while (isdigit(*s)) {
            fraction *= 0.1;
            result += (*s++ - '0') * fraction;
        }
    }

    if (*s == 'e' || *s == 'E') {
        ++s;

        double base = 10;
        if (*s == '+')
            ++s;
        else if (*s == '-') {
            ++s;
            base = 0.1;
        }

        unsigned int exponent = 0;
        while (isdigit(*s))
            exponent = (exponent * 10) + (*s++ - '0');

        double power = 1;
        for (; exponent; exponent >>= 1, base *= base)
            if (exponent & 1)
                power *= base;

        result *= power;
    }

    *endptr = (char*)s;
    return ch == '-' ? -result : result;
}

// Node

static JsonValue UNDEFINED_VALUE(Tag::UNDEFINED);
static JsonValue INVALID_VALUE(Tag::INVALID);

JsonValue::~JsonValue() {
    if (getTag() == Tag::ARRAY || getTag() == Tag::OBJECT) {
        while(pval_) { delete std::exchange(pval_, pval_->next);  }
    }
}

JsonValue& JsonValue::operator=(JsonValue&& b) {
    std::swap(pval_, b.pval_);
    std::swap(flags_, b.flags_);
    std::swap(strVal, b.strVal);
    return *this;
}

JsonValue JsonValue::clone() const {
    JsonValue res(flags_);
    res.strVal = strVal;
    if (!getNode()) {
        res.fval_ = fval_;
        return res;
    }
    JsonNode* tail = nullptr;
    for(auto item : *this) {
        JsonNode* obj = new JsonNode{item->value.clone(), nullptr, item->key};
        if(!tail) { res.pval_ = obj; } else { tail->next = obj; }
        tail = obj;
    }
    return res;
}

Builder Node::build() { return Builder(value); }

Builder Builder::operator[](const char* key) {
    Node n = Node::operator[](key);
    if (n.value == &UNDEFINED_VALUE) {
        if(value->getTag() == Tag::UNDEFINED) {
            value->flags_ = Tag::OBJECT;
        }
        auto* val = new JsonNode{Tag::UNDEFINED, nullptr, key};
        if(!value->getNode()) {
            value->pval_ = val;
        } else {
            JsonNode* obj = value->getNode();
            while(obj->next) { obj = obj->next; }
            obj->next = val;
        }
        return Builder(&val->value);
    }
    return Builder(n.value);
}

Node Node::operator[](const char* key) {
    if (value->getTag() == Tag::UNDEFINED) { return Node(&UNDEFINED_VALUE); }
    if (value->getTag() != Tag::OBJECT) { return Node(&INVALID_VALUE); }
    JsonNode* obj = value->pval_;
    while(obj && obj->key != key) {
      obj = obj->next;
    }
    return obj ? obj->node() : Node(&UNDEFINED_VALUE);
}

Builder Builder::operator[](int idx) {
    Node n = Node::operator[](idx);
    if (n.value == &UNDEFINED_VALUE) {
        if(value->getTag() == Tag::UNDEFINED) {
            value->flags_ = Tag::ARRAY;
        }
        auto* val = new JsonNode{Tag::UNDEFINED, nullptr, {}};
        if(!value->getNode()) {
            value->pval_ = val;
        } else {
            JsonNode* obj = value->getNode();
            while(obj->next) { obj = obj->next; }
            obj->next = val;
        }
        return Builder(&val->value);
    }
    return Builder(n.value);
}

Node Node::operator[](int idx) {
    if (value->getTag() == Tag::UNDEFINED) { return Node(&UNDEFINED_VALUE); }
    if (value->getTag() != Tag::ARRAY) { return Node(&INVALID_VALUE); }
    JsonNode* array = value->getNode();
    do {
        while (array && array->value.getTag() == Tag::YAML_COMMENT) { array = array->next; }
    } while (idx-- && array && (array = array->next));
    return array ? array->node() : Node(&UNDEFINED_VALUE);
}

void Node::push_back(JsonValue&& val) {
    if (value->getTag() == Tag::UNDEFINED) { value->flags_ =Tag:: ARRAY; }
    else if (value->getTag() != Tag::ARRAY) { return; }
    JsonNode* item = new JsonNode{std::move(val), nullptr, nullptr};
    JsonNode* array = value->getNode();
    if (!array) { value->pval_ = item; }
    while (array->next) { array = array->next; }
    array->next = item;
}

size_t Node::size() {
    size_t n = 0;
    for (JsonNode* obj = value->getNode(); obj; obj = obj->next) { ++n; }
    return n;
}

Node& Node::operator=(JsonValue&& val) {
  if (value != &UNDEFINED_VALUE && value != &INVALID_VALUE) {
      *value = std::move(val);
  }
  return *this;
}

template<> int Node::as(const int& _default) {
    return int(as<double>(double(_default)));
}

template<> float Node::as(const float& _default) {
    return float(as<double>(double(_default)));
}

template<> double Node::as(const double& _default) {
    if(value->isNumber()) { return value->getNumber(); }
    if(value->getTag() != Tag::STRING) { return _default; }
    char* endptr;
    auto s = value->getString();
    double val = s[0] == '0' ? strtoul(s.c_str(), &endptr, 0) : string2double(s.c_str(), &endptr);
    // endptr should point to '\0' terminator char after successful conversion
    return *endptr ? _default : val;  //endptr - s == strlen(s) ? val : _default;
}

template<> std::string Node::as(const std::string& _default) {
    if(value->isNumber()) { return std::to_string(value->getNumber()); }
    return value->getTag() == Tag::STRING ? value->getString() : _default;
}

template<> bool Node::as(const bool& _default) {
    // YAML 1.2 only allows true/false ... but if caller is asking for bool be flexible
    static const char* boolstrs[] = {"true","false","True","False","TRUE","FALSE",
        "y","n","Y","N","yes","no","Yes","No","YES","NO","on","off","On","Off","ON","OFF"};

    if(value->isNumber()) { return value->getNumber() != 0; }
    auto s = value->getString();
    int idx = 0;
    for (const char* boolstr : boolstrs) {
        if (s == boolstr) { return idx%2 == 0; }
        ++idx;
    }
    return  _default;
}


// Parser

static inline JsonNode* insertAfter(JsonNode* tail, JsonNode* node) {
    if (!tail)
        return node->next = node;
    node->next = tail->next;
    tail->next = node;
    return node;
}

static inline JsonValue listToValue(Tag tag, JsonNode* tail) {
    if (tail) {
        auto head = tail->next;
        tail->next = nullptr;
        return JsonValue(head, tag);
    }
    return JsonValue(nullptr, tag);
}

static inline char unescapedChar(char c) {
    switch(c) {
        case '\\':
        case '"':
        case '/':  return c;
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
    }
    return '\0';
}

static inline const char* escapedChar(char c) {
    switch(c) {
        case '\\':  return "\\\\";
        case '"':   return "\\\"";
        //case '/':   return "\\/"; -- can be escaped but doesn't need to be
        case '\b':  return "\\b";
        case '\f':  return "\\f";
        case '\n':  return "\\n";
        case '\r':  return "\\r";
        case '\t':  return "\\t";
    }
    return nullptr;
}

const char* jsonStrError(Error err) {
    static const char* errorMsg[] = {"ok", "bad number", "bad string", "bad identifier", "stack overflow",
        "stack underflow", "mismatched bracket", "unexpected character", "unquoted key", "breaking bad",
        "allocation failure", "invalid error code"};
    return errorMsg[std::min(size_t(err), size_t(Error::COUNT))];
}

Document parse(const std::string& s, int flags, ParseResult* resultout) {
    return parse(s.c_str(), flags, resultout);
}

#ifndef GAML_LOG
#define GAML_LOG(msg, ...) do { fprintf(stderr, msg, ## __VA_ARGS__); } while(0)
#endif

Document parse(const char* s, int flags, ParseResult* resultout) {
    Document doc;
    ParseResult res = parseTo(s, doc.value, flags);
    if (resultout) { *resultout = res; }
    if (res.error != Error::OK) {
        const char* newl = res.endptr;
        while(*newl && *newl != '\n') { ++newl; }
        GAML_LOG("YAML parse error (line %d): %s at %s\n",
                 res.linenum, jsonStrError(res.error), std::string(res.endptr, newl).c_str());
    }
    return doc;
}

ParseResult parseTo(const char *s, JsonValue *valueout, int flags) {
    constexpr size_t PARSE_STACK_SIZE = 32;
    JsonNode* tails[PARSE_STACK_SIZE];
    Tag tags[PARSE_STACK_SIZE];
    JsonValue keys[PARSE_STACK_SIZE];
    int indents[PARSE_STACK_SIZE];

    JsonValue o(Tag::UNDEFINED);
    int pos = -1;
    int indent = 0;
    int flowlevel = 0;
    int linenum = 1;
    bool separator = true;
    bool blockarrayobj = false;
    char nextchar;
    JsonNode* node;
    std::string temp;
    const char* s0 = s;
    const char* linestart = s;
    const char* endptr = s;

    while (1) {
        while (isspace(*s)) {
            if (*s == '\n') {
                if (!flowlevel) { linestart = s+1; }
                ++linenum;
            }
            ++s;
        }
        if (!*s) {
            if (flowlevel) { return {Error::MISMATCH_BRACKET, linenum, s}; }
            indent = -1;
        } else if (linestart) {
            indent = s - linestart;
        }

        if (*s == '{' || *s == '[') { ++flowlevel; }

        if (blockarrayobj) {
            ++s;  // skip ':'
            indent += 2;
            nextchar = '{';
        } else if (flowlevel || *s == '#') {
            endptr = s0 = s;
            nextchar = *s++;
        } else if (pos < 0 || indent > indents[pos]) {
            nextchar = (*s == '-' && isspace(s[1])) ? '[' : '{';
        } else if (pos >= 0 && indent < indents[pos]) {
            nextchar = tags[pos] == Tag::ARRAY ? ']' : '}';
        } else {
            endptr = s0 = s;
            nextchar = *s++;
            // handle array '-'
            if (linestart) {
                linestart = nullptr;
                if (nextchar == '-' && isspace(*s)) {
                    if (tags[pos] != Tag::ARRAY) { return {Error::UNEXPECTED_CHARACTER, linenum, endptr}; }
                    ++s;
                    continue;
                }
            }
        }

        switch (nextchar) {
        case '"':
            ++s0;  // skip "
            for(; *s; ++s) {
                char c = *s;
                if (c == '\\') {
                    temp.append(s0, s);
                    c = *++s;
                    if (c == 'u') {
                        int u = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (isxdigit(*++s)) {
                                u = u * 16 + char2int(*s);
                            } else {
                                return {Error::BAD_STRING, linenum, s};
                            }
                        }
                        if (u < 0x80) {
                            temp.push_back(u);  //*it = c;
                        } else if (u < 0x800) {
                            temp.push_back(0xC0 | (u >> 6));
                            temp.push_back(0x80 | (u & 0x3F));
                        } else {
                            temp.push_back(0xE0 | (u >> 12));
                            temp.push_back(0x80 | ((u >> 6) & 0x3F));
                            temp.push_back(0x80 | (u & 0x3F));
                        }
                    } else if ((c = unescapedChar(c))) {
                        temp.push_back(c);  //*it = c;
                    } else {
                        return {Error::BAD_STRING, linenum, s};
                    }
                    s0 = s+1;
                } else if (c == '"') {
                    temp.append(s0, s++);
                    break;
                }
            }
            if (!isdelim(*s)) {
                return {Error::BAD_STRING, linenum, s};
            }
            o = JsonValue(temp, Tag::YAML_DBLQUOTED);
            temp.clear();
            break;
        case '[':
        case '{':
            if (++pos == PARSE_STACK_SIZE)
                return {Error::STACK_OVERFLOW, linenum, endptr};
            tails[pos] = nullptr;
            tags[pos] = (nextchar == '{' ? Tag::OBJECT : Tag::ARRAY);
            keys[pos] = blockarrayobj ? std::move(o) : JsonValue();  //nullptr;
            indents[pos] = indent;
            separator = true;
            blockarrayobj = false;
            continue;
        case ']':
        case '}':
            if (pos == -1)
                return {Error::STACK_UNDERFLOW, linenum, endptr};
            if (tags[pos] != (nextchar == '}' ? Tag::OBJECT : Tag::ARRAY))
                return {Error::MISMATCH_BRACKET, linenum, endptr};
            if (nextchar == '}' && keys[pos])  // != nullptr)
                return {Error::UNEXPECTED_CHARACTER, linenum, endptr};
            if (flowlevel > 0) {
                tags[pos] = tags[pos] | Tag::YAML_FLOW;
                --flowlevel;
            }
            o = listToValue(tags[pos], tails[pos]);
            --pos;
            break;
        case ':':
            if (separator || !keys[pos])  // == nullptr)
                return {Error::UNEXPECTED_CHARACTER, linenum, endptr};
            separator = true;
            continue;
        case ',':
            if (separator || keys[pos])  // != nullptr)
                return {Error::UNEXPECTED_CHARACTER, linenum, endptr};
            separator = true;
            continue;
        case '\0':
            if (pos != -1) { return {Error::MISMATCH_BRACKET, linenum, endptr}; }
            break;
        // YAML only below
        case '#':  // comment
            while (*s != '\r' && *s != '\n') { ++s; }
            if (flags & PARSE_COMMENTS) {
                o = JsonValue(std::string(s0+1, s), Tag::YAML_COMMENT);  //*s = 0;
                break;
            }
            continue;
        case '\'':  // single quoted string
            ++s0;  // skip '
            for (; *s; ++s) {
                char c = *s;
                if (c == '\'') {
                    // only escape sequence allowed in single quoted strings is '' -> '
                    temp.append(s0, s);
                    if (*++s != '\'') { break; }
                    s0 = s;
                }
            }
            if (!isdelim(*s)) {
                return {Error::BAD_STRING, linenum, s};
            }
            o = JsonValue(temp, Tag::YAML_SINGLEQUOTED); //*it = 0;
            temp.clear();
            break;
        case '|':  // literal block scalar
        case '>':  // folded block scalar
        {
            char chomp = *s;
            if (!isspace(chomp)) { ++s; }

            int blockindent = INT_MAX;
            linestart = nullptr;
            while (*s) {
                if (*s == '\n') {
                    if (linestart) { temp.push_back('\n'); }  // blank lines
                    linestart = ++s;
                    ++linenum;
                    continue;
                }
                if (isspace(*s) && (++s) - linestart < blockindent) { continue; }

                if (!linestart) { return {Error::BAD_STRING, linenum, s}; }
                if (s - linestart <= indent) { break; }

                if (blockindent == INT_MAX) { blockindent = s - linestart; }
                else if (s - linestart < blockindent) { return {Error::BAD_STRING, linenum, s}; }

                s0 = s;
                while (*s && *s != '\r' && *s != '\n') { ++s; }
                temp.append(s0, s);

                temp.push_back(nextchar == '|' ? '\n' : ' ');
                linestart = nullptr;
            }

            if (chomp == '-') {
                temp.pop_back();
            } else if (chomp != '+') {
                while (temp.back() == '\n') { temp.pop_back(); }
                temp.push_back('\n');
            }

            o = JsonValue(temp, Tag::YAML_BLOCKSTRING);
            temp.clear();  // note that we do not move temp
            endptr = s;
            break;
        }
        // Unsupported YAML features
        case '?':  // mapping key
        case '&':  // node anchor
        case '*':  // alias
        case '!':  // tag handle
        case '@':  // reserved
        case '`':  // reserved
            return {Error::UNEXPECTED_CHARACTER, linenum, endptr};
        case '-':  // '-' could be array element, document separator, unquoted string, or number
            // '---' separates multiple documents in a single stream
            if (linestart && indent == 0 && *s == '-' && s[1] == '-') {
                if(pos != -1) { return {Error::UNEXPECTED_CHARACTER, linenum, endptr}; }
                s += 2;
                break;
            }
            [[fallthrough]];
        default:  // unquoted string
            if (!flowlevel && keys[pos]) {
                while(*s && *s != '\r' && *s != '\n' && *s != '#') { ++s; }
            } else {
                while(!isendscalar(*s)) { ++s; }
            }
            while (isspace(*(s-1))) { --s; }  // trim trailing spaces
            o = JsonValue(std::string(s0, s), Tag::YAML_UNQUOTED);
            break;
        }

        //linestart = nullptr;
        separator = false;

        // check for invalid JSON if requested
        if ((flags & PARSE_JSON) && o.getTag() == Tag::STRING) {
            Tag t = o.getFlags() & Tag::YAML_STRINGMASK;
            if (t == Tag::YAML_SINGLEQUOTED || t == Tag::YAML_BLOCKSTRING || t == Tag::YAML_COMMENT) {
                return {Error::UNEXPECTED_CHARACTER, linenum, endptr};
            }
            if (t == Tag::YAML_UNQUOTED) {
                if (o.getString() == "true") {
                    o = JsonValue(1, Tag::JSON_BOOL);
                } else if (o.getString() == "false") {
                    o = JsonValue(0.0, Tag::JSON_BOOL);
                } else if (o.getString() == "null") {
                    o = JsonValue(Tag::JSON_NULL);
                } else {
                    // try to parse as number
                    char* endnum;
                    double val = string2double(o.getCStr(), &endnum);
                    if(*endnum != '\0') { return {Error::BAD_NUMBER, linenum, endptr}; }
                    o = JsonValue(val);
                }
            }
        }


        if (pos == -1) {
            *valueout = std::move(o);
            return {Error::OK, linenum, s};
        }

        if (tags[pos] == Tag::OBJECT) {
            if (!keys[pos]) {
                if (o.getTag() != Tag::STRING)
                    return {Error::UNQUOTED_KEY, linenum, endptr};
                keys[pos] = std::move(o);  //o.getString().c_str();
                continue;
            }
            node = new JsonNode{std::move(o), nullptr, keys[pos].getString()};
            tails[pos] = insertAfter(tails[pos], node);
            keys[pos] = JsonValue();  //nullptr;
        } else {
            // handle case of object in array
            if (!flowlevel && *s == ':' && isspace(s[1])) {
                blockarrayobj = true;
                continue;
            }
            node = new JsonNode{std::move(o), nullptr, {}};
            tails[pos] = insertAfter(tails[pos], node);
        }
    }
}

// Writer

static std::string escapeSingleQuoted(const std::string& str) {
    std::string res = "'";
    for (const char* s = str.data(); *s; ++s) {
        res.push_back(*s);
        if(*s == '\'') { res.push_back('\''); }
    }
    return res.append("'");
}

static std::string escapeDoubleQuoted(const std::string& str) {
    std::string res = "\"";
    for (const char* s = str.data(); *s; ++s) {
        const char* esc = escapedChar(*s);
        if (esc) { res.append(esc); } else { res.push_back(*s); }
    }
    return res.append("\"");
}

static std::string escapeUnQuoted(const std::string& s, bool block, char quote) {
    const char* special = block ? "#\r\n" : ",:]}#\r\n";
    if (s.find_first_of(special) == std::string::npos) {
        return s;
    }
    return quote == '"' ? escapeDoubleQuoted(s) : escapeSingleQuoted(s);
}

static std::string blockString(const std::string& str, const std::string& indent) {
    std::string res = "|\n" + indent;
    for(const char* s = str.data(); *s; ++s) {
        res.push_back(*s);
        if(*s == '\n' && s[1]) {
            res.append(indent);
        }
    }
    return res;
}

static std::string strJoin(const std::vector<std::string>& strs, const std::string& sep) {
    if(strs.empty()) { return ""; }
    size_t n = sep.size()*(strs.size()-1);
    for(auto& s : strs) { n += s.size(); }
    std::string res;
    res.reserve(n);
    for(size_t ii = 0; ii < strs.size(); ++ii) {
      if(ii > 0) { res.append(sep); }
      res.append(strs[ii]);
    }
    return res;
}

std::string Writer::spacing(int level) {
    return indent > 0 && level > 0 && level < flowLevel ? std::string(indent*level, ' ') : "";
}

// quote key string if necessary
std::string Writer::keyString(std::string str) {
    static std::string special("!&*-:?{}[],#|>@`\"'%");
    if(!indent || isspace(str[0]) || special.find_first_of(str[0]) != std::string::npos
            || str.find_first_of(":#") != std::string::npos) {
        return quote == '"' ? escapeDoubleQuoted(str.c_str()) : escapeSingleQuoted(str.c_str());
    }
    return str;
}

std::string Writer::convertArray(const JsonValue& obj, int level) {
    std::vector<std::string> res;
    if(indent < 2 || level >= flowLevel || (obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) {
        for(auto item : obj) {
            res.push_back(convert(item->value, flowLevel));
        }
        return res.empty() ? "[]" : "[" + strJoin(res, ", ") + "]";
    }
    for(auto item : obj) {
        std::string str = convert(item->value, level+1);
        const char* s = str.c_str();
        while(isspace(*s)) { ++s; }
        res.push_back(spacing(level) + "-" + std::string(indent-1, ' ') + s);
    }
    return res.empty() ? "[]" : strJoin(res, "\n");
}

std::string Writer::convertHash(const JsonValue& obj, int level) {
    std::vector<std::string> res;
    if((obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) { level = flowLevel; }
    for(auto item : obj) {
        const std::string& key = item->key;
        JsonValue& val = item->value;
        if (val.getTag() == Tag::YAML_COMMENT) {
            res.push_back(convert(val, level+1));
        } else {
            bool sameLine = !indent || level+1 >= flowLevel ||
                    !val.getNode() || (val.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW;
            const char* sep = sameLine ? ": " : ":\n";
            res.push_back(spacing(level) + keyString(key) + sep + convert(val, level+1));
        }
    }
    if (res.empty())
        return "{}";
    if (level >= flowLevel)
        return "{ " + strJoin(res, ", ") + " }";
    return strJoin(res, std::string(std::max(1, 1+extraLines - level), '\n'));
}

std::string Writer::convert(const JsonValue& obj, int level) {
    switch(obj.getTag()) {
    case Tag::ARRAY:
        return convertArray(obj, level);
    case Tag::OBJECT:
        return convertHash(obj, level);
    case Tag::STRING:
        if(!indent) { return escapeDoubleQuoted(obj.getCStr()); }  // JSON
        switch(obj.getFlags() & Tag::YAML_STRINGMASK) {
        case Tag::YAML_SINGLEQUOTED:
            return escapeSingleQuoted(obj.getString());
        case Tag::YAML_UNQUOTED:
            return escapeUnQuoted(obj.getString(), level < flowLevel, quote);
        case Tag::YAML_BLOCKSTRING:
            return blockString(obj.getString(), spacing(level));
        case Tag::YAML_DBLQUOTED:
        default:
            return escapeDoubleQuoted(obj.getString());
        }
    case Tag::JSON_NULL:
        return "null";
    case Tag::NUMBER:
    {
        double val = obj.getNumber();
        return int64_t(val) == val ? std::to_string(int64_t(val)) : std::to_string(val);
    }
    case Tag::JSON_BOOL:
        return obj.getNumber() != 0 ? "true" : "false";
    case Tag::YAML_COMMENT:
        return indent ? "#" + obj.getString() + "\n" : "";
    default:
        return "";
    }
}

}  // namespace YAML

#if 1 //def GAML_MAIN

#include <fstream>
#include <sstream>

YAML::Document basicTests()
{
  static const char* yaml = R"(# comment
layer1:
  sub1: 4
  sub2: 'hello'
layer2:
  - item1
  - "item2"
)";

  static const char* json = R"({
  "json1": {"sub1": 4, "sub2": "hello"},
  "json2": ["item1", "item2"]
})";

  YAML::Document doc = YAML::parse(yaml);
  auto builder = doc.build();
  builder["a"]["b"] = "this is a.b";
  builder["a"]["c"][0] = "this is a.c[0]";
  builder["a"]["c"][1] = "this is a.c[1]";
  builder["b"] = 5.6;
  builder["cloned"] = doc.clone();
  //builder["c"] = {{"x", "this is c.x"}, {"y", "this is c.y"}};
  //builder["d"] = {"a", "b", "c", "d"};
  //builder["e"] = {1, 2, 3, 4};

  YAML::Document jdoc = YAML::parse(json);
  builder["jdoc"] = std::move(*jdoc.value);

  assert(doc["a"]["b"].Scalar() == "this is a.b");
  assert(doc["b"].as<double>() == 5.6);

  YAML::Writer writer;
  writer.indent = 4;
  writer.extraLines = 1;
  std::string out = writer.convert(*doc.value);
  puts(out.c_str());

  return doc;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "No input file specified!");
        YAML::Document doc = basicTests();
        return -1;
    }

    std::string infile(argv[1]);
    std::ifstream instrm(infile);  //"stylus-osm.yaml"); //argv[1]);
    std::stringstream buffer;
    buffer << instrm.rdbuf();

    //size_t ext = in.find_last_of('.');
    //std::ofstream outstr(infile.substr(0, ext) + );
    bool isJson = infile.substr(infile.size() - 5) == ".json";

    YAML::Document doc = YAML::parse(buffer.str(), isJson ? YAML::PARSE_JSON : 0);

    YAML::Writer writer;
    writer.indent = isJson ? 0 : 4;
    writer.extraLines = 1;
    std::string out = writer.convert(*doc.value);
    puts(out.c_str());
    return 0;
}

#endif
