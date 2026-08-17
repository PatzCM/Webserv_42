#include "Client.hpp"

Client::Client() : _fd(-1), _port(0), _state(READING), _bytesSent(0), _cgi(NULL),
	_cgiDeadline(0), _cgiTimedOut(false), _cgiBodySent(0), _lastActivity(0) {}

void Client::init(int fd, int port, unsigned long maxBody, const std::string& remoteAddr) {
	_fd = fd;
	_port = port;
	_remoteAddr = remoteAddr;
	_state = READING;
	_bytesSent = 0;
	_cgi = NULL;
	_cgiDeadline = 0;
	_cgiTimedOut = false;
	_cgiBodySent = 0;
	_request.reset(maxBody);
	_writeBuffer.clear();
	_cgiOutput.clear();
	_lastActivity = std::time(NULL);
}

int Client::fd() const {
	return _fd;
}

int Client::port() const {
	return _port;
}

const std::string& Client::remoteAddr() const {
	return _remoteAddr;
}

Client::State Client::state() const {
	return _state;
}

void Client::setState(State state) {
	_state = state;
}

Request& Client::request() {
	return _request;
}

const Request& Client::request() const {
	return _request;
}

std::string& Client::writeBuffer() {
	return _writeBuffer;
}

const std::string& Client::writeBuffer() const {
	return _writeBuffer;
}

void Client::setWriteBuffer(const std::string& data) {
	_writeBuffer = data;
	_bytesSent = 0;
}

size_t Client::bytesSent() const {
	return _bytesSent;
}

void Client::setBytesSent(size_t size) {
	_bytesSent = size;
}

bool Client::hasCgi() const {
	return _cgi != NULL;
}

CgiProcess& Client::cgi() {
	return *_cgi;
}

void Client::clearCgi() {
	delete _cgi;
	_cgi = NULL;
}

bool Client::beginCgi(const std::string& interpreter, const std::string& scriptPath,
                      const std::string& workingDir, const Request& request,
                      const std::string& serverName, const std::string& serverPort) {
	_cgi = new CgiProcess();
	if (!_cgi->start(interpreter, scriptPath, workingDir, request, serverName,
	                 serverPort, _remoteAddr)) {
		delete _cgi;
		_cgi = NULL;
		return false;
	}
	_cgiBodySent = 0;
	_cgiOutput.clear();
	_cgiTimedOut = false;
	_cgiDeadline = 0;
	return true;
}

time_t Client::cgiDeadline() const {
	return _cgiDeadline;
}

void Client::setCgiDeadline(time_t deadline) {
	_cgiDeadline = deadline;
}

bool Client::cgiTimedOut() const {
	return _cgiTimedOut;
}

void Client::setCgiTimedOut() {
	_cgiTimedOut = true;
}

size_t Client::cgiBodySent() const {
	return _cgiBodySent;
}

void Client::addCgiBodySent(size_t size) {
	_cgiBodySent += size;
}

std::string& Client::cgiOutput() {
	return _cgiOutput;
}

const std::string& Client::cgiOutput() const {
	return _cgiOutput;
}

time_t Client::lastActivity() const {
	return _lastActivity;
}

void Client::touch() {
	_lastActivity = std::time(NULL);
}