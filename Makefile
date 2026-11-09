SHELL := /usr/bin/env bash

.PHONY: build

build:
	docker build --file docker/minimal-runtime.Dockerfile --tag webhook-archive:minimal .
