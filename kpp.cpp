#include <fstream>
#include <iostream>
#include <set>
#include <string>

std::string get_directory(const std::string &path) {
  size_t found = path.find_last_of("/\\");
  if (found != std::string::npos) {
    return path.substr(0, found + 1);
  }
  return "";
}
void processFile(const std::string &filename,
                 std::set<std::string> &includedFiles, std::ostream &out) {
  if (includedFiles.count(filename)) {
    std::cerr << "Error: circular include detected for file '" << filename
              << "'\n";
    return;
  }
  includedFiles.insert(filename);

  std::ifstream file(filename);
  if (!file) {
    std::cerr << "Error: Could not open file '" << filename << "'\n";
    return;
  }
  std::string currDir = get_directory(filename);
  std::string line;
  while (std::getline(file, line)) {
    if (line.rfind("include", 0) == 0) {
      size_t firstQuote = line.find('"');
      size_t lastQuote = line.find('"', firstQuote + 1);

      if (firstQuote == std::string::npos || lastQuote == std::string::npos) {
        std::cerr << "Warning: Malformed include directive: " << line << "\n";
        out << line << "\n";
        continue;
      }

      std::string relFilename =
          line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
      std::string full_path_to_include = currDir + relFilename;
      processFile(full_path_to_include, includedFiles, out);

    } else {
      out << line << "\n";
    }
  }

  includedFiles.erase(filename);
}
