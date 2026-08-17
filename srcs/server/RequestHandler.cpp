#include "RequestHandler.hpp"
#include "Utils.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace {
	const int kCgiTimeoutSeconds = 30;

	std::string queryValue(const std::string& query, const std::string& key) {
		std::vector<std::string> pairs = util::split(query, '&');
		for (size_t i = 0; i < pairs.size(); ++i) {
			std::string::size_type eq = pairs[i].find('=');
			std::string name = util::percentDecode(pairs[i].substr(0, eq));
			if (name == key) {
				if (eq == std::string::npos)
					return "";
				return util::percentDecode(pairs[i].substr(eq + 1));
			}
		}
		return "";
	}

	bool writeFile(const std::string& path, const std::string& data) {
		std::ofstream file(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
		if (!file)
			return false;
		file.write(data.c_str(), static_cast<std::streamsize>(data.size()));
		return file.good();
	}

	std::string randomSessionId() {
		unsigned int seed = static_cast<unsigned int>(std::time(NULL) ^ getpid());
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%08x%08x",
		              static_cast<unsigned int>(std::rand()) ^ seed,
		              static_cast<unsigned int>(std::rand()) ^ (seed >> 16));
		return std::string(buffer);
	}
}

void RequestHandler::respond(Client& client, const Response& response) {
	std::string version = client.request().version;
	if (!util::startsWith(version, "HTTP/1."))
		version = "HTTP/1.1";
	client.setWriteBuffer(response.serialize(version));
	client.setState(Client::WRITING);
	client.touch();
}

std::string RequestHandler::resolve(const Location* loc, const std::string& uri) {
	std::string relative = loc->stripPrefix(uri);
	if (relative.empty() || relative[0] != '/')
		relative = "/" + relative;
	return util::join(loc->root, relative);
}

void RequestHandler::handle(Client& client, const Config& config) {
	const ServerBlock* block = config.findByPort(client.port());
	if (block == NULL)
		throw std::runtime_error("no server block for port");

	const Request& request = client.request();
	const Location* loc = block->matchLocation(request.path);
	if (loc == NULL)
		throw std::runtime_error("no location matched");

	if (loc->sessionSet && loc->session) {
		if (request.method == "GET") {
			serveSession(client, block, loc);
			return;
		}
		Response res = Response::fromError(405, block);
		res.setHeader("Allow", "GET");
		respond(client, res);
		return;
	}

	if (loc->redirectSet) {
		respond(client, Response::fromRedirect(loc->redirectCode, loc->redirectTarget));
		return;
	}

	if (!loc->methodAllowed(request.method)) {
		Response res = Response::fromError(405, block);
		res.setHeader("Allow", loc->allowedMethods());
		respond(client, res);
		return;
	}

	if (request.method == "GET")
		serveGet(client, block, loc);
	else if (request.method == "POST")
		servePost(client, block, loc);
	else if (request.method == "DELETE")
		serveDelete(client, block, loc);
	else {
		Response res = Response::fromError(501, block);
		res.setHeader("Allow", loc->allowedMethods());
		respond(client, res);
	}
}

void RequestHandler::serveGet(Client& client, const ServerBlock* block, const Location* loc) {
	const std::string& uri = client.request().path;
	std::string path = resolve(loc, uri);

	std::string interpreter;
	if (!loc->cgiHandlers.empty() && loc->cgiInterpreter(uri, interpreter)) {
		if (!util::isRegularFile(path))
			respond(client, Response::fromError(404, block,
			        "CGI script not found: " + util::htmlEscape(uri)));
		else
			startCgi(client, block, loc, path, interpreter);
		return;
	}

	if (!util::fileExists(path)) {
		respond(client, Response::fromError(404, block,
		        "no such file or directory: " + util::htmlEscape(uri)));
		return;
	}

	if (util::isDirectory(path)) {
		std::string index = util::join(path, loc->index);
		if (util::isRegularFile(index)) {
			respond(client, Response::fromFile(index, block));
			return;
		}
		if (loc->autoindex) {
			respond(client, Response::fromDirectory(path, uri));
			return;
		}
		respond(client, Response::fromError(403, block, "directory listing is disabled"));
		return;
	}

	respond(client, Response::fromFile(path, block));
}

void RequestHandler::servePost(Client& client, const ServerBlock* block, const Location* loc) {
	const std::string& uri = client.request().path;
	std::string path = resolve(loc, uri);

	std::string interpreter;
	if (!loc->cgiHandlers.empty() && loc->cgiInterpreter(uri, interpreter)) {
		if (!util::isRegularFile(path))
			respond(client, Response::fromError(404, block,
			        "CGI script not found: " + util::htmlEscape(uri)));
		else
			startCgi(client, block, loc, path, interpreter);
		return;
	}

	if (!loc->uploadSet) {
		respond(client, Response::fromError(403, block, "uploads are not allowed here"));
		return;
	}

	std::string name;
	std::string data;
	if (!extractUpload(client, name, data))
		name.clear();

	if (name.empty() || name == "." || name == "..") {
		respond(client, Response::fromError(400, block, "missing or invalid file name"));
		return;
	}

	if (!util::isDirectory(loc->uploadPath)) {
		respond(client, Response::fromError(500, block,
		        "upload directory does not exist: " + util::htmlEscape(loc->uploadPath)));
		return;
	}

	std::string destination = util::join(loc->uploadPath, name);
	if (!writeFile(destination, data)) {
		respond(client, Response::fromError(500, block, "cannot write uploaded file"));
		return;
	}
	respond(client, Response::fromCreated(name));
}

void RequestHandler::serveDelete(Client& client, const ServerBlock* block, const Location* loc) {
	std::string path = resolve(loc, client.request().path);

	if (!util::fileExists(path)) {
		respond(client, Response::fromError(404, block, "nothing to delete here"));
		return;
	}
	if (util::isDirectory(path)) {
		respond(client, Response::fromError(403, block, "cannot delete a directory"));
		return;
	}
	if (unlink(path.c_str()) != 0)
		respond(client, Response::fromError(500, block, "cannot delete the file"));
	else
		respond(client, Response::fromNoContent());
}

void RequestHandler::startCgi(Client& client, const ServerBlock* block, const Location* loc,
                              const std::string& scriptPath, const std::string& interpreter) {
	bool started = client.beginCgi(interpreter, util::absolutePath(scriptPath),
	                               util::absolutePath(loc->root),
	                               client.request(), block->serverName,
	                               util::intToString(block->port));
	if (!started) {
		respond(client, Response::fromError(500, block, "failed to launch CGI"));
		return;
	}

	client.setCgiDeadline(std::time(NULL) + kCgiTimeoutSeconds);

	if (client.request().hadBody()) {
		client.setState(Client::CGI_WRITE);
	} else {
		client.cgi().closeInput();
		client.setState(Client::CGI_READ);
	}
}

bool RequestHandler::extractUpload(const Client& client, std::string& name, std::string& data) {
	const Request& request = client.request();
	std::string contentType = request.getHeader("content-type");
	std::string raw = request.body;

	std::string::size_type namePos = raw.find("filename=\"");
	if (namePos != std::string::npos) {
		std::string::size_type start = namePos + 10;
		std::string::size_type end = raw.find('"', start);
		if (end != std::string::npos)
			name = raw.substr(start, end - start);
	}

	name = util::baseName(name);
	name = util::percentDecode(name);

	if (contentType.find("multipart/form-data") != std::string::npos) {
		std::string::size_type headerEnd = raw.find("\r\n\r\n");
		if (headerEnd != std::string::npos) {
			std::string::size_type dataEnd = raw.find("\r\n--", headerEnd + 4);
			if (dataEnd == std::string::npos)
				data = raw.substr(headerEnd + 4);
			else
				data = raw.substr(headerEnd + 4, dataEnd - headerEnd - 4);
		} else {
			data = raw;
		}
	} else {
		data = raw;
	}

	if (name.empty())
		name = queryValue(request.query, "name");
	return true;
}

void RequestHandler::serveSession(Client& client, const ServerBlock* block, const Location* loc) {
	(void)block;
	(void)loc;
	static std::map<std::string, size_t>* visits = NULL;
	if (visits == NULL)
		visits = new std::map<std::string, size_t>();

	std::string id = client.request().getCookie("session_id");
	bool fresh = false;
	if (id.empty()) {
		id = randomSessionId();
		fresh = true;
	}
	(*visits)[id] += 1;

	std::ostringstream content;
	content << "<h1>Session demo</h1>\n"
	        << "<p>Your session id: <code>" << id << "</code>"
	        << (fresh ? " (just created)" : " (existing)") << "</p>\n"
	        << "<p>This session has been seen " << (*visits)[id]
	        << " time" << ((*visits)[id] > 1 ? "s" : "") << ".</p>\n"
	        << "<p><a href=\"/session\">reload this page</a> to reuse the session.</p>\n";

	Response res = Response::fromHtml(200, "Session demo", content.str());
	res.setHeader("Set-Cookie", "session_id=" + id + "; Path=/");
	respond(client, res);
}