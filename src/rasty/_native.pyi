"""Declaring functions for Pylance to recognize them"""
"""Only used for Intellisense and type checking"""
from typing import TypedDict
from .obj2binTypes import ConvertReport

def add(a: int, b: int) -> int: ...

def convert_model(
    input: str,
    output: str,
    recompute_normals: bool = False,
    keep_colors: bool = True,
) -> ConvertReport: ...