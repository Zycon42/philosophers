NAME=philosophers
CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic

all: $(NAME)

$(NAME): $(NAME).o
	$(CC) $(CFLAGS) $(NAME).o -o $(NAME)
