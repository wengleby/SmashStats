#include "backend.h"
#include <cstring>
#include <sqlite3.h>
#include <queue>
#include <iostream>
#include <atomic>

// --- Rows visited counter implementation ---
std::atomic<size_t> g_backendRowsVisited{0};
size_t Backend_GetTotalRowsVisited() {
    return g_backendRowsVisited.load();
}

// --- ONLY load id, name, main_character ---
bool BackendDB_LoadAllPlayers(const std::string& db_path, PlayerHashTable& hash) {
    static PlayerTrie tr;
    return BackendDB_LoadAllPlayers(db_path, hash, tr);
}
bool BackendDB_LoadAllPlayers(const std::string& db_path, PlayerTrie& trie) {
    static PlayerHashTable ht;
    return BackendDB_LoadAllPlayers(db_path, ht, trie);
}
bool BackendDB_LoadAllPlayers(
    const std::string& db_path,
    PlayerHashTable& hashOut,
    PlayerTrie& trieOut)
{
    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);

    if (rc != SQLITE_OK || !db) {
        std::cerr << "sqlite3_open FAILED! Error code: " << rc
                  << " - " << (db ? sqlite3_errmsg(db) : "unknown") << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }

    const char* query =
        "SELECT player_id, tag, characters FROM players LIMIT 100000;";

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        std::cerr << "sqlite3_prepare_v2 FAILED! Error code: " << rc
                  << " - " << sqlite3_errmsg(db) << std::endl;
        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }

    hashOut.Clear();
    trieOut.Clear();

    size_t rows_visited = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlayerRecord rec;

        int id_val = sqlite3_column_int(stmt, 0);
        const unsigned char* col_name   = sqlite3_column_text(stmt, 1);
        const unsigned char* col_chars  = sqlite3_column_text(stmt, 2);

        rec.id = std::to_string(id_val);
        rec.name = (col_name != nullptr) ? reinterpret_cast<const char*>(col_name) : "";
        std::string characters = (col_chars != nullptr) ? reinterpret_cast<const char*>(col_chars) : "";

        if (characters.empty())
            continue;

        // --- Extract only the character name: match forward slash up to quote ---
        std::string main_char = "";
        auto slash = characters.find('/');
        if (slash != std::string::npos) {
            auto quote = characters.find('"', slash + 1);
            if (quote != std::string::npos && quote > slash + 1) {
                main_char = characters.substr(slash + 1, quote - slash - 1);
            }
        }
        // fallback: if parsing failed, use old behavior
        if (main_char.empty()) {
            size_t comma = characters.find(',');
            main_char = (comma == std::string::npos) ? characters : characters.substr(0, comma);
        }
        rec.main_character = main_char;

        rec.matches_played = -1;
        rec.matches_won = -1;
        rec.win_rate = -1.0;
        rec.stats_loaded = false;

        hashOut.Insert(rec);
        trieOut.Insert(rec);

        ++rows_visited;
        ++g_backendRowsVisited;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return true;
}

// --- Load per-player stats data (call after players loaded) ---
bool BackendDB_LoadPlayerStats(
    const std::string& db_path,
    PlayerHashTable& hash,
    PlayerTrie& trie)
{
    std::vector<PlayerRecord> all_players = hash.GetFirstNRecords(100000);

    if (all_players.empty()) return false;

    sqlite3* db = nullptr;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK || !db) {
        std::cerr << "sqlite3_open FAILED! " << rc << std::endl;
        if (db) sqlite3_close(db);
        return false;
    }

    const char* query =
        "SELECT "
            "COUNT(s.key) AS matches_played, "
            "SUM(CASE WHEN s.winner_id = ?1 THEN 1 ELSE 0 END) AS matches_won, "
            "CASE WHEN COUNT(s.key) = 0 THEN 0 "
                "ELSE 1.0 * SUM(CASE WHEN s.winner_id = ?1 THEN 1 ELSE 0 END) / COUNT(s.key) END AS win_rate "
        "FROM sets s "
        "WHERE (s.p1_id = ?1 OR s.p2_id = ?1);";

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        std::cerr << "sqlite3_prepare_v2 FAILED! Code: " << rc << std::endl;
        if (stmt) sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }

    size_t stats_rows_visited = 0;

    for (const PlayerRecord& original : all_players) {
        rc = sqlite3_bind_text(stmt, 1, original.id.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            int matches_played = sqlite3_column_int(stmt, 0);
            int matches_won    = sqlite3_column_int(stmt, 1);
            double win_rate    = sqlite3_column_double(stmt, 2);

            PlayerRecord rec = original;
            rec.matches_played = matches_played;
            rec.matches_won    = matches_won;
            rec.win_rate       = win_rate;
            rec.stats_loaded   = true;

            hash.Insert(rec);
            trie.Insert(rec);
            
            ++stats_rows_visited;
            ++g_backendRowsVisited;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return true;
}

// --- PlayerHashTable ---
PlayerHashTable::PlayerHashTable(size_t init_size) {
    size_t sizepow2 = 1;
    while (sizepow2 < init_size) sizepow2 <<= 1;
    _mask = sizepow2 - 1;
    _byName.resize(sizepow2);
    _byID.resize(sizepow2);
}
void PlayerHashTable::Clear() { std::lock_guard<std::mutex> lock(mut_); for (auto &e: _byName) e.taken = false; for (auto &e: _byID)   e.taken = false; }
size_t PlayerHashTable::hashString(const std::string& s) const { size_t hash = 0; for (char c : s) hash = hash * 31 + (unsigned char)c; return hash; }
void PlayerHashTable::Insert(const PlayerRecord& rec) {
    std::lock_guard<std::mutex> lock(mut_);
    size_t h = hashString(rec.name) & _mask;
    for (size_t i = 0; i < _byName.size(); ++i) {
        size_t p = (h + i) & _mask;
        if (!_byName[p].taken || _byName[p].data.name == rec.name) {
            _byName[p].data = rec; _byName[p].taken = true; break;
        }
    }
    h = hashString(rec.id) & _mask;
    for (size_t i = 0; i < _byID.size(); ++i) {
        size_t p = (h + i) & _mask;
        if (!_byID[p].taken || _byID[p].data.id == rec.id) {
            _byID[p].data = rec; _byID[p].taken = true; break;
        }
    }
}
const PlayerRecord* PlayerHashTable::SearchByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mut_);
    size_t h = hashString(name) & _mask;
    for (size_t i = 0; i < _byName.size(); ++i) {
        size_t p = (h + i) & _mask;
        if (!_byName[p].taken) return nullptr;
        if (_byName[p].data.name == name) return &_byName[p].data;
    }
    return nullptr;
}
const PlayerRecord* PlayerHashTable::SearchByID(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mut_);
    size_t h = hashString(id) & _mask;
    for (size_t i = 0; i < _byID.size(); ++i) {
        size_t p = (h + i) & _mask;
        if (!_byID[p].taken) return nullptr;
        if (_byID[p].data.id == id) return &_byID[p].data;
    }
    return nullptr;
}
std::vector<PlayerRecord> PlayerHashTable::GetFirstNRecords(size_t n) const {
    std::lock_guard<std::mutex> lock(mut_);
    std::vector<PlayerRecord> result;
    for (const auto& entry : _byName) {
        if (entry.taken) {
            result.push_back(entry.data);
            if (result.size() >= n) break;
        }
    }
    return result;
}
size_t PlayerHashTable::MemoryUsageBytes() const {
    std::lock_guard<std::mutex> lock(mut_);
    return _byName.size() * sizeof(Entry) + _byID.size() * sizeof(Entry);
}

// --- PlayerTrie ---
PlayerTrie::PlayerTrie() { root = new Node(); }
PlayerTrie::~PlayerTrie() { free(root); }
void PlayerTrie::Clear()  { std::lock_guard<std::mutex> lock(mut_); free(root); root = new Node(); }
void PlayerTrie::Insert(const PlayerRecord& rec) {
    std::lock_guard<std::mutex> lock(mut_);
    Node* node = root;
    for (char c : rec.name) {
        unsigned idx = (unsigned char)c;
        if (idx >= 128) continue;
        if (!node->next[idx]) node->next[idx] = new Node();
        node = node->next[idx];
    }
    node->rec = std::make_unique<PlayerRecord>(rec);
}
const PlayerRecord* PlayerTrie::SearchExact(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mut_);
    const Node* node = root;
    for (char c : name) {
        unsigned idx = (unsigned char)c;
        if (idx >= 128) return nullptr;
        if (!node->next[idx]) return nullptr;
        node = node->next[idx];
    }
    return node->rec ? node->rec.get() : nullptr;
}
void PlayerTrie::prefixSearch(Node* n, std::vector<const PlayerRecord*>& out) const {
    if (!n) return;
    if (n->rec) out.push_back(n->rec.get());
    for (int i = 0; i < 128; ++i)
        if (n->next[i]) prefixSearch(n->next[i], out);
}
std::vector<const PlayerRecord*> PlayerTrie::SearchByPrefix(const std::string& prefix) const {
    std::lock_guard<std::mutex> lock(mut_);
    std::vector<const PlayerRecord*> out;
    const Node* node = root;
    for (char c : prefix) {
        unsigned idx = (unsigned char)c;
        if (idx >= 128 || !node->next[idx]) return out;
        node = node->next[idx];
    }
    prefixSearch(const_cast<Node*>(node), out);
    return out;
}
void PlayerTrie::free(Node* n) {
    if (!n) return;
    for (int i=0; i<128; ++i)
        if (n->next[i]) free(n->next[i]);
    delete n;
}
std::vector<const PlayerRecord*> PlayerTrie::GetFirstNRecords(size_t n) const {
    std::lock_guard<std::mutex> lock(mut_);
    std::vector<const PlayerRecord*> result;
    std::queue<const Node*> nodes;
    nodes.push(root);
    while (!nodes.empty() && result.size() < n) {
        const Node* current = nodes.front();
        nodes.pop();
        if (current->rec) result.push_back(current->rec.get());
        for (int i = 0; i < 128; ++i)
            if (current->next[i]) nodes.push(current->next[i]);
    }
    return result;
}
size_t PlayerTrie::MemoryUsageBytes() const {
    std::lock_guard<std::mutex> lock(mut_);
    size_t total = 0;
    std::queue<Node*> nodes;
    nodes.push(root);
    while (!nodes.empty()) {
        Node* n = nodes.front(); nodes.pop();
        total += sizeof(Node);
        if (n->rec) total += sizeof(PlayerRecord);
        for (int i=0; i<128; ++i)
            if (n->next[i]) nodes.push(n->next[i]);
    }
    return total;
}
