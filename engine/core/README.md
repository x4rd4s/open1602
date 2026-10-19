# engine/core

Basic building blocks without game specifics. Depends on nothing else in the engine; zlib is linked
privately when the build has it.

| Header | Contents |
|---|---|
| `result.hpp` | `Result<T>` and `Error`, the error handling of every loader |
| `byte_reader.hpp`, `byte_writer.hpp` | Bounds-checked little-endian reading and writing of byte buffers |
| `file.hpp` | Reading files, case-insensitive path resolution, atomic writes, UTF-8 paths, environment variables |
| `text.hpp` | Windows-1252 to UTF-8 and back (the original's texts and fonts are Windows-1252) |
| `numeric.hpp` | Doubles from data files as integers, the same on every platform (`saturating_cast`) |
| `compress.hpp` | zlib streams (when the build has zlib) and CRC-32 for Open1602's own files |
| `png_writer.hpp` | PNG output for screenshots and the tools |
| `embedded_file.hpp` | Files compiled into the program (`cmake/EmbedFiles.cmake`) |

The deterministic random numbers and the entity pools of the simulation live in `engine/sim`
(`random.hpp`, `pool.hpp`).
