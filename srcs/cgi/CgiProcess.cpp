#include "CgiProcess.hpp"
#include "Utils.hpp"

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
	std::string envKeyName(const std::string& header) {
		std::string key = "HTTP_" + header;
		for (size_t i = 0; i < key.size(); ++i) {
			if (key[i] == '-')
				key[i] = '_';
			key[i] = static_cast<char>(
				std::toupper(static_cast<unsigned char>(key[i])));
		}
		return key;
	}
}

CgiProcess::CgiProcess() : _pid(-1), _inputClosed(true), _outputClosed(true) {
	_pipeIn[0] = _pipeIn[1] = -1;
	_pipeOut[0] = _pipeOut[1] = -1;
}

CgiProcess::~CgiProcess() {
	killChild();
	closeInput();
	closeOutput();
	reap();
}

bool CgiProcess::running() const {
	return _pid > 0;
}

int CgiProcess::inputFd() const {
	return _pipeIn[1];
}

int CgiProcess::outputFd() const {
	return _pipeOut[0];
}

void CgiProcess::closeInput() {
	if (_pipeIn[1] >= 0) {
		::close(_pipeIn[1]);
		_pipeIn[1] = -1;
	}
	_inputClosed = true;
}

void CgiProcess::closeOutput() {
	if (_pipeOut[0] >= 0) {
		::close(_pipeOut[0]);
		_pipeOut[0] = -1;
	}
	_outputClosed = true;
}

void CgiProcess::killChild() {
	if (_pid > 0) {
		kill(_pid, SIGKILL);
		reap();
	}
}

void CgiProcess::reap() {
	if (_pid > 0) {
		int status = 0;
		pid_t result = waitpid(_pid, &status, WNOHANG);
		if (result == _pid)
			_pid = -1;
	}
}

char** CgiProcess::buildEnv(const Request& request, const std::string& scriptPath,
                            const std::string& workingDir, const std::string& serverName,
                            const std::string& serverPort, const std::string& remoteAddr) const {
	std::map<std::string, std::string> env;
	env["REQUEST_METHOD"] = request.method;
	env["QUERY_STRING"] = request.query;
	env["CONTENT_TYPE"] = request.getHeader("content-type");
	env["CONTENT_LENGTH"] = util::intToString(static_cast<long>(request.body.size()));
	env["SCRIPT_FILENAME"] = scriptPath;
	env["SCRIPT_NAME"] = request.path;
	env["PATH_INFO"] = "";
	env["PATH_TRANSLATED"] = scriptPath;
	env["GATEWAY_INTERFACE"] = "CGI/1.1";
	env["SERVER_PROTOCOL"] = request.version;
	env["SERVER_SOFTWARE"] = "webserv";
	env["SERVER_NAME"] = serverName;
	env["SERVER_PORT"] = serverPort;
	env["REMOTE_ADDR"] = remoteAddr;
	env["DOCUMENT_ROOT"] = workingDir;
	env["REDIRECT_STATUS"] = "200";
	env["PWD"] = workingDir;

	for (std::map<std::string, std::string>::const_iterator it = request.headers.begin();
	     it != request.headers.end(); ++it) {
		env[envKeyName(it->first)] = it->second;
	}

	char** envp = new char*[env.size() + 1];
	size_t i = 0;
	for (std::map<std::string, std::string>::const_iterator it = env.begin();
	     it != env.end(); ++it) {
		std::string line = it->first + "=" + it->second;
		envp[i] = new char[line.size() + 1];
		std::strcpy(envp[i], line.c_str());
		++i;
	}
	envp[i] = NULL;
	return envp;
}

void CgiProcess::freeEnv(char** envp) const {
	for (size_t i = 0; envp[i] != NULL; ++i)
		delete[] envp[i];
	delete[] envp;
}

bool CgiProcess::start(const std::string& interpreter, const std::string& scriptPath,
                       const std::string& workingDir, const Request& request,
                       const std::string& serverName, const std::string& serverPort,
                       const std::string& remoteAddr) {
	if (pipe(_pipeIn) < 0 || pipe(_pipeOut) < 0)
		return false;
	_inputClosed = false;
	_outputClosed = false;
	util::setCloseOnExec(_pipeIn[0]);
	util::setCloseOnExec(_pipeIn[1]);
	util::setCloseOnExec(_pipeOut[0]);
	util::setCloseOnExec(_pipeOut[1]);

	char** envp = buildEnv(request, scriptPath, workingDir, serverName, serverPort, remoteAddr);

	_pid = fork();
	if (_pid < 0) {
		freeEnv(envp);
		closeInput();
		closeOutput();
		return false;
	}

	if (_pid == 0) {
		dup2(_pipeIn[0], STDIN_FILENO);
		dup2(_pipeOut[1], STDOUT_FILENO);
		::close(_pipeIn[0]);
		::close(_pipeIn[1]);
		::close(_pipeOut[0]);
		::close(_pipeOut[1]);
		if (!workingDir.empty())
			chdir(workingDir.c_str());
		char* const args[] = {
			const_cast<char*>(interpreter.c_str()),
			const_cast<char*>(scriptPath.c_str()),
			NULL
		};
		execve(interpreter.c_str(), args, envp);
		std::exit(127);
	}

	freeEnv(envp);
	::close(_pipeIn[0]);
	::close(_pipeOut[1]);
	_pipeIn[0] = -1;
	_pipeOut[1] = -1;
	util::setNonBlocking(_pipeIn[1]);
	util::setNonBlocking(_pipeOut[0]);
	return true;
}