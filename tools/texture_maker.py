import tkinter as tk
from tkinter import colorchooser, filedialog, messagebox, simpledialog
from PIL import Image
import copy
import argparse
import sys

PIXEL_SIZE = 20  # size of each square in the GUI
# MS Paint classic color palette (28 colors in 2 rows)
DEFAULT_PALETTE = [
    # Top row
    "#000000", "#808080", "#800000", "#808000", "#008000", "#008080", "#000080", "#800080",
    "#808040", "#004040", "#0080FF", "#004080", "#8000FF", "#804000", 
    # Bottom row  
    "#FFFFFF", "#C0C0C0", "#FF0000", "#FFFF00", "#00FF00", "#00FFFF", "#0000FF", "#FF00FF",
    "#FFFF80", "#00FF80", "#80FFFF", "#8080FF", "#FF0080", "#FF8040"
]

class PixelArtEditor:
    def __init__(self, root, initial_size=18):
        self.root = root
        
        # Size management
        self.grid_size = initial_size  # Use provided initial size
        self.update_title()

        # State management
        self.current_color = "#000000"  # default black
        self.background_color = "#ffffff"  # default white background/eraser
        self.pixels = [[self.background_color for _ in range(self.grid_size)] for _ in range(self.grid_size)]
        self.show_grid = True
        
        # Mouse drag state
        self.is_dragging = False
        self.drag_mode = None  # 'paint' or 'erase'
        self.drag_changed = False  # Track if any changes occurred during drag
        
        # Undo/Redo system
        self.history = []
        self.history_index = -1
        self.max_history = 50
        self.save_state()  # Save initial state

        # Create main frame
        main_frame = tk.Frame(root)
        main_frame.pack(padx=10, pady=10)

        # Create canvas
        self.canvas = tk.Canvas(main_frame, width=self.grid_size*PIXEL_SIZE, height=self.grid_size*PIXEL_SIZE)
        self.canvas.pack()

        self.rectangles = {}
        self.create_grid()

        self.canvas.bind("<Button-1>", self.start_paint)
        self.canvas.bind("<Button-3>", self.start_erase)  # Right-click to erase
        self.canvas.bind("<B1-Motion>", self.drag_paint)  # Left mouse drag
        self.canvas.bind("<B3-Motion>", self.drag_erase)  # Right mouse drag
        self.canvas.bind("<ButtonRelease-1>", self.end_drag)  # Left mouse release
        self.canvas.bind("<ButtonRelease-3>", self.end_drag)  # Right mouse release

        # Create UI panels
        self.create_toolbar(main_frame)
        self.create_color_palette(main_frame)

    def update_title(self):
        """Update window title with current canvas size"""
        self.root.title(f"{self.grid_size}x{self.grid_size} Texture Maker")

    def on_size_change(self, event=None):
        """Handle size change from text field"""
        try:
            new_size = int(self.size_var.get())
            if new_size < 1:
                messagebox.showerror("Invalid Size", "Size must be at least 1")
                self.size_var.set(str(self.grid_size))  # Reset to current size
                return
            if new_size > 512:
                messagebox.showerror("Invalid Size", "Size cannot exceed 512")
                self.size_var.set(str(self.grid_size))  # Reset to current size
                return
            
            # Only resize if size actually changed
            if new_size != self.grid_size:
                self.resize_canvas(new_size)
            
        except ValueError:
            messagebox.showerror("Invalid Size", "Please enter a valid number")
            self.size_var.set(str(self.grid_size))  # Reset to current size

    def resize_canvas(self, new_size):
        """Resize the canvas to a new size"""
        if messagebox.askyesno("Resize Canvas", 
                               f"Change canvas size to {new_size}x{new_size}?\nThis will clear your current work."):
            self.grid_size = new_size
            self.size_var.set(str(new_size))  # Update text field
            self.pixels = [[self.background_color for _ in range(self.grid_size)] for _ in range(self.grid_size)]
            
            # Update canvas size
            self.canvas.config(width=self.grid_size*PIXEL_SIZE, height=self.grid_size*PIXEL_SIZE)
            
            # Clear history and save new state
            self.history = []
            self.history_index = -1
            self.save_state()
            
            # Recreate grid and update title
            self.create_grid()
            self.update_title()
        else:
            # User cancelled, reset text field to current size
            self.size_var.set(str(self.grid_size))

    def custom_size_dialog(self):
        """Show dialog to enter custom grid size"""
        new_size = simpledialog.askinteger(
            "Custom Size", 
            "Enter grid size (1-512):",
            initialvalue=self.grid_size,
            minvalue=1,
            maxvalue=512
        )
        if new_size:
            self.resize_canvas(new_size)

    def create_grid(self):
        """Create the pixel grid on the canvas"""
        self.canvas.delete("all")
        self.rectangles = {}
        
        outline_color = "gray" if self.show_grid else self.background_color
        
        for y in range(self.grid_size):
            for x in range(self.grid_size):
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

        # Size selection
        size_frame = tk.Frame(toolbar_frame)
        size_frame.pack(side=tk.LEFT, padx=5)
        tk.Label(size_frame, text="Size:").pack(side=tk.LEFT)
        
        # Size input field
        self.size_var = tk.StringVar(value=str(self.grid_size))
        self.size_entry = tk.Entry(size_frame, textvariable=self.size_var, width=6)
        self.size_entry.pack(side=tk.LEFT, padx=2)
        self.size_entry.bind('<Return>', self.on_size_change)
        self.size_entry.bind('<FocusOut>', self.on_size_change)
        
        # Resize button
        tk.Button(size_frame, text="Resize", 
                 command=self.on_size_change).pack(side=tk.LEFT, padx=2)

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
        
        tk.Label(palette_frame, text="Color Palette (MS Paint style):").pack()
        
        colors_frame = tk.Frame(palette_frame)
        colors_frame.pack()
        
        self.palette_buttons = []
        
        # Create two rows of colors like MS Paint
        colors_per_row = 14  # 14 colors per row
        
        # Top row
        top_row_frame = tk.Frame(colors_frame)
        top_row_frame.pack()
        for i in range(colors_per_row):
            if i < len(DEFAULT_PALETTE):
                color = DEFAULT_PALETTE[i]
                btn = tk.Button(top_row_frame, width=3, height=2, bg=color,
                              command=lambda c=color: self.set_color(c))
                btn.pack(side=tk.LEFT, padx=1, pady=1)
                self.palette_buttons.append(btn)
        
        # Bottom row
        bottom_row_frame = tk.Frame(colors_frame)
        bottom_row_frame.pack()
        for i in range(colors_per_row, len(DEFAULT_PALETTE)):
            color = DEFAULT_PALETTE[i]
            btn = tk.Button(bottom_row_frame, width=3, height=2, bg=color,
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
            self.pixels = [[self.background_color for _ in range(self.grid_size)] for _ in range(self.grid_size)]
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

    def start_paint(self, event):
        """Start painting with left mouse button"""
        self.is_dragging = True
        self.drag_mode = 'paint'
        self.drag_changed = False
        self.paint_at_position(event.x, event.y)

    def start_erase(self, event):
        """Start erasing with right mouse button"""
        self.is_dragging = True
        self.drag_mode = 'erase'
        self.drag_changed = False
        self.erase_at_position(event.x, event.y)

    def drag_paint(self, event):
        """Continue painting while dragging left mouse"""
        if self.is_dragging and self.drag_mode == 'paint':
            self.paint_at_position(event.x, event.y)

    def drag_erase(self, event):
        """Continue erasing while dragging right mouse"""
        if self.is_dragging and self.drag_mode == 'erase':
            self.erase_at_position(event.x, event.y)

    def end_drag(self, event):
        """End drag operation and save state if changes occurred"""
        if self.is_dragging and self.drag_changed:
            self.save_state()
        self.is_dragging = False
        self.drag_mode = None
        self.drag_changed = False

    def paint_at_position(self, x, y):
        """Paint pixel at given canvas coordinates"""
        rect = self.canvas.find_closest(x, y)[0]
        if rect in self.rectangles:
            grid_x, grid_y = self.rectangles[rect]
            if self.pixels[grid_y][grid_x] != self.current_color:
                self.canvas.itemconfig(rect, fill=self.current_color)
                self.pixels[grid_y][grid_x] = self.current_color
                self.drag_changed = True

    def erase_at_position(self, x, y):
        """Erase pixel at given canvas coordinates"""
        rect = self.canvas.find_closest(x, y)[0]
        if rect in self.rectangles:
            grid_x, grid_y = self.rectangles[rect]
            if self.pixels[grid_y][grid_x] != self.background_color:
                self.canvas.itemconfig(rect, fill=self.background_color)
                self.pixels[grid_y][grid_x] = self.background_color
                self.drag_changed = True

    def load_png(self):
        """Load an existing PNG file"""
        file_path = filedialog.askopenfilename(
            title="Load PNG file",
            filetypes=[("PNG files", "*.png"), ("All files", "*.*")]
        )
        if file_path:
            try:
                img = Image.open(file_path)
                # Resize to current grid size if needed
                if img.size != (self.grid_size, self.grid_size):
                    img = img.resize((self.grid_size, self.grid_size), Image.NEAREST)
                
                # Convert to RGB if needed
                if img.mode != 'RGB':
                    img = img.convert('RGB')
                
                # Load pixels into grid
                for y in range(self.grid_size):
                    for x in range(self.grid_size):
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
            img = Image.new("RGB", (self.grid_size, self.grid_size))
            for y in range(self.grid_size):
                for x in range(self.grid_size):
                    img.putpixel((x, y), self.hex_to_rgb(self.pixels[y][x]))
            img = img.resize((self.grid_size, self.grid_size), Image.NEAREST)
            img.save(file_path)
            print(f"Saved {file_path}")

    @staticmethod
    def hex_to_rgb(hex_color):
        hex_color = hex_color.lstrip("#")
        return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))


if __name__ == "__main__":
    # Parse command-line arguments
    parser = argparse.ArgumentParser(description='Pixel Art Texture Maker')
    parser.add_argument('--size', '-s', type=int, default=18, 
                       help='Initial grid size (default: 18)')
    parser.add_argument('--max-size', type=int, default=512,
                       help='Maximum allowed grid size (default: 512)')
    
    # If no arguments provided, check if there's a single number argument
    if len(sys.argv) == 2 and sys.argv[1].isdigit():
        grid_size = int(sys.argv[1])
    else:
        args = parser.parse_args()
        grid_size = args.size
    
    # Validate size
    if grid_size < 1:
        print("Error: Grid size must be at least 1")
        sys.exit(1)
    if grid_size > 512:
        print("Error: Grid size too large (max 512)")
        sys.exit(1)
    
    root = tk.Tk()
    app = PixelArtEditor(root, initial_size=grid_size)
    root.mainloop()
