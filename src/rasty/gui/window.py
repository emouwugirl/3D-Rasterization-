"""A settings page and a rendering canvas, sharing one window."""

import statistics
import time
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, ttk

from .. import _native

# Starting camera: a little to the right of and above the model, far enough to see all of it.
DEFAULT_YAW = 0.6
DEFAULT_PITCH = 0.25
DEFAULT_DISTANCE = 3.0


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

        # Scene state: the CPU renderer holds the loaded model; the camera orbits it.
        self.cpu: _native.CpuRenderer | None = None
        self.all_cores = tk.BooleanVar(value=True)
        self.model_text = tk.StringVar()
        self.timing_text = tk.StringVar()
        self.benchmark_text = tk.StringVar()
        self.yaw = DEFAULT_YAW
        self.pitch = DEFAULT_PITCH
        self.distance = DEFAULT_DISTANCE
        self._drag_from: tuple[int, int] | None = None
        self._render_pending = False
        self._photo: tk.PhotoImage | None = None  # tk only shows an image while Python keeps it

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

        bar = ttk.Frame(self.scene, padding=8)
        bar.grid(row=0, column=0, sticky="ew")
        ttk.Button(bar, text="Back", command=self._show_settings).pack(side="left")
        ttk.Checkbutton(
            bar, text="All CPU cores", variable=self.all_cores, command=self._threads_changed
        ).pack(side="left", padx=(12, 0))
        ttk.Button(bar, text="Time 1920×1080", command=self._time_1080p).pack(
            side="left", padx=(12, 0)
        )
        ttk.Button(bar, text="Reset view", command=self._reset_view).pack(side="left", padx=(12, 0))
        ttk.Label(bar, textvariable=self.model_text).pack(side="left", padx=(12, 0))

        # Frames from the renderer are drawn here. Drag to orbit, scroll to zoom.
        self.canvas = tk.Canvas(self.scene, background="#181818", highlightthickness=0)
        self.canvas.grid(row=1, column=0, sticky="nsew")
        self._image_item = self.canvas.create_image(0, 0, anchor="nw")
        self.canvas.bind("<Configure>", lambda _event: self._request_render())
        self.canvas.bind("<ButtonPress-1>", self._start_drag)
        self.canvas.bind("<B1-Motion>", self._drag)
        self.canvas.bind("<ButtonRelease-1>", self._end_drag)
        self.canvas.bind("<MouseWheel>", self._zoom)  # Windows and macOS
        self.canvas.bind("<Button-4>", lambda _event: self._zoom_by(0.9))  # Linux wheel up
        self.canvas.bind("<Button-5>", lambda _event: self._zoom_by(1 / 0.9))  # Linux wheel down

        info = ttk.Frame(self.scene, padding=(8, 4, 8, 8))
        info.grid(row=2, column=0, sticky="ew")
        ttk.Label(info, textvariable=self.timing_text, font="TkFixedFont").pack(anchor="w")
        ttk.Label(info, textvariable=self.benchmark_text, font="TkFixedFont").pack(anchor="w")

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
        if self.renderer.get() == "gpu":
            self.error.set("The GPU renderer is not built yet. Choose CPU to render.")
            return

        self.error.set("Loading…")
        self.update_idletasks()
        self.selected_obj = path.resolve()
        self.selected_renderer = self.renderer.get()
        if not self._load_model():
            return

        self.error.set("")
        self.benchmark_text.set("")
        self.scene.tkraise()
        self.canvas.focus_set()
        self._reset_view()

    def _load_model(self) -> bool:
        """(Re)creates the CPU renderer and loads the selected model into it."""
        assert self.selected_obj is not None
        try:
            self.cpu = _native.CpuRenderer(threads=0 if self.all_cores.get() else 1)
            info = self.cpu.load(str(self.selected_obj))
        except Exception as exc:  # ConvertError, MeshError, OSError, ...
            self.cpu = None
            self.error.set(f"Could not load {self.selected_obj.name}: {exc}")
            return False
        self.model_text.set(
            f"{self.selected_obj.name} · {info['triangle_count']:,} triangles · "
            f"loaded in {info['load_seconds']:.2f} s"
        )
        return True

    def _show_settings(self) -> None:
        self.settings.tkraise()
        self.path_entry.focus_set()

    # --- rendering ----------------------------------------------------------------------------

    def _request_render(self) -> None:
        """Renders once the window is idle, so a burst of drag events draws one frame."""
        if not self._render_pending:
            self._render_pending = True
            self.after_idle(self._render_now)

    def _render_now(self) -> None:
        self._render_pending = False
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        if self.cpu is None or width < 2 or height < 2:
            return
        frame, stats = self.cpu.render(width, height, self.yaw, self.pitch, self.distance)

        # tk reads a binary PPM (P6): a short text header, then the RGB bytes. Alpha is dropped.
        start = time.perf_counter()
        ppm = f"P6 {width} {height} 255 ".encode() + frame[:, :, :3].tobytes()
        self._photo = tk.PhotoImage(width=width, height=height, data=ppm, format="PPM")
        self.canvas.itemconfigure(self._image_item, image=self._photo)
        display_ms = (time.perf_counter() - start) * 1000.0

        threads = stats["threads"]
        self.timing_text.set(
            f"CPU, {threads} thread{'s' if threads != 1 else ''} · {width}×{height} · "
            f"render {stats['total_ms']:.2f} ms (clear {stats['clear_ms']:.2f}, "
            f"vertex {stats['vertex_ms']:.2f}, raster {stats['raster_ms']:.2f}, "
            f"shade {stats['shade_ms']:.2f}) · {stats['triangles_rasterized']:,} triangles drawn · "
            f"display {display_ms:.1f} ms"
        )

    def _time_1080p(self) -> None:
        """Renders the current view at 1920×1080 eleven times; reports the median of the last ten."""
        if self.cpu is None:
            return
        self.benchmark_text.set("Timing 1920×1080…")
        self.update_idletasks()
        runs = [self.cpu.render(1920, 1080, self.yaw, self.pitch, self.distance)[1] for _ in range(11)]
        runs = runs[1:]  # the first frame resizes the buffers

        def median(key: str) -> float:
            return statistics.median(run[key] for run in runs)

        total = median("total_ms")
        threads = runs[0]["threads"]
        self.benchmark_text.set(
            f"1920×1080, median of 10, {threads} thread{'s' if threads != 1 else ''}: "
            f"{total:.2f} ms = {1000.0 / total:.0f} fps (clear {median('clear_ms'):.2f}, "
            f"vertex {median('vertex_ms'):.2f}, raster {median('raster_ms'):.2f}, "
            f"shade {median('shade_ms'):.2f})"
        )
        # The 1080p frames replaced the canvas-sized buffers; draw the canvas frame again.
        self._request_render()

    # --- camera -------------------------------------------------------------------------------

    def _reset_view(self) -> None:
        self.yaw, self.pitch, self.distance = DEFAULT_YAW, DEFAULT_PITCH, DEFAULT_DISTANCE
        self._request_render()

    def _threads_changed(self) -> None:
        if self.selected_obj is not None and self._load_model():
            self.benchmark_text.set("")
            self._request_render()

    def _start_drag(self, event: tk.Event) -> None:
        self._drag_from = (event.x, event.y)

    def _drag(self, event: tk.Event) -> None:
        if self._drag_from is None:
            return
        dx, dy = event.x - self._drag_from[0], event.y - self._drag_from[1]
        self._drag_from = (event.x, event.y)
        self.yaw -= dx * 0.01
        self.pitch = max(-1.5, min(1.5, self.pitch + dy * 0.01))
        self._request_render()

    def _end_drag(self, _event: tk.Event) -> None:
        self._drag_from = None

    def _zoom(self, event: tk.Event) -> None:
        self._zoom_by(0.9 if event.delta > 0 else 1 / 0.9)

    def _zoom_by(self, factor: float) -> None:
        self.distance = max(1.1, min(20.0, self.distance * factor))
        self._request_render()
