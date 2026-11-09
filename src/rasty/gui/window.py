"""A settings page and a rendering canvas, sharing one window."""

from pathlib import Path
import tkinter as tk
from tkinter import filedialog, ttk


class RastyWindow(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Rasty")
        self.geometry("900x600")
        self.minsize(480, 320)

        self.obj_path = tk.StringVar()
        self.renderer = tk.StringVar(value="cpu")
        self.error = tk.StringVar()
        self.selected_obj: Path | None = None
        self.selected_renderer = "cpu"

        self.rowconfigure(0, weight=1)
        self.columnconfigure(0, weight=1)
        self.settings = ttk.Frame(self, padding=24)
        self.scene = ttk.Frame(self)
        for page in (self.settings, self.scene):
            page.grid(row=0, column=0, sticky="nsew")

        self._build_settings()
        self._build_scene()
        self.settings.tkraise()

    def _build_settings(self) -> None:
        self.settings.columnconfigure(0, weight=1)
        ttk.Label(self.settings, text="OBJ file").grid(
            row=0, column=0, sticky="w", pady=(0, 8)
        )
        self.path_entry = ttk.Entry(self.settings, textvariable=self.obj_path)
        self.path_entry.grid(row=1, column=0, sticky="ew")
        ttk.Button(self.settings, text="Browse…", command=self._browse).grid(
            row=1, column=1, padx=(8, 0)
        )

        ttk.Label(self.settings, text="Renderer").grid(
            row=2, column=0, sticky="w", pady=(24, 8)
        )
        choices = ttk.Frame(self.settings)
        choices.grid(row=3, column=0, sticky="w")
        for name in ("cpu", "gpu"):
            ttk.Radiobutton(
                choices, text=name.upper(), variable=self.renderer, value=name
            ).pack(side="left", padx=(0, 16))

        ttk.Label(self.settings, textvariable=self.error, wraplength=400).grid(
            row=4, column=0, columnspan=2, sticky="w", pady=(16, 0)
        )
        ttk.Button(self.settings, text="Open scene", command=self._open_scene).grid(
            row=5, column=0, sticky="w", pady=(16, 0)
        )

    def _build_scene(self) -> None:
        self.scene.rowconfigure(1, weight=1)
        self.scene.columnconfigure(0, weight=1)
        ttk.Button(self.scene, text="Back", command=self._show_settings).grid(
            row=0, column=0, sticky="w", padx=8, pady=8
        )
        # Future renderers display their frames here.
        self.canvas = tk.Canvas(self.scene, background="#181818", highlightthickness=0)
        self.canvas.grid(row=1, column=0, sticky="nsew")

    def _browse(self) -> None:
        path = filedialog.askopenfilename(
            parent=self, title="Select an OBJ file", filetypes=[("OBJ files", "*.obj")]
        )
        if path:
            self.obj_path.set(path)
            self.error.set("")

    def _open_scene(self) -> None:
        path = Path(self.obj_path.get().strip()).expanduser()
        if path.suffix.lower() != ".obj" or not path.is_file():
            self.error.set("Choose an existing .obj file.")
            return

        self.error.set("")
        self.selected_obj = path.resolve()
        self.selected_renderer = self.renderer.get()
        self.scene.tkraise()
        self.canvas.focus_set()

    def _show_settings(self) -> None:
        self.settings.tkraise()
        self.path_entry.focus_set()
