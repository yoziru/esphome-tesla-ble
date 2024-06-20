.DEFAULT_GOAL := help
BOARD := m5stack-nanoc6
TARGET := tesla-ble-$(BOARD).yml

compile: .esphome/build/tesla-ble/.pioenvs/tesla-ble/firmware.bin .esphome/build/tesla-ble/$(TARGET).touchfile  ## Read the configuration and compile the binary.

.esphome/build/tesla-ble/$(TARGET).touchfile: .venv/touchfile $(TARGET) packages/*.yml boards/$(BOARD).yml  ## Validate the configuration and create a binary.
	. .venv/bin/activate; esphome compile $(TARGET)
	touch .esphome/build/$(TARGET).touchfile

.esphome/build/tesla-ble/.pioenvs/tesla-ble/firmware.bin: .esphome/build/tesla-ble/$(TARGET).touchfile ## Create the binary.

upload: .esphome/build/tesla-ble/.pioenvs/tesla-ble/firmware.bin ## Validate the configuration, create a binary, upload it, and start logs.
	. .venv/bin/activate; esphome upload $(TARGET); esphome logs $(TARGET)

logs:
	. .venv/bin/activate; esphome logs $(TARGET)

deps: .venv/touchfile ## Create the virtual environment and install the requirements.

.venv/touchfile: requirements.txt
	test -d .venv || python -m venv .venv
	. .venv/bin/activate && pip install -Ur requirements.txt
	touch .venv/touchfile

.PHONY: clean
clean: ## Remove the virtual environment and the esphome build directory
	rm -rf .venv
	rm -rf .esphome

.PHONY: help
help: ## Show help messages for make targets
	@grep -E '^[a-zA-\/Z_-]+:.*?## .*$$' $(firstword $(MAKEFILE_LIST)) \
		| sort \
		| awk 'BEGIN {FS = ":.*?## "}; {printf "\033[32m%-30s\033[0m %s\n", $$1, $$2}'


compile_docker: ## Compile the binary using docker
	docker run --platform linux/amd64 --rm -v $(PWD):/config esphome/esphome compile /config/$(TARGET)
