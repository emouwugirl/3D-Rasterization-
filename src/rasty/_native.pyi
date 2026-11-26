"""Declaring functions for Pylance to recognize them"""
"""Only used for Intellisense and type checking"""
"""i'm putting stuff here so that i don't get squiggly lines everywhere"""
from typing import TypedDict

import numpy as np
import numpy.typing as npt

from .obj2binTypes import ConvertReport

def add(a: int, b: int) -> int: ...

def convert_model(
    input: str,
    output: str,
    recompute_normals: bool = False,
    keep_colors: bool = True,
) -> ConvertReport: ...

class ModelInfo(TypedDict):
    path: str
    format: str
    vertex_count: int
    triangle_count: int
    has_colors: bool
    load_seconds: float
    warnings: list[str]

class FrameStats(TypedDict):
    clear_ms: float
    vertex_ms: float
    raster_ms: float
    shade_ms: float
    total_ms: float
    triangles: int
    triangles_rasterized: int
    triangles_clipped: int
    pixels_covered: int
    threads: int

class CpuRenderer:
    """The CPU renderer with one loaded model; render() draws it with an orbiting camera."""

    def __init__(self, threads: int = 0) -> None: ...
    @property
    def threads(self) -> int: ...
    def load(self, path: str) -> ModelInfo: ...
    def render(
        self,
        width: int,
        height: int,
        yaw: float = 0.6,
        pitch: float = 0.25,
        distance: float = 3.0,
    ) -> tuple[npt.NDArray[np.uint8], FrameStats]: ...
