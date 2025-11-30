//
//  AssetDatabase.cpp
//  SuperTerminal Framework - Asset Database System
//
//  SQLite wrapper implementation for asset database operations
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AssetDatabase.h"
#include <sqlite3.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/stat.h>

namespace SuperTerminal {

// Database schema SQL
static const char* SCHEMA_SQL = R"(
CREATE TABLE IF NOT EXISTS assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    kind TEXT NOT NULL,
    format TEXT NOT NULL,
    width INTEGER DEFAULT 0,
    height INTEGER DEFAULT 0,
    duration REAL DEFAULT 0.0,
    length INTEGER DEFAULT 0,
    i INTEGER DEFAULT 0,
    j INTEGER DEFAULT 0,
    k INTEGER DEFAULT 0,
    data BLOB NOT NULL,
    tags TEXT DEFAULT '',
    description TEXT DEFAULT '',
    checksum TEXT DEFAULT '',
    version TEXT DEFAULT '',
    author TEXT DEFAULT '',
    compressed INTEGER DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_assets_name ON assets(name);
CREATE INDEX IF NOT EXISTS idx_assets_kind ON assets(kind);
CREATE INDEX IF NOT EXISTS idx_assets_format ON assets(format);

CREATE TRIGGER IF NOT EXISTS update_timestamp 
AFTER UPDATE ON assets
BEGIN
    UPDATE assets SET updated_at = CURRENT_TIMESTAMP WHERE id = NEW.id;
END;
)";

// Constructor
AssetDatabase::AssetDatabase() = default;

AssetDatabase::AssetDatabase(const std::string& dbPath) {
    open(dbPath);
}

// Destructor
AssetDatabase::~AssetDatabase() {
    close();
}

// Move constructor
AssetDatabase::AssetDatabase(AssetDatabase&& other) noexcept
    : db(other.db)
    , databasePath(std::move(other.databasePath))
    , readOnly(other.readOnly)
    , lastError(std::move(other.lastError))
{
    other.db = nullptr;
}

// Move assignment
AssetDatabase& AssetDatabase::operator=(AssetDatabase&& other) noexcept {
    if (this != &other) {
        close();
        db = other.db;
        databasePath = std::move(other.databasePath);
        readOnly = other.readOnly;
        lastError = std::move(other.lastError);
        other.db = nullptr;
    }
    return *this;
}

// Open database
DatabaseResult<void> AssetDatabase::open(const std::string& dbPath, bool readOnlyMode) {
    if (db != nullptr) {
        return DatabaseResult<void>(AssetDatabaseError::ALREADY_EXISTS, "Database already open");
    }
    
    databasePath = dbPath;
    readOnly = readOnlyMode;
    
    int flags = readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    int rc = sqlite3_open_v2(dbPath.c_str(), &db, flags, nullptr);
    
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db);
        sqlite3_close(db);
        db = nullptr;
        return DatabaseResult<void>(AssetDatabaseError::OPEN_FAILED, "Failed to open database: " + error);
    }
    
    // Enable foreign keys
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    
    return DatabaseResult<void>(true);
}

// Close database
DatabaseResult<void> AssetDatabase::close() {
    if (db == nullptr) {
        return DatabaseResult<void>(true);
    }
    
    // Finalize all prepared statements
    finalizeAllStatements();
    
    int rc = sqlite3_close(db);
    if (rc != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Failed to close database");
    }
    
    db = nullptr;
    databasePath.clear();
    
    return DatabaseResult<void>(true);
}

// Create schema
DatabaseResult<void> AssetDatabase::createSchema() {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, SCHEMA_SQL, nullptr, nullptr, &errMsg);
    
    if (rc != SQLITE_OK) {
        std::string error = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Failed to create schema: " + error);
    }
    
    return DatabaseResult<void>(true);
}

// Check if initialized
bool AssetDatabase::isInitialized() const {
    if (!db) return false;
    
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='assets';";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

// Add asset
DatabaseResult<int64_t> AssetDatabase::addAsset(const AssetMetadata& metadata) {
    if (!db) {
        return DatabaseResult<int64_t>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (readOnly) {
        return DatabaseResult<int64_t>(AssetDatabaseError::READONLY, "Database is read-only");
    }
    
    if (!metadata.isValid()) {
        return DatabaseResult<int64_t>(AssetDatabaseError::INVALID_DATA, "Invalid asset metadata");
    }
    
    // Check if name already exists
    if (hasAsset(metadata.name)) {
        return DatabaseResult<int64_t>(AssetDatabaseError::ALREADY_EXISTS, "Asset with this name already exists");
    }
    
    const char* sql = R"(
        INSERT INTO assets (name, kind, format, width, height, duration, length, i, j, k, data, tags, description, checksum, version, author, compressed)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return DatabaseResult<int64_t>(AssetDatabaseError::INSERT_FAILED, sqlite3_errmsg(db));
    }
    
    // Bind parameters
    sqlite3_bind_text(stmt, 1, metadata.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, assetKindToString(metadata.kind), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, assetFormatToString(metadata.format), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, metadata.width);
    sqlite3_bind_int(stmt, 5, metadata.height);
    sqlite3_bind_double(stmt, 6, metadata.duration);
    sqlite3_bind_int(stmt, 7, metadata.length);
    sqlite3_bind_int(stmt, 8, metadata.i);
    sqlite3_bind_int(stmt, 9, metadata.j);
    sqlite3_bind_int(stmt, 10, metadata.k);
    sqlite3_bind_blob(stmt, 11, metadata.data.data(), metadata.data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, metadata.getTagsString().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, metadata.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, metadata.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, metadata.version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, metadata.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 17, metadata.compressed ? 1 : 0);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DatabaseResult<int64_t>(AssetDatabaseError::INSERT_FAILED, sqlite3_errmsg(db));
    }
    
    int64_t id = sqlite3_last_insert_rowid(db);
    return DatabaseResult<int64_t>(id);
}

// Get asset by ID
DatabaseResult<AssetMetadata> AssetDatabase::getAsset(int64_t id) const {
    if (!db) {
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    const char* sql = R"(
        SELECT id, name, kind, format, width, height, duration, length, i, j, k, data, tags, description, checksum,
               version, author, compressed, strftime('%s', created_at), strftime('%s', updated_at)
        FROM assets WHERE id = ?
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::NOT_FOUND, "Asset not found");
    }
    
    AssetMetadata metadata = readMetadata(stmt);
    sqlite3_finalize(stmt);
    
    return DatabaseResult<AssetMetadata>(metadata);
}

// Get asset by name
DatabaseResult<AssetMetadata> AssetDatabase::getAssetByName(const std::string& name) const {
    if (!db) {
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    const char* sql = R"(
        SELECT id, name, kind, format, width, height, duration, length, i, j, k, data, tags, description, checksum,
               version, author, compressed, strftime('%s', created_at), strftime('%s', updated_at)
        FROM assets WHERE name = ?
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return DatabaseResult<AssetMetadata>(AssetDatabaseError::NOT_FOUND, "Asset not found");
    }
    
    AssetMetadata metadata = readMetadata(stmt);
    sqlite3_finalize(stmt);
    
    return DatabaseResult<AssetMetadata>(metadata);
}

// Check if asset exists by name
bool AssetDatabase::hasAsset(const std::string& name) const {
    if (!db) return false;
    
    const char* sql = "SELECT 1 FROM assets WHERE name = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

// Check if asset exists by ID
bool AssetDatabase::hasAsset(int64_t id) const {
    if (!db) return false;
    
    const char* sql = "SELECT 1 FROM assets WHERE id = ? LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int64(stmt, 1, id);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    
    return exists;
}

// Update asset
DatabaseResult<void> AssetDatabase::updateAsset(const AssetMetadata& metadata) {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (readOnly) {
        return DatabaseResult<void>(AssetDatabaseError::READONLY, "Database is read-only");
    }
    
    const char* sql = R"(
        UPDATE assets SET name = ?, kind = ?, format = ?, width = ?, height = ?, 
                         duration = ?, length = ?, i = ?, j = ?, k = ?, data = ?, 
                         tags = ?, description = ?, checksum = ?, version = ?, author = ?, compressed = ?
        WHERE id = ?
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::UPDATE_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_text(stmt, 1, metadata.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, assetKindToString(metadata.kind), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, assetFormatToString(metadata.format), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, metadata.width);
    sqlite3_bind_int(stmt, 5, metadata.height);
    sqlite3_bind_double(stmt, 6, metadata.duration);
    sqlite3_bind_int(stmt, 7, metadata.length);
    sqlite3_bind_int(stmt, 8, metadata.i);
    sqlite3_bind_int(stmt, 9, metadata.j);
    sqlite3_bind_int(stmt, 10, metadata.k);
    sqlite3_bind_blob(stmt, 11, metadata.data.data(), metadata.data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, metadata.getTagsString().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, metadata.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 14, metadata.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 15, metadata.version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 16, metadata.author.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 17, metadata.compressed ? 1 : 0);
    sqlite3_bind_int64(stmt, 18, metadata.id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DatabaseResult<void>(AssetDatabaseError::UPDATE_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Delete asset by ID
DatabaseResult<void> AssetDatabase::deleteAsset(int64_t id) {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (readOnly) {
        return DatabaseResult<void>(AssetDatabaseError::READONLY, "Database is read-only");
    }
    
    const char* sql = "DELETE FROM assets WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::DELETE_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DatabaseResult<void>(AssetDatabaseError::DELETE_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Delete asset by name
DatabaseResult<void> AssetDatabase::deleteAssetByName(const std::string& name) {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (readOnly) {
        return DatabaseResult<void>(AssetDatabaseError::READONLY, "Database is read-only");
    }
    
    const char* sql = "DELETE FROM assets WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::DELETE_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DatabaseResult<void>(AssetDatabaseError::DELETE_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Query assets
DatabaseResult<std::vector<AssetMetadata>> AssetDatabase::queryAssets(const AssetQuery& query) const {
    if (!db) {
        return DatabaseResult<std::vector<AssetMetadata>>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    std::ostringstream sql;
    sql << "SELECT id, name, kind, format, width, height, duration, length, i, j, k, data, tags, description, checksum, "
        << "version, author, compressed, strftime('%s', created_at), strftime('%s', updated_at) FROM assets WHERE 1=1";
    
    // Build WHERE clause
    if (!query.namePattern.empty()) {
        sql << " AND name LIKE '%" << query.namePattern << "%'";
    }
    
    if (query.kind != AssetKind::UNKNOWN) {
        sql << " AND kind = '" << assetKindToString(query.kind) << "'";
    }
    
    if (query.format != AssetFormat::UNKNOWN) {
        sql << " AND format = '" << assetFormatToString(query.format) << "'";
    }
    
    // ORDER BY
    sql << " ORDER BY " << query.orderBy;
    if (!query.ascending) {
        sql << " DESC";
    }
    
    // LIMIT
    if (query.limit > 0) {
        sql << " LIMIT " << query.limit;
        if (query.offset > 0) {
            sql << " OFFSET " << query.offset;
        }
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return DatabaseResult<std::vector<AssetMetadata>>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    std::vector<AssetMetadata> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(readMetadata(stmt));
    }
    
    sqlite3_finalize(stmt);
    return DatabaseResult<std::vector<AssetMetadata>>(results);
}

// Get asset names
DatabaseResult<std::vector<std::string>> AssetDatabase::getAssetNames() const {
    if (!db) {
        return DatabaseResult<std::vector<std::string>>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    const char* sql = "SELECT name FROM assets ORDER BY name";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return DatabaseResult<std::vector<std::string>>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    std::vector<std::string> names;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name) names.push_back(name);
    }
    
    sqlite3_finalize(stmt);
    return DatabaseResult<std::vector<std::string>>(names);
}

// Get asset names by kind
DatabaseResult<std::vector<std::string>> AssetDatabase::getAssetNames(AssetKind kind) const {
    if (!db) {
        return DatabaseResult<std::vector<std::string>>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    const char* sql = "SELECT name FROM assets WHERE kind = ? ORDER BY name";
    sqlite3_stmt* stmt = nullptr;
    
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return DatabaseResult<std::vector<std::string>>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    sqlite3_bind_text(stmt, 1, kindToDBString(kind).c_str(), -1, SQLITE_TRANSIENT);
    
    std::vector<std::string> names;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (name) {
            names.push_back(name);
        }
    }
    
    sqlite3_finalize(stmt);
    return DatabaseResult<std::vector<std::string>>(names);
}

// Get assets by kind
DatabaseResult<std::vector<AssetMetadata>> AssetDatabase::getAssetsByKind(AssetKind kind) const {
    AssetQuery query;
    query.kind = kind;
    return queryAssets(query);
}

// Search assets by name pattern
DatabaseResult<std::vector<AssetMetadata>> AssetDatabase::searchAssets(const std::string& pattern) const {
    AssetQuery query;
    query.namePattern = pattern;
    return queryAssets(query);
}

// Get assets by tag
DatabaseResult<std::vector<AssetMetadata>> AssetDatabase::getAssetsByTag(const std::string& tag) const {
    if (!db) {
        return DatabaseResult<std::vector<AssetMetadata>>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    const char* sql = R"(
        SELECT id, name, kind, format, width, height, duration, length, i, j, k, data, tags, description, checksum,
               version, author, compressed, strftime('%s', created_at), strftime('%s', updated_at)
        FROM assets WHERE tags LIKE ? ORDER BY name
    )";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return DatabaseResult<std::vector<AssetMetadata>>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    std::string tagPattern = "%" + tag + "%";
    sqlite3_bind_text(stmt, 1, tagPattern.c_str(), -1, SQLITE_TRANSIENT);
    
    std::vector<AssetMetadata> assets;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        assets.push_back(readMetadata(stmt));
    }
    
    sqlite3_finalize(stmt);
    return DatabaseResult<std::vector<AssetMetadata>>(assets);
}

// Get asset count
int64_t AssetDatabase::getAssetCount() const {
    if (!db) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM assets";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

// Get asset count by kind
int64_t AssetDatabase::getAssetCount(AssetKind kind) const {
    if (!db) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM assets WHERE kind = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    sqlite3_bind_text(stmt, 1, kindToDBString(kind).c_str(), -1, SQLITE_TRANSIENT);
    
    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

// Get statistics
DatabaseResult<AssetStatistics> AssetDatabase::getStatistics() const {
    if (!db) {
        return DatabaseResult<AssetStatistics>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    AssetStatistics stats;
    
    // Get total count and size
    const char* sql = "SELECT COUNT(*), SUM(LENGTH(data)) FROM assets";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.totalAssets = sqlite3_column_int64(stmt, 0);
            stats.totalDataSize = sqlite3_column_int64(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    
    if (stats.totalAssets > 0) {
        stats.averageAssetSize = static_cast<double>(stats.totalDataSize) / stats.totalAssets;
    }
    
    // Count by kind
    const char* kindSql = "SELECT kind, COUNT(*) FROM assets GROUP BY kind";
    if (sqlite3_prepare_v2(db, kindSql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t count = sqlite3_column_int64(stmt, 1);
            
            if (strcmp(kind, "sprite") == 0) stats.spriteCount = count;
            else if (strcmp(kind, "tile") == 0) stats.tileCount = count;
            else if (strcmp(kind, "image") == 0) stats.imageCount = count;
            else if (strcmp(kind, "sound") == 0) stats.soundCount = count;
            else if (strcmp(kind, "music") == 0) stats.musicCount = count;
            else if (strcmp(kind, "font") == 0) stats.fontCount = count;
            else if (strcmp(kind, "data") == 0) stats.dataCount = count;
        }
        sqlite3_finalize(stmt);
    }
    
    return DatabaseResult<AssetStatistics>(stats);
}

// Begin transaction
DatabaseResult<void> AssetDatabase::beginTransaction() {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Commit transaction
DatabaseResult<void> AssetDatabase::commitTransaction() {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Rollback transaction
DatabaseResult<void> AssetDatabase::rollbackTransaction() {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Vacuum database
DatabaseResult<void> AssetDatabase::vacuum() {
    if (!db) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, "Database not open");
    }
    
    if (sqlite3_exec(db, "VACUUM", nullptr, nullptr, nullptr) != SQLITE_OK) {
        return DatabaseResult<void>(AssetDatabaseError::QUERY_FAILED, sqlite3_errmsg(db));
    }
    
    return DatabaseResult<void>(true);
}

// Get SQLite version
std::string AssetDatabase::getSQLiteVersion() {
    return sqlite3_libversion();
}

// Transaction RAII helper
AssetDatabase::Transaction::Transaction(AssetDatabase& db) : database(db) {
    database.beginTransaction();
}

AssetDatabase::Transaction::~Transaction() {
    if (!committed && !rolledBack) {
        database.rollbackTransaction();
    }
}

void AssetDatabase::Transaction::commit() {
    if (!committed && !rolledBack) {
        database.commitTransaction();
        committed = true;
    }
}

void AssetDatabase::Transaction::rollback() {
    if (!committed && !rolledBack) {
        database.rollbackTransaction();
        rolledBack = true;
    }
}

// Helper: Read metadata from statement
AssetMetadata AssetDatabase::readMetadata(sqlite3_stmt* stmt) const {
    AssetMetadata meta;
    
    meta.id = sqlite3_column_int64(stmt, 0);
    
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    if (name) meta.name = name;
    
    const char* kind = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    if (kind) meta.kind = stringToAssetKind(kind);
    
    const char* format = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (format) meta.format = stringToAssetFormat(format);
    
    meta.width = sqlite3_column_int(stmt, 4);
    meta.height = sqlite3_column_int(stmt, 5);
    meta.duration = sqlite3_column_double(stmt, 6);
    meta.length = sqlite3_column_int(stmt, 7);
    meta.i = sqlite3_column_int(stmt, 8);
    meta.j = sqlite3_column_int(stmt, 9);
    meta.k = sqlite3_column_int(stmt, 10);
    
    // Read BLOB data
    const void* blob = sqlite3_column_blob(stmt, 11);
    int blobSize = sqlite3_column_bytes(stmt, 11);
    if (blob && blobSize > 0) {
        meta.data.resize(blobSize);
        std::memcpy(meta.data.data(), blob, blobSize);
    }
    
    const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 12));
    if (tags) meta.setTagsFromString(tags);
    
    const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 13));
    if (desc) meta.description = desc;
    
    const char* checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 14));
    if (checksum) meta.checksum = checksum;
    
    const char* version = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 15));
    if (version) meta.version = version;
    
    const char* author = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 16));
    if (author) meta.author = author;
    
    meta.compressed = sqlite3_column_int(stmt, 17) != 0;
    
    meta.created_at = sqlite3_column_int64(stmt, 18);
    meta.updated_at = sqlite3_column_int64(stmt, 19);
    
    return meta;
}

// Helper: Finalize all statements
void AssetDatabase::finalizeAllStatements() {
    if (stmtGetAssetById) {
        sqlite3_finalize(stmtGetAssetById);
        stmtGetAssetById = nullptr;
    }
    if (stmtGetAssetByName) {
        sqlite3_finalize(stmtGetAssetByName);
        stmtGetAssetByName = nullptr;
    }
    if (stmtHasAsset) {
        sqlite3_finalize(stmtHasAsset);
        stmtHasAsset = nullptr;
    }
    if (stmtInsertAsset) {
        sqlite3_finalize(stmtInsertAsset);
        stmtInsertAsset = nullptr;
    }
    if (stmtUpdateAsset) {
        sqlite3_finalize(stmtUpdateAsset);
        stmtUpdateAsset = nullptr;
    }
    if (stmtDeleteAsset) {
        sqlite3_finalize(stmtDeleteAsset);
        stmtDeleteAsset = nullptr;
    }
}

// Helper: Convert AssetKind to string for storage
std::string AssetDatabase::kindToDBString(AssetKind kind) const {
    return assetKindToString(kind);
}

// Helper: Convert string to AssetKind from storage
AssetKind AssetDatabase::dbStringToKind(const std::string& str) const {
    return stringToAssetKind(str);
}

// Helper: Convert AssetFormat to string for storage
std::string AssetDatabase::formatToDBString(AssetFormat format) const {
    return assetFormatToString(format);
}

// Helper: Convert string to AssetFormat from storage
AssetFormat AssetDatabase::dbStringToFormat(const std::string& str) const {
    return stringToAssetFormat(str);
}

} // namespace SuperTerminal