CC = gcc

source= $(wildcard *.c)

obj = $(source:.c=)

all: $(obj)

$(obj): %: %.c
	$(CC) -o $@ $<

clean: 
	rm -f $(obj)
