# svf-mcp-server

## Быстрый старт

### Сборка

```bash
mkdir build && cd build
cmake .. -DLLVM_DIR=/path/to/llvm -DSVF_DIR=/path/to/svf
cmake --build .
```

### Подключение к OpenCode

```json
# Создайте opencode.json в корне анализируемого проекта.
{
  "mcp": {
    "svf": {
      "type": "local",
      "command": ["/path/to/svf-mcp-server"]
    }
  }
}
```
