#!/usr/bin/env python3
"""
Generador de fuentes bitmap (rejilla WxH, de 1x1 a 16x16) con
exportación/carga sin pérdida de código C y vista previa en vivo.

Formato C (cada fila es un entero sin signo de hasta 16 bits; el bit más
significativo es la columna izquierda, igual que { 'A', { 14,17,17,31,17,17,17 } }):

    /* font_dims: 16x16 */
    static const Glyph font_16x16[] = {
        { 'A', { 29127,36408,0,32640,49407,0,0,1984,7264,12336,9104,9096,12296,7224,992,0 } },
        ...
    };

Admite múltiples tamaños de fuente simultáneos en un mismo programa SDL3/DOS
(por ejemplo 4x6 diálogos sobre personajes, 8x10 menús y 16x16 títulos),
además de minúsculas (a-z) y caracteres en español (áéíóúüñÁÉÍÓÚÜÑ¿¡)
exportados como puntos de código hexadecimales ASCII puros (0x00F1, etc.).

Solo usa la biblioteca estándar (tkinter).
by @mmoroca & Arena.AI 2026 (http://arena.ai/)
"""
import re
import tkinter as tk
from tkinter import filedialog, messagebox

DEFAULT_CHARSET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?:;=.,-+/_%[]()<>|*"
LOWERCASE_CHARS = "abcdefghijklmnopqrstuvwxyz"
SPANISH_CHARS = "áéíóúüñÁÉÍÓÚÜÑ¿¡"

LIST_COLS = 4        # columnas de la lista de caracteres
CELL = 34            # tamaño base de casilla en píxeles
COLOR_ON = "#222222"
COLOR_OFF = "#f4f4f4"
COLOR_DONE = "#9be39b"
COLOR_MISSING = "#f2a6a6"
COLOR_SEL = "#1a4fd6"

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0",
           "\\": "\\", "'": "'", '"': '"'}

GLYPH_RE = re.compile(
    r"\{\s*(?:'((?:\\.[0-9a-fA-F]*|[^'\\])+)'|(0x[0-9a-fA-F]+|\d+))\s*,\s*\{([^{}]*)\}\s*\}"
)
HEADER_RE = re.compile(r"static\s+const\s+(\w+)\s+(\w+)\s*\[\s*\]")
DIMS_COMMENT_RE = re.compile(r"font_dims:\s*(\d+)x(\d+)")
NAME_DIMS_RE = re.compile(r"(?:^|_)(\d+)x(\d+)(?:$|_)")


# ---------------------------------------------------------------- utilidades
def decode_char(char_lit, num_lit):
    """Convierte un literal C ('A', '\\'', 0x00F1, 65...) a carácter Python."""
    if num_lit:
        code = int(num_lit, 0)
        return chr(code) if 0 <= code <= 0xFFFF else None
    if len(char_lit) == 1:
        return char_lit
    if char_lit[0] == "\\":
        if char_lit[1] in ESCAPES and len(char_lit) == 2:
            return ESCAPES[char_lit[1]]
        if char_lit[1] == "x":
            return chr(int(char_lit[2:], 16))
    return None


def encode_char_key(ch):
    """Devuelve literal ASCII puro sin extensión de signo para C (1..16 bits)."""
    if ch == "'":
        return "'\\''"
    if ch == "\\":
        return "'\\\\'"
    if ch == "\n":
        return "'\\n'"
    if ch == "\t":
        return "'\\t'"
    if ch == "\0":
        return "'\\0'"
    code = ord(ch)
    if 32 <= code <= 126:
        return "'%s'" % ch
    return "0x%04X" % code


def blank(w, h):
    return [[0] * w for _ in range(h)]


def reshape(m, w, h):
    return [[(m[y][x] if y < len(m) and x < len(m[0]) else 0)
             for x in range(w)] for y in range(h)]


def matrix_to_rows(m, w):
    return [sum(bit << (w - 1 - x) for x, bit in enumerate(row)) for row in m]


def rows_to_matrix(rows, w, h):
    rows = list(rows[:h]) + [0] * max(0, h - len(rows))
    return [[(rows[y] >> (w - 1 - x)) & 1 for x in range(w)] for y in range(h)]


def is_empty(m):
    return not any(any(r) for r in m)


def parse_c_font(raw_text, fallback_w=5):
    """Extrae (struct_name, array_name, w, h, [(ch, rows), ...]) de código C."""
    dims_match = DIMS_COMMENT_RE.search(raw_text)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", raw_text, flags=re.S)
    hdr = HEADER_RE.search(text)
    glyphs = []
    for m in GLYPH_RE.finditer(text):
        ch = decode_char(m.group(1), m.group(2))
        if ch is None:
            continue
        try:
            vals = [int(v.strip(), 0) for v in m.group(3).split(",") if v.strip()]
        except ValueError:
            continue
        glyphs.append((ch, vals))
    if not glyphs:
        return None

    struct_name = hdr.group(1) if hdr else "Glyph"
    array_name = hdr.group(2) if hdr else "font"

    if dims_match:
        nw, nh = int(dims_match.group(1)), int(dims_match.group(2))
    else:
        name_dims = NAME_DIMS_RE.search(array_name)
        if name_dims:
            nw, nh = int(name_dims.group(1)), int(name_dims.group(2))
        else:
            nh = max(len(v) for _, v in glyphs)
            max_bits = max((max(v).bit_length() if v else 1) for _, v in glyphs)
            nw = max(fallback_w if max_bits <= fallback_w <= 8 else 1, max_bits)

    nw = max(1, min(16, nw))
    nh = max(1, min(16, nh))
    return struct_name, array_name, nw, nh, glyphs


# ----------------------------------------------------------------- aplicación
class FontEditor:
    def __init__(self, root):
        self.root = root
        root.title("Generador de fuentes bitmap")
        self.w, self.h = 5, 7
        self.charset = list(DEFAULT_CHARSET)
        self.data = {}      # caracteres validados con "OK"
        self.draft = {}     # ediciones sin validar
        self.cur = self.charset[0]
        self.grid = blank(self.w, self.h)
        self.tiles = {}
        self.rects = []
        self.paint_value = 1

        self.struct_var = tk.StringVar(value="Glyph")
        self.name_var = tk.StringVar(value="font_5x7")
        self.typedef_var = tk.BooleanVar(value=False)
        self.multifont_var = tk.BooleanVar(value=True)
        self.w_var = tk.IntVar(value=self.w)
        self.h_var = tk.IntVar(value=self.h)
        self.add_var = tk.StringVar()
        self.sample_var = tk.StringVar(value="AVENTURA RPG 2026!")
        self.scale_var = tk.IntVar(value=2)
        self.status_var = tk.StringVar()

        self.build_ui()
        self.rebuild_grid()
        self.rebuild_list()
        self.select(self.cur, save=False)

    # ------------------------------------------------------------------ UI
    def build_ui(self):
        top = tk.Frame(self.root)
        top.pack(fill="x", padx=8, pady=6)
        tk.Button(top, text="Cargar C…", command=self.load_c).pack(side="left")
        tk.Button(top, text="Exportar C…", command=self.export_c).pack(side="left", padx=4)
        tk.Label(top, text="  struct:").pack(side="left")
        tk.Entry(top, textvariable=self.struct_var, width=9).pack(side="left")
        tk.Label(top, text="array:").pack(side="left", padx=(8, 0))
        tk.Entry(top, textvariable=self.name_var, width=12).pack(side="left")
        tk.Checkbutton(top, text="incluir typedef",
                       variable=self.typedef_var).pack(side="left", padx=6)
        tk.Checkbutton(top, text="rows[16] multi-fuente",
                       variable=self.multifont_var).pack(side="left", padx=4)

        main = tk.Frame(self.root)
        main.pack(fill="both", expand=True, padx=8, pady=4)

        # --- izquierda: rejilla + vista previa en vivo
        left = tk.Frame(main)
        left.pack(side="left", anchor="n")

        size = tk.Frame(left)
        size.pack(anchor="w", pady=(0, 6))
        tk.Label(size, text="Ancho").pack(side="left")
        tk.Spinbox(size, from_=1, to=16, width=3, textvariable=self.w_var).pack(side="left", padx=3)
        tk.Label(size, text="Alto").pack(side="left")
        tk.Spinbox(size, from_=1, to=16, width=3, textvariable=self.h_var).pack(side="left", padx=3)
        tk.Button(size, text="Aplicar tamaño", command=self.apply_size).pack(side="left", padx=6)

        self.cur_label = tk.Label(left, text="", font=("Courier", 15, "bold"))
        self.cur_label.pack(anchor="w")

        self.canvas = tk.Canvas(left, highlightthickness=1, highlightbackground="#888")
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self.on_press)
        self.canvas.bind("<B1-Motion>", self.on_drag)

        btns = tk.Frame(left)
        btns.pack(fill="x", pady=6)
        tk.Button(btns, text="Borrar", width=9, command=self.clear).pack(side="left")
        tk.Button(btns, text="OK", width=9, command=self.ok).pack(side="left", padx=6)
        tk.Button(btns, text="+ a-z", command=lambda: self.add_preset(LOWERCASE_CHARS)).pack(side="left", padx=2)
        tk.Button(btns, text="+ Español", command=lambda: self.add_preset(SPANISH_CHARS)).pack(side="left", padx=2)

        self.rows_label = tk.Label(left, text="", font=("Courier", 10), fg="#444",
                                   wraplength=540, justify="left")
        self.rows_label.pack(anchor="w")

        add = tk.Frame(left)
        add.pack(anchor="w", pady=(8, 0))
        tk.Label(add, text="Añadir carácter:").pack(side="left")
        e = tk.Entry(add, textvariable=self.add_var, width=4)
        e.pack(side="left", padx=4)
        e.bind("<Return>", lambda _e: self.add_char())
        tk.Button(add, text="Añadir", command=self.add_char).pack(side="left")

        preview_bar = tk.Frame(left)
        preview_bar.pack(anchor="w", pady=(10, 2))
        tk.Label(preview_bar, text="Prueba:").pack(side="left")
        pe = tk.Entry(preview_bar, textvariable=self.sample_var, width=28)
        pe.pack(side="left", padx=4)
        self.sample_var.trace_add("write", lambda *_: self.redraw_preview())
        tk.Label(preview_bar, text="Zoom:").pack(side="left", padx=(6, 2))
        tk.Spinbox(preview_bar, from_=1, to=4, width=2, textvariable=self.scale_var,
                   command=self.redraw_preview).pack(side="left")

        self.preview = tk.Canvas(left, width=540, height=76, bg="#000000",
                                 highlightthickness=1, highlightbackground="#444")
        self.preview.pack(anchor="w", pady=(2, 0))

        tk.Label(left, textvariable=self.status_var, fg="#444").pack(anchor="w", pady=(8, 0))

        # --- derecha: lista de caracteres con scroll
        right = tk.Frame(main)
        right.pack(side="left", fill="y", padx=(16, 0))
        self.list_canvas = tk.Canvas(right, width=LIST_COLS * 62, height=580, highlightthickness=0)
        sb = tk.Scrollbar(right, orient="vertical", command=self.list_canvas.yview)
        self.list_canvas.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.list_canvas.pack(side="left", fill="y")
        self.list_frame = tk.Frame(self.list_canvas)
        self.list_canvas.create_window((0, 0), window=self.list_frame, anchor="nw")
        self.list_frame.bind("<Configure>", lambda _e: self.list_canvas.configure(
            scrollregion=self.list_canvas.bbox("all")))
        for w in (self.list_canvas, self.list_frame):
            w.bind("<Enter>", self.bind_wheel)
            w.bind("<Leave>", self.unbind_wheel)

    def bind_wheel(self, _e):
        self.root.bind_all("<MouseWheel>", self.on_wheel)
        self.root.bind_all("<Button-4>", lambda e: self.list_canvas.yview_scroll(-2, "units"))
        self.root.bind_all("<Button-5>", lambda e: self.list_canvas.yview_scroll(2, "units"))

    def unbind_wheel(self, _e):
        self.root.unbind_all("<MouseWheel>")
        self.root.unbind_all("<Button-4>")
        self.root.unbind_all("<Button-5>")

    def on_wheel(self, e):
        step = -1 if e.delta > 0 else 1
        if abs(e.delta) >= 120:
            step *= abs(e.delta) // 120
        self.list_canvas.yview_scroll(step, "units")

    # ------------------------------------------------------------- rejilla
    def cell_px(self):
        return max(22, min(CELL, 520 // max(self.w, self.h)))

    def rebuild_grid(self):
        cpx = self.cell_px()
        self.canvas.delete("all")
        self.canvas.configure(width=self.w * cpx, height=self.h * cpx)
        self.rects = []
        for y in range(self.h):
            row = []
            for x in range(self.w):
                r = self.canvas.create_rectangle(
                    x * cpx, y * cpx, (x + 1) * cpx, (y + 1) * cpx,
                    fill=COLOR_ON if self.grid[y][x] else COLOR_OFF, outline="#999")
                row.append(r)
            self.rects.append(row)

    def redraw_grid(self):
        for y in range(self.h):
            for x in range(self.w):
                self.canvas.itemconfig(self.rects[y][x],
                                       fill=COLOR_ON if self.grid[y][x] else COLOR_OFF)
        self.update_rows_label()
        self.redraw_preview()

    def update_rows_label(self):
        rows = matrix_to_rows(self.grid, self.w)
        self.rows_label.config(text="{ " + ",".join(map(str, rows)) + " }")

    def glyph_for_preview(self, ch):
        if ch == self.cur:
            return self.grid
        if ch in self.draft:
            return self.draft[ch]
        if ch in self.data:
            return self.data[ch]
        up = ch.upper()
        if up == self.cur:
            return self.grid
        return self.draft.get(up) or self.data.get(up)

    def redraw_preview(self):
        self.preview.delete("all")
        try:
            zoom = max(1, min(4, int(self.scale_var.get())))
        except (tk.TclError, ValueError):
            zoom = 2
        cx, cy = 8, 8
        for ch in self.sample_var.get():
            if ch == "\n":
                cx = 8
                cy += (self.h + 2) * zoom
                continue
            if ch == " ":
                cx += max(2, self.w) * zoom
                continue
            mat = self.glyph_for_preview(ch)
            if mat:
                for y in range(self.h):
                    for x in range(self.w):
                        if mat[y][x]:
                            px = cx + x * zoom
                            py = cy + y * zoom
                            self.preview.create_rectangle(
                                px, py, px + zoom, py + zoom,
                                fill="#38f2ff", outline="")
            cx += (self.w + 1) * zoom

    def cell_at(self, event):
        cpx = self.cell_px()
        x, y = event.x // cpx, event.y // cpx
        if 0 <= x < self.w and 0 <= y < self.h:
            return x, y
        return None

    def set_cell(self, x, y, value):
        if self.grid[y][x] != value:
            self.grid[y][x] = value
            self.canvas.itemconfig(self.rects[y][x], fill=COLOR_ON if value else COLOR_OFF)
            self.update_rows_label()
            self.redraw_preview()

    def on_press(self, event):
        c = self.cell_at(event)
        if c:
            x, y = c
            self.paint_value = 0 if self.grid[y][x] else 1
            self.set_cell(x, y, self.paint_value)

    def on_drag(self, event):
        c = self.cell_at(event)
        if c:
            self.set_cell(c[0], c[1], self.paint_value)

    # --------------------------------------------------------------- lista
    def display(self, ch):
        return "␣" if ch == " " else ch

    def rebuild_list(self):
        for child in self.list_frame.winfo_children():
            child.destroy()
        self.tiles = {}
        for i, ch in enumerate(self.charset):
            lab = tk.Label(self.list_frame, text=self.display(ch), width=3,
                           font=("Courier", 14, "bold"), highlightthickness=3, bd=1,
                           relief="solid")
            lab.grid(row=i // LIST_COLS, column=i % LIST_COLS, padx=2, pady=2)
            lab.bind("<Button-1>", lambda _e, c=ch: self.select(c))
            self.tiles[ch] = lab
            self.style_tile(ch)
        self.update_status()

    def style_tile(self, ch):
        lab = self.tiles.get(ch)
        if not lab:
            return
        bg = COLOR_DONE if ch in self.data else COLOR_MISSING
        border = COLOR_SEL if ch == self.cur else bg
        lab.config(bg=bg, highlightbackground=border, highlightcolor=border)

    def update_status(self):
        self.status_var.set("Creados: %d / %d  (%dx%d)" % (
            len(self.data), len(self.charset), self.w, self.h))

    # ------------------------------------------------------------ acciones
    def save_draft(self):
        ch = self.cur
        if ch in self.data and self.grid == self.data[ch]:
            self.draft.pop(ch, None)
        elif is_empty(self.grid) and ch not in self.data:
            self.draft.pop(ch, None)
        else:
            self.draft[ch] = [row[:] for row in self.grid]

    def select(self, ch, save=True):
        if save:
            self.save_draft()
        old = self.cur
        self.cur = ch
        src = self.draft.get(ch) or self.data.get(ch) or blank(self.w, self.h)
        self.grid = [row[:] for row in src]
        self.cur_label.config(text="Carácter: %s  (U+%04X / %d)" % (
            self.display(ch), ord(ch), ord(ch)))
        self.redraw_grid()
        self.style_tile(old)
        self.style_tile(ch)

    def clear(self):
        self.grid = blank(self.w, self.h)
        self.redraw_grid()

    def ok(self):
        ch = self.cur
        self.data[ch] = [row[:] for row in self.grid]
        self.draft.pop(ch, None)
        self.style_tile(ch)
        self.update_status()
        n = len(self.charset)
        start = self.charset.index(ch)
        for k in range(1, n + 1):
            nxt = self.charset[(start + k) % n]
            if nxt not in self.data:
                self.select(nxt)
                return

    def add_preset(self, chars):
        added = False
        for ch in chars:
            if ch not in self.charset:
                self.charset.append(ch)
                added = True
        if added:
            self.rebuild_list()

    def add_char(self):
        s = self.add_var.get()
        self.add_var.set("")
        if len(s) != 1:
            return
        if s not in self.charset:
            self.charset.append(s)
            self.rebuild_list()
        self.select(s)

    def apply_size(self):
        try:
            nw, nh = int(self.w_var.get()), int(self.h_var.get())
        except (tk.TclError, ValueError):
            return
        nw, nh = max(1, min(16, nw)), max(1, min(16, nh))
        if (nw, nh) == (self.w, self.h):
            return
        if (self.data or self.draft) and (nw < self.w or nh < self.h):
            if not messagebox.askyesno(
                    "Reducir tamaño",
                    "Reducir la rejilla recortará los caracteres ya creados. ¿Continuar?"):
                self.w_var.set(self.w)
                self.h_var.set(self.h)
                return
        self.resize(nw, nh)

    def resize(self, nw, nh):
        self.save_draft()
        for store in (self.data, self.draft):
            for ch in list(store):
                store[ch] = reshape(store[ch], nw, nh)
        old_auto = "font_%dx%d" % (self.w, self.h)
        if self.name_var.get() == old_auto:
            self.name_var.set("font_%dx%d" % (nw, nh))
        self.grid = reshape(self.grid, nw, nh)
        self.w, self.h = nw, nh
        self.w_var.set(nw)
        self.h_var.set(nh)
        self.rebuild_grid()
        self.select(self.cur, save=False)
        self.update_status()

    # ------------------------------------------------------------ cargar C
    def load_c(self):
        path = filedialog.askopenfilename(
            title="Cargar código C",
            filetypes=[("Código C", "*.c *.h *.txt"), ("Todos", "*.*")])
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                raw_text = f.read()
        except OSError as e:
            messagebox.showerror("Error", str(e))
            return

        parsed = parse_c_font(raw_text, self.w)
        if not parsed:
            messagebox.showwarning("Sin datos", "No se encontraron glifos con el formato esperado.")
            return

        struct_name, array_name, nw, nh, glyphs = parsed
        self.struct_var.set(struct_name)
        self.name_var.set(array_name)

        self.save_draft()
        self.data = {}
        self.draft = {}
        self.resize(nw, nh)
        for ch, vals in glyphs:
            if ch not in self.charset:
                self.charset.append(ch)
            m = rows_to_matrix(vals, self.w, self.h)
            if not is_empty(m):
                self.data[ch] = m
        self.rebuild_list()
        self.select(self.charset[0], save=False)
        self.status_var.set("Cargados %d glifos (%d con datos, %dx%d) de %s" % (
            len(glyphs), len(self.data), self.w, self.h,
            path.split("/")[-1].split("\\")[-1]))

    # ----------------------------------------------------------- exportar C
    def build_c(self):
        self.save_draft()
        struct = self.struct_var.get().strip() or "Glyph"
        name = self.name_var.get().strip() or "font"
        entries = []
        for ch in self.charset:
            if ch in self.data:
                rows = matrix_to_rows(self.data[ch], self.w)
            else:
                rows = [0] * self.h
            entries.append("{ %s, { %s } }" % (
                encode_char_key(ch), ",".join(map(str, rows))))

        lines = []
        for i in range(0, len(entries), 2):
            lines.append("    " + ", ".join(entries[i:i + 2]))
        body = ",\n".join(lines)

        out = "/* font_dims: %dx%d */\n" % (self.w, self.h)
        if self.typedef_var.get():
            row_cap = 16 if self.multifont_var.get() else self.h
            row_type = "unsigned short" if (self.w > 8 or self.multifont_var.get()) else "unsigned char"
            guard = "%s_DEFINED" % struct.upper()
            out += (
                "#ifndef %s\n"
                "#define %s\n"
                "typedef struct %s {\n"
                "    unsigned short codepoint;\n"
                "    %s rows[%d];\n"
                "} %s;\n"
                "#endif\n\n" % (guard, guard, struct, row_type, row_cap, struct)
            )
        out += "static const %s %s[] = {\n%s\n};\n" % (struct, name, body)
        return out

    def export_c(self):
        path = filedialog.asksaveasfilename(
            title="Exportar código C", defaultextension=".h",
            initialfile=self.name_var.get() + ".h",
            filetypes=[("Cabecera / Código C", "*.h *.c"), ("Todos", "*.*")])
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.build_c())
        except OSError as e:
            messagebox.showerror("Error", str(e))
            return
        missing = len(self.charset) - len(self.data)
        self.status_var.set("Exportado %dx%d (%d sin crear a cero)" % (
            self.w, self.h, missing))


def main():
    import sys
    root = tk.Tk()
    patch = root.tk.call("info", "patchlevel")
    print("Python %s  |  Tcl/Tk %s  |  %s" % (sys.version.split()[0], patch, sys.platform))
    try:
        ver = tuple(int(p) for p in str(patch).split(".")[:3])
    except ValueError:
        ver = (0, 0, 0)
    if sys.platform == "darwin" and ver < (8, 6, 13):
        print("AVISO: este Tcl/Tk es antiguo para macOS reciente y puede no dibujar "
              "Canvas, Label ni Entry. Usa el Python de python.org o `brew install python-tk`.")
    FontEditor(root)
    root.title("Generador de fuentes bitmap — Tk %s" % patch)
    root.mainloop()


if __name__ == "__main__":
    main()
