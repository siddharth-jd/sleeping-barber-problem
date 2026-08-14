# Sleeping Barber Problem (Linux Kernel & Web Simulation)

[![Language: C](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Kernel: Linux](https://img.shields.io/badge/Kernel-Linux-orange.svg)](https://www.kernel.org/)
[![Backend: Flask](https://img.shields.io/badge/Backend-Flask-green.svg)](https://flask.palletsprojects.com/)
[![License: GPL--2.0](https://img.shields.io/badge/License-GPL--2.0-red.svg)](https://www.gnu.org/licenses/gpl-2.0.html)

A full-stack, kernel-level implementation and interactive visual simulation of Dijkstra's classic **Sleeping Barber Problem** for operating systems and process synchronization. 

Instead of standard user-space `pthread` POSIX threads, this project implements the core barber shop synchronization engine directly inside the **Linux Kernel** using **Kernel Threads (`kthread`)**, **Semaphores**, **Wait Queues**, and atomic variables. The kernel status is exposed to user space via the `/proc` filesystem and rendered on a real-time **Flask Web Dashboard**.

---

## 📐 System Architecture

```
 ┌───────────────────────────────────────────────────────────────┐
 │                   Web Dashboard (Frontend)                    │
 │               HTML5 / CSS3 / Vanilla JavaScript               │
 └───────────────────────────────┬───────────────────────────────┘
                                 │ HTTP Polling / REST API
 ┌───────────────────────────────▼───────────────────────────────┐
 │                     Flask Backend Server                      │
 │                  sleeping_barber_server.py                    │
 └───────────────────────────────┬───────────────────────────────┘
                                 │ Read / Write File I/O
 ┌───────────────────────────────▼───────────────────────────────┐
 │             ProcFS Interface: /proc/sleeping_barber_cop       │
 └───────────────────────────────┬───────────────────────────────┘
                                 │ Low-Level Kernel Driver
 ┌───────────────────────────────▼───────────────────────────────┐
 │                 Linux Kernel Module (LKM)                     │
 │                     sleeping_barber_cop.c                     │
 │  ├── Barber Thread (kthread)                                  │
 │  ├── Customer Generator Thread (kthread)                      │
 │  ├── FIFO Customer Queue (struct list_head)                   │
 │  └── Synchronization (Semaphores, Wait Queues & Mutexes)      │
 └───────────────────────────────────────────────────────────────┘
```

---

## ✨ Features

- **Kernel-Level Multithreading**: Executes synchronization logic using native Linux Kernel Threads (`kthread`).
- **Starvation-Free Queueing**: Implements a FIFO queue via kernel linked lists (`struct list_head`) to ensure customer fairness.
- **Hardware-Safe Atomicity**: Uses kernel atomic variables (`atomic_t`) and semaphores for thread-safe state management.
- **Virtual Filesystem Integration**: Custom `/proc` entry at `/proc/sleeping_barber_cop` for reading metrics and writing runtime commands.
- **Real-Time Visual Monitoring**: Interactive web dashboard showing barber state, waiting room occupancy, served customer counter, and lost customer counter.
- **Performance Analytics**: Integrated Python benchmarking script (`barber_performance_analysis.py`) to parse kernel event logs and plot throughput and drop rates.

---

## 🛠️ Tech Stack & Prerequisites

### Prerequisites
- **Operating System**: Linux (Ubuntu, Debian, Fedora, Arch, etc.)
- **Kernel Headers**: `linux-headers-$(uname -r)`
- **Compiler**: `gcc`, `make`
- **Python**: Python 3.8+ with `flask`, `matplotlib`, `seaborn`

---

## 📂 Repository Structure

```
sleeping-barber-problem/
├── kernel_cop/                      # Linux Kernel Module Source
│   ├── sleeping_barber_cop.c        # Main C kernel module code
│   ├── Makefile                     # Kbuild build script
│   ├── barber_performance_analysis.py # Log parser & metrics visualization script
│   ├── barber_log.txt               # Sample performance log
│   └── templates/
│       └── index.html               # Embedded HTML monitoring page
├── backend/                         # Flask Backend Application
│   ├── sleeping_barber_server.py    # Flask server interfacing with ProcFS
│   └── static/                      # Web frontend assets
│       ├── sleeping_barber_page.html# Visual dashboard UI
│       └── style.css                # Styling stylesheet
├── .gitignore                       # Git ignore configuration
└── README.md                        # Project documentation
```

---

## 🚀 Getting Started

### 1. Build and Load the Kernel Module

Navigate to the `kernel_cop/` directory, compile the Kernel Module using `make`, and insert it into the kernel:

```bash
cd kernel_cop

# Compile the module
make

# Insert the module into the Linux Kernel (requires root privileges)
sudo insmod sleeping_barber_cop.ko
```

Verify that the `/proc` virtual file was created successfully:

```bash
cat /proc/sleeping_barber_cop
```

### 2. Launch the Flask Web Server

Navigate to the `backend/` directory, install Flask dependencies, and start the web server:

```bash
cd ../backend

# Install dependencies
pip install flask

# Run the Flask server
python3 sleeping_barber_server.py
```

Open your browser and navigate to:
```
http://127.0.0.1:5000/
```

### 3. Running Performance Analytics

To parse the kernel logs and generate performance charts:

```bash
cd kernel_cop
python3 barber_performance_analysis.py
```

### 4. Unloading the Kernel Module

When finished, stop the Flask server and unload the kernel module:

```bash
sudo rmmod sleeping_barber_cop
```

---

## 🧠 Synchronization Logic

The problem is solved in `sleeping_barber_cop.c` using the following kernel primitives:

| Component | Kernel Primitive | Purpose |
| :--- | :--- | :--- |
| **Shared Mutex** | `struct semaphore mutex_sem` | Protects access to the waiting customer queue and counters. |
| **Barber Wait Queue** | `wait_queue_head_t barber_wq` | Puts the barber thread to sleep when no customers are present. |
| **Customer FIFO Queue** | `struct list_head waiting_customers` | Prevents customer starvation by storing arrival order. |
| **Atomic Customer Flag** | `atomic_t customer_waiting` | Signals the barber thread to wake up when a customer arrives. |

---

## 📄 License

This project is released under the **GPL-2.0** License.
