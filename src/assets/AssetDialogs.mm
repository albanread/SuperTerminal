//
//  AssetDialogs.mm
//  SuperTerminal Framework - Asset Database Dialog System
//
//  Native macOS dialogs for browsing, loading, and saving assets
//  Copyright © 2024 SuperTerminal. All rights reserved.
//

#import "AssetDialogs.h"
#include "AssetsManager.h"
#include "AssetMetadata.h"
#include "../include/SuperTerminal.h"
#include <vector>
#include <string>

using namespace SuperTerminal;

// ============================================================================
// Asset Info Wrapper for Table View
// ============================================================================

@interface AssetInfo : NSObject
@property (copy, nonatomic) NSString* name;
@property (copy, nonatomic) NSString* version;
@property (copy, nonatomic) NSString* author;
@property (copy, nonatomic) NSString* description;
@property (copy, nonatomic) NSString* tags;
@property (copy, nonatomic) NSString* dateCreated;
@property (copy, nonatomic) NSString* size;
@property (assign, nonatomic) BOOL compressed;
@end

@implementation AssetInfo
@synthesize name;
@synthesize version;
@synthesize author;
@synthesize description;
@synthesize tags;
@synthesize dateCreated;
@synthesize size;
@synthesize compressed;
@end

// ============================================================================
// Load Script Dialog Implementation
// ============================================================================

@implementation LoadScriptDialog {
    NSMutableArray<AssetInfo*>* _allAssets;
    NSMutableArray<AssetInfo*>* _filteredAssets;
}

- (instancetype)initWithAssetsManager:(AssetsManager*)manager {
    self = [super init];
    if (self) {
        _assetsManager = manager;
        _allAssets = [[NSMutableArray array] retain];
        _filteredAssets = [[NSMutableArray array] retain];
        [self createUI];
    }
    return self;
}

- (void)createUI {
    // Create panel
    NSRect frame = NSMakeRect(0, 0, 700, 500);
    _panel = [[NSPanel alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled |
                                                 NSWindowStyleMaskClosable |
                                                 NSWindowStyleMaskResizable)
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [_panel setTitle:@"Load Script from Asset Database"];
    [_panel setLevel:NSFloatingWindowLevel];
    [_panel setReleasedWhenClosed:NO];

    NSView* contentView = [_panel contentView];

    // Search field
    _searchField = [[NSSearchField alloc] initWithFrame:NSMakeRect(20, 450, 660, 30)];
    [_searchField setPlaceholderString:@"Search scripts..."];
    [_searchField setDelegate:self];
    [_searchField setAutoresizingMask:(NSViewWidthSizable | NSViewMinYMargin)];
    [contentView addSubview:_searchField];

    // Table view with scroll view
    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(20, 80, 660, 360)];
    [scrollView setHasVerticalScroller:YES];
    [scrollView setHasHorizontalScroller:NO];
    [scrollView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [scrollView setBorderType:NSBezelBorder];

    _tableView = [[NSTableView alloc] initWithFrame:[scrollView bounds]];
    [_tableView setDataSource:self];
    [_tableView setDelegate:self];
    [_tableView setAllowsMultipleSelection:NO];
    [_tableView setUsesAlternatingRowBackgroundColors:YES];
    [_tableView setDoubleAction:@selector(loadButtonClicked:)];

    // Add columns
    NSTableColumn* nameColumn = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    [[nameColumn headerCell] setStringValue:@"Script Name"];
    [nameColumn setWidth:200];
    [nameColumn setResizingMask:NSTableColumnUserResizingMask];
    [_tableView addTableColumn:nameColumn];

    NSTableColumn* versionColumn = [[NSTableColumn alloc] initWithIdentifier:@"version"];
    [[versionColumn headerCell] setStringValue:@"Version"];
    [versionColumn setWidth:80];
    [_tableView addTableColumn:versionColumn];

    NSTableColumn* authorColumn = [[NSTableColumn alloc] initWithIdentifier:@"author"];
    [[authorColumn headerCell] setStringValue:@"Author"];
    [authorColumn setWidth:120];
    [_tableView addTableColumn:authorColumn];

    NSTableColumn* dateColumn = [[NSTableColumn alloc] initWithIdentifier:@"date"];
    [[dateColumn headerCell] setStringValue:@"Modified"];
    [dateColumn setWidth:140];
    [_tableView addTableColumn:dateColumn];

    NSTableColumn* sizeColumn = [[NSTableColumn alloc] initWithIdentifier:@"size"];
    [[sizeColumn headerCell] setStringValue:@"Size"];
    [sizeColumn setWidth:80];
    [_tableView addTableColumn:sizeColumn];

    [scrollView setDocumentView:_tableView];
    [contentView addSubview:scrollView];

    // Info label
    _infoLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 50, 660, 20)];
    [_infoLabel setEditable:NO];
    [_infoLabel setBordered:NO];
    [_infoLabel setDrawsBackground:NO];
    [_infoLabel setStringValue:@"0 scripts"];
    [_infoLabel setAutoresizingMask:(NSViewWidthSizable | NSViewMaxYMargin)];
    [contentView addSubview:_infoLabel];

    // Buttons
    _cancelButton = [[NSButton alloc] initWithFrame:NSMakeRect(490, 12, 90, 32)];
    [_cancelButton setTitle:@"Cancel"];
    [_cancelButton setBezelStyle:NSBezelStyleRounded];
    [_cancelButton setKeyEquivalent:@"\e"];
    [_cancelButton setTarget:self];
    [_cancelButton setAction:@selector(cancelButtonClicked:)];
    [_cancelButton setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
    [contentView addSubview:_cancelButton];

    _loadButton = [[NSButton alloc] initWithFrame:NSMakeRect(590, 12, 90, 32)];
    [_loadButton setTitle:@"Load"];
    [_loadButton setBezelStyle:NSBezelStyleRounded];
    [_loadButton setKeyEquivalent:@"\r"];
    [_loadButton setTarget:self];
    [_loadButton setAction:@selector(loadButtonClicked:)];
    [_loadButton setEnabled:NO];
    [_loadButton setAutoresizingMask:(NSViewMinXMargin | NSViewMaxYMargin)];
    [contentView addSubview:_loadButton];
}

- (void)showDialog:(NSWindow*)parentWindow completion:(void (^)(NSString* _Nullable, NSString* _Nullable))completion {
    _completionHandler = completion;
    [self refreshAssetList];

    if (parentWindow) {
        [parentWindow beginSheet:_panel completionHandler:^(NSModalResponse returnCode) {
            // Sheet completed
        }];
    } else {
        [_panel center];
        [_panel makeKeyAndOrderFront:nil];
    }
}

- (void)refreshAssetList {
    [_allAssets removeAllObjects];

    if (!_assetsManager) {
        [self filterAssets];
        return;
    }

    // Get all scripts from database
    auto scriptNames = _assetsManager->listAssets(AssetKind::SCRIPT);

    for (const auto& name : scriptNames) {
        AssetMetadata metadata;
        if (_assetsManager->getAssetMetadata(name, metadata)) {
            AssetInfo* info = [[AssetInfo alloc] init];
            info.name = [NSString stringWithUTF8String:metadata.name.c_str()];
            info.version = [NSString stringWithUTF8String:metadata.version.c_str()];
            info.author = [NSString stringWithUTF8String:metadata.author.c_str()];
            info.description = [NSString stringWithUTF8String:metadata.description.c_str()];
            info.tags = [NSString stringWithUTF8String:metadata.getTagsString().c_str()];
            info.size = [NSString stringWithUTF8String:metadata.getDataSizeString().c_str()];
            info.compressed = metadata.compressed;

            // Format date
            if (metadata.updated_at > 0) {
                NSDate* date = [NSDate dateWithTimeIntervalSince1970:metadata.updated_at];
                NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
                [formatter setDateStyle:NSDateFormatterShortStyle];
                [formatter setTimeStyle:NSDateFormatterShortStyle];
                info.dateCreated = [formatter stringFromDate:date];
            } else {
                info.dateCreated = @"Unknown";
            }

            [_allAssets addObject:info];
        }
    }

    [self filterAssets];
}

- (void)filterAssets {
    [_filteredAssets removeAllObjects];

    NSString* searchText = [_searchField stringValue];
    if ([searchText length] == 0) {
        [_filteredAssets addObjectsFromArray:_allAssets];
    } else {
        NSString* lowercaseSearch = [searchText lowercaseString];
        for (AssetInfo* info in _allAssets) {
            if ([[info.name lowercaseString] containsString:lowercaseSearch] ||
                [[info.author lowercaseString] containsString:lowercaseSearch] ||
                [[info.tags lowercaseString] containsString:lowercaseSearch]) {
                [_filteredAssets addObject:info];
            }
        }
    }

    [_tableView reloadData];
    [_infoLabel setStringValue:[NSString stringWithFormat:@"%ld scripts", (long)[_filteredAssets count]]];
    [_loadButton setEnabled:NO];
}

- (void)loadButtonClicked:(id)sender {
    NSInteger selectedRow = [_tableView selectedRow];
    if (selectedRow < 0 || selectedRow >= [_filteredAssets count]) {
        return;
    }

    AssetInfo* info = _filteredAssets[selectedRow];
    NSString* scriptName = info.name;

    // Load script content from database
    if (_assetsManager) {
        std::string content;
        auto result = _assetsManager->loadScript([scriptName UTF8String], content);
        if (result == AssetLoadResult::SUCCESS) {
            NSString* scriptContent = [NSString stringWithUTF8String:content.c_str()];
            [_panel orderOut:nil];
            if ([_panel isSheet]) {
                [[_panel sheetParent] endSheet:_panel];
            }
            if (_completionHandler) {
                _completionHandler(scriptName, scriptContent);
            }
            return;
        } else {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Failed to Load Script"];
            [alert setInformativeText:[NSString stringWithFormat:@"Could not load script: %s",
                                     AssetsManager::loadResultToString(result)]];
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return;
        }
    }

    [_panel orderOut:nil];
    if ([_panel isSheet]) {
        [[_panel sheetParent] endSheet:_panel];
    }
    if (_completionHandler) {
        _completionHandler(nil, nil);
    }
}

- (void)cancelButtonClicked:(id)sender {
    [_panel orderOut:nil];
    if ([_panel isSheet]) {
        [[_panel sheetParent] endSheet:_panel];
    }
    if (_completionHandler) {
        _completionHandler(nil, nil);
    }
}

// NSTableViewDataSource
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    return [_filteredAssets count];
}

// NSTableViewDelegate
- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row {
    if (row < 0 || row >= [_filteredAssets count]) {
        return nil;
    }

    AssetInfo* info = _filteredAssets[row];
    NSString* identifier = [tableColumn identifier];

    NSTextField* textField = [tableView makeViewWithIdentifier:identifier owner:self];
    if (!textField) {
        textField = [[NSTextField alloc] initWithFrame:NSZeroRect];
        [textField setIdentifier:identifier];
        [textField setEditable:NO];
        [textField setBordered:NO];
        [textField setDrawsBackground:NO];
    }

    if ([identifier isEqualToString:@"name"]) {
        [textField setStringValue:info.name ? info.name : @""];
    } else if ([identifier isEqualToString:@"version"]) {
        [textField setStringValue:info.version ? info.version : @""];
    } else if ([identifier isEqualToString:@"author"]) {
        [textField setStringValue:info.author ? info.author : @""];
    } else if ([identifier isEqualToString:@"date"]) {
        [textField setStringValue:info.dateCreated ? info.dateCreated : @""];
    } else if ([identifier isEqualToString:@"size"]) {
        [textField setStringValue:info.size ? info.size : @""];
    }

    return textField;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    [_loadButton setEnabled:([_tableView selectedRow] >= 0)];
}

// NSSearchFieldDelegate
- (void)controlTextDidChange:(NSNotification*)notification {
    if ([notification object] == _searchField) {
        [self filterAssets];
    }
}

@end

// ============================================================================
// Save Script Dialog Implementation
// ============================================================================

@implementation SaveScriptDialog

- (instancetype)initWithAssetsManager:(AssetsManager*)manager {
    self = [super init];
    if (self) {
        _assetsManager = manager;
        [self createUI];
    }
    return self;
}

- (void)createUI {
    // Create panel
    NSRect frame = NSMakeRect(0, 0, 500, 380);
    _panel = [[NSPanel alloc] initWithContentRect:frame
                                        styleMask:(NSWindowStyleMaskTitled |
                                                 NSWindowStyleMaskClosable)
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
    [_panel setTitle:@"Save Script to Asset Database"];
    [_panel setLevel:NSFloatingWindowLevel];
    [_panel setReleasedWhenClosed:NO];

    NSView* contentView = [_panel contentView];

    // Labels and fields
    int yPos = 320;

    // Script Name
    NSTextField* nameLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, yPos, 120, 20)];
    [nameLabel setStringValue:@"Script Name:"];
    [nameLabel setEditable:NO];
    [nameLabel setBordered:NO];
    [nameLabel setDrawsBackground:NO];
    [nameLabel setAlignment:NSTextAlignmentRight];
    [contentView addSubview:nameLabel];

    _nameField = [[NSTextField alloc] initWithFrame:NSMakeRect(150, yPos - 3, 330, 24)];
    [_nameField setPlaceholderString:@"my_script"];
    [contentView addSubview:_nameField];
    yPos -= 40;

    // Version
    NSTextField* versionLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, yPos, 120, 20)];
    [versionLabel setStringValue:@"Version:"];
    [versionLabel setEditable:NO];
    [versionLabel setBordered:NO];
    [versionLabel setDrawsBackground:NO];
    [versionLabel setAlignment:NSTextAlignmentRight];
    [contentView addSubview:versionLabel];

    _versionField = [[NSTextField alloc] initWithFrame:NSMakeRect(150, yPos - 3, 330, 24)];
    [_versionField setPlaceholderString:@"1.0"];
    [contentView addSubview:_versionField];
    yPos -= 40;

    // Author
    NSTextField* authorLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, yPos, 120, 20)];
    [authorLabel setStringValue:@"Author:"];
    [authorLabel setEditable:NO];
    [authorLabel setBordered:NO];
    [authorLabel setDrawsBackground:NO];
    [authorLabel setAlignment:NSTextAlignmentRight];
    [contentView addSubview:authorLabel];

    _authorField = [[NSTextField alloc] initWithFrame:NSMakeRect(150, yPos - 3, 330, 24)];
    [_authorField setPlaceholderString:@"Your Name"];
    [contentView addSubview:_authorField];
    yPos -= 40;

    // Tags
    NSTextField* tagsLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, yPos, 120, 20)];
    [tagsLabel setStringValue:@"Tags:"];
    [tagsLabel setEditable:NO];
    [tagsLabel setBordered:NO];
    [tagsLabel setDrawsBackground:NO];
    [tagsLabel setAlignment:NSTextAlignmentRight];
    [contentView addSubview:tagsLabel];

    _tagsField = [[NSTextField alloc] initWithFrame:NSMakeRect(150, yPos - 3, 330, 24)];
    [_tagsField setPlaceholderString:@"gameplay, ai, level1"];
    [contentView addSubview:_tagsField];
    yPos -= 40;

    // Description
    NSTextField* descLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(20, yPos + 50, 120, 20)];
    [descLabel setStringValue:@"Description:"];
    [descLabel setEditable:NO];
    [descLabel setBordered:NO];
    [descLabel setDrawsBackground:NO];
    [descLabel setAlignment:NSTextAlignmentRight];
    [contentView addSubview:descLabel];

    NSScrollView* descScrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(150, yPos - 20, 330, 90)];
    [descScrollView setHasVerticalScroller:YES];
    [descScrollView setBorderType:NSBezelBorder];

    NSTextView* descTextView = [[NSTextView alloc] initWithFrame:[descScrollView bounds]];
    [descTextView setFont:[NSFont systemFontOfSize:12]];
    [descScrollView setDocumentView:descTextView];
    [contentView addSubview:descScrollView];
    _descriptionField = (NSTextField*)descTextView; // Store reference
    yPos -= 120;

    // Overwrite checkbox
    _overwriteCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(150, yPos - 5, 330, 24)];
    [_overwriteCheckbox setButtonType:NSButtonTypeSwitch];
    [_overwriteCheckbox setTitle:@"Overwrite if exists"];
    [_overwriteCheckbox setState:NSControlStateValueOff];
    [contentView addSubview:_overwriteCheckbox];

    // Buttons
    _cancelButton = [[NSButton alloc] initWithFrame:NSMakeRect(300, 12, 90, 32)];
    [_cancelButton setTitle:@"Cancel"];
    [_cancelButton setBezelStyle:NSBezelStyleRounded];
    [_cancelButton setKeyEquivalent:@"\e"];
    [_cancelButton setTarget:self];
    [_cancelButton setAction:@selector(cancelButtonClicked:)];
    [contentView addSubview:_cancelButton];

    _saveButton = [[NSButton alloc] initWithFrame:NSMakeRect(400, 12, 90, 32)];
    [_saveButton setTitle:@"Save"];
    [_saveButton setBezelStyle:NSBezelStyleRounded];
    [_saveButton setKeyEquivalent:@"\r"];
    [_saveButton setTarget:self];
    [_saveButton setAction:@selector(saveButtonClicked:)];
    [contentView addSubview:_saveButton];
}

- (void)showDialog:(NSWindow*)parentWindow scriptContent:(NSString*)content completion:(void (^)(BOOL, NSString* _Nullable))completion {
    // Strongly retain the script content to prevent deallocation
    _scriptContent = [content copy];
    _completionHandler = [completion copy];

    // Clear fields
    [_nameField setStringValue:@""];
    [_versionField setStringValue:@"1.0"];
    [_authorField setStringValue:@""];
    [_tagsField setStringValue:@""];
    [(NSTextView*)_descriptionField setString:@""];
    [_overwriteCheckbox setState:NSControlStateValueOff];

    if (parentWindow) {
        [parentWindow beginSheet:_panel completionHandler:^(NSModalResponse returnCode) {
            // Sheet completed
        }];
    } else {
        [_panel center];
        [_panel makeKeyAndOrderFront:nil];
    }

    [[_panel window] makeFirstResponder:_nameField];
}

- (void)prefillWithName:(NSString*)name {
    if (name) {
        [_nameField setStringValue:name];
    }
}

- (void)saveButtonClicked:(id)sender {
    NSString* name = [_nameField stringValue];

    if (!name || [name length] == 0) {
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Script Name Required"];
        [alert setInformativeText:@"Please enter a name for the script."];
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
        return;
    }

    // Get FRESH content from editor right now (not stale parameter)
    char* editorContent = editor_get_content();
    if (!editorContent || strlen(editorContent) == 0) {
        if (editorContent) free(editorContent);
        [self closeWithSuccess:NO message:@"No script content in editor"];
        return;
    }

    NSString* scriptContent = [NSString stringWithUTF8String:editorContent];
    free(editorContent);

    if (!scriptContent || [scriptContent length] == 0) {
        [self closeWithSuccess:NO message:@"Script content is empty"];
        return;
    }

    if (!_assetsManager) {
        [self closeWithSuccess:NO message:@"Asset manager not initialized"];
        return;
    }

    // Check if exists
    if (_assetsManager->hasAsset([name UTF8String])) {
        if ([_overwriteCheckbox state] != NSControlStateValueOn) {
            NSAlert* alert = [[NSAlert alloc] init];
            [alert setMessageText:@"Script Already Exists"];
            [alert setInformativeText:[NSString stringWithFormat:@"A script named '%@' already exists. Check 'Overwrite if exists' to replace it.", name]];
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
            return;
        }

        // Delete existing
        _assetsManager->removeAsset([name UTF8String]);
    }

    // Parse tags
    std::vector<std::string> tags;
    NSString* tagsStr = [_tagsField stringValue];
    if ([tagsStr length] > 0) {
        NSArray* tagArray = [tagsStr componentsSeparatedByString:@","];
        for (NSString* tag in tagArray) {
            NSString* trimmed = [tag stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
            if ([trimmed length] > 0) {
                tags.push_back([trimmed UTF8String]);
            }
        }
    }

    // Save script - safely handle nil/empty strings
    NSString* versionStr = [_versionField stringValue];
    NSString* authorStr = [_authorField stringValue];

    const char* nameCStr = [name UTF8String];
    const char* scriptCStr = [scriptContent UTF8String];
    const char* versionCStr = (versionStr && [versionStr length] > 0) ? [versionStr UTF8String] : "";
    const char* authorCStr = (authorStr && [authorStr length] > 0) ? [authorStr UTF8String] : "";

    bool success = _assetsManager->saveScript(
        nameCStr,
        scriptCStr,
        versionCStr,
        authorCStr,
        tags
    );

    if (success) {
        [self closeWithSuccess:YES message:[NSString stringWithFormat:@"Saved '%@' successfully", name]];
    } else {
        NSString* errorMsg = [NSString stringWithUTF8String:_assetsManager->getLastError().c_str()];
        [self closeWithSuccess:NO message:errorMsg];
    }
}

- (void)cancelButtonClicked:(id)sender {
    [self closeWithSuccess:NO message:@"Cancelled"];
}

- (void)closeWithSuccess:(BOOL)success message:(NSString*)message {
    [_panel orderOut:nil];
    if ([_panel isSheet]) {
        [[_panel sheetParent] endSheet:_panel];
    }
    if (_completionHandler) {
        _completionHandler(success, message);
    }
}

@end

// ============================================================================
// Asset Dialog Manager Implementation
// ============================================================================

@implementation AssetDialogManager

+ (instancetype)sharedManager {
    static AssetDialogManager* instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[AssetDialogManager alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _loadDialog = nil;
        _saveDialog = nil;
        _assetsManager = nullptr;
    }
    return self;
}

- (void)setAssetsManager:(AssetsManager*)manager {
    _assetsManager = manager;

    // Create dialogs lazily
    if (!_loadDialog && manager) {
        _loadDialog = [[LoadScriptDialog alloc] initWithAssetsManager:manager];
    }
    if (!_saveDialog && manager) {
        _saveDialog = [[SaveScriptDialog alloc] initWithAssetsManager:manager];
    }
}

- (void)showLoadScriptDialog:(NSWindow*)parentWindow completion:(void (^)(NSString* _Nullable, NSString* _Nullable))completion {
    if (!_loadDialog) {
        if (completion) {
            completion(nil, nil);
        }
        return;
    }

    [_loadDialog showDialog:parentWindow completion:completion];
}

- (void)showSaveScriptDialog:(NSWindow*)parentWindow scriptContent:(NSString*)content completion:(void (^)(BOOL, NSString* _Nullable))completion {
    if (!_saveDialog) {
        if (completion) {
            completion(NO, @"Dialog not initialized");
        }
        return;
    }

    [_saveDialog showDialog:parentWindow scriptContent:content completion:completion];
}

@end

// ============================================================================
// C++ Interface Implementation
// ============================================================================

namespace SuperTerminal {

void AssetDialogsInterface::Initialize(AssetsManager* manager) {
    [[AssetDialogManager sharedManager] setAssetsManager:manager];
}

void AssetDialogsInterface::ShowLoadScriptDialog(void* parentWindow,
                                                 std::function<void(const std::string&, const std::string&)> callback) {
    NSWindow* nsWindow = (__bridge NSWindow*)parentWindow;
    [[AssetDialogManager sharedManager] showLoadScriptDialog:nsWindow completion:^(NSString* name, NSString* content) {
        if (callback) {
            if (name && content) {
                callback([name UTF8String], [content UTF8String]);
            } else {
                callback("", "");
            }
        }
    }];
}

void AssetDialogsInterface::ShowSaveScriptDialog(void* parentWindow,
                                                 const std::string& scriptContent,
                                                 std::function<void(bool, const std::string&)> callback) {
    NSWindow* nsWindow = (__bridge NSWindow*)parentWindow;
    NSString* content = [NSString stringWithUTF8String:scriptContent.c_str()];
    [[AssetDialogManager sharedManager] showSaveScriptDialog:nsWindow scriptContent:content completion:^(BOOL success, NSString* message) {
        if (callback) {
            callback(success, message ? [message UTF8String] : "");
        }
    }];
}

} // namespace SuperTerminal
