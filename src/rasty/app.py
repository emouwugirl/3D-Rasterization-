"""The whole app goes here"""
"""GUI goes in gui"""
import rasty
import argparse

from .assets import convert_model

def main() -> None:
    """Start the Rasty application."""
    print(rasty._native.add(2, 3))
    print("successfully installed rasty!!!")
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--convert')
    args = parser.parse_args()

    if args.convert:
        report = convert_model(args.convert)

        print(f"Wrote: {report['output']}")
        print(f"Vertices: {report['vertex_count']}")
        print(f"Triangles: {report['triangle_count']}")

        for warning in report["warnings"]:
            print(f"Warning: {warning}")
