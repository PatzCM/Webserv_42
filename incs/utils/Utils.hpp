#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>
#include <ctime>

namespace util {

std::string trim(const std::string& s);
std::string toLower(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::string& a, const std::string& b);
std::string intToString(long value);

bool parseUnsignedLong(const std::string& s, unsigned long& out);
bool fileExists(const std::string& path);
bool isDirectory(const std::string& path);
bool isRegularFile(const std::string& path);

std::string httpDate(time_t when);
std::string localDate(time_t when);
std::string percentDecode(const std::string& s);
bool normalizePath(const std::string& raw, std::string& clean);
std::string baseName(const std::string& path);
std::string urlEncode(const std::string& s);
std::string htmlEscape(const std::string& s);
std::string absolutePath(const std::string& path);

void setNonBlocking(int fd);
void setCloseOnExec(int fd);

} // namespace util

#endif