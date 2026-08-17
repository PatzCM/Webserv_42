#include "MimeTypes.hpp"
#include "Utils.hpp"

#include <map>

namespace {
	const std::string kFallback = "application/octet-stream";

	const std::map<std::string, std::string>& mimeMap() {
		static std::map<std::string, std::string>* table = NULL;
		if (table == NULL) {
			table = new std::map<std::string, std::string>();
			(*table)["html"] = "text/html";
			(*table)["htm"] = "text/html";
			(*table)["css"] = "text/css";
			(*table)["js"] = "application/javascript";
			(*table)["json"] = "application/json";
			(*table)["xml"] = "application/xml";
			(*table)["txt"] = "text/plain";
			(*table)["csv"] = "text/csv";
			(*table)["png"] = "image/png";
			(*table)["jpg"] = "image/jpeg";
			(*table)["jpeg"] = "image/jpeg";
			(*table)["gif"] = "image/gif";
			(*table)["svg"] = "image/svg+xml";
			(*table)["ico"] = "image/x-icon";
			(*table)["bmp"] = "image/bmp";
			(*table)["webp"] = "image/webp";
			(*table)["pdf"] = "application/pdf";
			(*table)["zip"] = "application/zip";
			(*table)["gz"] = "application/gzip";
			(*table)["tar"] = "application/x-tar";
			(*table)["woff"] = "font/woff";
			(*table)["woff2"] = "font/woff2";
			(*table)["ttf"] = "font/ttf";
			(*table)["mp3"] = "audio/mpeg";
			(*table)["mp4"] = "video/mp4";
			(*table)["webm"] = "video/webm";
		}
		return *table;
	}
}

const std::string& MimeTypes::fromPath(const std::string& path) {
	std::string::size_type dot = path.find_last_of('.');
	if (dot == std::string::npos || dot + 1 >= path.size())
		return kFallback;

	std::string ext = util::toLower(path.substr(dot + 1));
	const std::map<std::string, std::string>& table = mimeMap();
	std::map<std::string, std::string>::const_iterator it = table.find(ext);
	if (it == table.end())
		return kFallback;
	return it->second;
}