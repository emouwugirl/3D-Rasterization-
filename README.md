# 3D Rasterization

## Setup and build

Requires Python 3 and a C++17
From repo root, run:

`make`

The first make will create `.venv`, installs packages in `requirements.txt`, configures CMake, and builds the `rasty_native` Python extension and `obj2bin` converter. Later runs reuse the environment and rebuild as needed. The converter tests are disabled by default (don't think we need this???)
