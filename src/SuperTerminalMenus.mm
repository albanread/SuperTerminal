//
//  SuperTerminalMenus.mm
//  SuperTerminal Framework - Native macOS Menu System
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Provides native macOS menu bar and context menus for SuperTerminal
//

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "SuperTerminal.h"
#include "CoreTextRenderer.h"
#include "GlobalShutdown.h"
#include "SubsystemManager.h"
#include "OverlayGraphicsLayer.h"
#include "ParticleSystem.h"
#include "BulletSystem.h"
#include "audio/AudioSystem.h"
#include "assets/AssetDialogs.h"
#include "assets/AssetsManager.h"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// Forward declarations
struct lua_State;
typedef struct lua_State lua_State;

// Global function pointer for external run script callback
typedef void (*run_script_callback_t)();
run_script_callback_t g_external_run_script_callback = nullptr;

// Lua GCD Runtime forward declarations
extern "C" {
    bool lua_gcd_initialize(void);
    void lua_gcd_shutdown(void);
    bool lua_gcd_exec(const char* lua_code, const char* script_name);
    bool lua_gcd_stop_script(void);
    bool lua_gcd_is_script_running(void);
    const char* lua_gcd_get_last_error(void);
    const char* lua_gcd_get_current_script_name(void);
    void lua_gcd_reset_repl(void);
}

// Forward declarations for editor functions
extern "C" {
    bool editor_is_active(void);
    bool editor_toggle(void);
    bool editor_save(const char* filename);
    bool editor_load(const char* filename);
    bool editor_load_file(const char* filepath);
    const char* editor_get_current_filename(void);
    void editor_execute_current(void);
    void editor_set_status(const char* message, int duration);
    bool editor_is_modified(void);
    void editor_new_file(void);
    char* editor_get_content(void);
    void editor_deactivate(void);
    void layer_set_enabled(int layer, bool enabled);
    void editor_shutdown(void);

    // REPL Console functions
    bool repl_initialize();
    void repl_shutdown();
    bool repl_is_initialized();
    void repl_activate();
    void repl_deactivate();
    void repl_toggle();
    bool repl_is_active();

    // Input system functions
    void input_system_key_down(int keycode);
    void input_system_key_up(int keycode);

    // Reset system functions (using SubsystemManager)
    // Note: These functions are now handled by SubsystemManager reset functionality

    // Lua management functions
    void reset_lua(void);
    void reset_lua_complete(void);
    void terminate_lua_script_with_cleanup(void);
    bool lua_is_executing(void);
    bool lua_terminate_current_script(void);
    char* editor_get_content(void);
    lua_State* lua_get_state(void);
    bool is_script_running(void);

    // Lua formatter function
    void editor_format_lua_code(void);
    void cleanup_finished_executions(void);

    // Shutdown system functions
    void reset_shutdown_system(void);

    // Direct script loading functions (zero deadlock)
    bool direct_load_script_file(const char* filename);
    bool direct_execute_script(const char* script_content);

    // REPL mode query function
    bool editor_is_repl_mode(void);

    // Note: CRT effects control functions now provided by CoreTextRenderer.h

    // Force shutdown function
    void superterminal_force_shutdown(void);

    // Editor jump to line function
    void editor_jump_to_line(int line_number);

    // Status bar update functions
    void superterminal_update_status(const char* status);
    void superterminal_update_cursor_position(int line, int col);
    void superterminal_update_script_name(const char* scriptName);

    // Graphics function
    void graphics_swap(void);
}

// Helper function to extract line number from Lua error message
static int extract_line_number_from_error(const char* error_msg) {
    if (!error_msg) return -1;

    // Lua error format: "[string ...]:<line>: <error message>"
    // Or: "Lua Load Error: [string ...]:<line>: <error message>"
    std::string error_str(error_msg);

    // Find the pattern ":<number>:"
    size_t colon_pos = error_str.find(":");
    while (colon_pos != std::string::npos) {
        size_t next_colon = error_str.find(":", colon_pos + 1);
        if (next_colon != std::string::npos) {
            std::string between = error_str.substr(colon_pos + 1, next_colon - colon_pos - 1);
            // Check if this is a number
            bool all_digits = !between.empty();
            for (char c : between) {
                if (!isdigit(c) && c != ' ') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                return atoi(between.c_str());
            }
        }
        colon_pos = error_str.find(":", colon_pos + 1);
    }

    return -1;
}

// Helper function to clean up Lua error message for display
static std::string clean_lua_error_message(const char* error_msg) {
    if (!error_msg) return "Unknown error";

    std::string error_str(error_msg);

    // Remove "Lua Load Error: " prefix if present
    size_t lua_prefix = error_str.find("Lua Load Error: ");
    if (lua_prefix == 0) {
        error_str = error_str.substr(16);
    }

    // Remove "Lua Error: " prefix if present
    lua_prefix = error_str.find("Lua Error: ");
    if (lua_prefix == 0) {
        error_str = error_str.substr(11);
    }

    // Find and remove [string "..."] pattern
    size_t bracket_start = error_str.find("[string");
    if (bracket_start != std::string::npos) {
        size_t bracket_end = error_str.find("]", bracket_start);
        if (bracket_end != std::string::npos) {
            // Find the line number after the bracket
            size_t colon_pos = error_str.find(":", bracket_end);
            if (colon_pos != std::string::npos) {
                size_t next_colon = error_str.find(":", colon_pos + 1);
                if (next_colon != std::string::npos) {
                    // Extract just the error message after ":<line>: "
                    error_str = error_str.substr(next_colon + 2);
                }
            }
        }
    }

    // Trim whitespace
    size_t start = error_str.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        error_str = error_str.substr(start);
    }

    return error_str;
}

// Show Lua error dialog with Go to Line button
static void show_lua_error_dialog(const char* error_msg, const char* script_name) {
    if (!error_msg) return;

    // Capture strings before async block
    NSString *errorMsgCopy = [NSString stringWithUTF8String:error_msg];
    NSString *scriptNameCopy = script_name ? [NSString stringWithUTF8String:script_name] : @"script";

    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert *alert = [[NSAlert alloc] init];
        NSString *title = [NSString stringWithFormat:@"Syntax Error in %@", scriptNameCopy];
        [alert setMessageText:title];
        [alert setAlertStyle:NSAlertStyleCritical];

        // Extract line number from error message
        int line_number = extract_line_number_from_error([errorMsgCopy UTF8String]);

        // Clean up the error message for better readability
        std::string cleaned_error = clean_lua_error_message([errorMsgCopy UTF8String]);
        NSString *errorText = [NSString stringWithUTF8String:cleaned_error.c_str()];

        // Set informative text with line number
        if (line_number > 0) {
            [alert setInformativeText:[NSString stringWithFormat:@"Line %d:", line_number]];
        } else {
            [alert setInformativeText:@"Error details:"];
        }

        // Create a scrollable text view for the error message
        NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 400, 100)];
        [scrollView setHasVerticalScroller:YES];
        [scrollView setHasHorizontalScroller:NO];
        [scrollView setBorderType:NSBezelBorder];
        [scrollView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        NSSize contentSize = [scrollView contentSize];
        NSTextView *textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, contentSize.width, contentSize.height)];

        [textView setMinSize:NSMakeSize(0.0, contentSize.height)];
        [textView setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
        [textView setVerticallyResizable:YES];
        [textView setHorizontallyResizable:NO];
        [textView setAutoresizingMask:NSViewWidthSizable];

        [[textView textContainer] setContainerSize:NSMakeSize(contentSize.width, FLT_MAX)];
        [[textView textContainer] setWidthTracksTextView:YES];

        [textView setString:errorText ? errorText : @"(no error message)"];
        [textView setEditable:NO];
        [textView setSelectable:YES];
        [textView setFont:[NSFont fontWithName:@"Monaco" size:12]];
        [textView setTextColor:[NSColor labelColor]];
        [textView setBackgroundColor:[NSColor controlBackgroundColor]];

        [scrollView setDocumentView:textView];
        [alert setAccessoryView:scrollView];

        // Add buttons
        [alert addButtonWithTitle:@"OK"];

        if (line_number > 0) {
            [alert addButtonWithTitle:@"Go to Line"];
        }

        // Show alert and handle response
        NSModalResponse response = [alert runModal];

        if (response == NSAlertSecondButtonReturn && line_number > 0) {
            // User clicked "Go to Line"
            editor_jump_to_line(line_number);
        }
    });
}

// C interface for showing Lua error dialog (can be called from C++ code)
extern "C" void show_lua_error_alert(const char* error_msg, const char* script_name) {
    show_lua_error_dialog(error_msg, script_name);
}

// Menu system state
static NSMenu* g_mainMenuBar = nil;
static NSMenu* g_contextMenu = nil;
static NSView* g_targetView = nil;
static bool g_repl_mode_enabled = false;

@interface SuperTerminalMenuDelegate : NSObject <NSMenuDelegate>
@end

@implementation SuperTerminalMenuDelegate

- (void)menuWillOpen:(NSMenu *)menu {
    // Update menu items based on current editor state
    [self updateMenuStates];
}

- (void)updateMenuStates {
    bool editorActive = editor_is_active();
    bool isModified = editor_is_modified();

    // Update main menu bar items
    if (g_mainMenuBar) {
        NSMenuItem* fileMenu = [g_mainMenuBar itemWithTitle:@"File"];
        if (fileMenu && [fileMenu hasSubmenu]) {
            NSMenu* submenu = [fileMenu submenu];



            NSMenuItem* saveItem = [submenu itemWithTitle:@"Save"];
            if (saveItem) {
                [saveItem setEnabled:editorActive];
                if (isModified) {
                    [saveItem setTitle:@"Save *"];
                } else {
                    [saveItem setTitle:@"Save"];
                }
            }

            NSMenuItem* runItem = [submenu itemWithTitle:@"Run Script"];
            if (runItem) {
                [runItem setEnabled:YES]; // Always enabled - can run script even when editor not visible
            }

            NSMenuItem* showEditorItem = [submenu itemWithTitle:@"Show Editor"];
            if (showEditorItem) {
                [showEditorItem setEnabled:!editorActive]; // Enabled when editor is hidden
            }

            NSMenuItem* hideEditorItem = [submenu itemWithTitle:@"Hide Editor"];
            if (hideEditorItem) {
                [hideEditorItem setEnabled:editorActive]; // Enabled when editor is visible
            }

            // Find and update REPL mode menu item
            NSMenuItem* replModeItem = nil;
            for (NSMenuItem* item in [submenu itemArray]) {
                if ([[item title] containsString:@"REPL Mode"]) {
                    replModeItem = item;
                    break;
                }
            }
            if (replModeItem) {
                bool replActive = repl_is_active();
                [replModeItem setTitle:(replActive ? @"Hide REPL Console" : @"Show REPL Console")];
                [replModeItem setEnabled:YES]; // Always enabled
            }
        }
    }

    // Update font menu CRT effects state
    NSMenuItem* fontMenu = [g_mainMenuBar itemWithTitle:@"Font"];
    if (fontMenu && [fontMenu hasSubmenu]) {
        NSMenu* submenu = [fontMenu submenu];

        NSMenuItem* glowItem = [submenu itemWithTitle:@"CRT Glow Effect"];
        if (glowItem) {
            bool isEnabled = coretext_get_crt_glow();
            [glowItem setState:isEnabled ? NSControlStateValueOn : NSControlStateValueOff];
        }

        NSMenuItem* scanlinesItem = [submenu itemWithTitle:@"CRT Scanlines"];
        if (scanlinesItem) {
            bool isEnabled = coretext_get_crt_scanlines();
            [scanlinesItem setState:isEnabled ? NSControlStateValueOn : NSControlStateValueOff];
        }
    }

    // Update context menu items
    if (g_contextMenu) {
        NSMenuItem* saveItem = [g_contextMenu itemWithTitle:@"Save"];
        if (saveItem) {
            [saveItem setEnabled:editorActive];
            if (isModified) {
                [saveItem setTitle:@"Save *"];
            } else {
                [saveItem setTitle:@"Save"];
            }
        }

        NSMenuItem* runItem = [g_contextMenu itemWithTitle:@"Run Script"];
        if (runItem) {
            [runItem setEnabled:YES]; // Always enabled - can run script even when editor not visible
        }

        NSMenuItem* showEditorItem = [g_contextMenu itemWithTitle:@"Show Editor"];
        if (showEditorItem) {
            [showEditorItem setEnabled:!editorActive]; // Enabled when editor is hidden
        }

        NSMenuItem* hideEditorItem = [g_contextMenu itemWithTitle:@"Hide Editor"];
        if (hideEditorItem) {
            [hideEditorItem setEnabled:editorActive]; // Enabled when editor is visible
        }
    }
}

@end

static SuperTerminalMenuDelegate* g_menuDelegate = nil;

// Menu action implementations
@interface SuperTerminalMenuActions : NSObject
- (IBAction)showEditor:(id)sender;
- (IBAction)hideEditor:(id)sender;
- (IBAction)toggleCRTGlow:(id)sender;
- (IBAction)toggleCRTScanlines:(id)sender;
- (void)switchTextMode:(id)sender;
- (IBAction)resetLua:(id)sender;
- (IBAction)toggleReplMode:(id)sender;
- (IBAction)resetReplState:(id)sender;
- (IBAction)formatLuaCode:(id)sender;
- (IBAction)checkSyntax:(id)sender;
- (IBAction)forceQuitApplication:(id)sender;
@end

@implementation SuperTerminalMenuActions

- (void)deleteAsset:(id)sender {
    NSLog(@"Delete Asset menu item selected");

    @autoreleasepool {
        // Get list of all assets
        extern std::vector<std::string> assets_get_all_names();
        std::vector<std::string> assetNames = assets_get_all_names();

        if (assetNames.empty()) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert setMessageText:@"No Assets Found"];
            [alert setInformativeText:@"The assets database is empty."];
            [alert setAlertStyle:NSAlertStyleInformational];
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return;
        }

        // Create dialog
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Delete Asset"];
        [alert setInformativeText:@"Select an asset to delete from the database:"];
        [alert setAlertStyle:NSAlertStyleWarning];
        [alert addButtonWithTitle:@"Delete"];
        [alert addButtonWithTitle:@"Cancel"];

        // Create popup button for asset selection
        NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 400, 25) pullsDown:NO];

        // Add assets to popup
        for (const auto& name : assetNames) {
            NSString* assetName = [NSString stringWithUTF8String:name.c_str()];
            [popup addItemWithTitle:assetName];
        }

        // Create container view with popup and info
        NSView* accessoryView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 60)];

        NSTextField* label = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 35, 400, 20)];
        [label setStringValue:@"Asset to delete:"];
        [label setBezeled:NO];
        [label setDrawsBackground:NO];
        [label setEditable:NO];
        [label setSelectable:NO];

        [popup setFrame:NSMakeRect(0, 5, 400, 25)];

        [accessoryView addSubview:label];
        [accessoryView addSubview:popup];

        [alert setAccessoryView:accessoryView];

        // Show dialog
        NSModalResponse response = [alert runModal];

        if (response == NSAlertFirstButtonReturn) {
            // User clicked Delete
            NSString* selectedAsset = [popup titleOfSelectedItem];
            if (selectedAsset && [selectedAsset length] > 0) {
                // Confirm deletion
                NSAlert* confirmAlert = [[NSAlert alloc] init];
                [confirmAlert setMessageText:@"Confirm Deletion"];
                [confirmAlert setInformativeText:[NSString stringWithFormat:@"Are you sure you want to permanently delete '%@'?\n\nThis action cannot be undone.", selectedAsset]];
                [confirmAlert setAlertStyle:NSAlertStyleCritical];
                [confirmAlert addButtonWithTitle:@"Delete"];
                [confirmAlert addButtonWithTitle:@"Cancel"];

                NSModalResponse confirmResponse = [confirmAlert runModal];

                if (confirmResponse == NSAlertFirstButtonReturn) {
                    // Perform deletion
                    extern bool assets_delete_by_name(const char* name);
                    const char* assetNameC = [selectedAsset UTF8String];
                    bool success = assets_delete_by_name(assetNameC);

                    if (success) {
                        NSAlert* successAlert = [[NSAlert alloc] init];
                        [successAlert setMessageText:@"Asset Deleted"];
                        [successAlert setInformativeText:[NSString stringWithFormat:@"'%@' has been deleted from the database.", selectedAsset]];
                        [successAlert setAlertStyle:NSAlertStyleInformational];
                        [successAlert addButtonWithTitle:@"OK"];
                        [successAlert runModal];

                        NSLog(@"Successfully deleted asset: %@", selectedAsset);
                    } else {
                        NSAlert* errorAlert = [[NSAlert alloc] init];
                        [errorAlert setMessageText:@"Deletion Failed"];
                        [errorAlert setInformativeText:[NSString stringWithFormat:@"Failed to delete '%@' from the database.", selectedAsset]];
                        [errorAlert setAlertStyle:NSAlertStyleCritical];
                        [errorAlert addButtonWithTitle:@"OK"];
                        [errorAlert runModal];

                        NSLog(@"Failed to delete asset: %@", selectedAsset);
                    }
                }
            }
        }
    }
}



- (IBAction)saveFile:(id)sender {
    if (!editor_is_active()) {
        editor_set_status("ERROR: Editor not active", 180);
        return;
    }

    const char* current_filename = editor_get_current_filename();

    // Update status bar with current script name if we have one
    if (current_filename && strlen(current_filename) > 0) {
        superterminal_update_script_name(current_filename);
    }
    // Get database metadata from editor
    bool isFromDB = editor_is_loaded_from_database();

    if (isFromDB) {
        // Script was loaded from database, update it
        const char* scriptName = editor_get_db_script_name();
        const char* version = editor_get_db_version();
        const char* author = editor_get_db_author();

        if (scriptName && strlen(scriptName) > 0) {
            // Get content and save to database
            char* content = editor_get_content();
            if (content) {
                NSString* scriptContent = [NSString stringWithUTF8String:content];
                free(content);

                SuperTerminal::AssetsManager* assetsManager = SubsystemManager::getInstance().getAssetsManager();
                if (assetsManager) {
                    // Delete old entry and save new one with same metadata
                    assetsManager->removeAsset(scriptName);
                    std::vector<std::string> tags;
                    bool success = assetsManager->saveScript(
                        scriptName,
                        [scriptContent UTF8String],
                        version ? version : "",
                        author ? author : "",
                        tags
                    );

                    if (success) {
                        editor_set_status([[NSString stringWithFormat:@"Saved '%s' to database", scriptName] UTF8String], 180);
                    } else {
                        editor_set_status([[NSString stringWithFormat:@"Save failed: %s", assetsManager->getLastError().c_str()] UTF8String], 180);
                    }
                } else {
                    editor_set_status("Assets manager not available", 180);
                }
            }
        } else {
            // Metadata missing, show save dialog
            [self saveScriptToDatabase:sender];
        }
    } else {
        // Not from database, show save dialog to create new entry
        [self saveScriptToDatabase:sender];
    }
}

- (IBAction)saveAsFile:(id)sender {
    NSLog(@"Menu: Save As File");
    if (!editor_is_active()) {
        editor_set_status("ERROR: Editor not active", 180);
        return;
    }

    NSSavePanel* savePanel = [NSSavePanel savePanel];
    [savePanel setTitle:@"Save Lua Script"];
    if (@available(macOS 12.0, *)) {
        [savePanel setAllowedContentTypes:@[[UTType typeWithFilenameExtension:@"lua"]]];
    } else {
        [savePanel setAllowedFileTypes:@[@"lua"]];
    }
    [savePanel setExtensionHidden:NO];
    [savePanel setCanCreateDirectories:YES];

    // Set default directory to scripts folder
    NSString* homeDir = NSHomeDirectory();
    NSString* scriptsDir = [homeDir stringByAppendingPathComponent:@"SuperTerminal/scripts"];
    NSURL* defaultURL = [NSURL fileURLWithPath:scriptsDir];
    [savePanel setDirectoryURL:defaultURL];

    [savePanel beginWithCompletionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            NSURL* url = [savePanel URL];
            NSString* path = [url path];
            NSString* filename = [[url lastPathComponent] stringByDeletingPathExtension];

            NSLog(@"Saving to: %@", path);
            bool saveResult = editor_save([filename UTF8String]);

            dispatch_async(dispatch_get_main_queue(), ^{
                if (saveResult) {
                    editor_set_status([[NSString stringWithFormat:@"Saved: %@", filename] UTF8String], 180);
                } else {
                    editor_set_status("Save failed", 180);
                }
            });
        }
    }];
}

- (IBAction)loadFile:(id)sender {
    NSLog(@"Menu: Load File");

    NSOpenPanel* openPanel = [NSOpenPanel openPanel];
    [openPanel setTitle:@"Load Lua Script"];
    if (@available(macOS 12.0, *)) {
        [openPanel setAllowedContentTypes:@[[UTType typeWithFilenameExtension:@"lua"]]];
    } else {
        [openPanel setAllowedFileTypes:@[@"lua"]];
    }
    [openPanel setAllowsMultipleSelection:NO];
    [openPanel setCanChooseDirectories:NO];
    [openPanel setCanChooseFiles:YES];

    // Set default directory to scripts folder
    NSString* homeDir = NSHomeDirectory();
    NSString* scriptsDir = [homeDir stringByAppendingPathComponent:@"SuperTerminal/scripts"];
    NSURL* defaultURL = [NSURL fileURLWithPath:scriptsDir];
    [openPanel setDirectoryURL:defaultURL];

    [openPanel beginWithCompletionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            NSURL* url = [openPanel URL];
            NSString* path = [url path];
            NSString* filename = [[url lastPathComponent] stringByDeletingPathExtension];

            NSLog(@"Loading: %@", path);

            // SIMPLE APPROACH: Just load the file into editor, no execution
            editor_set_status("Loading script file...", 90);

            // Ensure editor is active
            if (!editor_is_active()) {
                editor_toggle();
            }

            // Simply load the file content - no complex shutdown systems
            bool loadResult = editor_load_file([path UTF8String]);

            if (loadResult) {
                editor_set_status([[NSString stringWithFormat:@"Loaded: %@", filename] UTF8String], 180);
                NSLog(@"Script loaded successfully: %@", filename);

                // Update status bar with loaded script name
                const char* loaded_filename = editor_get_current_filename();
                if (loaded_filename && strlen(loaded_filename) > 0) {
                    superterminal_update_script_name(loaded_filename);
                }
            } else {
                editor_set_status("Failed to load script file", 180);
                NSLog(@"Failed to load script: %@", filename);
            }
        }
    }];
}

- (IBAction)newFile:(id)sender {
    NSLog(@"Menu: New File");

    // Ensure editor is active
    if (!editor_is_active()) {
        editor_toggle();
    }

    editor_new_file();
    editor_set_status("New file created", 180);
}

- (IBAction)runScript:(id)sender {
    NSLog(@"=== RUN SCRIPT MENU CLICKED V2 ===");
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "MENU: runScript() method called\n");
    fprintf(stderr, "========================================\n");
    fflush(stderr);

    // Check if a script is currently running (GCD runtime)
    fprintf(stderr, "MENU: About to check lua_gcd_is_script_running()\n");
    fflush(stderr);

    bool is_running = lua_gcd_is_script_running();

    fprintf(stderr, "MENU: lua_gcd_is_script_running() returned: %d\n", is_running);
    fflush(stderr);

    if (is_running) {
        fprintf(stderr, "MENU: Another script is running - auto-stopping it first\n");

        // Get current script name for diagnostics
        const char* current_script = lua_gcd_get_current_script_name();
        if (current_script && strlen(current_script) > 0) {
            fprintf(stderr, "MENU: Stopping currently running script: %s\n", current_script);
        }
        fflush(stderr);

        // Auto-stop the running script
        lua_gcd_stop_script();

        // Give it a moment to stop (GCD will handle cancellation)
        fprintf(stderr, "MENU: Waiting 200ms for script to stop...\n");
        fflush(stderr);
        usleep(200000); // 200ms

        // Check if it stopped
        if (lua_gcd_is_script_running()) {
            fprintf(stderr, "MENU: WARNING - Script still running after stop request\n");
            fflush(stderr);
            editor_set_status("Previous script still running, try again", 180);
            return;
        }

        fprintf(stderr, "MENU: Previous script stopped, proceeding with new script\n");
        fflush(stderr);
    }

    fprintf(stderr, "MENU: Passed script running check\n");
    fflush(stderr);

    // Try to call external callback if available (for interactive runner)
    if (g_external_run_script_callback) {
        fprintf(stderr, "MENU: Calling external run script callback\n");
        fflush(stderr);
        g_external_run_script_callback();
        return;
    }

    fprintf(stderr, "MENU: No external callback, using built-in editor execution\n");
    fflush(stderr);

    // Get editor content
    fprintf(stderr, "MENU: Getting editor content...\n");
    fflush(stderr);

    char* content = editor_get_content();
    fprintf(stderr, "MENU: editor_get_content() returned: %s\n", content ? "valid pointer" : "NULL");
    fflush(stderr);

    // Debug editor state in detail
    fprintf(stderr, "=== EDITOR STATE DEBUG ===\n");
    fprintf(stderr, "editor_is_active(): %d\n", editor_is_active());
    fprintf(stderr, "editor_is_modified(): %d\n", editor_is_modified());
    if (content) {
        fprintf(stderr, "Content length: %zu bytes\n", strlen(content));
        if (strlen(content) > 0) {
            fprintf(stderr, "Content preview (first 100 chars): %.100s\n", content);
        }
    }
    fprintf(stderr, "=== END EDITOR STATE DEBUG ===\n");
    fflush(stderr);

    if (!content || strlen(content) == 0) {
        fprintf(stderr, "MENU: ERROR - No script content to run\n");
        fflush(stderr);
        if (content) {
            free(content);
        }
        return;
    }

    fprintf(stderr, "MENU: Script content length: %zu characters\n", strlen(content));
    fflush(stderr);

    // CRITICAL FIX: Copy content to safe string BEFORE closing editor
    std::string safe_content(content);
    fprintf(stderr, "MENU: Copied content to safe buffer (length: %zu)\n", safe_content.length());
    fflush(stderr);

    // Free the original editor content pointer
    free(content);

    // Handle REPL mode vs full-screen mode differently
    if (g_repl_mode_enabled) {
        fprintf(stderr, "MENU: REPL MODE - Wrapping content\n");
        fflush(stderr);

        // In REPL mode, wrap the content and don't close the editor
        std::string wrapped_content = "start_of_script(\"repl\")\n" + safe_content + "\nend_of_script()";

        fprintf(stderr, "MENU: === CALLING lua_gcd_exec (REPL MODE) ===\n");
        fflush(stderr);

        // Execute using GCD Lua runtime
        bool success = lua_gcd_exec(wrapped_content.c_str(), "repl");

        if (success) {
            fprintf(stderr, "MENU: REPL script queued successfully\n");
            fflush(stderr);
        } else {
            fprintf(stderr, "MENU: FAILED - REPL script failed to queue\n");
            const char* error = lua_gcd_get_last_error();
            if (error && strlen(error) > 0) {
                fprintf(stderr, "MENU: Error message: %s\n", error);
            }
            fflush(stderr);
        }

        return; // Don't execute the full-screen logic below
    }

    // Full-screen mode: Close editor so user can see script output
    fprintf(stderr, "MENU: Full-screen mode\n");
    fflush(stderr);

    if (editor_is_active()) {
        fprintf(stderr, "MENU: Closing editor to show script output\n");
        fflush(stderr);
        editor_deactivate();
        layer_set_enabled(6, false);  // Also disable editor layer
    }

    // Clear screen for script output
    cls();
    background_color(rgba(0, 0, 50, 255));

    // Wrap content with end_of_script() marker for completion detection
    std::string wrapped_content = safe_content + "\nend_of_script()";
    fprintf(stderr, "MENU: Appended end_of_script() marker\n");
    fflush(stderr);

    // Update status bar - script is running
    const char* script_name = editor_get_current_filename();
    if (script_name && strlen(script_name) > 0) {
        superterminal_update_script_name(script_name);
    }
    superterminal_update_status("● Running");

    // Use sandboxed execution system with wrapped content
    fprintf(stderr, "MENU: === CALLING lua_gcd_exec() ===\n");
    fflush(stderr);

    // Execute using GCD Lua runtime
    bool success = lua_gcd_exec(wrapped_content.c_str(), "script");

    fprintf(stderr, "MENU: === lua_gcd_exec() returned: %d ===\n", success);
    fflush(stderr);

    if (success) {
        fprintf(stderr, "MENU: Script queued successfully\n");
        fflush(stderr);
        // Script status now shown in title bar and status bar
    } else {
        fprintf(stderr, "MENU: Script execution failed to queue\n");
        superterminal_update_status("● Stopped");

        const char* error = lua_gcd_get_last_error();
        if (error && strlen(error) > 0) {
            fprintf(stderr, "MENU: Error message: %s\n", error);
            // Show error dialog to user
            const char* script_name = lua_gcd_get_current_script_name();
            show_lua_error_dialog(error, script_name ? script_name : "script");
        }
        fflush(stderr);
    }

    fprintf(stderr, "MENU: runScript() method completed\n");
    fprintf(stderr, "========================================\n\n");
    fflush(stderr);

    // Content already freed after copying to safe buffer
}

- (IBAction)checkSyntax:(id)sender {
    @autoreleasepool {
        NSLog(@"Menu: Check Syntax - MENU HANDLER CALLED");

        // Ensure we're on the main thread
        if (![NSThread isMainThread]) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self checkSyntax:sender];
            });
            return;
        }

        if (!editor_is_active()) {
            NSLog(@"Menu: Editor not active");

            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Editor Not Active"];
            [alert setInformativeText:@"Please open the editor (F1) to check syntax."];
            [alert setAlertStyle:NSAlertStyleWarning];
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return;
        }

        // Get editor content
        char* content = editor_get_content();
        if (!content || strlen(content) == 0) {
            NSLog(@"Menu: No content to check");

            if (content) free(content);

            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:@"No Content"];
            [alert setInformativeText:@"The editor is empty. Nothing to check."];
            [alert setAlertStyle:NSAlertStyleInformational];
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return;
        }

        NSLog(@"Menu: Checking syntax for %zu bytes of content", strlen(content));

        // Perform syntax check on background thread, show results on main thread
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            @autoreleasepool {
                BOOL syntaxOK = NO;
                NSString *errorMessage = nil;
                int lineNumber = -1;

                // Create a temporary Lua state to check syntax
                lua_State* L = luaL_newstate();
                if (!L) {
                    NSLog(@"Menu: Failed to create Lua state for syntax check");
                    free(content);

                    dispatch_async(dispatch_get_main_queue(), ^{
                        NSAlert *alert = [[NSAlert alloc] init];
                        [alert setMessageText:@"Error"];
                        [alert setInformativeText:@"Failed to initialize syntax checker."];
                        [alert setAlertStyle:NSAlertStyleCritical];
                        [alert addButtonWithTitle:@"OK"];
                        [alert runModal];
                    });
                    return;
                }

                // Set a safe panic handler
                lua_atpanic(L, [](lua_State* L) -> int {
                    return 0; // Don't crash, just return
                });

                // Try to load the script (this checks syntax without executing)
                int result = luaL_loadstring(L, content);

                if (result == LUA_OK) {
                    syntaxOK = YES;
                } else {
                    // Syntax error found
                    const char* error_msg = lua_tostring(L, -1);
                    if (error_msg) {
                        errorMessage = [NSString stringWithUTF8String:error_msg];
                    } else {
                        errorMessage = @"Unknown syntax error";
                    }
                }

                // Clean up
                lua_close(L);
                free(content);

                // Get script name on main thread
                dispatch_async(dispatch_get_main_queue(), ^{
                    const char* script_name_c = editor_get_current_filename();
                    NSString *scriptName;
                    if (script_name_c && strlen(script_name_c) > 0) {
                        scriptName = [NSString stringWithUTF8String:script_name_c];
                    } else {
                        scriptName = @"unsaved script";
                    }

                    if (syntaxOK) {
                        NSLog(@"Menu: Syntax check PASSED");

                        NSAlert *alert = [[NSAlert alloc] init];
                        [alert setMessageText:@"Syntax OK ✓"];
                        [alert setInformativeText:@"No syntax errors found in the script."];
                        [alert setAlertStyle:NSAlertStyleInformational];
                        [alert addButtonWithTitle:@"OK"];
                        [alert runModal];

                        editor_set_status("Syntax check passed - no errors found", 180);
                    } else {
                        NSLog(@"Menu: Syntax check FAILED: %@", errorMessage);

                        // Show error dialog
                        show_lua_error_dialog([errorMessage UTF8String], [scriptName UTF8String]);
                        editor_set_status("Syntax error found - see dialog for details", 180);
                    }
                });
            }
        });
    }
}

- (IBAction)formatLuaCode:(id)sender {
    NSLog(@"Menu: Format Lua Code - MENU HANDLER CALLED");
    printf("Menu: Format Lua Code - MENU HANDLER CALLED\n");
    fflush(stdout);

    if (!editor_is_active()) {
        NSLog(@"Menu: Editor not active");
        printf("Menu: Editor not active\n");
        fflush(stdout);
        editor_set_status("ERROR: Editor not active", 180);
        return;
    }

    NSLog(@"Menu: Calling editor_format_lua_code()");
    printf("Menu: Calling editor_format_lua_code()\n");
    fflush(stdout);

    // Call the C++ formatter function
    editor_format_lua_code();

    NSLog(@"Menu: editor_format_lua_code() returned");
    printf("Menu: editor_format_lua_code() returned\n");
    fflush(stdout);
}

- (IBAction)showEditor:(id)sender {
    NSLog(@"Menu: Show Editor");
    if (!editor_is_active()) {
        // Simulate F1 key press to activate editor (0x7A is F1)
        input_system_key_down(0x7A);
        input_system_key_up(0x7A);
        editor_set_status("Editor shown via F1", 90);
    }
}

- (IBAction)hideEditor:(id)sender {
    NSLog(@"Menu: Hide Editor");
    if (editor_is_active()) {
        // Simulate F3 key press to hide editor (0x63 is F3)
        input_system_key_down(0x63);
        input_system_key_up(0x63);
        editor_set_status("Editor hidden via F3", 90);
    }
}

- (void)toggleCRTGlow:(id)sender {
    NSMenuItem* menuItem = (NSMenuItem*)sender;
    bool currentState = [menuItem state] == NSControlStateValueOn;
    bool newState = !currentState;

    // Update menu item state
    [menuItem setState:newState ? NSControlStateValueOn : NSControlStateValueOff];

    // Apply the CRT glow setting
    coretext_set_crt_glow(newState);

    NSLog(@"CRT Glow effect %s via menu", newState ? "ENABLED" : "DISABLED");
}

- (void)toggleCRTScanlines:(id)sender {
    NSMenuItem* menuItem = (NSMenuItem*)sender;
    bool currentState = [menuItem state] == NSControlStateValueOn;
    bool newState = !currentState;

    // Update menu item state
    [menuItem setState:newState ? NSControlStateValueOn : NSControlStateValueOff];

    // Apply the CRT scanlines setting
    coretext_set_crt_scanlines(newState);

    NSLog(@"CRT Scanlines effect %s via menu", newState ? "ENABLED" : "DISABLED");
}

- (void)switchTextMode:(id)sender {
    NSMenuItem* menuItem = (NSMenuItem*)sender;
    TextMode newMode = (TextMode)[menuItem tag];

    // Write comprehensive debug to file
    FILE *debugFile = fopen("/tmp/superterminal_debug.log", "a");
    if (debugFile) {
        fprintf(debugFile, "\n=== MENU TEXT MODE CHANGE ===\n");
        fprintf(debugFile, "SWITCHING TO MODE: %d\n", (int)newMode);
        fprintf(debugFile, "MENU ITEM: %s\n", [[menuItem title] UTF8String]);
        fflush(debugFile);
        fclose(debugFile);
    }

    NSLog(@"Switching to text mode: %d", newMode);

    // Call C API to switch mode
    if (text_mode_set(newMode)) {
        // Update checkmarks in the menu
        NSMenu* parentMenu = [menuItem menu];
        for (NSMenuItem* item in [parentMenu itemArray]) {
            [item setState:NSControlStateValueOff];
        }
        [menuItem setState:NSControlStateValueOn];

        TextModeConfig config = text_mode_get_config(newMode);
        NSLog(@"Text mode switched to: %s (%d×%d, %.1fpt)",
              config.name, config.columns, config.rows, config.fontSize);

        // Write success debug to file
        debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "MODE SWITCH SUCCESS: %s (%dx%d, %.1fpt)\n",
                   config.name, config.columns, config.rows, config.fontSize);
            fprintf(debugFile, "=============================\n");
            fflush(debugFile);
            fclose(debugFile);
        }
    } else {
        NSLog(@"ERROR: Failed to switch text mode");

        // Write failure debug to file
        debugFile = fopen("/tmp/superterminal_debug.log", "a");
        if (debugFile) {
            fprintf(debugFile, "MODE SWITCH FAILED!\n");
            fprintf(debugFile, "=============================\n");
            fflush(debugFile);
            fclose(debugFile);
        }
    }
}

- (IBAction)toggleReplMode:(id)sender {
    NSLog(@"Menu: Toggle REPL Console - MENU HANDLER CALLED");
    printf("Menu: Toggle REPL Console - MENU HANDLER CALLED\n");

    if (!repl_is_initialized()) {
        NSLog(@"REPL not initialized, calling repl_initialize()");
        bool init_result = repl_initialize();
        NSLog(@"repl_initialize() returned: %s", init_result ? "true" : "false");
    } else {
        NSLog(@"REPL already initialized");
    }

    NSLog(@"Calling repl_toggle()...");
    repl_toggle();

    bool replActive = repl_is_active();
    NSLog(@"After toggle, repl_is_active() = %s", replActive ? "true" : "false");

    if (replActive) {
        NSLog(@"REPL Console activated - Interactive Lua console shown");
        editor_set_status("REPL Console active - Press Shift+Enter or Ctrl+Enter to execute", 180);
    } else {
        NSLog(@"REPL Console deactivated");
        editor_set_status("REPL Console hidden", 120);
    }

    // Update menu titles
    if (g_menuDelegate) {
        [g_menuDelegate updateMenuStates];
    }
}

- (IBAction)resetReplState:(id)sender {
    NSLog(@"Menu: Reset REPL State");

    // Reset the persistent REPL Lua state
    lua_gcd_reset_repl();

    // Show confirmation feedback
    editor_set_status("REPL state reset - variables and functions cleared", 150);
    NSLog(@"REPL state has been reset");
}




- (IBAction)showAbout:(id)sender {
    NSLog(@"Menu: Show About");

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"SuperTerminal"];
    [alert setInformativeText:@"A retro-inspired terminal framework for macOS Apple Silicon\n\nVersion 1.0.0\nCopyright © 2024 SuperTerminal Project"];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert addButtonWithTitle:@"OK"];

    [alert runModal];
}

- (IBAction)resetSystem:(id)sender {
    NSLog(@"Menu: Reset All Subsystems");
    std::cout << "\n=== RESET SUBSYSTEMS DEBUG ===" << std::endl;
    std::cout << "Menu: Reset All Subsystems command triggered" << std::endl;

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Reset All Subsystems"];
    [alert setInformativeText:@"This will stop all sounds, clear all sprites, particles, bullets, and graphics overlays while keeping the core systems running. The current script will continue."];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert addButtonWithTitle:@"Reset All"];
    [alert addButtonWithTitle:@"Cancel"];

    NSModalResponse response = [alert runModal];

    if (response == NSAlertFirstButtonReturn) {
        std::cout << "User confirmed reset operation" << std::endl;
        console("=== MANUAL SUBSYSTEM RESET REQUESTED ===");

        // Reset all subsystems using SubsystemManager
        editor_set_status("Resetting all subsystems...", 180);
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            std::cout << "Calling subsystem_manager_reset(3000) for manual reset..." << std::endl;
            console("Calling subsystem_manager_reset with 3 second timeout...");

            bool success = subsystem_manager_reset(3000); // 3 second timeout

            std::cout << "Manual reset result: " << (success ? "SUCCESS" : "FAILED") << std::endl;

            dispatch_async(dispatch_get_main_queue(), ^{
                if (success) {
                    std::cout << "Manual subsystem reset completed successfully" << std::endl;
                    console("✅ All subsystems reset successfully");
                    editor_set_status("All subsystems reset successfully", 180);
                } else {
                    std::cout << "ERROR: Manual subsystem reset failed" << std::endl;
                    console("❌ Subsystem reset failed - check console for details");
                    editor_set_status("Subsystem reset failed - check console", 180);
                }
                std::cout << "=== MANUAL RESET COMPLETE ===" << std::endl;
            });
        });
    } else {
        std::cout << "User cancelled reset operation" << std::endl;
        console("Reset operation cancelled by user");
    }
    // Cancel = do nothing
}



- (IBAction)resetGraphicsOnly:(id)sender {
    NSLog(@"Menu: Reset Graphics Only");
    editor_set_status("Clearing graphics and sprites...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // Clear overlay graphics and particles
        overlay_graphics_layer_clear();
        particle_system_clear();
        bullet_clear_all();

        // Clear sprites and tiles
        sprites_clear();
        tiles_clear();

        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Graphics reset complete", 180);
        });
    });
}

- (IBAction)spritesClear:(id)sender {
    NSLog(@"Menu: Clear Sprites");
    editor_set_status("Clearing all sprites...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        sprites_clear();
        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Sprites cleared - ready for new sprites", 180);
        });
    });
}

- (IBAction)spritesShutdown:(id)sender {
    NSLog(@"Menu: Shutdown Sprites");
    editor_set_status("Shutting down sprite system...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        sprites_shutdown();
        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Sprite system shutdown complete", 180);
        });
    });
}

- (IBAction)tilesClear:(id)sender {
    NSLog(@"Menu: Clear Tiles");
    editor_set_status("Clearing all tiles...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        tiles_clear();
        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Tiles cleared - ready for new tiles", 180);
        });
    });
}

- (IBAction)tilesShutdown:(id)sender {
    NSLog(@"Menu: Shutdown Tiles");
    editor_set_status("Shutting down tile system...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        tiles_shutdown();
        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Tile system shutdown complete", 180);
        });
    });
}

- (IBAction)clearGraphics:(id)sender {
    NSLog(@"Menu: Clear Graphics");
    editor_set_status("Clearing all graphics...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // Clear overlay graphics and particles
        overlay_graphics_layer_clear();
        particle_system_clear();
        bullet_clear_all();

        // Clear sprites and tiles
        sprites_clear();
        tiles_clear();

        // Switch banks to make the clear visible
        graphics_swap();

        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("All graphics cleared", 180);
        });
    });
}

- (IBAction)clearText:(id)sender {
    NSLog(@"Menu: Clear Text");
    editor_set_status("Clearing all text...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // Clear terminal text
        cls();
        home();

        // Clear editor if active
        if (editor_is_active()) {
            editor_clear();
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("All text cleared", 180);
        });
    });
}

- (IBAction)clearAudio:(id)sender {
    NSLog(@"Menu: Clear Audio");
    editor_set_status("Stopping all sounds and music...", 180);
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // Stop all music and sounds
        music_stop();
        music_clear_queue();

        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("All audio cleared", 180);
        });
    });
}

- (IBAction)stopMusic:(id)sender {
    NSLog(@"Menu: Stop Music");
    editor_set_status("Stopping music...", 180);

    // Stop music
    music_stop();

    // Also stop any running Lua script
    if (lua_gcd_is_script_running()) {
        NSLog(@"Menu: Also stopping running Lua script");
        lua_gcd_stop_script();
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        editor_set_status("Music stopped", 180);
    });
}

- (IBAction)stopScript:(id)sender {
    NSLog(@"Menu: Stop Script");
    fprintf(stderr, "\n========================================\n");
    fprintf(stderr, "MENU: stopScript() called\n");
    fprintf(stderr, "========================================\n");
    fflush(stderr);

    if (!lua_gcd_is_script_running()) {
        fprintf(stderr, "MENU: No script currently running\n");
        fflush(stderr);
        editor_set_status("No script running", 90);
        return;
    }

    fprintf(stderr, "MENU: Calling lua_gcd_stop_script()...\n");
    fflush(stderr);

    editor_set_status("Stopping script...", 90);

    // Request cancellation via GCD runtime
    bool stopped = lua_gcd_stop_script();

    fprintf(stderr, "MENU: lua_gcd_stop_script() returned: %d\n", stopped);
    fflush(stderr);

    if (stopped) {
        fprintf(stderr, "MENU: Script cancellation requested successfully\n");
        fflush(stderr);
        editor_set_status("Script stopped", 90);

        // Update window title on main thread
        extern void set_window_title(const char* title);
        dispatch_async(dispatch_get_main_queue(), ^{
            set_window_title("SuperTerminal - Script stopped");
        });
    } else {
        fprintf(stderr, "MENU: No script was running to stop\n");
        fflush(stderr);
        editor_set_status("No script running", 90);
    }

    fprintf(stderr, "MENU: stopScript() completed\n");
    fprintf(stderr, "========================================\n\n");
    fflush(stderr);
}

- (IBAction)resetLuaPartial:(id)sender {
    NSLog(@"Menu: Reset Lua (Partial - Advanced)");

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Partial Lua Reset (Advanced)"];
    [alert setInformativeText:@"This is an advanced option that interrupts scripts but preserves global variables, functions, and modules. Use only if you need to maintain state between script runs."];
    [alert setAlertStyle:NSAlertStyleInformational];
    [alert addButtonWithTitle:@"Partial Reset"];
    [alert addButtonWithTitle:@"Cancel"];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        editor_set_status("Performing partial Lua reset...", 90);

        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            // reset_lua_partial(); // TODO: Implement partial reset for GCD runtime

            dispatch_async(dispatch_get_main_queue(), ^{
                editor_set_status("Partial Lua reset - variables preserved", 180);
            });
        });
    }
}

- (IBAction)resetLuaComplete:(id)sender {
    NSLog(@"Menu: Reset Lua (Complete)");

    editor_set_status("Performing complete Lua reset...", 90);

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
        reset_lua_complete();

        dispatch_async(dispatch_get_main_queue(), ^{
            editor_set_status("Complete Lua reset - fresh state created", 180);
        });
    });
}

- (IBAction)emergencyReset:(id)sender {
    NSLog(@"Menu: Emergency Reset");

    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Emergency Reset"];
    [alert setInformativeText:@"This will force-reset all systems without error checking. Use only if SuperTerminal is stuck or behaving abnormally."];
    [alert setAlertStyle:NSAlertStyleCritical];
    [alert addButtonWithTitle:@"Emergency Reset"];
    [alert addButtonWithTitle:@"Cancel"];

    if ([alert runModal] == NSAlertFirstButtonReturn) {
        editor_set_status("Performing emergency reset...", 180);
        superterminal_emergency_reset();
        editor_set_status("Emergency reset complete", 180);
    }
}

- (IBAction)quitApplication:(id)sender {
    NSLog(@"Menu: Quit Application");

    // Stop any running scripts before quitting
    if (lua_gcd_is_script_running()) {
        NSLog(@"Stopping running script before quit...");
        fprintf(stderr, "MENU: Stopping GCD script before quit\n");
        fflush(stderr);

        lua_gcd_stop_script();

        // Give script a moment to stop
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [[NSApplication sharedApplication] terminate:sender];
        });
    } else {
        [[NSApplication sharedApplication] terminate:sender];
    }

    // Don't use exit(0) timer here - let NSApplication handle termination naturally
    // The emphatic shutdown timer in superterminal_application_will_terminate (8 seconds)
    // will force exit if something hangs during cleanup
}

- (IBAction)forceQuitApplication:(id)sender {
    NSLog(@"Menu: Force Quit Application - immediate termination");

    // Show alert to confirm force quit
    NSAlert* alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Force Quit SuperTerminal"];
    [alert setInformativeText:@"This will immediately terminate the application without cleanup. Any unsaved work will be lost."];
    [alert addButtonWithTitle:@"Force Quit"];
    [alert addButtonWithTitle:@"Cancel"];
    [alert setAlertStyle:NSAlertStyleWarning];

    NSModalResponse response = [alert runModal];
    if (response == NSAlertFirstButtonReturn) {
        NSLog(@"Force quit confirmed - terminating immediately");
        superterminal_force_shutdown();
    }
}

// ============================================================================
// MARK: - Assets Menu Actions
// ============================================================================

- (IBAction)loadScriptFromDatabase:(id)sender {
    NSLog(@"Menu: Load Script from Database");

    // Get parent window
    NSWindow* parentWindow = [[NSApplication sharedApplication] mainWindow];

    // Show load dialog
    [[AssetDialogManager sharedManager] showLoadScriptDialog:parentWindow
                                                  completion:^(NSString* scriptName, NSString* scriptContent) {
        if (scriptName && scriptContent) {
            NSLog(@"Loading script from database: %@", scriptName);

            // Set status message
            editor_set_status("Loading script from database...", 90);

            // Ensure editor is active
            if (!editor_is_active()) {
                editor_toggle();
            }

            // Load directly from database to editor (no temp file, no copying!)
            SuperTerminal::AssetsManager* assetsManager = SubsystemManager::getInstance().getAssetsManager();
            if (assetsManager) {
                auto result = assetsManager->loadScriptToEditor([scriptName UTF8String]);

                if (result == SuperTerminal::AssetLoadResult::SUCCESS) {
                    NSString* status = [NSString stringWithFormat:@"Loaded '%@' from database", scriptName];
                    editor_set_status([status UTF8String], 180);
                    NSLog(@"Script loaded successfully into editor");
                } else {
                    editor_set_status("Failed to load script", 180);
                    NSLog(@"Failed to load script from database");
                }
            } else {
                editor_set_status("Assets manager not available", 180);
                NSLog(@"ERROR: Assets manager not available");
            }
        } else {
            NSLog(@"Load cancelled or no script selected");
        }
    }];
}

- (IBAction)saveScriptToDatabase:(id)sender {
    NSLog(@"Menu: Save Script to Database");

    // Get current script content from editor
    char* content = editor_get_content();
    if (!content || strlen(content) == 0) {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"No Script Content"];
        [alert setInformativeText:@"Please write or load a script before saving to database."];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        return;
    }

    NSString* scriptContent = [NSString stringWithUTF8String:content];
    free(content);

    // Get parent window
    NSWindow* parentWindow = [[NSApplication sharedApplication] mainWindow];

    // Show save dialog
    [[AssetDialogManager sharedManager] showSaveScriptDialog:parentWindow
                                                scriptContent:scriptContent
                                                   completion:^(BOOL success, NSString* message) {
        if (success) {
            NSLog(@"Script saved successfully: %@", message);
            editor_set_status([message UTF8String], 180);
        } else {
            NSLog(@"Script save failed: %@", message);
        }
    }];
}

- (IBAction)browseAssets:(id)sender {
    NSLog(@"Menu: Browse Assets");

    // Show load dialog (for browsing without necessarily loading)
    NSWindow* parentWindow = [[NSApplication sharedApplication] mainWindow];
    [[AssetDialogManager sharedManager] showLoadScriptDialog:parentWindow
                                                  completion:^(NSString* scriptName, NSString* scriptContent) {
        // User cancelled or selected something
        if (scriptName) {
            NSLog(@"Selected asset: %@", scriptName);
        }
    }];
}

@end

static SuperTerminalMenuActions* g_menuActions = nil;

// C functions for menu management
extern "C" {

void superterminal_setup_menus(void* nsview);
void superterminal_create_menu_bar(void);
void superterminal_create_context_menu(void);

void superterminal_setup_menus(void* nsview) {
    @autoreleasepool {
        NSLog(@"Setting up SuperTerminal menus");

        g_targetView = (__bridge NSView*)nsview;
        g_menuDelegate = [[SuperTerminalMenuDelegate alloc] init];
        g_menuActions = [[SuperTerminalMenuActions alloc] init];

        // Create main menu bar
        superterminal_create_menu_bar();

        // Create context menu
        superterminal_create_context_menu();

        NSLog(@"SuperTerminal menus setup complete");
    }
}

void superterminal_create_menu_bar() {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];

        // Get assets manager from SubsystemManager (already initialized)
        SuperTerminal::AssetsManager* g_assetsManager = SubsystemManager::getInstance().getAssetsManager();

        if (g_assetsManager) {
            // Initialize dialog system with the subsystem manager's assets manager
            SuperTerminal::AssetDialogsInterface::Initialize(g_assetsManager);
            NSLog(@"Assets dialog system connected to SubsystemManager");
        } else {
            NSLog(@"WARNING: AssetsManager not available - Assets menu will not work properly");
        }

        // Create main menu bar
        g_mainMenuBar = [[NSMenu alloc] initWithTitle:@"Main Menu"];
        [g_mainMenuBar setDelegate:g_menuDelegate];

        // Application Menu (SuperTerminal)
        NSMenuItem* appMenuItem = [[NSMenuItem alloc] initWithTitle:@"SuperTerminal" action:nil keyEquivalent:@""];
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"SuperTerminal"];

        [appMenu addItemWithTitle:@"About SuperTerminal" action:@selector(showAbout:) keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* quitItem = [appMenu addItemWithTitle:@"Quit SuperTerminal" action:@selector(quitApplication:) keyEquivalent:@"q"];
        [quitItem setTarget:g_menuActions];

        NSMenuItem* forceQuitItem = [appMenu addItemWithTitle:@"Force Quit SuperTerminal" action:@selector(forceQuitApplication:) keyEquivalent:@"Q"];
        [forceQuitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [forceQuitItem setTarget:g_menuActions];

        [appMenuItem setSubmenu:appMenu];
        [g_mainMenuBar addItem:appMenuItem];

        // File Menu
        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [fileMenu setDelegate:g_menuDelegate];

        NSMenuItem* newItem = [fileMenu addItemWithTitle:@"New Script" action:@selector(newFile:) keyEquivalent:@"n"];
        [newItem setTarget:g_menuActions];

        NSMenuItem* loadItem = [fileMenu addItemWithTitle:@"Load Script..." action:@selector(loadFile:) keyEquivalent:@"o"];
        [loadItem setTarget:g_menuActions];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* saveItem = [fileMenu addItemWithTitle:@"Save" action:@selector(saveFile:) keyEquivalent:@"s"];
        [saveItem setTarget:g_menuActions];

        NSMenuItem* saveAsItem = [fileMenu addItemWithTitle:@"Save As..." action:@selector(saveAsFile:) keyEquivalent:@"S"];
        [saveAsItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
        [saveAsItem setTarget:g_menuActions];

        [fileMenuItem setSubmenu:fileMenu];
        [g_mainMenuBar addItem:fileMenuItem];

        // Edit Menu
        NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenu setDelegate:g_menuDelegate];





        [editMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* showEditorItem = [editMenu addItemWithTitle:@"Show Editor" action:@selector(showEditor:) keyEquivalent:@""];
        [showEditorItem setTarget:g_menuActions];

        NSMenuItem* hideEditorItem = [editMenu addItemWithTitle:@"Hide Editor" action:@selector(hideEditor:) keyEquivalent:@""];
        [hideEditorItem setTarget:g_menuActions];

        [editMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* replModeItem = [editMenu addItemWithTitle:@"Show REPL Console" action:@selector(toggleReplMode:) keyEquivalent:@""];
        [replModeItem setTarget:g_menuActions];

        NSMenuItem* resetReplItem = [editMenu addItemWithTitle:@"Reset REPL State" action:@selector(resetReplState:) keyEquivalent:@""];
        [resetReplItem setTarget:g_menuActions];

        [editMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* formatItem = [editMenu addItemWithTitle:@"Format Lua Code" action:@selector(formatLuaCode:) keyEquivalent:@""];
        [formatItem setTarget:g_menuActions];

        [editMenuItem setSubmenu:editMenu];
        [g_mainMenuBar addItem:editMenuItem];

        // Script Menu
        NSMenuItem* scriptMenuItem = [[NSMenuItem alloc] initWithTitle:@"Script" action:nil keyEquivalent:@""];
        NSMenu* scriptMenu = [[NSMenu alloc] initWithTitle:@"Script"];
        [scriptMenu setDelegate:g_menuDelegate];

        NSMenuItem* runScriptItem = [scriptMenu addItemWithTitle:@"Run Script" action:@selector(runScript:) keyEquivalent:@"r"];
        [runScriptItem setTarget:g_menuActions];

        NSMenuItem* checkSyntaxItem = [scriptMenu addItemWithTitle:@"Check Syntax" action:@selector(checkSyntax:) keyEquivalent:@""];
        [checkSyntaxItem setTarget:g_menuActions];

        [scriptMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* stopScriptItem = [scriptMenu addItemWithTitle:@"Stop Script" action:@selector(stopScript:) keyEquivalent:@"L"];
        [stopScriptItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
        [stopScriptItem setTarget:g_menuActions];

        [scriptMenuItem setSubmenu:scriptMenu];
        [g_mainMenuBar addItem:scriptMenuItem];

        // Assets Menu
        NSMenuItem* assetsMenuItem = [[NSMenuItem alloc] initWithTitle:@"Assets" action:nil keyEquivalent:@""];
        NSMenu* assetsMenu = [[NSMenu alloc] initWithTitle:@"Assets"];
        [assetsMenu setDelegate:g_menuDelegate];

        NSMenuItem* loadScriptItem = [assetsMenu addItemWithTitle:@"Load Script from Database..."
                                                            action:@selector(loadScriptFromDatabase:)
                                                     keyEquivalent:@"L"];
        [loadScriptItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [loadScriptItem setTarget:g_menuActions];

        NSMenuItem* saveScriptItem = [assetsMenu addItemWithTitle:@"Save Script to Database..."
                                                            action:@selector(saveScriptToDatabase:)
                                                     keyEquivalent:@"S"];
        [saveScriptItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [saveScriptItem setTarget:g_menuActions];

        [assetsMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* browseAssetsItem = [assetsMenu addItemWithTitle:@"Browse Assets..."
                                                              action:@selector(browseAssets:)
                                                       keyEquivalent:@"B"];
        [browseAssetsItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [browseAssetsItem setTarget:g_menuActions];

        NSMenuItem* deleteAssetItem = [assetsMenu addItemWithTitle:@"Delete Asset..."
                                                            action:@selector(deleteAsset:)
                                                     keyEquivalent:@"D"];
        [deleteAssetItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagOption];
        [deleteAssetItem setTarget:g_menuActions];

        [assetsMenuItem setSubmenu:assetsMenu];
        [g_mainMenuBar addItem:assetsMenuItem];

        // System Menu
        NSMenuItem* systemMenuItem = [[NSMenuItem alloc] initWithTitle:@"System" action:nil keyEquivalent:@""];
        NSMenu* systemMenu = [[NSMenu alloc] initWithTitle:@"System"];
        [systemMenu setDelegate:g_menuDelegate];

        NSMenuItem* resetAllItem = [systemMenu addItemWithTitle:@"Reset All Subsystems" action:@selector(resetSystem:) keyEquivalent:@"R"];
        [resetAllItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
        [resetAllItem setTarget:g_menuActions];

        [systemMenu addItem:[NSMenuItem separatorItem]];

        // Clear functions
        NSMenuItem* clearGraphicsItem = [systemMenu addItemWithTitle:@"Clear Graphics" action:@selector(clearGraphics:) keyEquivalent:@""];
        [clearGraphicsItem setTarget:g_menuActions];

        NSMenuItem* clearTextItem = [systemMenu addItemWithTitle:@"Clear Text" action:@selector(clearText:) keyEquivalent:@""];
        [clearTextItem setTarget:g_menuActions];

        NSMenuItem* clearAudioItem = [systemMenu addItemWithTitle:@"Clear Audio" action:@selector(clearAudio:) keyEquivalent:@""];
        [clearAudioItem setTarget:g_menuActions];

        NSMenuItem* stopMusicItem = [systemMenu addItemWithTitle:@"Stop Music" action:@selector(stopMusic:) keyEquivalent:@""];
        [stopMusicItem setTarget:g_menuActions];


        [systemMenu addItem:[NSMenuItem separatorItem]];

        // Individual subsystem clearing
        NSMenuItem* spritesClearItem = [systemMenu addItemWithTitle:@"Clear Sprites" action:@selector(spritesClear:) keyEquivalent:@""];
        [spritesClearItem setTarget:g_menuActions];

        NSMenuItem* tilesClearItem = [systemMenu addItemWithTitle:@"Clear Tiles" action:@selector(tilesClear:) keyEquivalent:@""];
        [tilesClearItem setTarget:g_menuActions];

        [systemMenuItem setSubmenu:systemMenu];
        [g_mainMenuBar addItem:systemMenuItem];

        // Font Menu
        NSMenuItem* fontMenuItem = [[NSMenuItem alloc] initWithTitle:@"Font" action:nil keyEquivalent:@""];
        NSMenu* fontMenu = [[NSMenu alloc] initWithTitle:@"Font"];
        [fontMenu setDelegate:g_menuDelegate];

        NSMenuItem* glowItem = [fontMenu addItemWithTitle:@"CRT Glow Effect" action:@selector(toggleCRTGlow:) keyEquivalent:@""];
        [glowItem setTarget:g_menuActions];
        [glowItem setState:NSControlStateValueOff]; // Default to disabled

        NSMenuItem* scanlinesItem = [fontMenu addItemWithTitle:@"CRT Scanlines" action:@selector(toggleCRTScanlines:) keyEquivalent:@""];
        [scanlinesItem setTarget:g_menuActions];
        [scanlinesItem setState:NSControlStateValueOff]; // Default to disabled

        [fontMenuItem setSubmenu:fontMenu];
        [g_mainMenuBar addItem:fontMenuItem];

        // View Menu
        NSMenuItem* viewMenuItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        [viewMenu setDelegate:g_menuDelegate];

        // Text Mode submenu
        NSMenuItem* textModeMenuItem = [[NSMenuItem alloc] initWithTitle:@"Text Mode" action:nil keyEquivalent:@""];
        NSMenu* textModeMenu = [[NSMenu alloc] initWithTitle:@"Text Mode"];
        [textModeMenu setDelegate:g_menuDelegate];

        NSMenuItem* mode20x25 = [textModeMenu addItemWithTitle:@"Giant (20×25)" action:@selector(switchTextMode:) keyEquivalent:@""];
        [mode20x25 setTag:0];
        [mode20x25 setTarget:g_menuActions];

        NSMenuItem* mode40x25 = [textModeMenu addItemWithTitle:@"Large (40×25) - C64 Classic" action:@selector(switchTextMode:) keyEquivalent:@"1"];
        [mode40x25 setTag:1];
        [mode40x25 setTarget:g_menuActions];

        NSMenuItem* mode40x50 = [textModeMenu addItemWithTitle:@"Medium (40×50)" action:@selector(switchTextMode:) keyEquivalent:@"2"];
        [mode40x50 setTag:2];
        [mode40x50 setTarget:g_menuActions];

        NSMenuItem* mode64x44 = [textModeMenu addItemWithTitle:@"Standard (64×44)" action:@selector(switchTextMode:) keyEquivalent:@"3"];
        [mode64x44 setTag:3];
        [mode64x44 setTarget:g_menuActions];
        [mode64x44 setState:NSControlStateValueOn]; // Default mode

        NSMenuItem* mode80x25 = [textModeMenu addItemWithTitle:@"Compact (80×25)" action:@selector(switchTextMode:) keyEquivalent:@"4"];
        [mode80x25 setTag:4];
        [mode80x25 setTarget:g_menuActions];

        NSMenuItem* mode80x50 = [textModeMenu addItemWithTitle:@"Dense (80×50)" action:@selector(switchTextMode:) keyEquivalent:@"5"];
        [mode80x50 setTag:5];
        [mode80x50 setTarget:g_menuActions];

        NSMenuItem* mode120x60 = [textModeMenu addItemWithTitle:@"UltraWide (120×60)" action:@selector(switchTextMode:) keyEquivalent:@"6"];
        [mode120x60 setTag:6];
        [mode120x60 setTarget:g_menuActions];

        [textModeMenuItem setSubmenu:textModeMenu];
        [viewMenu addItem:textModeMenuItem];

        [viewMenuItem setSubmenu:viewMenu];
        [g_mainMenuBar addItem:viewMenuItem];

        // Set all targets
        for (NSMenuItem* item in [appMenu itemArray]) {
            if ([item target] == nil) {
                [item setTarget:g_menuActions];
            }
        }

        // Set the menu bar
        [app setMainMenu:g_mainMenuBar];

        NSLog(@"Menu bar created and set");
    }
}

void superterminal_create_context_menu() {
    @autoreleasepool {
        // Create context menu
        g_contextMenu = [[NSMenu alloc] initWithTitle:@"Context Menu"];
        [g_contextMenu setDelegate:g_menuDelegate];



        NSMenuItem* newItem = [g_contextMenu addItemWithTitle:@"New Script" action:@selector(newFile:) keyEquivalent:@""];
        [newItem setTarget:g_menuActions];

        NSMenuItem* loadItem = [g_contextMenu addItemWithTitle:@"Load Script..." action:@selector(loadFile:) keyEquivalent:@""];
        [loadItem setTarget:g_menuActions];

        NSMenuItem* saveItem = [g_contextMenu addItemWithTitle:@"Save" action:@selector(saveFile:) keyEquivalent:@""];
        [saveItem setTarget:g_menuActions];


        [g_contextMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* showEditorItem = [g_contextMenu addItemWithTitle:@"Show Editor" action:@selector(showEditor:) keyEquivalent:@""];
        [showEditorItem setTarget:g_menuActions];

        NSMenuItem* hideEditorItem = [g_contextMenu addItemWithTitle:@"Hide Editor" action:@selector(hideEditor:) keyEquivalent:@""];
        [hideEditorItem setTarget:g_menuActions];

        NSMenuItem* replConsoleItem = [g_contextMenu addItemWithTitle:@"Show REPL Console" action:@selector(toggleReplMode:) keyEquivalent:@""];
        [replConsoleItem setTarget:g_menuActions];

        NSMenuItem* resetReplItem = [g_contextMenu addItemWithTitle:@"Reset REPL State" action:@selector(resetReplState:) keyEquivalent:@""];
        [resetReplItem setTarget:g_menuActions];

        [g_contextMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* clearGraphicsItem = [g_contextMenu addItemWithTitle:@"Clear Graphics" action:@selector(clearGraphics:) keyEquivalent:@""];
        [clearGraphicsItem setTarget:g_menuActions];

        NSMenuItem* clearTextItem = [g_contextMenu addItemWithTitle:@"Clear Text" action:@selector(clearText:) keyEquivalent:@""];
        [clearTextItem setTarget:g_menuActions];

        NSMenuItem* stopMusicItem = [g_contextMenu addItemWithTitle:@"Stop Music" action:@selector(stopMusic:) keyEquivalent:@""];
        [stopMusicItem setTarget:g_menuActions];

        NSMenuItem* resetAllItem = [g_contextMenu addItemWithTitle:@"Reset All Subsystems" action:@selector(resetSystem:) keyEquivalent:@""];
        [resetAllItem setTarget:g_menuActions];

        NSLog(@"Context menu created");
    }
}

void superterminal_show_context_menu(float x, float y) {
    @autoreleasepool {
        if (!g_contextMenu || !g_targetView) {
            NSLog(@"Context menu or target view not available");
            return;
        }

        // Update menu states before showing
        [g_menuDelegate updateMenuStates];

        // Convert coordinates
        NSPoint point = NSMakePoint(x, [g_targetView bounds].size.height - y);

        NSLog(@"Showing context menu at (%.1f, %.1f)", point.x, point.y);

        // Show the context menu
        [g_contextMenu popUpMenuPositioningItem:nil atLocation:point inView:g_targetView];
    }
}

void superterminal_update_menu_states() {
    @autoreleasepool {
        if (g_menuDelegate) {
            [g_menuDelegate updateMenuStates];
        }
    }
}

// C function to trigger script execution (for F8 key)
extern "C" void trigger_run_script(void) {
    if (g_menuActions) {
        [g_menuActions runScript:nil];
    }
}

extern "C" void set_external_run_script_callback(run_script_callback_t callback) {
    g_external_run_script_callback = callback;
}

void superterminal_cleanup_menus() {
    @autoreleasepool {
        NSLog(@"Cleaning up SuperTerminal menus");

        g_mainMenuBar = nil;
        g_contextMenu = nil;
        g_targetView = nil;
        g_menuDelegate = nil;
        g_menuActions = nil;
    }
}

// C function to query REPL mode state (legacy compatibility)
bool editor_is_repl_mode(void) {
    return repl_is_active();
}

} // extern "C"
