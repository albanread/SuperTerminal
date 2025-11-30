//
//  LuaFormatter.cpp
//  SuperTerminal Framework - Lua Code Formatter
//
//  Created by SuperTerminal Project
//  Copyright © 2024 SuperTerminal. All rights reserved.
//
//  Implementation of simple Lua code formatter with 2-space indentation
//

#include "LuaFormatter.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>

// Static keyword definitions
const std::vector<std::string> LuaFormatter::LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", 
    "function", "if", "in", "local", "nil", "not", "or", "repeat", 
    "return", "then", "true", "until", "while"
};

const std::vector<std::string> LuaFormatter::OPENING_KEYWORDS = {
    "function", "if", "for", "while", "repeat", "do"
};

const std::vector<std::string> LuaFormatter::CLOSING_KEYWORDS = {
    "end", "until"
};

const std::vector<std::string> LuaFormatter::CONTROL_KEYWORDS = {
    "else", "elseif"
};

const std::string LuaFormatter::OPERATORS = "=+-*/^%<>~";

std::vector<std::string> LuaFormatter::formatLines(const std::vector<std::string>& lines, 
                                                 const FormattingOptions& options) {
    std::vector<std::string> result;
    FormattingContext context;
    
    for (const auto& line : lines) {
        std::string formatted = formatLine(line, context, options);
        result.push_back(formatted);
    }
    
    // Ensure final newline if requested
    if (options.ensureFinalNewline && !result.empty() && !result.back().empty()) {
        // This is handled by the editor, not by adding empty lines
    }
    
    return result;
}

std::string LuaFormatter::formatString(const std::string& luaCode, const FormattingOptions& options) {
    std::vector<std::string> lines;
    std::istringstream stream(luaCode);
    std::string line;
    
    while (std::getline(stream, line)) {
        lines.push_back(line);
    }
    
    std::vector<std::string> formatted = formatLines(lines, options);
    
    std::ostringstream result;
    for (size_t i = 0; i < formatted.size(); ++i) {
        result << formatted[i];
        if (i < formatted.size() - 1) {
            result << "\n";
        }
    }
    
    return result.str();
}

void LuaFormatter::formatInPlace(std::vector<std::string>& lines, const FormattingOptions& options) {
    lines = formatLines(lines, options);
}

std::string LuaFormatter::formatLine(const std::string& line, FormattingContext& context, 
                                   const FormattingOptions& options) {
    // Skip empty lines - just trim them
    if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
        return "";
    }
    
    // Update context for strings and comments
    updateStringContext(line, context);
    updateCommentContext(line, context);
    
    // Don't format lines that are entirely within strings or multi-line constructs
    if (context.inMultiLineString || context.inMultiLineComment) {
        return line; // Return as-is
    }
    
    // Special handling for ABC musical notation patterns
    std::string trimmed = line;
    size_t firstChar = trimmed.find_first_not_of(" \t");
    if (firstChar != std::string::npos) {
        trimmed = trimmed.substr(firstChar);
    }
    
    // ABC voice lines like "[V:violin]" - keep minimal formatting
    if (trimmed.length() > 3 && trimmed[0] == '[' && trimmed.find("V:") == 1) {
        std::string formatted = trimmed;
        formatted = applyIndentation(formatted, context.currentIndentLevel, options);
        return formatted;
    }
    
    // ABC header lines like "T:Title" or "M:4/4" - keep minimal formatting  
    if (trimmed.length() > 2 && trimmed[1] == ':' && 
        (trimmed[0] == 'T' || trimmed[0] == 'M' || trimmed[0] == 'L' || 
         trimmed[0] == 'K' || trimmed[0] == 'C' || trimmed[0] == 'V')) {
        std::string formatted = trimmed;
        formatted = applyIndentation(formatted, context.currentIndentLevel, options);
        return formatted;
    }
    
    // Musical note sequences - minimal formatting for readability
    if (trimmed.find_first_of("ABCDEFGabcdefg") == 0 && 
        (trimmed.find('|') != std::string::npos || trimmed.find(':') != std::string::npos)) {
        std::string formatted = trimmed;
        formatted = applyIndentation(formatted, context.currentIndentLevel, options);
        return formatted;
    }
    
    // Calculate indent change BEFORE applying current indent
    int indentChange = calculateIndentChange(line, context);
    
    // Check if this line contains a control keyword (else, elseif)
    // Control keywords need special handling - they should be at the reduced indent level
    bool isControlKeyword = false;
    std::string lowerLine = toLower(trimmed);
    std::vector<std::string> tokens = tokenizeLine(trimmed);
    for (const auto& token : tokens) {
        std::string lowerToken = toLower(token);
        if (lowerToken == "else" || lowerToken == "elseif") {
            isControlKeyword = true;
            break;
        }
    }
    
    // Apply closing indent (reduce before formatting this line)
    // For control keywords, also reduce by 1 to match the opening 'if' level
    if (indentChange < 0) {
        context.currentIndentLevel = std::max(0, context.currentIndentLevel + indentChange);
    } else if (isControlKeyword) {
        // Control keywords should be at the same level as their matching 'if'
        context.currentIndentLevel = std::max(0, context.currentIndentLevel - 1);
    }
    
    // Format the line content - first trim leading and trailing whitespace
    std::string formatted = line;
    
    // Remove leading whitespace (we'll apply our own indentation)
    size_t firstNonSpace = formatted.find_first_not_of(" \t");
    if (firstNonSpace != std::string::npos) {
        formatted = formatted.substr(firstNonSpace);
    } else {
        formatted = ""; // Line was only whitespace
    }
    
    // Trim trailing whitespace
    formatted = trimWhitespace(formatted, options);
    
    // Skip comment-only lines for most formatting
    if (formatted.length() >= 2 && formatted.substr(0, 2) == "--") {
        formatted = formatComments(formatted, options);
        formatted = applyIndentation(formatted, context.currentIndentLevel, options);
        return formatted;
    }
    
    // Apply formatting
    formatted = normalizeSpacing(formatted, options);
    formatted = formatOperators(formatted, options);
    formatted = formatKeywords(formatted, options);
    formatted = formatComments(formatted, options);
    // formatted = formatArrayElements(formatted, options); // TODO: implement this function
    
    // Apply indentation
    formatted = applyIndentation(formatted, context.currentIndentLevel, options);
    
    // Apply opening indent (increase after formatting this line)
    // For control keywords, we reduced by 1 earlier, so always add it back
    // because else/elseif always have a body that needs indenting
    if (isControlKeyword) {
        context.currentIndentLevel += 1;
    }
    if (indentChange > 0) {
        context.currentIndentLevel += indentChange;
    }
    
    return formatted;
}

std::string LuaFormatter::applyIndentation(const std::string& line, int indentLevel,
                                         const FormattingOptions& options) {
    if (line.empty()) return line;
    
    std::string indent = repeat(" ", indentLevel * options.indentSize);
    
    // Find first non-whitespace character
    size_t firstChar = line.find_first_not_of(" \t");
    if (firstChar == std::string::npos) {
        return ""; // Empty line
    }
    
    return indent + line.substr(firstChar);
}

std::string LuaFormatter::normalizeSpacing(const std::string& line, const FormattingOptions& options) {
    if (line.empty()) return line;
    
    std::string result;
    result.reserve(line.length() * 2);
    
    bool inString = false;
    char stringChar = '\0';
    bool inComment = false;
    
    // Special handling for array entries in musical data
    if (line.find("name=") != std::string::npos && line.find("instrument=") != std::string::npos) {
        // Voice definition line - add extra spacing around =
        std::string formatted = line;
        size_t pos = 0;
        while ((pos = formatted.find("=", pos)) != std::string::npos) {
            if (pos > 0 && formatted[pos-1] != ' ') {
                formatted.insert(pos, " ");
                pos++;
            }
            if (pos + 1 < formatted.length() && formatted[pos+1] != ' ') {
                formatted.insert(pos + 1, " ");
                pos++;
            }
            pos++;
        }
        return formatted;
    }
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        char next = (i + 1 < line.length()) ? line[i + 1] : '\0';
        
        // Handle string literals
        if (!inComment && (c == '"' || c == '\'') && (i == 0 || line[i-1] != '\\')) {
            if (!inString) {
                inString = true;
                stringChar = c;
            } else if (c == stringChar) {
                inString = false;
                stringChar = '\0';
            }
            result += c;
            continue;
        }
        
        // Handle comments
        if (!inString && c == '-' && next == '-') {
            inComment = true;
            result += c;
            continue;
        }
        
        // Don't format inside strings or comments
        if (inString || inComment) {
            result += c;
            continue;
        }
        
        // Normalize multiple spaces to single space
        if (c == ' ' || c == '\t') {
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
            continue;
        }
        
        result += c;
    }
    
    return result;
}

std::string LuaFormatter::formatOperators(const std::string& line, const FormattingOptions& options) {
    if (!options.spaceAroundOperators || line.empty()) return line;
    
    std::string result;
    result.reserve(line.length() * 2);
    
    bool inString = false;
    char stringChar = '\0';
    bool inComment = false;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        char prev = (i > 0) ? line[i-1] : '\0';
        char next = (i + 1 < line.length()) ? line[i + 1] : '\0';
        
        // Handle strings and comments
        if (!inComment && (c == '"' || c == '\'') && prev != '\\') {
            if (!inString) {
                inString = true;
                stringChar = c;
            } else if (c == stringChar) {
                inString = false;
            }
            result += c;
            continue;
        }
        
        if (!inString && c == '-' && next == '-') {
            inComment = true;
            result += c;
            continue;
        }
        
        if (inString || inComment) {
            result += c;
            continue;
        }
        
        // Format operators - handle multi-character operators first
        if (isOperator(c)) {
            // Check for multi-character operators
            if ((c == '<' && next == '=') || 
                (c == '>' && next == '=') ||
                (c == '=' && next == '=') ||
                (c == '~' && next == '=')) {
                // Multi-character operator - add spaces around the whole operator
                if (!result.empty() && result.back() != ' ') {
                    result += ' ';
                }
                result += c;
                result += next;
                i++; // Skip next character since we processed it
                // Add space after if next character isn't whitespace or operator
                if (i + 1 < line.length() && line[i + 1] != ' ' && !isOperator(line[i + 1])) {
                    result += ' ';
                }
            } else if (c == '.' && next == '.') {
                // Handle .. (concatenation)
                if (!result.empty() && result.back() != ' ') {
                    result += ' ';
                }
                result += c;
                result += next;
                i++; // Skip next character
                if (i + 1 < line.length() && line[i + 1] != ' ') {
                    result += ' ';
                }
            } else {
                // Single-character operator - add spaces around it
                if (!result.empty() && result.back() != ' ') {
                    result += ' ';
                }
                result += c;
                if (next != '\0' && next != ' ' && !isOperator(next)) {
                    result += ' ';
                }
            }
        } else {
            result += c;
        }
    }
    
    return result;
}

std::string LuaFormatter::formatKeywords(const std::string& line, const FormattingOptions& options) {
    if (!options.spaceAfterKeywords) return line;
    
    std::string result = line;
    
    // Add space after keywords like "if", "for", "while", etc.
    for (const auto& keyword : LUA_KEYWORDS) {
        size_t pos = 0;
        while ((pos = result.find(keyword, pos)) != std::string::npos) {
            // Check if it's a whole word
            bool isWholeWord = true;
            if (pos > 0 && std::isalnum(result[pos-1])) {
                isWholeWord = false;
            }
            if (pos + keyword.length() < result.length() && 
                std::isalnum(result[pos + keyword.length()])) {
                isWholeWord = false;
            }
            
            if (isWholeWord) {
                size_t afterKeyword = pos + keyword.length();
                if (afterKeyword < result.length() && result[afterKeyword] != ' ' && 
                    result[afterKeyword] != '\t' && result[afterKeyword] != '\n') {
                    // Keywords that should have space after them
                    if (keyword == "if" || keyword == "for" || keyword == "while" || 
                        keyword == "function" || keyword == "local" || keyword == "return") {
                        result.insert(afterKeyword, " ");
                    }
                }
            }
            pos += keyword.length();
        }
    }
    
    return result;
}

std::string LuaFormatter::formatComments(const std::string& line, const FormattingOptions& options) {
    if (!options.normalizeComments) return line;
    
    size_t commentPos = line.find("--");
    if (commentPos == std::string::npos) return line;
    
    std::string beforeComment = line.substr(0, commentPos);
    std::string comment = line.substr(commentPos);
    
    // Handle comment content after --
    if (comment.length() > 2) {
        std::string commentContent = comment.substr(2);
        
        // Remove leading spaces/tabs from comment content
        size_t firstNonSpace = commentContent.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos) {
            commentContent = commentContent.substr(firstNonSpace);
        } else {
            commentContent = ""; // Comment was only spaces
        }
        
        // Rebuild comment with single space after --
        if (commentContent.empty()) {
            comment = "--";
        } else {
            comment = "-- " + commentContent;
        }
    }
    
    return beforeComment + comment;
}

std::string LuaFormatter::trimWhitespace(const std::string& line, const FormattingOptions& options) {
    if (!options.trimTrailingSpaces) return line;
    
    // Trim trailing whitespace
    size_t end = line.find_last_not_of(" \t");
    if (end == std::string::npos) return "";
    
    return line.substr(0, end + 1);
}

int LuaFormatter::calculateIndentChange(const std::string& line, FormattingContext& context) {
    std::string trimmed = line;
    size_t firstChar = trimmed.find_first_not_of(" \t");
    if (firstChar != std::string::npos) {
        trimmed = trimmed.substr(firstChar);
    }
    
    // Convert to lowercase for comparison
    std::string lower = toLower(trimmed);
    
    int change = 0;
    
    // Tokenize the line to find actual keywords (not parts of identifiers)
    std::vector<std::string> tokens = tokenizeLine(trimmed);
    std::vector<std::string> lowerTokens;
    for (const auto& token : tokens) {
        lowerTokens.push_back(toLower(token));
    }
    
    // Count ALL keywords on the line, not just the first of each type
    // This fixes the issue where multiple keywords on one line would be miscounted
    
    // Special handling: for/while statements include 'do' but shouldn't double-count
    // We need to check if 'do' is part of a for/while statement
    bool hasForOrWhile = false;
    for (const auto& token : lowerTokens) {
        if (token == "for" || token == "while") {
            hasForOrWhile = true;
            break;
        }
    }
    
    // Check for closing keywords (decrease indent)
    for (const auto& token : lowerTokens) {
        for (const auto& keyword : CLOSING_KEYWORDS) {
            if (token == keyword) {
                change -= 1;
                break; // Break from CLOSING_KEYWORDS loop, continue with next token
            }
        }
    }
    
    // Check for control keywords (else, elseif)
    // Note: These are now handled specially in formatLine() for proper indentation
    // We don't count them here since they need to be at the reduced level
    for (const auto& token : lowerTokens) {
        for (const auto& keyword : CONTROL_KEYWORDS) {
            if (token == keyword) {
                // Control keywords don't change the indent level calculation
                // They're handled separately in formatLine()
                break; // Break from CONTROL_KEYWORDS loop, continue with next token
            }
        }
    }
    
    // Check for opening keywords (increase indent for following lines)
    for (const auto& token : lowerTokens) {
        for (const auto& keyword : OPENING_KEYWORDS) {
            if (token == keyword) {
                // Don't count 'do' if we already have 'for' or 'while' on this line
                if (token == "do" && hasForOrWhile) {
                    break; // Skip this keyword, continue to next token
                }
                change += 1;
                break; // Break from OPENING_KEYWORDS loop, continue with next token
            }
        }
    }
    
    // Handle table braces - count all braces on the line
    for (const auto& token : tokens) {
        if (token == "}") {
            // Closing brace - decrease indent for this line
            change -= 1;
        } else if (token == "{") {
            // Opening brace - increase indent for following lines
            change += 1;
        }
    }
    
    // Special case: Don't change indentation for ABC notation lines
    std::string trimmedForABC = trimmed;
    if (!trimmedForABC.empty()) {
        // ABC voice lines, header lines, and note sequences should not affect indentation
        if ((trimmedForABC.length() > 3 && trimmedForABC[0] == '[' && trimmedForABC.find("V:") == 1) ||
            (trimmedForABC.length() > 2 && trimmedForABC[1] == ':' && 
             (trimmedForABC[0] == 'T' || trimmedForABC[0] == 'M' || trimmedForABC[0] == 'L' || 
              trimmedForABC[0] == 'K' || trimmedForABC[0] == 'C')) ||
            (trimmedForABC.find_first_of("ABCDEFGabcdefg") == 0 && 
             (trimmedForABC.find('|') != std::string::npos || trimmedForABC.find(':') != std::string::npos))) {
            return 0; // No indentation change for ABC notation
        }
    }
    
    return change;
}

bool LuaFormatter::isOpeningKeyword(const std::string& word) {
    return std::find(OPENING_KEYWORDS.begin(), OPENING_KEYWORDS.end(), word) != OPENING_KEYWORDS.end();
}

bool LuaFormatter::isClosingKeyword(const std::string& word) {
    return std::find(CLOSING_KEYWORDS.begin(), CLOSING_KEYWORDS.end(), word) != CLOSING_KEYWORDS.end();
}

bool LuaFormatter::isControlKeyword(const std::string& word) {
    return std::find(CONTROL_KEYWORDS.begin(), CONTROL_KEYWORDS.end(), word) != CONTROL_KEYWORDS.end();
}

void LuaFormatter::updateStringContext(const std::string& line, FormattingContext& context) {
    // Simple string tracking - this could be more sophisticated
    for (char c : line) {
        if (c == '"' || c == '\'') {
            if (!context.inString) {
                context.inString = true;
                context.stringDelimiter = c;
            } else if (c == context.stringDelimiter) {
                context.inString = false;
                context.stringDelimiter = '\0';
            }
        }
    }
}

void LuaFormatter::updateCommentContext(const std::string& line, FormattingContext& context) {
    // Handle multi-line comments --[[ ]]--
    if (line.find("--[[") != std::string::npos) {
        context.inMultiLineComment = true;
    }
    if (line.find("]]") != std::string::npos && context.inMultiLineComment) {
        context.inMultiLineComment = false;
    }
}

bool LuaFormatter::isInStringOrComment(const FormattingContext& context) {
    return context.inString || context.inComment || context.inMultiLineString || context.inMultiLineComment;
}

std::string LuaFormatter::repeat(const std::string& str, int count) {
    std::string result;
    result.reserve(str.length() * count);
    for (int i = 0; i < count; ++i) {
        result += str;
    }
    return result;
}

std::vector<std::string> LuaFormatter::tokenizeLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else if (std::isalnum(c) || c == '_') {
            // Identifier character
            current += c;
        } else {
            // Operator or punctuation - push current identifier if any
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            
            // Handle multi-character operators
            std::string op(1, c);
            if (i + 1 < line.length()) {
                char next = line[i + 1];
                if ((c == '=' && next == '=') ||
                    (c == '~' && next == '=') ||
                    (c == '<' && next == '=') ||
                    (c == '>' && next == '=') ||
                    (c == '.' && next == '.')) {
                    op += next;
                    i++; // Skip next character
                }
            }
            
            // Only add non-whitespace operators/punctuation as tokens
            if (!op.empty() && !std::isspace(op[0])) {
                tokens.push_back(op);
            }
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

bool LuaFormatter::isOperator(char c) {
    return OPERATORS.find(c) != std::string::npos;
}

bool LuaFormatter::isKeyword(const std::string& word) {
    return std::find(LUA_KEYWORDS.begin(), LUA_KEYWORDS.end(), word) != LUA_KEYWORDS.end();
}

std::string LuaFormatter::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}