#include "expand_abc_repeats.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <vector>

// Structure to hold a voice section
struct VoiceSection {
    std::string voice_marker;  // e.g., "[V:lead]"
    std::string content;       // The music lines for this voice
};

std::string expandABCRepeats(const std::string& abc_content) {
    std::istringstream input(abc_content);
    std::ostringstream header_output;
    std::ostringstream result_output;
    std::string line;
    
    bool in_header = true;
    std::vector<VoiceSection> voice_sections;
    VoiceSection current_section;
    
    // First pass: separate header from music and collect voice sections
    while (std::getline(input, line)) {
        if (in_header) {
            header_output << line << "\n";
            // Check if this is the K: field (end of header)
            if (line.length() > 2 && line[0] == 'K' && line[1] == ':') {
                in_header = false;
            }
        } else {
            // Check for voice switch marker [V:...]
            if (line.length() > 2 && line[0] == '[' && line[1] == 'V' && line[2] == ':') {
                // Save previous section if it has content
                if (!current_section.content.empty()) {
                    voice_sections.push_back(current_section);
                }
                
                // Start new section
                current_section.voice_marker = line;
                current_section.content.clear();
            } else if (!line.empty() && line[0] != '%') {
                // Add to current voice section (skip comments)
                current_section.content += line + "\n";
            }
        }
    }
    
    // Don't forget the last section
    if (!current_section.content.empty()) {
        voice_sections.push_back(current_section);
    }
    
    // Now expand repeats in each voice section separately
    result_output << header_output.str();
    
    if (voice_sections.empty()) {
        // No voice sections - process as single-voice ABC
        std::string music_content = current_section.content;
        result_output << expandMusicRepeats(music_content);
    } else {
        // Multi-voice ABC - expand each voice separately
        for (const auto& section : voice_sections) {
            result_output << section.voice_marker << "\n";
            std::string expanded = expandMusicRepeats(section.content);
            result_output << expanded;
        }
    }
    
    return result_output.str();
}

std::string expandLineRepeats(const std::string& line) {
    std::string result = line;
    
    // Handle simple repeats |: ... :|
    std::regex repeat_regex(R"(\|:\s*([^|]*?)\s*:\|)");
    std::smatch match;
    
    while (std::regex_search(result, match, repeat_regex)) {
        std::string repeat_content = match[1].str();
        std::string replacement = repeat_content + " " + repeat_content;
        result = std::regex_replace(result, repeat_regex, replacement, std::regex_constants::format_first_only);
    }
    
    return result;
}

std::string expandMusicRepeats(const std::string& music_content) {
    std::string result = music_content;
    
    // Handle multi-line repeats |: ... :|
    // This regex finds repeat sections that can span multiple lines
    // but stops at voice markers or other structural elements
    std::regex repeat_regex(R"(\|:\s*((?:[^|]|\|[^:])*?)\s*:\|)", std::regex_constants::ECMAScript);
    
    int max_iterations = 100; // Prevent infinite loops
    int iterations = 0;
    
    while (iterations < max_iterations) {
        std::smatch match;
        if (!std::regex_search(result, match, repeat_regex)) {
            break;
        }
        
        std::string repeat_content = match[1].str();
        // Remove the repeat markers and duplicate the content
        std::string replacement = repeat_content + " " + repeat_content;
        
        // Replace just this one occurrence
        result = result.substr(0, match.position()) + replacement + result.substr(match.position() + match.length());
        
        iterations++;
    }
    
    if (iterations >= max_iterations) {
        std::cerr << "Warning: expandMusicRepeats hit iteration limit, possible infinite loop" << std::endl;
    }
    
    return result;
}