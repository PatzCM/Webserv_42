#ifndef REQUEST_HPP
#define REQUEST_HPP

#include <map>
#include <string>

class Request {
public:
	enum State {
		HEADERS,      // waiting for the end of the header block
		LENGTH,       // reading a Content-Length body
		CHUNK_SIZE,   // reading a chunk size line
		CHUNK_DATA,   // reading chunk payload
		TRAILERS,     // reading trailers after the last chunk
		DONE
	};

	enum Status {
		INCOMPLETE,   // need more bytes
		COMPLETE,     // request fully parsed
		TOO_LARGE,    // body exceeds client_max_body_size
		BAD_REQUEST,  // malformed request
		BAD_VERSION   // unsupported HTTP version
	};

	Request();

	std::string method;
	std::string version;
	std::string path;
	std::string query;
	std::string body;
	std::map<std::string, std::string> headers;

	void reset(unsigned long maxBody);
	Status feed(const char* data, size_t size);
	bool hadBody() const;
	const std::string& getHeader(const std::string& key) const;
	std::string getCookie(const std::string& name) const;

private:
	State _state;
	std::string _buffer;
	unsigned long _maxBody;
	unsigned long _remain;
	unsigned long _chunkRemain;
	bool _bodyDeclared;

	Status processHeaders();
	Status processLengthBody();
	Status processChunked();
	Status parseRequestLine(const std::string& line);
	Status parseHeaderLines(const std::string& head);
};

#endif