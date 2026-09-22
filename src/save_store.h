#pragma once
#include <string>
#include <vector>
namespace peek {
// Where the modded run keeps its progress. One file per name the game asks Steam Cloud for,
// so a save imported from Steam is a plain copy and stays readable by the unmodded game.
std::wstring saveFolder();
// The game only ever names its own slots: "save0", "save0-dlc", "save0-dlc2". A name carrying
// a separator, a drive, or a parent step would write outside the save folder, so it is refused
// rather than resolved.
bool validSaveName(const std::string& file);
// The progress files themselves. The game hands over a whole save in one call, and a file left
// half written is the loss this exists to prevent, so a write lands beside the save and
// replaces it only once every byte is on disk.
struct SaveStorage {
    std::wstring folder;
    // Optional record of what the game did with its save, for the log that explains a failure.
    void (*note)(const char* what,const char* file,int bytes)=nullptr;
    bool write(const std::string& file,const void* data,int bytes) const;
    int read(const std::string& file,void* data,int bytes) const;
    int size(const std::string& file) const;
    bool exists(const std::string& file) const;
    bool erase(const std::string& file) const;
};
// Copies save files from `from` into `into`, first keeping whatever was already there in a
// dated folder beside them: an import replaces progress, and the copy it replaced has to stay
// recoverable. Returns how many files arrived, with a reason in `error` when none did.
int importSaves(const std::wstring& from,const std::vector<std::string>& files,const std::wstring& into,std::wstring& error);
// What the game receives instead of the context Steam would hand it. Only remote storage is
// present, backed by `storage`; every other interface stays null, which is the case the game
// already guards for, so its stats, achievements, and overlay calls stay unmade.
void* steamContext(const SaveStorage& storage);
}
