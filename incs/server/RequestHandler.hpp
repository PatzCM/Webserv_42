#ifndef REQUESTHANDLER_HPP
#define REQUESTHANDLER_HPP

#include "Client.hpp"
#include "Config.hpp"
#include "Response.hpp"

class RequestHandler {
public:
	static void handle(Client& client, const Config& config);

private:
	static void respond(Client& client, const Response& response);
	static void serveGet(Client& client, const ServerBlock* block, const Location* loc);
	static void servePost(Client& client, const ServerBlock* block, const Location* loc);
	static void serveDelete(Client& client, const ServerBlock* block, const Location* loc);
	static void serveSession(Client& client, const ServerBlock* block, const Location* loc);
	static void startCgi(Client& client, const ServerBlock* block, const Location* loc,
	                     const std::string& scriptPath, const std::string& interpreter);
	static bool extractUpload(const Client& client, std::string& name, std::string& data);
	static std::string resolve(const Location* loc, const std::string& uri);
};

#endif