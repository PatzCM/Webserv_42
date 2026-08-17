#include "ServerBlock.hpp"
#include "Utils.hpp"

ServerBlock::ServerBlock() : host("0.0.0.0"), port(8080), serverName("webserv"),
	clientMaxBodySize(1024 * 1024) {

	Location root("/");
	root.rootSet = true;
	root.root = "www";
	root.indexSet = true;
	root.index = "index.html";
	locations.push_back(root);
}

const Location* ServerBlock::matchLocation(const std::string& uri) const {
	const Location* best = NULL;
	size_t bestLen = 0;

	for (size_t i = 0; i < locations.size(); ++i) {
		const Location& loc = locations[i];
		size_t len = loc.path.size();
		if (len < bestLen)
			continue;
		if (!util::startsWith(uri, loc.path))
			continue;
		if (len > 1 && uri.size() > len && uri[len] != '/')
			continue;
		best = &loc;
		bestLen = len;
	}
	return best;
}

std::string ServerBlock::errorPageFor(int code) const {
	std::map<int, std::string>::const_iterator it = errorPages.find(code);
	if (it == errorPages.end())
		return "";
	if (!util::fileExists(it->second))
		return "";
	return it->second;
}