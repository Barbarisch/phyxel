import tkinter as tk
from tkinter import colorchooser, filedialog, messagebox
from PIL import Image
import copy

GRID_SIZE = 16
PIXEL_SIZE = 20  # size of each square in the GUI
DEFAULT_PALETTE = [
    "#000000", "#FFFFFF", "#FF0000", "#00FF00", "#0000FF", "#FFFF00", 
    "#FF00FF", "#00FFFF", "#800000", "#008000", "#000080", "#808000", 
    "#800080", "#008080", "#808080", "#C0C0C0"
]

class PixelArtEditor:
    def __init__(self, root):
        self.root = root
        self.root.title("16x16 Texture Maker")

        # State management
        self.current_color = "#000000"  # default black
        self.background_color = "#ffffff"  # default white background/eraser
        self.pixels = [[self.background_color for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]
        self.show_grid = True
        
        # Undo/Redo system
        self.history = []
        self.history_index = -1
        self.max_history = 50
        self.save_state()  # Save initial state

        # Create main frame
        main_frame = tk.Frame(root)
        main_frame.pack(padx=10, pady=10)

        # Create canvas
        self.canvas = tk.Canvas(main_frame, width=GRID_SIZE*PIXEL_SIZE, height=GRID_SIZE*PIXEL_SIZE)
        self.canvas.pack()

        self.rectangles = {}
        self.create_grid()

        self.canvas.bind("<Button-1>", self.paint_pixel)
        self.canvas.bind("<Button-3>", self.erase_pixel)  # Right-click to erase

        # Create UI panels
        self.create_toolbar(main_frame)
        self.create_color_palette(main_frame)

    def create_grid(self):
        """Create the pixel grid on the canvas"""
        self.canvas.delete("all")
        self.rectangles = {}
        
        outline_color = "gray" if self.show_grid else self.background_color
        
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                rect = self.canvas.create_rectangle(
                    x*PIXEL_SIZE, y*PIXEL_SIZE,
                    (x+1)*PIXEL_SIZE, (y+1)*PIXEL_SIZE,
                    fill=self.pixels[y][x], outline=outline_color
                )
                self.rectangles[rect] = (x, y)

    def create_toolbar(self, parent):
        """Create the toolbar with main buttons"""
        toolbar_frame = tk.Frame(parent)
        toolbar_frame.pack(pady=5)

        # File operations
        file_frame = tk.Frame(toolbar_frame)
        file_frame.pack(side=tk.LEFT, padx=5)
        tk.Button(file_frame, text="Load PNG", command=self.load_png).pack(side=tk.LEFT, padx=2)
        tk.Button(file_frame, text="Save PNG", command=self.save_png).pack(side=tk.LEFT, padx=2)

        # Color operations
        color_frame = tk.Frame(toolbar_frame)
        color_frame.pack(side=tk.LEFT, padx=5)
        tk.Button(color_frame, text="Pick Color", command=self.choose_color).pack(side=tk.LEFT, padx=2)
        tk.Button(color_frame, text="Pick Background", command=self.choose_background).pack(side=tk.LEFT, padx=2)

        # Edit operations
        edit_frame = tk.Frame(toolbar_frame)
        edit_frame.pack(side=tk.LEFT, padx=5)
        tk.Button(edit_frame, text="Undo (Ctrl+Z)", command=self.undo).pack(side=tk.LEFT, padx=2)
        tk.Button(edit_frame, text="Redo (Ctrl+Y)", command=self.redo).pack(side=tk.LEFT, padx=2)

        # View operations
        view_frame = tk.Frame(toolbar_frame)
        view_frame.pack(side=tk.LEFT, padx=5)
        tk.Button(view_frame, text="Toggle Grid", command=self.toggle_grid).pack(side=tk.LEFT, padx=2)
        tk.Button(view_frame, text="Clear All", command=self.clear_canvas).pack(side=tk.LEFT, padx=2)

        # Keyboard shortcuts
        self.root.bind("<Control-z>", lambda e: self.undo())
        self.root.bind("<Control-y>", lambda e: self.redo())
        self.root.focus_set()  # Enable keyboard shortcuts

    def create_color_palette(self, parent):
        """Create a color palette for quick color selection"""
        palette_frame = tk.Frame(parent)
        palette_frame.pack(pady=5)
        
        tk.Label(palette_frame, text="Color Palette:").pack()
        
        colors_frame = tk.Frame(palette_frame)
        colors_frame.pack()
        
        self.palette_buttons = []
        for i, color in enumerate(DEFAULT_PALETTE):
            btn = tk.Button(colors_frame, width=3, height=2, bg=color,
                          command=lambda c=color: self.set_color(c))
            btn.pack(side=tk.LEFT, padx=1, pady=1)
            self.palette_buttons.append(btn)

    def save_state(self):
        """Save current state for undo/redo"""
        # Remove any states after current index (for when we undo then make new changes)
        self.history = self.history[:self.history_index + 1]
        
        # Add new state
        state = copy.deepcopy(self.pixels)
        self.history.append(state)
        self.history_index += 1
        
        # Limit history size
        if len(self.history) > self.max_history:
            self.history.pop(0)
            self.history_index -= 1

    def undo(self):
        """Undo last action"""
        if self.history_index > 0:
            self.history_index -= 1
            self.pixels = copy.deepcopy(self.history[self.history_index])
            self.create_grid()

    def redo(self):
        """Redo last undone action"""
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
            self.pixels = copy.deepcopy(self.history[self.history_index])
            self.create_grid()

    def toggle_grid(self):
        """Toggle grid visibility"""
        self.show_grid = not self.show_grid
        self.create_grid()

    def clear_canvas(self):
        """Clear the entire canvas"""
        if messagebox.askyesno("Clear Canvas", "Are you sure you want to clear the entire canvas?"):
            self.pixels = [[self.background_color for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]
            self.create_grid()
            self.save_state()

    def set_color(self, color):
        """Set current color from palette"""
        self.current_color = color

    def choose_color(self):
        color_code = colorchooser.askcolor(title="Choose color")
        if color_code[1]:
            self.current_color = color_code[1]

    def choose_background(self):
        color_code = colorchooser.askcolor(title="Choose background/eraser color")
        if color_code[1]:
            self.background_color = color_code[1]

    def paint_pixel(self, event):
        rect = self.canvas.find_closest(event.x, event.y)[0]
        if rect in self.rectangles:
            x, y = self.rectangles[rect]
            if self.pixels[y][x] != self.current_color:  # Only save state if change occurs
                self.canvas.itemconfig(rect, fill=self.current_color)
                self.pixels[y][x] = self.current_color
                self.save_state()

    def erase_pixel(self, event):
        rect = self.canvas.find_closest(event.x, event.y)[0]
        if rect in self.rectangles:
            x, y = self.rectangles[rect]
            if self.pixels[y][x] != self.background_color:  # Only save state if change occurs
                self.canvas.itemconfig(rect, fill=self.background_color)
                self.pixels[y][x] = self.background_color
                self.save_state()

    def load_png(self):
        """Load an existing PNG file"""
        file_path = filedialog.askopenfilename(
            title="Load PNG file",
            filetypes=[("PNG files", "*.png"), ("All files", "*.*")]
        )
        if file_path:
            try:
                img = Image.open(file_path)
                # Resize to 16x16 if needed
                if img.size != (GRID_SIZE, GRID_SIZE):
                    img = img.resize((GRID_SIZE, GRID_SIZE), Image.NEAREST)
                
                # Convert to RGB if needed
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                
                # Load pixels into grid
                for y in range(GRID_SIZE):
                    for x in range(GRID_SIZE):
                        r, g, b = img.getpixel((x, y))
                        hex_color = f"#{r:02x}{g:02x}{b:02x}"
                        self.pixels[y][x] = hex_color
                
                self.create_grid()
                self.save_state()
                print(f"Loaded {file_path}")
                
            except Exception as e:
                messagebox.showerror("Error", f"Failed to load image: {str(e)}")

    def save_png(self):
        file_path = filedialog.asksaveasfilename(defaultextension=".png",
                                                 filetypes=[("PNG files", "*.png")])
        if file_path:
            img = Image.new("RGB", (GRID_SIZE, GRID_SIZE))
            for y in range(GRID_SIZE):
                for x in range(GRID_SIZE):
                    img.putpixel((x, y), self.hex_to_rgb(self.pixels[y][x]))
            img = img.resize((GRID_SIZE, GRID_SIZE), Image.NEAREST)
            img.save(file_path)
            print(f"Saved {file_path}")

    @staticmethod
    def hex_to_rgb(hex_color):
        hex_color = hex_color.lstrip("#")
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))


if __name__ == "__main__":
    root = tk.Tk()
    app = PixelArtEditor(root)
    root.mainloop()
