//
//  LuaFormatter.h
//  SuperTerminal Framework - Lua Code Formatter
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Simple Lua code formatter for consistent styling with 2-space indentation
//

#ifndef LuaFormatter_h
#define LuaFormatter_h

#include <string>
#include <vector>

class LuaFormatter {
public:
    struct FormattingOptions {
        int indentSize;
        bool spaceAroundOperators;
        bool spaceAfterCommas;
        bool spaceAfterKeywords;
        bool normalizeComments;
        bool trimTrailingSpaces;
        bool ensureFinalNewline;
        
        FormattingOptions() 
            : indentSize(2)
            , spaceAroundOperators(true)
            , spaceAfterCommas(true)
            , spaceAfterKeywords(true)
            , normalizeComments(true)
            , trimTrailingSpaces(true)
            , ensureFinalNewline(true) {}
    };
    
    // Main formatting functions
    static std::vector<std::string> formatLines(const std::vector<std::string>& lines, 
                                              const FormattingOptions& options = FormattingOptions());
    
    static std::string formatString(const std::string& luaCode,
                                   const FormattingOptions& options = FormattingOptions());
    
    static void formatInPlace(std::vector<std::string>& lines,
                             const FormattingOptions& options = FormattingOptions());

private:
    struct FormattingContext {
        int currentIndentLevel = 0;
        bool inString = false;
        bool inComment = false;
        bool inMultiLineString = false;
        bool inMultiLineComment = false;
        char stringDelimiter = '\0';
        
        FormattingContext() 
            : currentIndentLevel(0)
            , inString(false)
            , inComment(false)
            , inMultiLineString(false)
            , inMultiLineComment(false)
            , stringDelimiter('\0') {}
    };
    
    // Core formatting methods
    static std::string formatLine(const std::string& line, FormattingContext& context, 
                                 const FormattingOptions& options);
    
    static std::string applyIndentation(const std::string& line, int indentLevel,
                                       const FormattingOptions& options);
    
    static std::string normalizeSpacing(const std::string& line, const FormattingOptions& options);
    
    static std::string formatOperators(const std::string& line, const FormattingOptions& options);
    
    static std::string formatKeywords(const std::string& line, const FormattingOptions& options);
    
    static std::string formatComments(const std::string& line, const FormattingOptions& options);
    
    static std::string trimWhitespace(const std::string& line, const FormattingOptions& options);
    
    // Analysis methods
    static int calculateIndentChange(const std::string& line, FormattingContext& context);
    
    static bool isOpeningKeyword(const std::string& word);
    
    static bool isClosingKeyword(const std::string& word);
    
    static bool isControlKeyword(const std::string& word);
    
    static void updateStringContext(const std::string& line, FormattingContext& context);
    
    static void updateCommentContext(const std::string& line, FormattingContext& context);
    
    static bool isInStringOrComment(const FormattingContext& context);
    
    // Utility methods
    static std::string repeat(const std::string& str, int count);
    
    static std::vector<std::string> tokenizeLine(const std::string& line);
    
    static bool isOperator(char c);
    
    static bool isKeyword(const std::string& word);
    
    static std::string toLower(const std::string& str);
    
    // Lua keywords and operators for reference
    static const std::vector<std::string> LUA_KEYWORDS;
    static const std::vector<std::string> OPENING_KEYWORDS;
    static const std::vector<std::string> CLOSING_KEYWORDS;
    static const std::vector<std::string> CONTROL_KEYWORDS;
    static const std::string OPERATORS;
};

#endif /* LuaFormatter_h */