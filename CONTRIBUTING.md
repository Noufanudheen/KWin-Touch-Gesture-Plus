# Contributing to KWin Touch Gesture Plus

Thank you for your interest in contributing! Whether you are reporting a bug, proposing a new feature, or submitting code improvements, your contributions are welcome.

---

## 1. Code of Conduct & Development Principles

- **Preserve Touch Correctness**: Ordinary application taps and edge interactions must never be sacrificed for gesture speed.
- **Security-First**: Do not execute arbitrary shell commands directly with string concatenation. Use `QProcess` with discrete argument vectors.
- **Fail-Safe & Isolated**: A failure in an action or configuration file must never crash or hang the KWin compositor.
- **Upstream Compatibility**: Code written for the core recognizer must adhere strictly to KDE KWin coding guidelines and C++20 standards.

---

## 2. Building and Testing

### Prerequisites
- KDE Plasma 6 development libraries (`kwin-devel` / `kwin`, `qt6-base`, `extra-cmake-modules`)
- CMake >= 3.20
- GCC or Clang with C++20 support

### Build Instructions
```bash
git clone https://github.com/Noufanudheen/KWin-Touch-Gesture-Plus.git
cd KWin-Touch-Gesture-Plus

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build .
```

### Running Tests
Before submitting a pull request, ensure all unit tests pass:
```bash
ctest --output-on-failure
```

---

## 3. Submitting Pull Requests

1. Fork the repository and create a descriptive feature branch:
   ```bash
   git checkout -b feature/custom-action-provider
   ```
2. Commit your changes with clear, semantic commit messages:
   ```
   plugin: add support for custom sound effect triggers on gesture completion
   ```
3. Update relevant documentation in `docs/` and add test cases in `tests/` for new logic.
4. Open a Pull Request on GitHub against the `main` branch.
