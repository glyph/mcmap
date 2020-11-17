#ifndef CANVAS_H_
#define CANVAS_H_

#include "./helper.h"
#include "./png.h"
#include "./section.h"
#include "./worldloader.h"
#include <filesystem>

#define CHANSPERPIXEL 4
#define BYTESPERCHAN 1
#define BYTESPERPIXEL 4

struct Beam {
  uint8_t position;
  const Colors::Block *color;

  Beam() : position(0), color(nullptr){};
  Beam(uint8_t x, uint8_t z, const Colors::Block *c)
      : position((x << 4) + z), color(c){};

  inline uint8_t x() const { return position >> 4; }
  inline uint8_t z() const { return position & 0x0f; }

  inline bool column(uint8_t x, uint8_t z) const {
    return position == ((x << 4) + z);
  }

  Beam &operator=(Beam &&other) {
    position = other.position;
    color = other.color;
    return *this;
  }
};

// Canvas
// Common features of both canvas types.
struct Canvas {
  Terrain::Coordinates map; // The coordinates describing the 3D map
  uint32_t width, height;   // Bitmap width and height

  virtual size_t getLine(uint8_t *buffer, size_t size, uint64_t line) const {
    switch (type) {
    case BYTES:
      return _get_line(&drawing.bytes_buffer->operator[](0), buffer, size,
                       line);
    case IMAGE:
      return _get_line(drawing.image_buffer, buffer, size, line);

    default:
      return 0;
    }
  }

  size_t _get_line(const uint8_t *, uint8_t *, size_t, uint64_t) const;
  size_t _get_line(PNG::PNGReader *, uint8_t *, size_t, uint64_t) const;

  bool save(const std::filesystem::path, uint8_t) const;

  virtual std::string to_string() const {
    return fmt::format("Canvas with type {}\n", type);
  };

  enum BufferType { BYTES, CANVAS, IMAGE, EMPTY };

  union DrawingBuffer {
    long null_buffer;
    std::vector<uint8_t> *bytes_buffer;
    // std::vector<Canvas> *canvas_buffer;
    PNG::PNGReader *image_buffer;

    DrawingBuffer() : null_buffer(0) {}
    // drawing_buffer(std::vector<Canvas> fragments) :
    // canvas_buffer(fragments)
    // {}
    DrawingBuffer(std::filesystem::path file) {
      image_buffer = new PNG::PNGReader(file);
    }

    DrawingBuffer(BufferType type) {
      switch (type) {
      case BYTES: {
        bytes_buffer = new std::vector<uint8_t>();
        break;
      }

      case IMAGE:
        logger::error("Default constructing image canvas not supported\n");

      default: {
        null_buffer = long(0);
      }
      }
    }

    void destroy(BufferType type) {
      switch (type) {
      case BYTES: {
        if (bytes_buffer)
          delete bytes_buffer;
        break;
      }

      case IMAGE: {
        if (image_buffer)
          delete image_buffer;
        break;
      }

      default:
        break;
      }
    }
  };

  BufferType type;
  DrawingBuffer drawing;

  Canvas() : drawing() {}
  Canvas(BufferType _type) : type(_type), drawing(_type) {}
  Canvas(const std::filesystem::path &file) : type(IMAGE), drawing(file) {
    width = drawing.image_buffer->get_width();
    height = drawing.image_buffer->get_height();
  };

  Canvas &operator=(Canvas &&other) {
    map = other.map;
    width = other.width;
    height = other.height;

    switch (other.type) {
    case BYTES: {
      drawing.bytes_buffer = std::move(other.drawing.bytes_buffer);
      other.drawing.bytes_buffer = nullptr;
      break;
    }

    case IMAGE: {
      drawing.image_buffer = std::move(other.drawing.image_buffer);
      other.drawing.image_buffer = nullptr;
      break;
    }
    default:
      drawing.null_buffer = long(0);
    }

    return *this;
  }

  ~Canvas() { drawing.destroy(type); }
};

struct ImageCanvas : Canvas {
  const std::filesystem::path file;

  ImageCanvas(const std::filesystem::path &file) : Canvas(file) {}
};

// Isometric canvas
// This structure holds the final bitmap data, a 2D array of pixels. It is
// created with a set of 3D coordinates, and translate every block drawn
// into a 2D position.
struct IsometricCanvas : Canvas {
  bool shading;

  uint32_t sizeX, sizeZ;    // The size of the 3D map
  uint8_t offsetX, offsetZ; // Offset of the first block in the first chunk

  Colors::Palette palette; // The colors to use when drawing
  Colors::Block air,
      water,      // fire, earth. Teh four nations lived in harmoiny
      beaconBeam; // Cached colors for easy access

  // TODO bye bye
  uint8_t totalMarkers = 0;
  Colors::Marker (*markers)[256];

  float *brightnessLookup;

  Section sections[16];

  // In-chunk variables
  uint32_t chunkX;
  uint32_t chunkZ;
  int8_t yPos, minSection, maxSection;

  // Beams in the chunk being rendered
  uint8_t beamNo = 0;
  Beam beams[256];

  uint8_t orientedX, orientedZ, y;

  IsometricCanvas() : Canvas(BYTES) {}

  ~IsometricCanvas() { // delete[] bytesBuffer;
  }

  std::string to_string() const override;

  void setColors(const Colors::Palette &);
  void setMap(const Terrain::Coordinates &);
  void setMarkers(uint8_t n, Colors::Marker (*array)[256]) {
    totalMarkers = n;
    markers = array;
  }

  // Drawing methods
  // Helpers for position lookup
  void orientChunk(int32_t &x, int32_t &z);
  void orientSection(uint8_t &x, uint8_t &z);
  inline uint8_t *pixel(uint32_t x, uint32_t y) {
    return &(*drawing.bytes_buffer)[(x + y * width) * BYTESPERPIXEL];
  }

  // Drawing entrypoints
  void renderTerrain(Terrain::Data &);
  void renderChunk(Terrain::Data &);
  void renderSection();
  // Draw a block from virtual coords in the canvas
  void renderBlock(const Colors::Block *, const uint32_t, const uint32_t,
                   const uint32_t, const NBT &metadata);

  // Empty section with only beams
  void renderBeamSection(const int64_t, const int64_t, const uint8_t);

  const Colors::Block *nextBlock();
};

struct CompositeCanvas : public Canvas {
  // A sparse canvas made with smaller canvasses
  //
  // To render multiple canvasses made by threads, we compose an image from
  // them directly. This object allows to do so. It is given a list of
  // canvasses, and can be read as an image (made out of lines, with a
  // height and width) that is composed of the canvasses, without actually
  // using any more memory.
  //
  // This is done by keeping track of the offset of each sub-canvas from the
  // top left of the image. When reading a line, it is composed of the lines
  // of each sub-canvas, with the appropriate offset.
  //
  // +-------------------+
  // |Composite Canvas   |
  // |+------------+     |
  // ||Canvas 1    |     |
  // ||    +------------+|
  // ||    |Canvas 2    ||
  // ||====|============|| < Read line
  // ||    |            ||
  // |+----|            ||
  // |     |            ||
  // |     +------------+|
  // +-------------------+

  struct Position {
    // Struct holding metadata about where the subCanvas is to be drawn.
    int64_t offsetX, offsetY; // Offsets to draw the image
    const Canvas *subCanvas;  // Canvas to draw
  };

  std::vector<Position>
      subCanvasses; // Sorted list of Positions to draw the final image

  CompositeCanvas(const std::vector<Canvas> &);

  size_t getLine(uint8_t *, size_t, uint64_t) const override;

  std::string to_string() const override;
};

#endif
