#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <stdexcept>
#include <string>
#include <vector>

#include "ServerBlock.hpp"

class ConfigException : public std::runtime_error {
public:
	explicit ConfigException(const std::string& msg) : std::runtime_error(msg) {}
};

class Config {
public:
	Config();
	explicit Config(const std::string& path);

	std::vector<ServerBlock> serverBlocks;

	const ServerBlock* findByPort(int port) const;

private:
	void parseFile(const std::string& path);
};

#endif