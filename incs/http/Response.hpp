#ifndef RESPONSE_HPP
#define RESPONSE_HPP

#include <map>
#include <string>

#include "ServerBlock.hpp"

class Response {
public:
	Response();

	int statusCode;
	std::string reason;
	std::map<std::string, std::string> headers;
	std::string body;

	static std::string reasonPhrase(int code);
	static Response fromError(int code, const ServerBlock* block, const std::string& detail = "");
	static Response fromFile(const std::string& path, const ServerBlock* block);
	static Response fromDirectory(const std::string& dirPath, const std::string& uri);
	static Response fromRedirect(int code, const std::string& target);
	static Response fromNoContent();
	static Response fromCreated(const std::string& uploadName);
	static Response fromHtml(int code, const std::string& title, const std::string& content);
	static Response fromCgi(const std::string& raw);

	void setStatus(int code, const std::string& phrase);
	void setHeader(const std::string& key, const std::string& value);
	void setBody(const std::string& data);
	std::string serialize(const std::string& httpVersion) const;
};

#endif