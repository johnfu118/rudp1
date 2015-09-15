include ../../lwip.mk

project.targets := test_cli test_svr

test_svr.name := test_svr
test_svr.path := bin 
test_svr.sources := server.cpp rudp.c
test_svr.ldadd := ../../lib/liblwip.a
test_svr.debug=1
test_svr.defines = LWIP_PREFIX_BYTEORDER_FUNCS LWIP_DEBUG LWIP_TCP=1

test_cli.name := test_cli
test_cli.path := bin 
test_cli.sources := client.cpp rudp.c
test_cli.ldadd := ../../lib/liblwip.a
test_cli.debug=1
test_cli.defines = LWIP_PREFIX_BYTEORDER_FUNCS LWIP_DEBUG 

include ../../inc.mk
