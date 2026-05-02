# Captain's Compass — build pipeline.
# See docs/tdd-issue-002-captains-compass.md §4 for the design.

FIRMWARE_SHA := $(shell cat meshtastic-firmware-pin 2>/dev/null)
FIRMWARE_DIR := vendor/meshtastic-firmware
IMAGE        := cc-builder
OUTPUT_DIR   := $(CURDIR)/output

.PHONY: help setup build shell clean bump-pin

help:
	@echo "Captain's Compass — build pipeline"
	@echo
	@echo "Targets:"
	@echo "  setup     — shallow-clone meshtastic/firmware into $(FIRMWARE_DIR) at pinned SHA"
	@echo "  build     — build $(IMAGE) docker image and emit output/firmware.uf2"
	@echo "  shell     — drop into a shell inside the builder image"
	@echo "  clean     — remove output/"
	@echo "  bump-pin  — write SHA=<sha> to meshtastic-firmware-pin and re-setup"
	@echo
	@echo "FIRMWARE_SHA = $(FIRMWARE_SHA)"

setup:
	@test -n "$(FIRMWARE_SHA)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	@if [ ! -d $(FIRMWARE_DIR)/.git ]; then \
	  mkdir -p $(FIRMWARE_DIR); \
	  cd $(FIRMWARE_DIR) && \
	    git init -q && \
	    git remote add origin https://github.com/meshtastic/firmware.git; \
	fi
	cd $(FIRMWARE_DIR) && \
	  git fetch --depth=1 origin $(FIRMWARE_SHA) && \
	  git checkout -q FETCH_HEAD && \
	  git submodule update --init --recursive --depth=1

build:
	@test -n "$(FIRMWARE_SHA)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	docker build --build-arg FIRMWARE_SHA=$(FIRMWARE_SHA) -t $(IMAGE) -f docker/Dockerfile .
	@mkdir -p $(OUTPUT_DIR)
	docker run --rm -v "$(OUTPUT_DIR):/output" $(IMAGE)
	@echo "Artifact: $(OUTPUT_DIR)/firmware.uf2"

shell:
	docker run --rm -it --entrypoint /bin/bash -v "$(OUTPUT_DIR):/output" $(IMAGE)

clean:
	rm -rf $(OUTPUT_DIR)

bump-pin:
	@test -n "$(SHA)" || ( echo "usage: make bump-pin SHA=<full-sha>" && exit 1 )
	@printf "%s\n" "$(SHA)" > meshtastic-firmware-pin
	@$(MAKE) setup
