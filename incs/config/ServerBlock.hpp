#ifndef SERVERBLOCK_HPP
#define SERVERBLOCK_HPP

#include <map>
#include <string>
#include <vector>

#include "Location.hpp"

class ServerBlock {
public:
	ServerBlock();

	std::string host;
	int port;

	std::string serverName;
	unsigned long clientMaxBodySize;

	std::map<int, std::string> errorPages;
	std::vector<Location> locations;

	const Location* matchLocation(const std::string& uri) const;
	std::string errorPageFor(int code) const;
};

#endif