#include "string_aux.h"
#include <algorithm>
#include <sstream>

std::string Trim( const std::string& str, const std::string& whitespace )
{
	const auto strBegin = str.find_first_not_of( whitespace );
	if ( strBegin == std::string::npos ) {
		return "";    // no content
	}

	const auto strEnd = str.find_last_not_of( whitespace );
	const auto strRange = strEnd - strBegin + 1;

	return str.substr( strBegin, strRange );
}

std::vector<std::string> Split( const std::string &text, char sep, bool ignoreEmptyStrings )
{
	std::vector<std::string> tokens;
	std::size_t start = 0, end = 0;
	while ( ( end = text.find( sep, start ) ) != std::string::npos ) {
		std::string temp = text.substr( start, end - start );
		if ( temp != "" || !ignoreEmptyStrings ) {
			tokens.push_back( temp );
		}
		start = end + 1;
	}
	std::string temp = text.substr( start );
	if ( temp != "" || !ignoreEmptyStrings ) {
		tokens.push_back( temp );
	}
	return tokens;
}

std::deque<std::string> SplitDeque( const std::string &text, char sep )
{
	std::deque<std::string> tokens;
	std::size_t start = 0, end = 0;
	while ( ( end = text.find( sep, start ) ) != std::string::npos ) {
		std::string temp = text.substr( start, end - start );
		if ( temp != "" ) {
			tokens.push_back( temp );
		}
		start = end + 1;
	}
	std::string temp = text.substr( start );
	if ( temp != "" ) {
		tokens.push_back( temp );
	}
	return tokens;
}

std::string Uppercase( std::string text )
{
    std::transform(text.begin(), text.end(), text.begin(), ::toupper);

    return text;
}

// https://stackoverflow.com/questions/874134/find-if-string-ends-with-another-string-in-c
bool EndsWith( const std::string &fullString, const std::string &ending ) {
	if ( fullString.length() >= ending.length() ) {
		return fullString.compare( fullString.length() - ending.length(), ending.length(), ending ) == 0;
	} else {
		return false;
	}
}

std::vector<std::string> NaiveCommandLineParse( const std::string &input ) {
	std::vector<std::string> args;

	std::string arg;

	bool quotes = false;
	bool writingWord = false;

	for ( const auto &letter : input ) {
		switch ( letter ) {
			case '"': {
				if ( writingWord ) {
					if ( quotes ) {
						quotes = false;
						args.push_back( arg );
						arg = "";
					} else {
						writingWord = true;
						quotes = true;
					}
				} else {
					quotes = true;
				}

				break;
			}

			case ' ': {
				if ( quotes ) {
					arg += letter;
				} else if ( writingWord ) {
					args.push_back( arg );
					arg.clear();

					writingWord = false;
				}

				break;
			}

			default: {
				writingWord = true;
				arg += letter;

				break;
			}
		}
	}

	if ( !arg.empty() ) {
		args.push_back( arg );
	}

	return args;
}

// Based on https://www.rosettacode.org/wiki/Word_wrap#C.2B.2B
std::vector<std::string> Wrap( const char *text, float line_length, std::function<float(const std::string &)> text_length_calculator ) {
	std::istringstream words( text );
	std::ostringstream wrapped;
	std::string word;

	std::vector<std::string> result;

	if ( words >> word ) {
		wrapped << word;
		float space_left = line_length - text_length_calculator( word );

		while ( words >> word ) {
			float wordLength = text_length_calculator( word );
			if ( space_left < wordLength + 1 ) {
				result.push_back( wrapped.str() );
				wrapped = std::ostringstream(); // to reset 'wrapped' stream
				wrapped << word;
				space_left = line_length - wordLength;
			} else {
				wrapped << ' ' << word;
				space_left -= wordLength + 1;
			}
		}

		result.push_back( wrapped.str() );
	}

	return result;
}