from typing import TypedDict

class ConvertReport(TypedDict):
    input: str
    output: str
    format: str
    vertex_count: int
    triangle_count: int
    has_colors: bool
    warnings: list[str]
    read_seconds: float
    build_seconds: float
    bounds_min: tuple[float, float, float]
    bounds_max: tuple[float, float, float]
    center: tuple[float, float, float]
    radius: float