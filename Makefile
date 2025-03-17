CC = gcc
LIBS = -lreadline
CFLAGS = -g -Wall -Wextra -Werror

.PHONY: default all clean

default: pssh job_info
all: default

# pssh object files
PSSH_OBJS = pssh.o parse.o builtin.o job_control.o

# job_info object files
JOB_INFO_OBJS = job_info.o

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $< -o $@

pssh: $(PSSH_OBJS)
	$(CC) $(PSSH_OBJS) -Wall $(LIBS) -o $@

job_info: $(JOB_INFO_OBJS)
	$(CC) $(JOB_INFO_OBJS) -Wall -o $@

clean:
	-rm -f *.o
	-rm -f pssh job_info
