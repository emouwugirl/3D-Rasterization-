"""A settings page and a rendering canvas, sharing one window."""

import statistics
import time
from pathlib import Path
from tkinter import BooleanVar, Canvas, Event, PhotoImage, StringVar, Tk, filedialog, ttk
from typing import Literal

from .. import _native

# Starting camera: a little to the right of and above the model, far enough to see all of it.
DEFAULT_YAW = 0.6
DEFAULT_PITCH = 0.25
DEFAULT_DISTANCE = 3.0


class RastyWindow(Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Rasty")
        self.geometry("900x600")
        self.minsize(480, 320)

        self.obj_path = StringVar()
        self.renderer = StringVar(value="cpu")
        self.error = StringVar()
        self.selected_obj: Path | None = None
        self.selected_renderer = "cpu"

        self.active_renderer: _native.CpuRenderer | _native.GpuRenderer | None = None
        self.all_cores = BooleanVar(value=True)
        self.model_text = StringVar()
        self.timing_text = StringVar()
        self.benchmark_text = StringVar()
        self.yaw = DEFAULT_YAW
        self.pitch = DEFAULT_PITCH
        self.distance = DEFAULT_DISTANCE
        self._drag_from: tuple[int, int] | None = None
        self._render_pending = False
        self._photo: PhotoImage | None = None  # Keep a reference so Tk can display the image.

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
        self.thread_control = ttk.Checkbutton(
            bar, text="All CPU cores", variable=self.all_cores, command=self._threads_changed
        )
        self.thread_control.pack(side="left", padx=(12, 0))
        ttk.Button(bar, text="Time 1920×1080", command=self._time_1080p).pack(
            side="left", padx=(12, 0)
        )
        ttk.Button(bar, text="Reset view", command=self._reset_view).pack(side="left", padx=(12, 0))
        ttk.Label(bar, textvariable=self.model_text).pack(side="left", padx=(12, 0))

        # Frames from the renderer are drawn here. Drag to orbit, scroll to zoom.
        self.canvas = Canvas(self.scene, background="#181818", highlightthickness=0)
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
        renderer_name = self.renderer.get()
        if renderer_name == "gpu" and not hasattr(_native, "GpuRenderer"):
            self.error.set("Enable CUDA with CMAKE_ARGS=-DRASTY_BUILD_GPU=ON make, then restart Rasty.")
            return

        self.error.set("Loading…")
        self.update_idletasks()
        if not self._load_model(path.resolve(), renderer_name):
            return

        self.error.set("")
        self.benchmark_text.set("")
        self.scene.tkraise()
        self.canvas.focus_set()
        self._reset_view()

    def _load_model(self, path: Path, renderer_name: str) -> bool:
        renderer: _native.CpuRenderer | _native.GpuRenderer
        try:
            if renderer_name == "gpu":
                renderer = _native.GpuRenderer()
            else:
                renderer = _native.CpuRenderer(threads=0 if self.all_cores.get() else 1)
            info = renderer.load(str(path))
        except Exception as exc:  # ConvertError, MeshError, OSError, ...
            self.error.set(f"Could not open {path.name} with {renderer_name.upper()}: {exc}")
            return False
        self.active_renderer = renderer
        self.selected_obj = path
        self.selected_renderer = renderer_name
        self.thread_control.state(["disabled"] if renderer_name == "gpu" else ["!disabled"])
        self.model_text.set(
            f"{path.name} · {info['triangle_count']:,} triangles · "
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
        if self.active_renderer is None or width < 2 or height < 2:
            return
        try:
            frame, stats = self.active_renderer.render(width, height, self.yaw, self.pitch, self.distance)
        except (RuntimeError, ValueError) as exc:
            self.canvas.itemconfigure(self._image_item, image="")
            self._photo = None
            self.timing_text.set(f"{self.selected_renderer.upper()} render failed: {exc}")
            return

        # PhotoImage reads binary PPM (P6) using the RGB bytes without alpha.
        start = time.perf_counter()
        ppm = f"P6 {width} {height} 255 ".encode() + frame[:, :, :3].tobytes()
        self._photo = PhotoImage(width=width, height=height, data=ppm, format="PPM")
        self.canvas.itemconfigure(self._image_item, image=self._photo)
        display_ms = (time.perf_counter() - start) * 1000.0

        timing = f"GPU · {width}×{height} · render {stats['total_ms']:.2f} ms"
        if "threads" in stats:
            threads = stats["threads"]
            timing = (
                f"CPU, {threads} thread{'s' if threads != 1 else ''} · {width}×{height} · "
                f"render {stats['total_ms']:.2f} ms (clear {stats['clear_ms']:.2f}, "
                f"vertex {stats['vertex_ms']:.2f}, raster {stats['raster_ms']:.2f}, "
                f"shade {stats['shade_ms']:.2f}) · {stats['triangles_rasterized']:,} triangles drawn"
            )
        self.timing_text.set(f"{timing} · display {display_ms:.1f} ms")

    def _time_1080p(self) -> None:
        """Renders the current view at 1920×1080 eleven times; reports the median of the last ten."""
        renderer = self.active_renderer
        if renderer is None:
            return
        self.benchmark_text.set("Timing 1920×1080…")
        self.update_idletasks()
        try:
            runs = [renderer.render(1920, 1080, self.yaw, self.pitch, self.distance)[1] for _ in range(11)]
        except (RuntimeError, ValueError) as exc:
            self.benchmark_text.set(f"Benchmark failed: {exc}")
            return
        runs = runs[1:]  # Omit the warmup frame.

        total = statistics.median(run["total_ms"] for run in runs)
        result = f"{total:.2f} ms = {1000.0 / total:.0f} fps"
        prefix = "1920×1080, median of 10"
        cpu_runs = [run for run in runs if "threads" in run]
        if cpu_runs:
            def median(key: Literal["clear_ms", "vertex_ms", "raster_ms", "shade_ms"]) -> float:
                return statistics.median(run[key] for run in cpu_runs)

            threads = cpu_runs[0]["threads"]
            prefix += f", {threads} thread{'s' if threads != 1 else ''}"
            result += (
                f" (clear {median('clear_ms'):.2f}, "
                f"vertex {median('vertex_ms'):.2f}, raster {median('raster_ms'):.2f}, "
                f"shade {median('shade_ms'):.2f})"
            )
        else:
            prefix += ", GPU"
        self.benchmark_text.set(f"{prefix}: {result}")
        # The 1080p frames replaced the canvas-sized buffers; draw the canvas frame again.
        self._request_render()

    # --- camera -------------------------------------------------------------------------------

    def _reset_view(self) -> None:
        self.yaw, self.pitch, self.distance = DEFAULT_YAW, DEFAULT_PITCH, DEFAULT_DISTANCE
        self._request_render()

    def _threads_changed(self) -> None:
        if self.selected_obj is None or self.selected_renderer != "cpu":
            return
        if self._load_model(self.selected_obj, "cpu"):
            self.benchmark_text.set("")
            self._request_render()
        else:
            self.timing_text.set(self.error.get())

    def _start_drag(self, event: Event) -> None:
        self._drag_from = (event.x, event.y)

    def _drag(self, event: Event) -> None:
        if self._drag_from is None:
            return
        dx, dy = event.x - self._drag_from[0], event.y - self._drag_from[1]
        self._drag_from = (event.x, event.y)
        self.yaw -= dx * 0.01
        self.pitch = max(-1.5, min(1.5, self.pitch + dy * 0.01))
        self._request_render()

    def _end_drag(self, _event: Event) -> None:
        self._drag_from = None

    def _zoom(self, event: Event) -> None:
        self._zoom_by(0.9 if event.delta > 0 else 1 / 0.9)

    def _zoom_by(self, factor: float) -> None:
        self.distance = max(1.1, min(20.0, self.distance * factor))
        self._request_render()
