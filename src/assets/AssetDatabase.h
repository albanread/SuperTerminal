//
//  AssetDatabase.h
//  SuperTerminal Framework - Asset Database System
//
//  SQLite wrapper for asset database operations
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ASSET_DATABASE_H
#define ASSET_DATABASE_H

#include "AssetMetadata.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declare SQLite types to avoid exposing sqlite3.h in header
struct sqlite3;
struct sqlite3_stmt;

namespace SuperTerminal {

// Database error codes
enum class AssetDatabaseError {
    NONE = 0,
    OPEN_FAILED,
    QUERY_FAILED,
    INSERT_FAILED,
    UPDATE_FAILED,
    DELETE_FAILED,
    NOT_FOUND,
    ALREADY_EXISTS,
    INVALID_DATA,
    DISK_FULL,
    READONLY,
    CORRUPT
};

// Result type for operations
template<typename T>
struct DatabaseResult {
    bool success = false;
    T value;
    AssetDatabaseError error = AssetDatabaseError::NONE;
    std::string errorMessage;
    
    DatabaseResult() = default;
    DatabaseResult(const T& val) : success(true), value(val) {}
    DatabaseResult(AssetDatabaseError err, const std::string& msg)
        : success(false), error(err), errorMessage(msg) {}
    
    operator bool() const { return success; }
};

// Specialization for void operations
template<>
struct DatabaseResult<void> {
    bool success = false;
    AssetDatabaseError error = AssetDatabaseError::NONE;
    std::string errorMessage;
    
    DatabaseResult() = default;
    DatabaseResult(bool s) : success(s) {}
    DatabaseResult(AssetDatabaseError err, const std::string& msg)
        : success(false), error(err), errorMessage(msg) {}
    
    operator bool() const { return success; }
};

// Asset database class
class AssetDatabase {
public:
    // Constructor/Destructor
    AssetDatabase();
    explicit AssetDatabase(const std::string& dbPath);
    ~AssetDatabase();
    
    // No copy (SQLite handles can't be copied)
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;
    
    // Move is OK
    AssetDatabase(AssetDatabase&& other) noexcept;
    AssetDatabase& operator=(AssetDatabase&& other) noexcept;
    
    // === DATABASE OPERATIONS ===
    
    // Open/close database
    DatabaseResult<void> open(const std::string& dbPath, bool readOnly = false);
    DatabaseResult<void> close();
    bool isOpen() const { return db != nullptr; }
    
    // Create schema (call after open on new database)
    DatabaseResult<void> createSchema();
    
    // Check if database is properly initialized
    bool isInitialized() const;
    
    // Get database path
    std::string getPath() const { return databasePath; }
    
    // === ASSET CRUD OPERATIONS ===
    
    // Create - Add new asset
    DatabaseResult<int64_t> addAsset(const AssetMetadata& metadata);
    
    // Read - Get asset by ID
    DatabaseResult<AssetMetadata> getAsset(int64_t id) const;
    
    // Read - Get asset by name
    DatabaseResult<AssetMetadata> getAssetByName(const std::string& name) const;
    
    // Read - Check if asset exists
    bool hasAsset(const std::string& name) const;
    bool hasAsset(int64_t id) const;
    
    // Update - Modify existing asset
    DatabaseResult<void> updateAsset(const AssetMetadata& metadata);
    
    // Update - Update only metadata (not data BLOB)
    DatabaseResult<void> updateAssetMetadata(int64_t id, const AssetMetadata& metadata);
    
    // Delete - Remove asset
    DatabaseResult<void> deleteAsset(int64_t id);
    DatabaseResult<void> deleteAssetByName(const std::string& name);
    
    // === QUERY OPERATIONS ===
    
    // Query assets with filters
    DatabaseResult<std::vector<AssetMetadata>> queryAssets(const AssetQuery& query) const;
    
    // Get all asset names (lightweight, no data)
    DatabaseResult<std::vector<std::string>> getAssetNames() const;
    DatabaseResult<std::vector<std::string>> getAssetNames(AssetKind kind) const;
    
    // Get all assets of a specific kind
    DatabaseResult<std::vector<AssetMetadata>> getAssetsByKind(AssetKind kind) const;
    
    // Search assets by name pattern
    DatabaseResult<std::vector<AssetMetadata>> searchAssets(const std::string& pattern) const;
    
    // Get assets with specific tag
    DatabaseResult<std::vector<AssetMetadata>> getAssetsByTag(const std::string& tag) const;
    
    // Count operations
    int64_t getAssetCount() const;
    int64_t getAssetCount(AssetKind kind) const;
    
    // === STATISTICS ===
    
    DatabaseResult<AssetStatistics> getStatistics() const;
    
    // === MAINTENANCE OPERATIONS ===
    
    // Vacuum database (reclaim space)
    DatabaseResult<void> vacuum();
    
    // Verify database integrity
    DatabaseResult<bool> verifyIntegrity() const;
    
    // Repair database (if possible)
    DatabaseResult<void> repair();
    
    // Get database size in bytes
    int64_t getDatabaseSize() const;
    
    // === TRANSACTION SUPPORT ===
    
    // Begin transaction (for batch operations)
    DatabaseResult<void> beginTransaction();
    
    // Commit transaction
    DatabaseResult<void> commitTransaction();
    
    // Rollback transaction
    DatabaseResult<void> rollbackTransaction();
    
    // RAII transaction helper
    class Transaction {
    public:
        explicit Transaction(AssetDatabase& db);
        ~Transaction();
        
        void commit();
        void rollback();
        
    private:
        AssetDatabase& database;
        bool committed = false;
        bool rolledBack = false;
    };
    
    // === BATCH OPERATIONS ===
    
    // Delete all assets (use with caution!)
    DatabaseResult<void> deleteAllAssets();
    
    // Delete all assets of a specific kind
    DatabaseResult<void> deleteAssetsByKind(AssetKind kind);
    
    // Export all assets to directory
    DatabaseResult<void> exportAll(const std::string& directory) const;
    
    // Import assets from directory
    DatabaseResult<int> importDirectory(const std::string& directory, AssetKind defaultKind);
    
    // === ERROR HANDLING ===
    
    // Get last error message
    std::string getLastError() const { return lastError; }
    
    // Clear last error
    void clearLastError() { lastError.clear(); }
    
    // === UTILITY FUNCTIONS ===
    
    // Get SQLite version
    static std::string getSQLiteVersion();
    
    // Check if file is a valid SQLite database
    static bool isDatabaseFile(const std::string& path);
    
    // Create backup of database
    DatabaseResult<void> backup(const std::string& backupPath) const;
    
    // Restore from backup
    DatabaseResult<void> restore(const std::string& backupPath);
    
private:
    // SQLite handle
    sqlite3* db = nullptr;
    
    // Database path
    std::string databasePath;
    
    // Read-only flag
    bool readOnly = false;
    
    // Last error message
    mutable std::string lastError;
    
    // Prepared statements cache (for performance)
    mutable sqlite3_stmt* stmtGetAssetById = nullptr;
    mutable sqlite3_stmt* stmtGetAssetByName = nullptr;
    mutable sqlite3_stmt* stmtHasAsset = nullptr;
    mutable sqlite3_stmt* stmtInsertAsset = nullptr;
    mutable sqlite3_stmt* stmtUpdateAsset = nullptr;
    mutable sqlite3_stmt* stmtDeleteAsset = nullptr;
    
    // === INTERNAL HELPERS ===
    
    // Set error message
    void setError(const std::string& message) const;
    void setError(AssetDatabaseError error, const std::string& message) const;
    
    // Execute SQL without results
    bool executeSQL(const std::string& sql);
    
    // Prepare statement
    sqlite3_stmt* prepareStatement(const std::string& sql) const;
    
    // Finalize statement
    void finalizeStatement(sqlite3_stmt* stmt) const;
    
    // Finalize all cached statements
    void finalizeAllStatements();
    
    // Bind metadata to statement
    void bindMetadata(sqlite3_stmt* stmt, const AssetMetadata& metadata) const;
    
    // Read metadata from statement
    AssetMetadata readMetadata(sqlite3_stmt* stmt) const;
    
    // Convert AssetKind to string for storage
    std::string kindToDBString(AssetKind kind) const;
    
    // Convert string to AssetKind from storage
    AssetKind dbStringToKind(const std::string& str) const;
    
    // Convert AssetFormat to string for storage
    std::string formatToDBString(AssetFormat format) const;
    
    // Convert string to AssetFormat from storage
    AssetFormat dbStringToFormat(const std::string& str) const;
    
    // Build WHERE clause from query
    std::string buildWhereClause(const AssetQuery& query, std::vector<std::string>& params) const;
    
    // Get file extension from path
    static std::string getFileExtension(const std::string& path);
    
    // Guess asset kind from file extension
    static AssetKind guessKindFromExtension(const std::string& extension);
    
    // Guess asset format from file extension
    static AssetFormat guessFormatFromExtension(const std::string& extension);
};

} // namespace SuperTerminal

#endif // ASSET_DATABASE_H