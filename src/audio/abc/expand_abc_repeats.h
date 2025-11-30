#ifndef EXPAND_ABC_REPEATS_H
#define EXPAND_ABC_REPEATS_H

#include <string>

/**
 * Expand ABC notation repeats at the text level (macro expansion)
 * This processes |: ... :| repeat markers by duplicating the content
 */
std::string expandABCRepeats(const std::string& abc_content);

/**
 * Expand repeats in a single line of ABC music notation
 */
std::string expandLineRepeats(const std::string& line);

/**
 * Expand repeats in a music section (can span multiple lines)
 */
std::string expandMusicRepeats(const std::string& music_content);

#endif // EXPAND_ABC_REPEATS_H