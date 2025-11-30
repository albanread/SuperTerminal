# ABC Player Audio Subsystem

Modern ABC music notation player integrated into SuperTerminal via XPC service architecture.

## Overview

The ABC Player provides full ABC music notation parsing and playback through an isolated XPC service. This ensures crash isolation, better resource management, and follows modern macOS security practices.

## Architecture

```
SuperTerminal.app
│
├── src/audio/
│   ├── abc/                          # ABC Player Library (static)
│   │   ├── ABCParser.h/cpp           # Main parser coordinator
│   │   ├── ABCHeaderParser.h/cpp     # Parses X:, T:, M:, K:, etc.
│   │   ├── ABCMusicParser.h/cpp      # Note/chord/rhythm parsing
│   │   ├── ABCPlayer.h/cpp           # Playback engine
│   │   ├── ABCTypes.h                # Type definitions
│   │   ├── ABCVoiceManager.h/cpp     # Multi-voice support
│   │   ├── MIDIGenerator.h/cpp       # MIDI event generation
│   │   └── expand_abc_repeats.h/cpp  # Repeat expansion
│   │
│   ├── xpc/                          # XPC Service
│   │   ├── ABCPlayerService.mm       # Service implementation
│   │   ├── ABCPlayerServiceProtocol.h # XPC protocol definition
│   │   ├── ABCPlayerServiceProtocol.m # Protocol constants
│   │   └── Info.plist                # Service bundle config
│   │
│   ├── ABCPlayerXPCClient.h          # Client API (C++ & C)
│   └── ABCPlayerXPCClient.mm         # Client implementation
│
└── Contents/XPCServices/
    └── com.superterminal.ABCPlayer.xpc  # Deployed service bundle
```

## Components

### 1. ABC Player Library (`src/audio/abc/`)

**Full-featured ABC notation parser (Version: Snafu100)**

- **ABCParser**: Main parsing coordinator
  - Splits ABC content into lines
  - Manages header vs. body parsing
  - Expands repeats before parsing
  - Coordinates sub-parsers

- **ABCHeaderParser**: Metadata parser
  - `X:` - Reference number
  - `T:` - Title
  - `C:` - Composer
  - `M:` - Time signature (4/4, 3/4, 6/8, C, C|)
  - `L:` - Default note length
  - `K:` - Key signature
  - `V:` - Voice definitions
  - `Q:` - Tempo

- **ABCMusicParser**: Music notation parser
  - Notes: `C D E F G A B c d e f g a b`
  - Accidentals: `^C` (sharp), `_C` (flat), `=C` (natural)
  - Octaves: `,,,` (lower), `'''` (higher)
  - Durations: `C2` (double), `C/2` (half), `C3/2` (dotted)
  - Chords: `[CEG]` (multi-note), `"C"` (guitar symbols)
  - Rests: `z`, `z2`, `z/2`
  - Bar lines: `|`, `||`, `|:`, `:|`, `|1`, `|2`
  - Tuplets: `(3ABC` (triplet)
  - Ties/slurs: `-`, `()`
  - Inline voice switches: `[V:voice_name]`

- **ABCVoiceManager**: Polyphonic support
  - Multiple independent voices
  - Per-voice time tracking
  - Voice switching mid-tune
  - Channel assignment for MIDI

- **MIDIGenerator**: MIDI event generation
  - Converts parsed ABC to MIDI events
  - Note on/off with velocity
  - Program changes (instruments)
  - Tempo changes
  - Time signature metadata
  - Multi-track output

- **ABCPlayer**: Playback engine
  - Core Audio integration (macOS)
  - Real-time MIDI synthesis
  - Volume control
  - Pause/resume/stop
  - Synchronous and asynchronous modes

### 2. XPC Service (`src/audio/xpc/`)

**Isolated playback service running in separate process**

- **ABCPlayerService**: Main service implementation
  - Queue management with worker thread
  - Full ABC Player library integration
  - Thread-safe playback state
  - Automatic lifecycle management via launchd

- **Protocol**: Type-safe XPC communication
  - `playABC:withName:reply:` - Play ABC notation
  - `playABCFile:reply:` - Play from file
  - `stopWithReply:` - Stop playback
  - `pauseWithReply:` - Pause
  - `resumeWithReply:` - Resume
  - `setVolume:reply:` - Volume control (0.0-1.0)
  - `getStatusWithReply:` - Query state
  - `getQueueListWithReply:` - List queued songs
  - `pingWithReply:` - Health check

### 3. XPC Client (`src/audio/`)

**Client-side API for main application**

**C++ API:**
```cpp
#include "audio/ABCPlayerXPCClient.h"

SuperTerminal::ABCPlayerXPCClient client;
client.initialize();

// Play ABC notation
std::string abc = "X:1\nT:Scale\nC D E F G A B c";
client.playABC(abc, "Test Song");

// Control playback
client.setVolume(0.7f);
client.pause();
client.resume();
client.stop();

// Query status
auto status = client.getStatus();
if (status.is_playing) {
    std::cout << "Playing: " << status.current_song << std::endl;
}

client.shutdown();
```

**C API (for Lua integration):**
```c
#include "audio/ABCPlayerXPCClient.h"

abc_client_initialize();
abc_client_play_abc("X:1\nT:Test\nC D E F", "Song");
abc_client_set_volume(0.8f);
abc_client_stop();
abc_client_shutdown();
```

## Features

### Supported ABC Notation

- ✅ **Headers**: X, T, C, M, L, K, V, Q, O, P
- ✅ **Notes**: All pitches with octaves
- ✅ **Accidentals**: Sharps, flats, naturals
- ✅ **Durations**: Whole, half, quarter, eighth, dotted
- ✅ **Chords**: Multi-note `[CEG]` and guitar symbols `"Am7"`
- ✅ **Rests**: All durations
- ✅ **Bar lines**: Single, double, repeat markers
- ✅ **Repeats**: `|:` `:|` with volta brackets `|1` `|2`
- ✅ **Tuplets**: Triplets, quintuplets, etc.
- ✅ **Multi-voice**: Full polyphonic support
- ✅ **Inline fields**: Key/time changes mid-tune

### Not Yet Supported

- ⏳ Grace notes
- ⏳ Ornaments (trills, mordents)
- ⏳ Fermatas
- ⏳ Dynamics (crescendo, diminuendo)
- ⏳ Slurs/ties (parsed but not rendered)
- ⏳ Lyrics

## Benefits of XPC Architecture

1. **Crash Isolation**: Audio crashes don't affect SuperTerminal
2. **Resource Management**: System manages audio process independently
3. **Security**: Modern macOS sandboxing and entitlements
4. **Lifecycle**: launchd auto-starts and terminates service
5. **Type Safety**: Compile-time checked protocol

## Building

The ABC Player is built as part of SuperTerminal:

```bash
cd build
cmake ..
make ABCPlayerService    # Build XPC service
make SuperTerminal       # Build framework with client
make interactive_luarunner # Build app with embedded service
```

Built artifacts:
- `libABCPlayer.a` - Static library
- `com.superterminal.ABCPlayer.xpc/` - XPC service bundle
- `SuperTerminal.framework` - Framework with client
- `SuperTerminal.app/Contents/XPCServices/` - Deployed service

## Testing

### C++ Test
```bash
cd build
./test_abc_xpc
```

### Lua Test
```lua
-- From SuperTerminal app
dofile("examples/test_xpc_abc.lua")
```

### View Logs
```bash
# Real-time service logs
log stream --predicate 'process contains "ABCPlayer"' --level debug

# Recent logs
log show --predicate 'process contains "ABCPlayer"' --last 5m
```

## Performance

- **Connection**: ~10-50ms (first call), ~1-5ms (subsequent)
- **Parsing**: <1ms for typical ABC tune
- **Audio Start**: ~50-100ms (Core Audio init)
- **Memory**: ~5-10MB service base, ~1-2MB per song
- **CPU**: ~2-5% during playback, 0% when idle
- **Idle Timeout**: Service auto-terminates after 60s idle

## Example ABC Notation

```abc
X:1
T:Jingle Bells
C:James Lord Pierpont
M:4/4
L:1/4
K:G
B B B2|B B B2|B d G A|B4|
c c c c|c B B B|B A A B|A2 d2|
B B B B|B B B B|B d G A|B4|
c c c c|c B B B|d d c A|G4|
```

## Troubleshooting

### Service Won't Start
1. Check if app bundle contains service:
   ```bash
   ls -la SuperTerminal.app/Contents/XPCServices/
   ```
2. View console for errors:
   ```bash
   log show --predicate 'process contains "ABCPlayer"' --last 1m
   ```

### No Audio
- Verify Core Audio permissions in System Preferences
- Check MIDI synthesizer availability:
  ```bash
  ls -la /System/Library/Components/DLSMusicDevice.component
  ```
- Enable debug output: `client.setDebugOutput(true)`

### Connection Failures
- Ensure XPC service bundle ID matches: `com.superterminal.ABCPlayer`
- Service must be called from within app bundle context
- Check Info.plist is correct

## References

- ABC Notation Standard: http://abcnotation.com/
- XPC Services Guide: https://developer.apple.com/documentation/xpc
- Core Audio: https://developer.apple.com/documentation/coreaudio

## License

Copyright © 2024 SuperTerminal. All rights reserved.