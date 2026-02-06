COMPOSE := docker compose

.PHONY: help build lint verify shell ui ui-down clean

help: ## Show available targets
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  %-12s %s\n", $$1, $$2}'

build: ## Build firmware inside Docker
	$(COMPOSE) --profile build run --rm builder build

lint: ## Run all linters (ruff, clang-format, clang-tidy)
	$(COMPOSE) --profile build run --rm builder lint

verify: ## Run verification scripts (requires prior build)
	$(COMPOSE) --profile build run --rm builder verify

shell: ## Open interactive shell in build container
	$(COMPOSE) --profile build run --rm builder shell

ui: ## Start mock UI dev server on localhost:8080
	$(COMPOSE) --profile ui up ui

ui-down: ## Stop mock UI dev server
	$(COMPOSE) --profile ui down

clean: ## Remove Docker build caches (forces full rebuild)
	$(COMPOSE) --profile build --profile ui down -v
