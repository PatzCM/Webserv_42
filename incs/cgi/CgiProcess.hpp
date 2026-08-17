#ifndef CGIPROCESS_HPP
#define CGIPROCESS_HPP

#include <sys/types.h>

#include <string>

#include "Request.hpp"

class CgiProcess {
public:
	CgiProcess();
	~CgiProcess();

	bool start(const std::string& interpreter, const std::string& scriptPath,
	           const std::string& workingDir, const Request& request,
	           const std::string& serverName, const std::string& serverPort,
	           const std::string& remoteAddr);

	bool running() const;
	int inputFd() const;
	int outputFd() const;
	void closeInput();
	void closeOutput();
	void killChild();
	void reap();

private:
	pid_t _pid;
	int _pipeIn[2];
	int _pipeOut[2];
	bool _inputClosed;
	bool _outputClosed;

	char** buildEnv(const Request& request, const std::string& scriptPath,
	                const std::string& workingDir, const std::string& serverName,
	                const std::string& serverPort, const std::string& remoteAddr) const;
	void freeEnv(char** envp) const;

	CgiProcess(const CgiProcess&);
	CgiProcess& operator=(const CgiProcess&);
};

#endif