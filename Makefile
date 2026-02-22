.PHONY: all install uninstall

LIBDIR ?= /usr/lib
BINDIR ?= /usr/sbin
RUNDIR ?= /run/remountd
BUILDDIR ?= build

# Define the destination directories for clarity.
DEST_CONF_DIR = $(DESTDIR)/etc/remountd
#DEST_DATADIR = $(DESTDIR)/usr/share/remountd

all:

install_configs:
	@echo "Installing configuration files to $(DEST_CONF_DIR)..."
	# Ensure the destination directory exists. Use quotes.
	install -d --owner=root --group=root "$(DEST_CONF_DIR)"
	# Run over all .conf files in configs/ and install files that do not already exist in the destination.
	@find configs -maxdepth 1 \( -name '*.conf' -o -name '*.yaml' \) -exec sh -c ' \
	    src_file="$$1"; \
	    dest_dir="$$2"; \
	    if [ -z "$$dest_dir" ]; then \
	        echo "ERROR: Destination directory argument is empty!" >&2; \
	        exit 1; \
	    fi; \
	    filename=$$(basename "$$src_file"); \
	    dest_file="$$dest_dir/$$filename"; \
	    if [ ! -e "$$dest_file" ]; then \
	        echo "Installing '\''$$src_file'\'' to '\''$$dest_file'\'' (new)"; \
	        install --owner=root --group=root --mode=644 "$$src_file" "$$dest_file"; \
	    else \
	        echo "Skipping '\''$$src_file'\'' ('\''$$dest_file'\'' already exists)"; \
	    fi \
	' _ {} "$(DEST_CONF_DIR)" \; # Pass $(DEST_CONF_DIR) as the second argument

install: install_configs
	install --directory $(DEST_CONF_DIR) $(DESTDIR)$(BINDIR) $(DESTDIR)$(RUNDIR)
	install --owner=root --group=root --mode=644 services/remountd.service $(DESTDIR)$(LIBDIR)/systemd/system/
	install --owner=root --group=root --mode=644 services/remountd.socket $(DESTDIR)$(LIBDIR)/systemd/system/
#	install --owner=root --group=root --mode=644 configs/lo.sh $(DEST_DATADIR)/
	install --owner=root --group=root --mode=755 $(BUILDDIR)/src/remountd $(DESTDIR)$(BINDIR)
	install --owner=root --group=root --mode=755 $(BUILDDIR)/src/remountctl $(DESTDIR)$(BINDIR)

uninstall:
	systemctl stop "remountd.socket" "remountd.service" || true
	systemctl disable "remountd.socket" || true

	rm -f $(DESTDIR)$(LIBDIR)/systemd/system/remountd.service
	rm -f $(DESTDIR)$(LIBDIR)/systemd/system/remountd.socket
	rm -f $(DESTDIR)$(RUNDIR)/remountd.sock
	rmdir $(DESTDIR)$(RUNDIR)
#	rm -f $(DEST_DATADIR)/lo.sh
	rm -f $(DESTDIR)$(BINDIR)/remountd
	rm -f $(DESTDIR)$(BINDIR)/remountctl
