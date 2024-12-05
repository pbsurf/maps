#include "gason.h"
#include <stdlib.h>

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

static double string2double(char *s, char **endptr) {
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

    *endptr = s;
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

static inline char escapedChar(char c) {
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

int jsonParse(char *s, char **endptr, JsonValue *value, JsonAllocator &allocator, int flags) {
    JsonNode* tails[JSON_STACK_SIZE];
    JsonTag tags[JSON_STACK_SIZE];
    char* keys[JSON_STACK_SIZE];
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
            o = JsonValue(JSON_STRING, s);
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
                    } else if (!(*it = escapedChar(c))) {
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
                o = JsonValue(YAML_COMMENT, s);
            }
            while (*s != '\r' && *s != '\n') { ++s; }
            *s = 0;  // terminate string
            break;

        case '\'':
            o = JsonValue(YAML_SINGLEQUOTED, s);
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


        // Special characters
        case '|':  // literal scalar
        case '>':  // folder block scalar
        {
            char chomp = *s;
            if (!isspace(chomp)) { ++s; }

            o = JsonValue(YAML_UNQUOTED, s);

            unsigned int blockindent = 0;

            linestart = nullptr;

            while (*s) {

                if (*s == '\n') {
                    if (linestart && !blockindent) { o.str.push_back('\n'); }  // leading blank line
                    linestart = s+1;
                }
                if (isspace(*s) && (++s) - linestart < blockindent) { continue; }

                if (!linestart) { return JSON_BAD_STRING; }
                if (s - linestart <= indent) { break; }

                if (!blockindent) { blockindent = s - linestart; }
                else if (s - linestart < blockindent) { return JSON_BAD_STRING; }

                while (*s && *s != '\r' && *s != '\n') { o.str.append(*s++); }

                o.str.append(nextchar == '|' ? '\n' : ' ');
            }

            if (chomp == '-') {
                o.str.pop_back();
            } else if (chomp != '+') {
                while (o.str.back() == '\n') { o.str.pop_back(); }
                o.str.push_back('\n');
            }
        }

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
            o = JsonValue(YAML_UNQUOTED, s);
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
            if (strcmp(o.toString(), "true") == 0) {
                o = JsonValue(JSON_TRUE);
            } else if (strcmp(o.toString(), "false") == 0) {
                o = JsonValue(JSON_FALSE);
            } else if (strcmp(o.toString(), "null") == 0) {
                o = JsonValue(JSON_NULL);
            } else {
                // try to parse as number
                char* endnum;
                o = JsonValue(string2double(o.toString(), &endnum));
                if(*endnum != '\0') { return JSON_BAD_NUMBER; }
            }
        }


        if (pos == -1) {
            *endptr = s;
            *value = o;
            return JSON_OK;
        }

        if (tags[pos] == JSON_OBJECT) {
            if (!keys[pos]) {
                if (o.getTag() != JSON_STRING)
                    return JSON_UNQUOTED_KEY;
                keys[pos] = o.toString();
                continue;
            }
            if ((node = (JsonNode *) allocator.allocate(sizeof(JsonNode))) == nullptr)
                return JSON_ALLOCATION_FAILURE;
            tails[pos] = insertAfter(tails[pos], node);
            tails[pos]->key = keys[pos];
            keys[pos] = nullptr;
        } else {
            if ((node = (JsonNode *) allocator.allocate(sizeof(JsonNode) - sizeof(char *))) == nullptr)
                return JSON_ALLOCATION_FAILURE;
            tails[pos] = insertAfter(tails[pos], node);
        }
        tails[pos]->value = o;
    }
    return JSON_BREAKING_BAD;
}
