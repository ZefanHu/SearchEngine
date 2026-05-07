#ifndef __TRIE_TREE_H__
#define __TRIE_TREE_H__

#include <unordered_map>
#include <set>
#include <string>
#include <vector>

class TrieNode
{
public:
    std::unordered_map<std::string, TrieNode *> children;
    std::set<int> indices;
};

class TrieTree
{
public:
    TrieTree();
    ~TrieTree();

    TrieTree(const TrieTree &) = delete;
    TrieTree &operator=(const TrieTree &) = delete;
    TrieTree(TrieTree &&) = delete;
    TrieTree &operator=(TrieTree &&) = delete;

    void insert(const std::string &word, int dict_index);

    std::set<int> searchPrefix(const std::string &prefix);

private:
    std::vector<std::string> splitUtf8(const std::string &str) const;

    static size_t nBytesCode(unsigned char ch);

    void destroy(TrieNode *node);

    TrieNode *_root;
};

#endif
