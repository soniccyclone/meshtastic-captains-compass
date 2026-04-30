SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help
.ONESHELL:
.SILENT:

UNAME_S := $(shell uname -s)

SHELL_FILES := $(shell find ./scripts ./src -type f \( -name "*.sh" -o -name "*.bash" \) 2>/dev/null)
WORKFLOWS   := $(shell find .github/workflows -type f -name "*.yml" 2>/dev/null)

##@ Setup

.PHONY: setup
setup: setup/shellcheck setup/actionlint ## Install dev tools

.PHONY: setup/shellcheck
setup/shellcheck: ## Install shellcheck
	if command -v shellcheck >/dev/null 2>&1; then
		echo "shellcheck already installed"
		exit 0
	fi
ifeq ($(UNAME_S),Darwin)
	brew install shellcheck
else ifeq ($(UNAME_S),Linux)
	sudo apt-get update -y && sudo apt-get install -y shellcheck
else
	echo "Unsupported OS: $(UNAME_S)"; exit 1
endif

.PHONY: setup/actionlint
setup/actionlint: ## Install actionlint
	if command -v actionlint >/dev/null 2>&1; then
		echo "actionlint already installed"
		exit 0
	fi
ifeq ($(UNAME_S),Darwin)
	brew install actionlint
else ifeq ($(UNAME_S),Linux)
	curl -sSLo /tmp/actionlint.tgz https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz
	mkdir -p /tmp/actionlint && tar -xzf /tmp/actionlint.tgz -C /tmp/actionlint
	sudo install /tmp/actionlint/actionlint /usr/local/bin/actionlint
else
	echo "Unsupported OS: $(UNAME_S)"; exit 1
endif

##@ Quality

.PHONY: test
test: lint ## Run all checks

.PHONY: lint
lint: lint/shell lint/workflows ## Lint shell scripts and workflows

.PHONY: lint/shell
lint/shell: ## Run shellcheck over scripts and ralph
	if [ -z "$(SHELL_FILES)" ]; then echo "no shell files"; exit 0; fi
	shellcheck $(SHELL_FILES)

.PHONY: lint/workflows
lint/workflows: ## Run actionlint over workflows
	if [ -z "$(WORKFLOWS)" ]; then echo "no workflows"; exit 0; fi
	actionlint $(WORKFLOWS)

##@ Utilities

.PHONY: clean
clean: ## Remove ralph scratch dirs
	rm -rf .ralph

.PHONY: help
help: ## Show this help
	awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n"} /^[a-zA-Z_\/-]+:.*?##/ { printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2 } /^##@/ { printf "\n\033[1m%s\033[0m\n", substr($$0, 5) }' $(MAKEFILE_LIST)
