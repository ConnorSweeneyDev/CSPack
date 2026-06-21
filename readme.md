# CSPack
A single-header library defining the csp format: a packed, memory-mappable container of blobs.

A csp file is a 32-byte header followed by a 16-byte-aligned content region. One header is the single source of truth
for the layout and is shared by both sides: the writer packs blobs into a file at build-time, and the mapper memory-maps
that file at run-time and points spans straight at the bytes, with no parsing or allocation per blob.

## Features
- Single header, depending only on the C++20 standard library and the OS mapping API.
- One definition of the format, shared by the writer and the mapper so they cannot drift.
- Copy-on-write mapping; reading pages in on demand stays zero-copy, so mapping is cheap regardless of file size; any
  spans the application resolves in place at mount become private edits that never touch the file on disk.
- A CRC32 fingerprint guards against corruption and an FNV-1a signature ties a file to the build that expects it.

## Requirements
- Windows or Linux OS.
- A C++20 compiler.

## Usage
### Writer (Build-Time)
Append blobs in order, then serialise. `table` records the `(offset, size)` each blob landed at so the build can emit
matching patches for the mapper.
```cpp
csp::pack pack;
pack.append(first_bytes);
pack.append(second_bytes);
csp::write(pack, "Data.csp");
```

### Mapper (Run-Time)
Declare a manifest; it owns the patches by value and each one points a span at a region of the file. Constructing the
manifest registers it, then `mount()` maps the file, validates the header, and fills in every span. `directory` is the
folder the file lives in.
```cpp
std::span<const unsigned char> first;
std::span<const unsigned char> second;

namespace csp
{
  const manifest instance{"Data.csp", 6125984697962060194ull,
                          std::array<patch, 2>{{
                            {&first, 32, 2036},
                            {&second, 2080, 500000},
                          }}};
}

// At startup, before anything reads the spans, to make them point straight at their regions of the file.
csp::mount(directory);
```
