# Convenience targets. Everything here calls the same scripts the docs describe, so
# there is no second source of truth for how the project is built or tested.
.PHONY: help bootstrap build test run lint measure release-asset clean

help:
	@echo "make bootstrap      restore pinned dependencies and the toolchain"
	@echo "make build          CMake Release build (build/bin/baritone-sim)"
	@echo "make test           full regression suite (C++ harnesses + Python tests)"
	@echo "make run            run every example into build/reports"
	@echo "make lint           validate every example with tools/scenario-lint"
	@echo "make measure        record a measurement of the suite in reports/metrics.json"
	@echo "make release-asset  build dist/scenario-lint.tar.gz reproducibly"
	@echo "make clean          remove build artifacts (keeps third_party/ and caches)"

bootstrap:
	./scripts/bootstrap.sh

build: bootstrap
	./scripts/build.sh

test: bootstrap
	./tests/run.sh

run: build
	./build/bin/baritone-sim run examples/*.json --batch-out build/reports || true

lint:
	./tools/scenario-lint/scenario_lint.py examples/*.json

measure: bootstrap
	python3 scripts/measure.py --label "suite-$$(date +%Y%m%dT%H%M%S)" \
	    --output reports/metrics.json -- ./tests/run.sh

release-asset:
	./tools/scenario-lint/build-release.sh

clean:
	rm -rf build build-android
