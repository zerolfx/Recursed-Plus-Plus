#pragma once
#include <string>
namespace peek {
// The mod itself, carried inside the launcher so that what a player downloads is one file. Loading
// it into the game needs a real file on disk, so this writes the copy out and returns its path, or
// an empty string with a reason in `error`. The copy is named after the contents it holds, which is
// what keeps a running game's locked copy and a newer build out of each other's way.
std::wstring embeddedPlugin(std::wstring& error);
// The third-party notices carried in the same executable, ready for a text control: wide, with the
// line endings a Win32 EDIT needs. Empty when the build somehow left them out.
std::wstring embeddedNotices();
}
