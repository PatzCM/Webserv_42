#include "Config.hpp"
#include "Utils.hpp"

#include <fstream>
#include <sstream>
#include <cstdlib>

namespace {

// Token stream over the configuration file: words plus '{', '}', ';'.
class TokenStream {
public:
	explicit TokenStream(const std::string& content) : pos(0) { tokenize(content); }

	bool end() const { return pos >= tokens.size(); }

	const std::string& peek() const {
		static const std::string empty;
		return end() ? empty : tokens[pos];
	}

	std::string next() {
		if (end())
			throw ConfigException("unexpected end of configuration file");
		return tokens[pos++];
	}

private:
	void tokenize(const std::string& content) {
		std::string word;
		bool comment = false;
		for (size_t i = 0; i < content.size(); ++i) {
			char c = content[i];
			if (comment) {
				if (c == '\n')
					comment = false;
				continue;
			}
			if (c == '#') {
				comment = true;
				continue;
			}
			if (c == '{' || c == '}' || c == ';') {
				if (!word.empty()) {
					tokens.push_back(word);
					word.clear();
				}
				std::string tok;
				tok += c;
				tokens.push_back(tok);
				continue;
			}
			if (std::isspace(static_cast<unsigned char>(c))) {
				if (!word.empty()) {
					tokens.push_back(word);
					word.clear();
				}
				continue;
			}
			word += c;
		}
		if (!word.empty())
			tokens.push_back(word);
	}

	std::vector<std::string> tokens;
	size_t pos;
};

std::string readFile(const std::string& path) {
	std::ifstream file(path.c_str());
	if (!file)
		throw ConfigException("cannot open configuration file: " + path);
	std::ostringstream content;
	content << file.rdbuf();
	return content.str();
}

unsigned long parseSize(const std::string& value) {
	if (value.empty())
		throw ConfigException("invalid size value");
	char* end = NULL;
	unsigned long size = std::strtoul(value.c_str(), &end, 10);
	if (end == value.c_str())
		throw ConfigException("invalid size value: " + value);
	if (*end != '\0') {
		std::string suffix = util::toLower(std::string(end));
		if (suffix == "k")
			size *= 1024;
		else if (suffix == "m")
			size *= 1024 * 1024;
		else
			throw ConfigException("invalid size suffix: " + std::string(end));
	}
	return size;
}

std::string takeValue(TokenStream& tokens, const std::string& directive) {
	std::string value = tokens.next();
	if (value == "{" || value == "}" || value == ";")
		throw ConfigException(directive + ": missing value");
	return value;
}

void consumeSemicolon(TokenStream& tokens) {
	if (!tokens.end() && tokens.peek() == ";")
		tokens.next();
}

void parseLocation(TokenStream& tokens, Location& loc) {
	if (tokens.next() != "{")
		throw ConfigException("location: expected '{'");

	while (!tokens.end()) {
		std::string directive = tokens.next();
		if (directive == "}")
			return;
		if (directive == "methods") {
			loc.methods.clear();
			loc.methodsExplicit = true;
			while (!tokens.end() && tokens.peek() != ";" && tokens.peek() != "}")
				loc.methods.push_back(takeValue(tokens, "methods"));
		} else if (directive == "redirect") {
			std::string code = takeValue(tokens, "redirect");
			loc.redirectTarget = takeValue(tokens, "redirect");
			char* end = NULL;
			long value = std::strtol(code.c_str(), &end, 10);
			if (*end != '\0' || value < 300 || value > 399)
				throw ConfigException("redirect: status code must be 3xx");
			loc.redirectCode = static_cast<int>(value);
			loc.redirectSet = true;
		} else if (directive == "root") {
			loc.root = takeValue(tokens, "root");
			loc.rootSet = true;
		} else if (directive == "autoindex") {
			std::string value = takeValue(tokens, "autoindex");
			if (value != "on" && value != "off")
				throw ConfigException("autoindex: must be 'on' or 'off'");
			loc.autoindex = (value == "on");
			loc.autoindexSet = true;
		} else if (directive == "index") {
			loc.index = takeValue(tokens, "index");
			loc.indexSet = true;
		} else if (directive == "upload_path") {
			loc.uploadPath = takeValue(tokens, "upload_path");
			loc.uploadSet = true;
		} else if (directive == "session") {
			std::string value = takeValue(tokens, "session");
			if (value != "on" && value != "off")
				throw ConfigException("session: must be 'on' or 'off'");
			loc.session = (value == "on");
			loc.sessionSet = true;
		} else if (directive == "cgi_pass") {
			std::string ext = takeValue(tokens, "cgi_pass");
			std::string interpreter = takeValue(tokens, "cgi_pass");
			if (ext.empty() || ext[0] != '.')
				throw ConfigException("cgi_pass: extension must start with a dot");
			loc.cgiHandlers.push_back(std::make_pair(ext, interpreter));
		} else {
			throw ConfigException("location: unknown directive '" + directive + "'");
		}
		consumeSemicolon(tokens);
	}
	throw ConfigException("location: missing '}'");
}

void parseListen(TokenStream& tokens, ServerBlock& block) {
	std::string value = takeValue(tokens, "listen");
	std::string host = "0.0.0.0";
	std::string portStr = value;

	std::string::size_type colon = value.find(':');
	if (colon != std::string::npos) {
		host = value.substr(0, colon);
		portStr = value.substr(colon + 1);
		if (host.empty())
			host = "0.0.0.0";
		if (portStr.find(':') != std::string::npos)
			throw ConfigException("listen: IPv6 addresses are not supported");
	}

	unsigned long port = 0;
	if (!util::parseUnsignedLong(portStr, port) || port == 0 || port > 65535)
		throw ConfigException("listen: invalid port '" + portStr + "'");

	block.host = host;
	block.port = static_cast<int>(port);
}

void parseServer(TokenStream& tokens, ServerBlock& block) {
	if (tokens.next() != "{")
		throw ConfigException("server: expected '{'");

	while (!tokens.end()) {
		std::string directive = tokens.next();
		if (directive == "}")
			return;
		if (directive == "listen") {
			parseListen(tokens, block);
		} else if (directive == "server_name") {
			block.serverName = takeValue(tokens, "server_name");
		} else if (directive == "error_page") {
			std::string code = takeValue(tokens, "error_page");
			std::string file = takeValue(tokens, "error_page");
			char* end = NULL;
			long value = std::strtol(code.c_str(), &end, 10);
			if (*end != '\0' || value < 100 || value > 599)
				throw ConfigException("error_page: invalid status code '" + code + "'");
			block.errorPages[static_cast<int>(value)] = file;
		} else if (directive == "client_max_body_size") {
			block.clientMaxBodySize = parseSize(takeValue(tokens, "client_max_body_size"));
		} else if (directive == "location") {
			std::string path = takeValue(tokens, "location");
			if (path.empty() || path[0] != '/')
				throw ConfigException("location: path must start with '/'");
			Location loc(path);
			parseLocation(tokens, loc);
			block.locations.push_back(loc);
		} else {
			throw ConfigException("server: unknown directive '" + directive + "'");
		}
		consumeSemicolon(tokens);
	}
	throw ConfigException("server: missing '}'");
}

} // namespace

Config::Config() {}

Config::Config(const std::string& path) {
	parseFile(path);
}

void Config::parseFile(const std::string& path) {
	std::string content = readFile(path);
	TokenStream tokens(content);

	while (!tokens.end()) {
		std::string keyword = tokens.next();
		if (keyword != "server")
			throw ConfigException("expected 'server' block, found '" + keyword + "'");
		ServerBlock block;
		parseServer(tokens, block);
		serverBlocks.push_back(block);
	}

	if (serverBlocks.empty())
		throw ConfigException("no server block found in configuration file");

	for (size_t i = 0; i < serverBlocks.size(); ++i) {
		for (size_t j = i + 1; j < serverBlocks.size(); ++j) {
			if (serverBlocks[i].host == serverBlocks[j].host &&
			    serverBlocks[i].port == serverBlocks[j].port)
				throw ConfigException("duplicate listen on " + serverBlocks[i].host + ":" +
				                      util::intToString(serverBlocks[i].port));
		}
	}
}

const ServerBlock* Config::findByPort(int port) const {
	for (size_t i = 0; i < serverBlocks.size(); ++i) {
		if (serverBlocks[i].port == port)
			return &serverBlocks[i];
	}
	return NULL;
}