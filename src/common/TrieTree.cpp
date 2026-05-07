#include "../include/TrieTree.h"

TrieTree::TrieTree()
    : _root(new TrieNode())
{
}

TrieTree::~TrieTree()
{
    destroy(_root);
    _root = nullptr;
}

void TrieTree::destroy(TrieNode *node)
{
    if (!node)
        return;

    for (auto &kv : node->children)
    {
        destroy(kv.second);
    }

    delete node;
}

size_t TrieTree::nBytesCode(unsigned char ch)
{
    if (ch & (1 << 7))
    {
        size_t nBytes = 1;
        for (int idx = 0; idx != 6; ++idx)
        {
            if (ch & (1 << (6 - idx)))
            {
                ++nBytes;
            }
            else
            {
                break;
            }
        }
        return nBytes;
    }
    return 1;
}

std::vector<std::string> TrieTree::splitUtf8(const std::string &str) const
{
    std::vector<std::string> result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size();)
    {
        size_t n = nBytesCode(static_cast<unsigned char>(str[i]));

        if (n == 0 || i + n > str.size())
        {
            n = 1;
        }

        result.emplace_back(str.substr(i, n));
        i += n;
    }

    return result;
}

void TrieTree::insert(const std::string &word, int dict_index)
{
    std::vector<std::string> chars = splitUtf8(word);

    TrieNode *cur = _root;

    for (const auto &ch : chars)
    {
        auto it = cur->children.find(ch);
        if (it == cur->children.end())
        {
            TrieNode *new_node = new TrieNode();
            cur->children[ch] = new_node;
            cur = new_node;
        }
        else
        {
            cur = it->second;
        }

        cur->indices.insert(dict_index);
    }
}

std::set<int> TrieTree::searchPrefix(const std::string &prefix)
{
    std::vector<std::string> chars = splitUtf8(prefix);

    TrieNode *cur = _root;

    for (const auto &ch : chars)
    {
        auto it = cur->children.find(ch);
        if (it == cur->children.end())
        {
            return {};
        }
        cur = it->second;
    }

    return cur->indices;
}
