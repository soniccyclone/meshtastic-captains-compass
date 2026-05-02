# Captain's Compass — build pipeline.
# See docs/tdd-issue-002-captains-compass.md §4 for the design.

FIRMWARE_REF := $(shell cat meshtastic-firmware-pin 2>/dev/null)
FIRMWARE_DIR := vendor/meshtastic-firmware
IMAGE        := cc-builder
OUTPUT_DIR   := $(CURDIR)/output

.PHONY: help setup build shell clean bump-pin

help:
	@echo "Captain's Compass — build pipeline"
	@echo
	@echo "Targets:"
	@echo "  setup      — shallow-clone meshtastic/firmware into $(FIRMWARE_DIR) at pinned ref"
	@echo "  build      — build $(IMAGE) docker image and emit output/firmware.uf2"
	@echo "  shell      — drop into a shell inside the builder image"
	@echo "  clean      — remove output/"
	@echo "  bump-pin   — write REF=<tag-or-sha> to meshtastic-firmware-pin and re-setup"
	@echo
	@echo "FIRMWARE_REF = $(FIRMWARE_REF)"
	@echo
	@echo "The pin can be any git ref: a release tag (preferred — e.g. v2.7.15.567b8ea)"
	@echo "or a full commit SHA. GitHub allows shallow-fetching either form."

setup:
	@test -n "$(FIRMWARE_REF)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	@if [ ! -d $(FIRMWARE_DIR)/.git ]; then \
	  mkdir -p $(FIRMWARE_DIR); \
	  cd $(FIRMWARE_DIR) && \
	    git init -q && \
	    git remote add origin https://github.com/meshtastic/firmware.git; \
	fi
	cd $(FIRMWARE_DIR) && \
	  git fetch --depth=1 origin $(FIRMWARE_REF) && \
	  git checkout -q FETCH_HEAD && \
	  git submodule update --init --recursive --depth=1

build:
	@test -n "$(FIRMWARE_REF)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	docker build --build-arg FIRMWARE_REF=$(FIRMWARE_REF) -t $(IMAGE) -f docker/Dockerfile .
	@mkdir -p $(OUTPUT_DIR)
	docker run --rm -v "$(OUTPUT_DIR):/output" $(IMAGE)
	@echo "Artifact: $(OUTPUT_DIR)/firmware.uf2"

shell:
	docker run --rm -it --entrypoint /bin/bash -v "$(OUTPUT_DIR):/output" $(IMAGE)

clean:
	rm -rf $(OUTPUT_DIR)

bump-pin:
	@test -n "$(REF)" || ( echo "usage: make bump-pin REF=<tag-or-sha>" && exit 1 )
	@printf "%s\n" "$(REF)" > meshtastic-firmware-pin
	@$(MAKE) setup
