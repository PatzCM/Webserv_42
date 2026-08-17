NAME		= webserv

CXX			= c++
CXXFLAGS	= -Wall -Wextra -Werror -std=c++98

INC_DIR		= incs
SRC_DIR		= srcs
OBJ_DIR		= objs

INC_FLAGS	= -I$(INC_DIR)/utils -I$(INC_DIR)/config -I$(INC_DIR)/http \
			  -I$(INC_DIR)/cgi -I$(INC_DIR)/server

SRCS		= srcs/main.cpp \
			  srcs/config/Config.cpp \
			  srcs/config/ServerBlock.cpp \
			  srcs/config/Location.cpp \
			  srcs/http/Request.cpp \
			  srcs/http/Response.cpp \
			  srcs/http/MimeTypes.cpp \
			  srcs/server/Server.cpp \
			  srcs/server/Client.cpp \
			  srcs/server/RequestHandler.cpp \
			  srcs/cgi/CgiProcess.cpp \
			  srcs/utils/Utils.cpp

OBJS		= $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

CONFIG		?= configs/default.conf

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INC_FLAGS) -c $< -o $@

run: $(NAME)
	./$(NAME) $(CONFIG)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re run