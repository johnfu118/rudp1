include ./lwip.mk

project.targets := liblwip
liblwip.name := liblwip.a
liblwip.type := lib
liblwip.path := lib
all_sources := $(shell find ./src -name "*.c")
liblwip.sources := $(all_sources)
liblwip.debug=1
liblwip.defines=LWIP_PREFIX_BYTEORDER_FUNCS LWIP_DEBUG 

include ./inc.mk

gendep:
	@echo "generate ${project.targets} depend file ok."
