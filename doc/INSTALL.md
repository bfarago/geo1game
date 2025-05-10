

# Installation Guide

This project is currently in a **test/experimental phase** and not yet intended for production deployment. Use at your own risk and feel free to contribute or report issues via GitHub.

---

## 📦 Installation (User)

To run the system in a basic test environment:

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/geo.git
   cd geo/src
   ```

2. **Install Python dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

3. **Run the test server:**
   ```bash
   python3 geoapi.py
   ```

4. **Access via browser:**
   The API should be accessible at `http://localhost:8000/geoapi/` (assuming reverse proxy is configured).

---

## 🧑‍💻 Developer Setup

If you want to contribute to the project or run a full development environment:

### Prerequisites

Install the following tools:

- **Python 3.10+**
- **SQLite3** (for local database)
- **Apache2** or other (if you wish to use reverse proxy locally)
- **CMake** (for native C module `libmapgen`)
- **GCC** or Clang (C compiler)
- **Make**
- **Git**
- **PlantUML** (optional, for documents, diagrams)
- **VSCode** or any modern IDE with Python and C support

### Setup Instructions

1. **Create virtual environment (optional but recommended):**
   ```bash
   python3 -m venv venv
   source venv/bin/activate
   ```

2. **Install Python dependencies:**
   ```bash
   pip install -r requirements.txt
   ```

3. **Build native C module (libmapgen):**
   ```bash
   cd mapgen
   make
   cd ..
   ```

4. **Run test server:**
   ```bash
   python3 geoapi.py
   ```

5. **(Optional)**: Run with reverse proxy (Apache/Nginx) for full simulation.

---
## Unit-test
Ceedling requires Ruby 3.0 or newer.
My local machine example is a macOs one :
```bash
brew install ruby      # macOS
echo 'export PATH="/opt/homebrew/opt/ruby/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
gem install ceedling --user-install
export PATH="$HOME/.gem/ruby/3.4.0/bin:$PATH"  # move this to ~/.zshrc vagy ~/.bash_profile

brew install json-c
```

Remote machine / linux server:

   ```bash
sudo apt install ruby  # Debian/Ubuntu
gem install ceedling

   ```

## 🛠️ Notes

- This project is under active development and subject to change.
- Contributions are welcome! Please submit PRs against the `dev` branch.

---