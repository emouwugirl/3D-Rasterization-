PYTHON := .venv/bin/python
CMAKE := .venv/bin/cmake
DEPENDENCIES := .venv/.requirements-installed

.PHONY: all setup configure build clean

all: build

setup: $(DEPENDENCIES)

$(PYTHON):
	python3 -m venv .venv

$(DEPENDENCIES): requirements.txt pyproject.toml $(PYTHON)
	$(PYTHON) -m pip install -r requirements.txt
	touch $(DEPENDENCIES)

configure: $(DEPENDENCIES)
	$(CMAKE) -S . -B build -DPython_EXECUTABLE="$(CURDIR)/$(PYTHON)" -Dpybind11_DIR="$$($(PYTHON) -m pybind11 --cmakedir)" -DRASTY_BUILD_TESTS=OFF

# This builds the python package
build: $(DEPENDENCIES)
	$(PYTHON) -m pip install --no-build-isolation --editable .

clean:
	rm -rf build
