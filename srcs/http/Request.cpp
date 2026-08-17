#include "Request.hpp"
#include "Utils.hpp"

#include <cstdlib>

namespace {
	const size_t kMaxHeaderBytes = 64 * 1024;
}

Request::Request() : _state(HEADERS), _maxBody(0), _remain(0), _chunkRemain(0),
	_bodyDeclared(false) {}

void Request::reset(unsigned long maxBody) {
	method.clear();
	version.clear();
	path.clear();
	query.clear();
	body.clear();
	headers.clear();
	_state = HEADERS;
	_buffer.clear();
	_remain = 0;
	_chunkRemain = 0;
	_bodyDeclared = false;
	_maxBody = maxBody;
}

bool Request::hadBody() const {
	return _bodyDeclared;
}

Request::Status Request::feed(const char* data, size_t size) {
	_buffer.append(data, size);

	Status status = INCOMPLETE;
	if (_state == HEADERS)
		status = processHeaders();
	if (status == INCOMPLETE) {
		if (_state == LENGTH)
			status = processLengthBody();
		else if (_state == CHUNK_SIZE || _state == CHUNK_DATA || _state == TRAILERS)
			status = processChunked();
	}
	if (_state == DONE)
		status = COMPLETE;
	return status;
}

Request::Status Request::processHeaders() {
	std::string::size_type end = _buffer.find("\r\n\r\n");
	if (end == std::string::npos)
		return _buffer.size() > kMaxHeaderBytes ? BAD_REQUEST : INCOMPLETE;

	std::string head = _buffer.substr(0, end);
	_buffer.erase(0, end + 4);

	Status status = parseHeaderLines(head);
	if (status == INCOMPLETE) {
		if (_state == LENGTH)
			status = processLengthBody();
		else if (_state == CHUNK_SIZE || _state == CHUNK_DATA || _state == TRAILERS)
			status = processChunked();
	}
	if (_state == DONE)
		return COMPLETE;
	return status;
}

Request::Status Request::parseRequestLine(const std::string& line) {
	std::vector<std::string> parts = util::split(line, ' ');
	std::vector<std::string> filtered;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (!parts[i].empty())
			filtered.push_back(parts[i]);
	}
	if (filtered.size() != 3)
		return BAD_REQUEST;

	method = filtered[0];
	std::string target = filtered[1];

	if (!util::startsWith(filtered[2], "HTTP/"))
		return BAD_REQUEST;
	version = filtered[2];
	std::string ver = version.substr(5);
	if (ver != "1.1" && ver != "1.0")
		return BAD_VERSION;

	if (target.empty() || target[0] != '/')
		return BAD_REQUEST;

	std::string::size_type qmark = target.find('?');
	if (qmark != std::string::npos) {
		path = target.substr(0, qmark);
		query = target.substr(qmark + 1);
	} else {
		path = target;
		query.clear();
	}

	std::string decoded = util::percentDecode(path);
	if (!util::normalizePath(decoded, path))
		return BAD_REQUEST;
	return INCOMPLETE;
}

Request::Status Request::parseHeaderLines(const std::string& head) {
	std::vector<std::string> lines = util::split(head, '\n');
	if (lines.empty())
		return BAD_REQUEST;

	for (size_t i = 0; i < lines.size(); ++i) {
		if (!lines[i].empty() && lines[i][lines[i].size() - 1] == '\r')
			lines[i].erase(lines[i].size() - 1);
	}

	Status status = parseRequestLine(lines[0]);
	if (status != INCOMPLETE)
		return status;

	bool contentLengthSeen = false;
	for (size_t i = 1; i < lines.size(); ++i) {
		std::string line = util::trim(lines[i]);
		if (line.empty())
			continue;
		if (line[0] == ' ' || line[0] == '\t')
			continue;

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos)
			return BAD_REQUEST;
		std::string key = line.substr(0, colon);
		std::string value = util::trim(line.substr(colon + 1));
		if (key.empty() || key.find(' ') != std::string::npos)
			return BAD_REQUEST;

		key = util::toLower(key);
		if (key == "content-length") {
			if (contentLengthSeen)
				return BAD_REQUEST;
			unsigned long declared = 0;
			if (!util::parseUnsignedLong(value, declared))
				return BAD_REQUEST;
			_remain = declared;
			_bodyDeclared = true;
			contentLengthSeen = true;
		}
		headers[key] = value;
	}

	if (version == "HTTP/1.1" && headers.find("host") == headers.end())
		return BAD_REQUEST;

	std::string te = util::toLower(getHeader("transfer-encoding"));
	if (te.find("chunked") != std::string::npos) {
		_state = CHUNK_SIZE;
		_bodyDeclared = true;
		_remain = 0;
		return INCOMPLETE;
	}

	if (_bodyDeclared) {
		if (_remain > _maxBody)
			return TOO_LARGE;
		_state = LENGTH;
		return INCOMPLETE;
	}

	_state = DONE;
	return COMPLETE;
}

Request::Status Request::processLengthBody() {
	if (_buffer.size() < _remain)
		return INCOMPLETE;
	if (_remain > _maxBody)
		return TOO_LARGE;
	body = _buffer.substr(0, _remain);
	_remain = 0;
	_state = DONE;
	return COMPLETE;
}

Request::Status Request::processChunked() {
	while (true) {
		if (_state == CHUNK_SIZE) {
			std::string::size_type lineEnd = _buffer.find("\r\n");
			if (lineEnd == std::string::npos)
				return INCOMPLETE;

			std::string line = _buffer.substr(0, lineEnd);
			std::string::size_type ext = line.find(';');
			if (ext != std::string::npos)
				line = line.substr(0, ext);
			line = util::trim(line);

			char* end = NULL;
			unsigned long size = std::strtoul(line.c_str(), &end, 16);
			if (end == line.c_str())
				return BAD_REQUEST;

			_buffer.erase(0, lineEnd + 2);
			_chunkRemain = size;
			if (size == 0) {
				_state = TRAILERS;
			} else {
				if (body.size() + size > _maxBody)
					return TOO_LARGE;
				_state = CHUNK_DATA;
			}
		} else if (_state == CHUNK_DATA) {
			if (_buffer.size() < _chunkRemain + 2)
				return INCOMPLETE;
			body.append(_buffer, 0, _chunkRemain);
			_buffer.erase(0, _chunkRemain + 2);
			_state = CHUNK_SIZE;
		} else if (_state == TRAILERS) {
			if (_buffer.empty())
				return INCOMPLETE;
			if (util::startsWith(_buffer, "\r\n")) {
				_buffer.erase(0, 2);
				_state = DONE;
				return COMPLETE;
			}
			std::string::size_type lineEnd = _buffer.find("\r\n");
			if (lineEnd == std::string::npos)
				return INCOMPLETE;
			_buffer.erase(0, lineEnd + 2);
		}
	}
}

const std::string& Request::getHeader(const std::string& key) const {
	static const std::string empty;
	std::map<std::string, std::string>::const_iterator it = headers.find(util::toLower(key));
	if (it == headers.end())
		return empty;
	return it->second;
}

std::string Request::getCookie(const std::string& name) const {
	std::string cookieHeader = getHeader("cookie");
	std::vector<std::string> pairs = util::split(cookieHeader, ';');
	for (size_t i = 0; i < pairs.size(); ++i) {
		std::string pair = util::trim(pairs[i]);
		std::string::size_type eq = pair.find('=');
		if (eq == std::string::npos)
			continue;
		if (util::trim(pair.substr(0, eq)) == name)
			return util::trim(pair.substr(eq + 1));
	}
	return "";
}