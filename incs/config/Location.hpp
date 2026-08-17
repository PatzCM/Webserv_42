#ifndef LOCATION_HPP
#define LOCATION_HPP

#include <string>
#include <utility>
#include <vector>

class Location {
public:
	Location();
	explicit Location(const std::string& path);

	std::string path;
	std::vector<std::string> methods;
	bool methodsExplicit;

	bool redirectSet;
	int redirectCode;
	std::string redirectTarget;

	bool rootSet;
	std::string root;

	bool autoindexSet;
	bool autoindex;

	bool indexSet;
	std::string index;

	bool uploadSet;
	std::string uploadPath;

	bool sessionSet;
	bool session;

	std::vector<std::pair<std::string, std::string> > cgiHandlers;

	bool methodAllowed(const std::string& method) const;
	std::string allowedMethods() const;
	bool cgiInterpreter(const std::string& path, std::string& interpreter) const;
	std::string stripPrefix(const std::string& uri) const;
};

#endif