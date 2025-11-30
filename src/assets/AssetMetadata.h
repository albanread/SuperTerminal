//
//  AssetMetadata.h
//  SuperTerminal Framework - Asset Database System
//
//  Metadata structures for asset database entries
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ASSET_METADATA_H
#define ASSET_METADATA_H

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace SuperTerminal {

// Asset kind enumeration
enum class AssetKind {
    UNKNOWN = 0,
    SPRITE,
    TILE,
    TILEMAP,
    IMAGE,
    SOUND,
    MUSIC,
    FONT,
    SCRIPT,
    TEXTSCREEN,
    DATA
};

// Asset format enumeration
enum class AssetFormat {
    UNKNOWN = 0,
    PNG,
    JPG,
    WAV,
    MP3,
    OGG,
    MIDI,
    ABC,
    TTF,
    OTF,
    LUA,
    PCM,             // Raw PCM audio data (float samples)
    TILEMAP_DATA,    // Custom tilemap format
    TEXTSCREEN_DATA, // Custom text screen format
    BINARY
};

// Convert enum to string
const char* assetKindToString(AssetKind kind);
const char* assetFormatToString(AssetFormat format);

// Convert string to enum
AssetKind stringToAssetKind(const std::string& str);
AssetFormat stringToAssetFormat(const std::string& str);

// Asset metadata structure
struct AssetMetadata {
    int64_t id = 0;                      // Database ID
    std::string name;                    // Unique asset name
    AssetKind kind = AssetKind::UNKNOWN; // Asset type
    AssetFormat format = AssetFormat::UNKNOWN; // Data format
    
    // Dimensions (for images, sprites, tiles)
    int32_t width = 0;
    int32_t height = 0;
    
    // Duration (for audio, video)
    double duration = 0.0;               // in seconds
    
    // Length (for music, animations)
    int32_t length = 0;                  // measures, frames, etc.
    
    // General purpose numeric fields
    int32_t i = 0;                       // e.g., sprite frames, tile set columns
    int32_t j = 0;                       // e.g., animation speed, tile set rows
    int32_t k = 0;                       // e.g., flags, layers
    
    // Binary data
    std::vector<uint8_t> data;           // Actual asset data
    
    // Additional metadata
    std::vector<std::string> tags;       // Searchable tags
    std::string description;             // Human-readable description
    std::string checksum;                // MD5 or SHA256 hash
    std::string version;                 // Version string (e.g., "1.0.0", "v2")
    std::string author;                  // Author/creator name
    
    // Timestamps
    std::time_t created_at = 0;
    std::time_t updated_at = 0;
    
    // Compression flag
    bool compressed = false;             // True if data is zstd compressed
    
    // Constructors
    AssetMetadata() = default;
    AssetMetadata(const std::string& name, AssetKind kind, AssetFormat format)
        : name(name), kind(kind), format(format) {}
    
    // Helper methods
    bool isValid() const { return !name.empty() && kind != AssetKind::UNKNOWN; }
    size_t getDataSize() const { return data.size(); }
    bool hasData() const { return !data.empty(); }
    
    // Get human-readable size
    std::string getDataSizeString() const;
    
    // Get formatted timestamps
    std::string getCreatedAtString() const;
    std::string getUpdatedAtString() const;
    
    // Check if has specific tag
    bool hasTag(const std::string& tag) const;
    
    // Add tag (if not already present)
    void addTag(const std::string& tag);
    
    // Remove tag
    void removeTag(const std::string& tag);
    
    // Get comma-separated tags string
    std::string getTagsString() const;
    
    // Set tags from comma-separated string
    void setTagsFromString(const std::string& tagsStr);
    
    // Get kind and format as strings
    std::string getKindString() const { return assetKindToString(kind); }
    std::string getFormatString() const { return assetFormatToString(format); }
    
    // Debug output
    std::string toString() const;
};

// Asset query parameters
struct AssetQuery {
    std::string namePattern;             // SQL LIKE pattern (e.g., "%player%")
    AssetKind kind = AssetKind::UNKNOWN; // Filter by kind (UNKNOWN = any)
    AssetFormat format = AssetFormat::UNKNOWN; // Filter by format
    std::vector<std::string> tags;       // Must have all these tags
    
    int32_t minWidth = 0;                // Minimum width
    int32_t maxWidth = 0;                // Maximum width (0 = no limit)
    int32_t minHeight = 0;
    int32_t maxHeight = 0;
    
    double minDuration = 0.0;
    double maxDuration = 0.0;
    
    std::string orderBy = "name";        // Sort field: name, created_at, updated_at, kind
    bool ascending = true;               // Sort direction
    
    int32_t limit = 0;                   // Result limit (0 = no limit)
    int32_t offset = 0;                  // Result offset for pagination
    
    // Constructors
    AssetQuery() = default;
    
    // Helper constructors
    static AssetQuery byName(const std::string& name) {
        AssetQuery q;
        q.namePattern = name;
        return q;
    }
    
    static AssetQuery byKind(AssetKind kind) {
        AssetQuery q;
        q.kind = kind;
        return q;
    }
    
    static AssetQuery byTag(const std::string& tag) {
        AssetQuery q;
        q.tags.push_back(tag);
        return q;
    }
    
    // Builder pattern methods
    AssetQuery& withName(const std::string& pattern) {
        namePattern = pattern;
        return *this;
    }
    
    AssetQuery& withKind(AssetKind k) {
        kind = k;
        return *this;
    }
    
    AssetQuery& withFormat(AssetFormat f) {
        format = f;
        return *this;
    }
    
    AssetQuery& withTag(const std::string& tag) {
        tags.push_back(tag);
        return *this;
    }
    
    AssetQuery& withDimensions(int32_t minW, int32_t maxW, int32_t minH, int32_t maxH) {
        minWidth = minW;
        maxWidth = maxW;
        minHeight = minH;
        maxHeight = maxH;
        return *this;
    }
    
    AssetQuery& sortBy(const std::string& field, bool asc = true) {
        orderBy = field;
        ascending = asc;
        return *this;
    }
    
    AssetQuery& limitTo(int32_t count, int32_t off = 0) {
        limit = count;
        offset = off;
        return *this;
    }
};

// Asset statistics
struct AssetStatistics {
    int64_t totalAssets = 0;
    int64_t totalDataSize = 0;           // Total bytes
    
    // Counts by kind
    int64_t spriteCount = 0;
    int64_t tileCount = 0;
    int64_t imageCount = 0;
    int64_t soundCount = 0;
    int64_t musicCount = 0;
    int64_t fontCount = 0;
    int64_t dataCount = 0;
    
    // Counts by format
    int64_t pngCount = 0;
    int64_t jpgCount = 0;
    int64_t wavCount = 0;
    int64_t mp3Count = 0;
    int64_t oggCount = 0;
    int64_t midiCount = 0;
    int64_t abcCount = 0;
    int64_t ttfCount = 0;
    
    // Size statistics
    int64_t largestAssetSize = 0;
    int64_t smallestAssetSize = 0;
    double averageAssetSize = 0.0;
    
    std::string largestAssetName;
    std::string smallestAssetName;
    
    // Get human-readable total size
    std::string getTotalSizeString() const;
    
    // Get average size string
    std::string getAverageSizeString() const;
    
    // Debug output
    std::string toString() const;
};

} // namespace SuperTerminal

#endif // ASSET_METADATA_H