"""The desktop GUI entry point."""


def run() -> None:
    from .window import RastyWindow

    RastyWindow().mainloop()
