//
//  AssetDialogs.h
//  SuperTerminal Framework - Asset Database Dialog System
//
//  Native macOS dialogs for browsing, loading, and saving assets
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#ifndef ASSET_DIALOGS_H
#define ASSET_DIALOGS_H

#import <Cocoa/Cocoa.h>
#include <string>
#include <vector>
#include <functional>

namespace SuperTerminal {
    class AssetsManager;
    struct AssetMetadata;
}

// ============================================================================
// Load Script Dialog - Browse and load scripts from database
// ============================================================================

@interface LoadScriptDialog : NSObject <NSTableViewDataSource, NSTableViewDelegate, NSSearchFieldDelegate>

@property (strong, nonatomic) NSPanel* panel;
@property (strong, nonatomic) NSTableView* tableView;
@property (strong, nonatomic) NSSearchField* searchField;
@property (strong, nonatomic) NSButton* loadButton;
@property (strong, nonatomic) NSButton* cancelButton;
@property (strong, nonatomic) NSTextField* infoLabel;

@property (assign, nonatomic) SuperTerminal::AssetsManager* assetsManager;
@property (copy, nonatomic) void (^completionHandler)(NSString* _Nullable scriptName, NSString* _Nullable scriptContent);

- (instancetype)initWithAssetsManager:(SuperTerminal::AssetsManager*)manager;
- (void)showDialog:(NSWindow*)parentWindow completion:(void (^)(NSString* _Nullable scriptName, NSString* _Nullable scriptContent))completion;
- (void)refreshAssetList;

@end

// ============================================================================
// Save Script Dialog - Save script with metadata
// ============================================================================

@interface SaveScriptDialog : NSObject <NSTextFieldDelegate>

@property (strong, nonatomic) NSPanel* panel;
@property (strong, nonatomic) NSTextField* nameField;
@property (strong, nonatomic) NSTextField* versionField;
@property (strong, nonatomic) NSTextField* authorField;
@property (strong, nonatomic) NSTextField* descriptionField;
@property (strong, nonatomic) NSTextField* tagsField;
@property (strong, nonatomic) NSButton* saveButton;
@property (strong, nonatomic) NSButton* cancelButton;
@property (strong, nonatomic) NSButton* overwriteCheckbox;

@property (assign, nonatomic) SuperTerminal::AssetsManager* assetsManager;
@property (copy, nonatomic) NSString* scriptContent;
@property (copy, nonatomic) void (^completionHandler)(BOOL success, NSString* _Nullable message);

- (instancetype)initWithAssetsManager:(SuperTerminal::AssetsManager*)manager;
- (void)showDialog:(NSWindow*)parentWindow 
        scriptContent:(NSString*)content 
           completion:(void (^)(BOOL success, NSString* _Nullable message))completion;
- (void)prefillWithName:(NSString*)name;

@end

// ============================================================================
// Asset Dialog Manager - Singleton for easy access
// ============================================================================

@interface AssetDialogManager : NSObject

@property (strong, nonatomic) LoadScriptDialog* loadDialog;
@property (strong, nonatomic) SaveScriptDialog* saveDialog;
@property (assign, nonatomic) SuperTerminal::AssetsManager* assetsManager;

+ (instancetype)sharedManager;
- (void)setAssetsManager:(SuperTerminal::AssetsManager*)manager;

// Show dialogs
- (void)showLoadScriptDialog:(NSWindow*)parentWindow 
                  completion:(void (^)(NSString* _Nullable scriptName, NSString* _Nullable scriptContent))completion;

- (void)showSaveScriptDialog:(NSWindow*)parentWindow 
                scriptContent:(NSString*)content 
                   completion:(void (^)(BOOL success, NSString* _Nullable message))completion;

@end

// ============================================================================
// C++ Interface for easy integration
// ============================================================================

namespace SuperTerminal {

class AssetDialogsInterface {
public:
    static void Initialize(AssetsManager* manager);
    static void ShowLoadScriptDialog(void* parentWindow, 
                                     std::function<void(const std::string& name, const std::string& content)> callback);
    static void ShowSaveScriptDialog(void* parentWindow, 
                                     const std::string& scriptContent,
                                     std::function<void(bool success, const std::string& message)> callback);
};

} // namespace SuperTerminal

#endif // ASSET_DIALOGS_H