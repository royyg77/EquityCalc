# EquityCalc

A fast poker equity calculator. C++ engine, Python (FastAPI) HTTP layer, React frontend.

## Status

v0.0 — skeleton only. See [CLAUDE.md](./CLAUDE.md) for current state and roadmap.

## Quick start

Requires: C++20 compiler, CMake 3.20+, Python 3.10+, Node 18+, Docker (optional).

```bash
# Build engine
cd engine
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cli/equitycalc --help

# Run backend
cd ../backend
pip install -e .
uvicorn app.main:app --reload

# Run frontend
cd ../frontend
npm install
npm run dev
```

Or run everything via Docker:

```bash
docker compose up
```

## Documentation

- [CLAUDE.md](./CLAUDE.md) — architecture, build commands, conventions
- `engine/README.md` — engine internals (TODO)
- `backend/README.md` — API reference (TODO)
- `frontend/README.md` — UI conventions (TODO)

## License

TBD.
