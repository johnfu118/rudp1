project.targets := lwip_test
all_sources := $(shell find . -name "*.c")
lwip_test.sources := $(all_sources)
lwip_test.debug=1
lwip_test.includes := $(project.includes) $(shell pwd)
lwip_test.ldadd=../../lib/liblwip.a -lcheck
lwip_test.defines=LWIP_PREFIX_BYTEORDER_FUNCS LWIP_UNITTESTS_NOFORK LWIP_DEBUG 

include ../../inc.mk

gendep:
	@echo "generate ${project.targets} depend file ok."
