#include "Location.hpp"
#include "Utils.hpp"

Location::Location() : methodsExplicit(false), redirectSet(false), redirectCode(301),
	autoindexSet(false), autoindex(false), indexSet(false), uploadSet(false),
	sessionSet(false), session(false) {}

Location::Location(const std::string& p) : path(p), methodsExplicit(false),
	redirectSet(false), redirectCode(301), autoindexSet(false), autoindex(false),
	indexSet(false), uploadSet(false), sessionSet(false), session(false) {}

bool Location::methodAllowed(const std::string& method) const {
	if (!methodsExplicit)
		return true;
	for (size_t i = 0; i < methods.size(); ++i) {
		if (methods[i] == method)
			return true;
	}
	return false;
}

std::string Location::allowedMethods() const {
	std::string out;
	for (size_t i = 0; i < methods.size(); ++i) {
		if (i > 0)
			out += ", ";
		out += methods[i];
	}
	if (out.empty())
		out = "GET, POST, DELETE";
	return out;
}

bool Location::cgiInterpreter(const std::string& path, std::string& interpreter) const {
	if (cgiHandlers.empty())
		return false;

	std::string::size_type dot = path.find_last_of('.');
	if (dot == std::string::npos)
		return false;
	std::string ext = util::toLower(path.substr(dot));

	for (size_t i = 0; i < cgiHandlers.size(); ++i) {
		if (util::toLower(cgiHandlers[i].first) == ext) {
			interpreter = cgiHandlers[i].second;
			return true;
		}
	}
	return false;
}

std::string Location::stripPrefix(const std::string& uri) const {
	if (path == "/")
		return uri;
	return uri.substr(path.size());
}