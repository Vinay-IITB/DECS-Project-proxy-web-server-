## 📄 Project Description

This project is a **multi-threaded HTTP Proxy Server** written in C that integrates an **LRU (Least Recently Used) Cache** for efficient web content retrieval.  
It intercepts client HTTP requests, forwards them to the target web server, caches the responses, and serves repeated requests directly from the cache to minimize latency and bandwidth usage.

The proxy uses a modular design:
- `proxy_server_with_LRU_cache.c` handles client connections, caching, and threading.
- `proxy_parse.c` and `proxy_parse.h` provide robust HTTP request parsing functionality.

The system supports multiple concurrent clients using POSIX threads (`pthread`) and ensures synchronization and cache consistency through proper locking mechanisms.  
It serves as a foundational implementation for understanding **web proxies**, **request parsing**, **multi-threaded networking**, and **cache management** in C.
