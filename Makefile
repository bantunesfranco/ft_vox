RED=\033[1;31m
GREEN=\033[1;32m
YELLOW=\033[1;33m
BLUE=\033[1;34m
MAGENTA=\033[1;35m
CYAN=\033[1;36m
END=\033[0m

NAME = ft_vox

CC = g++
CFLAGS = -Wall -Wextra -Werror -Wpedantic
# CFLAGS += -g -fsanitize=address

DIR_S = srcs
DIR_I = incs
DIR_O = obj

SRCS = $(shell find $(DIR_S) -name '*.cpp')
HEADERS = $(shell find $(DIR_I) -name '*.hpp')
INCS = -I $(DIR_I)
OBJS = $(patsubst $(DIR_S)/%.s,$(DIR_O)/%.o,$(SRCS))

ENGINE = engine
SRCS += $(shell find $(ENGINE)/$(DIR_S) -name '*.cpp')
HEADERS += $(shell find $(ENGINE)/$(DIR_I) -name '*.hpp')
INCS += -I $(ENGINE)/$(DIR_I)

GLFW = glfw
INCS += -I $(GLFW)/include -I $(GLFW)/deps

ifeq ($(OS),Windows_NT)
	GLFW_LIB = $(GLFW)/build/src/Debug/glfw3.lib
else
	GLFW_LIB = $(GLFW)/build/src/libglfw3.a
endif

ifeq ($(OS),Windows_NT)
	NAME = $(NAME).exe
endif

ifeq ($(shell uname -s),Linux)
	LIBS = -lwayland-client -lxkbcommon -lX11 -lXrandr -lXinerama -lXi -lXxf86vm -lXcursor
else
	LIBS = ""
endif

$(NAME): $(OBJS) $(GLFW_LIB)
	@echo "$(MAGENTA)Creating $@$(END)"
	@$(CC) $(CFLAGS) $(INCS) $(OBJS) $(LIBS) -L$(GLFW)/build/src -lglfw3 -o $@
	@echo "$(GREEN)Done!$(END)"

$(DIR_O)/%.o: $(DIR_S)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	@echo "$(BLUE)Compiling $(notdir $<)$(END)"
	@$(CC) $(CFLAGS) $(INCS) -c $< -o $@

$(GLFW_LIB):
	@cmake $(GLFW) -B $(GLFW)/build
	@make -C $(GLFW)/build -j4

all: $(NAME)

clean: 
	@echo "$(RED)Removing objs$(END)"
	@rm -rf obj

fclean: clean
	@echo "$(RED)Removing $(NAME)$(END)"
	@rm -rf $(NAME)

re: fclean all

test: all
	@echo "$(CYAN)Testing $(NAME)$(END)\n"
	@./tester.sh

.PHONY: all clean fclean re test 