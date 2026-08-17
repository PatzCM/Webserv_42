#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <ctime>
#include <string>

#include "CgiProcess.hpp"
#include "Request.hpp"

class Client {
public:
	enum State {
		READING,   // reading the request from the socket
		WRITING,   // sending the response to the socket
		CGI_WRITE, // forwarding the request body to the CGI
		CGI_READ   // reading the CGI output
	};

	Client();
	void init(int fd, int port, unsigned long maxBody, const std::string& remoteAddr);

	int fd() const;
	int port() const;
	const std::string& remoteAddr() const;

	State state() const;
	void setState(State state);

	Request& request();
	const Request& request() const;

	std::string& writeBuffer();
	const std::string& writeBuffer() const;
	void setWriteBuffer(const std::string& data);
	size_t bytesSent() const;
	void setBytesSent(size_t size);

	bool hasCgi() const;
	CgiProcess& cgi();
	void clearCgi();
	bool beginCgi(const std::string& interpreter, const std::string& scriptPath,
	              const std::string& workingDir, const Request& request,
	              const std::string& serverName, const std::string& serverPort);
	time_t cgiDeadline() const;
	void setCgiDeadline(time_t deadline);
	bool cgiTimedOut() const;
	void setCgiTimedOut();

	size_t cgiBodySent() const;
	void addCgiBodySent(size_t size);
	std::string& cgiOutput();
	const std::string& cgiOutput() const;

	time_t lastActivity() const;
	void touch();

private:
	int _fd;
	int _port;
	std::string _remoteAddr;
	State _state;
	Request _request;
	std::string _writeBuffer;
	size_t _bytesSent;
	CgiProcess* _cgi;
	time_t _cgiDeadline;
	bool _cgiTimedOut;
	size_t _cgiBodySent;
	std::string _cgiOutput;
	time_t _lastActivity;
};

#endif