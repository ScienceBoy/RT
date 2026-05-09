#include "SimpleXML.h"

#include <cctype>
#include <stdexcept>

void SimpleXML::skipSpaces(const std::string& s, size_t& i)
{
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i])))
        i++;
}

std::string SimpleXML::parseName(const std::string& s, size_t& i)
{
    std::string name;

    while (i < s.size())
    {
        char c = s[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
        {
            name += c;
            i++;
        }
        else break;
    }

    return name;
}

std::string SimpleXML::parseAttrValue(const std::string& s, size_t& i)
{
    std::string value;

    while (i < s.size() && s[i] != '"') i++;
    i++; // "

    while (i < s.size() && s[i] != '"')
    {
        value += s[i++];
    }

    i++; // "

    return value;
}

std::unique_ptr<XMLNode> SimpleXML::parseNode(const std::string& s, size_t& i)
{
    skipSpaces(s, i);

    // ===============================
    // 1. COMMENT SKIP: <!-- ... -->
    // ===============================
    if (i + 4 < s.size() &&
        s[i] == '<' && s[i+1] == '!' && s[i+2] == '-' && s[i+3] == '-')
    {
        i += 4;

        while (i + 2 < s.size() &&
              !(s[i] == '-' && s[i+1] == '-' && s[i+2] == '>'))
        {
            i++;
        }

        if (i + 2 < s.size())
            i += 3; // skip "-->"

        return parseNode(s, i); // next real node
    }

    // ===============================
    // 2. MUST START WITH '<'
    // ===============================
    if (i >= s.size() || s[i] != '<')
        return nullptr;

    i++; // skip '<'

    // ===============================
    // 3. CLOSE TAG CHECK </...>
    // ===============================
    if (i < s.size() && s[i] == '/')
        return nullptr;

    auto node = std::make_unique<XMLNode>();

    // ===============================
    // 4. NODE NAME
    // ===============================
    node->name = parseName(s, i);

    skipSpaces(s, i);

    // ===============================
    // 5. ATTRIBUTES
    // ===============================
    while (i < s.size() && s[i] != '>' && s[i] != '/')
    {
        std::string key = parseName(s, i);

        skipSpaces(s, i);

        if (i >= s.size() || s[i] != '=')
            break;

        i++; // '='
        skipSpaces(s, i);

        std::string value = parseAttrValue(s, i);

        node->attr[key] = value;

        skipSpaces(s, i);
    }

    // ===============================
    // 6. SELF CLOSING TAG <tag/>
    // ===============================
    if (i < s.size() && s[i] == '/')
    {
        while (i < s.size() && s[i] != '>')
            i++;

        if (i < s.size())
            i++;

        return node;
    }

    // ===============================
    // 7. CLOSE START TAG '>'
    // ===============================
    if (i < s.size() && s[i] == '>')
        i++;

    // ===============================
    // 8. CHILDREN / TEXT
    // ===============================
    while (i < s.size())
    {
        skipSpaces(s, i);

        // END TAG </name>
        if (i + 1 < s.size() &&
            s[i] == '<' && s[i+1] == '/')
        {
            i += 2;

            parseName(s, i); // skip name

            while (i < s.size() && s[i] != '>')
                i++;

            if (i < s.size())
                i++;

            break;
        }

        // CHILD NODE
        if (i < s.size() && s[i] == '<')
        {
            auto child = parseNode(s, i);
            if (child)
                node->children.push_back(std::move(child));
        }
        else
        {
            // TEXT CONTENT
            node->text += s[i++];
        }
    }

    return node;
}

std::unique_ptr<XMLNode> SimpleXML::parse(const std::string& data)
{
    size_t i = 0;

    // 🔥 SKIP WHITESPACE / BOM
    while (i < data.size() &&
           (data[i] == ' ' || data[i] == '\n' || data[i] == '\r' || data[i] == '\t'))
        i++;

    // optional BOM skip
    if (i + 3 < data.size() &&
        (unsigned char)data[i] == 0xEF &&
        (unsigned char)data[i+1] == 0xBB &&
        (unsigned char)data[i+2] == 0xBF)
        i += 3;

    auto root = parseNode(data, i);

    if (!root)
        throw std::runtime_error("XML Parse Error: Invalid root node");

    return root;
}