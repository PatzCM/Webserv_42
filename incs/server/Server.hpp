#ifndef SERVER_HPP
#define SERVER_HPP

#include <poll.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "Client.hpp"
#include "Config.hpp"
#include "Response.hpp"

class ServerException : public std::runtime_error {
public:
	explicit ServerException(const std::string& msg) : std::runtime_error(msg) {}
};

class Server {
public:
	explicit Server(const Config& config);
	~Server();

	void setup();
	void run();

private:
	static const int kIdleTimeout = 30;
	static const int kCgiTimeout = 30;

	const Config& _config;
	std::map<int, int> _listen;       // listen socket fd -> port
	std::map<int, Client> _clients;   // client socket fd -> Client
	std::map<int, int> _cgiFds;       // cgi pipe fd -> client fd
	std::vector<struct pollfd> _pollfds;

	int createListenSocket(const std::string& host, int port) const;
	void rebuildPollfds();
	void acceptClients(int listenFd);
	void readFromClient(int fd);
	void writeToClient(int fd);
	void writeToCgi(int clientFd);
	void readFromCgi(int clientFd);
	void finishCgi(int clientFd);
	void handleRequest(int clientFd);
	void removeClient(int fd);
	void respond(int clientFd, const Response& response);
	void checkTimeouts();
	const ServerBlock* blockFor(int port) const;
};

#endif