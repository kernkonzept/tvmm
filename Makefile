PKGDIR	= .
L4DIR	?= $(PKGDIR)/../..

include $(L4DIR)/mk/subdir.mk

# the server needs the lib itself
server: lib
