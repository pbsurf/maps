#include "gason.h"
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <set>
#include <utility>

#define JSON_ZONE_SIZE 4096
#define JSON_STACK_SIZE 32

const char *jsonStrError(int err) {
    switch (err) {
#define XX(no, str) \
    case JSON_##no: \
        return str;
        JSON_ERRNO_MAP(XX)
#undef XX
    default:
        return "unknown";
    }
}

void *JsonAllocator::allocate(size_t size) {
    size = (size + 7) & ~7;

    if (head && head->used + size <= JSON_ZONE_SIZE) {
        char *p = (char *)head + head->used;
        head->used += size;
        return p;
    }

    size_t allocSize = sizeof(Zone) + size;
    Zone *zone = (Zone *)malloc(allocSize <= JSON_ZONE_SIZE ? JSON_ZONE_SIZE : allocSize);
    if (zone == nullptr)
        return nullptr;
    zone->used = allocSize;
    if (allocSize <= JSON_ZONE_SIZE || head == nullptr) {
        zone->next = head;
        head = zone;
    } else {
        zone->next = head->next;
        head->next = zone;
    }
    return (char *)zone + sizeof(Zone);
}

void JsonAllocator::deallocate() {
    while (head) {
        Zone *next = head->next;
        free(head);
        head = next;
    }
}

// Node

static JsonValue UNDEFINED_VALUE(UNDEFINED);
static JsonValue INVALID_VALUE(INVALID);

JsonNode::~JsonNode() {
    while(next) {
        delete std::exchange(next, next->next);
    }
}

SettingNode SettingNode::operator[](const char* key) {
    Node n = Node::operator[](key);
    if (n.value == &UNDEFINED_VALUE) {
        if(value->getTag() == UNDEFINED) {
            value->tag_ = JSON_OBJECT;
        }
        auto* val = new JsonNode{UNDEFINED, nullptr, key};
        if(!value->getNode()) {
            value->pval_ = val;
        } else {
            JsonNode* obj = value->getNode();
            while(obj->next) { obj = obj->next; }
            obj->next = val;
        }
        return SettingNode(&val->value);
    }
    return SettingNode(n.value);
}

Node Node::operator[](const char* key) {
    if (value->getTag() == UNDEFINED) { return Node(&UNDEFINED_VALUE); }
    if (value->getTag() != JSON_OBJECT) { return Node(&INVALID_VALUE); }
    JsonNode* obj = value->pval_;
    while(obj && obj->key != key) {
      obj = obj->next;
    }
    return obj ? obj->node() : Node(&UNDEFINED_VALUE);
}

SettingNode SettingNode::operator[](size_t idx) {
    Node n = Node::operator[](idx);
    if (n.value == &UNDEFINED_VALUE) {
        if(value->getTag() == UNDEFINED) {
            value->tag_ = JSON_ARRAY;
        }
        auto* val = new JsonNode{UNDEFINED, nullptr, {}};
        if(!value->getNode()) {
            value->pval_ = val;
        } else {
            JsonNode* obj = value->getNode();
            while(obj->next) { obj = obj->next; }
            obj->next = val;
        }
        return SettingNode(&val->value);
    }
    return SettingNode(n.value);
}

Node Node::operator[](size_t idx) {
    if (value->getTag() == UNDEFINED) { return Node(&UNDEFINED_VALUE); }
    if (value->getTag() != JSON_ARRAY) { return Node(&INVALID_VALUE); }
    JsonNode* array = value->getNode();
    while (array && idx--) { array = array->next; }
    return array ? array->node() : Node(&UNDEFINED_VALUE);
}

void Node::push_back(JsonValue&& val) {
    if (value->getTag() == UNDEFINED) { value->tag_ = JSON_ARRAY; }
    else if (value->getTag() != JSON_ARRAY) { return; }
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
    return c == ',' || c == ':' || c == ']' || c == '}' || isspace(c)
#if ENABLE_YAML
        || c == '#'
#endif
        || !c;
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

static inline JsonValue listToValue(JsonTag tag, JsonNode *tail) {
    if (tail) {
        auto head = tail->next;
        tail->next = nullptr;
        return JsonValue(tag, head);
    }
    return JsonValue(tag, nullptr);
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

int jsonParse(char *s, char **endptr, JsonValue *value, JsonAllocator &allocator, int flags) {
    JsonNode* tails[JSON_STACK_SIZE];
    JsonTag tags[JSON_STACK_SIZE];
    const char* keys[JSON_STACK_SIZE];
    int indents[JSON_STACK_SIZE];

    JsonValue o;
    int pos = -1;
    int indent = 0;
    bool separator = true;
    bool unquoted = false;
    char nextchar;
    JsonNode *node;
    char* linestart = s;
    *endptr = s;

    while (*s) {

        isflow = indent[pos] == indent[pos-1];

        while (isspace(*s)) {
            if (!isflow && *s == '\n') {
                linestart = s+1;
            }
            ++s;
        }
        if (!*s) { break; }
        if (linestart) {
            indent = s - linestart;
        }
        if (pos >= 0 && indent > indents[pos]) {
            nextchar = (*s == '-' && isspace(*(s+1))) ? '[' : '{';
        } else if (pos >= 0 && indent < indents[pos]) {
            nextchar = tags[pos] == JSON_ARRAY ? ']' : '}';
        } else {
            *endptr = s;
            nextchar = *s++;
            if (unquoted) {
                **endptr = 0;
                unquoted = false;
            }
        }

        switch (nextchar) {
        case '"':
            o = JsonValue(s, JSON_STRING);
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
                    *it = 0;
                    ++s;
                    break;
                }
            }
            if (!isdelim(*s)) {
                *endptr = s;
                return JSON_BAD_STRING;
            }
            break;
        case ']':
            if (pos == -1)
                return JSON_STACK_UNDERFLOW;
            if (tags[pos] != JSON_ARRAY)
                return JSON_MISMATCH_BRACKET;
            o = listToValue(JSON_ARRAY, tails[pos--]);
            break;
        case '}':
            if (pos == -1)
                return JSON_STACK_UNDERFLOW;
            if (tags[pos] != JSON_OBJECT)
                return JSON_MISMATCH_BRACKET;
            if (keys[pos] != nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            o = listToValue(JSON_OBJECT, tails[pos--]);
            break;
        case '[':
            if (++pos == JSON_STACK_SIZE)
                return JSON_STACK_OVERFLOW;
            tails[pos] = nullptr;
            tags[pos] = JSON_ARRAY;
            keys[pos] = nullptr;
            indents[pos] = indent;
            separator = true;
            continue;
        case '{':
            if (++pos == JSON_STACK_SIZE)
                return JSON_STACK_OVERFLOW;
            tails[pos] = nullptr;
            tags[pos] = JSON_OBJECT;
            keys[pos] = nullptr;
            indents[pos] = indent;
            separator = true;
            continue;
        case ':':
            if (separator || keys[pos] == nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            separator = true;
            continue;
        case ',':
            if (separator || keys[pos] != nullptr)
                return JSON_UNEXPECTED_CHARACTER;
            separator = true;
            continue;
        case '\0':
            continue;


#if ENABLE_YAML

        case '#':  // comment
            if (flags & PARSE_COMMENTS) {
                o = JsonValue(s, YAML_COMMENT);
            }
            while (*s != '\r' && *s != '\n') { ++s; }
            *s = 0;  // terminate string
            break;

        case '\'':
            o = JsonValue(s, YAML_SINGLEQUOTED);
            for (char *it = s; *s; ++it, ++s) {
                int c = *it = *s;
                if ((unsigned int)c < ' ' || c == '\x7F') {
                    *endptr = s;
                    return JSON_BAD_STRING;
                } else if (c == '\'') {
                    // only escape sequence allowed in single quoted strings is '' -> '
                    if (*++s != '\'') {
                        *it = 0;
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

            o = JsonValue(YAML_BLOCKSTRING);
            std::string& str = o.getString();

            unsigned int blockindent = 0;

            linestart = nullptr;

            while (*s) {

                if (*s == '\n') {
                    if (linestart) { str.push_back('\n'); }  // blank lines
                    linestart = s+1;
                }
                if (isspace(*s) && (++s) - linestart < blockindent) { continue; }

                if (!linestart) { return JSON_BAD_STRING; }
                if (s - linestart <= indent) { break; }

                if (!blockindent) { blockindent = s - linestart; }
                else if (s - linestart < blockindent) { return JSON_BAD_STRING; }

                char* s0 = s;
                while (*s && *s != '\r' && *s != '\n') { ++s; }
                str.insert(s0, s);

                str.append(nextchar == '|' ? '\n' : ' ');
                linestart = nullptr;
            }

            if (chomp == '-') {
                str.pop_back();
            } else if (chomp != '+') {
                while (str.back() == '\n') { str.pop_back(); }
                str.push_back('\n');
            }

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

        case '-':
            if (linestart && isspace(*s)) {
              ++s;
              continue;
            }
        default:  // unquoted string
            o = JsonValue(s, YAML_UNQUOTED);
            if (!isflow && keys[pos]) {
                while(*s && *s != '\r' && *s != '\n') { ++s; }
            } else {
                while(!isendscalar(*s)) { ++s; }
            }
            if (isspace(*s)) {
                *s++ = 0;
            } else {
                unquoted = true;
            }
            break;
#else
        default:
            return JSON_UNEXPECTED_CHARACTER;
#endif
        }

        linestart = nullptr;
        separator = false;


        if ((flags & PARSE_NUMBERS) && o.getTag() == YAML_UNQUOTED) {
            if (o.getString() == "true") {
                o = JsonValue(JSON_BOOL, 1);
            } else if (o.getString() == "false") {
                o = JsonValue(JSON_BOOL, 0);
            } else if (o.getString() == "null") {
                o = JsonValue(JSON_NULL);
            } else {
                // try to parse as number
                char* endnum;
                o = JsonValue(string2double(o.getCStr(), &endnum));
                if(*endnum != '\0') { return JSON_BAD_NUMBER; }
            }
        }


        if (pos == -1) {
            *endptr = s;
            *value = std::move(o);
            return JSON_OK;
        }

        if (tags[pos] == JSON_OBJECT) {
            if (!keys[pos]) {
                if (o.getTag() != JSON_STRING)
                    return JSON_UNQUOTED_KEY;
                keys[pos] = o.getString().c_str();
                continue;
            }
            //if ((node = (JsonNode *) allocator.allocate(sizeof(JsonNode))) == nullptr)
            //    return JSON_ALLOCATION_FAILURE;
            node = new JsonNode{std::move(o), nullptr, keys[pos]};
            tails[pos] = insertAfter(tails[pos], node);
            //tails[pos]->key = keys[pos];
            keys[pos] = nullptr;
        } else {
            //if ((node = (JsonNode *) allocator.allocate(sizeof(JsonNode) - sizeof(char *))) == nullptr)
            //    return JSON_ALLOCATION_FAILURE;
            node = new JsonNode{std::move(o), nullptr, {}};
            tails[pos] = insertAfter(tails[pos], node);
        }
        //tails[pos]->value = std::move(o);
    }
    return JSON_BREAKING_BAD;
}

// Writer

struct JsonWriter {
    char quote = '"';
    int indent = 2;  // size (i.e. number of spaces) of each indent step
    int flowLevel = 10; // switch to flow style beyond this indentation level
    int extraLines = 0;  // add (extraLines - level) lines between map/hash blocks
    std::set<std::string> alwaysFlow; // always use flow style for specified key names

    std::string spacing(int level);
    std::string keyString(std::string str);
    std::string convertArray(const JsonValue& obj, int level);
    std::string convertHash(const JsonValue& obj, int level);

    std::string convert(const JsonValue& obj, int level = 0);
};

static std::string escapeSingleQuoted(const char* s) {
    std::string res = "'";
    for (; *s; ++s) {
        res.push_back(*s);
        if(*s == '\'') { res.push_back('\''); }
    }
    return res.append("'");
}

static std::string escapeDoubleQuoted(const char* s) {
    std::string res = "\"";
    for (; *s; ++s) {
        const char* esc = escapedChar(*s);
        if (esc) { res.append(esc); } else { res.push_back(*s); }
    }
    return res.append("\"");
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

std::string JsonWriter::spacing(int level) {
    return level < flowLevel ? std::string(' ', indent*level) : "";
}

// quote key string if necessary
std::string JsonWriter::keyString(std::string str) {
    static std::string special("!&*-:?{}[],#|>@`\"'%");
    if(isspace(str[0]) || special.find_first_of(str[0]) != std::string::npos
            || str.find_first_of(":#") != std::string::npos) {
        return quote == '"' ? escapeDoubleQuoted(str.c_str()) : escapeSingleQuoted(str.c_str());
    }
    return str;
}

std::string JsonWriter::convertArray(const JsonValue& obj, int level) {
    std::vector<std::string> res;
    if(level >= flowLevel) {
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

std::string JsonWriter::convertHash(const JsonValue& obj, int level) {
    std::vector<std::string> res;
    for(auto item : obj) {
        const std::string& key = item->key;
        JsonValue& val = item->value;
        bool isScalar = val.getTag() != JSON_OBJECT && val.getTag() == JSON_ARRAY;
        if (isScalar || val.isEmpty() || level+1 >= flowLevel || alwaysFlow.find(key) != alwaysFlow.end()) {
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


std::string JsonWriter::convert(const JsonValue& obj, int level) {
    switch(obj.getTag()) {
    case JSON_ARRAY:
        return convertArray(obj, level);
    case JSON_OBJECT:
        return convertHash(obj, level);
    case YAML_UNQUOTED:
        return obj.getString();
    case JSON_STRING:
        return escapeDoubleQuoted(obj.getCStr());
    case YAML_SINGLEQUOTED:
        return escapeSingleQuoted(obj.getCStr());
    case YAML_BLOCKSTRING:
    {
        std::string res = "|\n";
        for(const char* s = obj.getCStr(); *s; ++s) {
            res.push_back(*s);
            if(*s == '\n' && s[1]) {
                res.append(spacing(level));
            }
        }
        return res;
    }
    case JSON_NULL:
        return "null";
    case JSON_NUMBER:
        return std::to_string(obj.getNumber());
    case JSON_BOOL:
        return obj.getPayload() ? "true" : "false";
    }
}
