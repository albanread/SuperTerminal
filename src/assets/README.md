# SuperTerminal Assets Management System

The SuperTerminal Assets Management System provides a comprehensive solution for managing game assets (sprites, sounds, music, tiles, images, fonts, and data) with SQLite-backed storage, caching, and seamless integration with the SuperTerminal framework.

## Architecture Overview

```
┌─────────────────────────────────────┐
│         Lua Scripts                 │
│  sprite_load_db("player")           │
│  asset_list("sprite")               │
└─────────────┬───────────────────────┘
              │
              ▼
┌─────────────────────────────────────┐
│      AssetsManager (C++)            │
│  • Check assets.db first            │
│  • Fallback to filesystem           │
│  • LRU caching                      │
│  • ID allocation                    │
└─────────────┬───────────────────────┘
              │
    ┌─────────┴──────────┐
    ▼                    ▼
┌──────────┐      ┌──────────────┐
│ Database │      │  Filesystem  │
│ assets.db│      │  (fallback)  │
└──────────┘      └──────────────┘
```

## Components

### 1. AssetMetadata (`AssetMetadata.h/cpp`)

Core data structures representing asset information:

**AssetKind Enumeration:**
- `SPRITE` - Game sprites (128x128 PNG)
- `TILE` - Tilemap tiles
- `IMAGE` - Generic images
- `SOUND` - Sound effects (WAV/MP3/OGG)
- `MUSIC` - Music tracks (ABC notation, MIDI)
- `FONT` - Fonts (TTF/OTF)
- `DATA` - Generic binary data

**AssetFormat Enumeration:**
- `PNG`, `JPG` - Image formats
- `WAV`, `MP3`, `OGG` - Audio formats
- `MIDI`, `ABC` - Music formats
- `TTF`, `OTF` - Font formats
- `BINARY` - Raw binary data

**AssetMetadata Structure:**
```cpp
struct AssetMetadata {
    int64_t id;              // Database ID
    std::string name;        // Unique asset name
    AssetKind kind;          // Asset type
    AssetFormat format;      // Data format
    
    int32_t width, height;   // Dimensions (for images/sprites)
    double duration;         // Duration in seconds (for audio)
    int32_t length;          // Length in measures/frames
    
    int32_t i, j, k;         // General purpose numeric fields
    std::vector<uint8_t> data; // Binary data
    
    std::vector<std::string> tags; // Searchable tags
    std::string description; // Human-readable description
    std::string checksum;    // Data integrity hash
    
    std::time_t created_at;  // Creation timestamp
    std::time_t updated_at;  // Last update timestamp
};
```

### 2. AssetDatabase (`AssetDatabase.h/cpp`)

Low-level SQLite wrapper for asset persistence.

**Key Features:**
- CRUD operations (Create, Read, Update, Delete)
- Efficient querying with filters
- Transaction support for batch operations
- Database maintenance (vacuum, integrity checks)
- Prepared statement caching for performance

**API Examples:**

```cpp
AssetDatabase db;
db.open("assets.db", false);
db.createSchema();

// Add asset
AssetMetadata sprite;
sprite.name = "player";
sprite.kind = AssetKind::SPRITE;
sprite.data = loadPNGFile("player.png");
auto result = db.addAsset(sprite);

// Query assets
AssetQuery query;
query.kind = AssetKind::SPRITE;
query.tags.push_back("player");
auto assets = db.queryAssets(query);

// Search by pattern
auto results = db.searchAssets("%enemy%");

// Get statistics
auto stats = db.getStatistics();
```

### 3. AssetsManager (`AssetsManager.h/cpp`)

High-level asset management with caching and loading.

**Key Features:**
- Automatic database and filesystem fallback
- LRU cache for frequently accessed assets
- Automatic ID allocation for sprites and sounds
- Direct loading into sprite/audio systems
- Preloading support for performance
- Multiple search paths

**Configuration:**

```cpp
AssetLoadConfig config;
config.enableCache = true;
config.maxCacheSize = 100 * 1024 * 1024;  // 100 MB
config.maxCachedAssets = 256;
config.fallbackToFilesystem = true;
config.spriteIdStart = 1;
config.spriteIdEnd = 255;

AssetsManager manager(config);
```

**Usage Examples:**

```cpp
// Initialize
AssetsManager manager;
manager.initialize("assets.db");

// Load sprite (auto-allocate ID)
uint16_t spriteId;
auto result = manager.loadSprite("player_walk", spriteId);

// Load sprite with specific ID
manager.loadSprite("enemy_01", 10);

// Load sound
uint32_t soundId;
manager.loadSound("jump_sound", soundId);

// Query assets
auto sprites = manager.listAssets(AssetKind::SPRITE);
for (const auto& name : sprites) {
    std::cout << name << std::endl;
}

// Search
auto results = manager.searchAssets("%player%");

// Get metadata without loading
AssetMetadata meta;
if (manager.getAssetMetadata("player_walk", meta)) {
    std::cout << "Size: " << meta.getDataSizeString() << std::endl;
}

// Cache management
manager.clearCache(AssetKind::SPRITE);
std::cout << "Cache size: " << manager.getCacheSize() << std::endl;

// Preload for performance
manager.preloadAssets(AssetKind::SPRITE);
manager.preloadAssetsByTag("level_1");

// Statistics
auto stats = manager.getLoadStatistics();
std::cout << "Cache hit rate: " << stats.getCacheHitRate() << std::endl;
```

## Command-Line Tool: `assetdb`

Located in `tools/assetdb/main.cpp`, this utility provides database management:

### Commands

**Initialize database:**
```bash
./assetdb init assets.db
```

**List assets:**
```bash
./assetdb list assets.db
./assetdb list assets.db --kind sprite
./assetdb list assets.db --format png
```

**Search assets:**
```bash
./assetdb search assets.db "player"
./assetdb search assets.db "enemy_*"
```

**Get asset info:**
```bash
./assetdb info assets.db player_sprite
```

**Add asset:**
```bash
./assetdb add assets.db player.png --name player_sprite --kind sprite --tags "player,character"
```

**Delete asset:**
```bash
./assetdb delete assets.db player_sprite
```

**Export asset:**
```bash
./assetdb export assets.db player_sprite output.png
```

**Import assets:**
```bash
./assetdb import assets.db sprites/ --kind sprite --batch
```

**Statistics:**
```bash
./assetdb stats assets.db
```

**Vacuum database:**
```bash
./assetdb vacuum assets.db
```

**Version info:**
```bash
./assetdb version
```

## Database Schema

```sql
CREATE TABLE assets (
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
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX idx_assets_name ON assets(name);
CREATE INDEX idx_assets_kind ON assets(kind);
CREATE INDEX idx_assets_format ON assets(format);
CREATE INDEX idx_assets_tags ON assets(tags);
```

## File Locations

### Development
- **Primary database:** `assets.db` (in project root)
- **Test database:** `test_assets.db` (created by tests)

### App Bundle
- **Read-only database:** `SuperTerminal.app/Contents/Resources/assets.db`
- Packaged with the application
- Contains shipped game assets

### User Assets
- **User database:** `~/Library/Application Support/SuperTerminal/assets/assets.db`
- User-created or downloaded assets
- Writable location for user modifications

## Integration with SuperTerminal

### Sprite Loading

Traditional filesystem loading:
```cpp
sprite_load(1, "assets/sprite001.png");
```

Database loading (future):
```cpp
sprite_load_db(1, "player_walk");
```

### Search Paths

The AssetsManager searches in order:
1. Primary database path (specified at initialization)
2. App bundle resources (`Contents/Resources/`)
3. User assets directory (`~/Library/Application Support/SuperTerminal/assets/`)
4. Additional search paths (added via `addSearchPath()`)
5. Current working directory

## Best Practices

### Asset Naming
- Use descriptive names: `player_walk_01` instead of `sprite1`
- Use underscores for word separation
- Include variant numbers: `enemy_red_01`, `enemy_red_02`
- Prefix by category: `ui_button`, `sfx_jump`, `music_level1`

### Tagging
- Use consistent tag conventions
- Tag by category: `player`, `enemy`, `ui`, `sfx`
- Tag by level/area: `level1`, `forest`, `dungeon`
- Tag by state: `menu`, `gameplay`, `cutscene`

### Performance
- Preload assets at level start
- Use tags to preload related assets together
- Set appropriate cache limits based on memory
- Clear cache between levels to free memory

### Organization
```
assets/
├── sprites/
│   ├── player/
│   ├── enemies/
│   └── objects/
├── sounds/
│   ├── sfx/
│   └── music/
├── tiles/
└── fonts/
```

## Workflow Example

### 1. Create Database and Import Assets

```bash
# Initialize database
./assetdb init game_assets.db

# Import sprites
./assetdb import game_assets.db assets/sprites/player/ \
    --kind sprite --tags "player,character" --batch

./assetdb import game_assets.db assets/sprites/enemies/ \
    --kind sprite --tags "enemy" --batch

# Import sounds
./assetdb import game_assets.db assets/sounds/sfx/ \
    --kind sound --tags "sfx" --batch

./assetdb import game_assets.db assets/sounds/music/ \
    --kind music --tags "music,background" --batch
```

### 2. Use in C++ Code

```cpp
// Initialize asset manager
AssetsManager assetMgr;
assetMgr.initialize("game_assets.db");

// Preload level assets
assetMgr.preloadAssetsByTag("level1");

// Load player sprite
uint16_t playerId;
assetMgr.loadSprite("player_walk", playerId);

// Use in game
sprite_show(playerId, 100, 100);
```

### 3. Query and Manage

```bash
# Check what's in the database
./assetdb stats game_assets.db

# Search for specific assets
./assetdb search game_assets.db "player"

# Export an asset
./assetdb export game_assets.db player_walk exported_sprite.png
```

## Testing

Comprehensive tests available in `tests/cpp/test_assets_manager.cpp`:

```bash
cd build
make test_assets_manager
./test_assets_manager
```

**Test Coverage:**
- Initialization and configuration
- Database operations (add, remove, update, query)
- Asset loading and caching
- ID allocation (sprites and sounds)
- Search and filtering
- Statistics and monitoring
- Import/export functionality
- Cache management (LRU eviction)

## Future Enhancements

### Planned Features
1. **Lua Bindings** - Direct database access from Lua scripts
2. **Streaming Support** - Large asset streaming for music/video
3. **Compression** - Automatic compression for BLOB data
4. **Encryption** - Optional encryption for protected assets
5. **Remote Assets** - Download assets from remote servers
6. **Asset Bundles** - Pack multiple assets into single files
7. **Hot Reload** - Reload assets without restarting
8. **Asset Editor** - GUI tool for asset management

### Integration TODO
- [ ] Implement Lua bindings (`src/assets/AssetsLuaBindings.cpp`)
- [ ] Add sprite_load_db(), sound_load_db() functions
- [ ] Integrate with existing asset loading pipeline
- [ ] Add CMake packaging rules for assets.db in app bundle
- [ ] Create default assets.db for distribution
- [ ] Add image decoding support (stb_image or libpng)
- [ ] Implement direct memory loading (bypass temp files)

## Performance Considerations

### Database Performance
- Prepared statements are cached for repeated queries
- Indexes on name, kind, format, and tags for fast lookups
- BLOB data stored efficiently in SQLite pages
- Vacuum regularly to reclaim space

### Cache Performance
- LRU eviction policy ensures most-used assets stay cached
- Configurable cache size limits (memory and count)
- Cache hit/miss statistics for tuning
- Preloading reduces load-time hitching

### Memory Usage
- Default cache: 100 MB, 256 assets
- Adjust based on target platform
- Clear cache between levels
- Use preloading judiciously

## Troubleshooting

### Database Locked
**Problem:** `SQLITE_BUSY` error when accessing database  
**Solution:** Ensure only one writer at a time, use transactions for batch operations

### Asset Not Found
**Problem:** Asset exists but isn't loaded  
**Solution:** Check search paths with `getSearchPaths()`, verify asset name spelling

### Memory Issues
**Problem:** Application crashes due to memory  
**Solution:** Reduce `maxCacheSize`, clear cache more frequently, preload selectively

### Slow Loading
**Problem:** Assets load slowly  
**Solution:** Use preloading, check cache hit rate, vacuum database, add missing indexes

## API Reference

See header files for complete API documentation:
- `AssetMetadata.h` - Data structures and enums
- `AssetDatabase.h` - Low-level database operations
- `AssetsManager.h` - High-level asset management

## License

Copyright © 2024 SuperTerminal. All rights reserved.