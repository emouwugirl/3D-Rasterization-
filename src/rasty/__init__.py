"""Setting up the Rasty package, configuring what exactly 'import rasty' imports"""
"""shouldn't need to touch ever"""

from . import _native

__all__ = ["_native"]
