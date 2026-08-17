#ifndef MIMETYPES_HPP
#define MIMETYPES_HPP

#include <string>

class MimeTypes {
public:
	static const std::string& fromPath(const std::string& path);
};

#endif