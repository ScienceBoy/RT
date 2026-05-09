#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

struct XMLNode
{
    std::string name;
    std::map<std::string, std::string> attr;
    std::vector<std::unique_ptr<XMLNode>> children;
    std::string text;
};

class SimpleXML
{
public:
    static std::unique_ptr<XMLNode> parse(const std::string& data);

private:
    static void skipSpaces(const std::string& s, size_t& i);
    static std::string parseName(const std::string& s, size_t& i);
    static std::string parseAttrValue(const std::string& s, size_t& i);

    static std::unique_ptr<XMLNode> parseNode(const std::string& s, size_t& i);
};