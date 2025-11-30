//
//  AssetsManager.h
//  SuperTerminal Framework - Asset Management System
//
//  High-level asset manager with caching and loading support
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ASSETS_MANAGER_H
#define ASSETS_MANAGER_H

#include "AssetDatabase.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <vector>

namespace SuperTerminal {

// Forward declarations
class AssetDatabase;

// Asset load result
enum class AssetLoadResult {
    SUCCESS = 0,
    NOT_FOUND,
    INVALID_FORMAT,
    LOAD_FAILED,
    DATABASE_ERROR,
    ALREADY_LOADED,
    CACHE_FULL,
    FILESYSTEM_ERROR
};

// Cached asset entry
struct CachedAsset {
    AssetMetadata metadata;
    bool loaded = false;
    uint16_t spriteId = 0;      // For sprites/tiles
    uint32_t soundId = 0;        // For sounds
    std::string filePath;        // Fallback filesystem path
    size_t accessCount = 0;
    std::time_t lastAccess = 0;
    
    CachedAsset() = default;
    CachedAsset(const AssetMetadata& meta) : metadata(meta) {}
};

// Asset loading configuration
struct AssetLoadConfig {
    bool enableCache = true;
    bool fallbackToFilesystem = true;
    bool preloadMetadata = false;
    size_t maxCacheSize = 100 * 1024 * 1024;  // 100 MB default
    size_t maxCachedAssets = 256;
    
    // Asset ID allocation ranges
    uint16_t spriteIdStart = 1;
    uint16_t spriteIdEnd = 255;
    uint32_t soundIdStart = 1;
    uint32_t soundIdEnd = 255;
};

// Asset search paths
struct AssetSearchPaths {
    std::string databasePath;           // Primary database
    std::string bundleResourcesPath;    // App bundle resources
    std::string userAssetsPath;         // User Application Support
    std::string fallbackPath;           // Filesystem fallback
    
    AssetSearchPaths() = default;
};

// AssetsManager class
class AssetsManager {
public:
    // Constructor/Destructor
    AssetsManager();
    explicit AssetsManager(const AssetLoadConfig& config);
    ~AssetsManager();
    
    // No copy
    AssetsManager(const AssetsManager&) = delete;
    AssetsManager& operator=(const AssetsManager&) = delete;
    
    // === INITIALIZATION ===
    
    // Initialize with database path
    bool initialize(const std::string& databasePath);
    
    // Initialize with search paths
    bool initialize(const AssetSearchPaths& searchPaths);
    
    // Shutdown and cleanup
    void shutdown();
    
    // Check if initialized
    bool isInitialized() const { return initialized; }
    
    // === DATABASE ACCESS ===
    
    // Get underlying database (for direct access)
    AssetDatabase* getDatabase() { return database.get(); }
    const AssetDatabase* getDatabase() const { return database.get(); }
    
    // === ASSET LOADING ===
    
    // Load sprite by name (loads into sprite system)
    AssetLoadResult loadSprite(const std::string& name, uint16_t& outSpriteId);
    
    // Load sprite with explicit ID
    AssetLoadResult loadSprite(const std::string& name, uint16_t spriteId);
    
    // Load sound by name (loads into audio system)
    AssetLoadResult loadSound(const std::string& name, uint32_t& outSoundId);
    
    // Load sound with explicit ID
    AssetLoadResult loadSound(const std::string& name, uint32_t soundId);
    
    // Load music by name (prepares for playback)
    AssetLoadResult loadMusic(const std::string& name, std::string& outMusicData);
    
    // Load image data (returns raw pixel data)
    AssetLoadResult loadImage(const std::string& name, std::vector<uint8_t>& outPixels,
                               int& outWidth, int& outHeight);
    
    // Load tile (same as sprite but different semantic usage)
    AssetLoadResult loadTile(const std::string& name, uint16_t tileId);
    
    // Load tilemap data (returns decompressed tilemap)
    AssetLoadResult loadTilemap(const std::string& name, std::vector<uint8_t>& outData);
    
    // Load text screen data (returns decompressed screen buffer)
    AssetLoadResult loadTextScreen(const std::string& name, std::vector<uint8_t>& outData);
    
    // Load script (returns decompressed script text)
    AssetLoadResult loadScript(const std::string& name, std::string& outScript);
    
    // Load script directly into editor (most efficient - no intermediate copies)
    AssetLoadResult loadScriptToEditor(const std::string& name);
    
    // Load generic asset data (returns raw blob, handles decompression)
    AssetLoadResult loadAssetData(const std::string& name, std::vector<uint8_t>& outData);
    
    // === ASSET QUERIES ===
    
    // Check if asset exists (in DB or filesystem)
    bool hasAsset(const std::string& name) const;
    
    // Get asset metadata (without loading data)
    bool getAssetMetadata(const std::string& name, AssetMetadata& outMetadata) const;
    
    // List all assets of a specific kind
    std::vector<std::string> listAssets(AssetKind kind) const;
    
    // List all asset names
    std::vector<std::string> listAllAssets() const;
    
    // Search assets by pattern
    std::vector<std::string> searchAssets(const std::string& pattern) const;
    
    // Get assets by tag
    std::vector<std::string> getAssetsByTag(const std::string& tag) const;
    
    // === CACHE MANAGEMENT ===
    
    // Check if asset is cached
    bool isCached(const std::string& name) const;
    
    // Get cached asset info
    const CachedAsset* getCachedAsset(const std::string& name) const;
    
    // Clear specific asset from cache
    void uncache(const std::string& name);
    
    // Clear all cached assets
    void clearCache();
    
    // Clear cache for specific kind
    void clearCache(AssetKind kind);
    
    // Get cache statistics
    size_t getCacheSize() const;
    size_t getCachedAssetCount() const;
    size_t getCacheMemoryUsage() const;
    
    // Set cache limits
    void setCacheLimit(size_t maxSizeBytes, size_t maxAssets);
    
    // === PRELOADING ===
    
    // Preload assets by kind
    int preloadAssets(AssetKind kind);
    
    // Preload assets by tag
    int preloadAssetsByTag(const std::string& tag);
    
    // Preload specific assets
    int preloadAssets(const std::vector<std::string>& names);
    
    // === ASSET MANAGEMENT ===
    
    // Add asset to database
    bool addAsset(const AssetMetadata& metadata);
    
    // Remove asset from database
    bool removeAsset(const std::string& name);
    
    // Update asset metadata
    bool updateAsset(const AssetMetadata& metadata);
    
    // Export asset to file
    bool exportAsset(const std::string& name, const std::string& outputPath);
    
    // Import asset from file
    bool importAsset(const std::string& filePath, const std::string& assetName,
                     AssetKind kind, bool autoCompress = true);
    
    // Save script to database
    bool saveScript(const std::string& name, const std::string& script,
                    const std::string& version = "", const std::string& author = "",
                    const std::vector<std::string>& tags = {});
    
    // Save tilemap to database
    bool saveTilemap(const std::string& name, const std::vector<uint8_t>& tilemapData,
                     int width, int height, const std::string& version = "",
                     const std::vector<std::string>& tags = {});
    
    // Save text screen to database
    bool saveTextScreen(const std::string& name, const std::vector<uint8_t>& screenData,
                        const std::string& version = "", const std::string& author = "",
                        const std::vector<std::string>& tags = {});
    
    // === ID ALLOCATION ===
    
    // Allocate next available sprite ID
    uint16_t allocateSpriteId();
    
    // Allocate next available sound ID
    uint32_t allocateSoundId();
    
    // Free sprite ID
    void freeSpriteId(uint16_t id);
    
    // Free sound ID
    void freeSoundId(uint32_t id);
    
    // Reset ID allocators
    void resetIdAllocators();
    
    // === FILESYSTEM FALLBACK ===
    
    // Enable/disable filesystem fallback
    void setFilesystemFallback(bool enabled);
    
    // Add filesystem search path
    void addSearchPath(const std::string& path);
    
    // Remove search path
    void removeSearchPath(const std::string& path);
    
    // Get all search paths
    std::vector<std::string> getSearchPaths() const;
    
    // === STATISTICS ===
    
    // Get asset statistics from database
    AssetStatistics getStatistics() const;
    
    // Get load statistics
    struct LoadStatistics {
        size_t totalLoads = 0;
        size_t cacheHits = 0;
        size_t cacheMisses = 0;
        size_t filesystemLoads = 0;
        size_t loadFailures = 0;
        
        float getCacheHitRate() const {
            return totalLoads > 0 ? (float)cacheHits / totalLoads : 0.0f;
        }
    };
    
    LoadStatistics getLoadStatistics() const { return loadStats; }
    
    // Reset load statistics
    void resetLoadStatistics();
    
    // === CONFIGURATION ===
    
    // Get current configuration
    AssetLoadConfig getConfig() const { return config; }
    
    // Update configuration
    void setConfig(const AssetLoadConfig& newConfig);
    
    // === ERROR HANDLING ===
    
    // Get last error message
    std::string getLastError() const { return lastError; }
    
    // Clear last error
    void clearLastError() { lastError.clear(); }
    
    // Convert load result to string
    static const char* loadResultToString(AssetLoadResult result);
    
    // === UTILITY ===
    
    // Get default database path
    static std::string getDefaultDatabasePath();
    
    // Get app bundle resources path
    static std::string getBundleResourcesPath();
    
    // Get user assets path (Application Support)
    static std::string getUserAssetsPath();
    
    // Resolve asset path with search paths
    std::string resolveAssetPath(const std::string& name) const;
    
private:
    // Initialization state
    bool initialized = false;
    
    // Configuration
    AssetLoadConfig config;
    
    // Search paths
    AssetSearchPaths searchPaths;
    std::vector<std::string> additionalSearchPaths;
    
    // Database
    std::unique_ptr<AssetDatabase> database;
    
    // Asset cache
    std::unordered_map<std::string, CachedAsset> cache;
    size_t cacheMemoryUsage = 0;
    
    // ID allocation tracking
    std::vector<bool> spriteIdAllocated;
    std::vector<bool> soundIdAllocated;
    uint16_t nextSpriteId = 1;
    uint32_t nextSoundId = 1;
    
    // Statistics
    LoadStatistics loadStats;
    
    // Error tracking
    mutable std::string lastError;
    
    // === INTERNAL HELPERS ===
    
    // Set error message
    void setError(const std::string& message);
    
    // Load asset from database
    AssetLoadResult loadFromDatabase(const std::string& name, AssetMetadata& outMetadata);
    
    // Load asset from filesystem
    AssetLoadResult loadFromFilesystem(const std::string& name, AssetMetadata& outMetadata);
    
    // Add to cache
    void addToCache(const std::string& name, const CachedAsset& asset);
    
    // Remove from cache (LRU if needed)
    void evictFromCache();
    
    // Load sprite data into sprite system
    bool loadSpriteData(const std::vector<uint8_t>& data, uint16_t spriteId);
    
    // Load sound data into audio system
    bool loadSoundData(const std::vector<uint8_t>& data, uint32_t soundId);
    
    // Decode image from PNG/JPG
    bool decodeImage(const std::vector<uint8_t>& data, AssetFormat format,
                     std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight);
    
    // Find file in search paths
    std::string findFileInSearchPaths(const std::string& filename) const;
    
    // Load file from disk
    bool loadFileData(const std::string& path, std::vector<uint8_t>& outData);
    
    // Guess asset kind from filename
    AssetKind guessAssetKind(const std::string& filename) const;
    
    // Guess asset format from filename
    AssetFormat guessAssetFormat(const std::string& filename) const;
    
    // Update cache access time
    void updateCacheAccess(const std::string& name);
    
    // Find least recently used cache entry
    std::string findLRUCacheEntry() const;
    
    // Internal loading helpers (to avoid overload ambiguity)
    AssetLoadResult loadSpriteInternal(const std::string& name, uint16_t spriteId);
    AssetLoadResult loadSoundInternal(const std::string& name, uint32_t soundId);
    
    // Compression helpers (zstd)
    bool compressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    bool decompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output);
    bool shouldCompress(AssetKind kind, AssetFormat format) const;
};

// Global AssetsManager instance (optional singleton access)
AssetsManager* GetGlobalAssetsManager();
void SetGlobalAssetsManager(AssetsManager* manager);

} // namespace SuperTerminal

#endif // ASSETS_MANAGER_H