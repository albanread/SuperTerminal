//
//  AssetsManager.cpp
//  SuperTerminal Framework - Asset Management System
//
//  High-level asset manager implementation
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AssetsManager.h"
#include "AssetDatabase.h"
#include "AssetMetadata.h"
#include "../include/SuperTerminal.h"
#include <fstream>
#include <algorithm>
#include <ctime>
#include <sys/stat.h>
#include <zstd.h>
#include <sqlite3.h>
#include <iostream>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#endif

// External editor functions (C linkage)
extern "C" {
    void editor_set_content(const char* content, const char* filename);
    void editor_set_database_metadata(const char* script_name, const char* version, const char* author);
    
    // Audio system functions
    bool audio_initialize();
    bool audio_load_sound_from_buffer_with_id(const float* samples, size_t sampleCount,
                                              uint32_t sampleRate, uint32_t channels,
                                              uint32_t sound_id);
}

namespace SuperTerminal {

// Global instance
static AssetsManager* g_globalAssetsManager = nullptr;

AssetsManager* GetGlobalAssetsManager() {
    return g_globalAssetsManager;
}

void SetGlobalAssetsManager(AssetsManager* manager) {
    g_globalAssetsManager = manager;
}

// ============================================================================
// Constructor/Destructor
// ============================================================================

AssetsManager::AssetsManager() {
    // Initialize ID allocation arrays
    spriteIdAllocated.resize(65536, false);
    soundIdAllocated.resize(65536, false);
}

AssetsManager::AssetsManager(const AssetLoadConfig& cfg)
    : config(cfg) {
    spriteIdAllocated.resize(65536, false);
    soundIdAllocated.resize(65536, false);
    nextSpriteId = config.spriteIdStart;
    nextSoundId = config.soundIdStart;
}

AssetsManager::~AssetsManager() {
    shutdown();
}

// ============================================================================
// Initialization
// ============================================================================

bool AssetsManager::initialize(const std::string& databasePath) {
    AssetSearchPaths paths;
    paths.databasePath = databasePath;
    paths.bundleResourcesPath = getBundleResourcesPath();
    paths.userAssetsPath = getUserAssetsPath();
    return initialize(paths);
}

bool AssetsManager::initialize(const AssetSearchPaths& paths) {
    if (initialized) {
        setError("AssetsManager already initialized");
        return false;
    }
    
    searchPaths = paths;
    
    // Create database instance
    database = std::make_unique<AssetDatabase>();
    
    // Try to open database at specified path
    if (!searchPaths.databasePath.empty()) {
        auto result = database->open(searchPaths.databasePath, false);
        if (result) {
            // Database opened successfully - check if we need to create schema
            // Check file size - if it's 0 or very small, schema probably doesn't exist
            struct stat st;
            bool needsSchema = false;
            if (stat(searchPaths.databasePath.c_str(), &st) == 0) {
                if (st.st_size < 1024) {  // Less than 1KB = empty or corrupted
                    needsSchema = true;
                }
            }
            
            if (needsSchema) {
                // Create schema
                auto schemaResult = database->createSchema();
                if (!schemaResult) {
                    setError("Failed to create database schema: " + schemaResult.errorMessage);
                    return false;
                }
            }
            
            initialized = true;
            return true;
        } else {
            setError("Failed to open database: " + result.errorMessage);
        }
    }
    
    // If primary database failed, try bundle resources
    if (!searchPaths.bundleResourcesPath.empty()) {
        std::string bundleDb = searchPaths.bundleResourcesPath + "/assets.db";
        auto result = database->open(bundleDb, true); // Read-only
        if (result) {
            initialized = true;
            return true;
        }
    }
    
    // If filesystem fallback is enabled, we can still work without database
    if (config.fallbackToFilesystem) {
        initialized = true;
        return true;
    }
    
    setError("Failed to initialize: no database available and filesystem fallback disabled");
    return false;
}

void AssetsManager::shutdown() {
    if (!initialized) {
        return;
    }
    
    // Clear cache
    clearCache();
    
    // Close database
    if (database) {
        database->close();
        database.reset();
    }
    
    // Reset state
    initialized = false;
    resetIdAllocators();
    resetLoadStatistics();
}

// ============================================================================
// Asset Loading - Sprites
// ============================================================================

AssetLoadResult AssetsManager::loadSprite(const std::string& name, uint16_t& outSpriteId) {
    uint16_t id = allocateSpriteId();
    if (id == 0) {
        setError("No sprite IDs available");
        return AssetLoadResult::CACHE_FULL;
    }
    
    AssetLoadResult result = loadSpriteInternal(name, id);
    if (result == AssetLoadResult::SUCCESS) {
        outSpriteId = id;
    } else {
        freeSpriteId(id);
    }
    
    return result;
}

AssetLoadResult AssetsManager::loadSprite(const std::string& name, uint16_t spriteId) {
    return loadSpriteInternal(name, spriteId);
}

AssetLoadResult AssetsManager::loadSpriteInternal(const std::string& name, uint16_t spriteId) {
    if (!initialized) {
        setError("AssetsManager not initialized");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    loadStats.totalLoads++;
    
    // Check cache first
    if (config.enableCache && isCached(name)) {
        const CachedAsset* cached = getCachedAsset(name);
        if (cached && cached->loaded && cached->spriteId == spriteId) {
            loadStats.cacheHits++;
            updateCacheAccess(name);
            return AssetLoadResult::SUCCESS;
        }
    }
    
    loadStats.cacheMisses++;
    
    // Try to load from database
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    // Fallback to filesystem if enabled
    if (result != AssetLoadResult::SUCCESS && config.fallbackToFilesystem) {
        result = loadFromFilesystem(name, metadata);
        if (result == AssetLoadResult::SUCCESS) {
            loadStats.filesystemLoads++;
        }
    }
    
    if (result != AssetLoadResult::SUCCESS) {
        loadStats.loadFailures++;
        return result;
    }
    
    // Load sprite data into sprite system
    if (!loadSpriteData(metadata.data, spriteId)) {
        setError("Failed to load sprite data into sprite system");
        loadStats.loadFailures++;
        return AssetLoadResult::LOAD_FAILED;
    }
    
    // Add to cache
    if (config.enableCache) {
        CachedAsset cached(metadata);
        cached.loaded = true;
        cached.spriteId = spriteId;
        cached.lastAccess = std::time(nullptr);
        cached.accessCount = 1;
        addToCache(name, cached);
    }
    
    return AssetLoadResult::SUCCESS;
}

// ============================================================================
// Asset Loading - Sounds
// ============================================================================

AssetLoadResult AssetsManager::loadSound(const std::string& name, uint32_t& outSoundId) {
    uint32_t id = allocateSoundId();
    if (id == 0) {
        setError("No sound IDs available");
        return AssetLoadResult::CACHE_FULL;
    }
    
    AssetLoadResult result = loadSoundInternal(name, id);
    if (result == AssetLoadResult::SUCCESS) {
        outSoundId = id;
    } else {
        freeSoundId(id);
    }
    
    return result;
}

AssetLoadResult AssetsManager::loadSound(const std::string& name, uint32_t soundId) {
    return loadSoundInternal(name, soundId);
}

AssetLoadResult AssetsManager::loadSoundInternal(const std::string& name, uint32_t soundId) {
    if (!initialized) {
        setError("AssetsManager not initialized");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    loadStats.totalLoads++;
    
    // Check cache
    if (config.enableCache && isCached(name)) {
        const CachedAsset* cached = getCachedAsset(name);
        if (cached && cached->loaded) {
            loadStats.cacheHits++;
            updateCacheAccess(name);
            return AssetLoadResult::SUCCESS;
        }
    }
    
    loadStats.cacheMisses++;
    
    // Load from database or filesystem
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS && config.fallbackToFilesystem) {
        result = loadFromFilesystem(name, metadata);
        if (result == AssetLoadResult::SUCCESS) {
            loadStats.filesystemLoads++;
        }
    }
    
    if (result != AssetLoadResult::SUCCESS) {
        loadStats.loadFailures++;
        return result;
    }
    
    // Load sound data into audio system
    if (!loadSoundData(metadata.data, soundId)) {
        setError("Failed to load sound data into audio system");
        loadStats.loadFailures++;
        return AssetLoadResult::LOAD_FAILED;
    }
    
    // Cache it
    if (config.enableCache) {
        CachedAsset cached(metadata);
        cached.loaded = true;
        cached.soundId = soundId;
        cached.lastAccess = std::time(nullptr);
        cached.accessCount = 1;
        addToCache(name, cached);
    }
    
    return AssetLoadResult::SUCCESS;
}

// ============================================================================
// Asset Loading - Music
// ============================================================================

AssetLoadResult AssetsManager::loadMusic(const std::string& name, std::string& outMusicData) {
    if (!initialized) {
        setError("AssetsManager not initialized");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    loadStats.totalLoads++;
    
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS && config.fallbackToFilesystem) {
        result = loadFromFilesystem(name, metadata);
        if (result == AssetLoadResult::SUCCESS) {
            loadStats.filesystemLoads++;
        }
    }
    
    if (result != AssetLoadResult::SUCCESS) {
        loadStats.loadFailures++;
        return result;
    }
    
    // Convert binary data to string
    outMusicData = std::string(metadata.data.begin(), metadata.data.end());
    return AssetLoadResult::SUCCESS;
}

// ============================================================================
// Asset Loading - Images & Tiles
// ============================================================================

AssetLoadResult AssetsManager::loadImage(const std::string& name, std::vector<uint8_t>& outPixels,
                                          int& outWidth, int& outHeight) {
    if (!initialized) {
        setError("AssetsManager not initialized");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS && config.fallbackToFilesystem) {
        result = loadFromFilesystem(name, metadata);
    }
    
    if (result != AssetLoadResult::SUCCESS) {
        return result;
    }
    
    // Decode image
    if (!decodeImage(metadata.data, metadata.format, outPixels, outWidth, outHeight)) {
        setError("Failed to decode image");
        return AssetLoadResult::INVALID_FORMAT;
    }
    
    return AssetLoadResult::SUCCESS;
}

AssetLoadResult AssetsManager::loadTile(const std::string& name, uint16_t tileId) {
    // Tiles are loaded same as sprites
    return loadSpriteInternal(name, tileId);
}

AssetLoadResult AssetsManager::loadAssetData(const std::string& name, std::vector<uint8_t>& outData) {
    if (!initialized) {
        setError("AssetsManager not initialized");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    loadStats.totalLoads++;
    
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS && config.fallbackToFilesystem) {
        result = loadFromFilesystem(name, metadata);
        if (result == AssetLoadResult::SUCCESS) {
            loadStats.filesystemLoads++;
        }
    }
    
    if (result != AssetLoadResult::SUCCESS) {
        loadStats.loadFailures++;
        return result;
    }
    
    // Decompress if needed
    if (metadata.compressed) {
        if (!decompressData(metadata.data, outData)) {
            setError("Failed to decompress asset data");
            return AssetLoadResult::LOAD_FAILED;
        }
    } else {
        outData = metadata.data;
    }
    
    return AssetLoadResult::SUCCESS;
}

AssetLoadResult AssetsManager::loadScript(const std::string& name, std::string& outScript) {
    std::vector<uint8_t> data;
    AssetLoadResult result = loadAssetData(name, data);
    if (result == AssetLoadResult::SUCCESS) {
        outScript = std::string(data.begin(), data.end());
    }
    return result;
}

AssetLoadResult AssetsManager::loadScriptToEditor(const std::string& name) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    // Load metadata to check if compressed
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS) {
        setError("Script not found in database");
        return result;
    }
    
    // Decompress if needed
    std::vector<uint8_t> scriptData;
    if (metadata.compressed) {
        if (!decompressData(metadata.data, scriptData)) {
            setError("Failed to decompress script");
            return AssetLoadResult::LOAD_FAILED;
        }
    } else {
        scriptData = metadata.data;
    }
    
    // Ensure null termination and load directly into editor
    scriptData.push_back('\0');
    
    // Create filename with .lua extension for display in editor
    std::string displayName = name + ".lua";
    
    // Load directly into editor (no temp file, no copying!)
    editor_set_content(reinterpret_cast<const char*>(scriptData.data()), displayName.c_str());
    
    // Set database metadata so Save (⌘S) can update the database
    editor_set_database_metadata(name.c_str(), metadata.version.c_str(), metadata.author.c_str());
    
    return AssetLoadResult::SUCCESS;
}

AssetLoadResult AssetsManager::loadTilemap(const std::string& name, std::vector<uint8_t>& outData) {
    return loadAssetData(name, outData);
}

AssetLoadResult AssetsManager::loadTextScreen(const std::string& name, std::vector<uint8_t>& outData) {
    return loadAssetData(name, outData);
}

// ============================================================================
// Asset Queries
// ============================================================================

bool AssetsManager::hasAsset(const std::string& name) const {
    if (!initialized) {
        return false;
    }
    
    // Check database
    if (database && database->isOpen()) {
        if (database->hasAsset(name)) {
            return true;
        }
    }
    
    // Check filesystem
    if (config.fallbackToFilesystem) {
        std::string path = findFileInSearchPaths(name);
        return !path.empty();
    }
    
    return false;
}

bool AssetsManager::getAssetMetadata(const std::string& name, AssetMetadata& outMetadata) const {
    if (!initialized) {
        return false;
    }
    
    if (database && database->isOpen()) {
        auto result = database->getAssetByName(name);
        if (result) {
            outMetadata = result.value;
            return true;
        }
    }
    
    return false;
}

std::vector<std::string> AssetsManager::listAssets(AssetKind kind) const {
    std::vector<std::string> names;
    
    if (!initialized || !database || !database->isOpen()) {
        return names;
    }
    
    auto result = database->getAssetNames(kind);
    if (result) {
        names = result.value;
    }
    
    return names;
}

std::vector<std::string> AssetsManager::listAllAssets() const {
    std::vector<std::string> names;
    
    if (!initialized || !database || !database->isOpen()) {
        return names;
    }
    
    auto result = database->getAssetNames();
    if (result) {
        names = result.value;
    }
    
    return names;
}

std::vector<std::string> AssetsManager::searchAssets(const std::string& pattern) const {
    std::vector<std::string> names;
    
    if (!initialized || !database || !database->isOpen()) {
        return names;
    }
    
    auto result = database->searchAssets(pattern);
    if (result) {
        for (const auto& asset : result.value) {
            names.push_back(asset.name);
        }
    }
    
    return names;
}

std::vector<std::string> AssetsManager::getAssetsByTag(const std::string& tag) const {
    std::vector<std::string> names;
    
    if (!initialized || !database || !database->isOpen()) {
        return names;
    }
    
    auto result = database->getAssetsByTag(tag);
    if (result) {
        for (const auto& asset : result.value) {
            names.push_back(asset.name);
        }
    }
    
    return names;
}

// ============================================================================
// Cache Management
// ============================================================================

bool AssetsManager::isCached(const std::string& name) const {
    return cache.find(name) != cache.end();
}

const CachedAsset* AssetsManager::getCachedAsset(const std::string& name) const {
    auto it = cache.find(name);
    if (it != cache.end()) {
        return &it->second;
    }
    return nullptr;
}

void AssetsManager::uncache(const std::string& name) {
    auto it = cache.find(name);
    if (it != cache.end()) {
        cacheMemoryUsage -= it->second.metadata.getDataSize();
        cache.erase(it);
    }
}

void AssetsManager::clearCache() {
    cache.clear();
    cacheMemoryUsage = 0;
}

void AssetsManager::clearCache(AssetKind kind) {
    auto it = cache.begin();
    while (it != cache.end()) {
        if (it->second.metadata.kind == kind) {
            cacheMemoryUsage -= it->second.metadata.getDataSize();
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

size_t AssetsManager::getCacheSize() const {
    return cache.size();
}

size_t AssetsManager::getCachedAssetCount() const {
    return cache.size();
}

size_t AssetsManager::getCacheMemoryUsage() const {
    return cacheMemoryUsage;
}

void AssetsManager::setCacheLimit(size_t maxSizeBytes, size_t maxAssets) {
    config.maxCacheSize = maxSizeBytes;
    config.maxCachedAssets = maxAssets;
    
    // Evict if over limit
    while (cacheMemoryUsage > config.maxCacheSize || cache.size() > config.maxCachedAssets) {
        evictFromCache();
    }
}

// ============================================================================
// Preloading
// ============================================================================

int AssetsManager::preloadAssets(AssetKind kind) {
    if (!initialized || !database || !database->isOpen()) {
        return 0;
    }
    
    auto result = database->getAssetsByKind(kind);
    if (!result) {
        return 0;
    }
    
    int loaded = 0;
    for (const auto& asset : result.value) {
        if (config.enableCache) {
            CachedAsset cached(asset);
            cached.loaded = false;
            addToCache(asset.name, cached);
            loaded++;
        }
    }
    
    return loaded;
}

int AssetsManager::preloadAssetsByTag(const std::string& tag) {
    if (!initialized || !database || !database->isOpen()) {
        return 0;
    }
    
    auto result = database->getAssetsByTag(tag);
    if (!result) {
        return 0;
    }
    
    int loaded = 0;
    for (const auto& asset : result.value) {
        if (config.enableCache) {
            CachedAsset cached(asset);
            cached.loaded = false;
            addToCache(asset.name, cached);
            loaded++;
        }
    }
    
    return loaded;
}

int AssetsManager::preloadAssets(const std::vector<std::string>& names) {
    int loaded = 0;
    
    for (const auto& name : names) {
        AssetMetadata metadata;
        if (getAssetMetadata(name, metadata)) {
            if (config.enableCache) {
                CachedAsset cached(metadata);
                cached.loaded = false;
                addToCache(name, cached);
                loaded++;
            }
        }
    }
    
    return loaded;
}

// ============================================================================
// Asset Management
// ============================================================================

bool AssetsManager::addAsset(const AssetMetadata& metadata) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    auto result = database->addAsset(metadata);
    if (!result) {
        setError(result.errorMessage);
        return false;
    }
    
    return true;
}

bool AssetsManager::removeAsset(const std::string& name) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    // Remove from cache first
    uncache(name);
    
    auto result = database->deleteAssetByName(name);
    if (!result) {
        setError(result.errorMessage);
        return false;
    }
    
    return true;
}

bool AssetsManager::updateAsset(const AssetMetadata& metadata) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    // Invalidate cache
    uncache(metadata.name);
    
    auto result = database->updateAsset(metadata);
    if (!result) {
        setError(result.errorMessage);
        return false;
    }
    
    return true;
}

bool AssetsManager::exportAsset(const std::string& name, const std::string& outputPath) {
    AssetMetadata metadata;
    AssetLoadResult result = loadFromDatabase(name, metadata);
    
    if (result != AssetLoadResult::SUCCESS) {
        return false;
    }
    
    std::ofstream file(outputPath, std::ios::binary);
    if (!file) {
        setError("Failed to open output file");
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(metadata.data.data()), metadata.data.size());
    return file.good();
}

bool AssetsManager::importAsset(const std::string& filePath, const std::string& assetName,
                                 AssetKind kind, bool autoCompress) {
    std::vector<uint8_t> data;
    if (!loadFileData(filePath, data)) {
        setError("Failed to read file");
        return false;
    }
    
    AssetMetadata metadata;
    metadata.name = assetName;
    metadata.kind = kind;
    metadata.format = guessAssetFormat(filePath);
    metadata.created_at = std::time(nullptr);
    metadata.updated_at = metadata.created_at;
    
    // Auto-compress if requested and asset type is compressible
    if (autoCompress && shouldCompress(kind, metadata.format)) {
        std::vector<uint8_t> compressed;
        if (compressData(data, compressed)) {
            metadata.data = std::move(compressed);
            metadata.compressed = true;
        } else {
            metadata.data = std::move(data);
            metadata.compressed = false;
        }
    } else {
        metadata.data = std::move(data);
        metadata.compressed = false;
    }
    
    return addAsset(metadata);
}

bool AssetsManager::saveScript(const std::string& name, const std::string& script,
                                const std::string& version, const std::string& author,
                                const std::vector<std::string>& tags) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    AssetMetadata metadata;
    metadata.name = name;
    metadata.kind = AssetKind::SCRIPT;
    metadata.format = AssetFormat::LUA;
    metadata.version = version;
    metadata.author = author;
    metadata.tags = tags;
    metadata.created_at = std::time(nullptr);
    metadata.updated_at = metadata.created_at;
    
    // Convert script to bytes
    std::vector<uint8_t> scriptData(script.begin(), script.end());
    
    // Compress script
    std::vector<uint8_t> compressed;
    if (compressData(scriptData, compressed)) {
        metadata.data = std::move(compressed);
        metadata.compressed = true;
    } else {
        metadata.data = std::move(scriptData);
        metadata.compressed = false;
    }
    
    return addAsset(metadata);
}

bool AssetsManager::saveTilemap(const std::string& name, const std::vector<uint8_t>& tilemapData,
                                 int width, int height, const std::string& version,
                                 const std::vector<std::string>& tags) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    AssetMetadata metadata;
    metadata.name = name;
    metadata.kind = AssetKind::TILEMAP;
    metadata.format = AssetFormat::TILEMAP_DATA;
    metadata.width = width;
    metadata.height = height;
    metadata.version = version;
    metadata.tags = tags;
    metadata.created_at = std::time(nullptr);
    metadata.updated_at = metadata.created_at;
    
    // Compress tilemap data
    std::vector<uint8_t> compressed;
    if (compressData(tilemapData, compressed)) {
        metadata.data = std::move(compressed);
        metadata.compressed = true;
    } else {
        metadata.data = tilemapData;
        metadata.compressed = false;
    }
    
    return addAsset(metadata);
}

bool AssetsManager::saveTextScreen(const std::string& name, const std::vector<uint8_t>& screenData,
                                    const std::string& version, const std::string& author,
                                    const std::vector<std::string>& tags) {
    if (!initialized || !database || !database->isOpen()) {
        setError("Database not available");
        return false;
    }
    
    AssetMetadata metadata;
    metadata.name = name;
    metadata.kind = AssetKind::TEXTSCREEN;
    metadata.format = AssetFormat::TEXTSCREEN_DATA;
    metadata.width = 80;  // Standard text screen width
    metadata.height = 25; // Standard text screen height
    metadata.version = version;
    metadata.author = author;
    metadata.tags = tags;
    metadata.created_at = std::time(nullptr);
    metadata.updated_at = metadata.created_at;
    
    // Compress screen data
    std::vector<uint8_t> compressed;
    if (compressData(screenData, compressed)) {
        metadata.data = std::move(compressed);
        metadata.compressed = true;
    } else {
        metadata.data = screenData;
        metadata.compressed = false;
    }
    
    return addAsset(metadata);
}

// ============================================================================
// ID Allocation
// ============================================================================

uint16_t AssetsManager::allocateSpriteId() {
    // Scan from start to find first available ID
    for (uint16_t id = config.spriteIdStart; id <= config.spriteIdEnd; id++) {
        if (!spriteIdAllocated[id]) {
            spriteIdAllocated[id] = true;
            return id;
        }
    }
    return 0; // No IDs available
}

uint32_t AssetsManager::allocateSoundId() {
    // Scan from start to find first available ID
    for (uint32_t id = config.soundIdStart; id <= config.soundIdEnd; id++) {
        if (!soundIdAllocated[id]) {
            soundIdAllocated[id] = true;
            return id;
        }
    }
    return 0; // No IDs available
}

void AssetsManager::freeSpriteId(uint16_t id) {
    if (id < spriteIdAllocated.size()) {
        spriteIdAllocated[id] = false;
    }
}

void AssetsManager::freeSoundId(uint32_t id) {
    if (id < soundIdAllocated.size()) {
        soundIdAllocated[id] = false;
    }
}

void AssetsManager::resetIdAllocators() {
    std::fill(spriteIdAllocated.begin(), spriteIdAllocated.end(), false);
    std::fill(soundIdAllocated.begin(), soundIdAllocated.end(), false);
    nextSpriteId = config.spriteIdStart;
    nextSoundId = config.soundIdStart;
}

// ============================================================================
// Filesystem Fallback
// ============================================================================

void AssetsManager::setFilesystemFallback(bool enabled) {
    config.fallbackToFilesystem = enabled;
}

void AssetsManager::addSearchPath(const std::string& path) {
    additionalSearchPaths.push_back(path);
}

void AssetsManager::removeSearchPath(const std::string& path) {
    auto it = std::find(additionalSearchPaths.begin(), additionalSearchPaths.end(), path);
    if (it != additionalSearchPaths.end()) {
        additionalSearchPaths.erase(it);
    }
}

std::vector<std::string> AssetsManager::getSearchPaths() const {
    std::vector<std::string> paths;
    
    if (!searchPaths.fallbackPath.empty()) {
        paths.push_back(searchPaths.fallbackPath);
    }
    if (!searchPaths.bundleResourcesPath.empty()) {
        paths.push_back(searchPaths.bundleResourcesPath);
    }
    if (!searchPaths.userAssetsPath.empty()) {
        paths.push_back(searchPaths.userAssetsPath);
    }
    
    paths.insert(paths.end(), additionalSearchPaths.begin(), additionalSearchPaths.end());
    
    return paths;
}

// ============================================================================
// Statistics
// ============================================================================

AssetStatistics AssetsManager::getStatistics() const {
    if (!initialized || !database || !database->isOpen()) {
        return AssetStatistics();
    }
    
    auto result = database->getStatistics();
    if (result) {
        return result.value;
    }
    
    return AssetStatistics();
}

void AssetsManager::resetLoadStatistics() {
    loadStats = LoadStatistics();
}

// ============================================================================
// Configuration
// ============================================================================

void AssetsManager::setConfig(const AssetLoadConfig& newConfig) {
    config = newConfig;
    
    // Update ID allocator ranges
    nextSpriteId = config.spriteIdStart;
    nextSoundId = config.soundIdStart;
}

// ============================================================================
// Utility
// ============================================================================

const char* AssetsManager::loadResultToString(AssetLoadResult result) {
    switch (result) {
        case AssetLoadResult::SUCCESS: return "Success";
        case AssetLoadResult::NOT_FOUND: return "Not found";
        case AssetLoadResult::INVALID_FORMAT: return "Invalid format";
        case AssetLoadResult::LOAD_FAILED: return "Load failed";
        case AssetLoadResult::DATABASE_ERROR: return "Database error";
        case AssetLoadResult::ALREADY_LOADED: return "Already loaded";
        case AssetLoadResult::CACHE_FULL: return "Cache full";
        case AssetLoadResult::FILESYSTEM_ERROR: return "Filesystem error";
        default: return "Unknown error";
    }
}

std::string AssetsManager::getDefaultDatabasePath() {
    return "assets.db";
}

std::string AssetsManager::getBundleResourcesPath() {
#ifdef __APPLE__
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (mainBundle) {
        CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
        if (resourcesURL) {
            char path[PATH_MAX];
            if (CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, PATH_MAX)) {
                CFRelease(resourcesURL);
                return std::string(path);
            }
            CFRelease(resourcesURL);
        }
    }
#endif
    return "";
}

std::string AssetsManager::getUserAssetsPath() {
#ifdef __APPLE__
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/Library/Application Support/SuperTerminal/assets";
    }
#endif
    return "";
}

std::string AssetsManager::resolveAssetPath(const std::string& name) const {
    return findFileInSearchPaths(name);
}

// ============================================================================
// Internal Helpers
// ============================================================================

void AssetsManager::setError(const std::string& message) {
    lastError = message;
}

AssetLoadResult AssetsManager::loadFromDatabase(const std::string& name, AssetMetadata& outMetadata) {
    if (!database || !database->isOpen()) {
        return AssetLoadResult::DATABASE_ERROR;
    }
    
    auto result = database->getAssetByName(name);
    if (!result) {
        return AssetLoadResult::NOT_FOUND;
    }
    
    outMetadata = result.value;
    return AssetLoadResult::SUCCESS;
}

AssetLoadResult AssetsManager::loadFromFilesystem(const std::string& name, AssetMetadata& outMetadata) {
    std::string path = findFileInSearchPaths(name);
    if (path.empty()) {
        return AssetLoadResult::NOT_FOUND;
    }
    
    std::vector<uint8_t> data;
    if (!loadFileData(path, data)) {
        return AssetLoadResult::FILESYSTEM_ERROR;
    }
    
    outMetadata.name = name;
    outMetadata.kind = guessAssetKind(name);
    outMetadata.format = guessAssetFormat(name);
    outMetadata.data = std::move(data);
    outMetadata.created_at = std::time(nullptr);
    outMetadata.updated_at = outMetadata.created_at;
    
    return AssetLoadResult::SUCCESS;
}

void AssetsManager::addToCache(const std::string& name, const CachedAsset& asset) {
    // Check if we need to evict
    while (cacheMemoryUsage + asset.metadata.getDataSize() > config.maxCacheSize ||
           cache.size() >= config.maxCachedAssets) {
        evictFromCache();
    }
    
    cache[name] = asset;
    cacheMemoryUsage += asset.metadata.getDataSize();
}

void AssetsManager::evictFromCache() {
    if (cache.empty()) {
        return;
    }
    
    std::string lruName = findLRUCacheEntry();
    uncache(lruName);
}

bool AssetsManager::loadSpriteData(const std::vector<uint8_t>& data, uint16_t spriteId) {
    // For now, we need to write to temp file and use sprite_load
    // In future, add direct memory loading to sprite system
    std::string tempPath = "/tmp/st_sprite_" + std::to_string(spriteId) + ".png";
    
    std::ofstream file(tempPath, std::ios::binary);
    if (!file) {
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
    bool result = sprite_load(spriteId, tempPath.c_str());
    
    // Clean up temp file
    remove(tempPath.c_str());
    
    return result;
}
bool AssetsManager::loadSoundData(const std::vector<uint8_t>& data, uint32_t soundId) {
    std::cout << "AssetsManager: loadSoundData called with soundId=" << soundId 
              << ", data.size()=" << data.size() << std::endl;
    
    if (data.empty()) {
        setError("Empty sound data");
        std::cout << "AssetsManager: ERROR - Empty sound data" << std::endl;
        return false;
    }
    
    // Parse PCM header: sampleRate (4) + channels (4) + sampleCount (4) = 12 bytes
    if (data.size() < 12) {
        setError("PCM data too small (missing header)");
        return false;
    }
    
    uint32_t sampleRate = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    uint32_t channels = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    uint32_t sampleCount = data[8] | (data[9] << 8) | (data[10] << 16) | (data[11] << 24);
    
    std::cout << "AssetsManager: PCM data - sampleRate=" << sampleRate 
              << ", channels=" << channels << ", sampleCount=" << sampleCount << std::endl;
    
    // Verify data size
    size_t expectedSize = 12 + (sampleCount * sizeof(float));
    if (data.size() < expectedSize) {
        setError("PCM data size mismatch");
        return false;
    }
    
    // Extract float samples
    const float* floatSamples = reinterpret_cast<const float*>(&data[12]);
    
    // Ensure audio is initialized
    audio_initialize();
    
    // Load into audio system with the specific sound ID
    bool success = audio_load_sound_from_buffer_with_id(floatSamples, sampleCount, 
                                                         sampleRate, channels, soundId);
    
    if (!success) {
        setError("Failed to load sound into audio system");
        return false;
    }
    
    std::cout << "AssetsManager: Successfully loaded PCM sound with ID: " << soundId << std::endl;
    
    return true;
}
bool AssetsManager::decodeImage(const std::vector<uint8_t>& data, AssetFormat format,
                                 std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    // TODO: Implement image decoding using stb_image or similar
    setError("Image decoding not yet implemented");
    return false;
}

std::string AssetsManager::findFileInSearchPaths(const std::string& filename) const {
    auto paths = getSearchPaths();
    
    for (const auto& searchPath : paths) {
        std::string fullPath = searchPath + "/" + filename;
        
        struct stat buffer;
        if (stat(fullPath.c_str(), &buffer) == 0) {
            return fullPath;
        }
    }
    
    // Try filename as-is
    struct stat buffer;
    if (stat(filename.c_str(), &buffer) == 0) {
        return filename;
    }
    
    return "";
}

bool AssetsManager::loadFileData(const std::string& path, std::vector<uint8_t>& outData) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    outData.resize(size);
    return file.read(reinterpret_cast<char*>(outData.data()), size).good();
}

AssetKind AssetsManager::guessAssetKind(const std::string& filename) const {
    std::string ext;
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        ext = filename.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        // Check if it's in sprites or tiles directory
        if (filename.find("sprite") != std::string::npos) {
            return AssetKind::SPRITE;
        } else if (filename.find("tile") != std::string::npos && filename.find("tilemap") == std::string::npos) {
            return AssetKind::TILE;
        } else if (filename.find("tilemap") != std::string::npos) {
            return AssetKind::TILEMAP;
        }
        return AssetKind::IMAGE;
    } else if (ext == "wav" || ext == "mp3" || ext == "ogg") {
        if (filename.find("music") != std::string::npos) {
            return AssetKind::MUSIC;
        }
        return AssetKind::SOUND;
    } else if (ext == "abc" || ext == "midi" || ext == "mid") {
        return AssetKind::MUSIC;
    } else if (ext == "ttf" || ext == "otf") {
        return AssetKind::FONT;
    } else if (ext == "lua") {
        return AssetKind::SCRIPT;
    } else if (ext == "tilemap") {
        return AssetKind::TILEMAP;
    } else if (ext == "textscreen" || ext == "screen") {
        return AssetKind::TEXTSCREEN;
    }
    
    return AssetKind::DATA;
}

AssetFormat AssetsManager::guessAssetFormat(const std::string& filename) const {
    std::string ext;
    size_t pos = filename.find_last_of('.');
    if (pos != std::string::npos) {
        ext = filename.substr(pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    if (ext == "png") return AssetFormat::PNG;
    if (ext == "jpg" || ext == "jpeg") return AssetFormat::JPG;
    if (ext == "wav") return AssetFormat::WAV;
    if (ext == "mp3") return AssetFormat::MP3;
    if (ext == "ogg") return AssetFormat::OGG;
    if (ext == "midi" || ext == "mid") return AssetFormat::MIDI;
    if (ext == "abc") return AssetFormat::ABC;
    if (ext == "ttf") return AssetFormat::TTF;
    if (ext == "otf") return AssetFormat::OTF;
    if (ext == "lua") return AssetFormat::LUA;
    if (ext == "tilemap") return AssetFormat::TILEMAP_DATA;
    if (ext == "textscreen" || ext == "screen") return AssetFormat::TEXTSCREEN_DATA;
    
    return AssetFormat::BINARY;
}

void AssetsManager::updateCacheAccess(const std::string& name) {
    auto it = cache.find(name);
    if (it != cache.end()) {
        it->second.lastAccess = std::time(nullptr);
        it->second.accessCount++;
    }
}

std::string AssetsManager::findLRUCacheEntry() const {
    if (cache.empty()) {
        return "";
    }
    
    std::string lruName;
    std::time_t oldestAccess = std::time(nullptr);
    
    for (const auto& pair : cache) {
        if (pair.second.lastAccess < oldestAccess) {
            oldestAccess = pair.second.lastAccess;
            lruName = pair.first;
        }
    }
    
    return lruName;
}

// ============================================================================
// Compression Helpers (zstd)
// ============================================================================

bool AssetsManager::shouldCompress(AssetKind kind, AssetFormat format) const {
    // Compress text-based assets
    switch (kind) {
        case AssetKind::SCRIPT:
        case AssetKind::TEXTSCREEN:
        case AssetKind::TILEMAP:
            return true;
            
        case AssetKind::MUSIC:
            // Compress ABC notation (text) but not MIDI (binary)
            return format == AssetFormat::ABC;
            
        case AssetKind::DATA:
            // Compress if it's text-based
            return true;
            
        default:
            // Don't compress already-compressed formats
            return false;
    }
}

bool AssetsManager::compressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (input.empty()) {
        return false;
    }
    
    // Estimate compressed size (worst case)
    size_t maxCompressedSize = ZSTD_compressBound(input.size());
    output.resize(maxCompressedSize);
    
    // Compress with default compression level (3)
    size_t compressedSize = ZSTD_compress(
        output.data(), 
        output.size(),
        input.data(), 
        input.size(),
        3  // Compression level (1=fast, 22=max)
    );
    
    if (ZSTD_isError(compressedSize)) {
        setError(std::string("Compression failed: ") + ZSTD_getErrorName(compressedSize));
        return false;
    }
    
    // Resize to actual compressed size
    output.resize(compressedSize);
    return true;
}

bool AssetsManager::decompressData(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    if (input.empty()) {
        return false;
    }
    
    // Get decompressed size
    unsigned long long decompressedSize = ZSTD_getFrameContentSize(input.data(), input.size());
    
    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        setError("Invalid compressed data");
        return false;
    }
    
    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        setError("Decompressed size unknown");
        return false;
    }
    
    output.resize(decompressedSize);
    
    // Decompress
    size_t result = ZSTD_decompress(
        output.data(), 
        output.size(),
        input.data(), 
        input.size()
    );
    
    if (ZSTD_isError(result)) {
        setError(std::string("Decompression failed: ") + ZSTD_getErrorName(result));
        return false;
    }
    
    return true;
}



} // namespace SuperTerminal
