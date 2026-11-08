from pathlib import Path
from typing import Optional

from . import _native
from .obj2binTypes import ConvertReport

def convert_model(
    input_path: str | Path, 
    output_path: Optional[str | Path] = None, 
    *, 
    recompute_normals: bool = False, 
    keep_colors: bool = True
) -> ConvertReport:
    input_path = Path(input_path)

    if output_path is None:
        output_path = input_path.with_suffix(".bin")
    else:
        output_path = Path(output_path)

    return _native.convert_model(str(input_path), str(output_path), recompute_normals, keep_colors)