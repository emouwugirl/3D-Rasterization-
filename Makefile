PYTHON := .venv/bin/python
CMAKE := .venv/bin/cmake
DEPENDENCIES := .venv/.requirements-installed

.PHONY: all setup configure build clean

all: build

setup: $(DEPENDENCIES)

$(PYTHON):
	python3 -m venv .venv

$(DEPENDENCIES): requirements.txt $(PYTHON)
	$(PYTHON) -m pip install -r requirements.txt
	touch $(DEPENDENCIES)

configure: $(DEPENDENCIES)
	$(CMAKE) -S . -B build -DPython_EXECUTABLE="$(CURDIR)/$(PYTHON)" -Dpybind11_DIR="$$($(PYTHON) -m pybind11 --cmakedir)" -DRASTY_BUILD_TESTS=OFF

build: configure
	$(CMAKE) --build build -j

clean:
	rm -rf build
