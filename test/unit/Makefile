project.targets := lwip_test
all_sources := $(shell find . -name "*.c" -or -name "*cc")
lwip_test.sources := $(all_sources)
lwip_test.debug=1
lwip_test.includes := $(project.includes) $(shell pwd)
lwip_test.ldadd=-lgtest -lpthread ../../lib/liblwip.a
lwip_test.defines=LWIP_PREFIX_BYTEORDER_FUNCS LWIP_UNITTESTS_NOFORK LWIP_DEBUG 

include ../../inc.mk

gendep:
	@echo "generate ${project.targets} depend file ok."
