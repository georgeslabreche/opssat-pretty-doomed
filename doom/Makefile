# DOOM for OPS-SAT
#
# Local build:  docker-compose run --rm doom make
# SEPP package: ./build-sepp.sh

PACKAGE_VERSION ?= v3
PACKAGE_NAME = exp4023-DOOM-$(PACKAGE_VERSION)
PACKAGE_DIR = package/$(PACKAGE_NAME)

.PHONY: all clean doom package-prepare package-tar

all: doom

doom:
	$(MAKE) -C src STATIC=YES

clean:
	$(MAKE) -C src clean
	rm -rf package

# Run inside ARM32 container
package-prepare: doom
	mkdir -p $(PACKAGE_DIR)/demos
	mkdir -p $(PACKAGE_DIR)/toGround
	cp src/bin/opssat-doom $(PACKAGE_DIR)/
	cp run $(PACKAGE_DIR)/
	chmod +x $(PACKAGE_DIR)/run
	cp demos/doom.wad $(PACKAGE_DIR)/demos/
	cp demos/*.lmp $(PACKAGE_DIR)/demos/
	cp demos/*.txt $(PACKAGE_DIR)/demos/
	@echo "Package prepared in $(PACKAGE_DIR)/"
	@echo "Run outside container: make package-tar"

# Run OUTSIDE container (BusyBox tar lacks --owner)
package-tar:
	COPYFILE_DISABLE=1 tar --owner=exp --group=exp \
		-czvf package/$(PACKAGE_NAME).tar.gz \
		-C $(PACKAGE_DIR) .
	@echo "Package created: package/$(PACKAGE_NAME).tar.gz"
	@ls -lh package/$(PACKAGE_NAME).tar.gz
