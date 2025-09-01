#include <set>
#include <string>

std::string get_directory(const std::string &path);
void processFile(const std::string &filename,
                 std::set<std::string> &includedFiles, std::ostream &out);
