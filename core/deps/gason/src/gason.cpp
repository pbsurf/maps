#include "gason.h"
#include <stdlib.h>
#include <string.h>
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

// Node

static JsonValue UNDEFINED_VALUE(Tag::UNDEFINED);
static JsonValue INVALID_VALUE(Tag::INVALID);

JsonValue::~JsonValue() {
    if ((getTag() == Tag::ARRAY || getTag() == Tag::OBJECT) && pval_) { delete pval_; }
    pval_ = nullptr;
}

JsonValue& JsonValue::operator=(JsonValue&& b) {
    std::swap(pval_, b.pval_);
    std::swap(flags_, b.flags_);
    std::swap(strVal, b.strVal);
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
}

JsonNode::~JsonNode() {
    while(next) { delete std::exchange(next, next->next);  }
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
    while (array && idx--) { array = array->next; }
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
    if(!isString()) { return _default; }
    char* endptr;
    auto s = value->getString();
    double val = s[0] == '0' ? strtoul(s.c_str(), &endptr, 0) : string2double(s.c_str(), &endptr);
    return *endptr ? _default : val;  //endptr - s == strlen(s) ? val : _default;
}

template<> std::string Node::as(const std::string& _default) {
    if(value->isNumber()) { return std::to_string(value->getNumber()); }
    return value->getString()isString() ? toString() : _default;
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

static inline JsonNode *insertAfter(JsonNode *tail, JsonNode *node) {
    if (!tail)
        return node->next = node;
    node->next = tail->next;
    tail->next = node;
    return node;
}

static inline JsonValue listToValue(Tag tag, JsonNode *tail) {
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
        case '/':   return "\\\/";
        case '\b':  return "\\b";
        case '\f':  return "\\f";
        case '\n':  return "\\n";
        case '\r':  return "\\r";
        case '\t':  return "\\t";
    }
    return nullptr;
}

const char *jsonStrError(int err) {
    static const char* errorMsg[] = {"ok", "bad number", "bad string", "bad identifier", "stack overflow",
        "stack underflow", "mismatch bracket", "unexpected character", "unquoted key", "breaking bad",
        "allocation failure", "invalid error code"};
    return errorMsg[std::min(size_t(err), size_t(JSON_ERROR_COUNT))];
}

Document parse(const std::string& s, int flags, int* resultout) { parse(s.c_str(), flags, resultout); }

Document parse(const char* s, int flags, int* resultout) {
    Document doc;
    char* endptr;
    int res = parseTo((char*)s, &endptr, doc.value, flags);
    if (resultout) { *resultout = res; }
    return doc;
}

int parseTo(char *s, char **endptr, JsonValue *valueout, int flags) {
    constexpr size_t JSON_STACK_SIZE = 32;
    JsonNode* tails[JSON_STACK_SIZE];
    Tag tags[JSON_STACK_SIZE];
    JsonValue keys[JSON_STACK_SIZE];
    int indents[JSON_STACK_SIZE];

    JsonValue o(Tag::UNDEFINED);
    int pos = -1;
    int indent = 0;
    int flowlevel = 0;
    bool separator = true;
    char nextchar;
    JsonNode *node;
    std::string temp;
    char* s0;
    char* linestart = s;
    *endptr = s;

    while (*s) {
        while (isspace(*s)) {
            if (!flowlevel && *s == '\n') { linestart = s+1; }
            ++s;
        }
        if (!*s) { break; }

        if (linestart) { indent = s - linestart; }
        if (*s == '{' || *s == '[') { ++flowlevel; }

        if (!flowlevel && (pos < 0 || indent > indents[pos])) {
            nextchar = (*s == '-' && isspace(s[1])) ? '[' : '{';
        } else if (!flowlevel && (pos < 0 || indent < indents[pos])) {
            nextchar = tags[pos] == Tag::ARRAY ? ']' : '}';
        } else {
            *endptr = s0 = s;
            nextchar = *s++;
        }

        switch (nextchar) {
        case '"':
            for (char *it = s; *s; ++it, ++s) {
                int c = *it = *s;
                if (c == '\\') {
                    c = *++s;
                    if (c == 'u') {
                        c = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (isxdigit(*++s)) {
                                c = c * 16 + char2int(*s);
                            } else {
                                *endptr = s;
                                return JSON_BAD_STRING;
                            }
                        }
                        if (c < 0x80) {
                            *it = c;
                        } else if (c < 0x800) {
                            *it++ = 0xC0 | (c >> 6);
                            *it = 0x80 | (c & 0x3F);
                        } else {
                            *it++ = 0xE0 | (c >> 12);
                            *it++ = 0x80 | ((c >> 6) & 0x3F);
                            *it = 0x80 | (c & 0x3F);
                        }
                    } else if (!(*it = unescapedChar(c))) {
                        *endptr = s;
                        return JSON_BAD_STRING;
                    }
                } else if ((unsigned int)c < ' ' || c == '\x7F') {
                    *endptr = s;
                    return JSON_BAD_STRING;
                } else if (c == '"') {
                    o = JsonValue(std::string(s0, it), Tag::YAML_DBLQUOTED);  //*it = 0;
                    ++s;
                    break;
                }
            }
            if (!isdelim(*s)) {
                *endptr = s;
                return JSON_BAD_STRING;
            }
            break;
        case '[':
        case '{':
            if (++pos == JSON_STACK_SIZE)
                return JSON_STACK_OVERFLOW;
            tails[pos] = nullptr;
            tags[pos] = (nextchar == '{' ? Tag::OBJECT : Tag::ARRAY);
            keys[pos] = JsonValue();  //nullptr;
            indents[pos] = indent;
            separator = true;
            continue;
        case ']':
        case '}':
            if (pos == -1)
                return JSON_STACK_UNDERFLOW;
            if (tags[pos] != (nextchar == '}' ? Tag::OBJECT : Tag::ARRAY))
                return JSON_MISMATCH_BRACKET;
            if (nextchar == '}' && keys[pos])  // != nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            if (flowlevel > 0) {
                tags[pos] = tags[pos] | Tag::YAML_FLOW;
                --flowlevel;
            }
            o = listToValue(tags[pos], tails[pos]);
            --pos;
            break;
        case ':':
            if (separator || !keys[pos])  // == nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            separator = true;
            continue;
        case ',':
            if (separator || keys[pos])  // != nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            separator = true;
            continue;
        case '\0':
            continue;
        // YAML only below
        case '#':  // comment
            while (*s != '\r' && *s != '\n') { ++s; }
            if (flags & PARSE_COMMENTS) {
                o = JsonValue(std::string(s0, s), Tag::YAML_COMMENT);  //*s = 0;
            }
            break;
        case '\'':  // single quoted string
            for (char *it = s; *s; ++it, ++s) {
                int c = *it = *s;
                if ((unsigned int)c < ' ' || c == '\x7F') {
                    *endptr = s;
                    return JSON_BAD_STRING;
                } else if (c == '\'') {
                    // only escape sequence allowed in single quoted strings is '' -> '
                    if (*++s != '\'') {
                        o = JsonValue(std::string(s0, it), Tag::YAML_SINGLEQUOTED); //*it = 0;
                        break;
                    }
                }
            }
            if (!isdelim(*s)) {
                *endptr = s;
                return JSON_BAD_STRING;
            }
            break;
        case '|':  // literal block scalar
        case '>':  // folded block scalar
        {
            char chomp = *s;
            if (!isspace(chomp)) { ++s; }

            unsigned int blockindent = 0;
            linestart = nullptr;
            while (*s) {
                if (*s == '\n') {
                    if (linestart) { temp.push_back('\n'); }  // blank lines
                    linestart = s+1;
                }
                if (isspace(*s) && (++s) - linestart < blockindent) { continue; }

                if (!linestart) { return JSON_BAD_STRING; }
                if (s - linestart <= indent) { break; }

                if (!blockindent) { blockindent = s - linestart; }
                else if (s - linestart < blockindent) { return JSON_BAD_STRING; }

                char* s0 = s;
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
            break;
        }
        // Unsupported YAML features
        case '?':  // mapping key
        case '&':  // node anchor
        case '*':  // alias
        case '!':  // tag handle
        case '@':  // reserved
        case '`':  // reserved
            return JSON_UNEXPECTED_CHARACTER;
        case '-':  // '-' could be array element, document separator, unquoted string, or number
            if (linestart && isspace(*s)) {
                ++s;
                continue;
            }
            // '---' separates multiple documents in a single stream
            if (linestart && indent == 0 && *s == '-' && s[1] == '-') {
                *endptr = s+2;
                return JSON_OK;
            }
            [[fallthrough]];
        default:  // unquoted string
            if (!flowlevel && keys[pos]) {
                while(*s && *s != '\r' && *s != '\n') { ++s; }
            } else {
                while(!isendscalar(*s)) { ++s; }
            }
            o = JsonValue(std::string(s0, s++), Tag::YAML_UNQUOTED);
            break;
        }

        linestart = nullptr;
        separator = false;

        // check for invalid JSON if requested
        if ((flags & PARSE_JSON) && o.getTag() == Tag::STRING) {
            Tag t = o.getFlags() & Tag::YAML_STRINGMASK;
            if (t == Tag::YAML_SINGLEQUOTED || t == Tag::YAML_BLOCKSTRING || t == Tag::YAML_COMMENT) {
                return JSON_UNEXPECTED_CHARACTER;
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
                    o = JsonValue(string2double(o.getCStr(), &endnum));
                    if(*endnum != '\0') { return JSON_BAD_NUMBER; }
                }
            }
        }


        if (pos == -1) {
            *endptr = s;
            *valueout = std::move(o);
            return JSON_OK;
        }

        if (tags[pos] == Tag::OBJECT) {
            if (!keys[pos]) {
                if (o.getTag() != Tag::STRING)
                    return JSON_UNQUOTED_KEY;
                keys[pos] = std::move(o);  //o.getString().c_str();
                continue;
            }
            node = new JsonNode{std::move(o), nullptr, keys[pos].getString()};
            tails[pos] = insertAfter(tails[pos], node);
            keys[pos] = JsonValue();  //nullptr;
        } else {
            node = new JsonNode{std::move(o), nullptr, {}};
            tails[pos] = insertAfter(tails[pos], node);
        }
    }
    return JSON_BREAKING_BAD;
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
    std::string res = "|\n";
    for(const char* s = str.data(); *s; ++s) {
        res.push_back(*s);
        if(*s == '\n' && s[1]) {
            res.append(indent);
        }
    }
    return res;
}

static std::string strJoin(const std::vector<std::string>& strs, const std::string& sep) {
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
    return indent && level < flowLevel ? std::string(' ', indent*level) : "";
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
    if(!indent || level >= flowLevel || (obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) {
        for(auto item : obj) {
            res.push_back(convert(item->value, flowLevel));
        }
        return "[" + strJoin(res, ", ") + "]";
    } else {
        // always use flow for nested array (for now)
      for(auto item : obj) {
            res.push_back(spacing(level) + "- " + convert(item->value, flowLevel));
        }
        return res.empty() ? "[]" : strJoin(res, "\n");
    }
}

std::string Writer::convertHash(const JsonValue& obj, int level) {
    std::vector<std::string> res;
    if((obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) { level = flowLevel; }
    for(auto item : obj) {
        const std::string& key = item->key;
        JsonValue& val = item->value;
        bool isScalar = val.getTag() != Tag::OBJECT && val.getTag() == Tag::ARRAY;
        if (val.getTag() == Tag::YAML_COMMENT) {
            res.push_back(convert(val, level+1));
        } else if (!indent || level+1 >= flowLevel || isScalar || !val.getNode()) {
            res.push_back(spacing(level) + keyString(key) + ": " + convert(val, flowLevel));
        } else {
            res.push_back(spacing(level) + keyString(key) + ":\n" + convert(val, level+1));
        }
    }
    if (res.empty())
        return "{}";
    if (level >= flowLevel)
        return "{ " + strJoin(res, ", ") + " }";
    return strJoin(res, std::string('\n', std::max(1, 1+extraLines - level)));
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
        case Tag::YAML_UNQUOTED:
            return escapeUnQuoted(obj.getString(), level < flowLevel, quote);
        case Tag::YAML_DBLQUOTED:
            return escapeDoubleQuoted(obj.getString());
        case Tag::YAML_SINGLEQUOTED:
            return escapeSingleQuoted(obj.getString());
        case Tag::YAML_BLOCKSTRING:
            return blockString(obj.getString(), spacing(level));
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

  assert(doc["a"]["b"].Scalar() == "this is a.b");
  assert(doc["b"].as<double>() == 5.6);
  return doc;
}

int main(int argc, char **argv) {

  /*
    if (argc < 2) {
        fprintf(stderr, "No input file specified!");
        return -1;
    }

    std::ifstream instrm(argv[1]);
    std::stringstream buffer;
    buffer << instrm.rdbuf();

    std::string infile(argv[1]);
    //size_t ext = in.find_last_of('.');
    //std::ofstream outstr(infile.substr(0, ext) + );

    YAML::Document doc = YAML::parse(buffer.str());
    */

    YAML::Document doc = basicTests();

    YAML::Writer writer;
    //if (infile.substr(infile.size() - 5) == ".json") { writer.indent = 0; }
    std::string out = writer.convert(*doc.value);
    puts(out.c_str());
    return 0;
}

#endif
