ifeq ($(origin CC), default)
	CC = gcc
endif
LDLIBS += -ludev
ibexpair:
