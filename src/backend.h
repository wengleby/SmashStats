#pragma once
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

// --- Added for row counter ---
extern std::atomic<size_t> g_backendRowsVisited;
size_t Backend_GetTotalRowsVisited();

struct PlayerRecord {
    std::string id;
    std::string name;
    std::string main_character;
    int matches_played = -1;
    int matches_won = -1;
    double win_rate = -1.0;
    bool stats_loaded = false;
};

class PlayerHashTable;
class PlayerTrie;

bool BackendDB_LoadAllPlayers(const std::string& db_path, PlayerHashTable& hash, PlayerTrie& trie);
bool BackendDB_LoadAllPlayers(const std::string& db_path, PlayerHashTable& hash);
bool BackendDB_LoadAllPlayers(const std::string& db_path, PlayerTrie& trie);
// Call after loading players:
bool BackendDB_LoadPlayerStats(const std::string& db_path, PlayerHashTable& hash, PlayerTrie& trie);

class PlayerHashTable {
public:
    PlayerHashTable(size_t init_size = 131072);
    void Insert(const PlayerRecord& record);
    const PlayerRecord* SearchByName(const std::string& name) const;
    const PlayerRecord* SearchByID(const std::string& id) const;
    void Clear();
    std::vector<PlayerRecord> GetFirstNRecords(size_t n) const;
    size_t MemoryUsageBytes() const;
private:
    struct Entry {
        PlayerRecord data;
        bool taken = false;
    };
    std::vector<Entry> _byName;
    std::vector<Entry> _byID;
    size_t _mask;
    size_t hashString(const std::string& s) const;
    mutable std::mutex mut_;
};

class PlayerTrie {
public:
    PlayerTrie();
    ~PlayerTrie();
    void Insert(const PlayerRecord& record);
    const PlayerRecord* SearchExact(const std::string& name) const;
    std::vector<const PlayerRecord*> SearchByPrefix(const std::string& prefix) const;
    void Clear();
    std::vector<const PlayerRecord*> GetFirstNRecords(size_t n) const;
    size_t MemoryUsageBytes() const;
private:
    struct Node {
        Node* next[128] = {nullptr};
        std::unique_ptr<PlayerRecord> rec;
    };
    Node* root;
    void free(Node* n);
    void prefixSearch(Node* n, std::vector<const PlayerRecord*>& out) const;
    mutable std::mutex mut_;
};
