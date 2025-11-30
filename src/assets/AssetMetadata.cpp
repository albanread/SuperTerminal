//
//  AssetMetadata.cpp
//  SuperTerminal Framework - Asset Database System
//
//  Implementation of asset metadata structures and utilities
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#include "AssetMetadata.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace SuperTerminal {

// Convert enum to string
const char* assetKindToString(AssetKind kind) {
    switch (kind) {
        case AssetKind::SPRITE: return "sprite";
        case AssetKind::TILE: return "tile";
        case AssetKind::TILEMAP: return "tilemap";
        case AssetKind::IMAGE: return "image";
        case AssetKind::SOUND: return "sound";
        case AssetKind::MUSIC: return "music";
        case AssetKind::FONT: return "font";
        case AssetKind::SCRIPT: return "script";
        case AssetKind::TEXTSCREEN: return "textscreen";
        case AssetKind::DATA: return "data";
        default: return "unknown";
    }
}

const char* assetFormatToString(AssetFormat format) {
    switch (format) {
        case AssetFormat::PNG: return "png";
        case AssetFormat::JPG: return "jpg";
        case AssetFormat::WAV: return "wav";
        case AssetFormat::MP3: return "mp3";
        case AssetFormat::OGG: return "ogg";
        case AssetFormat::MIDI: return "midi";
        case AssetFormat::ABC: return "abc";
        case AssetFormat::TTF: return "ttf";
        case AssetFormat::OTF: return "otf";
        case AssetFormat::LUA: return "lua";
        case AssetFormat::PCM: return "pcm";
        case AssetFormat::TILEMAP_DATA: return "tilemap_data";
        case AssetFormat::TEXTSCREEN_DATA: return "textscreen_data";
        case AssetFormat::BINARY: return "binary";
        default: return "unknown";
    }
}

// Convert string to enum
AssetKind stringToAssetKind(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    if (lower == "sprite") return AssetKind::SPRITE;
    if (lower == "tile") return AssetKind::TILE;
    if (lower == "tilemap") return AssetKind::TILEMAP;
    if (lower == "image") return AssetKind::IMAGE;
    if (lower == "sound") return AssetKind::SOUND;
    if (lower == "music") return AssetKind::MUSIC;
    if (lower == "font") return AssetKind::FONT;
    if (lower == "script") return AssetKind::SCRIPT;
    if (lower == "textscreen") return AssetKind::TEXTSCREEN;
    if (lower == "data") return AssetKind::DATA;
    
    return AssetKind::UNKNOWN;
}

AssetFormat stringToAssetFormat(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Remove leading dot if present
    if (!lower.empty() && lower[0] == '.') {
        lower = lower.substr(1);
    }
    
    if (lower == "png") return AssetFormat::PNG;
    if (lower == "jpg" || lower == "jpeg") return AssetFormat::JPG;
    if (lower == "wav") return AssetFormat::WAV;
    if (lower == "mp3") return AssetFormat::MP3;
    if (lower == "ogg") return AssetFormat::OGG;
    if (lower == "midi" || lower == "mid") return AssetFormat::MIDI;
    if (lower == "abc") return AssetFormat::ABC;
    if (lower == "ttf") return AssetFormat::TTF;
    if (lower == "otf") return AssetFormat::OTF;
    if (lower == "lua") return AssetFormat::LUA;
    if (lower == "pcm") return AssetFormat::PCM;
    if (lower == "tilemap_data" || lower == "tilemap") return AssetFormat::TILEMAP_DATA;
    if (lower == "textscreen_data" || lower == "textscreen") return AssetFormat::TEXTSCREEN_DATA;
    if (lower == "binary" || lower == "bin") return AssetFormat::BINARY;
    
    return AssetFormat::UNKNOWN;
}

// Helper function to format file size
static std::string formatSize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return oss.str();
}

// Helper function to format timestamp
static std::string formatTime(std::time_t time) {
    if (time == 0) {
        return "N/A";
    }
    
    std::tm* tm = std::localtime(&time);
    if (!tm) {
        return "Invalid";
    }
    
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buffer);
}

// AssetMetadata implementation
std::string AssetMetadata::getDataSizeString() const {
    return formatSize(data.size());
}

std::string AssetMetadata::getCreatedAtString() const {
    return formatTime(created_at);
}

std::string AssetMetadata::getUpdatedAtString() const {
    return formatTime(updated_at);
}

bool AssetMetadata::hasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

void AssetMetadata::addTag(const std::string& tag) {
    if (!hasTag(tag) && !tag.empty()) {
        tags.push_back(tag);
    }
}

void AssetMetadata::removeTag(const std::string& tag) {
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
}

std::string AssetMetadata::getTagsString() const {
    if (tags.empty()) {
        return "";
    }
    
    std::ostringstream oss;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i > 0) oss << ",";
        oss << tags[i];
    }
    return oss.str();
}

void AssetMetadata::setTagsFromString(const std::string& tagsStr) {
    tags.clear();
    
    if (tagsStr.empty()) {
        return;
    }
    
    std::istringstream iss(tagsStr);
    std::string tag;
    while (std::getline(iss, tag, ',')) {
        // Trim whitespace
        tag.erase(0, tag.find_first_not_of(" \t\n\r"));
        tag.erase(tag.find_last_not_of(" \t\n\r") + 1);
        
        if (!tag.empty()) {
            tags.push_back(tag);
        }
    }
}

std::string AssetMetadata::toString() const {
    std::ostringstream oss;
    oss << "Asset: " << name << "\n";
    oss << "  ID: " << id << "\n";
    oss << "  Kind: " << getKindString() << "\n";
    oss << "  Format: " << getFormatString() << "\n";
    
    if (width > 0 || height > 0) {
        oss << "  Dimensions: " << width << "×" << height << "\n";
    }
    
    if (duration > 0.0) {
        oss << "  Duration: " << duration << " seconds\n";
    }
    
    if (length > 0) {
        oss << "  Length: " << length << "\n";
    }
    
    if (i != 0 || j != 0 || k != 0) {
        oss << "  Numeric: i=" << i << " j=" << j << " k=" << k << "\n";
    }
    
    oss << "  Data Size: " << getDataSizeString() << "\n";
    
    if (!tags.empty()) {
        oss << "  Tags: " << getTagsString() << "\n";
    }
    
    if (!description.empty()) {
        oss << "  Description: " << description << "\n";
    }
    
    if (!checksum.empty()) {
        oss << "  Checksum: " << checksum << "\n";
    }
    
    oss << "  Created: " << getCreatedAtString() << "\n";
    oss << "  Updated: " << getUpdatedAtString() << "\n";
    
    return oss.str();
}

// AssetStatistics implementation
std::string AssetStatistics::getTotalSizeString() const {
    return formatSize(totalDataSize);
}

std::string AssetStatistics::getAverageSizeString() const {
    return formatSize(static_cast<size_t>(averageAssetSize));
}

std::string AssetStatistics::toString() const {
    std::ostringstream oss;
    oss << "Asset Database Statistics\n";
    oss << "========================\n";
    oss << "Total Assets: " << totalAssets << "\n";
    oss << "Total Size: " << getTotalSizeString() << "\n";
    oss << "Average Size: " << getAverageSizeString() << "\n";
    oss << "\n";
    
    oss << "By Kind:\n";
    if (spriteCount > 0) oss << "  Sprites: " << spriteCount << "\n";
    if (tileCount > 0) oss << "  Tiles: " << tileCount << "\n";
    if (imageCount > 0) oss << "  Images: " << imageCount << "\n";
    if (soundCount > 0) oss << "  Sounds: " << soundCount << "\n";
    if (musicCount > 0) oss << "  Music: " << musicCount << "\n";
    if (fontCount > 0) oss << "  Fonts: " << fontCount << "\n";
    if (dataCount > 0) oss << "  Data: " << dataCount << "\n";
    oss << "\n";
    
    oss << "By Format:\n";
    if (pngCount > 0) oss << "  PNG: " << pngCount << "\n";
    if (jpgCount > 0) oss << "  JPG: " << jpgCount << "\n";
    if (wavCount > 0) oss << "  WAV: " << wavCount << "\n";
    if (mp3Count > 0) oss << "  MP3: " << mp3Count << "\n";
    if (oggCount > 0) oss << "  OGG: " << oggCount << "\n";
    if (midiCount > 0) oss << "  MIDI: " << midiCount << "\n";
    if (abcCount > 0) oss << "  ABC: " << abcCount << "\n";
    if (ttfCount > 0) oss << "  TTF: " << ttfCount << "\n";
    oss << "\n";
    
    if (largestAssetSize > 0) {
        oss << "Largest Asset: " << largestAssetName 
            << " (" << formatSize(largestAssetSize) << ")\n";
    }
    
    if (smallestAssetSize > 0) {
        oss << "Smallest Asset: " << smallestAssetName 
            << " (" << formatSize(smallestAssetSize) << ")\n";
    }
    
    return oss.str();
}

} // namespace SuperTerminal