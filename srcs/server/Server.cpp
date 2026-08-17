#include "Server.hpp"
#include "RequestHandler.hpp"
#include "Utils.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace {
	volatile sig_atomic_t g_stop = 0;

	void stopHandler(int) {
		g_stop = 1;
	}
}

Server::Server(const Config& config) : _config(config) {}

Server::~Server() {
	for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		close(it->first);
	for (std::map<int, int>::iterator it = _listen.begin(); it != _listen.end(); ++it)
		close(it->first);
}

const ServerBlock* Server::blockFor(int port) const {
	return _config.findByPort(port);
}

int Server::createListenSocket(const std::string& host, int port) const {
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo* result = NULL;
	std::string portStr = util::intToString(port);
	if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0)
		return -1;

	int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(result);
		return -1;
	}

	int opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (bind(fd, result->ai_addr, result->ai_addrlen) < 0) {
		close(fd);
		freeaddrinfo(result);
		return -1;
	}
	freeaddrinfo(result);

	if (listen(fd, 128) < 0) {
		close(fd);
		return -1;
	}
	util::setNonBlocking(fd);
	return fd;
}

void Server::setup() {
	for (size_t i = 0; i < _config.serverBlocks.size(); ++i) {
		const ServerBlock& block = _config.serverBlocks[i];
		std::string key = block.host + ":" + util::intToString(block.port);

		int fd = createListenSocket(block.host, block.port);
		if (fd < 0)
			throw ServerException("cannot listen on " + key + ": " + std::strerror(errno));
		_listen[fd] = block.port;
	}

	if (_listen.empty())
		throw ServerException("no listening sockets");

	for (std::map<int, int>::const_iterator it = _listen.begin(); it != _listen.end(); ++it)
		std::cout << "webserv: listening on " << it->second << "\n";
}

void Server::rebuildPollfds() {
	_pollfds.clear();
	_cgiFds.clear();

	for (std::map<int, int>::const_iterator it = _listen.begin(); it != _listen.end(); ++it) {
		struct pollfd pfd;
		pfd.fd = it->first;
		pfd.events = POLLIN;
		pfd.revents = 0;
		_pollfds.push_back(pfd);
	}

	for (std::map<int, Client>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
		Client& client = it->second;
		int clientFd = it->first;

		if (client.state() == Client::READING) {
			struct pollfd pfd;
			pfd.fd = clientFd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			_pollfds.push_back(pfd);
		} else if (client.state() == Client::WRITING) {
			struct pollfd pfd;
			pfd.fd = clientFd;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			_pollfds.push_back(pfd);
		} else if (client.state() == Client::CGI_WRITE && client.hasCgi()) {
			int cgiFd = client.cgi().inputFd();
			if (cgiFd >= 0) {
				struct pollfd pfd;
				pfd.fd = cgiFd;
				pfd.events = POLLOUT;
				pfd.revents = 0;
				_pollfds.push_back(pfd);
				_cgiFds[cgiFd] = clientFd;
			}
		} else if (client.state() == Client::CGI_READ && client.hasCgi()) {
			int cgiFd = client.cgi().outputFd();
			if (cgiFd >= 0) {
				struct pollfd pfd;
				pfd.fd = cgiFd;
				pfd.events = POLLIN;
				pfd.revents = 0;
				_pollfds.push_back(pfd);
				_cgiFds[cgiFd] = clientFd;
			}
		}
	}
}

void Server::acceptClients(int listenFd) {
	while (true) {
		struct sockaddr_in addr;
		socklen_t addrLen = sizeof(addr);
		int clientFd = accept(listenFd, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
		if (clientFd < 0)
			break;

		util::setNonBlocking(clientFd);
		const ServerBlock* block = blockFor(_listen[listenFd]);
		unsigned long maxBody = block ? block->clientMaxBodySize : 1024 * 1024;

		Client& client = _clients[clientFd];
		client.init(clientFd, _listen[listenFd], maxBody, inet_ntoa(addr.sin_addr));
	}
}

void Server::handleRequest(int clientFd) {
	Client& client = _clients[clientFd];
	try {
		RequestHandler::handle(client, _config);
	} catch (const std::exception&) {
		respond(clientFd, Response::fromError(500, blockFor(client.port())));
	}
}

void Server::readFromClient(int fd) {
	if (_clients.find(fd) == _clients.end())
		return;
	Client& client = _clients[fd];

	char buffer[8192];
	while (true) {
		ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
		if (received > 0) {
			client.touch();
			Request::Status status = client.request().feed(buffer, static_cast<size_t>(received));
			if (status == Request::COMPLETE) {
				handleRequest(fd);
				return;
			}
			if (status == Request::BAD_REQUEST) {
				respond(fd, Response::fromError(400, blockFor(client.port())));
				return;
			}
			if (status == Request::TOO_LARGE) {
				respond(fd, Response::fromError(413, blockFor(client.port())));
				return;
			}
			if (status == Request::BAD_VERSION) {
				respond(fd, Response::fromError(505, blockFor(client.port())));
				return;
			}
			continue;
		}
		if (received == 0) {
			removeClient(fd);
			return;
		}
		return;
	}
}

void Server::writeToClient(int fd) {
	if (_clients.find(fd) == _clients.end())
		return;
	Client& client = _clients[fd];
	const std::string& buffer = client.writeBuffer();
	size_t offset = client.bytesSent();

	while (offset < buffer.size()) {
		ssize_t sent = send(fd, buffer.c_str() + offset, buffer.size() - offset, 0);
		if (sent <= 0)
			break;
		offset += static_cast<size_t>(sent);
	}
	client.setBytesSent(offset);
	if (offset >= buffer.size())
		removeClient(fd);
}

void Server::writeToCgi(int clientFd) {
	if (_clients.find(clientFd) == _clients.end())
		return;
	Client& client = _clients[clientFd];
	if (!client.hasCgi())
		return;

	const std::string& body = client.request().body;
	size_t offset = client.cgiBodySent();

	while (offset < body.size()) {
		ssize_t written = write(client.cgi().inputFd(), body.c_str() + offset, body.size() - offset);
		if (written <= 0)
			break;
		offset += static_cast<size_t>(written);
	}
	client.addCgiBodySent(offset - client.cgiBodySent());

	if (offset >= body.size()) {
		client.cgi().closeInput();
		client.setState(Client::CGI_READ);
	}
}

void Server::readFromCgi(int clientFd) {
	if (_clients.find(clientFd) == _clients.end())
		return;
	Client& client = _clients[clientFd];
	if (!client.hasCgi())
		return;

	char buffer[8192];
	while (true) {
		ssize_t received = read(client.cgi().outputFd(), buffer, sizeof(buffer));
		if (received > 0) {
			client.cgiOutput().append(buffer, static_cast<size_t>(received));
			client.touch();
			continue;
		}
		if (received == 0) {
			finishCgi(clientFd);
			return;
		}
		return;
	}
}

void Server::finishCgi(int clientFd) {
	if (_clients.find(clientFd) == _clients.end())
		return;
	Client& client = _clients[clientFd];
	if (!client.hasCgi())
		return;

	client.cgi().closeOutput();
	client.cgi().reap();

	Response response;
	if (client.cgiTimedOut()) {
		response = Response::fromError(504, blockFor(client.port()));
	} else if (client.cgiOutput().empty()) {
		response = Response::fromError(502, blockFor(client.port()));
	} else {
		response = Response::fromCgi(client.cgiOutput());
	}

	client.clearCgi();
	respond(clientFd, response);
}

void Server::removeClient(int fd) {
	std::map<int, Client>::iterator it = _clients.find(fd);
	if (it != _clients.end()) {
		it->second.clearCgi();
		_clients.erase(it);
	}
	close(fd);
}

void Server::respond(int clientFd, const Response& response) {
	if (_clients.find(clientFd) == _clients.end())
		return;
	Client& client = _clients[clientFd];

	std::string version = client.request().version;
	if (!util::startsWith(version, "HTTP/1."))
		version = "HTTP/1.1";

	client.setWriteBuffer(response.serialize(version));
	client.setState(Client::WRITING);
	client.touch();
}

void Server::checkTimeouts() {
	time_t now = std::time(NULL);
	std::map<int, Client>::iterator it = _clients.begin();
	while (it != _clients.end()) {
		int clientFd = it->first;
		Client& client = it->second;

		if (client.state() == Client::CGI_WRITE || client.state() == Client::CGI_READ) {
			if (now > client.cgiDeadline()) {
				client.setCgiTimedOut();
				if (client.hasCgi())
					client.cgi().killChild();
				finishCgi(clientFd);
			}
		} else if (now - client.lastActivity() > kIdleTimeout) {
			removeClient(clientFd);
		}

		if (_clients.find(clientFd) == _clients.end())
			it = _clients.begin();
		else
			++it;
	}
}

void Server::run() {
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, stopHandler);
	signal(SIGTERM, stopHandler);

	while (!g_stop) {
		rebuildPollfds();

		int ready = poll(&_pollfds[0], _pollfds.size(), 500);
		if (ready > 0) {
			for (size_t i = 0; i < _pollfds.size(); ++i) {
				const struct pollfd& pfd = _pollfds[i];
				short revents = pfd.revents;
				if (revents == 0)
					continue;
				int fd = pfd.fd;

				if (_listen.find(fd) != _listen.end()) {
					if (revents & POLLIN)
						acceptClients(fd);
					continue;
				}

				if (_cgiFds.find(fd) != _cgiFds.end()) {
					int clientFd = _cgiFds[fd];
					if (_clients.find(clientFd) == _clients.end())
						continue;
					Client& client = _clients[clientFd];

					if (revents & POLLIN)
						readFromCgi(clientFd);
					if (revents & POLLOUT)
						writeToCgi(clientFd);
					if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
						if (_clients.find(clientFd) == _clients.end())
							continue;
						if (client.state() == Client::CGI_WRITE) {
							client.cgi().closeInput();
							client.setState(Client::CGI_READ);
						} else if (client.state() == Client::CGI_READ) {
							finishCgi(clientFd);
						}
					}
					continue;
				}

				if (_clients.find(fd) == _clients.end())
					continue;

				if (revents & POLLIN)
					readFromClient(fd);
				if (revents & POLLOUT) {
					if (_clients.find(fd) != _clients.end() &&
					    _clients[fd].state() == Client::WRITING)
						writeToClient(fd);
				}
				if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
					if (_clients.find(fd) != _clients.end())
						removeClient(fd);
				}
			}
		}
		checkTimeouts();
	}
}