#include <cstdlib>
#include <iostream>
#include <string>

#include "Config.hpp"
#include "Server.hpp"
#include "Utils.hpp"

namespace {
	std::string defaultConfigPath() {
		if (util::fileExists("configs/default.conf"))
			return "configs/default.conf";
		return "default.conf";
	}
}

int main(int argc, char** argv) {
	try {
		if (argc > 2) {
			std::cerr << "usage: " << argv[0] << " [configuration file]\n";
			return 1;
		}

		std::string configPath = (argc == 2) ? argv[1] : defaultConfigPath();
		Config config(configPath);
		Server server(config);
		server.setup();
		server.run();

		std::cout << "webserv: shutting down\n";
	} catch (const ConfigException& e) {
		std::cerr << "webserv: configuration error: " << e.what() << "\n";
		return 1;
	} catch (const ServerException& e) {
		std::cerr << "webserv: server error: " << e.what() << "\n";
		return 1;
	} catch (const std::exception& e) {
		std::cerr << "webserv: fatal error: " << e.what() << "\n";
		return 1;
	}
	return 0;
}