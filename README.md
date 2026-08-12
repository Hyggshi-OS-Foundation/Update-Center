# UpdateCenter

A graphical Linux system update manager for Debian/Ubuntu-based distributions, providing a polished GUI front-end for `apt`.

## Features

- **System Updates**: Check for, browse, and install available package updates
- **Update History**: View past transactions parsed from real apt/dpkg logs
- **Repository Manager**: Add third-party repos with GPG key fingerprint verification
- **Dark/Light Themes**: Switch between visual themes
- **Bilingual**: English and Vietnamese language support
- **Security**: Never runs as root — individual commands are elevated via `pkexec` (Polkit)

## Prerequisites

- CMake 3.16+
- Qt 6 (Widgets + Network)
- C++17 compiler
- Debian/Ubuntu-based Linux with `apt` and `pkexec`

## Build & Run

```bash
mkdir build && cd build
cmake ..
make
./UpdateCenter
```

## Project Structure

```
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # Entry point
│   ├── mainwindow.cpp/h      # Core UI (sidebar, pages, animations)
│   ├── aptmanager.cpp/h      # APT backend (refresh, list, install)
│   ├── repositorymanager.cpp/h # Repo management with GPG verification
│   ├── historymanager.cpp/h  # Parses /var/log/apt/history.log
│   ├── updateitem.h          # Update item data struct
│   └── lang.cpp/h            # EN/VI translation system
└── resources/
    ├── resources.qrc         # Qt resource bundle
    ├── style.qss             # Light theme
    └── style_dark.qss        # Dark theme
```

## Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C++17 |
| GUI | Qt 6 (Widgets + Network) |
| Build | CMake |
| Package Backend | `apt-get` / `apt-cache` / `dpkg` |
| Privilege Elevation | `pkexec` (PolicyKit) |
| Styling | Qt Style Sheets (QSS) |
