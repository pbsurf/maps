# ref: https://pillow.readthedocs.io/en/stable/reference/Image.html#PIL.Image.new
# - https://stackoverflow.com/questions/31826335/how-to-convert-pil-image-image-object-to-base64-string

from PIL import Image
import base64
from io import BytesIO

terrain_data = (
        (0.00, (0.2, 0.2, 0.6)),
        (0.15, (0.0, 0.6, 1.0)),
        (0.25, (0.0, 0.8, 0.4)),
        (0.50, (1.0, 1.0, 0.6)),
        (0.75, (0.5, 0.36, 0.33)),
        (1.00, (1.0, 1.0, 1.0)))

#img = Image.new('RGBA', (len(terrain_data), 1))
#for ii, entry in enumerate(terrain_data):
#  img.putpixel((ii, 0), tuple(int(255*c + 0.5) for c in entry[1]))

def preview_svg(b64):
  return """<svg xmlns="http://www.w3.org/2000/svg" width="100%" height="100%"><image href="data:image/png;base64,{}" /></svg>""".format(b64)


def relief_colormap():
  otm_data = (
    (0   , ( 17, 120,   3)),
    (100 , ( 72, 162,  69)),
    (300 , (231, 218, 158)),
    (1500, (161,  67,   0)),
    (3000, (130,  30,  30)),
    (4000, (110, 110, 110)),
    (5000, (255, 255, 255)),
    (6000, (255, 255, 255)))

  img = Image.new('RGBA', (51, 1))

  jj = 0
  for ii in range(0, 51):
    h = ii*100
    if h > otm_data[jj+1][0]:
      jj += 1
    c0, c1 = otm_data[jj][1], otm_data[jj+1][1]
    frac = (h - otm_data[jj][0])/(otm_data[jj+1][0] - otm_data[jj][0])
    img.putpixel((ii, 0), tuple(int((1 - frac)*c0[k] + frac*c1[k] + 0.5) for k in range(3)))

  buffered = BytesIO()
  img.save(buffered, format="PNG")
  print(base64.b64encode(buffered.getvalue()))


def angle_colormap():
  otm_data = (
    (0  , ( 0,  255,   0)),
    (25 , (255, 255,   0)),
    (35 , (255,   0,   0)),
    (45 , (255,   0, 255)),
    (60 , (  0,   0, 255)),
    (90 , (  0,   0,   0)))

  w = 45
  img = Image.new('RGBA', (w, 1))

  jj = 0
  for ii in range(0, w):
    h = ii*2.0
    if h > otm_data[jj+1][0]:
      jj += 1
    c0, c1 = otm_data[jj][1], otm_data[jj+1][1]
    frac = (h - otm_data[jj][0])/(otm_data[jj+1][0] - otm_data[jj][0])
    img.putpixel((ii, 0), tuple(int((1 - frac)*c0[k] + frac*c1[k] + 0.5) for k in range(3)))

  buffered = BytesIO()
  img.save(buffered, format="PNG")
  print(base64.b64encode(buffered.getvalue()))


angle_colormap()
