#include "Utils.hpp"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace util {

std::string trim(const std::string& s) {
	std::string::size_type first = s.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return "";
	std::string::size_type last = s.find_last_not_of(" \t\r\n");
	return s.substr(first, last - first + 1);
}

std::string toLower(const std::string& s) {
	std::string out = s;
	for (std::string::size_type i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

bool startsWith(const std::string& s, const std::string& prefix) {
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size() &&
	       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
	std::vector<std::string> parts;
	std::string current;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		if (s[i] == sep) {
			parts.push_back(current);
			current.clear();
		} else {
			current += s[i];
		}
	}
	parts.push_back(current);
	return parts;
}

std::string join(const std::string& a, const std::string& b) {
	if (a.empty())
		return b;
	if (a[a.size() - 1] == '/')
		return a + b;
	return a + "/" + b;
}

std::string intToString(long value) {
	std::ostringstream out;
	out << value;
	return out.str();
}

bool parseUnsignedLong(const std::string& s, unsigned long& out) {
	if (s.empty())
		return false;
	char* end = NULL;
	unsigned long value = std::strtoul(s.c_str(), &end, 10);
	if (*end != '\0')
		return false;
	out = value;
	return true;
}

bool fileExists(const std::string& path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0;
}

bool isDirectory(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISDIR(st.st_mode) != 0;
}

bool isRegularFile(const std::string& path) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return false;
	return S_ISREG(st.st_mode) != 0;
}

std::string httpDate(time_t when) {
	char buffer[64];
	struct tm tmv;
	if (gmtime_r(&when, &tmv) == NULL)
		return "";
	std::strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tmv);
	return buffer;
}

std::string localDate(time_t when) {
	char buffer[64];
	struct tm tmv;
	if (localtime_r(&when, &tmv) == NULL)
		return "";
	std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tmv);
	return buffer;
}

std::string percentDecode(const std::string& s) {
	std::string out;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size() &&
		    std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
		    std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
			char hex[3] = { s[i + 1], s[i + 2], '\0' };
			out += static_cast<char>(std::strtoul(hex, NULL, 16));
			i += 2;
		} else {
			out += s[i];
		}
	}
	return out;
}

bool normalizePath(const std::string& raw, std::string& clean) {
	if (raw.empty() || raw[0] != '/')
		return false;

	std::vector<std::string> segments = split(raw, '/');
	std::vector<std::string> stack;
	for (size_t i = 0; i < segments.size(); ++i) {
		if (segments[i].empty() || segments[i] == ".")
			continue;
		if (segments[i] == "..") {
			if (stack.empty())
				return false;
			stack.pop_back();
		} else {
			stack.push_back(segments[i]);
		}
	}

	if (stack.empty()) {
		clean = "/";
		return true;
	}
	clean.clear();
	for (size_t i = 0; i < stack.size(); ++i)
		clean += "/" + stack[i];
	return true;
}

std::string baseName(const std::string& path) {
	std::string::size_type slash = path.find_last_of('/');
	if (slash == std::string::npos)
		return path;
	return path.substr(slash + 1);
}

std::string urlEncode(const std::string& s) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

std::string htmlEscape(const std::string& s) {
	std::string out;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		switch (s[i]) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			case '"': out += "&quot;"; break;
			default:  out += s[i];
		}
	}
	return out;
}

std::string absolutePath(const std::string& path) {
	if (!path.empty() && path[0] == '/')
		return path;
	char cwd[4096];
	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return path;
	return join(cwd, path);
}

void setNonBlocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void setCloseOnExec(int fd) {
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

} // namespace util