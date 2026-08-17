#include "Response.hpp"
#include "MimeTypes.hpp"
#include "Utils.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <fstream>
#include <sstream>
#include <vector>

#include <cstdlib>

std::string Response::reasonPhrase(int code) {
	switch (code) {
		case 200: return "OK";
		case 201: return "Created";
		case 204: return "No Content";
		case 301: return "Moved Permanently";
		case 302: return "Found";
		case 303: return "See Other";
		case 400: return "Bad Request";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 413: return "Payload Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 502: return "Bad Gateway";
		case 504: return "Gateway Timeout";
		case 505: return "HTTP Version Not Supported";
		default: return "Unknown";
	}
}

Response::Response() : statusCode(200), reason("OK") {}

void Response::setStatus(int code, const std::string& phrase) {
	statusCode = code;
	reason = phrase;
}

void Response::setHeader(const std::string& key, const std::string& value) {
	headers[key] = value;
}

void Response::setBody(const std::string& data) {
	body = data;
}

Response Response::fromHtml(int code, const std::string& title, const std::string& content) {
	Response res;
	res.setStatus(code, reasonPhrase(code));
	std::ostringstream html;
	html << "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
	     << "<title>" << title << "</title>\n"
	     << "<link rel=\"stylesheet\" href=\"/style.css\">\n"
	     << "</head>\n<body>\n"
	     << content
	     << "<hr>\n<footer><em>webserv</em> - 42 project</footer>\n"
	     << "</body>\n</html>\n";
	res.setBody(html.str());
	res.setHeader("Content-Type", "text/html; charset=utf-8");
	return res;
}

Response Response::fromError(int code, const ServerBlock* block, const std::string& detail) {
	if (block != NULL) {
		std::string page = block->errorPageFor(code);
		if (!page.empty()) {
			Response res = fromFile(page, block);
			res.setStatus(code, reasonPhrase(code));
			return res;
		}
	}

	std::ostringstream content;
	content << "<h1>" << code << " " << reasonPhrase(code) << "</h1>\n";
	if (!detail.empty())
		content << "<p>" << detail << "</p>\n";
	content << "<p><a href=\"/\">back to home</a></p>\n";
	return fromHtml(code, reasonPhrase(code), content.str());
}

Response Response::fromFile(const std::string& path, const ServerBlock* block) {
	Response res;
	struct stat st;
	if (stat(path.c_str(), &st) != 0)
		return fromError(404, block);
	if (S_ISDIR(st.st_mode))
		return fromError(403, block, "the requested path is a directory");

	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	if (!file)
		return fromError(404, block);

	std::ostringstream content;
	content << file.rdbuf();
	res.setStatus(200, reasonPhrase(200));
	res.setBody(content.str());
	res.setHeader("Content-Type", MimeTypes::fromPath(path));
	res.setHeader("Last-Modified", util::httpDate(st.st_mtime));
	return res;
}

namespace {
	std::string entryHref(const std::string& dirUri, const std::string& name, bool isDir) {
		std::string href = dirUri;
		if (href.empty() || href[href.size() - 1] != '/')
			href += "/";
		href += util::urlEncode(name);
		if (isDir)
			href += "/";
		return href;
	}
}

Response Response::fromDirectory(const std::string& dirPath, const std::string& uri) {
	DIR* dir = opendir(dirPath.c_str());
	if (dir == NULL)
		return fromError(403, NULL);

	std::ostringstream content;
	content << "<h1>Index of " << util::htmlEscape(uri) << "</h1>\n"
	        << "<table>\n<tr><th>Name</th><th>Size</th><th>Modified</th></tr>\n";

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		std::string name = entry->d_name;
		if (name == "." || name == "..")
			continue;

		std::string full = util::join(dirPath, name);
		bool isDirEntry = util::isDirectory(full);
		std::string size = "-";
		std::string modified = "-";
		struct stat st;
		if (stat(full.c_str(), &st) == 0) {
			if (!isDirEntry)
				size = util::intToString(static_cast<long>(st.st_size));
			modified = util::localDate(st.st_mtime);
		}
		if (isDirEntry)
			size = "&lt;dir&gt;";

		content << "<tr><td><a href=\"" << entryHref(uri, name, isDirEntry) << "\">"
		        << util::htmlEscape(name)
		        << (isDirEntry ? "/" : "") << "</a></td><td>" << size
		        << "</td><td>" << modified << "</td></tr>\n";
	}
	closedir(dir);
	content << "</table>\n";
	return fromHtml(200, "Directory listing", content.str());
}

Response Response::fromRedirect(int code, const std::string& target) {
	Response res;
	res.setStatus(code, reasonPhrase(code));
	res.setHeader("Location", target);
	res.setHeader("Content-Length", "0");
	return res;
}

Response Response::fromNoContent() {
	Response res;
	res.setStatus(204, reasonPhrase(204));
	res.setHeader("Content-Length", "0");
	return res;
}

Response Response::fromCreated(const std::string& uploadName) {
	Response res;
	res.setStatus(201, reasonPhrase(201));
	std::ostringstream content;
	content << "<h1>201 Created</h1>\n"
	        << "<p>The resource <code>" << util::htmlEscape(uploadName)
	        << "</code> has been uploaded.</p>\n"
	        << "<p><a href=\"/files\">see uploaded files</a></p>\n";
	res.setBody(content.str());
	res.setHeader("Content-Type", "text/html; charset=utf-8");
	return res;
}

Response Response::fromCgi(const std::string& raw) {
	Response res;

	std::string head;
	std::string bodyPart;
	std::string::size_type sep = raw.find("\r\n\r\n");
	if (sep == std::string::npos)
		sep = raw.find("\n\n");

	if (sep == std::string::npos) {
		res.setStatus(200, reasonPhrase(200));
		res.setBody(raw);
		res.setHeader("Content-Type", "text/html; charset=utf-8");
		return res;
	}

	head = raw.substr(0, sep);
	bodyPart = raw.substr(sep + (raw[sep] == '\r' ? 4 : 2));

	bool hasStatus = false;
	std::vector<std::string> lines = util::split(head, '\n');
	for (size_t i = 0; i < lines.size(); ++i) {
		std::string line = lines[i];
		if (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.empty())
			continue;

		if (util::startsWith(line, "Status:")) {
			int code = std::atoi(line.substr(7).c_str());
			res.setStatus(code, reasonPhrase(code));
			hasStatus = true;
			continue;
		}

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		std::string key = line.substr(0, colon);
		std::string value = util::trim(line.substr(colon + 1));
		if (util::toLower(key) == "content-length")
			continue;
		if (key.empty())
			continue;
		res.setHeader(key, value);
	}

	if (!hasStatus) {
		if (res.headers.find("Location") == res.headers.end()) {
			res.setStatus(200, reasonPhrase(200));
		} else {
			res.setStatus(302, reasonPhrase(302));
		}
	}
	res.setBody(bodyPart);
	return res;
}

std::string Response::serialize(const std::string& httpVersion) const {
	std::ostringstream out;
	out << httpVersion << " " << statusCode << " " << reason << "\r\n";
	out << "Server: webserv\r\n";
	out << "Date: " << util::httpDate(std::time(NULL)) << "\r\n";
	out << "Connection: close\r\n";

	bool hasLength = false;
	for (std::map<std::string, std::string>::const_iterator it = headers.begin();
	     it != headers.end(); ++it) {
		if (util::toLower(it->first) == "content-length")
			hasLength = true;
		out << it->first << ": " << it->second << "\r\n";
	}
	if (!hasLength)
		out << "Content-Length: " << body.size() << "\r\n";

	out << "\r\n" << body;
	return out.str();
}