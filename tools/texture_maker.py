import tkinter as tk
from tkinter import colorchooser, filedialog
from PIL import Image

GRID_SIZE = 16
PIXEL_SIZE = 20  # size of each square in the GUI

class PixelArtEditor:
    def __init__(self, root):
        self.root = root
        self.root.title("16x16 Texture Maker")

        self.canvas = tk.Canvas(root, width=GRID_SIZE*PIXEL_SIZE, height=GRID_SIZE*PIXEL_SIZE)
        self.canvas.pack()

        self.current_color = "#000000"  # default black
        self.pixels = [[self.current_color for _ in range(GRID_SIZE)] for _ in range(GRID_SIZE)]

        self.rectangles = {}
        for y in range(GRID_SIZE):
            for x in range(GRID_SIZE):
                rect = self.canvas.create_rectangle(
                    x*PIXEL_SIZE, y*PIXEL_SIZE,
                    (x+1)*PIXEL_SIZE, (y+1)*PIXEL_SIZE,
                    fill="white", outline="gray"
                )
                self.rectangles[rect] = (x, y)

        self.canvas.bind("<Button-1>", self.paint_pixel)

        button_frame = tk.Frame(root)
        button_frame.pack(pady=5)

        tk.Button(button_frame, text="Pick Color", command=self.choose_color).pack(side=tk.LEFT, padx=5)
        tk.Button(button_frame, text="Save PNG", command=self.save_png).pack(side=tk.LEFT, padx=5)

    def choose_color(self):
        color_code = colorchooser.askcolor(title="Choose color")
        if color_code[1]:
            self.current_color = color_code[1]

    def paint_pixel(self, event):
        rect = self.canvas.find_closest(event.x, event.y)[0]
        x, y = self.rectangles[rect]
        self.canvas.itemconfig(rect, fill=self.current_color)
        self.pixels[y][x] = self.current_color

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
